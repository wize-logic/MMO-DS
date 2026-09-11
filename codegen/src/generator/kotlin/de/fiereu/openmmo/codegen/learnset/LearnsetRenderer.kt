package de.fiereu.openmmo.codegen.learnset

import gg.jte.ContentType
import gg.jte.TemplateEngine
import gg.jte.output.FileOutput
import gg.jte.resolve.DirectoryCodeResolver
import java.io.File

class LearnsetRenderer(
    private val templatesDir: File,
    private val outputDir: File,
    private val classCacheDir: File,
    private val registryChunkSize: Int = 50,
) {

  fun render(learnsets: List<ParsedLearnset>, moveSources: List<ParsedMoveSources>) {
    classCacheDir.mkdirs()
    val engine =
        TemplateEngine.create(
            DirectoryCodeResolver(templatesDir.toPath()),
            classCacheDir.toPath(),
            ContentType.Plain,
        )

    val packageRoot = File(outputDir, BASE_PACKAGE.replace('.', '/'))
    if (packageRoot.exists()) packageRoot.deleteRecursively()

    write(
        engine,
        "LearnsetRegistry.jte",
        "GeneratedLearnsets.kt",
        learnsets.chunked(registryChunkSize))
    write(
        engine,
        "MoveSources.jte",
        "GeneratedMoveSources.kt",
        moveSources.chunked(registryChunkSize))
  }

  private fun write(
      engine: TemplateEngine,
      template: String,
      fileName: String,
      chunks: List<List<Any>>,
  ) {
    val file = File(outputDir, "${BASE_PACKAGE.replace('.', '/')}/$fileName")
    file.parentFile.mkdirs()
    FileOutput(file.toPath()).use { out -> engine.render(template, mapOf("chunks" to chunks), out) }
  }

  companion object {
    private const val BASE_PACKAGE = "de.fiereu.openmmo.pokemon.generated"
  }
}
