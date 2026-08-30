package de.fiereu.openmmo.server.game.world

import de.fiereu.openmmo.maps.MapManager
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Singleton

/** One region's bank and map packed into a key, which is all a local warp can name. */
internal fun localMapKey(bankId: Int, mapId: Int): Int =
    ((bankId and 0xFF) shl 8) or (mapId and 0xFF)

/**
 * Which maps a scene could honestly have walked somebody to from the one they were on.
 *
 * A client that runs its own field scenes reports where it ended up. That report was checked three
 * ways: the map exists, the tile is inside it, the tile is not collision. All three are about the
 * destination being a real place, none about it being a reachable one, so any map in the region and
 * any standable tile was accepted, and the region is the whole game.
 *
 * The map data already says what a warp can do. A scene moves a player through a door, a staircase,
 * a map edge, or back the way they came, and every one of those is an edge of the warp graph the
 * maps describe, so the destination has to be one hop away or the map they are already on.
 *
 * Both directions on purpose. A door is a warp tile naming a tile on another map and the return
 * trip is usually the matching tile, but not always, and taking both costs nothing a cheat can use:
 * a one hop neighbour is somewhere the player could have walked in a few seconds.
 *
 * Heal locations are in the set as well, for the region rather than the map, because a blackout is
 * the one legitimate move that crosses the world.
 *
 * Computed once per map and kept: the graph is fixed once the maps are registered.
 */
@Singleton
class WarpNeighbours @Inject constructor(private val mapManager: MapManager) {

  private val cache = ConcurrentHashMap<Int, Set<Int>>()
  private val healPoints = ConcurrentHashMap<Int, Set<Int>>()

  /**
   * The maps a scene on [bankId]:[mapId] could have left the player on, as [localMapKey] values.
   * Always contains the map itself, which is the in-map case.
   */
  fun oneHopFrom(regionId: Int, bankId: Int, mapId: Int): Set<Int> {
    val here = localMapKey(bankId, mapId)
    return cache.computeIfAbsent((regionId and 0xFF shl 16) or here) { build(regionId, here) }
  }

  private fun build(regionId: Int, here: Int): Set<Int> {
    val reachable = mutableSetOf(here)
    for (map in mapManager.all()) {
      if (map.regionId.toInt() and 0xFF != regionId and 0xFF) continue
      val key = localMapKey(map.bankId.toInt(), map.mapId.toInt())
      // Forward: what this map's exits name. Reverse: the maps whose exits name this one. A
      // dynamic warp names nothing until it is set, so its placeholder is not an edge.
      val exits =
          map.warps
              .filterNot { it.dynamic }
              .map { localMapKey(it.targetBankId.toInt(), it.targetMapId.toInt()) } +
              map.connections.map { localMapKey(it.targetBank, it.targetMap) }
      if (key == here) reachable += exits else if (here in exits) reachable += key
    }
    reachable += healLocations(regionId)
    return reachable
  }

  /** Every Pokemon Centre in the region, because a blackout goes to one from anywhere. */
  private fun healLocations(regionId: Int): Set<Int> =
      healPoints.computeIfAbsent(regionId and 0xFF) {
        mapManager
            .all()
            .filter { it.regionId.toInt() and 0xFF == regionId and 0xFF && it.healLocation != null }
            .map { localMapKey(it.bankId.toInt(), it.mapId.toInt()) }
            .toSet()
      }
}
