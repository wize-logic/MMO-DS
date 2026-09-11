package de.fiereu.openmmo.maps

import de.fiereu.openmmo.common.HealLocation
import de.fiereu.openmmo.common.Tile2D
import de.fiereu.openmmo.common.enums.EncounterMethod
import de.fiereu.openmmo.common.enums.EncounterType
import de.fiereu.openmmo.common.enums.Lighting
import de.fiereu.openmmo.common.enums.MapType
import de.fiereu.openmmo.common.enums.TileBehavior
import de.fiereu.openmmo.common.enums.Weather
import de.fiereu.openmmo.net.game.packets.MapData
import java.util.Base64

/** The wire region a ported map travels as, and the first header id one may have. */
const val PORTED_REGION = 3
const val PORTED_HEADER_BASE = 594

class MapDef(
    /**
     * What the decomp calls this map, lowercased: `jubilife_city_pokecenter_1f`. It is the only
     * human name a map has here, the wire carries a bank and a map number and nothing else, so it
     * is what /handbook searches and what /warp accepts instead of two numbers.
     */
    val name: String = "",
    val regionId: Byte,
    val bankId: Byte,
    val mapId: Byte,
    val width: Int = 20,
    val height: Int = 15,
    val paletteIdx1: Int = 12,
    val paletteIdx2: Int = 14,
    val borderWidth: Int = 2,
    val borderHeight: Int = 2,
    val unknownShort: Int = 0,
    val unknownByte: Int = 0,
    val borderTiles: List<Tile2D> = listOf(Tile2D(8, 0), Tile2D(8, 0), Tile2D(8, 0), Tile2D(8, 0)),
    val lighting: Lighting = Lighting.REGULAR,
    val weather: Weather = Weather.REGULAR_WEATHER,
    val mapType: MapType = MapType.CITY,
    val encounterType: EncounterType = EncounterType.RANDOM,
    val wildEncounters: List<WildEncounterTable> = emptyList(),
    /** Form selectors the map's encounter archive carries, or null where it carries none. */
    val wildEncounterForms: WildEncounterForms? = null,
    val connections: List<MapData.GbaConnection> = emptyList(),
    val warps: List<WarpTile> = emptyList(),
    val npcs: List<NpcDef> = emptyList(),
    val bgEvents: List<BgEventDef> = emptyList(),
    /** Decomp label of the script that runs when a player enters this map, or "" if none. */
    val onTransitionScript: String = "",
    /**
     * Where entering this map sends the player back to after a white out (the decomp `setrespawn`),
     * or null when the map sets no respawn.
     */
    val healLocation: HealLocation? = null,
    /** Conditional entry scripts: run [MapFrameScript.script] when its var equals its value. */
    val onFrameScripts: List<MapFrameScript> = emptyList(),
    /** Conditional tile scripts checked after the player completes a step. */
    val coordScripts: List<MapCoordScript> = emptyList(),
    private val blockData: String = "",
    private val behaviorData: String = "",
    /**
     * The matrix this map is placed on, for the DS regions, whose geometry belongs to the matrix
     * rather than to the map. Null for the GBA regions, which carry their own [blockData].
     */
    /** Whether anything grows on this map, which only a cave has to be asked. */
    val hasGrass: Boolean = false,
    val terrain: TerrainPlane? = null,
    /**
     * A `static:<species>:<level>` fight this map's own scripts stage that no person and no sign on
     * the map carries, or "" for every other map.
     */
    val staticSite: String = "",
    /** The tiles of this map's headbutt trees, on a ported map that has any; see [HeadbuttTree]. */
    val headbuttTrees: List<HeadbuttTree> = emptyList(),
    /** What a smashed rock may leave behind here, or null where the rubble is only rubble. */
    val rubble: RockSmashRubble? = null,
    /** Whether Fly may be used from this map, which is the header's own `isFlyAllowed`. */
    val flyAllowed: Boolean = false,
) {

  val tiles: List<Tile2D> by lazy { decodeBlockData(blockData, behaviorData) }

  /** Whether this map came out of another cartridge. */
  val ported: Boolean
    get() =
        regionId.toInt() == PORTED_REGION &&
            (((bankId.toInt() and 0xFF) shl 8) or (mapId.toInt() and 0xFF)) >= PORTED_HEADER_BASE

  fun tileAt(x: Int, y: Int): Tile2D? =
      terrain?.tileAt(x, y)
          ?: if (x in 0 until width && y in 0 until height) tiles.getOrNull(y * width + x) else null

  /** The wild table for [method] on this map, or null when the map has no such encounters. */
  fun encounterTable(method: EncounterMethod): WildEncounterTable? =
      wildEncounters.firstOrNull { it.method == method }
}

/** One ON_FRAME entry: run [script] when the story var [varKey] currently equals [value]. */
data class MapFrameScript(
    val varKey: String,
    val value: Int,
    val script: String,
)

/** One map coordinate trigger, corresponding to a decomp map.json `coord_events` entry. */
data class MapCoordScript(
    val x: Int,
    val y: Int,
    val elevation: Int,
    val varKey: String,
    val value: Int,
    val script: String,
    val width: Int = 1,
    val height: Int = 1,
) {
  /** True when ([tileX], [tileY]) is inside this trigger's rectangle. */
  fun covers(tileX: Int, tileY: Int): Boolean =
      tileX in x until (x + width) && tileY in y until (y + height)
}

private fun decodeBlockData(blockData: String, behaviorData: String): List<Tile2D> {
  if (blockData.isEmpty()) return emptyList()
  val bytes = Base64.getDecoder().decode(blockData)
  // One behavior byte per tile, generated alongside the block data. Empty on maps we could not
  // resolve behaviors for, in which case every tile stays NORMAL.
  val behaviors =
      if (behaviorData.isEmpty()) ByteArray(0) else Base64.getDecoder().decode(behaviorData)
  val behaviorValues = TileBehavior.entries
  return List(bytes.size / 2) { i ->
    val raw = (bytes[i * 2].toInt() and 0xFF) or ((bytes[i * 2 + 1].toInt() and 0xFF) shl 8)
    val behavior = behaviors.getOrNull(i)?.toInt()?.let { behaviorValues.getOrNull(it) }
    Tile2D(
        material = (raw and 0x3FF).toShort(),
        collision = ((raw shr 10) and 0x3F).toByte(),
        behavior = behavior ?: TileBehavior.NORMAL,
    )
  }
}
