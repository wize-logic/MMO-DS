@file:JvmName("Main")

package de.fiereu.openmmo.codegen.text

import de.fiereu.openmmo.common.enums.Region
import java.io.File

fun main(args: Array<String>) {
  require(args.size >= 5) {
    "Usage: <output-dir> <templates-dir> <class-cache-dir> <decomp-dir> <region> got ${args.toList()}"
  }
  val outputDir = File(args[0])
  val templatesDir = File(args[1])
  val classCacheDir = File(args[2])
  val decompDir = File(args[3])
  val region =
      Region.entries.find { it.name.equals(args[4], ignoreCase = true) }
          ?: error("unknown region '${args[4]}', its text ids cannot be built")

  val banks = TextBankReader.read(decompDir)
  val messages = banks.sumOf { it.messages.size }
  val skipped = TextBankReader.bankOrder(decompDir).size - banks.size
  println(
      "[text] ${region.name.lowercase()}: $messages ids over ${banks.size} banks from $decompDir" +
          " ($skipped assembled during the ROM build and left out)")
  TextRenderer(region, templatesDir, outputDir, classCacheDir).render(banks)
}
