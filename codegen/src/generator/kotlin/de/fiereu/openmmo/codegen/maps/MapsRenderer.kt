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
  }
}
