@file:JvmName("Main")

package de.fiereu.openmmo.codegen.pokemon

import de.fiereu.openmmo.codegen.gen5.Gen5Tables
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

  println("[pokemon] parsing from $decompDir and $gen5Dir")
  val hidden = Gen5Tables(gen5Dir).hiddenAbilities
  val species =
      (NdsSpeciesParser(decompDir, hidden).parseAll() + gen5Species(gen5Dir, hidden)).sortedBy {
        it.id
      }
  println("[pokemon] parsed ${species.size} species. writing to $outputDir")
  PokemonRenderer(templatesDir, outputDir, classCacheDir).render(species)
  println("[pokemon] done")
}
