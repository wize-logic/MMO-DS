package de.fiereu.openmmo.server.game.services

import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.common.enums.TileBehavior
import de.fiereu.openmmo.maps.MapDef
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.maps.NpcDef
import de.fiereu.openmmo.maps.generated.sinnoh.SpawnLocations
import de.fiereu.openmmo.net.game.packets.EntityTransportationPacket
import de.fiereu.openmmo.net.game.packets.StoryFlagUpdatePacket
import de.fiereu.openmmo.net.game.packets.TRANSPORTATION_SURFING
import de.fiereu.openmmo.server.game.script.Badge
import de.fiereu.openmmo.server.game.script.MovementStep
import de.fiereu.openmmo.server.game.session.PlayerState
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.StoredCharacter
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/** The HM walls that are not a one-shot removal. */
@Singleton
class FieldMoveService
@Inject
constructor(
    private val mapManager: MapManager,
    private val characterStore: CharacterStore,
    private val presenceService: PresenceService,
    private val scriptMovement: ScriptMovementService,
) {

  /**
   * Mount or dismount, telling the player and everyone watching them. Both need it: the packet
   * carries an entity id, and the client that receives it about itself is the one that changes its
   * own avatar (`tS1.D40` branches on whether the id is the session's own).
   */
  fun setSurfing(session: SessionContext, state: PlayerState, surfing: Boolean) {
    if (state.isSurfing == surfing) return
    val charId = state.characterId ?: return
    val transportation =
        if (surfing) TRANSPORTATION_SURFING
        else (state.transportation.toInt() and TRANSPORTATION_SURFING.toInt().inv()).toByte()
    state.transportation = transportation
    val packet = EntityTransportationPacket(entityId = charId, transportation = transportation)
    session.send(packet)
    presenceService.broadcastToObservers(session, packet)
    log.debug { "Character $charId transportation is now $transportation" }
  }

  /**
   * What the player is riding where they are standing, which is whatever the tile under them says.
   */
  fun restoreFromTile(state: PlayerState) {
    state.transportation =
        if (standingOnWater(state)) TRANSPORTATION_SURFING
        else (state.transportation.toInt() and TRANSPORTATION_SURFING.toInt().inv()).toByte()
  }

  /**
   * The same derivation as [restoreFromTile], for a client that walked itself onto or off the water
   * and said so.
   */
  fun syncMountToTile(session: SessionContext, state: PlayerState) =
      setSurfing(session, state, standingOnWater(state))

  /** Whether the tile under the player is water, which is the whole of the mount question. */
  private fun standingOnWater(state: PlayerState): Boolean =
      currentMap(state)?.tileAt(state.x.toInt(), state.y.toInt())?.behavior?.isWater == true

  /**
   * The decomp's `DoStrengthFunc FIELD_MOVE_FUNC_SET_ACTIVE`. Strength is visit-scoped: a map
   * change forgets it, and nothing stores it.
   */
  fun activateStrength(session: SessionContext, state: PlayerState) =
      setStrength(session, state, true)

  /**
   * Forget Strength and this visit's boulder positions. The decomp clears `FLAG_STRENGTH_ACTIVE` on
   * every map change (`field_map_change_flags.c`) and reloads objects from the map, so a push never
   * survives a door.
   */
  fun onMapChange(session: SessionContext, state: PlayerState) {
    if (state.strengthActive) setStrength(session, state, false)
    state.boulderTiles.clear()
  }

  /** The Strength boulder standing on [x], [y] for this visit, or null. */
  fun boulderAt(state: PlayerState, map: MapDef, x: Int, y: Int): NpcDef? =
      map.npcs.firstOrNull {
        it.script == STRENGTH_BOULDER_SCRIPT && boulderPos(state, it) == x to y
      }

  /**
   * Slide the boulder on [toX], [toY] one tile along [into] if the player can move one and that
   * tile can hold it. The player stays put: the decomp's push is walk-on-spot, not a step onto the
   * rock.
   */
  fun tryPushBoulder(
      session: SessionContext,
      state: PlayerState,
      map: MapDef,
      toX: Int,
      toY: Int,
      into: Direction,
  ): Boolean {
    val boulder = boulderAt(state, map, toX, toY) ?: return false
    if (!state.strengthActive && !canUseStrength(state)) return false
    val destX = toX + into.dx
    val destY = toY + into.dy
    if (!boulderCanLand(state, map, destX, destY)) return false
    state.boulderTiles[boulder.entityIdx] = destX to destY
    scriptMovement.repositionNpc(session, state, boulder.entityIdx, destX, destY)
    log.debug { "Strength pushed boulder ${boulder.entityIdx} to ($destX, $destY)" }
    return true
  }

  /**
   * Whether this player could turn Strength on where they stand, which is the two facts
   * `FieldMoves_CheckStrength` reads: the Mine Badge, and a party member that knows the move.
   */
  private fun canUseStrength(state: PlayerState): Boolean {
    val charId = state.characterId ?: return false
    val stored = characterStore.getCharacter(charId) ?: return false
    val region = Region.byWireValue(stored.info.positionRegionId)?.name?.lowercase() ?: return false
    if (Badge.MINE.keyIn(region) !in stored.storyFlags) return false
    return stored.pokemon.any {
      it.container == PokemonContainer.PARTY && it.moves.any { move -> move.id == MOVE_STRENGTH }
    }
  }

  /** The spawn row a reported landing is Fly's, or null when the landing is not one at all. */
  fun flyLanding(header: Int, x: Int, z: Int): Int? =
      SpawnLocations.FLY.indexOfFirst { it.mapHeaderId == header && it.x == x && it.z == z }
          .takeIf { it >= 0 }
          ?.plus(1)

  /**
   * Whether [stored] could have flown from where it was standing to the spawn row [flyId], which is
   * the four things `FieldMoves_CheckFly` and the town map read between them.
   */
  fun canFlyTo(stored: StoredCharacter, from: MapDef?, flyId: Int): Boolean {
    val row = SpawnLocations.flyLocation(flyId) ?: return false
    val region = Region.byWireValue(stored.info.positionRegionId)?.name?.lowercase() ?: return false
    if (Badge.COBBLE.keyIn(region) !in stored.storyFlags) return false
    if (from?.flyAllowed != true) return false
    if (VmStoryKeys.flag(stored.info.positionRegionId, row.firstArrivalFlagId) !in
        stored.storyFlags) {
      return false
    }
    return stored.pokemon.any {
      it.container == PokemonContainer.PARTY && it.moves.any { move -> move.id == MOVE_FLY }
    }
  }

  private fun setStrength(session: SessionContext, state: PlayerState, active: Boolean) {
    if (state.strengthActive == active) return
    state.strengthActive = active
    session.send(StoryFlagUpdatePacket(SINNOH_REGION, FLAG_STRENGTH_ACTIVE, active))
  }

  private fun boulderPos(state: PlayerState, npc: NpcDef): Pair<Int, Int> =
      state.boulderTiles[npc.entityIdx] ?: (npc.x to npc.y)

  private fun boulderCanLand(state: PlayerState, map: MapDef, x: Int, y: Int): Boolean {
    if (x !in 0 until map.width || y !in 0 until map.height) return false
    val tile = map.tileAt(x, y) ?: return true
    if (tile.blocksMovement()) return false
    return when (tile.behavior) {
      TileBehavior.SURFABLE_WATER,
      TileBehavior.WATERFALL -> false
      else -> boulderAt(state, map, x, y) == null
    }
  }

  /** The decomp's `UseSurf`: the player mounts and rides onto the water tile they are facing. */
  suspend fun useSurf(session: SessionContext, state: PlayerState) {
    setSurfing(session, state, true)
    scriptMovement.moveSelf(session, state, listOf(walk(state.facingDirection)))
  }

  /**
   * The decomp's `UseWaterfall`: the player climbs the run of waterfall tiles ahead and lands on
   * the water above it.
   */
  suspend fun useWaterfall(session: SessionContext, state: PlayerState) =
      ride(session, state, TileBehavior.WATERFALL)

  /** The decomp's `UseRockClimb`: the player scales the wall ahead and steps off past the top. */
  suspend fun useRockClimb(session: SessionContext, state: PlayerState) =
      ride(session, state, TileBehavior.ROCK_CLIMB_NORTH_SOUTH, TileBehavior.ROCK_CLIMB_EAST_WEST)

  /** Walk the whole run of [over] tiles ahead of the player and one more onto what follows it. */
  private suspend fun ride(
      session: SessionContext,
      state: PlayerState,
      vararg over: TileBehavior,
  ) {
    val map = currentMap(state) ?: return
    val facing = state.facingDirection
    var tiles = 0
    var x = state.x.toInt()
    var y = state.y.toInt()
    while (true) {
      x += facing.dx
      y += facing.dy
      tiles++
      val behavior = map.tileAt(x, y)?.behavior ?: return
      if (behavior !in over) break
      // A run this long is not a wall, it is a misread; stop rather than walk the player away.
      if (tiles > MAX_RIDE_TILES) return
    }
    if (x !in 0 until map.width || y !in 0 until map.height) return
    if (map.tileAt(x, y)?.blocksMovement() == true) return
    scriptMovement.moveSelf(session, state, List(tiles) { walk(facing) })
  }

  private fun currentMap(state: PlayerState): MapDef? {
    val charId = state.characterId ?: return null
    val info = characterStore.getCharacter(charId)?.info ?: return null
    return mapManager.getMap(info.positionRegionId, info.positionBankId, info.positionMapId)
  }

  private fun walk(direction: Direction): MovementStep =
      when (direction) {
        Direction.UP -> MovementStep.WALK_UP
        Direction.DOWN -> MovementStep.WALK_DOWN
        Direction.LEFT -> MovementStep.WALK_LEFT
        Direction.RIGHT -> MovementStep.WALK_RIGHT
        // A dive is not a step this server takes; nothing reaches here.
        else -> MovementStep.WALK_DOWN
      }

  private companion object {
    /** The tallest rock face in Sinnoh is well under this; the cap is only a runaway guard. */
    const val MAX_RIDE_TILES = 32
    /** Shared field-move script id for a Strength boulder (`SCRIPT_ID(FIELD_MOVES, 2)`). */
    const val STRENGTH_BOULDER_SCRIPT = "10002"
    /** Sinnoh's region id on the wire. */
    const val SINNOH_REGION: Byte = 3
    /** `MOVE_STRENGTH`, the move `FieldMoves_CheckStrength` looks for in the party. */
    const val MOVE_STRENGTH: Short = 70
    /** `MOVE_FLY`, the move `FieldMoves_CheckFly` looks for in the party. */
    const val MOVE_FLY: Short = 19
    /**
     * The decomp's `FLAG_STRENGTH_ACTIVE`. Sent live on 0x2A so the fused client can set the engine
     * sysflag; not a persisted story flag.
     */
    const val FLAG_STRENGTH_ACTIVE = 2402
  }
}

/** Reads as the same question everywhere: the surfing bit of the byte the wire carries. */
val PlayerState.isSurfing: Boolean
  get() = (transportation.toInt() and TRANSPORTATION_SURFING.toInt()) != 0
