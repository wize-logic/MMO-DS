package de.fiereu.openmmo.codegen.trainer

import de.fiereu.openmmo.common.enums.MAX_IV
import java.io.File
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.int
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

/** Platinum's trainers, which are one JSON file each rather than two C tables. */
class PlatinumTrainerParser(private val rootDir: File) {

  private val json = Json { ignoreUnknownKeys = true }
  private val trainerIds = enumTable("trainers.txt")
  private val classIds = enumTable("trainer_classes.txt")
  private val speciesIds = enumTable("species.txt")
  private val moveIds = enumTable("moves.txt")
  private val itemIds = enumTable("items.txt")

  fun parseAll(): List<ParsedTrainer> {
    val prizeRates = readPrizeRates()
    val dataDir = File(rootDir, "res/trainers/data")
    return trainerIds
        .filterKeys { it != "TRAINER_NONE" && it.startsWith("TRAINER_") }
        .mapNotNull { (constant, id) ->
          val file = File(dataDir, constant.removePrefix("TRAINER_").lowercase() + ".json")
          if (!file.exists()) {
            println("[trainer] skipped $constant, ${file.name} is not in res/trainers/data")
            return@mapNotNull null
          }
          val obj = json.parseToJsonElement(file.readText()).jsonObject
          val party = obj["party"]?.jsonArray.orEmpty().mapNotNull { toMon(it.jsonObject) }
          if (party.isEmpty()) {
            println("[trainer] skipped $constant, ${file.name} has no usable members")
            return@mapNotNull null
          }
          val trainerClass = obj["class"]?.jsonPrimitive?.contentOrNull.orEmpty()
          ParsedTrainer(
              id = id,
              name = obj["name"]?.jsonPrimitive?.contentOrNull.orEmpty(),
              trainerClass = classIds[trainerClass] ?: 0,
              doubleBattle = obj["double_battle"]?.jsonPrimitive?.contentOrNull == "true",
              prizeRate = prizeRates[trainerClass] ?: 0,
              party = party,
              defeatedFlag = "sinnoh/FLAG_DEFEATED_$constant",
          )
        }
        .sortedBy { it.id }
  }

  private fun toMon(member: JsonObject): ParsedTrainerMon? {
    val dexId = speciesIds[member["species"]?.jsonPrimitive?.contentOrNull] ?: return null
    val level = member["level"]?.jsonPrimitive?.intOrNull ?: return null
    // `moves` is null on the 1,188 members that fight with their level-up set, and a list on the
    // other 690.
    val moves =
        (member["moves"] as? JsonArray)
            ?.mapNotNull { moveIds[it.jsonPrimitive.contentOrNull] }
            ?.filter { it != 0 }
            .orEmpty()
    // The same 0 to 255 difficulty the GBA parties carry, scaled once here. One member, Volkner's
    // Electivire, is written as 2500, which is the field's own units overrun rather than an IV.
    val difficulty = (member["iv_scale"]?.jsonPrimitive?.int ?: 0).coerceIn(0, 255)
    return ParsedTrainerMon(
        dexId = dexId,
        level = level,
        iv = difficulty * MAX_IV / 255,
        heldItem = itemIds[member["item"]?.jsonPrimitive?.contentOrNull] ?: 0,
        moveIds = moves,
    )
  }

  /**
   * `sTrainerClassPrizeMul`, which pays a trainer class this much per level of its last
   * monster, the same shape as the GBA's `gTrainerMoneyTable`, as a designated-initializer
   * array rather than a list of pairs.
   */
  private fun readPrizeRates(): Map<String, Int> {
    val file = File(rootDir, "include/data/trainer_class_prize_mul.h")
    require(file.exists()) { "Decomp not initialized at $rootDir (missing ${file.path})" }
    val rows =
        PRIZE_ROW.findAll(file.readText()).associate {
          it.groupValues[1] to it.groupValues[2].toInt()
        }
    require(rows.isNotEmpty()) { "${file.path} parsed to no rows; its shape changed" }
    return rows
  }

  /** An enum dump under `generated/`, whose line order is the value. */
  private fun enumTable(name: String): Map<String, Int> {
    val file = File(rootDir, "generated/$name")
    require(file.exists()) { "Decomp not initialized at $rootDir (missing ${file.path})" }
    return file
        .readLines()
        .map { it.trim() }
        .withIndex()
        .filter { (_, line) -> line.isNotEmpty() && !line.contains('=') }
        .associate { (index, line) -> line to index }
  }

  private companion object {
    val PRIZE_ROW = Regex("""\[(TRAINER_CLASS_\w+)]\s*=\s*(\d+)""")
  }
}
