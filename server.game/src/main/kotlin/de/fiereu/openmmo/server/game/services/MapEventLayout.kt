package de.fiereu.openmmo.server.game.services

import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.maps.BgEventDef
import de.fiereu.openmmo.maps.MapDef
import de.fiereu.openmmo.maps.WarpTile
import de.fiereu.openmmo.story.generated.sinnoh.SinnohFlags

/** A map's warps, signs and forced moves as this player sees them. */
internal object MapEventLayout {

  fun warps(map: MapDef, storyFlags: Set<String>): List<WarpTile> =
      when {
        isValleyWindworksOutside(map) &&
            SinnohFlags.FLAG_UNLOCKED_VALLEY_WINDWORKS_DOOR !in storyFlags ->
            map.warps.mapIndexed { i, warp ->
              if (i == VALLEY_WINDWORKS_DOOR_WARP) warp.copy(x = PARK_X, y = PARK_Y) else warp
            }
        else -> map.warps
      }

  fun bgEvents(map: MapDef, storyFlags: Set<String>): List<BgEventDef> =
      when {
        isValleyWindworksOutside(map) &&
            SinnohFlags.FLAG_UNLOCKED_VALLEY_WINDWORKS_DOOR in storyFlags ->
            map.bgEvents.mapIndexed { i, event ->
              if (i == VALLEY_WINDWORKS_DOOR_SIGN) event.copy(x = PARK_X, y = PARK_Y) else event
            }
        else -> map.bgEvents
      }

  fun warpAt(map: MapDef, storyFlags: Set<String>, x: Int, y: Int): WarpTile? =
      warps(map, storyFlags).find { it.x == x && it.y == y }

  /** The tile a step out of ([fromX], [fromY]) in [direction] ends on. */
  fun stepLanding(
      map: MapDef,
      fromX: Int,
      fromY: Int,
      direction: Direction,
      claimedTiles: Int = 1,
  ): Pair<Int, Int> {
    // A map this server has no surface for answers with the client's own count and nothing else.
    if (surfaceIsClientOwned(map)) {
      val span = claimedTiles.coerceIn(1, 3)
      return (fromX + direction.dx * span) to (fromY + direction.dy * span)
    }
    val ahead = map.tileAt(fromX + direction.dx, fromY + direction.dy)?.behavior
    val tiles =
        when {
          ahead?.jumpsWhenWalking == direction -> 2
          ahead?.jumpsTwiceWhenWalking == direction -> 3
          map.tileAt(fromX, fromY)?.behavior?.rampRidesToward == direction ->
              if (claimedTiles == 3) 3 else 1
          else -> 1
        }
    return (fromX + direction.dx * tiles) to (fromY + direction.dy * tiles)
  }

  /** True on a map whose walkable surface is not the one this server has data for. */
  fun surfaceIsClientOwned(map: MapDef): Boolean =
      (map.regionId.toInt() and 0xFF) == SINNOH &&
          (map.bankId.toInt() and 0xFF) == DISTORTION_WORLD_BANK &&
          (map.mapId.toInt() and 0xFF) in DISTORTION_WORLD_MAPS

  private fun isValleyWindworksOutside(map: MapDef): Boolean =
      (map.regionId.toInt() and 0xFF) == SINNOH &&
          (map.bankId.toInt() and 0xFF) == VALLEY_WINDWORKS_BANK &&
          (map.mapId.toInt() and 0xFF) == VALLEY_WINDWORKS_MAP

  private const val SINNOH = 3
  private const val DISTORTION_WORLD_BANK = 2
  /**
   * 1F, B1F..B4F, B5F..B7F, Giratina's room and the Turnback Cave room. Bank 2 map 66 is not one.
   */
  private val DISTORTION_WORLD_MAPS = (61..65) + (67..71)
  private const val VALLEY_WINDWORKS_BANK = 0
  private const val VALLEY_WINDWORKS_MAP = 200
  private const val VALLEY_WINDWORKS_DOOR_WARP = 0
  private const val VALLEY_WINDWORKS_DOOR_SIGN = 1
  private const val PARK_X = 243
  private const val PARK_Y = 650
}
