package de.fiereu.openmmo.server.game.services

import de.fiereu.openmmo.maps.BgEventDef
import de.fiereu.openmmo.maps.MapDef
import de.fiereu.openmmo.maps.WarpTile
import de.fiereu.openmmo.story.generated.sinnoh.SinnohFlags

/** A map's warps and signs as this player sees them. */
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

  private fun isValleyWindworksOutside(map: MapDef): Boolean =
      (map.regionId.toInt() and 0xFF) == SINNOH &&
          (map.bankId.toInt() and 0xFF) == VALLEY_WINDWORKS_BANK &&
          (map.mapId.toInt() and 0xFF) == VALLEY_WINDWORKS_MAP

  private const val SINNOH = 3
  private const val VALLEY_WINDWORKS_BANK = 0
  private const val VALLEY_WINDWORKS_MAP = 200
  private const val VALLEY_WINDWORKS_DOOR_WARP = 0
  private const val VALLEY_WINDWORKS_DOOR_SIGN = 1
  private const val PARK_X = 243
  private const val PARK_Y = 650
}
