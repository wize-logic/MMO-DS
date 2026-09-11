package de.fiereu.openmmo.codegen.trainer

import de.fiereu.openmmo.common.enums.MAX_IV
import java.io.File
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

/**
 * HeartGold's trainers, which are one json array rather than a file each or two C tables: index in
 * `files/poketool/trainer/trainers.json` is the trainer id, the same number a ported map's people
 * carry (`tools/portmap.py --trainers`, where a person's trainer is their line minus one).
 */
class HeartGoldTrainerParser(private val rootDir: File) {

  private val json = Json { ignoreUnknownKeys = true }
  private val trainerIds = defines("trainers.h", "TRAINER_")
  private val classIds = defines("trainer_class.h", "TRAINERCLASS_")
  private val speciesIds = defines("species.h", "SPECIES_")
  private val moveIds = defines("moves.h", "MOVE_")
  private val itemIds = defines("items.h", "ITEM_")

  fun parseAll(): List<ParsedTrainer> {
    val prizeRates = readPrizeRates()
    val names = trainerIds.entries.associate { (name, id) -> id to name }
    val file = File(rootDir, "files/poketool/trainer/trainers.json")
    require(file.exists()) { "Decomp not initialized at $rootDir (missing ${file.path})" }
    val all =
        json.parseToJsonElement(file.readText()).jsonObject["trainers"]?.jsonArray
            ?: error("${file.path} has no `trainers` array; its shape changed")
    val awards = readBadgeAwards(all)

    return all.mapIndexedNotNull { id, element ->
          if (id == 0) return@mapIndexedNotNull null // TRAINER_NONE, the empty slot
          val obj = element.jsonObject
          val party = obj["party"]?.jsonArray.orEmpty().mapNotNull { toMon(it.jsonObject) }
          if (party.isEmpty()) {
            println("[trainer] skipped johto trainer $id, it has no usable members")
            return@mapIndexedNotNull null
          }
          val trainerClass = obj["class"]?.jsonPrimitive?.contentOrNull.orEmpty()
          ParsedTrainer(
              id = id,
              // The name carries the game's own `{TRNAME}` prefix, which is a text-template marker
              // and not part of what anybody is called.
              name = obj["name"]?.jsonPrimitive?.contentOrNull.orEmpty().removePrefix("{TRNAME}"),
              trainerClass = classIds[trainerClass] ?: 0,
              doubleBattle = (obj["double"]?.jsonPrimitive?.intOrNull ?: 0) != 0,
              prizeRate = prizeRates[trainerClass] ?: 0,
              party = party,
              defeatedFlag = names[id]?.let { "johto/FLAG_DEFEATED_$it" } ?: "",
              badge = awards[trainerClass] ?: "",
          )
        }
        .sortedBy { it.id }
  }

  /** Which badge a trainer class earns the player, out of the gym scripts. */
  private fun readBadgeAwards(trainers: JsonArray): Map<String, String> {
    val badgeIds = defines("badge.h", "BADGE_")
    val dir = File(rootDir, "files/fielddata/script/scr_seq")
    if (!dir.isDirectory) return emptyMap()
    val scripts = dir.listFiles { f -> f.name.endsWith(".s") }.orEmpty()
    val byArea = scripts.groupBy { areaOf(it.name) }
    val classOf =
        trainers.withIndex().associate { (id, element) ->
          (trainerIds.entries.firstOrNull { it.value == id }?.key ?: "") to
              element.jsonObject["class"]?.jsonPrimitive?.contentOrNull.orEmpty()
        }
    val out = mutableMapOf<String, String>()
    for (script in scripts) {
      val badge = GIVE_BADGE.find(script.readText())?.groupValues?.get(1) ?: continue
      val id =
          badgeIds[badge] ?: error("${script.name} gives $badge, which badge.h does not number")
      val region = if (id < KANTO_FIRST_BADGE) "johto" else "kanto"
      val key = "$region/BADGE_ID_${badge.removePrefix("BADGE_")}"
      val own = leaderClasses(listOf(script), classOf)
      val leaders = own.ifEmpty { leaderClasses(byArea[areaOf(script.name)].orEmpty(), classOf) }
      require(leaders.size == 1) {
        "${script.name} gives $badge and names ${leaders.size} leader(s): $leaders"
      }
      out[leaders.single()] = key
    }
    println("[trainer] ${out.size} leader class(es) award a badge out of ${byArea.size} areas")
    return out
  }

