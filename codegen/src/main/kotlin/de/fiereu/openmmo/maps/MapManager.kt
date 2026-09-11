package de.fiereu.openmmo.maps

import de.fiereu.openmmo.common.utils.isNdsRegion
import de.fiereu.openmmo.maps.generated.GeneratedMaps
import de.fiereu.openmmo.net.game.packets.LoadMapPacket
import de.fiereu.openmmo.net.game.packets.MapData
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class MapManager @Inject constructor() {

  private val maps = ConcurrentHashMap<Long, MapDef>()

  init {
    GeneratedMaps.loadInto(this)
  }

  fun register(map: MapDef) {
    maps[key(map.regionId, map.bankId, map.mapId)] = map
  }

  fun getMap(regionId: Byte, bankId: Byte, mapId: Byte): MapDef? =
      maps[key(regionId, bankId, mapId)]

  fun getMap(regionId: Int, bankId: Int, mapId: Int): MapDef? =
      getMap(regionId.toByte(), bankId.toByte(), mapId.toByte())

  /** The map called [name] exactly, or null. Case and separators are ignored. */
  fun byName(name: String): MapDef? =
      maps.values.firstOrNull { normalize(it.name) == normalize(name) }

  /**
   * Every map whose name contains [text], nearest match first: an exact name, then one that starts
   * with it, then the rest, and alphabetically within each. Empty [text] matches every named map,
   * which is how the handbook pages through them.
   */
  fun search(text: String): List<MapDef> {
    val wanted = normalize(text)
    return maps.values
        .filter { it.name.isNotEmpty() && normalize(it.name).contains(wanted) }
        .sortedWith(
            compareBy(
                {
                  val n = normalize(it.name)
                  if (n == wanted) 0 else if (n.startsWith(wanted)) 1 else 2
                },
                { it.name },
            ))
  }

  fun size(): Int = maps.size

  /** Every registered map, for the passes that need the whole world rather than one address. */
  fun all(): Collection<MapDef> = maps.values

  private fun normalize(s: String) = s.filter { it.isLetterOrDigit() }.lowercase()

  fun createLoadMapPacket(
      map: MapDef,
      reloadPlayer: Boolean = false,
      deleteCache: Boolean = false,
  ): LoadMapPacket =
      LoadMapPacket(
          reloadPlayer = reloadPlayer,
          deleteCache = deleteCache,
          regionId = map.regionId.toInt() and 0xFF,
          bankId = map.bankId.toInt() and 0xFF,
          mapId = map.mapId.toInt() and 0xFF,
          mapData = mapDataFor(map),
      )

  // A DS map carries no geometry on the wire: the client draws it from its own cartridge, so the
  // packet is the header's presentation fields and nothing else.
  private fun mapDataFor(map: MapDef): MapData =
      if (isNdsRegion(map.regionId.toInt() and 0xFF)) {
        MapData.NdsMapData(
            lighting = map.lighting,
            weather = map.weather,
            mapType = map.mapType,
        )
      } else {
        MapData.GbaMapData(
            width = map.width,
            height = map.height,
            paletteIdx1 = map.paletteIdx1,
            paletteIdx2 = map.paletteIdx2,
            borderWidth = map.borderWidth,
            borderHeight = map.borderHeight,
            unknownShort = map.unknownShort,
            unknownByte = map.unknownByte,
            borderTiles = map.borderTiles,
            lighting = map.lighting,
            weather = map.weather,
            mapType = map.mapType,
            encounterType = map.encounterType,
            connections = map.connections,
        )
      }

  private fun key(regionId: Byte, bankId: Byte, mapId: Byte): Long =
      ((regionId.toLong() and 0xFF) shl 16) or
          ((bankId.toLong() and 0xFF) shl 8) or
          (mapId.toLong() and 0xFF)
}
