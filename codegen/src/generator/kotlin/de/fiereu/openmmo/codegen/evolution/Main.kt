@file:JvmName("Main")

package de.fiereu.openmmo.codegen.evolution

import java.io.File

fun main(args: Array<String>) {
  require(args.size >= 5) {
    "Usage: <output-dir> <templates-dir> <class-cache-dir> <decomp-dir> <gen5-dir> " +
        "got ${args.toList()}"
  }
  val outputDir = File(args[0])
  val templatesDir = File(args[1])
  val classCacheDir = File(args[2])
  val decompDir = File(args[3])
  val gen5Dir = File(args[4])

  println("[evolution] parsing from $decompDir and $gen5Dir")
  val (newEvolutions, newBreeding) = gen5Evolutions(gen5Dir)
  val parsed = SpeciesDataParser(decompDir).parseAll()
  val data =
      parsed.copy(
          evolutions = (parsed.evolutions + newEvolutions).sortedBy { it.dexId },
          breeding = (parsed.breeding + newBreeding).sortedBy { it.dexId },
      )
  println(
      "[evolution] parsed ${data.evolutions.sumOf { it.evolutions.size }} evolutions over " +
          "${data.evolutions.size} species and ${data.breeding.size} breeding rows. " +
          "writing to $outputDir")
  EvolutionRenderer(templatesDir, outputDir, classCacheDir).render(data)
  println("[evolution] done")
}
