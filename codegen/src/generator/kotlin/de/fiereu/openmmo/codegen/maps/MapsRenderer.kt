package de.fiereu.openmmo.codegen.maps

import gg.jte.ContentType
import gg.jte.TemplateEngine
import gg.jte.output.FileOutput
import gg.jte.resolve.DirectoryCodeResolver
import java.io.File

class MapsRenderer(
    private val templatesDir: File,
    private val outputDir: File,
    private val classCacheDir: File,
    private val registryChunkSize: Int = 100,
) {

  fun render(regions: List<ParsedRegion>) {
    val maps = regions.flatMap { it.maps }
    classCacheDir.mkdirs()
    val engine =
        TemplateEngine.create(
            DirectoryCodeResolver(templatesDir.toPath()),
            classCacheDir.toPath(),
            ContentType.Plain,
        )

    val packageRoot = File(outputDir, BASE_PACKAGE.replace('.', '/'))
    if (packageRoot.exists()) packageRoot.deleteRecursively()

    regions.filter { it.terrainPackage.isNotEmpty() }.forEach(::renderTerrain)
    regions.mapNotNull { it.marts }.forEach(::renderMarts)
    regions.map { it.headbuttLuts }.filter { it.isNotEmpty() }.forEach(::renderHeadbuttLuts)
    regions.filter { it.spawnLocations.isNotEmpty() }.forEach(::renderSpawnLocations)
    regions.filter { it.dailyEncounters != null }.forEach(::renderDailyEncounters)

    val references = mutableListOf<String>()
    for (map in maps) {
      val packageName = packageFor(map)
      val objectName = identifier(map.sourceName)
      references += "$packageName.$objectName"

      val target = File(outputDir, "${packageName.replace('.', '/')}/$objectName.kt")
      target.parentFile.mkdirs()
      FileOutput(target.toPath()).use { out ->
        engine.render(
            "MapDef.jte",
            mapOf("packageName" to packageName, "objectName" to objectName, "map" to map),
            out,
        )
      }
    }

    val chunks = references.sorted().chunked(registryChunkSize)
    val registry = File(outputDir, "${BASE_PACKAGE.replace('.', '/')}/GeneratedMaps.kt")
    registry.parentFile.mkdirs()
    FileOutput(registry.toPath()).use { out ->
      engine.render("MapsRegistry.jte", mapOf("chunks" to chunks), out)
    }
  }

  /** The source cartridge's shelves. */
  private fun renderMarts(marts: ParsedMarts) {
    val dir = File(outputDir, MARTS_PACKAGE.replace('.', '/'))
    dir.mkdirs()
    File(dir, "PortedMarts.kt")
        .writeText(
            "package $MARTS_PACKAGE\n\n" +
                "/**\n" +
                " * The mart tables of the cartridge the ported regions came out of.\n" +
                " *\n" +
                " * [COMMON] is the ordinary mart, which is one table for every town: each row is an\n" +
                " * item and the badge tier it first appears at, and the shelf is every row at or\n" +
                " * under the tier the player's badges buy. [SPECIALTIES] are the numbered shops, in\n" +
                " * the source's own order, and a [NpcDef.martShelf] of 1 or more indexes them off by\n" +
                " * one so that -1 can mean the person sells nothing.\n" +
                " */\n" +
                "object PortedMarts {\n" +
                "  val COMMON: List<Pair<Int, Int>> =\n      listOf(\n" +
                marts.commonTiers.joinToString("") { "          ${it.first} to ${it.second},\n" } +
                END_LIST +
                "  val SPECIALTIES: List<List<Int>> =\n      listOf(\n" +
                marts.specialties.joinToString("") {
                  "          listOf(${it.joinToString(", ")}),\n"
                } +
                "      )\n" +
                "}\n")
  }

  /**
   * The cartridge's spawn table, in its own order, so a row's index is the engine's own warp id.
   */
  private fun renderSpawnLocations(region: ParsedRegion) {
    val dir = File(outputDir, region.spawnPackage.replace('.', '/'))
    dir.mkdirs()
    File(dir, "SpawnLocations.kt")
        .writeText(
            "package ${region.spawnPackage}\n\n" +
                "import de.fiereu.openmmo.common.FlyLocation\n" +
                "import de.fiereu.openmmo.common.HealLocation\n\n" +
                "/**\n" +
                " * The cartridge's `sSpawnLocations[]`, one row per place a white out can land, in the\n" +
                " * engine's own order: the save's black-out warp id is a 1-based index into [ROWS].\n" +
                " * [FLY] is the same table's other half, the tile Fly lands on, by the same id.\n" +
                " */\n" +
                "object SpawnLocations {\n" +
                "  val ROWS: List<HealLocation> =\n      listOf(\n" +
                region.spawnLocations.joinToString("") {
                  "          HealLocation(regionId = ${it.heal.region}.toByte(), " +
                      "bankId = ${it.heal.bank}.toByte(), mapId = ${it.heal.map}.toByte(), " +
                      "x = ${it.heal.x}.toShort(), y = ${it.heal.y}.toShort()),\n"
                } +
                END_LIST +
                "  /** Fly's landing per row, by the same id as [ROWS]. */\n" +
                "  val FLY: List<FlyLocation> =\n      listOf(\n" +
                region.spawnLocations.joinToString("") {
                  "          FlyLocation(mapHeaderId = ${it.flyHeader}, x = ${it.flyX}," +
                      " z = ${it.flyZ}, firstArrivalFlagId = ${it.firstArrivalFlagId}),\n"
                } +
                END_LIST +
                "  /** Where Fly lands for warp id [id], or null for an id off the table. */\n" +
                "  fun flyLocation(id: Int): FlyLocation? = FLY.getOrNull(id - 1)\n\n" +
                "  /** Where warp id [id] lands, or null for an id off the table. */\n" +
                "  fun healLocation(id: Int): HealLocation? = ROWS.getOrNull(id - 1)\n\n" +
                "  /** The warp id whose row is [location], or null for a location that is no row. */\n" +
                "  fun idFor(location: HealLocation?): Int? =\n" +
                "      location?.let { ROWS.indexOf(it).takeIf { i -> i >= 0 }?.plus(1) }\n" +
                "}\n")
  }

  /**
   * The species the extra encounter archive keeps for the two maps that rotate: the Great Marsh's
   * thirty-two per dex and the Trophy Garden's sixteen.
   */
  private fun renderDailyEncounters(region: ParsedRegion) {
    val daily = region.dailyEncounters ?: return
    val dir = File(outputDir, region.dailyPackage.replace('.', '/'))
    dir.mkdirs()
    fun list(name: String, doc: String, values: List<Int>): String =
        "$doc  val $name: List<Int> =\n      listOf(\n" +
            values.chunked(8).joinToString("") { "          ${it.joinToString(", ")},\n" } +
            END_LIST
    File(dir, "DailyEncounters.kt")
        .writeText(
            "package ${region.dailyPackage}\n\n" +
                "/**\n" +
                " * The species no map's own encounter archive carries: the extra archive's members\n" +
                " * 8, 9 and 10, which the cartridge reads over slots 6 and 7 of the grass table of\n" +
                " * the Great Marsh and of the Trophy Garden.\n" +
                " *\n" +
                " * The marsh reads [MARSH_NATIONAL_DEX] once the national dex is obtained and\n" +
                " * [MARSH_LOCAL] before it, both slots taking the SAME species: the index is\n" +
                " * [AREA_BITS] bits of the day's roll, cut at the area's own position in\n" +
                " * [MARSH_AREAS]. The Trophy Garden takes two of [TROPHY_GARDEN_MONS], and only\n" +
                " * once the national dex is obtained.\n" +
                " */\n" +
                "object DailyEncounters {\n" +
                "  /** The wire region these headers belong to. */\n" +
                "  const val REGION: Int = ${daily.regionId}\n\n" +
                "  /** The six marsh headers, in the order the cartridge numbers the areas. */\n" +
                "  val MARSH_AREAS: List<Int> = listOf(${daily.marshAreaHeaders.joinToString(", ")})\n\n" +
                "  /** The Trophy Garden's header. */\n" +
                "  const val TROPHY_GARDEN: Int = ${daily.trophyGardenHeader}\n\n" +
                "  /** How many bits of the day's roll one marsh area's index is cut out of. */\n" +
                "  const val AREA_BITS: Int = ${daily.marshAreaBits}\n\n" +
                "  /** The mask that index is taken with. */\n" +
                "  const val AREA_MASK: Int = ${daily.marshNationalDex.size - 1}\n\n" +
                list(
                    "MARSH_NATIONAL_DEX",
                    "  /** Member 9: the marsh once the national dex is obtained. */\n",
                    daily.marshNationalDex) +
                list(
                    "MARSH_LOCAL", "  /** Member 10: the marsh before it. */\n", daily.marshLocal) +
                list(
                        "TROPHY_GARDEN_MONS",
                        "  /** Member 8: what Mr. Backlot's garden may hold. */\n",
                        daily.trophyGardenMons)
                    .removeSuffix("\n") +
                "}\n")
  }

  private fun renderHeadbuttLuts(luts: Map<Int, List<List<Int>>>) {
    val dir = File(outputDir, MARTS_PACKAGE.replace('.', '/'))
    dir.mkdirs()
    File(dir, "HeadbuttLuts.kt")
        .writeText(
            "package $MARTS_PACKAGE\n\n" +
                "/**\n" +
                " * Which of a map's headbutt trees are rare for a given trainer, out of the source\n" +
                " * cartridge's own lookup tables.\n" +
                " *\n" +
                " * [BY_TREE_COUNT] is keyed by how many regular trees the map has, 1 to 5 with 5\n" +
                " * standing for five or more; a row is the trainer id's last digit and a column the\n" +
                " * tree's index (wrapping at five in the last table). A cell is 0 for the common\n" +
                " * table, 1 for the rare one and -1 for a tree nothing lives in.\n" +
                " */\n" +
                "object HeadbuttLuts {\n" +
                "  val BY_TREE_COUNT: Map<Int, List<List<Int>>> =\n      mapOf(\n" +
                luts.toSortedMap().entries.joinToString("") { (count, rows) ->
                  "          $count to\n              listOf(\n" +
                      rows.joinToString("") {
                        "                  listOf(${it.joinToString(", ")}),\n"
                      } +
                      "              ),\n"
                } +
                "      )\n" +
                "}\n")
  }

  /** The DS terrain, which belongs to the matrix rather than to a map. */
  private fun renderTerrain(region: ParsedRegion) {
    val dir = File(outputDir, region.terrainPackage.replace('.', '/'))
    dir.mkdirs()
    val header = "package ${region.terrainPackage}\n\n"

    File(dir, "TileBehaviors.kt")
        .writeText(
            header +
                "import de.fiereu.openmmo.common.enums.TileBehavior\n\n" +
                "internal val TILE_BEHAVIORS: Array<TileBehavior> =\n    arrayOf(\n" +
                region.tileBehaviors.joinToString("") { "        $it,\n" } +
                "    )\n")

    for (chunk in region.terrainChunks) {
      File(dir, "${chunk.name}.kt")
          .writeText(header + "internal val ${chunk.name} = \"${chunk.encoded}\"\n")
    }

    for (matrix in region.terrainMatrices) {
      val cells =
          matrix.chunks.joinToString("") { if (it == null) "        null,\n" else "        $it,\n" }
      File(dir, "${matrix.name.uppercase()}.kt")
          .writeText(
              header +
                  "import de.fiereu.openmmo.maps.TerrainPlane\n\n" +
                  "internal val ${matrix.name.uppercase()}: TerrainPlane =\n" +
                  "    TerrainPlane(\n" +
                  "        cols = ${matrix.cols},\n" +
                  "        rows = ${matrix.rows},\n" +
                  "        chunkSide = ${matrix.chunkSide},\n" +
                  "        chunks =\n            listOf(\n" +
                  cells.prependIndent("    ") +
                  "            ),\n" +
                  "        altitudes = listOf(${matrix.altitudes.joinToString(", ")}),\n" +
                  "        headers = listOf(${matrix.headers.joinToString(", ")}),\n" +
                  "        behaviors = TILE_BEHAVIORS,\n" +
                  "    )\n")
    }
  }

  private fun packageFor(map: ParsedMap): String =
      "$BASE_PACKAGE.${identifier(map.regionName)}.${groupSegment(map.groupName)}"

  private fun groupSegment(groupName: String): String {
    val stripped = groupName.removePrefix("gMapGroup_")
    val sanitized = stripped.filter { it.isLetterOrDigit() }.lowercase()
    return sanitized.ifEmpty { "misc" }
  }

  private fun identifier(name: String): String {
    val sanitized = name.map { if (it.isLetterOrDigit() || it == '_') it else '_' }.joinToString("")
    return if (sanitized.firstOrNull()?.isDigit() == true) "_$sanitized" else sanitized
  }

  companion object {
    private const val BASE_PACKAGE = "de.fiereu.openmmo.maps.generated"
    private const val MARTS_PACKAGE = "$BASE_PACKAGE.ported"

    /** Closing a rendered `listOf(` and leaving a blank line before the next member. */
    private const val END_LIST = "      )\n\n"
  }
}
