package de.fiereu.openmmo.codegen.offline

import de.fiereu.openmmo.codegen.generatedEnumTable
import java.io.File
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

/** Every species one Platinum cartridge can put in a save without help from a second game. */
class PlatinumSourceParser(private val rootDir: File) {

  private val json = Json { ignoreUnknownKeys = true }
  private val speciesIds = generatedEnumTable(File(rootDir, "generated/species.txt"))
  private val scripts =
      File(rootDir, "res/field/scripts")
          .listFiles { file -> file.name.endsWith(".s") }
          .orEmpty()
          .sortedBy { it.name }
          .map { it.readText() }

  /** Species named by a table, before evolution and breeding are followed. */
  fun directSpecies(): Set<Int> =
      encounterSpecies() +
          tradeSpecies() +
          scriptSpecies() +
          fossilSpecies() +
          starterSpecies() +
          roamerSpecies()

  /**
   * The set closed over evolution and breeding: what evolves from something reachable is reachable,
   * and so is what something reachable breeds into.
   */
  fun closure(direct: Set<Int>): Set<Int> {
    val evolvesInto = mutableMapOf<Int, MutableSet<Int>>()
    val breedsInto = mutableMapOf<Int, Int>()
    for ((constant, dexId) in speciesIds) {
      if (constant in NOT_A_SPECIES) continue
      val data = speciesData(constant) ?: continue
      data["evolutions"]?.jsonArray?.forEach { row ->
        val target = row.jsonArray.last().jsonPrimitive.content
        if (target != "SPECIES_NONE") {
          evolvesInto.getOrPut(dexId) { mutableSetOf() } += speciesId(target, "$constant evolves")
        }
      }
      val offspring = data["offspring"]?.jsonPrimitive?.content
      if (offspring != null && offspring != "SPECIES_NONE" && offspring != constant) {
        breedsInto[dexId] = speciesId(offspring, "$constant breeds")
      }
    }

    val reached = direct.toMutableSet()
    var pending = ArrayDeque(direct)
    while (pending.isNotEmpty()) {
      val next = pending.removeFirst()
      val onward = evolvesInto[next].orEmpty() + listOfNotNull(breedsInto[next])
      for (species in onward) if (reached.add(species)) pending.addLast(species)
    }
    return reached
  }

  /** The species behind a key item nothing hands out. */
  fun eventSpecies(direct: Set<Int>): List<ParsedEventSpecies> {
    val ungranted = eventItemConstants()
    val tabled = encounterSpecies() + tradeSpecies() + fossilSpecies() + starterSpecies()
    return EVENT_GIFTS.map { (species, item) ->
      require(item in ungranted) {
        "$item is handed out by a script now, so $species is no longer a Mystery Gift species"
      }
      val dexId = speciesId(species, "event gift")
      require(dexId !in tabled) { "$species is on a table of the cartridge's own now" }
      require(dexId in direct) { "$species is on no source at all; the gift script moved" }
      ParsedEventSpecies(dexId, species.removePrefix("SPECIES_"), item)
    }
  }

  /** The items a script asks after that nothing in the cartridge ever puts in the bag. */
  fun eventItems(): List<Int> {
    val itemIds = generatedEnumTable(File(rootDir, "generated/items.txt"))
    return eventItemConstants()
        .map { constant ->
          itemIds[constant] ?: error("a script asks after $constant, which is not an item")
        }
        .sorted()
  }

  private fun eventItemConstants(): Set<String> {
    val text = scripts.joinToString("\n")
    val given =
        ADD_ITEM.findAll(text).map { it.groupValues[1] }.toSet() +
            SET_ITEM_VAR.findAll(text).map { it.groupValues[1] }.toSet()
    val asked = CHECK_ITEM.findAll(text).map { it.groupValues[1] }.toSet()
    val found = asked - given
    require(found.isNotEmpty()) { "No event items in the field scripts; their shape changed" }
    return found
  }

  /** Everything the wild encounter archives can roll, minus the five dual-slot lists. */
  private fun encounterSpecies(): Set<Int> {
    val dir = File(rootDir, "res/field/encounters")
    require(dir.isDirectory) { "Missing encounter archives at ${dir.path}" }
    val found = mutableSetOf<Int>()
    for (file in dir.listFiles { f -> f.name.endsWith(".json") }.orEmpty()) {
      val obj = json.parseToJsonElement(file.readText()).jsonObject
      for ((key, value) in obj) {
        if (key in DUAL_SLOT_LISTS || value !is JsonArray) continue
        for (entry in value) {
          val name =
              when (entry) {
                is JsonObject -> entry["species"]?.jsonPrimitive?.content
                else -> entry.jsonPrimitive.content
              }
          if (name != null && name.startsWith("SPECIES_") && name != "SPECIES_NONE") {
            found += speciesId(name, file.name)
          }
        }
      }
    }
    require(found.isNotEmpty()) { "No encounter species in ${dir.path}; its shape changed" }
    return found
  }

