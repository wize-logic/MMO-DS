package de.fiereu.openmmo.codegen.evolution

import gg.jte.ContentType
import gg.jte.TemplateEngine
import gg.jte.output.FileOutput
import gg.jte.resolve.DirectoryCodeResolver
import java.io.File

class EvolutionRenderer(
    private val templatesDir: File,
    private val outputDir: File,
    private val classCacheDir: File,
    private val registryChunkSize: Int = 50,
) {

  fun render(data: ParsedSpeciesData) {
    classCacheDir.mkdirs()
    val engine =
        TemplateEngine.create(
            DirectoryCodeResolver(templatesDir.toPath()),
            classCacheDir.toPath(),
            ContentType.Plain,
        )

    val packageRoot = File(outputDir, BASE_PACKAGE.replace('.', '/'))
    if (packageRoot.exists()) packageRoot.deleteRecursively()
    packageRoot.mkdirs()

    write(engine, packageRoot, "GeneratedEvolutions.kt", "EvolutionRegistry.jte") {
      mapOf("chunks" to data.evolutions.chunked(registryChunkSize))
    }
    write(engine, packageRoot, "GeneratedBreeding.kt", "BreedingRegistry.jte") {
      mapOf(
          "chunks" to data.breeding.chunked(registryChunkSize),
          "incenseBabies" to data.incenseBabies,
      )
    }
    write(engine, packageRoot, "GeneratedNamedIds.kt", "NamedIds.jte") {
      mapOf("ids" to data.namedSpeciesIds)
    }
  }

  private fun write(
      engine: TemplateEngine,
      packageRoot: File,
      fileName: String,
      template: String,
      params: () -> Map<String, Any>,
  ) {
    FileOutput(File(packageRoot, fileName).toPath()).use { out ->
      engine.render(template, params(), out)
    }
  }

  companion object {
    private const val BASE_PACKAGE = "de.fiereu.openmmo.pokemon.generated"
  }
}
