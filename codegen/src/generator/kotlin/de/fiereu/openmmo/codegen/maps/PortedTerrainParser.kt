package de.fiereu.openmmo.codegen.maps

import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.Base64
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

/**
 * A whole region that reached the client out of another cartridge, as the server has to know it.
 */
class PortedTerrainParser(
    private val decompDir: File,
    private val mmoDir: File,
    private val regions: List<String>,
    private val headerBase: Int,
    private val regionId: Int,
    private val tileBehaviors: List<String>,
) {

  private val cutOrigins = mutableMapOf<String, Pair<Int, Int>>()

  fun parse(): ParsedRegion {
    val translation = readTranslation()
    val encounters = readEncounters()
    val rows = readMapRows().filter { it.region in regions && it.kind in PORTED_KINDS }
    if (rows.isEmpty()) {
      error("no map in ${File(mmoDir, MAPS).path} is in ${regions.joinToString(" or ")}")
    }
    val ordered = rows.sortedBy { it.header }
    // A map's address is `base + its own source header`, not its position in
    // this run: an index would have moved every Johto map the day Kanto was
    // ported beside it, and a saved position would name somewhere else.
    val headers = ordered.associate { it.header to headerBase + it.header }

    val byHeader = ordered.associateBy { it.header }
    val warpSources = readSourceWarps(ordered)

    val land = Narc.members(File(decompDir, LAND_DATA).readBytes())
    val chunks = mutableMapOf<String, ParsedTerrainChunk>()
    val matrices = mutableListOf<ParsedTerrainMatrix>()
    val refs = mutableMapOf<String, ParsedTerrainMatrix>()

    // One plane per region, for the reason the porter cuts one per region: the
    // two together span 45 columns of the shared overworld against the
    // engine's 30, so they are one package and two matrices.
    val outdoor = ordered.filter { it.matrix == SHARED_MATRIX }
    if (outdoor.isNotEmpty()) {
      val shared = readMatrix(matrixFile(SHARED_MATRIX))
      for (region in outdoor.map { it.region }.distinct().sorted()) {
        val mine = outdoor.filter { it.region == region }
        val matrix = cutRegion(region, shared, mine, headers, land, translation, chunks)
        matrices += matrix
        mine.forEach { refs[it.name] = matrix }
      }
    }
    // A matrix that belongs to one map is carried whole, and two maps naming the same one share it,
    // HeartGold gives every Pokemon Center of a size the same 1x1, and generating a copy per map
    // would be the same bytes under 180 names.
    val own = mutableMapOf<Int, ParsedTerrainMatrix>()
    for (row in ordered.filter { it.matrix != SHARED_MATRIX }) {
      refs[row.name] =
          own.getOrPut(row.matrix) {
            val matrix =
                carryWhole(
                    "matrix_${row.matrix}",
                    readMatrix(matrixFile(row.matrix)),
                    land,
                    translation,
                    chunks,
                )
            matrices += matrix
            matrix
          }
    }

    // After the cuts: a shared-matrix map's people are placed in its region's frame, and that
    // frame is not known until the region has been cut out of the overworld.
    val npcSources =
        readSourceTrainers(ordered, readMartScripts(), readApricornTrees(), readStaticSites())
    val headbutt = readHeadbutt()
    val rubble = readRubble()
    val maps =
        ordered.map { row ->
          val matrix = refs.getValue(row.name)
          val header = headers.getValue(row.header)
          buildMap(
              row,
              header,
              matrix,
              encounters[row.enc].orEmpty(),
              portWarps(row, warpSources, byHeader),
              npcSources[row.header].orEmpty(),
              FieldMoveTables(headbutt[row.header], rubble[row.header]))
        }
    println(
        "[maps] ported: ${maps.size} map(s) of ${regions.joinToString(", ")} " +
            "from ${decompDir.name}, ${matrices.size} matrices, " +
            "${chunks.size} land data chunks, headers " +
            "${headers.values.min()}..${headers.values.max()}")
    return ParsedRegion(
        maps = maps,
        terrainChunks = chunks.values.sortedBy { it.name },
        terrainMatrices = matrices,
        tileBehaviors = tileBehaviors,
        terrainPackage = "$GENERATED_PACKAGE.$PORTED_PACKAGE.terrain",
        marts = readMarts(),
        headbuttLuts = readHeadbuttLuts(),
    )
  }

  /** Both of the source cartridge's mart tables, out of its own `scrcmd_mart.c`. */
  /**
   * Which fruit each Apricorn tree grows, by the tree's index: the source's `sTreeApricorns`, read
   * as text the way the mart tables are, with the kind names resolved through its own constants.
   */
  private fun readApricornTrees(): Map<Int, Int> {
    val kinds =
        APRICORN_KIND.findAll(File(decompDir, APRICORN_KINDS).readText()).associate {
          it.groupValues[1] to it.groupValues[2].toInt()
        }
    val body =
        APRICORN_TABLE.find(File(decompDir, APRICORN_SOURCE).readText())?.groupValues?.get(1)
            ?: error("$APRICORN_SOURCE has no sTreeApricorns")
    return APRICORN_NAME.findAll(body.replace(Regex("//[^\n]*"), ""))
        .mapIndexed { index, m ->
          index to (kinds[m.value] ?: error("$APRICORN_SOURCE names ${m.value}"))
        }
        .toMap()
  }

  private fun readMarts(): ParsedMarts {
    val src = File(decompDir, MART_SOURCE).readText()
    val items =
        Regex("""#define\s+(ITEM_\w+)\s+(\d+)""")
            .findAll(File(decompDir, ITEM_IDS).readText())
            .associate { it.groupValues[1] to it.groupValues[2].toInt() }
    fun itemOf(name: String): Int =
        items[name] ?: error("$MART_SOURCE names $name, which $ITEM_IDS does not define")

    val commonBody =
        Regex(
                """BadgeMartItems\s+$MART_COMMON_TABLE\[\]\s*=\s*\{(.*?)}\s*;""",
                RegexOption.DOT_MATCHES_ALL)
            .find(src)
            ?.groupValues
            ?.get(1) ?: error("$MART_SOURCE has no $MART_COMMON_TABLE")
    val commonTiers =
        Regex("""\{\s*(ITEM_\w+)\s*,\s*(\d+)\s*}""").findAll(commonBody).map {
          itemOf(it.groupValues[1]) to it.groupValues[2].toInt()
        }
    // Every `const u16 name[] = { ITEM_..., 0xFFFF };` in the file, so the pointer array below can
    // be resolved by name. The terminator is the list's end and not one of its rows.
    val lists =
        Regex("""const\s+u16\s+(\w+)\[\]\s*=\s*\{(.*?)}\s*;""", RegexOption.DOT_MATCHES_ALL)
            .findAll(src)
            .associate { m ->
              m.groupValues[1] to
                  Regex("""ITEM_\w+""").findAll(m.groupValues[2]).map { itemOf(it.value) }.toList()
            }
    val order =
        Regex(
                """const\s+u16\s+\*$MART_SPECIAL_TABLE\[\]\s*=\s*\{(.*?)}\s*;""",
                RegexOption.DOT_MATCHES_ALL)
            .find(src)
            ?.groupValues
            ?.get(1) ?: error("$MART_SOURCE has no $MART_SPECIAL_TABLE")
    val specialties =
        Regex("""\b(_\w+)\b""").findAll(order).map { m ->
          lists[m.groupValues[1]]
              ?: error("$MART_SPECIAL_TABLE names ${m.groupValues[1]}, which is not a list here")
        }
    return ParsedMarts(commonTiers.toList(), specialties.toList())
  }

  /** Which script label puts a shelf up, and which shelf. */
  private fun readMartScripts(): Map<String, Int> {
    val dir = File(decompDir, FIELD_SCRIPTS)
    if (!dir.isDirectory) return emptyMap()
    val out = mutableMapOf<String, Int>()
    for (file in dir.listFiles().orEmpty().filter { it.name.endsWith(".s") }) {
      val blocks = mutableMapOf<String, MutableList<String>>()
      var current: MutableList<String>? = null
      for (raw in file.readLines()) {
        val line = raw.trim()
        val label = LABEL.matchEntire(line)
        if (label != null) {
          current = blocks.getOrPut(label.groupValues[1]) { mutableListOf() }
        } else if (line.isNotEmpty()) {
          current?.add(line)
        }
      }
      for (entry in blocks.keys.filter { it.startsWith("scr_seq") }) {
        walkForMart(entry, blocks)?.let { out[entry] = it }
      }
    }
    return out
  }

  /**
   * The people whose script stages a wild fight, `(map name, object index) -> (species, level)`,
   * out of mmo/STATIC_SITES.
   */
  private fun readStaticSites(): Map<Pair<String, Int>, Pair<Int, Int>> {
    val file = File(mmoDir, STATIC_SITES)
    if (!file.isFile) return emptyMap()
    val out = mutableMapOf<Pair<String, Int>, Pair<Int, Int>>()
    for (line in file.readLines()) {
      val m = STATIC_ROW.matchEntire(line.trim()) ?: continue
      out[m.groupValues[7] to m.groupValues[2].toInt()] =
          m.groupValues[3].toInt() to m.groupValues[4].toInt()
    }
    return out
  }

  private fun walkForMart(entry: String, blocks: Map<String, List<String>>): Int? {
    val seen = mutableSetOf<String>()
    var shop = 0
    var at: String? = entry
    while (at != null && seen.add(at) && seen.size <= MART_WALK_CAP) {
      var next: String? = null
      for (line in blocks[at].orEmpty()) {
        SHOP_VAR.matchEntire(line)?.let { shop = it.groupValues[1].toInt() }
        when {
          line == "CallStd std_pokemart" -> return 0
          line == "CallStd std_special_mart" -> return shop + 1
        }
        if (next == null) next = JUMP.matchEntire(line)?.groupValues?.get(1)
      }
      at = next
    }
    return null
  }

  /** The region's outdoor maps as one plane, cut out of the matrix they share. */
  private fun cutRegion(
      region: String,
      matrix: Matrix,
      outdoor: List<MapRow>,
      headers: Map<Int, Int>,
      land: List<ByteArray>,
      translation: IntArray,
      chunks: MutableMap<String, ParsedTerrainChunk>,
  ): ParsedTerrainMatrix {
    val owners = outdoor.map { it.header }.toSet()
    val edge = owners - HEADER_EVERYWHERE
    val grid = requireNotNull(matrix.headers) { "the shared matrix carries no header grid" }
    val cells =
        (0 until matrix.cols * matrix.rows).filter {
          grid[it] in edge && matrix.land[it] != EMPTY_CELL
        }
    require(cells.isNotEmpty()) { "no map of $region owns a cell of the shared matrix" }
    val x0 = cells.minOf { it % matrix.cols }
    val x1 = cells.maxOf { it % matrix.cols }
    val y0 = cells.minOf { it / matrix.cols }
    val y1 = cells.maxOf { it / matrix.cols }
    // The porter subtracts this same origin from every event coordinate, so a
    // door's tile is spoken in the cut's frame on both sides of the wire.
    cutOrigins[region] = (x0 * CHUNK_SIDE) to (y0 * CHUNK_SIDE)

    val names = mutableListOf<String?>()
    val altitudes = mutableListOf<Int>()
    val owned = mutableListOf<Int>()
    for (y in y0..y1) {
      for (x in x0..x1) {
        val i = y * matrix.cols + x
        val landId = matrix.land[i]
        if (landId == EMPTY_CELL || grid[i] !in owners) {
          names += null
          altitudes += 0
          owned += 0
          continue
        }
        names += chunkFor(landId, land, translation, chunks)
        altitudes += matrix.altitudes[i]
        owned += headers.getValue(grid[i])
      }
    }
    return ParsedTerrainMatrix(
        name = region.uppercase(),
        cols = x1 - x0 + 1,
        rows = y1 - y0 + 1,
        chunkSide = CHUNK_SIDE,
        chunks = names,
        altitudes = altitudes,
        headers = owned,
        hasGrass = names.any { it != null && chunks[it]?.hasGrass == true },
    )
  }

  /** A matrix that belongs to one map: every cell is its, and there is nothing to cut. */
  private fun carryWhole(
      name: String,
      matrix: Matrix,
      land: List<ByteArray>,
      translation: IntArray,
      chunks: MutableMap<String, ParsedTerrainChunk>,
  ): ParsedTerrainMatrix {
    val cells =
        (0 until matrix.cols * matrix.rows).map {
          if (matrix.land[it] == EMPTY_CELL) null
          else chunkFor(matrix.land[it], land, translation, chunks)
        }
    return ParsedTerrainMatrix(
        name = name.uppercase(),
        cols = matrix.cols,
        rows = matrix.rows,
        chunkSide = CHUNK_SIDE,
        chunks = cells,
        altitudes = (0 until matrix.cols * matrix.rows).map { matrix.altitudes[it] },
        // No header grid, and that is what a matrix of one map means. On the shared one the grid
        // is what `MovementService.changedZone` reads to swap the map under a player who walks
        // from one onto the next; a room has nothing to walk onto.
        headers = emptyList(),
        hasGrass = cells.any { it != null && chunks[it]?.hasGrass == true },
    )
  }

  /** The plane of one land data member, named for the member so two maps sharing it share one. */
  private fun chunkFor(
      landId: Int,
      land: List<ByteArray>,
      translation: IntArray,
      chunks: MutableMap<String, ParsedTerrainChunk>,
  ): String {
    val name = "LAND_$landId"
    chunks.getOrPut(name) {
      ParsedTerrainChunk(
          name, encodePlane(land[landId], translation), planeHasGrass(land[landId], translation))
    }
    return name
  }

  /** One ported map, as the wire addresses it. */
  private fun buildMap(
      row: MapRow,
      header: Int,
      matrix: ParsedTerrainMatrix,
      encounters: List<ParsedEncounterTable>,
      warps: List<ParsedWarp>,
      npcs: List<ParsedNpc>,
      fieldMoves: FieldMoveTables,
  ): ParsedMap =
      ParsedMap(
          // Only a cave map is asked, and a cave map never shares its matrix with anything but
          // another floor of the same cave, so the matrix's answer is this map's.
          hasGrass = matrix.hasGrass,
          // A headbutt tree's tiles are in the source's own frame, as its objects are.
          headbuttTrees =
              fieldMoves.headbutt?.trees?.map {
                val (ox, oy) = originOf(row)
                it.copy(x = it.x - ox, y = it.y - oy)
              } ?: emptyList(),
          rubble = fieldMoves.rubble,
          regionName = row.region,
          sourceName = row.name,
          groupName = "bank${header shr 8}",
          mapId = row.name.uppercase(),
          region = regionId,
          bank = header shr 8,
          index = header and 0xFF,
          width = matrix.cols * matrix.chunkSide,
          height = matrix.rows * matrix.chunkSide,
          paletteIdx1 = 0,
          paletteIdx2 = 0,
          musicId = 0,
          mapsecId = 0,
          borderTiles = emptyList(),
          blockData = "",
          behaviorData = "",
          encounters = encounters + fieldMoves.headbutt?.tables.orEmpty(),
          encounterForms = null,
          lighting = "Lighting.REGULAR",
          weather = "Weather.REGULAR_WEATHER",
          mapType = MAP_TYPES.getValue(row.kind),
          encounterType = "EncounterType.RANDOM",
          connections = emptyList(),
          warps = warps,
          visibleNpcs = npcs,
          bgEvents = emptyList(),
          onTransitionScript = "",
          healLocation = null,
          onFrameScripts = emptyList(),
          coordScripts = emptyList(),
          terrainRef = "$GENERATED_PACKAGE.$PORTED_PACKAGE.terrain.${matrix.name.uppercase()}",
      )

  /** One land data member's attribute plane, as the destination game would store it. */
  /**
   * Whether this plane holds a tall-grass tile, which is how a cave that is really a forest is told
   * from a cave.
   */
  private fun planeHasGrass(member: ByteArray, translation: IntArray): Boolean {
    val src = ByteBuffer.wrap(member).order(ByteOrder.LITTLE_ENDIAN)
    val plane = HG_LAND_HEADER + (src.getShort(HG_FIFTH_SIZE).toInt() and 0xFFFF)
    for (i in 0 until TERRAIN_SIZE step 2) {
      val behavior = translation[src.getShort(plane + i).toInt() and 0xFF]
      if (behavior < 0) continue
      val name = tileBehaviors.getOrNull(behavior) ?: continue
      if (name.endsWith("TALL_GRASS") || name.endsWith("LONG_GRASS")) {
        return true
      }
    }
    return false
  }

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

  /** One mmo/MAPS row, in the columns a region port reads. */
  private class MapRow(
      val name: String,
      val header: Int,
      val matrix: Int,
      /** The map's zone_event member, which is where its doors are written. */
      val events: Int,
      /** The map's entry in the source game's encounter data, or null for none. */
      val enc: String?,
      val kind: String,
      val region: String,
  )

  /**
   * The flags each map's arrival script leaves set: mmo/MAPSCENES, `hg <map> NAME=n ...`; a map
   * written down as `?` is absent, which reads as "every flag hides" the way the porter reads it.
   */
  private fun readScenes(): Map<String, Set<String>> {
    val file = File(mmoDir, MAPSCENES)
    if (!file.isFile) error("no ${file.path}; run tools/gen_mapscenes.py")
    val out = mutableMapOf<String, Set<String>>()
    for (line in file.readLines()) {
      val m = SCENE_ROW.matchEntire(line) ?: continue
      val rest = m.groupValues[2].substringBefore('#').trim()
      if (rest == "?") continue
      out[m.groupValues[1]] =
          rest
              .split(Regex("\\s+"))
              .filter { '=' in it && !it.startsWith("hide=") && !it.startsWith("show=") }
              .map { it.substringBefore('=') }
              .toSet()
    }
    return out
  }

  private class Walls(
      val set: MutableSet<String> = mutableSetOf(),
      val cleared: MutableSet<String> = mutableSetOf()
  )

  /** mmo/MAPWALLS: `hg <map> set|clear NAME=n`, the hand-written rows over the scene. */
  private fun readWalls(): Map<String, Walls> {
    val file = File(mmoDir, MAPWALLS)
    if (!file.isFile) return emptyMap()
    val out = mutableMapOf<String, Walls>()
    for (line in file.readLines()) {
      val m = WALL_ROW.matchEntire(line) ?: continue
      val walls = out.getOrPut(m.groupValues[1]) { Walls() }
      (if (m.groupValues[2] == "set") walls.set else walls.cleared).add(m.groupValues[3])
    }
    return out
  }

  /** One door of the source's own events, still in source-global coordinates. */
  private class SourceWarp(val x: Int, val z: Int, val destId: Int, val anchor: Int)

  /**
   * Every ported map's doors, off the decomp's zone_event JSON. The events member number is a
   * column of mmo/MAPS; the file that holds it is `NNN_<name>.json` in the decomp.
   */
  private fun readSourceWarps(rows: List<MapRow>): Map<Int, List<SourceWarp>> {
    val dir = File(decompDir, ZONE_EVENTS)
    if (!dir.isDirectory) {
      println("[maps] ported: no ${dir.path}, so no door is spoken server-side")
      return emptyMap()
    }
    val mapIds =
        Regex("""#define\s+MAP_(\w+)\s+(\d+)""")
            .findAll(File(decompDir, MAP_IDS).readText())
            .associate { it.groupValues[1] to it.groupValues[2].toInt() }
    val files =
        dir.listFiles()
            .orEmpty()
            .filter { it.name.endsWith(".json") }
            .associateBy { it.name.substringBefore('_') }
    val out = mutableMapOf<Int, List<SourceWarp>>()
    for (row in rows) {
      val file = files["%03d".format(row.events)] ?: continue
      val root = Json.parseToJsonElement(file.readText()).jsonObject
      out[row.header] =
          root["warps"]?.jsonArray.orEmpty().mapNotNull { element ->
            val warp = element.jsonObject
            val dest = warp["header"]?.jsonPrimitive?.contentOrNull ?: return@mapNotNull null
            val id = mapIds[dest.removePrefix("MAP_")] ?: return@mapNotNull null
            SourceWarp(
                x = warp["x"]?.jsonPrimitive?.intOrNull ?: return@mapNotNull null,
                z = warp["z"]?.jsonPrimitive?.intOrNull ?: return@mapNotNull null,
                destId = id,
                anchor = warp["anchor"]?.jsonPrimitive?.intOrNull ?: 0,
            )
          }
    }
    return out
  }

  /** Every ported map's trainers, off the same zone_event json the doors come from. */
  private fun readSourceTrainers(
      rows: List<MapRow>,
      marts: Map<String, Int>,
      apricornTrees: Map<Int, Int>,
      statics: Map<Pair<String, Int>, Pair<Int, Int>> = emptyMap(),
  ): Map<Int, List<ParsedNpc>> {
    val dir = File(decompDir, ZONE_EVENTS)
    if (!dir.isDirectory) return emptyMap()
    val scenes = readScenes()
    val walls = readWalls()
    val trainerIds =
        Regex("""#define\s+(TRAINER_\w+)\s+(\d+)""")
            .findAll(File(decompDir, TRAINER_IDS).readText())
            .associate { it.groupValues[1] to it.groupValues[2].toInt() }
    val files =
        dir.listFiles()
            .orEmpty()
            .filter { it.name.endsWith(".json") }
            .associateBy { it.name.substringBefore('_') }
    val out = mutableMapOf<Int, List<ParsedNpc>>()
    for (row in rows) {
      val file = files["%03d".format(row.events)] ?: continue
      val root = Json.parseToJsonElement(file.readText()).jsonObject
      val (ox, oy) = originOf(row)
      out[row.header] =
          root["objects"]?.jsonArray.orEmpty().mapIndexedNotNull { index, element ->
            val obj = element.jsonObject
            val script = obj["scriptId"]?.jsonPrimitive?.contentOrNull.orEmpty()
            val trainer = TRAINER_SCRIPT.find(script)?.groupValues?.get(1)
            val id = trainer?.let { trainerIds[it] } ?: 0
            // The two kinds of person the server has to know about on a ported map: one it
            // fights for, and one it sells for. Everybody else is the client's, drawn and
            // spoken by the package, and putting them here would be a row that answers
            // nothing.
            val label = SCRIPT_LABEL.find(script)?.groupValues?.get(1)
            val shelf = if (id == 0) label?.let { marts[it] } ?: -1 else -1
            // And a third: an Apricorn tree, whose pick the server answers. The row says which
            // tree and which fruit in its script string, the way a sign would name its script.
            val treeIndex =
                if (script == APRICORN_SCRIPT) obj["param0"]?.jsonPrimitive?.intOrNull else null
            val tree =
                treeIndex?.let {
                  "apricorn:$it:${apricornTrees[it] ?: error("apricorn tree $it grows nothing")}"
                }
            // And a fourth: a breakable rock, whose smash the server rolls, what the rubble
            // met, and what it left behind. The row only says where a rock stands; the press
            // and the shatter are the client's own routine.
            val rock = if (script == ROCK_SCRIPT) ROCK_MARK else null
            // And a fifth: a person whose script stages a wild fight, which this server deals
            // on the press and remembers per character (StaticEncounterService).
            val site = if (id == 0) statics[row.name to index] else null
            val static = site?.let { "$STATIC_MARK${it.first}:${it.second}" }
            if (id == 0 && shelf < 0 && tree == null && rock == null && static == null) {
              return@mapIndexedNotNull null
            }
            val flag = obj["eventFlag"]?.jsonPrimitive?.contentOrNull.orEmpty()
            // A rock's flag is a map-temp one: the record that it was smashed this visit, which
            // every map change clears. That is not a story hiding it, and the rock stays.
            val hiddenByScene =
                flag.isNotEmpty() &&
                    flag != "FLAG_NOTHING" &&
                    !(rock != null && flag.startsWith(MAP_TEMP_FLAG)) &&
                    (scenes[row.name]?.contains(flag) ?: true)
            val wall = walls[row.name]
            // A static site crosses whatever the story's end says about its flag, a visitor's
            // world offers every one of those fights once, and only a `set` row drops it.
            val hidden =
                if (static != null) wall?.set?.contains(flag) == true
                else
                    (hiddenByScene && wall?.cleared?.contains(flag) != true) ||
                        wall?.set?.contains(flag) == true
            if (hidden) return@mapIndexedNotNull null
            ParsedNpc(
                entityIdx = index,
                // Not a sprite of this game's. The client draws a ported map's people from the
                // package's own carried art, and the source's sprite ids are its own
                // namespace, so putting one here would be a foreign number in a field that
                // means something else.
                graphicsId = -1,
                x = (obj["x"]?.jsonPrimitive?.intOrNull ?: return@mapIndexedNotNull null) - ox,
                y = (obj["z"]?.jsonPrimitive?.intOrNull ?: return@mapIndexedNotNull null) - oy,
                elevation = 0,
                // The source's own movement id rides in `rawMovementId`; what a trainer does
                // before the fight is the client's business, and the server only needs to know
                // where they stand and how far they see.
                movementType = "MovementType.NONE",
                movementRangeX = 0,
                movementRangeY = 0,
                trainerType = obj["type"]?.jsonPrimitive?.intOrNull ?: 0,
                facing =
                    FACING.getOrElse(obj["facingDirection"]?.jsonPrimitive?.intOrNull ?: 0) {
                      "Direction.DOWN"
                    },
                script = static ?: tree ?: rock ?: "",
                // A won site hides behind a flag of this server's own: the character's story
                // flags carry it, and the client hides its copy behind the porter's band.
                hideFlag = if (static != null) "${row.region}/static:${row.name}:$index" else "",
                // A trainer's first data word is how far they see. A clerk's is not, so nobody
                // is challenged from behind a counter.
                sightRange = if (id == 0) 0 else obj["param0"]?.jsonPrimitive?.intOrNull ?: 0,
                trainerId = id,
                martShelf = shelf,
            )
          }
    }
    return out
  }

  /** A map's frame: the shared cut subtracts its region's origin, an interior is its own. */
  private fun originOf(row: MapRow): Pair<Int, Int> =
      if (row.matrix == SHARED_MATRIX) cutOrigins.getValue(row.region) else 0 to 0

  /**
   * A row's doors in the destination's own terms: the tile in the cut's frame, the target as the
   * ported header's wire address, and the landing as the destination's anchored door, the same
   * resolution the engine makes when it takes the door client-side, so the server's record of a
   * local warp and the client's landing agree tile for tile.
   */
  private fun portWarps(
      row: MapRow,
      sources: Map<Int, List<SourceWarp>>,
      byHeader: Map<Int, MapRow>,
  ): List<ParsedWarp> {
    val (ox, oy) = originOf(row)
    return sources[row.header].orEmpty().mapNotNull { warp ->
      val destRow = byHeader[warp.destId] ?: return@mapNotNull null
      val destWarps = sources[warp.destId].orEmpty()
      if (destWarps.isEmpty()) return@mapNotNull null
      val anchor = destWarps.getOrElse(warp.anchor) { destWarps[0] }
      val (dx, dy) = originOf(destRow)
      val target = headerBase + warp.destId
      ParsedWarp(
          x = warp.x - ox,
          y = warp.z - oy,
          elevation = 0,
          targetRegion = regionId,
          targetBank = target shr 8,
          targetMap = target and 0xFF,
          targetX = anchor.x - dx,
          targetY = anchor.z - dy,
          targetElevation = 0,
      )
    }
  }

  /** Every row of mmo/MAPS, which is generated from the same decomp. */
  private fun readMapRows(): List<MapRow> {
    val file = File(mmoDir, MAPS)
    require(file.isFile) { "no ${file.path}; run mmo/tools/gen_maps.py" }
    // Split rather than matched: a row is sixteen fields and a place name with
    // spaces in it, and a regular expression for that is one somebody has to
    // recount every time a column is added.
    val rows =
        file
            .readLines()
            .map { it.trim().split(SPACES, limit = COLUMNS + 1) }
            .filter { it.size > COLUMNS && it[0] == "hg" }
            .map {
              MapRow(
                  name = it[1],
                  header = it[2].toInt(),
                  matrix = it[4].toInt(),
                  events = it[5].toInt(),
                  enc = it[8].takeIf { e -> e != "-" },
                  kind = it[9],
                  region = it[10],
              )
            }
    require(rows.isNotEmpty()) {
      "${file.path} has no rows in the sixteen-column shape; run mmo/tools/gen_maps.py"
    }
    return rows
  }

  /** The wild monsters of every map that meets any, out of the source game's own encounter data. */
  private fun readEncounters(): Map<String?, List<ParsedEncounterTable>> {
    val file = File(decompDir, ENCOUNTERS)
    if (!file.isFile) {
      println("[maps] ported: no ${file.path}, so nothing is met in either region")
      return emptyMap()
    }
    val species = readSpecies()
    val out = mutableMapOf<String?, List<ParsedEncounterTable>>()
    val root = Json.parseToJsonElement(file.readText()).jsonObject
    for (element in root["encounters"]?.jsonArray.orEmpty()) {
      val obj = element.jsonObject
      val code = obj["map"]?.jsonPrimitive?.contentOrNull ?: continue
      val tables = mutableListOf<ParsedEncounterTable>()
      landTable(obj, species)?.let { tables += it }
      rangeTable(obj["surf"], species, "EncounterMethod.WATER", WATER_WEIGHTS)?.let { tables += it }
      rangeTable(obj["rock_smash"], species, "EncounterMethod.ROCK_SMASH", ROCK_WEIGHTS)?.let {
        tables += it
      }
      val fishing = obj["fishing"]?.jsonObject
      rangeTable(fishing?.get("old_rod"), species, "EncounterMethod.OLD_ROD", WATER_WEIGHTS)?.let {
        tables += it
      }
      rangeTable(fishing?.get("good_rod"), species, "EncounterMethod.GOOD_ROD", ROD_WEIGHTS)?.let {
        tables += it
      }
      rangeTable(fishing?.get("super_rod"), species, "EncounterMethod.SUPER_ROD", ROD_WEIGHTS)
          ?.let { tables += it }
      if (tables.isNotEmpty()) out[code] = tables
    }
    println("[maps] ported: ${out.size} encounter entries, ${out.values.sumOf { it.size }} tables")
    return out
  }

  /** Grass, and the two swaps that make it a different table after dawn. */
  /** One side of a version-exclusive slot. */
  private fun oneVersion(element: kotlinx.serialization.json.JsonElement?) =
      if (element is JsonObject && VERSION in element) element[VERSION] else element

  private fun landTable(obj: JsonObject, species: Map<String, Int>): ParsedEncounterTable? {
    val land = obj["land"]?.jsonObject ?: return null
    val rate = land["rate"]?.jsonPrimitive?.intOrNull ?: 0
    if (rate == 0) return null
    val mons = land["mons"]?.jsonArray.orEmpty().map { it.jsonObject }
    // A slot's species is three names, morning, day, night, on a map whose grass changes
    // with the hour, and one name on a map whose does not. Both shapes are in this file and
    // the one-name form is the same species at every hour rather than a slot with no night.
    fun speciesAt(index: Int, whenOf: String): Int? {
      val at = oneVersion(mons.getOrNull(index)?.get("species")) ?: return null
      val name =
          when (at) {
            is JsonObject -> oneVersion(at[whenOf])?.jsonPrimitive?.contentOrNull
            is JsonPrimitive -> at.contentOrNull
            else -> null
          } ?: return null
      return species[name] ?: error("unknown species $name in ${obj["map"]}")
    }
    // A level is one number on most slots and a {min, max} on some. Both are
    // in this file and the one-number form is a slot whose range is a point.
    fun levelsAt(index: Int): Pair<Int, Int>? {
      val at = oneVersion(mons.getOrNull(index)?.get("level")) ?: return null
      return when (at) {
        is JsonPrimitive -> at.intOrNull?.let { it to it }
        is JsonObject ->
            (oneVersion(at["min"])?.jsonPrimitive?.intOrNull ?: 0) to
                (oneVersion(at["max"])?.jsonPrimitive?.intOrNull ?: 0)
        else -> null
      }
    }
    val slots =
        mons.indices.mapNotNull { i ->
          val id = speciesAt(i, "morn") ?: return@mapNotNull null
          val (lo, hi) = levelsAt(i) ?: return@mapNotNull null
          val weight = LAND_WEIGHTS.getOrNull(i) ?: return@mapNotNull null
          if (id == 0) null else ParsedEncounterSlot(id, lo, hi, weight)
        }
    if (slots.isEmpty()) return null
    val overrides =
        listOf("day" to "EncounterVariant.DAY", "nite" to "EncounterVariant.NIGHT").mapNotNull {
            (key, variant) ->
          val moved =
              mons.indices.filter { i ->
                val a = speciesAt(i, "morn")
                val b = speciesAt(i, key)
                a != null && b != null && a != b
              }
          if (moved.isEmpty()) null
          else ParsedEncounterOverride(variant, moved, moved.map { i -> speciesAt(i, key) ?: 0 })
        }
    return ParsedEncounterTable("EncounterMethod.LAND", rate, slots, overrides)
  }

  /** Surf, Rock Smash and a rod: a rate, and slots that name a level range. */
  private fun rangeTable(
      element: kotlinx.serialization.json.JsonElement?,
      species: Map<String, Int>,
      method: String,
      weights: List<Int>,
  ): ParsedEncounterTable? {
    val obj = element?.jsonObject ?: return null
    val rate = obj["rate"]?.jsonPrimitive?.intOrNull ?: 0
    if (rate == 0) return null
    val slots =
        obj["mons"]?.jsonArray.orEmpty().mapIndexedNotNull { index, mon ->
          val entry = mon.jsonObject
          // Surfing carries the same two shapes grass does: one species, or three that change
          // with the hour. A rod's table has no hour, and where one of these does the morning
          // name is the table's, the swaps this model has are the grass table's own.
          val at = oneVersion(entry["species"]) ?: return@mapIndexedNotNull null
          val name =
              when (at) {
                is JsonObject -> oneVersion(at["morn"])?.jsonPrimitive?.contentOrNull
                is JsonPrimitive -> at.contentOrNull
                else -> null
              } ?: return@mapIndexedNotNull null
          val id = species[name] ?: error("unknown species $name")
          val level = oneVersion(entry["level"])
          val weight = weights.getOrNull(index) ?: return@mapIndexedNotNull null
          val lo: Int
          val hi: Int
          when (level) {
            is JsonPrimitive -> {
              lo = level.intOrNull ?: 0
              hi = lo
            }
            is JsonObject -> {
              lo = oneVersion(level["min"])?.jsonPrimitive?.intOrNull ?: 0
              hi = oneVersion(level["max"])?.jsonPrimitive?.intOrNull ?: 0
            }
            else -> return@mapIndexedNotNull null
          }
          if (id == 0) null
          else ParsedEncounterSlot(speciesId = id, minLevel = lo, maxLevel = hi, weight = weight)
        }
    return if (slots.isEmpty()) null else ParsedEncounterTable(method, rate, slots)
  }

  /** `SPECIES_PIDGEY -> 16`, out of the source game's own constants. */
  /** What a field move meets on one map: its headbutt trees and tables, and its rubble. */
  private class FieldMoveTables(val headbutt: HeadbuttEntry?, val rubble: ParsedRubble?)

  /** A map's headbutt trees and the three tables they roll, keyed by the source header. */
  private class HeadbuttEntry(
      val tables: List<ParsedEncounterTable>,
      val trees: List<ParsedHeadbuttTree>,
  )

  /** What lives in the trees, out of the source game's own headbutt archive. */
  private fun readHeadbutt(): Map<Int, HeadbuttEntry> {
    val file = File(decompDir, HEADBUTT_TABLES)
    if (!file.isFile) {
      println("[maps] ported: no ${file.path}, so nothing lives in the trees")
      return emptyMap()
    }
    val species = readSpecies()
    val out = mutableMapOf<Int, HeadbuttEntry>()
    val root = Json.parseToJsonElement(file.readText()).jsonObject
    root["tables"]?.jsonArray.orEmpty().forEachIndexed { header, element ->
      val obj = element.jsonObject
      val trees = mutableListOf<ParsedHeadbuttTree>()
      val regular = obj["Trees"]?.jsonArray.orEmpty()
      regular.forEachIndexed { index, group ->
        group.jsonArray.forEach { tile ->
          val t = tile.jsonObject
          trees +=
              ParsedHeadbuttTree(
                  x = t["x"]?.jsonPrimitive?.intOrNull ?: return@forEach,
                  y = t["y"]?.jsonPrimitive?.intOrNull ?: return@forEach,
                  tree = index,
                  secret = false)
        }
      }
      obj["SecretTrees"]?.jsonArray.orEmpty().forEachIndexed { index, group ->
        group.jsonArray.forEach { tile ->
          val t = tile.jsonObject
          trees +=
              ParsedHeadbuttTree(
                  x = t["x"]?.jsonPrimitive?.intOrNull ?: return@forEach,
                  y = t["y"]?.jsonPrimitive?.intOrNull ?: return@forEach,
                  tree = regular.size + index,
                  secret = true)
        }
      }
      if (trees.isEmpty()) return@forEachIndexed
      val tables =
          HEADBUTT_TABLE_KEYS.mapNotNull { (key, method) ->
            headbuttTable(obj[key], species, method)
          }
      if (tables.isEmpty()) return@forEachIndexed
      out[header] = HeadbuttEntry(tables, trees)
    }
    println(
        "[maps] ported: ${out.size} maps with headbutt trees, " +
            "${out.values.sumOf { it.trees.size }} tree tiles")
    return out
  }

  /** Which of a map's trees are rare for a given trainer, out of the source's own lookup tables. */
  private fun readHeadbuttLuts(): Map<Int, List<List<Int>>> {
    val file = File(decompDir, HEADBUTT_SOURCE)
    if (!file.isFile) return emptyMap()
    val text = file.readText()
    val out = mutableMapOf<Int, List<List<Int>>>()
    for ((count, suffix) in HEADBUTT_LUTS) {
      val body =
          Regex(
                  """sRareTreeLUT_$suffix\[\]\[(\d+)\]\s*=\s*\{(.*?)\};""",
                  RegexOption.DOT_MATCHES_ALL)
              .find(text) ?: error("$HEADBUTT_SOURCE has no sRareTreeLUT_$suffix")
      val width = body.groupValues[1].toInt()
      require(width == count) { "sRareTreeLUT_$suffix is $width wide, not $count" }
      val rows =
          Regex("""\{([^{}]*)\}""").findAll(body.groupValues[2]).map { row ->
            TREE_TYPE.findAll(row.groupValues[1]).map { TREE_TYPES.getValue(it.value) }.toList()
          }
      val table = rows.toList()
      require(table.isNotEmpty() && table.all { it.size == count }) {
        "sRareTreeLUT_$suffix has a row that is not $count wide"
      }
      out[count] = table
    }
    return out
  }

  private fun headbuttTable(
      element: kotlinx.serialization.json.JsonElement?,
      species: Map<String, Int>,
      method: String,
  ): ParsedEncounterTable? {
    val slots =
        element?.jsonArray.orEmpty().mapIndexedNotNull { index, mon ->
          val entry = mon.jsonObject
          val at = entry["species"]
          val name =
              when (at) {
                is JsonObject -> at[HEADBUTT_VERSION]?.jsonPrimitive?.contentOrNull
                is JsonPrimitive -> at.contentOrNull
                else -> null
              } ?: return@mapIndexedNotNull null
          val id = species[name] ?: error("unknown species $name in $HEADBUTT_TABLES")
          val weight = HEADBUTT_WEIGHTS.getOrNull(index) ?: return@mapIndexedNotNull null
          val lo = entry["minLevel"]?.jsonPrimitive?.intOrNull ?: 0
          val hi = entry["maxLevel"]?.jsonPrimitive?.intOrNull ?: lo
          if (id == 0) null
          else ParsedEncounterSlot(speciesId = id, minLevel = lo, maxLevel = hi, weight = weight)
        }
    return if (slots.isEmpty()) null else ParsedEncounterTable(method, HEADBUTT_RATE, slots)
  }

  /** What a smashed rock leaves behind, per map, out of the source's own archive and source. */
  private fun readRubble(): Map<Int, ParsedRubble> {
    val narc = File(decompDir, ROCK_SMASH_ODDS)
    val source = File(decompDir, ROCK_SMASH_SOURCE)
    val header = File(decompDir, ROCK_SMASH_KINDS)
    if (!narc.isFile || !source.isFile || !header.isFile) {
      println("[maps] ported: no rock smash odds under ${decompDir.path}; rubble is only rubble")
      return emptyMap()
    }
    val kinds =
        ROCK_SMASH_KIND.findAll(header.readText()).associate {
          it.groupValues[2].toInt() to it.groupValues[1]
        }
    val text = source.readText()
    val tables =
        kinds.mapValues { (_, name) ->
          // ROCK_SMASH_TYPE_RUINS_OF_ALPH names sRockSmashItems_RuinsOfAlph.
          val table =
              name.removePrefix("ROCK_SMASH_TYPE_").lowercase().split('_').joinToString("") {
                it.replaceFirstChar(Char::uppercase)
              }
          val body =
              Regex(
                      """#ifdef HEARTGOLD\s*static const u16 sRockSmashItems_$table\[\]\s*=\s*\{(.*?)\}""",
                      RegexOption.DOT_MATCHES_ALL)
                  .find(text)
                  ?.groupValues
                  ?.get(1)
                  ?: Regex(
                          """static const u16 sRockSmashItems_$table\[\]\s*=\s*\{(.*?)\}""",
                          RegexOption.DOT_MATCHES_ALL)
                      .find(text)
                      ?.groupValues
                      ?.get(1)
                  ?: error("$ROCK_SMASH_SOURCE has no sRockSmashItems_$table")
          val items =
              ITEM_NAME.findAll(body).map { "Items." + it.value.removePrefix("ITEM_") }.toList()
          require(items.size == ROCK_SMASH_TABLE_SIZE) {
            "sRockSmashItems_$table holds ${items.size} items, and the draw wants $ROCK_SMASH_TABLE_SIZE"
          }
          items
        }
    val out = mutableMapOf<Int, ParsedRubble>()
    narcMembers(narc).forEachIndexed { header, member ->
      if (member.size < 4) return@forEachIndexed
      val buf = ByteBuffer.wrap(member).order(ByteOrder.LITTLE_ENDIAN)
      val odds = buf.getShort(0).toInt() and 0xFFFF
      val kind = buf.getShort(2).toInt() and 0xFFFF
      if (odds == 0) return@forEachIndexed
      val items = tables[kind] ?: error("rock smash row $header names item table $kind")
      out[header] = ParsedRubble(odds, items)
    }
    println("[maps] ported: ${out.size} maps whose rubble holds something")
    return out
  }

  /**
   * The members of an archive committed to the decompilation as the built file: a `BTAF` table of
   * (start, end) pairs after the header, and the bytes after the `GMIF` chunk's own header.
   */
  private fun narcMembers(file: File): List<ByteArray> {
    val bytes = file.readBytes()
    val buf = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
    require(String(bytes, 0, 4, Charsets.US_ASCII) == "NARC") { "${file.path} is not a NARC" }
    require(String(bytes, 0x10, 4, Charsets.US_ASCII) == "BTAF") { "${file.path}: no BTAF" }
    val count = buf.getInt(0x18)
    val btnfAt = 0x10 + buf.getInt(0x14)
    val gmifAt = btnfAt + buf.getInt(btnfAt + 4)
    require(String(bytes, gmifAt, 4, Charsets.US_ASCII) == "GMIF") { "${file.path}: no GMIF" }
    val dataAt = gmifAt + 8
    return List(count) { i ->
      val start = buf.getInt(0x1C + 8 * i)
      val end = buf.getInt(0x20 + 8 * i)
      bytes.copyOfRange(dataAt + start, dataAt + end)
    }
  }

  private fun readSpecies(): Map<String, Int> {
    val file = File(decompDir, SPECIES)
    require(file.isFile) { "no ${file.path}; the encounter species have no numbers without it" }
    return SPECIES_ROW.findAll(file.readText()).associate {
      it.groupValues[1] to it.groupValues[2].toInt()
    }
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
    private const val GENERATED_PACKAGE = "de.fiereu.openmmo.maps.generated"

    /**
     * One terrain package for every ported region, because the planes are one parse and
     * `MapsRenderer` writes a region's terrain once. The MAPS themselves keep their own region's
     * package, which is what a name in /handbook and /warp is looked up under.
     */
    private const val PORTED_PACKAGE = "ported"

    private const val LAND_DATA = "files/a/0/6/5"
    private const val ENCOUNTERS = "files/fielddata/encountdata/gs_enc_data.json"
    private const val SPECIES = "include/constants/species.h"
    private const val MAP_MATRIX_DIR = "files/fielddata/mapmatrix/map_matrix"
    private const val MAPS = "MAPS"
    private const val MAPSCENES = "MAPSCENES"
    private const val MAPWALLS = "MAPWALLS"
    private val SCENE_ROW = Regex("""^hg\s+(\S+)\s+(.*)$""")
    private val WALL_ROW = Regex("""^hg\s+(\S+)\s+(set|clear)\s+(\w+)=\S+.*$""")
    private const val TERRAIN_MAP = "TERRAIN_MAP"

    /** Matrix 0 is the one every outdoor map of both regions shares. */
    private const val SHARED_MATRIX = 0

    /** `MAP_HEADER_EVERYWHERE`: the sea between the places, on both sides. */
    private const val HEADER_EVERYWHERE = 0

    /** Which of mmo/MAPS's kind column a port carries, and what this game calls it. */
    private val MAP_TYPES =
        mapOf(
            "city_town" to "MapType.CITY",
            "route" to "MapType.ROUTE",
            "cave" to "MapType.UNDERGROUND",
            "interior" to "MapType.INSIDE",
        )

    private val PORTED_KINDS = MAP_TYPES.keys

    private const val CHUNK_SIDE = 32
    private const val TERRAIN_SIZE = 0x800
    private const val ZONE_EVENTS = "files/fielddata/eventdata/zone_event"
    private const val MAP_IDS = "include/constants/maps.h"
    private const val TRAINER_IDS = "include/constants/trainers.h"
    private const val ITEM_IDS = "include/constants/items.h"
    private const val MART_SOURCE = "src/scrcmd_mart.c"
    // The Apricorn trees: one routine for all of them, and a table compiled into the game that
    // says which fruit grows on the tree whose index an object carries in its first data word.
    private const val APRICORN_SOURCE = "src/apricorn_tree_sys.c"
    private const val APRICORN_KINDS = "include/constants/apricorns.h"
    private const val APRICORN_SCRIPT = "std_apricorn_tree"
    private val APRICORN_TABLE =
        Regex("""sTreeApricorns\[[^\]]*\]\s*=\s*\{(.*?)\}\s*;""", RegexOption.DOT_MATCHES_ALL)
    private val APRICORN_KIND = Regex("""#define\s+(APRICORN_\w+)\s+(\d+)""")
    private val APRICORN_NAME = Regex("""APRICORN_\w+""")
    // The rocks and the trees a field move is used on. A rock is an object routed to the
    // source's rock-smash routine; the server keeps it as an npc marked ROCK_MARK so the smash
    // can be checked against a rock the player faces. The trees are not objects at all.
    private const val ROCK_SCRIPT = "std_field_rock_smash"
    private const val ROCK_MARK = "rock"
    private const val MAP_TEMP_FLAG = "FLAG_MAPTEMP_"
    private const val ROCK_SMASH_ODDS = "files/a/2/5/3"
    private const val ROCK_SMASH_SOURCE = "src/field/rock_smash_item.c"
    private const val ROCK_SMASH_KINDS = "include/field/rock_smash_item.h"
    private const val ROCK_SMASH_TABLE_SIZE = 8
    private val ROCK_SMASH_KIND = Regex("""#define\s+(ROCK_SMASH_TYPE_\w+)\s+(\d+)""")
    private val ITEM_NAME = Regex("""ITEM_\w+""")
    private const val HEADBUTT_TABLES = "files/arc/headbutt.json"
    private const val HEADBUTT_SOURCE = "src/field/headbutt.c"
    private val HEADBUTT_LUTS = listOf(1 to "1", 2 to "2", 3 to "3", 4 to "4", 5 to "5Plus")
    private val TREE_TYPE = Regex("""TREETYPE_\w+""")
    private val TREE_TYPES =
        mapOf(
            "TREETYPE_COMMON" to 0,
            "TREETYPE_RARE" to 1,
            "TREETYPE_SECRET" to 2,
            "TREETYPE_NONE" to -1)
    private const val HEADBUTT_VERSION = "gold"
    private const val HEADBUTT_RATE = 100
    private val HEADBUTT_TABLE_KEYS =
        listOf(
            "CommonMons" to "EncounterMethod.HEADBUTT_COMMON",
            "RareMons" to "EncounterMethod.HEADBUTT_RARE",
            "SecretMons" to "EncounterMethod.HEADBUTT_SECRET",
        )
    private const val MART_COMMON_TABLE = "_020FBF22"
    private const val MART_SPECIAL_TABLE = "_0210FA3C"
    private const val FIELD_SCRIPTS = "files/fielddata/script/scr_seq"
    private const val MART_WALK_CAP = 64
    /** The mark on a static site's script: `static:<species>:<level>`. */
    const val STATIC_MARK = "static:"
    private const val STATIC_SITES = "STATIC_SITES"
    // header oid species level flag src_flag name
    private val STATIC_ROW = Regex("""(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\S+)""")
    private val LABEL = Regex("""(\w+):""")
    private val SCRIPT_LABEL = Regex("""_EV_(scr_seq_\w+)""")
    private val SHOP_VAR = Regex("""SetVar VAR_SPECIAL_x8004, *(\d+)""")
    private val JUMP = Regex("""(?:GoTo|GoToIf\w*|Call|CallIf\w*) +(?:\w+, *)?(_\w+)""")
    private val TRAINER_SCRIPT = Regex("""std_trainer\w*\((TRAINER_\w+)""")
    // The source's facing numbering, which is its movement table's first four: up, down, left,
    // right. A person whose facing is anything else looks down, as this game's own default does.
    private val FACING =
        listOf("Direction.UP", "Direction.DOWN", "Direction.LEFT", "Direction.RIGHT")
    private const val HG_LAND_HEADER = 0x14
    private const val HG_FIFTH_SIZE = 0x12
    private const val COLLISION_BIT = 0x8000
    private const val CARRIED_BITS = 0x80FF
    private const val EMPTY_CELL = 0xFFFF

    /**
     * How many fields a row has before the place name, which has spaces in it: the `hg` tag, name,
     * header, area bank, matrix, events, scripts, msg, enc, kind, region, bgm, battle background,
     * arrival-script header, area icon, camera type.
     */
    private const val COLUMNS = 16
    private val SPACES = Regex("""\s+""")
    private val TERRAIN_ROW = Regex("""^0x([0-9A-F]{2})\s+0x([0-9A-F]{2})\s+(\S+)""")
    private val SPECIES_ROW = Regex("""#define\s+(SPECIES_\w+)\s+(\d+)""")

    /** Which side of a version-exclusive slot a server holds. */
    private const val VERSION = "HEARTGOLD"

    /**
     * The fixed Gen 4 slot weights, the same in both games: a grass table is twelve slots, water
     * and the old rod five, and the other two rods five of their own.
     */
    private val LAND_WEIGHTS = listOf(20, 20, 10, 10, 10, 10, 5, 5, 4, 4, 1, 1)
    private val WATER_WEIGHTS = listOf(60, 30, 5, 4, 1)
    private val ROD_WEIGHTS = listOf(40, 40, 15, 4, 1)
    // A smashed rock rolls two slots (`EncounterSlot_WildMonSlotRoll_RockSmash`: the second one
    // in five), and a headbutted tree six (`EncounterSlot_WildMonSlotRoll_Headbutt`).
    private val ROCK_WEIGHTS = listOf(80, 20)
    private val HEADBUTT_WEIGHTS = listOf(50, 15, 15, 10, 5, 5)
  }
}
