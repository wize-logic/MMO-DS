package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.TileBehavior
import de.fiereu.openmmo.common.utils.isNdsRegion
import de.fiereu.openmmo.maps.MapDef
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.net.game.packets.EntityFaceTurnPacket
import de.fiereu.openmmo.net.game.packets.EntityMovePacket
import de.fiereu.openmmo.net.game.packets.FaceDirectionPacket
import de.fiereu.openmmo.net.game.packets.GbaEntityMovePacket
import de.fiereu.openmmo.net.game.packets.MapData
import de.fiereu.openmmo.net.game.packets.MovementPacket
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.PlayerState
import de.fiereu.openmmo.server.game.storage.CharacterStore
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/** Resolve a cardinal Gen-3 ledge hop to its tile two spaces away. */
internal fun ledgeLanding(
    map: MapDef,
    fromX: Int,
    fromY: Int,
    direction: Direction,
): Pair<Int, Int>? {
  val expectedBehavior =
      when (direction) {
        Direction.DOWN -> TileBehavior.JUMP_SOUTH
        Direction.UP -> TileBehavior.JUMP_NORTH
        Direction.LEFT -> TileBehavior.JUMP_WEST
        Direction.RIGHT -> TileBehavior.JUMP_EAST
        Direction.DIVE,
        Direction.EMERGE -> return null
      }
  val ledgeX = fromX + direction.dx
  val ledgeY = fromY + direction.dy
  if (map.tileAt(ledgeX, ledgeY)?.behavior != expectedBehavior) return null

  val landingX = fromX + direction.dx * 2
  val landingY = fromY + direction.dy * 2
  if (landingX !in 0 until map.width || landingY !in 0 until map.height) return null
  if (map.tileAt(landingX, landingY)?.blocksMovement() == true) return null
  return landingX to landingY
}

