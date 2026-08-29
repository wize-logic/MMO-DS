package de.fiereu.openmmo.server.game.services

import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.DynamicWarp
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.maps.MapDef
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.net.game.packets.DialogDataPacket
import de.fiereu.openmmo.net.game.packets.NpcUpdatePacket
import de.fiereu.openmmo.server.game.script.MovementStep
import de.fiereu.openmmo.server.game.session.PlayerState
import de.fiereu.openmmo.server.game.storage.CharacterStore
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton
import kotlinx.coroutines.delay

private val log = KotlinLogging.logger {}

/**
 * Drives scripted overworld movement (the decomp applymovement/waitmovement). Steps play one
 * tile at a time with a short delay between them, so the call only returns once the whole path
 * is done, which is exactly waitmovement.
 */
@Singleton
class ScriptMovementService
@Inject
constructor(
    private val mapManager: MapManager,
    private val npcService: NpcService,
    private val characterStore: CharacterStore,
) {
  data class Pose(val x: Int, val y: Int, val facing: Direction)

  /** Walk a map npc (its decomp local id, that is its entityIdx) through [steps] for the player. */
  suspend fun moveNpc(
      session: SessionContext,
      state: PlayerState,
      localId: Int,
      steps: List<MovementStep>,
  ) {
    val charId = state.characterId ?: return
    val info = characterStore.getCharacter(charId)?.info ?: return
    val map =
        mapManager.getMap(info.positionRegionId, info.positionBankId, info.positionMapId) ?: return
    val npc = map.npcs.firstOrNull { it.entityIdx == localId } ?: return
    val entityId =
        npcService.entityIdFor(
            info.positionRegionId.toInt(),
            info.positionBankId.toInt(),
            info.positionMapId.toInt(),
            localId,
        )
    log.info {
      "MOVE npc local=$localId entity=$entityId from (${npc.x},${npc.y}) facing ${npc.facing}"
    }
    drive(session, entityId, Pose(npc.x, npc.y, npc.facing), steps)
  }

  /** Starts concurrent NPC movement paths. */
  suspend fun moveNpcs(
      session: SessionContext,
      state: PlayerState,
      paths: List<Pair<Int, List<MovementStep>>>,
  ) {
    val charId = state.characterId ?: return
    val info = characterStore.getCharacter(charId)?.info ?: return
    val map =
        mapManager.getMap(info.positionRegionId, info.positionBankId, info.positionMapId) ?: return
    val resolved =
        paths.mapNotNull { (localId, steps) ->
          if (map.npcs.none { it.entityIdx == localId }) return@mapNotNull null
          npcService.entityIdFor(
              info.positionRegionId.toInt(),
              info.positionBankId.toInt(),
              info.positionMapId.toInt(),
              localId,
          ) to steps
        }
    resolved.forEach { (entityId, steps) -> sendActions(session, entityId, steps) }
    delay(resolved.maxOfOrNull { (_, steps) -> durationMs(steps) } ?: 0)
  }

  /** Starts player and NPC movement together. */
  suspend fun moveSelfAndNpcs(
      session: SessionContext,
      state: PlayerState,
      selfSteps: List<MovementStep>,
      paths: List<Pair<Int, List<MovementStep>>>,
  ) {
    val charId = state.characterId ?: return
    val info = characterStore.getCharacter(charId)?.info ?: return
    val map =
        mapManager.getMap(info.positionRegionId, info.positionBankId, info.positionMapId) ?: return
    val resolved =
        paths.mapNotNull { (localId, steps) ->
          if (map.npcs.none { it.entityIdx == localId }) return@mapNotNull null
          npcService.entityIdFor(
              info.positionRegionId.toInt(),
              info.positionBankId.toInt(),
              info.positionMapId.toInt(),
              localId,
          ) to steps
        }
    sendActions(session, info.id, selfSteps)
    resolved.forEach { (entityId, steps) -> sendActions(session, entityId, steps) }
    delay(
        maxOf(
            durationMs(selfSteps),
            resolved.maxOfOrNull { (_, steps) -> durationMs(steps) } ?: 0,
        ))

    val start = Pose(info.positionX.toInt(), info.positionY.toInt(), state.facingDirection)
    commitPose(charId, state, map, applySteps(start, selfSteps))
  }

  /** Show a normally hidden map npc to the player for a cutscene (the decomp addobject). */
  fun showNpc(session: SessionContext, state: PlayerState, localId: Int) {
    val charId = state.characterId ?: return
    val info = characterStore.getCharacter(charId)?.info ?: return
    npcService.spawnNpc(
        session,
        info.positionRegionId.toInt(),
        info.positionBankId.toInt(),
        info.positionMapId.toInt(),
        localId,
    )
  }

  /** Show a normally hidden map npc at an overridden cutscene position. */
  fun showNpcAt(session: SessionContext, state: PlayerState, localId: Int, x: Int, y: Int) {
    val charId = state.characterId ?: return
    val info = characterStore.getCharacter(charId)?.info ?: return
    npcService.spawnNpcAt(
        session,
        info.positionRegionId.toInt(),
        info.positionBankId.toInt(),
        info.positionMapId.toInt(),
        localId,
        x,
        y,
    )
  }

  /**
   * Remove the npc a script was triggered by, the decomp's `RemoveObject VAR_LAST_TALKED`. The
   * script only has the entity id it was handed, so the map's own object list is walked back
   * to find whose it is.
   */
  fun removeNpcTalkedTo(session: SessionContext, state: PlayerState, entityId: Long) {
    val charId = state.characterId ?: return
    val info = characterStore.getCharacter(charId)?.info ?: return
    val region = info.positionRegionId.toInt()
    val bank = info.positionBankId.toInt()
    val map = info.positionMapId.toInt()
    val npc =
        mapManager.getMap(region, bank, map)?.npcs?.firstOrNull {
          npcService.getNpcEntityId(region, bank, map, it.entityIdx) == entityId
        } ?: return
    npcService.despawnNpc(session, region, bank, map, npc.entityIdx)
  }

  /** Relocate an npc that the normal map spawn already created. */
  fun repositionNpc(session: SessionContext, state: PlayerState, localId: Int, x: Int, y: Int) {
    val charId = state.characterId ?: return
    val info = characterStore.getCharacter(charId)?.info ?: return
    npcService.repositionNpc(
        session,
        info.positionRegionId.toInt(),
        info.positionBankId.toInt(),
        info.positionMapId.toInt(),
        localId,
        x,
        y,
    )
  }

  /** Repositions the player and synchronizes server state. */
  fun repositionSelf(
      session: SessionContext,
      state: PlayerState,
      x: Int,
      y: Int,
      facing: Direction,
  ) {
    val charId = state.characterId ?: return
    val info = characterStore.getCharacter(charId)?.info ?: return
    val map =
        mapManager.getMap(info.positionRegionId, info.positionBankId, info.positionMapId) ?: return
    if (!commitPose(charId, state, map, Pose(x, y, facing))) return
    session.send(
        NpcUpdatePacket(
            entityId = info.id,
            regionId = info.positionRegionId.toInt(),
            bankId = info.positionBankId.toInt(),
            mapId = info.positionMapId.toInt(),
            x = x,
            y = y,
            movementMode = 0xF6,
            heading = facing.ordinal,
        ))
  }

  /**
   * The decomp's SetHasPartner plus SetMovementType FOLLOW_PLAYER. The npc stays on the map and the
   * engine walks them; a hide flag still stops a second copy spawning when the player comes back.
   */
  fun setHasPartner(session: SessionContext, state: PlayerState, localId: Int) {
    val charId = state.characterId ?: return
    val info = characterStore.getCharacter(charId)?.info ?: return
    npcService.setHasPartner(
        session,
        info.positionRegionId.toInt(),
        info.positionBankId.toInt(),
        info.positionMapId.toInt(),
        localId,
    )
  }

  /** The decomp's ClearHasPartner. */
  fun clearHasPartner(session: SessionContext, state: PlayerState) {
    val charId = state.characterId ?: return
    val info = characterStore.getCharacter(charId)?.info ?: return
    npcService.clearHasPartner(
        session,
        info.positionRegionId.toInt(),
        info.positionBankId.toInt(),
        info.positionMapId.toInt(),
    )
  }

  /** Removes a cutscene NPC and its collision. */
  fun removeNpc(session: SessionContext, state: PlayerState, localId: Int) {
    val charId = state.characterId ?: return
    val info = characterStore.getCharacter(charId)?.info ?: return
    npcService.despawnNpc(
        session,
        info.positionRegionId.toInt(),
        info.positionBankId.toInt(),
        info.positionMapId.toInt(),
        localId,
    )
  }

  /** Resolves local NPC ids to entity ids. */
  fun npcEntityId(state: PlayerState, localId: Int): Long? {
    val charId = state.characterId ?: return null
    val info = characterStore.getCharacter(charId)?.info ?: return null
    return npcService.entityIdFor(
        info.positionRegionId.toInt(),
        info.positionBankId.toInt(),
        info.positionMapId.toInt(),
        localId,
    )
  }

  /** Returns the created player's gender. */
  fun playerGender(state: PlayerState): Byte? =
      state.characterId?.let(characterStore::getCharacter)?.info?.rivalSex

  /**
   * Set the destination a MAP_DYNAMIC warp resolves to for this player (the decomp setdynamicwarp).
   */
  fun setDynamicWarp(state: PlayerState, warp: DynamicWarp) {
    val charId = state.characterId ?: return
    characterStore.setDynamicWarp(charId, warp)
  }

  /** Walk the player's own avatar through [steps] and commit the final tile as authoritative. */
  suspend fun moveSelf(
      session: SessionContext,
      state: PlayerState,
      steps: List<MovementStep>,
  ) {
    val charId = state.characterId ?: return
    val info = characterStore.getCharacter(charId)?.info ?: return
    val map =
        mapManager.getMap(info.positionRegionId, info.positionBankId, info.positionMapId) ?: return
    val start = Pose(info.positionX.toInt(), info.positionY.toInt(), state.facingDirection)
    val end = drive(session, info.id, start, steps)
    commitPose(charId, state, map, end)
  }

  /** Sends one packet per step from [start] and waits between them. Returns the final pose. */
  suspend fun drive(
      session: SessionContext,
      entityId: Long,
      start: Pose,
      steps: List<MovementStep>,
  ): Pose {
    if (steps.isEmpty()) return start
    sendActions(session, entityId, steps)
    // waitmovement: hold until the client has had time to play the sequence.
    delay(durationMs(steps))
    return applySteps(start, steps)
  }

  /**
   * Commits where a cutscene left the player, unless that is off the map, which would persist a
   * position every later step reads as a desync and snaps back from.
   */
  private fun commitPose(charId: Long, state: PlayerState, map: MapDef, pose: Pose): Boolean {
    if (pose.x !in 0 until map.width || pose.y !in 0 until map.height) {
      log.warn {
        "Refused a scripted move of character $charId to (${pose.x}, ${pose.y}) on " +
            "${map.bankId}:${map.mapId}, which is outside the map"
      }
      return false
    }
    characterStore.updatePosition(charId, pose.x.toShort(), pose.y.toShort(), facing = pose.facing)
    state.x = pose.x.toShort()
    state.y = pose.y.toShort()
    state.facingDirection = pose.facing
    return true
  }

  private fun applySteps(start: Pose, steps: List<MovementStep>): Pose {
    var pose = start
    for (step in steps) {
      pose =
          if (step.walks)
              Pose(pose.x + step.direction.dx, pose.y + step.direction.dy, step.direction)
          else if (step.changesFacing) pose.copy(facing = step.direction) else pose
    }
    return pose
  }

  private fun sendActions(
      session: SessionContext,
      entityId: Long,
      steps: List<MovementStep>,
  ) {
    if (steps.isEmpty()) return
    log.info {
      "MOVE send entity=$entityId ${steps.size} step(s): " + steps.joinToString(",") { it.name }
    }
    // Send each movement sequence in one packet.
    val actions = ByteArray(steps.size) { steps[it].action.toByte() }
    session.send(DialogDataPacket(entityId, unk1 = 0, type = steps.size, data = actions))
  }

  private fun durationMs(steps: List<MovementStep>): Long =
      steps.sumOf { if (it.fast) FAST_STEP_MS else if (it.walks) WALK_STEP_MS else FACE_STEP_MS }

  private companion object {
    // Rough client step timings, tune if the animation and server drift apart.
    const val WALK_STEP_MS = 250L
    const val FAST_STEP_MS = 130L
    const val FACE_STEP_MS = 120L
  }
}
