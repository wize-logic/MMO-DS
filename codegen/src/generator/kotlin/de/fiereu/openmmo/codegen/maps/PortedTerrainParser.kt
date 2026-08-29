package de.fiereu.openmmo.codegen.maps

import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.Base64

/** The walkable surface of a map this game did not ship, read out of the cartridge it came from. */
class PortedTerrainParser(
    private val decompDir: File,
    private val mmoDir: File,
    private val names: List<String>,
    private val tileBehaviors: List<String>,
) {

  fun parse(): ParsedRegion {
    if (names.isEmpty()) return ParsedRegion(emptyList())
    val translation = readTranslation()
    val rows = readMapRows()
    val land = Narc.members(File(decompDir, LAND_DATA).readBytes())
    val overworld by lazy { readMatrix(File(decompDir, MAP_MATRIX)) }

    val chunks = mutableMapOf<String, ParsedTerrainChunk>()
    val matrices = mutableListOf<ParsedTerrainMatrix>()
    for (name in names) {
      val row = rows[name] ?: error("no map called '$name' in ${File(mmoDir, MAPS).path}")
      // Matrix 0 is the shared overworld and the map is the cells that name its header; anything
      // else belongs to this map alone and is carried whole. The porter splits on the same
      // column, which is what keeps the two halves agreeing about where a tile is.
      val matrix = if (row.matrix == 0) overworld else readMatrix(matrixFile(row.matrix))
      matrices +=
          cutOut(name, if (row.matrix == 0) row.header else null, matrix, land, translation, chunks)
    }
    println(
        "[maps] ported: ${matrices.size} map(s) from ${decompDir.name}, " +
            "${chunks.size} land data chunks")
    return ParsedRegion(
        maps = emptyList(),
        terrainChunks = chunks.values.sortedBy { it.name },
        terrainMatrices = matrices,
        tileBehaviors = tileBehaviors,
        terrainPackage = PORTED_PACKAGE,
    )
  }

  /** One map's cells as a plane of its own. */
  private fun cutOut(
      name: String,
      headerId: Int?,
      matrix: Matrix,
      land: List<ByteArray>,
      translation: IntArray,
      chunks: MutableMap<String, ParsedTerrainChunk>,
  ): ParsedTerrainMatrix {
    val owns = { i: Int -> headerId == null || matrix.headers?.get(i) == headerId }
    val cells =
        (0 until matrix.cols * matrix.rows).filter { owns(it) && matrix.land[it] != EMPTY_CELL }
    require(cells.isNotEmpty()) { "$name owns no cell of the matrix it was said to sit on" }
    val xs = cells.map { it % matrix.cols }
    val ys = cells.map { it / matrix.cols }
    val x0 = xs.min()
    val x1 = xs.max()
    val y0 = ys.min()
    val y1 = ys.max()

    val cellNames = mutableListOf<String?>()
    val altitudes = mutableListOf<Int>()
    for (y in y0..y1) {
      for (x in x0..x1) {
        val i = y * matrix.cols + x
        val landId = matrix.land[i]
        if (landId == EMPTY_CELL || !owns(i)) {
          cellNames += null
          altitudes += 0
          continue
        }
        val chunkName = "${name.uppercase()}_$landId"
        chunks.getOrPut(chunkName) {
          ParsedTerrainChunk(chunkName, encodePlane(land[landId], translation))
        }
        cellNames += chunkName
        altitudes += matrix.altitudes[i]
      }
    }
    return ParsedTerrainMatrix(
        name = name.uppercase(),
        cols = x1 - x0 + 1,
        rows = y1 - y0 + 1,
        chunkSide = CHUNK_SIDE,
        chunks = cellNames,
        altitudes = altitudes,
        // No header grid, and that is the point of a cut-out. On a generated region the grid
        // is what `MovementService.changedZone` reads to swap the map under a player who walks
        // from one map onto the next, and it reads a cell as the wire address, bank in the
        // high byte.
        headers = emptyList(),
    )
  }

  /** One land data member's attribute plane, as the destination game would store it. */
  private fun encodePlane(member: ByteArray, translation: IntArray): String {
    val src = ByteBuffer.wrap(member).order(ByteOrder.LITTLE_ENDIAN)
    require(src.getInt(0) == TERRAIN_SIZE) {
      "a land data member's attribute plane is ${src.getInt(0)} bytes, not $TERRAIN_SIZE"
    }
    // The fifth section comes first, so the plane begins behind it. On the 446 of 676 members
    // with no fifth section that is 0x14 and this is the same read; on the other 230 it is
    // what the map's own events say, see the two counts in the design notes.
    val plane = HG_LAND_HEADER + (src.getShort(HG_FIFTH_SIZE).toInt() and 0xFFFF)
    val out = ByteArray(TERRAIN_SIZE)
    for (i in 0 until TERRAIN_SIZE step 2) {
      val raw = src.getShort(plane + i).toInt() and 0xFFFF
      val behavior = translation[raw and 0xFF]
      require(behavior >= 0) {
        "terrain byte 0x${(raw and 0xFF).toString(16)} has no row in ${File(mmoDir, TERRAIN_MAP)}"
      }
      val word = ((raw and COLLISION_BIT) or behavior) and CARRIED_BITS
      out[i] = word.toByte()
      out[i + 1] = (word shr 8).toByte()
    }
    return Base64.getEncoder().encodeToString(out)
  }

  private class Matrix(
      val cols: Int,
      val rows: Int,
      /** Null on a matrix that belongs to one map, where every cell is that map's. */
      val headers: IntArray?,
      val altitudes: IntArray,
      val land: IntArray,
  )

  /**
   * One matrix. Both games read this container with the same routine: two byte counts, two flags
   * saying whether the header and altitude grids are present, a name, then the grids.
   */
  private fun readMatrix(file: File): Matrix {
    require(file.isFile) { "no ${file.path}, the ported map's cells cannot be found without it" }
    val b = ByteBuffer.wrap(file.readBytes()).order(ByteOrder.LITTLE_ENDIAN)
    val cols = b[0].toInt() and 0xFF
    val rows = b[1].toInt() and 0xFF
    val hasHeaders = b[2].toInt() != 0
    val hasAltitudes = b[3].toInt() != 0
    var p = 5 + (b[4].toInt() and 0xFF)
    val n = cols * rows
    val headers = IntArray(n)
    if (hasHeaders) {
      for (i in 0 until n) headers[i] = b.getShort(p + i * 2).toInt() and 0xFFFF
      p += n * 2
    }
    val altitudes = IntArray(n)
    if (hasAltitudes) {
      for (i in 0 until n) altitudes[i] = b[p + i].toInt() and 0xFF
      p += n
    }
    val land = IntArray(n) { b.getShort(p + it * 2).toInt() and 0xFFFF }
    return Matrix(cols, rows, if (hasHeaders) headers else null, altitudes, land)
  }

  /** The file one of HeartGold's matrices is committed under, by its number. */
  private fun matrixFile(id: Int): File {
    val dir = File(decompDir, MAP_MATRIX_DIR)
    val prefix = "map_matrix_%04d".format(id)
    val hit =
        dir.listFiles()?.firstOrNull { it.name.startsWith(prefix) && it.name.endsWith(".bin") }
    require(hit != null) { "no $prefix*.bin under ${dir.path}" }
    return hit
  }

  /** One mmo/MAPS row: the source header id and which matrix the map sits on. */
  private class MapRow(val header: Int, val matrix: Int)

  /** `<name> -> <row>` out of mmo/MAPS, which is generated from the same decomp. */
  private fun readMapRows(): Map<String, MapRow> {
    val file = File(mmoDir, MAPS)
    require(file.isFile) { "no ${file.path}; run mmo/tools/gen_maps.py" }
    val rows =
        file
            .readLines()
            .mapNotNull { MAPS_ROW.find(it) }
            .associate {
              it.groupValues[1] to MapRow(it.groupValues[2].toInt(), it.groupValues[4].toInt())
            }
    require(rows.isNotEmpty()) {
      "${file.path} has no rows in the seven-column shape; run mmo/tools/gen_maps.py"
    }
    return rows
  }

  /** `<source byte> -> <byte to write>`, or -1 where the table has no row. */
  private fun readTranslation(): IntArray {
    val file = File(mmoDir, TERRAIN_MAP)
    require(file.isFile) { "no ${file.path}; run mmo/tools/gen_terrain_map.py" }
    val out = IntArray(256) { -1 }
    var rows = 0
    for (line in file.readLines()) {
      val m = TERRAIN_ROW.find(line) ?: continue
      out[m.groupValues[1].toInt(16)] = m.groupValues[2].toInt(16)
      rows++
    }
    require(rows >= 200) { "${file.path} has only $rows rows; it is not the generated table" }
    return out
  }

  /** Just enough of a NARC to take the members out of one. */
  private object Narc {
    fun members(b: ByteArray): List<ByteArray> {
      val buf = ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN)
      require(String(b, 0, 4, Charsets.US_ASCII) == "NARC") { "not a NARC" }
      val headerSize = buf.getShort(12).toInt() and 0xFFFF
      var at = headerSize
      var btaf = -1
      var gmif = -1
      repeat(buf.getShort(14).toInt() and 0xFFFF) {
        when (String(b, at, 4, Charsets.US_ASCII)) {
          "BTAF" -> btaf = at
          "GMIF" -> gmif = at
        }
        at += buf.getInt(at + 4)
      }
      require(btaf >= 0 && gmif >= 0) { "NARC has no BTAF or no GMIF block" }
      val data = gmif + 8
      val count = buf.getShort(btaf + 8).toInt() and 0xFFFF
      return (0 until count).map {
        val start = data + buf.getInt(btaf + 12 + it * 8)
        val end = data + buf.getInt(btaf + 16 + it * 8)
        b.copyOfRange(start, end)
      }
    }
  }

  companion object {
    const val PORTED_PACKAGE = "de.fiereu.openmmo.maps.generated.ported.terrain"

    private const val LAND_DATA = "files/a/0/6/5"
    private const val MAP_MATRIX_DIR = "files/fielddata/mapmatrix/map_matrix"
    private const val MAP_MATRIX = "$MAP_MATRIX_DIR/map_matrix_0000_EVERYWHERE.bin"
    private const val MAPS = "MAPS"
    private const val TERRAIN_MAP = "TERRAIN_MAP"

    private const val CHUNK_SIDE = 32
    private const val TERRAIN_SIZE = 0x800
    private const val HG_LAND_HEADER = 0x14
    private const val HG_FIFTH_SIZE = 0x12
    private const val COLLISION_BIT = 0x8000
    private const val CARRIED_BITS = 0x80FF
    private const val EMPTY_CELL = 0xFFFF

    private val MAPS_ROW =
        Regex("""^hg\s+(\S+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\S+)\s+(\S+)\s*$""")
    private val TERRAIN_ROW = Regex("""^0x([0-9A-F]{2})\s+0x([0-9A-F]{2})\s+(\S+)""")
  }
}
