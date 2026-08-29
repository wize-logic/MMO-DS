@file:JvmName("Main")

package de.fiereu.openmmo.codegen.evolution

import java.io.File

fun main(args: Array<String>) {
  require(args.size >= 4) {
    "Usage: <output-dir> <templates-dir> <class-cache-dir> <decomp-dir> got ${args.toList()}"
  }
  val outputDir = File(args[0])
  val templatesDir = File(args[1])
  val classCacheDir = File(args[2])
  val decompDir = File(args[3])

  println("[evolution] parsing from $decompDir")
  val data = SpeciesDataParser(decompDir).parseAll()
  println(
      "[evolution] parsed ${data.evolutions.sumOf { it.evolutions.size }} evolutions over " +
          "${data.evolutions.size} species and ${data.breeding.size} breeding rows. " +
          "writing to $outputDir")
  EvolutionRenderer(templatesDir, outputDir, classCacheDir).render(data)
  println("[evolution] done")
}
