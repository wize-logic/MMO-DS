@file:JvmName("Main")

package de.fiereu.openmmo.codegen.learnset

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

  println("[learnset] parsing from $decompDir and $gen5Dir")
  val learnsets =
      (NdsLearnsetParser(decompDir).parseAll() + gen5Learnsets(gen5Dir)).sortedBy { it.dexId }
  // Machines, egg moves and tutors for the 493 come from the DS decomp, which is the cartridge
  // this server hosts.
  val moveSources =
      (NdsMoveSourceParser(decompDir).parseAll() + gen5MoveSources(gen5Dir)).sortedBy { it.dexId }
  println(
      "[learnset] parsed ${learnsets.size} learnsets and ${moveSources.size} move source lists. " +
          "writing to $outputDir")
  LearnsetRenderer(templatesDir, outputDir, classCacheDir).render(learnsets, moveSources)
  println("[learnset] done")
}
