package de.fiereu.openmmo.codegen.text

import de.fiereu.openmmo.codegen.dialog.DialogEntry
import de.fiereu.openmmo.codegen.dialog.RenderUtil
import de.fiereu.openmmo.common.dialog.TextId
import de.fiereu.openmmo.common.enums.Region
import gg.jte.ContentType
import gg.jte.TemplateEngine
import gg.jte.output.FileOutput
import gg.jte.resolve.DirectoryCodeResolver
import java.io.File

/** One row of the catalogue: a bank, where it sits in the archive, and how many ids it has. */
data class BankEntry(val name: String, val index: Int, val messageCount: Int)

/**
 * Emits one Kotlin file per text bank, each an enum of that bank's lines, plus the catalogue of
 * every id the server can send.
 */
class TextRenderer(
    private val region: Region,
    private val templatesDir: File,
    private val outputDir: File,
    private val classCacheDir: File,
) {
  private val basePackage = "de.fiereu.openmmo.dialog.generated.${region.name.lowercase()}"
  private val catalogueName =
      "${RenderUtil.className(region.name.lowercase().capitalise())}TextBank"

  fun render(banks: List<TextBank>) {
    classCacheDir.mkdirs()
    val engine =
        TemplateEngine.create(
            DirectoryCodeResolver(templatesDir.toPath()),
            classCacheDir.toPath(),
            ContentType.Plain,
        )

    val packageRoot = File(outputDir, basePackage.replace('.', '/'))
    if (packageRoot.exists()) packageRoot.deleteRecursively()
    packageRoot.mkdirs()

    for (bank in banks) {
      val enumName = className(bank.name)
      val entries =
          bank.messages.map {
            DialogEntry(
                entryName(enumName, it.label),
                TextId.ds(region, bank.index, it.entry),
                RenderUtil.preview(it.text),
            )
          }
      write(engine, packageRoot, enumName, "TextBank.jte") {
        mapOf(
            "pkg" to basePackage,
            "enumName" to enumName,
            "bankName" to bank.name,
            "bankIndex" to bank.index,
            "entries" to entries,
        )
      }
    }

    val rows = banks.map { BankEntry(className(it.name), it.index, it.messages.size) }
    write(engine, packageRoot, catalogueName, "TextCatalogue.jte") {
      mapOf(
          "pkg" to basePackage,
          "enumName" to catalogueName,
          "region" to region.name,
          "banks" to rows,
      )
    }
  }

  private fun write(
      engine: TemplateEngine,
      packageRoot: File,
      name: String,
      template: String,
      params: () -> Map<String, Any>,
  ) {
    FileOutput(File(packageRoot, "${name.trim('`')}.kt").toPath()).use { out ->
      engine.render(template, params(), out)
    }
  }

  private fun className(bank: String): String = TextNames.className(bank)

  private fun entryName(enumName: String, label: String): String =
      TextNames.entryName(enumName, label)

  private fun String.capitalise(): String = replaceFirstChar { it.uppercase() }
}
