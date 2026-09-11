package de.fiereu.openmmo.server.game.services

import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.maps.MapDef
import de.fiereu.openmmo.maps.NpcDef
import de.fiereu.openmmo.server.game.battle.BattleResult
import de.fiereu.openmmo.server.game.script.MovementStep
import de.fiereu.openmmo.server.game.script.Script
import de.fiereu.openmmo.server.game.script.ScriptRunner
import de.fiereu.openmmo.server.game.session.CLIENT_RUNS_SCRIPTS
import de.fiereu.openmmo.server.game.session.PlayerState
import de.fiereu.openmmo.trainer.TrainerRegistry
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/**
 * A trainer noticing the player, which is the one field mechanic this server had neither half of.
 */
@Singleton
class TrainerSightService
@Inject
constructor(
    private val npcService: NpcService,
    private val trainers: TrainerRegistry,
    private val storyService: StoryService,
    private val scriptRunner: ScriptRunner,
) {

  /**
   * Called after a completed step. Starts the challenge if the player walked into somebody's line
   * of sight, and answers whether it did so the caller can hold back the encounter roll.
   */
  fun onStep(
      session: SessionContext,
      state: PlayerState,
      map: MapDef,
      x: Int,
      y: Int,
  ): Boolean {
    // A session that runs its own field scripts runs its own trainer sight: the engine spots
    // the trainer, fights the fight, and reports the outcome, the flags and the prize the same
    // way every other local play does.
    if (session.attributes[CLIENT_RUNS_SCRIPTS] == true) return false
    if (state.inDialog) return false
    val charId = state.characterId ?: return false
    val region = regionOf(map, state) ?: return false
    val seen = trainerInSight(session, map, x, y, charId, region) ?: return false

    log.info {
      "Trainer ${seen.npc.trainerId} on ${map.regionId}:${map.bankId}:${map.mapId} sees char " +
          "$charId from ${seen.distance} tile(s) away"
    }
    scriptRunner.run(
        session,
        state,
        challenge(seen),
        npcService.entityIdFor(
            map.regionId.toInt(), map.bankId.toInt(), map.mapId.toInt(), seen.npc.entityIdx),
    )
    return true
  }

  /** The player walked up and pressed A instead of being spotted. */
  fun challengeOnInteract(
      session: SessionContext,
      state: PlayerState,
      npc: NpcDef,
      map: MapDef,
  ): Boolean {
    // Same sentence as [onStep]: a client that runs scripts fights its own trainers, ported or not.
    if (session.attributes[CLIENT_RUNS_SCRIPTS] == true) return false
    val charId = state.characterId ?: return false
    val region = regionOf(map, state) ?: return false
    val def = trainers.get(region, npc.trainerId) ?: return false
    if (def.defeatedFlag.isNotEmpty() && storyService.isFlagSet(charId, def.defeatedFlag)) {
      log.info { "Trainer ${npc.trainerId} is already beaten and has no ported line to say" }
      return true
    }
    val seen =
        SeenBy(
            npc,
            distance = 1,
            facing = npc.facing,
            defeatedFlag = def.defeatedFlag,
            badge = def.badge,
            region = region)
    scriptRunner.run(
        session,
        state,
        Script { ctx ->
          if (ctx.trainerBattle(npc.trainerId, seen.region) == BattleResult.VICTORY) {
            if (seen.defeatedFlag.isNotEmpty()) ctx.setFlag(seen.defeatedFlag)
            if (seen.badge.isNotEmpty()) ctx.setFlag(seen.badge)
          }
        },
        npcService.entityIdFor(state.regionId, state.bankId, state.mapId, npc.entityIdx),
    )
    return true
  }

  /** The trainer walks up to the player, fights, and stays beaten. */
  private fun challenge(seen: SeenBy): Script = Script { ctx ->
    // One tile short of the player, then facing them: the engine's own approach.
    val approach = List(seen.distance - 1) { seen.toward } + seen.facePlayer
    ctx.moveNpc(seen.npc.entityIdx, *approach.toTypedArray())
    if (ctx.trainerBattle(seen.npc.trainerId, seen.region) == BattleResult.VICTORY) {
      if (seen.defeatedFlag.isNotEmpty()) ctx.setFlag(seen.defeatedFlag)
      // A gym leader's badge, which the source's own gym script hands over after the fight.
      if (seen.badge.isNotEmpty()) ctx.setFlag(seen.badge)
    }
  }

  /** Which region's trainer table this map's people are numbered in. */
  private fun regionOf(map: MapDef, state: PlayerState): Region? =
      if (map.ported) Region.JOHTO else Region.byWireValue(state.regionId.toByte())

  /** Whoever can see ([x], [y]) and has not been beaten, or null. */
  private fun trainerInSight(
      session: SessionContext,
      map: MapDef,
      x: Int,
      y: Int,
      charId: Long,
      region: Region,
  ): SeenBy? {
    for (npc in map.npcs) {
      if (npc.trainerId == 0 || npc.sightRange <= 0) continue
      val def = trainers.get(region, npc.trainerId) ?: continue
      if (def.defeatedFlag.isNotEmpty() && storyService.isFlagSet(charId, def.defeatedFlag))
          continue
      val visible =
          npcService.visibleNpcAt(
              session,
              map.regionId.toInt(),
              map.bankId.toInt(),
              map.mapId.toInt(),
              npc.x,
              npc.y,
          )
      // A hidden trainer sees nobody, and a story that moved one sees from where it is standing.
      if (visible?.entityIdx != npc.entityIdx) continue
      val lines =
          if (npc.trainerType == VIEW_ALL_DIRECTIONS) Direction.entries else listOf(npc.facing)
      for (facing in lines) {
        val distance = distanceAlong(map, npc, facing, x, y) ?: continue
        return SeenBy(npc, distance, facing, def.defeatedFlag, def.badge, region)
      }
    }
    return null
  }

  /**
   * How many tiles ahead of [npc] the player is standing when looking along [facing], or null when
   * they are not on that line, are out of range, or something walkable-blocking is between the two.
   * `GetDistanceNorth` and its three siblings, plus `IsPathInterrupted`.
   */
  private fun distanceAlong(
      map: MapDef,
      npc: NpcDef,
      facing: Direction,
      x: Int,
      y: Int,
  ): Int? {
    val dx = facing.dx
    val dy = facing.dy
    // A diagonal or a dive is not a line of sight.
    if (dx == 0 && dy == 0) return null
    for (step in 1..npc.sightRange) {
      val tileX = npc.x + dx * step
      val tileY = npc.y + dy * step
      if (map.tileAt(tileX, tileY)?.blocksMovement() == true) return null
      if (tileX == x && tileY == y) return step
    }
    return null
  }

  private data class SeenBy(
      val npc: NpcDef,
      val distance: Int,
      val facing: Direction,
      val defeatedFlag: String,
      /** The badge this trainer's defeat earns, or "", a ported gym leader's. */
      val badge: String,
      /** Whose table this trainer is numbered in, which a ported map's is not the player's. */
      val region: Region,
  ) {
    /** One step along the way it is looking. */
    val toward: MovementStep
      get() =
          when (facing) {
            Direction.UP -> MovementStep.WALK_UP
            Direction.DOWN -> MovementStep.WALK_DOWN
            Direction.LEFT -> MovementStep.WALK_LEFT
            else -> MovementStep.WALK_RIGHT
          }

    /** And a turn to look at the player, which is the way it was already looking. */
    val facePlayer: MovementStep
      get() =
          when (facing) {
            Direction.UP -> MovementStep.FACE_UP
            Direction.DOWN -> MovementStep.FACE_DOWN
            Direction.LEFT -> MovementStep.FACE_LEFT
            else -> MovementStep.FACE_RIGHT
          }
  }

  private companion object {
    /** `TRAINER_TYPE_VIEW_ALL_DIRECTIONS`, the only type that does not look along its facing. */
    const val VIEW_ALL_DIRECTIONS = 2
  }
}
