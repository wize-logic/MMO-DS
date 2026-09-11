package de.fiereu.openmmo.codegen.learnset

import de.fiereu.openmmo.codegen.generatedEnumTable
import java.io.File
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

/**
 * The three move lists that sit beside the level up learnset in the DS decomp's per species data:
 * `by_tm`, `egg_moves` and `by_tutor`.
 */
class NdsMoveSourceParser(private val rootDir: File) {

  private val json = Json { ignoreUnknownKeys = true }

  fun parseAll(): List<ParsedMoveSources> {
    val speciesIds = generatedEnumTable(File(rootDir, "generated/species.txt"))
    val moveIds = generatedEnumTable(File(rootDir, "generated/moves.txt"))
    val machineMoves = readMachineMoves(moveIds)

    return speciesIds
        .filterKeys { it !in NOT_A_SPECIES }
        .mapNotNull { (constant, dexId) ->
          val file =
              File(
                  rootDir, "res/pokemon/${constant.removePrefix("SPECIES_").lowercase()}/data.json")
          require(file.exists()) { "Missing species data for $constant at ${file.path}" }
          val learnset =
              json.parseToJsonElement(file.readText()).jsonObject.getValue("learnset").jsonObject

          val machines =
              learnset["by_tm"]?.jsonArray.orEmpty().map { entry ->
                val machine = entry.jsonPrimitive.content
                machineMoves[machine]
                    ?: error("$constant is compatible with $machine, which teaches nothing")
              }
          val eggMoves =
              learnset["egg_moves"]?.jsonArray.orEmpty().map { entry ->
                val move = entry.jsonPrimitive.content
                moveIds[move] ?: error("$constant hatches knowing $move, which is not a known move")
              }
          val tutorMoves =
              learnset["by_tutor"]?.jsonArray.orEmpty().map { entry ->
                val move = entry.jsonPrimitive.content
                moveIds[move] ?: error("$constant is tutored $move, which is not a known move")
              }

          if (machines.isEmpty() && eggMoves.isEmpty() && tutorMoves.isEmpty()) {
            null
          } else {
            ParsedMoveSources(
                dexId = dexId,
                machineMoveIds = machines.distinct().sorted(),
                eggMoveIds = eggMoves.distinct().sorted(),
                tutorMoveIds = tutorMoves.distinct().sorted(),
            )
          }
        }
        .sortedBy { it.dexId }
  }

  /** Machine name to the move it teaches, off the machines' own item data. */
  private fun readMachineMoves(moveIds: Map<String, Int>): Map<String, Int> {
    val dataDir = File(rootDir, "res/items/data")
    require(dataDir.isDirectory) { "Missing item data at ${dataDir.path}" }
    val machines =
        dataDir
            .listFiles { file -> MACHINE_FILE.matches(file.name) }
            .orEmpty()
            .associate { file ->
              val obj = json.parseToJsonElement(file.readText()).jsonObject
              val name = obj.getValue("name").jsonPrimitive.content
              val move = obj.getValue("teachesMove").jsonPrimitive.content
              name to (moveIds[move] ?: error("$name teaches $move, which is not a known move"))
            }
    require(machines.isNotEmpty()) { "No machines in ${dataDir.path}; its shape changed" }
    return machines
  }

  private companion object {
    /** Entries in the species table that are not a monster and have no data of their own. */
    val NOT_A_SPECIES = setOf("SPECIES_NONE", "SPECIES_EGG", "SPECIES_BAD_EGG")

    val MACHINE_FILE = Regex("""^(tm|hm)\d+\.json$""")
  }
}
