package de.fiereu.openmmo.maps

import de.fiereu.openmmo.common.Tile2D
import de.fiereu.openmmo.common.enums.TileBehavior
import java.util.Base64
import java.util.concurrent.ConcurrentHashMap

/** The walkable surface of one Sinnoh map matrix. */
class TerrainPlane(
    val cols: Int,
    val rows: Int,
    private val chunkSide: Int,
    /** One chunk per cell in row major order, null where the matrix leaves the cell empty. */
    private val chunks: List<String?>,
    /** The matrix's own per cell altitude, carried as it is stored; nothing here interprets it. */
    private val altitudes: List<Int>,
    private val behaviors: Array<TileBehavior>,
    /** The map header id that owns each cell. */
    private val headers: List<Int> = emptyList(),
) {

  val width: Int = cols * chunkSide
  val height: Int = rows * chunkSide

  private val decoded = ConcurrentHashMap<Int, IntArray>()

  fun tileAt(x: Int, y: Int): Tile2D? {
    val plane = planeAt(x, y) ?: return null
    val raw = plane[(y % chunkSide) * chunkSide + (x % chunkSide)]
    return Tile2D(
        // No metatile id exists on a DS map, the scene is a model. The attribute word is carried
        // whole so nothing is lost to a field that means something else here.
        material = raw.toShort(),
        // Bit 15 set is impassable: TerrainCollisionManager_CheckCollision reads exactly this bit.
        collision = if ((raw and 0x8000) != 0) 1 else 0,
        behavior = behaviors[raw and 0xFF],
    )
  }

  /**
   * The matrix cell altitude under a tile, or null outside the matrix or on a plane without
   * them.
   */
  fun altitudeAt(x: Int, y: Int): Int? {
    if (altitudes.isEmpty() || x < 0 || y < 0 || x >= width || y >= height) return null
    return altitudes[(y / chunkSide) * cols + (x / chunkSide)]
  }

  /** The map header id that owns a tile, or null outside the matrix or on a plane without them. */
  fun headerAt(x: Int, y: Int): Int? {
    if (headers.isEmpty() || x < 0 || y < 0 || x >= width || y >= height) return null
    return headers[(y / chunkSide) * cols + (x / chunkSide)]
  }

  private fun planeAt(x: Int, y: Int): IntArray? {
    if (x < 0 || y < 0 || x >= width || y >= height) return null
    val cell = (y / chunkSide) * cols + (x / chunkSide)
    decoded[cell]?.let {
      return it
    }
    val encoded = chunks[cell] ?: return null
    val bytes = Base64.getDecoder().decode(encoded)
    val plane =
        IntArray(bytes.size / 2) {
          (bytes[it * 2].toInt() and 0xFF) or ((bytes[it * 2 + 1].toInt() and 0xFF) shl 8)
        }
    decoded[cell] = plane
    return plane
  }
}