  /** What an in-game trade hands over, which is the trade's own `species` field. */
  private fun tradeSpecies(): Set<Int> {
    val dir = File(rootDir, "res/npc_trades")
    require(dir.isDirectory) { "Missing in-game trades at ${dir.path}" }
    val found =
        dir.listFiles { f -> f.name.endsWith(".json") }
            .orEmpty()
            .map { file ->
              val name =
                  json
                      .parseToJsonElement(file.readText())
                      .jsonObject
                      .getValue("species")
                      .jsonPrimitive
                      .content
              speciesId(name, file.name)
            }
            .toSet()
    require(found.isNotEmpty()) { "No in-game trades in ${dir.path}; its shape changed" }
    return found
  }

  /** The gifts, the eggs and the fixed battles the field scripts name outright. */
  private fun scriptSpecies(): Set<Int> {
    val found =
        scripts
            .flatMap { text -> SCRIPT_SPECIES.findAll(text).map { it.groupValues[2] }.toList() }
            .map { speciesId(it, "a field script") }
            .toSet()
    require(found.isNotEmpty()) { "No scripted species in the field scripts; their shape changed" }
    return found
  }

  private fun fossilSpecies(): Set<Int> =
      sweep(File(rootDir, "src/scrcmd_fossil.c"), FOSSIL_ROW, "the fossil table")

  private fun starterSpecies(): Set<Int> =
      sweep(
          File(rootDir, "src/choose_starter/choose_starter_app.c"),
          STARTER_ROW,
          "the starter options")

  private fun roamerSpecies(): Set<Int> =
      sweep(File(rootDir, "src/roaming_pokemon.c"), ROAMER_ROW, "the roamer table")

  private fun sweep(file: File, row: Regex, what: String): Set<Int> {
    require(file.exists()) { "Missing $what at ${file.path}" }
    val found = row.findAll(file.readText()).map { speciesId(it.groupValues[1], what) }.toSet()
    require(found.isNotEmpty()) { "No species in $what at ${file.path}; its shape changed" }
    return found
  }

  private fun speciesData(constant: String): JsonObject? {
    val file =
        File(rootDir, "res/pokemon/${constant.removePrefix("SPECIES_").lowercase()}/data.json")
    return if (file.exists()) json.parseToJsonElement(file.readText()).jsonObject else null
  }

  private fun speciesId(constant: String, where: String): Int =
      speciesIds[constant] ?: error("$where names $constant, which is not a species")

  private companion object {
    val NOT_A_SPECIES = setOf("SPECIES_NONE", "SPECIES_EGG", "SPECIES_BAD_EGG")

    /** The five slot-2 cartridges. Every encounter archive carries a list per game. */
    val DUAL_SLOT_LISTS = setOf("ruby", "sapphire", "emerald", "firered", "leafgreen")

    /**
     * A script handing a monster over, hatching one, or standing one in front of the player. Every
     * command in the field scripts that takes a species and produces one, the rest of them play a
     * cry, draw a preview or ask the Pokedex.
     */
    val SCRIPT_SPECIES =
        Regex(
            """\b(GivePokemon|GiveEgg|StartWildBattle|StartLegendaryBattle""" +
                """|StartFatefulEncounter|StartGiratinaOriginBattle)\s+(SPECIES_\w+)""")

    val ADD_ITEM = Regex("""\bAddItem\s+(ITEM_\w+)""")
    val CHECK_ITEM = Regex("""\bCheckItem\s+(ITEM_\w+)""")
    val SET_ITEM_VAR = Regex("""\bSetVar\s+VAR_\w+,\s*(ITEM_\w+)""")
    val FOSSIL_ROW = Regex("""\.species\s*=\s*(SPECIES_\w+)""")
    val STARTER_ROW = Regex("""#define\s+STARTER_OPTION_\d+\s+(SPECIES_\w+)""")
    val ROAMER_ROW = Regex("""\bspecies\s*=\s*(SPECIES_\w+)\s*;""")

    /**
     * The distribution monsters: each has a battle script on a map of its own, and each of those
     * maps is behind a key item that arrives only over Nintendo's own event service.
     */
    val EVENT_GIFTS =
        listOf(
            "SPECIES_DARKRAI" to "ITEM_MEMBER_CARD",
            "SPECIES_SHAYMIN" to "ITEM_OAKS_LETTER",
            "SPECIES_ARCEUS" to "ITEM_AZURE_FLUTE",
        )
  }
}
