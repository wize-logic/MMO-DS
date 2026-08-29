package de.fiereu.openmmo.codegen.pokemon

import de.fiereu.openmmo.codegen.defineTable
import java.io.File

/**
 * The national dex ids a GBA decomp actually has a monster for. Its species table carries
 * placeholder entries with no stats behind them, and the learnset and trainer readers use this
 * to drop the learnsets and party members that point at one.
 */
object GbaSpeciesIds {

  fun read(rootDir: File): Set<Int> {
    val file = File(rootDir, "src/data/pokemon/species_info.h")
    require(file.exists()) {
      "Decomp not initialized at $rootDir (missing ${file.path}). " +
          "Run: git submodule update --init --recursive"
    }
    val ids = defineTable(File(rootDir, "include/constants/species.h"), "SPECIES_")
    val nationalDex = NationalDex.read(rootDir)
    val text = file.readText()
    val entryStart = Regex("""\[(SPECIES_\w+)]\s*=""")
    val matches = entryStart.findAll(text).toList()
    return matches
        .mapIndexedNotNull { i, match ->
          val end = if (i + 1 < matches.size) matches[i + 1].range.first else text.length
          val body = text.substring(match.range.last + 1, end)
          if (!body.contains(".baseHP")) return@mapIndexedNotNull null
          ids[match.groupValues[1]]?.let { nationalDex[it] }
        }
        .toSet()
  }
}
