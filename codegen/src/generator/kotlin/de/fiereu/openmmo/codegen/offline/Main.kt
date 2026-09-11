@file:JvmName("Main")

package de.fiereu.openmmo.codegen.offline

import de.fiereu.openmmo.codegen.trainer.PlatinumTrainerParser
import java.io.File

fun main(args: Array<String>) {
  require(args.size >= 4) {
    "Usage: <output-dir> <templates-dir> <class-cache-dir> <decomp-dir> got ${args.toList()}"
  }
  val outputDir = File(args[0])
  val templatesDir = File(args[1])
  val classCacheDir = File(args[2])
  val decompDir = File(args[3])

  println("[offline] reading what one cartridge can produce from $decompDir")
  val parser = PlatinumSourceParser(decompDir)
  val direct = parser.directSpecies()
  val events = parser.eventSpecies(direct)
  val species = parser.closure(direct - events.map { it.dexId }.toSet())
  val eventItems = parser.eventItems()

  val trainers = PlatinumTrainerParser(decompDir).parseAll()
  val moneyCap = MoneyCap.compute(trainers)

  println(
      "[offline] ${direct.size} species on a table, ${species.size} once evolution and breeding " +
          "are followed; ${events.size} held back: " +
          events.joinToString(", ") { "${it.name} behind ${it.gateItem}" })
  println("[offline] money cap $moneyCap over ${trainers.size} trainers")
  println(
      "[offline] ${eventItems.size} key item(s) a script asks after and none hands out: $eventItems")

  OfflineRenderer(templatesDir, outputDir, classCacheDir)
      .render(
          ParsedCartridgeLimits(
              speciesIds = species.sorted(),
              directSpeciesIds = direct.sorted(),
              eventSpecies = events,
              eventItemIds = eventItems,
              moneyCap = moneyCap,
              trainerCount = trainers.size,
          ))
  println("[offline] done")
}
