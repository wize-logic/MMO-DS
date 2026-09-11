package de.fiereu.openmmo.codegen.offline

import gg.jte.ContentType
import gg.jte.TemplateEngine
import gg.jte.output.FileOutput
import gg.jte.resolve.DirectoryCodeResolver
import java.io.File

class OfflineRenderer(
    private val templatesDir: File,
    private val outputDir: File,
    private val classCacheDir: File,
) {

  fun render(limits: ParsedCartridgeLimits) {
    classCacheDir.mkdirs()
    val engine =
        TemplateEngine.create(
            DirectoryCodeResolver(templatesDir.toPath()),
            classCacheDir.toPath(),
            ContentType.Plain,
        )

    val packageRoot = File(outputDir, BASE_PACKAGE.replace('.', '/'))
    if (packageRoot.exists()) packageRoot.deleteRecursively()

    val file = File(packageRoot, "GeneratedCartridgeLimits.kt")
    file.parentFile.mkdirs()
    FileOutput(file.toPath()).use { out ->
      engine.render("CartridgeLimits.jte", mapOf("limits" to limits), out)
    }
  }

  companion object {
    private const val BASE_PACKAGE = "de.fiereu.openmmo.offline.generated"
  }
}
