package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.TileBehavior
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.net.game.packets.EntityInteractPacket
import de.fiereu.openmmo.net.game.packets.TileInteractPacket
import de.fiereu.openmmo.server.game.script.Script
import de.fiereu.openmmo.server.game.script.ScriptRegistry
import de.fiereu.openmmo.server.game.script.ScriptRunner
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.PlayerState
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.StoredCharacter
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

@Singleton
class InteractionService
@Inject
constructor(
    private val npcService: NpcService,
    private val mapManager: MapManager,
    private val characterStore: CharacterStore,
    private val scriptRegistry: ScriptRegistry,
    private val scriptRunner: ScriptRunner,
    private val trainerSight: TrainerSightService,
    private val violations: ViolationLog,
) {

  /** The player pressed the action button on a specific entity, that is an npc. */
  fun onEntityInteract(event: PacketEvent<EntityInteractPacket>) {
    val session = event.session
    val state = session.attributes[PLAYER_STATE] ?: return
    if (state.inDialog) return
    val stored = currentCharacter(state) ?: return
    val npcEntityId = event.packet.entityId

    val currentMap =
        mapManager.getMap(
            stored.info.positionRegionId,
            stored.info.positionBankId,
            stored.info.positionMapId,
        ) ?: return
    val regionId = stored.info.positionRegionId.toInt()
    val bankId = stored.info.positionBankId.toInt()
    val mapId = stored.info.positionMapId.toInt()

    for (npc in currentMap.npcs) {
      if (npcService.getNpcEntityId(regionId, bankId, mapId, npc.entityIdx) == npcEntityId) {
        // Its sibling onTileInteract never asks this, because it works out who is in front of the
        // player from a position this server holds. This one is handed a name, and took any name
        // on the map: the ids are not secret, so one walk up to a clerk reopened that mart from
        // anywhere for the rest of the session. The reach is the data's own: the person's tile
        // plus the box their movement range names, plus the tile you talk across.
        val reachX = 1 + npc.movementRangeX
        val reachY = 1 + npc.movementRangeY
        val playerX = stored.info.positionX.toInt()
        val playerY = stored.info.positionY.toInt()
        if (kotlin.math.abs(playerX - npc.x) > reachX ||
            kotlin.math.abs(playerY - npc.y) > reachY) {
          violations.record(
              state.characterId,
              ViolationLog.Kind.OUT_OF_REACH,
              "presses A on entity $npcEntityId at (${npc.x}, ${npc.y}) from" +
                  " ($playerX, $playerY), too far to talk across")
          return
        }
        val script = scriptRegistry.forMap(regionId, bankId, mapId, npc.script)
        if (script != null) {
          runScript(session, state, script, npcEntityId)
        } else {
          log.info { "NPC entityIdx=${npc.entityIdx} script=${npc.script} has no wired dialog" }
        }
        return
      }
    }
    log.info { "Entity interaction for entity $npcEntityId not found on current map" }
  }

  /**
   * The player pressed the action button on the tile they face: a person, a sign, or a piece of
   * furniture.
   */
  fun onTileInteract(event: PacketEvent<TileInteractPacket>) {
    val session = event.session
    val state = session.attributes[PLAYER_STATE] ?: return
    if (state.inDialog) return
    val stored = currentCharacter(state) ?: return

    val currentMap =
        mapManager.getMap(
            stored.info.positionRegionId,
            stored.info.positionBankId,
            stored.info.positionMapId,
        ) ?: return

    val facingX =
        when (state.facingDirection) {
          Direction.RIGHT -> stored.info.positionX.toInt() + 1
          Direction.LEFT -> stored.info.positionX.toInt() - 1
          else -> stored.info.positionX.toInt()
        }
    val facingY =
        when (state.facingDirection) {
          Direction.UP -> stored.info.positionY.toInt() - 1
          Direction.DOWN -> stored.info.positionY.toInt() + 1
          else -> stored.info.positionY.toInt()
        }
    val regionId = stored.info.positionRegionId.toInt()
    val bankId = stored.info.positionBankId.toInt()
    val mapId = stored.info.positionMapId.toInt()

    val npc = npcService.visibleNpcAt(session, regionId, bankId, mapId, facingX, facingY)
    if (npc != null) {
      // A trainer the table owns has no script to look up: its object carries the trainer id in the
      // place a script id would go, so the fight is the table's and talking to it starts the fight.
      if (npc.trainerId != 0 && trainerSight.challengeOnInteract(session, state, npc)) return
      val script = scriptRegistry.forMap(regionId, bankId, mapId, npc.script)
      if (script == null) {
        log.info {
          "NO SCRIPT: npc ${npc.entityIdx} on $regionId:$bankId:$mapId faces the player and its " +
              "script ${npc.script} is not ported"
        }
        return
      }
      runScript(
          session, state, script, npcService.entityIdFor(regionId, bankId, mapId, npc.entityIdx))
      return
    }

    val bgEvent =
        MapEventLayout.bgEvents(currentMap, stored.storyFlags).find {
          it.x == facingX && it.y == facingY && facingDirOk(it.facingDir, state.facingDirection)
        }
    if (bgEvent != null) {
      val script = scriptRegistry.forMap(regionId, bankId, mapId, bgEvent.script)
      if (script != null) {
        runScript(session, state, script, entityId = -1)
      } else {
        log.info { "Bg event at ($facingX, $facingY) script=${bgEvent.script} has no wired dialog" }
      }
      return
    }

    val fieldMove =
        fieldMoveScriptId(
            currentMap.tileAt(facingX, facingY)?.behavior, state.facingDirection, state.isSurfing)
    if (fieldMove != null) {
      val script = scriptRegistry.forMap(regionId, bankId, mapId, fieldMove)
      if (script != null) {
        runScript(session, state, script, entityId = -1)
        return
      }
      log.info {
        "Field-move tile at ($facingX, $facingY) wants script $fieldMove, which is unwired"
      }
      return
    }

    log.debug { "Tile interaction at ($facingX, $facingY) has no bg event" }
  }

  /** The shared field-move script an HM tile asks for, or null when the tile is only scenery. */
  private fun fieldMoveScriptId(
      behavior: TileBehavior?,
      facing: Direction,
      surfing: Boolean,
  ): String? =
      when {
        behavior == null -> null
        behavior == TileBehavior.WATERFALL -> FIELD_MOVE_WATERFALL
        behavior.rockClimbAllows(facing) -> FIELD_MOVE_ROCK_CLIMB
        behavior == TileBehavior.SURFABLE_WATER && !surfing -> FIELD_MOVE_SURF
        else -> null
      }

  private fun currentCharacter(state: PlayerState): StoredCharacter? {
    val charId = state.characterId ?: return null
    return characterStore.getCharacter(charId)
  }

  private fun facingDirOk(eventDir: String, facing: Direction): Boolean =
      eventDir == "BG_EVENT_PLAYER_FACING_ANY" ||
          eventDir == "BG_EVENT_DIR_ALL" ||
          ((eventDir == "BG_EVENT_PLAYER_FACING_NORTH" || eventDir == "BG_EVENT_DIR_NORTH") &&
              facing == Direction.UP) ||
          ((eventDir == "BG_EVENT_PLAYER_FACING_SOUTH" || eventDir == "BG_EVENT_DIR_SOUTH") &&
              facing == Direction.DOWN) ||
          ((eventDir == "BG_EVENT_PLAYER_FACING_WEST" || eventDir == "BG_EVENT_DIR_WEST") &&
              facing == Direction.LEFT) ||
          ((eventDir == "BG_EVENT_PLAYER_FACING_EAST" || eventDir == "BG_EVENT_DIR_EAST") &&
              facing == Direction.RIGHT)

  private fun runScript(
      session: SessionContext,
      state: PlayerState,
      script: Script,
      entityId: Long,
  ) = scriptRunner.run(session, state, script, entityId)

  private companion object {
    // The FIELD_MOVES chunk's own entry order, the ids Field_TileBehaviorToScript returns.
    const val FIELD_MOVE_ROCK_CLIMB = "10003"
    const val FIELD_MOVE_SURF = "10004"
    const val FIELD_MOVE_WATERFALL = "10006"
  }
}
