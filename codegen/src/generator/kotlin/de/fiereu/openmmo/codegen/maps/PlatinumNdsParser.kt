package de.fiereu.openmmo.codegen.maps

import java.io.File
import java.util.Base64
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.int
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

/** Reads Sinnoh out of the pokeplatinum decomp. */
class PlatinumNdsParser(
    private val rootDir: File,
    private val region: RegionConstants,
) {

  private val json = Json { ignoreUnknownKeys = true }

  private val fieldDir = File(rootDir, "res/field")

  // Encounter archives by id: several map headers share one, and the ones with no fixed tables
  // should say so once rather than once per header.
  private val archives = mutableMapOf<String, JsonObject?>()

  // Init script archives by id, for the same reason: 113 headers name the empty one.
  private val initScripts = mutableMapOf<String, InitScripts>()

  fun parseAll(): ParsedRegion {
    val defines = readTerrainDefines()
    val terrainOffset = defines.getValue("TERRAIN_ATTRIBUTES_OFFSET")
    val terrainSize = defines.getValue("TERRAIN_ATTRIBUTES_SIZE")
    val collisionShift = defines.getValue("TERRAIN_ATTRIBUTES_COLLISION_SHIFT")
    val behaviorMask = defines.getValue("TERRAIN_ATTRIBUTES_TILE_BEHAVIOR_MASK")
    require(collisionShift == 15 && behaviorMask == 0xFF) {
      "TerrainPlane decodes bit 15 and the low byte; map.h now says bit $collisionShift and " +
          "mask 0x${behaviorMask.toString(16)}"
    }
    val chunkSide = chunkSideOf(terrainSize)

    val headerIds = readHeaderIds()
    val headerFields = readHeaderFields()
    val speciesIds = readEnumTable("species.txt")
    val graphicsIds = readEnumTable("object_events_gfx.txt")
    val movementIds = readEnumTable("movement_types.txt")
    val trainerTypes = readEnumTable("trainer_types.txt")
    val trainerIds = readEnumTable("trainers.txt")
    val behaviors = readTileBehaviors()

    val chunks = readTerrainChunks(terrainOffset, terrainSize)
    val matrices = mutableMapOf<String, ParsedTerrainMatrix>()
    val events = mutableMapOf<String, JsonObject>()

    // Two passes, because a warp steps out where its destination header's own warp list puts it and
    // so no map can be built until every map's events are read.
    val rows =
        headerIds.mapNotNull { (name, id) ->
          val fields = headerFields[name] ?: return@mapNotNull null
          val matrixName = fields["mapMatrixID"] ?: return@mapNotNull null
          matrices.getOrPut(matrixName) { readMatrix(matrixName, chunkSide, chunks, headerIds) }
          events[name] = readEvents(fields) ?: return@mapNotNull null
          Triple(name, id, fields)
        }
    val tables =
        Tables(headerIds, events, speciesIds, graphicsIds, movementIds, trainerTypes, trainerIds)
    val maps =
        rows.map { (name, id, fields) ->
          buildParsedMap(
              name = name,
              headerId = id,
              fields = fields,
              matrix = matrices.getValue(fields.getValue("mapMatrixID")),
              events = events.getValue(name),
              tables = tables,
          )
        }

    println(
        "[maps] ${region.name}: ${maps.size} headers over ${matrices.size} matrices, " +
            "${chunks.size} land data chunks, ${maps.sumOf { it.warps.size }} warps, " +
            "${maps.sumOf { it.visibleNpcs.size }} objects")
    return ParsedRegion(
        maps = maps,
        terrainChunks = chunks.values.sortedBy { it.name },
        terrainMatrices = matrices.values.sortedBy { it.name },
        tileBehaviors = behaviors,
        terrainPackage = "$GENERATED_PACKAGE.${region.name}.terrain",
    )
  }

  // ---------------------------------------------------------------- sources

  private fun readTerrainDefines(): Map<String, Int> {
    val file = File(rootDir, "include/constants/field/map.h")
    val text = file.readText()
    return TERRAIN_DEFINES.associateWith { name ->
      val match = Regex("""^#define\s+$name\s+(0[xX][0-9a-fA-F]+|\d+)\s*$""", RegexOption.MULTILINE)
      val value =
          match.find(text)?.groupValues?.get(1)
              ?: error(
                  "${file.path} no longer defines $name; the terrain plane cannot be decoded " +
                      "without it and a guess here is not worth the bytes it saves")
      if (value.startsWith("0x", ignoreCase = true)) value.substring(2).toInt(16) else value.toInt()
    }
  }

  private fun chunkSideOf(terrainSize: Int): Int {
    val tiles = terrainSize / 2
    val side = Math.round(Math.sqrt(tiles.toDouble())).toInt()
    require(side * side == tiles) {
      "a $terrainSize byte terrain plane is $tiles tiles, not square"
    }
    return side
  }

  private fun readHeaderIds(): Map<String, Int> = PlatinumHeaders.ids(rootDir)

  private fun readHeaderFields(): Map<String, Map<String, String>> = PlatinumHeaders.fields(rootDir)

  /**
   * An enum dump under `generated/`, whose line order is the value. A row that names its own value
   * (`NAME = 4096`) keeps that value and still holds its line, so neither form shifts the other.
   */
  private fun readEnumTable(name: String): Map<String, Int> =
      File(rootDir, "generated/$name")
          .readLines()
          .map { it.trim() }
          .withIndex()
          .filter { (_, line) -> line.isNotEmpty() }
          .associate { (index, line) ->
            if ('=' !in line) {
              line to index
            } else {
              val (key, value) = line.split('=', limit = 2)
              key.trim() to Integer.decode(value.trim())
            }
          }

  /** The behaviour enum, mapped onto the normalized set the server acts on. */
  private fun readTileBehaviors(): List<String> {
    val file = File(rootDir, "include/constants/field/map_tile_behaviors.h")
    val names =
        Regex("""^\s*(TILE_BEHAVIOR_\w+)\s*(?:=\s*0)?,""", RegexOption.MULTILINE)
            .findAll(file.readText())
            .map { it.groupValues[1] }
            .toList()
    require(names.firstOrNull() == "TILE_BEHAVIOR_NONE") {
      "${file.path} no longer starts at TILE_BEHAVIOR_NONE, so its order is not its value"
    }
    val water = readWaterBehaviors(names.toSet())
    return List(256) { value ->
      val name = names.getOrNull(value) ?: return@List "TileBehavior.NORMAL"
      PLATINUM_TILE_BEHAVIORS[name]?.let { "TileBehavior.$it" }
          ?: if (name in water) "TileBehavior.SURFABLE_WATER" else "TileBehavior.NORMAL"
    }
  }

  /** Which behaviours the game calls water, read out of the table it reads. */
  private fun readWaterBehaviors(known: Set<String>): Set<String> {
    val file = File(rootDir, "src/map_tile_behavior.c")
    val text = file.readText()
    val table =
        Regex("""sTileBehaviorFlags\[]\s*=\s*\{(.*?)\n};""", RegexOption.DOT_MATCHES_ALL)
            .find(text)
            ?.groupValues
            ?.get(1)
    require(table != null) { "${file.path} has no sTileBehaviorFlags table" }
    val surfable =
        Regex("""\[(TILE_BEHAVIOR_\w+)]\s*=\s*([^,]+),""")
            .findAll(table)
            .filter { it.groupValues[2].contains("SURFABLE") }
            .map { it.groupValues[1] }
            .toSet()
    require(surfable.isNotEmpty()) { "${file.path} flags no behaviour surfable" }
    val bridges =
        Regex(
                """BOOL TileBehavior_IsBridgeOverWater\(u8 behavior\)\s*\{(.*?)\n}""",
                RegexOption.DOT_MATCHES_ALL)
            .find(text)
            ?.groupValues
            ?.get(1)
            ?.let { Regex("""TILE_BEHAVIOR_\w+""").findAll(it).map { m -> m.value }.toSet() }
    require(!bridges.isNullOrEmpty()) { "${file.path} has no TileBehavior_IsBridgeOverWater" }
    val unknown = (surfable + bridges) - known
    require(unknown.isEmpty()) { "${file.path} names behaviours the enum does not: $unknown" }
    return surfable - bridges
  }

  private fun readTerrainChunks(offset: Int, size: Int): Map<String, ParsedTerrainChunk> {
    val dir = File(fieldDir, "maps/data")
    val files = dir.listFiles { f: File -> f.name.matches(Regex("""map_data_\d+\.bin""")) }
    require(!files.isNullOrEmpty()) { "no land data under ${dir.path}" }
    return files
        .sortedBy { it.name }
        .associate { file ->
          val blob = file.readBytes()
          require(blob.size >= offset + size) { "${file.name} is too short for its terrain plane" }
          val key = file.name.removePrefix("map_data_").removeSuffix(".bin")
          val name = "MAP_$key"
          name to
              ParsedTerrainChunk(
                  name = name,
                  encoded =
                      Base64.getEncoder().encodeToString(blob.copyOfRange(offset, offset + size)),
              )
        }
  }

  private fun readMatrix(
      name: String,
      chunkSide: Int,
      chunks: Map<String, ParsedTerrainChunk>,
      headerIds: Map<String, Int>,
  ): ParsedTerrainMatrix {
    val obj = json.parseToJsonElement(File(fieldDir, "matrices/$name.json").readText()).jsonObject
    val grid = obj.getValue("maps").jsonArray.map { it.jsonArray }
    val altitudes = obj["altitudes"]?.jsonArray?.map { it.jsonArray }
    val headers = obj["headers"]?.jsonArray?.map { it.jsonArray }
    val rows = grid.size
    val cols = grid.first().size
    return ParsedTerrainMatrix(
        name = name,
        cols = cols,
        rows = rows,
        chunkSide = chunkSide,
        chunks =
            grid.flatMap { row ->
              row.map { cell ->
                val id = cell.jsonPrimitive.content
                if (id == EMPTY_CELL) null
                else chunks[id]?.name ?: error("$name places $id, which has no land data file")
              }
            },
        altitudes =
            altitudes?.flatMap { row -> row.map { it.jsonPrimitive.int } }
                ?: List(rows * cols) { 0 },
        // Which header owns a cell is what tells the server the player has walked from one map
        // into the next: an overworld map is a stretch of the matrix, not a rectangle with edges.
        headers =
            headers?.flatMap { row ->
              row.map { cell ->
                val header = cell.jsonPrimitive.content
                headerIds[header] ?: error("$name places $header, which is not a map header")
              }
            } ?: List(rows * cols) { 0 },
    )
  }

  private fun readEvents(fields: Map<String, String>): JsonObject? {
    val archive = fields["eventsArchiveID"] ?: return null
    val file = File(fieldDir, "events/$archive.json")
    if (!file.exists()) return null
    return json.parseToJsonElement(file.readText()).jsonObject
  }

  /** What a map runs on arrival: the ON_TRANSITION script and the ON_FRAME table. */
  private class InitScripts(val onTransition: String, val onFrame: List<ParsedFrameScript>) {
    companion object {
      val NONE = InitScripts("", emptyList())
    }
  }

  /** A map's arrival scripts, out of the archive its header names. */
  private fun readInitScripts(fields: Map<String, String>): InitScripts {
    val archive = fields["initScriptsArchiveID"] ?: return InitScripts.NONE
    return initScripts.getOrPut(archive) {
      val file = File(fieldDir, "scripts/$archive.s")
      if (!file.exists()) return@getOrPut InitScripts.NONE
      val text = file.readText()
      val transition =
          ON_TRANSITION.find(text)?.groupValues?.get(1)?.let { scriptId(it, file) } ?: ""
      InitScripts(transition, readFrameTable(text, file))
    }
  }

  /**
   * The ON_FRAME table the entry points at, in file order, because the server runs the first row
   * whose var matches and the decomp does the same.
   */
  private fun readFrameTable(text: String, file: File): List<ParsedFrameScript> {
    val label = ON_FRAME_TABLE.find(text)?.groupValues?.get(1) ?: return emptyList()
    val start = Regex("""^$label:\s*$""", RegexOption.MULTILINE).find(text)
    requireNotNull(start) { "${file.path} points at a frame table $label it does not define" }
    val end = text.indexOf("InitScriptFrameTableEnd", start.range.last)
    require(end > 0) { "${file.path}'s frame table $label is never closed" }
    return FRAME_ROW.findAll(text.substring(start.range.last, end))
        .map { row ->
          ParsedFrameScript(
              varKey = "${region.name}/${row.groupValues[1]}",
              value = constant(row.groupValues[2], file),
              script = scriptId(row.groupValues[3], file),
          )
        }
        .toList()
  }

  /**
   * A script id as the rest of the map data carries it: decimal, whatever base it was written in.
   */
  private fun scriptId(token: String, file: File): String = constant(token, file).toString()

  /**
   * A decimal or hex literal, or one of the few named constants the tables use. An unknown name
   * stops the build rather than becoming a zero that quietly matches a fresh save.
   */
  private fun constant(token: String, file: File): Int =
      when {
        token.startsWith("0x", ignoreCase = true) -> token.substring(2).toInt(16)
        else ->
            token.toIntOrNull()
                ?: initScriptConstants[token]
                ?: error("${file.path} uses the constant $token, which nothing here defines")
      }

  /**
   * The named values the init tables compare against. Only the Distortion World's progress enum and
   * the decomp's own booleans appear in 207 rows, so this reads that one enum rather than every
   * constant header in the decomp.
   */
  private val initScriptConstants: Map<String, Int> by lazy {
    val file = File(rootDir, "include/constants/distortion_world.h")
    val body =
        Regex("""enum DistWorldProgress\s*\{(.*?)}""", RegexOption.DOT_MATCHES_ALL)
            .find(file.readText())
            ?.groupValues
            ?.get(1) ?: error("${file.path} no longer declares enum DistWorldProgress")
    val progress =
        body
            .split(',')
            .map { it.substringBefore("//").trim() }
            .filter { it.isNotEmpty() }
            .withIndex()
            .associate { (index, entry) ->
              val name = entry.substringBefore('=').trim()
              val explicit = entry.substringAfter('=', "").trim().toIntOrNull()
              require(explicit == null || explicit == index) {
                "$name is $explicit rather than its position $index; this enum is no longer a run"
              }
              name to index
            }
    progress + mapOf("FALSE" to 0, "TRUE" to 1)
  }

  // ------------------------------------------------------------------ maps

  /** The lookups every map shares: the decomp's enum dumps and the whole set of event archives. */
  private class Tables(
      val headerIds: Map<String, Int>,
      val eventsByHeader: Map<String, JsonObject>,
      val speciesIds: Map<String, Int>,
      /** `generated/object_events_gfx.txt`, whose line order is the object-event graphics id. */
      val graphicsIds: Map<String, Int>,
      val movementIds: Map<String, Int>,
      val trainerTypes: Map<String, Int>,
      /** `generated/trainers.txt`, whose line order is the trainer id. */
      val trainerIds: Map<String, Int>,
  )

  private fun buildParsedMap(
      name: String,
      headerId: Int,
      fields: Map<String, String>,
      matrix: ParsedTerrainMatrix,
      events: JsonObject,
      tables: Tables,
  ): ParsedMap {
    val mapType = fields["mapType"] ?: "MAP_TYPE_NONE"
    val archive = readEncounterArchive(fields)
    val init = readInitScripts(fields)
    return ParsedMap(
        regionName = region.name,
        sourceName = name.removePrefix("MAP_HEADER_").lowercase(),
        groupName = "bank${headerId shr 8}",
        mapId = name,
        region = region.regionId,
        bank = headerId shr 8,
        index = headerId and 0xFF,
        width = matrix.cols * matrix.chunkSide,
        height = matrix.rows * matrix.chunkSide,
        paletteIdx1 = 0,
        paletteIdx2 = 0,
        musicId = 0,
        mapsecId = 0,
        borderTiles = emptyList(),
        blockData = "",
        behaviorData = "",
        encounters = readEncounters(archive, tables.speciesIds),
        encounterForms = readForms(archive),
        // Platinum has no per header lighting field: a dark cave is weather 16, DARK_FLASH.
        lighting = "Lighting.REGULAR",
        weather = weatherRef(fields["weather"]),
        mapType = PLATINUM_MAP_TYPES[mapType] ?: "MapType.UNKNOWN_0x00",
        encounterType = "EncounterType.RANDOM",
        connections = emptyList(),
        warps = readWarps(events, tables.headerIds, tables.eventsByHeader),
        visibleNpcs =
            readNpcs(
                events,
                tables.graphicsIds,
                tables.movementIds,
                tables.trainerTypes,
                tables.trainerIds,
            ),
        bgEvents = readBgEvents(events),
        onTransitionScript = init.onTransition,
        healLocation = null,
        onFrameScripts = init.onFrame,
        coordScripts = readCoordScripts(events),
        terrainRef = "$GENERATED_PACKAGE.${region.name}.terrain.${matrix.name.uppercase()}",
    )
  }

  private fun weatherRef(weather: String?): String {
    val value = PLATINUM_WEATHER_IDS[weather ?: "OVERWORLD_WEATHER_CLEAR"]
    requireNotNull(value) { "unknown platinum weather $weather" }
    return "Weather.entries[$GEN4_WEATHER_BASE + $value]"
  }

  private fun readWarps(
      events: JsonObject,
      headerIds: Map<String, Int>,
      eventsByHeader: Map<String, JsonObject>,
  ): List<ParsedWarp> =
      events["warp_events"]?.jsonArray.orEmpty().map { element ->
        val warp = element.jsonObject
        val destName = warp["dest_header_id"]?.jsonPrimitive?.contentOrNull
        val slot = warp["dest_warp_id"]?.jsonPrimitive?.intOrNull ?: 0
        val destId = headerIds[destName]
        // The warp names its destination header and a slot in that header's own warp list, and
        // steps out where that slot sits. MAP_HEADER_DYNAMIC is the runtime destination instead.
        val target =
            if (destId == null) null
            else
                eventsByHeader[destName]?.get("warp_events")?.jsonArray?.getOrNull(slot)?.jsonObject
        ParsedWarp(
            x = warp.getValue("x").jsonPrimitive.int,
            y = warp.getValue("z").jsonPrimitive.int,
            elevation = warp["y"]?.jsonPrimitive?.intOrNull ?: 0,
            targetRegion = region.regionId,
            targetBank = (destId ?: 0) shr 8,
            targetMap = (destId ?: 0) and 0xFF,
            targetX = target?.get("x")?.jsonPrimitive?.int ?: 0,
            targetY = target?.get("z")?.jsonPrimitive?.int ?: 0,
            targetElevation = target?.get("y")?.jsonPrimitive?.intOrNull ?: 0,
            dynamic = destId == null,
        )
      }

  /** A map's people. */
  private fun readNpcs(
      events: JsonObject,
      graphicsIds: Map<String, Int>,
      movementIds: Map<String, Int>,
      trainerTypes: Map<String, Int>,
      trainerIds: Map<String, Int>,
  ): List<ParsedNpc> =
      events["object_events"]?.jsonArray.orEmpty().mapIndexed { index, element ->
        val obj = element.jsonObject
        val movement = obj["movement_type"]?.jsonPrimitive?.contentOrNull ?: "MOVEMENT_TYPE_NONE"
        val hidden = obj["hidden_flag"]?.jsonPrimitive?.contentOrNull ?: "0"
        val script = obj["script"]?.jsonPrimitive?.contentOrNull
        val trainerId = script?.let { trainerIds[it] } ?: 0
        val graphics = obj["graphics_id"]?.jsonPrimitive?.contentOrNull
        ParsedNpc(
            entityIdx = index,
            // The graphics id is a symbolic OBJ_EVENT_GFX_* name here, and the client indexes its
            // own sprite table by the number. Zero is not a safe default: it is
            // OBJ_EVENT_GFX_PLAYER_M, so an unresolved name draws a second copy of the player.
            graphicsId =
                requireNotNull(graphicsIds[graphics]) {
                  "object event graphics id '$graphics' is not in generated/object_events_gfx.txt"
                },
            x = obj.getValue("x").jsonPrimitive.int,
            y = obj.getValue("z").jsonPrimitive.int,
            elevation = obj["y"]?.jsonPrimitive?.intOrNull ?: 0,
            movementType = "MovementType.${PLATINUM_MOVEMENT_TYPES[movement] ?: "NONE"}",
            movementRangeX = obj["movement_range_x"]?.jsonPrimitive?.intOrNull ?: 0,
            movementRangeY = obj["movement_range_z"]?.jsonPrimitive?.intOrNull ?: 0,
            trainerType = trainerTypes[obj["trainer_type"]?.jsonPrimitive?.contentOrNull] ?: 0,
            facing =
                FACING_BY_DIR[obj["initial_dir"]?.jsonPrimitive?.intOrNull ?: 0]
                    ?: "Direction.DOWN",
            script = obj["script"]?.jsonPrimitive?.intOrNull?.toString() ?: "0",
            hideFlag = if (hidden.toIntOrNull() != null) "" else "${region.name}/$hidden",
            rawMovementId = movementIds[movement] ?: 0,
            sightRange = obj["data"]?.jsonArray?.firstOrNull()?.jsonPrimitive?.intOrNull ?: 0,
            trainerId = trainerId,
        )
      }

  private fun readBgEvents(events: JsonObject): List<ParsedBgEvent> =
      events["bg_events"]?.jsonArray.orEmpty().map { element ->
        val obj = element.jsonObject
        ParsedBgEvent(
            x = obj.getValue("x").jsonPrimitive.int,
            y = obj.getValue("z").jsonPrimitive.int,
            facingDir = obj["player_facing_dir"]?.jsonPrimitive?.contentOrNull ?: "",
            script = obj["script"]?.jsonPrimitive?.intOrNull?.toString() ?: "0",
        )
      }

  private fun readCoordScripts(events: JsonObject): List<ParsedCoordScript> =
      events["coord_events"]?.jsonArray.orEmpty().map { element ->
        val obj = element.jsonObject
        ParsedCoordScript(
            x = obj.getValue("x").jsonPrimitive.int,
            y = obj.getValue("z").jsonPrimitive.int,
            elevation = obj["y"]?.jsonPrimitive?.intOrNull ?: 0,
            varKey = "${region.name}/${obj["var"]?.jsonPrimitive?.contentOrNull ?: ""}",
            value = obj["value"]?.jsonPrimitive?.intOrNull ?: 0,
            script = obj["script"]?.jsonPrimitive?.intOrNull?.toString() ?: "0",
            // `length` runs along z, which is this model's y.
            width = obj["width"]?.jsonPrimitive?.intOrNull ?: 1,
            height = obj["length"]?.jsonPrimitive?.intOrNull ?: 1,
        )
      }

  // ------------------------------------------------------------ encounters

  /**
   * The map's encounter archive, or null where it has none or holds no fixed tables. The Great
   * Marsh and the Trophy Garden rotate their tables daily and store them in a shape of their own;
   * nothing here invents a fixed table for them.
   */
  private fun readEncounterArchive(fields: Map<String, String>): JsonObject? {
    val archive = fields["wildEncountersArchiveID"] ?: return null
    return archives.getOrPut(archive) {
      val file = File(fieldDir, "encounters/$archive.json")
      if (!file.exists()) return@getOrPut null
      val obj = json.parseToJsonElement(file.readText()).jsonObject
      if (obj.containsKey("land_encounters")) {
        obj
      } else {
        println("[maps] ${region.name}: $archive has no fixed tables, skipped")
        null
      }
    }
  }

  private fun readEncounters(
      obj: JsonObject?,
      speciesIds: Map<String, Int>,
  ): List<ParsedEncounterTable> {
    if (obj == null) return emptyList()
    val tables = mutableListOf<ParsedEncounterTable>()
    landTable(obj, speciesIds)?.let { tables += it }
    rangeTable(obj, speciesIds, "surf", "EncounterMethod.WATER", WATER_WEIGHTS)?.let {
      tables += it
    }
    rangeTable(obj, speciesIds, "old_rod", "EncounterMethod.OLD_ROD", WATER_WEIGHTS)?.let {
      tables += it
    }
    rangeTable(obj, speciesIds, "good_rod", "EncounterMethod.GOOD_ROD", ROD_WEIGHTS)?.let {
      tables += it
    }
    rangeTable(obj, speciesIds, "super_rod", "EncounterMethod.SUPER_ROD", ROD_WEIGHTS)?.let {
      tables += it
    }
    return tables
  }

  private fun landTable(obj: JsonObject, speciesIds: Map<String, Int>): ParsedEncounterTable? {
    val rate = obj["land_rate"]?.jsonPrimitive?.intOrNull ?: 0
    if (rate == 0) return null
    val slots =
        obj["land_encounters"]?.jsonArray.orEmpty().mapIndexedNotNull { index, element ->
          val entry = element.jsonObject
          val species =
              speciesIds[entry["species"]?.jsonPrimitive?.contentOrNull]
                  ?: return@mapIndexedNotNull null
          val level = entry["level"]?.jsonPrimitive?.intOrNull ?: return@mapIndexedNotNull null
          val weight = LAND_WEIGHTS.getOrNull(index) ?: return@mapIndexedNotNull null
          if (species == 0) null else ParsedEncounterSlot(species, level, level, weight)
        }
    if (slots.isEmpty()) return null
    return ParsedEncounterTable("EncounterMethod.LAND", rate, slots, landOverrides(obj, speciesIds))
  }

  /** The conditional swaps into the grass table. */
  private fun landOverrides(
      obj: JsonObject,
      speciesIds: Map<String, Int>,
  ): List<ParsedEncounterOverride> =
      LAND_OVERRIDE_SLOTS.mapNotNull { (key, spec) ->
        val (variant, slots) = spec
        val names = obj[key]?.jsonArray.orEmpty().map { it.jsonPrimitive.contentOrNull }
        if (names.isEmpty()) return@mapNotNull null
        require(names.size == slots.size) {
          "$key holds ${names.size} species for ${slots.size} slots"
        }
        val species = names.map { name -> speciesIds[name] ?: error("unknown species $name") }
        // An archive with no grass fills every variant with SPECIES_NONE rather than leaving the
        // key out; a swap that writes nothing is not one.
        if (species.all { it == 0 }) null else ParsedEncounterOverride(variant, slots, species)
      }

  /**
   * The archive's form words. `CreateWildMon` reads the first two as the sea a Shellos or a
   * Gastrodon came from, and the Unown table id picks a group of letters; the remaining three it
   * never reads at all, so they are carried in file order rather than named.
   */
  private fun readForms(obj: JsonObject?): ParsedEncounterForms? {
    if (obj == null) return null
    fun word(key: String) = obj[key]?.jsonPrimitive?.intOrNull ?: 0
    return ParsedEncounterForms(
        shellosForm = word("rate_form0"),
        gastrodonForm = word("rate_form1"),
        unownTableId = word("unown_table"),
        unusedFormWords = listOf(word("rate_form2"), word("rate_form3"), word("rate_form4")),
    )
  }

  private fun rangeTable(
      obj: JsonObject,
      speciesIds: Map<String, Int>,
      prefix: String,
      method: String,
      weights: List<Int>,
  ): ParsedEncounterTable? {
    val rate = obj["${prefix}_rate"]?.jsonPrimitive?.intOrNull ?: 0
    if (rate == 0) return null
    val slots =
        obj["${prefix}_encounters"]?.jsonArray.orEmpty().mapIndexedNotNull { index, element ->
          val entry = element.jsonObject
          val species =
              speciesIds[entry["species"]?.jsonPrimitive?.contentOrNull]
                  ?: return@mapIndexedNotNull null
          val weight = weights.getOrNull(index) ?: return@mapIndexedNotNull null
          if (species == 0) null
          else
              ParsedEncounterSlot(
                  speciesId = species,
                  minLevel = entry["level_min"]?.jsonPrimitive?.intOrNull ?: 0,
                  maxLevel = entry["level_max"]?.jsonPrimitive?.intOrNull ?: 0,
                  weight = weight,
              )
        }
    return if (slots.isEmpty()) null else ParsedEncounterTable(method, rate, slots)
  }

  companion object {
    private const val GENERATED_PACKAGE = "de.fiereu.openmmo.maps.generated"
    private const val EMPTY_CELL = "MAP_NONE"

    /** Where Weather's gen 4 block starts; platinum's own ids run from there. */
    private const val GEN4_WEATHER_BASE = 18

    /** The three init script commands a map's arrival reads. */
    private val ON_TRANSITION =
        Regex("""^\s*InitScriptEntry_OnTransition\s+(\S+)""", RegexOption.MULTILINE)
    private val ON_FRAME_TABLE =
        Regex("""^\s*InitScriptEntry_OnFrameTable\s+(\w+)""", RegexOption.MULTILINE)
    private val FRAME_ROW =
        Regex(
            """^\s*InitScriptGoToIfEqual\s+(\w+)\s*,\s*(\w+)\s*,\s*(\w+)""", RegexOption.MULTILINE)

    private val TERRAIN_DEFINES =
        listOf(
            "TERRAIN_ATTRIBUTES_OFFSET",
            "TERRAIN_ATTRIBUTES_SIZE",
            "TERRAIN_ATTRIBUTES_COLLISION_SHIFT",
            "TERRAIN_ATTRIBUTES_TILE_BEHAVIOR_MASK",
        )

    // overlay006/wild_encounters.c: GetGroundEncounterSlot rolls 0..99 and cuts it at 20, 40, 50,
    // 60, 70, 80, 85, 90, 94, 98, 99. GetWaterEncounterSlot and the old rod cut at 60, 90, 95, 99;
    // the good and super rods at 40, 80, 95, 99.
    private val LAND_WEIGHTS = listOf(20, 20, 10, 10, 10, 10, 5, 5, 4, 4, 1, 1)

    // The grass table's conditional slots, by the key that holds them in the archive: the variant
    // they render as and the slot indices the game writes them over.
    private val LAND_OVERRIDE_SLOTS =
        listOf(
            "day" to ("EncounterVariant.DAY" to listOf(2, 3)),
            "night" to ("EncounterVariant.NIGHT" to listOf(2, 3)),
            "swarms" to ("EncounterVariant.SWARM" to listOf(0, 1)),
            "radar" to ("EncounterVariant.RADAR" to listOf(4, 5, 10, 11)),
            "ruby" to ("EncounterVariant.DUAL_SLOT_RUBY" to listOf(8, 9)),
            "sapphire" to ("EncounterVariant.DUAL_SLOT_SAPPHIRE" to listOf(8, 9)),
            "emerald" to ("EncounterVariant.DUAL_SLOT_EMERALD" to listOf(8, 9)),
            "firered" to ("EncounterVariant.DUAL_SLOT_FIRERED" to listOf(8, 9)),
            "leafgreen" to ("EncounterVariant.DUAL_SLOT_LEAFGREEN" to listOf(8, 9)),
        )
    private val WATER_WEIGHTS = listOf(60, 30, 5, 4, 1)
    private val ROD_WEIGHTS = listOf(40, 40, 15, 4, 1)

    private val FACING_BY_DIR =
        mapOf(
            0 to "Direction.UP",
            1 to "Direction.DOWN",
            2 to "Direction.LEFT",
            3 to "Direction.RIGHT",
        )

    private val PLATINUM_MAP_TYPES =
        mapOf(
            "MAP_TYPE_TOWN_CITY" to "MapType.CITY",
            "MAP_TYPE_OUTDOORS" to "MapType.ROUTE",
            "MAP_TYPE_INDOORS" to "MapType.INSIDE",
            "MAP_TYPE_CAVE" to "MapType.UNDERGROUND",
            "MAP_TYPE_UNDERGROUND" to "MapType.UNDERGROUND",
            "MAP_TYPE_NONE" to "MapType.UNKNOWN_0x00",
        )

    private val PLATINUM_WEATHER_IDS =
        mapOf(
            "OVERWORLD_WEATHER_CLEAR" to 0,
            "OVERWORLD_WEATHER_CLOUDY" to 1,
            "OVERWORLD_WEATHER_RAINING" to 2,
            "OVERWORLD_WEATHER_HEAVY_RAIN" to 3,
            "OVERWORLD_WEATHER_THUNDERSTORM" to 4,
            "OVERWORLD_WEATHER_SNOWING" to 5,
            "OVERWORLD_WEATHER_HEAVY_SNOW" to 6,
            "OVERWORLD_WEATHER_BLIZZARD" to 7,
            "OVERWORLD_WEATHER_CLEAR_8" to 8,
            "OVERWORLD_WEATHER_SLOW_ASHFALL" to 9,
            "OVERWORLD_WEATHER_SANDSTORM" to 10,
            "OVERWORLD_WEATHER_HAILING" to 11,
            "OVERWORLD_WEATHER_SPIRITS" to 12,
            "OVERWORLD_WEATHER_CLEAR_13" to 13,
            "OVERWORLD_WEATHER_FOG" to 14,
            "OVERWORLD_WEATHER_DEEP_FOG" to 15,
            "OVERWORLD_WEATHER_DARK_FLASH" to 16,
            "OVERWORLD_WEATHER_23" to 23,
            "OVERWORLD_WEATHER_26" to 26,
            "OVERWORLD_WEATHER_27" to 27,
            "OVERWORLD_WEATHER_28" to 28,
            "OVERWORLD_WEATHER_29" to 29,
            "OVERWORLD_WEATHER_30" to 30,
            "OVERWORLD_WEATHER_ROUTE_212_SOUTH" to 32,
            "OVERWORLD_WEATHER_ROUTE_213" to 33,
            "OVERWORLD_WEATHER_ROUTE_216" to 34,
            "OVERWORLD_WEATHER_ACUITY_LAKEFRONT" to 35,
            "OVERWORLD_WEATHER_SNOWPOINT_CITY" to 36,
        )

    /** Platinum names its facings by compass point; the normalized set names them by screen. */
    private val PLATINUM_MOVEMENT_TYPES =
        mapOf(
            "MOVEMENT_TYPE_NONE" to "NONE",
            "MOVEMENT_TYPE_LOOK_AROUND" to "LOOK_AROUND",
            "MOVEMENT_TYPE_WANDER_AROUND" to "WANDER_AROUND",
            "MOVEMENT_TYPE_WANDER_NORTH_AND_SOUTH" to "WANDER_UP_AND_DOWN",
            "MOVEMENT_TYPE_WANDER_WEST_AND_EAST" to "WANDER_LEFT_AND_RIGHT",
            "MOVEMENT_TYPE_LOOK_NORTH" to "FACE_UP",
            "MOVEMENT_TYPE_LOOK_SOUTH" to "FACE_DOWN",
            "MOVEMENT_TYPE_LOOK_WEST" to "FACE_LEFT",
            "MOVEMENT_TYPE_LOOK_EAST" to "FACE_RIGHT",
            "MOVEMENT_TYPE_LOOK_NORTH_AND_SOUTH" to "FACE_DOWN_AND_UP",
            "MOVEMENT_TYPE_LOOK_WEST_AND_EAST" to "FACE_LEFT_AND_RIGHT",
            "MOVEMENT_TYPE_LOOK_NORTH_AND_WEST" to "FACE_UP_AND_LEFT",
            "MOVEMENT_TYPE_LOOK_NORTH_AND_EAST" to "FACE_UP_AND_RIGHT",
            "MOVEMENT_TYPE_LOOK_SOUTH_AND_WEST" to "FACE_DOWN_AND_LEFT",
            "MOVEMENT_TYPE_LOOK_SOUTH_AND_EAST" to "FACE_DOWN_AND_RIGHT",
            "MOVEMENT_TYPE_LOOK_NORTH_SOUTH_AND_WEST" to "FACE_DOWN_UP_AND_LEFT",
            "MOVEMENT_TYPE_LOOK_NORTH_SOUTH_AND_EAST" to "FACE_DOWN_UP_AND_RIGHT",
            "MOVEMENT_TYPE_LOOK_NORTH_WEST_AND_EAST" to "FACE_UP_LEFT_AND_RIGHT",
            "MOVEMENT_TYPE_LOOK_SOUTH_WEST_AND_EAST" to "FACE_DOWN_LEFT_AND_RIGHT",
            "MOVEMENT_TYPE_ROTATE_CLOCKWISE" to "ROTATE_CLOCKWISE",
            "MOVEMENT_TYPE_ROTATE_COUNTERCLOCKWISE" to "ROTATE_COUNTERCLOCKWISE",
            "MOVEMENT_TYPE_BERRY_SOIL" to "BERRY_TREE_GROWTH",
        )

    /**
     * The behaviours the server acts on, by their platinum name. Everything else stays NORMAL: a
     * behaviour whose effect is only drawn does not need a normalized name to be walked over.
     */
    private val PLATINUM_TILE_BEHAVIORS =
        mapOf(
            "TILE_BEHAVIOR_TALL_GRASS" to "TALL_GRASS",
            "TILE_BEHAVIOR_VERY_TALL_GRASS" to "LONG_GRASS",
            "TILE_BEHAVIOR_MUD_WITH_GRASS" to "TALL_GRASS",
            "TILE_BEHAVIOR_MUD_DEEP_WITH_GRASS" to "TALL_GRASS",
            "TILE_BEHAVIOR_JUMP_EAST" to "JUMP_EAST",
            "TILE_BEHAVIOR_JUMP_WEST" to "JUMP_WEST",
            "TILE_BEHAVIOR_JUMP_NORTH" to "JUMP_NORTH",
            "TILE_BEHAVIOR_JUMP_SOUTH" to "JUMP_SOUTH",
            "TILE_BEHAVIOR_DOOR" to "DOOR",
            "TILE_BEHAVIOR_WARP_ENTRANCE_EAST" to "NON_ANIMATED_DOOR",
            "TILE_BEHAVIOR_WARP_ENTRANCE_WEST" to "NON_ANIMATED_DOOR",
            "TILE_BEHAVIOR_WARP_ENTRANCE_NORTH" to "NON_ANIMATED_DOOR",
            "TILE_BEHAVIOR_WARP_ENTRANCE_SOUTH" to "NON_ANIMATED_DOOR",
            "TILE_BEHAVIOR_WARP_PANEL" to "LADDER",
            "TILE_BEHAVIOR_ESCALATOR" to "LADDER",
            "TILE_BEHAVIOR_ESCALATOR_FLIP_FACE" to "LADDER",
            "TILE_BEHAVIOR_WARP_STAIRS_EAST" to "STAIR_WARP_EAST",
            "TILE_BEHAVIOR_WARP_STAIRS_WEST" to "STAIR_WARP_WEST",
            "TILE_BEHAVIOR_WARP_NORTH" to "NORTH_ARROW_WARP",
            "TILE_BEHAVIOR_WARP_SOUTH" to "SOUTH_ARROW_WARP",
            "TILE_BEHAVIOR_WARP_EAST" to "EAST_ARROW_WARP",
            "TILE_BEHAVIOR_WARP_WEST" to "WEST_ARROW_WARP",
            // The HM walls. WATER is no longer listed here at all, `readWaterBehaviors`
            // reads the game's own flag table, because a list of names was wrong in both
            // directions and a table cannot be.
            "TILE_BEHAVIOR_WATERFALL" to "WATERFALL",
            "TILE_BEHAVIOR_ROCK_CLIMB_N_S" to "ROCK_CLIMB_NORTH_SOUTH",
            "TILE_BEHAVIOR_ROCK_CLIMB_E_W" to "ROCK_CLIMB_EAST_WEST",
        )
  }
}