  /** The classes of every `TRAINER_LEADER_*` the scripts fight. */
  private fun leaderClasses(scripts: List<File>, classOf: Map<String, String>): Set<String> =
      scripts
          .flatMap { LEADER.findAll(it.readText()).map { m -> m.value }.toList() }
          .mapNotNull { classOf[it] }
          .filter { it.isNotEmpty() }
          .toSet()

  /** The map-area code of a script file: `scr_seq_0859_T22GYM0101.s` is area `T22`. */
  private fun areaOf(name: String): String =
      name.removePrefix("scr_seq_").substringAfter('_').take(3)

  private fun toMon(member: JsonObject): ParsedTrainerMon? {
    val dexId = speciesIds[member["species"]?.jsonPrimitive?.contentOrNull] ?: return null
    val level = member["level"]?.jsonPrimitive?.intOrNull ?: return null
    val moves =
        (member["moves"] as? JsonArray)
            ?.mapNotNull { moveIds[it.jsonPrimitive.contentOrNull] }
            ?.filter { it != 0 }
            .orEmpty()
    // The same 0 to 255 scale the other two games' parties carry, under the name the source gives
    // it. Nothing in this cartridge writes past 250.
    val difficulty = (member["difficulty"]?.jsonPrimitive?.intOrNull ?: 0).coerceIn(0, 255)
    return ParsedTrainerMon(
        dexId = dexId,
        level = level,
        iv = difficulty * MAX_IV / 255,
        heldItem = itemIds[member["item"]?.jsonPrimitive?.contentOrNull] ?: 0,
        moveIds = moves,
    )
  }

  /**
   * `sPrizeMoneyTbl`, which pays a trainer class this much per level of its last monster. It is
   * still assembly here, as a list of `(class, rate)` shorts; a class the table leaves out pays
   * nothing.
   */
  private fun readPrizeRates(): Map<String, Int> {
    val file = File(rootDir, "asm/overlay_12_battle_command.s")
    require(file.exists()) { "Decomp not initialized at $rootDir (missing ${file.path})" }
    val body = file.readText().substringAfter("sPrizeMoneyTbl:", "")
    require(body.isNotEmpty()) { "${file.path} has no sPrizeMoneyTbl; its shape changed" }
    val rows =
        PRIZE_ROW.findAll(body.substringBefore("\n\t.balign")).associate {
          it.groupValues[1] to it.groupValues[2].toInt()
        }
    require(rows.isNotEmpty()) { "${file.path} parsed to no prize rows; its shape changed" }
    return rows
  }

  /** A `#define NAME value` header, which is how this decomp keeps its enums. */
  private fun defines(name: String, prefix: String): Map<String, Int> {
    val file = File(rootDir, "include/constants/$name")
    require(file.exists()) { "Decomp not initialized at $rootDir (missing ${file.path})" }
    return file
        .readLines()
        .mapNotNull { DEFINE_ROW.find(it.trim())?.groupValues }
        .filter { it[1].startsWith(prefix) }
        .associate { it[1] to it[2].toInt() }
  }

  private companion object {
    val PRIZE_ROW = Regex("""\.short\s+(TRAINERCLASS_\w+),\s*(\d+)""")
    val DEFINE_ROW = Regex("""^#define\s+(\w+)\s+(\d+)\s*$""")
  }
}

private val GIVE_BADGE = Regex("""GiveBadge\s+(BADGE_\w+)""")
private val LEADER = Regex("""TRAINER_LEADER_\w+""")

/** HeartGold's `BADGE_BOULDER`: the first of Kanto's eight. */
private const val KANTO_FIRST_BADGE = 8