@Singleton
class MovementService
@Inject
constructor(
    private val warpService: WarpService,
    private val mapLoadService: MapLoadService,
    private val npcService: NpcService,
    private val presenceService: PresenceService,
    private val mapManager: MapManager,
    private val characterStore: CharacterStore,
    private val encounterService: EncounterService,
    private val mapScriptService: MapScriptService,
    private val trainerSightService: TrainerSightService,
    private val fieldMoveService: FieldMoveService,
    private val violations: ViolationLog,
) {

  /**
   * How fast a player may take tiles.
   *
   * Collision was checked on every step and the position was always the server's, so this was never
   * a way through a wall. It was a way to cross Sinnoh in the time it takes to cross a room:
   * nothing asked how often a step arrived, and the pipeline's flood limiter sits thirty times
   * above the pace the client's own animation walks at. Twelve a second is half again the fastest
   * the game moves anyone, and the burst is three seconds of them.
   */
  private val pace = PaceLimit(burst = 40.0, perSecond = 12.0)

  /** One step. The client sends the tile it left and the direction, the server derives the rest. */
  fun onMovement(event: PacketEvent<MovementPacket>) {
    val ctx = event.session
    val state = ctx.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    val msg = event.packet
    log.debug { "Movement: char=$charId from (${msg.x}, ${msg.y}) dir=${msg.direction}" }

    val stored = characterStore.getCharacter(charId) ?: return
    val currentMap =
        mapManager.getMap(
            stored.info.positionRegionId,
            stored.info.positionBankId,
            stored.info.positionMapId,
        ) ?: return
    val fromX = stored.info.positionX.toInt()
    val fromY = stored.info.positionY.toInt()

    // A step the game could not have taken this soon. Refused rather than delayed, and counted:
    // one is a connection catching up, a stream of them is a speed no animation runs at.
    if (!pace.allow(charId)) {
      violations.record(
          charId,
          ViolationLog.Kind.IMPOSSIBLE_PACE,
          "is taking tiles faster than the game walks; the step was dropped")
      sendPositionReset(ctx, charId, currentMap, fromX, fromY, state.facingDirection)
      return
    }

    val atServerTile = msg.x == fromX && msg.y == fromY

    when {
      // Drop every step until the client asks for its player, else one left over from the old map
      // can fire a second warp.
      state.justWarped -> return
      // A script owns the player, like the decomp's lockall.
      state.inDialog -> {
        sendPositionReset(ctx, charId, currentMap, fromX, fromY, state.facingDirection)
        return
      }
      !atServerTile -> {
        log.debug {
          "DESYNC: char=$charId claims (${msg.x}, ${msg.y}), server has ($fromX, $fromY), resetting"
        }
        sendPositionReset(ctx, charId, currentMap, fromX, fromY, msg.direction)
        return
      }
    }

    // Only once the step is accepted, so a locked player keeps the facing its script left.
    state.facingDirection = msg.direction

    var toX = fromX + msg.direction.dx
    var toY = fromY + msg.direction.dy

    // Ledge hops land two tiles away.
    ledgeLanding(currentMap, fromX, fromY, msg.direction)?.let { landing ->
      toX = landing.first
      toY = landing.second
    }

    // Stairs and arrow warps fire from the tile the player stands on.
    val standingBehavior = currentMap.tileAt(fromX, fromY)?.behavior
    if (standingBehavior?.warpsWhenWalking == msg.direction) {
      val onTileWarp = MapEventLayout.warpAt(currentMap, stored.storyFlags, fromX, fromY)
      if (onTileWarp != null) {
        warpService.executeWarp(ctx, charId, onTileWarp)
        return
      }
    }

    // Walking off the edge of a map hands the player to the neighbouring map, if there is one.
    if (toX !in 0 until currentMap.width || toY !in 0 until currentMap.height) {
      val connection = currentMap.connections.find { it.direction == msg.direction }
      // Connections stay inside one region.
      val targetMap =
          connection?.let {
            mapManager.getMap(currentMap.regionId, it.targetBank.toByte(), it.targetMap.toByte())
          }
      if (connection == null || targetMap == null) {
        sendPositionReset(ctx, charId, currentMap, fromX, fromY, msg.direction)
        return
      }
      val entryX =
          when (msg.direction) {
            Direction.LEFT -> targetMap.width - 1
            Direction.RIGHT -> 0
            else -> fromX - connection.unknown
          }
      val entryY =
          when (msg.direction) {
            Direction.UP -> targetMap.height - 1
            Direction.DOWN -> 0
            else -> fromY - connection.unknown
          }
      // The entry used to be clamped to the target's bounds, and a clamped coordinate is a
      // tile the connection never named, it has put players under the map.
      if (entryX !in 0 until targetMap.width ||
          entryY !in 0 until targetMap.height ||
          !isWalkable(targetMap, entryX, entryY, state, msg.direction)) {
        log.debug { "EDGE: char=$charId refused at ($entryX, $entryY) on the far map" }
        sendPositionReset(ctx, charId, currentMap, fromX, fromY, msg.direction)
        return
      }
      edgeTransition(ctx, charId, currentMap.regionId, connection, entryX.toByte(), entryY.toByte())
      return
    }

    // A door only warps when walked into from below, a ladder warps on the step itself.
    val targetBehavior = currentMap.tileAt(toX, toY)?.behavior
    val stepsIntoWarp =
        when (targetBehavior) {
          TileBehavior.DOOR -> msg.direction == Direction.UP
          else -> targetBehavior?.warpsOnStep == true
        }
    val warp =
        if (!stepsIntoWarp) null else MapEventLayout.warpAt(currentMap, stored.storyFlags, toX, toY)
    if (warp != null) {
      log.info { "WARP at ($toX, $toY) facing ${msg.direction}" }
      warpService.executeWarp(ctx, charId, warp)
      return
    }

    if (fieldMoveService.tryPushBoulder(ctx, state, currentMap, toX, toY, msg.direction)) {
      sendPositionReset(ctx, charId, currentMap, fromX, fromY, msg.direction)
      return
    }

    if (!isWalkable(currentMap, toX, toY, state, msg.direction)) {
      log.debug { "WALL: char=$charId blocked at ($toX, $toY)" }
      sendPositionReset(ctx, charId, currentMap, fromX, fromY, msg.direction)
      return
    }

    characterStore.updatePosition(charId, toX.toShort(), toY.toShort(), facing = msg.direction)
    state.x = toX.toShort()
    state.y = toY.toShort()

    // Riding ashore ends the ride, the way the avatar dismounts on the sand in the decomp. There
    // is no script and no question here: the tile the step landed on is the whole answer.
    if (state.isSurfing && currentMap.tileAt(toX, toY)?.behavior?.isWater != true) {
      fieldMoveService.setSurfing(ctx, state, false)
    }

    // A Sinnoh overworld map is a stretch of a shared matrix rather than a rectangle with edges,
    // so a step can leave one map for the next without ever leaving the plane.
    val map = changedZone(ctx, charId, currentMap, toX, toY) ?: currentMap

    // The client already walked itself there, so only the observers need telling.
    presenceService.broadcastToObservers(
        ctx,
        movePacket(charId, map, toX, toY, msg.direction),
    )

    // A trainer noticing the player comes first, then a story coordinate event, then a random
    // encounter, the order `Field_CheckStandardInput` runs them in.
    if (trainerSightService.onStep(ctx, state, map, toX, toY)) return
    if (!mapScriptService.onStep(ctx, state, map, toX, toY)) {
      encounterService.onStep(ctx, charId, map, toX, toY)
    }
  }

  /**
   * The matrix's own header grid, which is the only thing that says which map a tile belongs to on
   * a region built this way: `FieldMap_ChangeZone` reads exactly this and swaps the map header
   * under the player without moving them.
   */
  private fun changedZone(
      ctx: SessionContext,
      charId: Long,
      from: MapDef,
      x: Int,
      y: Int,
  ): MapDef? {
    val header = from.terrain?.headerAt(x, y) ?: return null
    val current = ((from.bankId.toInt() and 0xFF) shl 8) or (from.mapId.toInt() and 0xFF)
    if (header == current) return null
    val bank = (header shr 8).toByte()
    val mapId = (header and 0xFF).toByte()
    // A cell the matrix leaves to MAP_HEADER_EVERYWHERE has no map of its own; those cells carry
    // no land data either, so nothing can stand on one.
    val target = mapManager.getMap(from.regionId, bank, mapId) ?: return null

    val state = ctx.attributes[PLAYER_STATE]
    if (state != null) {
      state.bankId = bank.toInt()
      state.mapId = mapId.toInt()
    }
    characterStore.updatePosition(charId, x.toShort(), y.toShort(), bank, mapId)
    characterStore.flushCharacterAsync(charId)
    presenceService.refresh(ctx)
    npcService.spawnNpcsForMap(ctx, bank.toInt(), mapId.toInt(), from.regionId.toInt())
    if (state != null) mapScriptService.onMapEnter(ctx, state, target)

    log.info { "Player $charId changed zone to bank=$bank map=$mapId" }
    return target
  }

  /** Snap the client back to the position the server considers authoritative. */
  private fun sendPositionReset(
      ctx: SessionContext,
      charId: Long,
      map: MapDef,
      x: Int,
      y: Int,
      direction: Direction,
  ) {
    ctx.send(movePacket(charId, map, x, y, direction))
  }

  /** One entity's step, in the packet its map's grid fits. */
  private fun movePacket(
      charId: Long,
      map: MapDef,
      x: Int,
      y: Int,
      direction: Direction,
  ): Any =
      if (isNdsRegion(map.regionId.toInt() and 0xff))
          EntityMovePacket(entityId = charId, x = x, y = y, direction = direction)
      else
          GbaEntityMovePacket(
              entityId = charId,
              bankId = map.bankId.toInt() and 0xff,
              mapId = map.mapId.toInt() and 0xff,
              x = x,
              y = y,
              movementMode = 2,
              direction = direction,
          )

  /** Turning in place. Only observers need it, the client has already turned itself. */
  fun onFaceDirection(event: PacketEvent<FaceDirectionPacket>) {
    val ctx = event.session
    val state = ctx.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    if (state.inDialog) {
      // The client already turned itself, so put it back. A face turn is only ever sent about
      // other entities, so the correction rides on the same packet a blocked step uses.
      val stored = characterStore.getCharacter(charId) ?: return
      val map =
          mapManager.getMap(
              stored.info.positionRegionId,
              stored.info.positionBankId,
              stored.info.positionMapId,
          ) ?: return
      sendPositionReset(
          ctx,
          charId,
          map,
          stored.info.positionX.toInt(),
          stored.info.positionY.toInt(),
          state.facingDirection,
      )
      return
    }
    val msg = event.packet
    state.facingDirection = msg.direction
    characterStore.updatePosition(charId, state.x, state.y, facing = msg.direction)

    presenceService.broadcastToObservers(
        ctx,
        EntityFaceTurnPacket(entityId = charId, facing = msg.direction.ordinal.toByte()),
    )
  }

  /**
   * Whether a step lands somewhere the player can be, which is the collision bit, what the tile is,
   * and, since Strength, whether a boulder is standing on it.
   */
  private fun isWalkable(
      map: MapDef,
      x: Int,
      y: Int,
      state: PlayerState,
      into: Direction
  ): Boolean {
    if (x !in 0 until map.width || y !in 0 until map.height) return false
    if (fieldMoveService.boulderAt(state, map, x, y) != null) return false
    val tile = map.tileAt(x, y) ?: return true
    if (tile.blocksMovement()) return false
    return when (tile.behavior) {
      // Only something riding on the water gets onto the water.
      TileBehavior.SURFABLE_WATER -> state.isSurfing
      // A waterfall is ridden down by walking into it from above and climbed only by the move,
      // which is `FieldMoveService.useWaterfall` and an A press.
      TileBehavior.WATERFALL -> state.isSurfing && into == Direction.DOWN
      else -> true
    }
  }

  private fun edgeTransition(
      ctx: SessionContext,
      charId: Long,
      regionId: Byte,
      connection: MapData.GbaConnection,
      targetX: Byte,
      targetY: Byte,
  ) {
    val targetBank = connection.targetBank.toByte()
    val targetMap = connection.targetMap.toByte()
    val map = mapManager.getMap(regionId, targetBank, targetMap) ?: return

    val state = ctx.attributes[PLAYER_STATE]
    if (state != null) {
      state.bankId = targetBank.toInt()
      state.mapId = targetMap.toInt()
      state.x = targetX.toShort()
      state.y = targetY.toShort()
    }
    characterStore.updatePosition(
        charId,
        targetX.toShort(),
        targetY.toShort(),
        targetBank,
        targetMap,
    )
    characterStore.flushCharacterAsync(charId)
    presenceService.refresh(ctx)

    mapLoadService.preloadConnectedMaps(ctx, map, depth = 1)
    npcService.spawnNpcsForMap(ctx, targetBank.toInt(), targetMap.toInt(), regionId.toInt())

    if (state != null) mapScriptService.onMapEnter(ctx, state, map)

    log.info { "Player $charId edge-transitioned to bank=$targetBank map=$targetMap" }
  }
}
