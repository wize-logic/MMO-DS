package de.fiereu.openmmo.codegen.learnset

import de.fiereu.openmmo.codegen.generatedEnumTable
import java.io.File
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

/**
 * Reads the level up learnsets out of the DS decomp's per species data, beside the species
 * table itself.
 */
class NdsLearnsetParser(private val rootDir: File) {

  private val json = Json { ignoreUnknownKeys = true }

  fun parseAll(): List<ParsedLearnset> {
    val speciesIds = generatedEnumTable(File(rootDir, "generated/species.txt"))
    val moveIds = generatedEnumTable(File(rootDir, "generated/moves.txt"))

    return speciesIds
        .filterKeys { it !in NOT_A_SPECIES }
        .mapNotNull { (constant, dexId) ->
          val file =
              File(
                  rootDir, "res/pokemon/${constant.removePrefix("SPECIES_").lowercase()}/data.json")
          require(file.exists()) { "Missing species data for $constant at ${file.path}" }
          val byLevel =
              json
                  .parseToJsonElement(file.readText())
                  .jsonObject
                  .getValue("learnset")
                  .jsonObject
                  .getValue("by_level")
                  .jsonArray
          val moves =
              byLevel.map { entry ->
                val pair = entry.jsonArray
                require(pair.size == 2) { "$constant has a $pair sized learnset entry" }
                val move = pair[1].jsonPrimitive.content
                ParsedLevelUpMove(
                    level = pair[0].jsonPrimitive.int,
                    moveId =
                        moveIds[move] ?: error("$constant learns $move, which is not a known move"),
                )
              }
          if (moves.isEmpty()) null else ParsedLearnset(dexId, moves.sortedBy { it.level })
        }
        .sortedBy { it.dexId }
  }

  private companion object {
    /** Entries in the species table that are not a monster and have no data of their own. */
    val NOT_A_SPECIES = setOf("SPECIES_NONE", "SPECIES_EGG", "SPECIES_BAD_EGG")
  }
}
