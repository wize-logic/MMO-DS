package de.fiereu.openmmo.codegen.pokemon

import de.fiereu.openmmo.codegen.generatedEnumTable
import de.fiereu.openmmo.codegen.item.ITEM_REGION_BLOCK
import de.fiereu.openmmo.common.enums.Ability
import de.fiereu.openmmo.common.enums.BodyColor
import de.fiereu.openmmo.common.enums.EggGroup
import de.fiereu.openmmo.common.enums.GrowthRate
import de.fiereu.openmmo.common.enums.PokemonType
import java.io.File
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.boolean
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

/** Reads the DS decomp's per species data, which is the generation this server hosts. */
class NdsSpeciesParser(
    private val rootDir: File,
    /**
     * The hidden ability of every species, which is not in this decomp at all: Gen 5 invented the
     * third slot and Gen 4's species data has no room for one.
     */
    private val hiddenAbilities: Map<Int, String>,
) {

  private val json = Json { ignoreUnknownKeys = true }

  private val speciesIds = enumTable("species.txt")
  private val itemIds = enumTable("items.txt")
  private val types = enumTable("pokemon_types.txt")
  private val abilities = enumTable("abilities.txt")
  private val expRates = enumTable("exp_rates.txt")
  private val eggGroups = enumTable("egg_groups.txt")
  private val colors = enumTable("pokemon_colors.txt")
  private val genderRatios = valueTable("gender_ratios.txt")

  fun parseAll(): List<ParsedSpecies> =
      speciesIds
          .filterKeys { it !in NOT_A_SPECIES }
          .map { (constant, dexId) -> parseOne(constant, dexId) }
          .sortedBy { it.id }

  private fun parseOne(constant: String, dexId: Int): ParsedSpecies {
    val file =
        File(rootDir, "res/pokemon/${constant.removePrefix("SPECIES_").lowercase()}/data.json")
    require(file.exists()) { "Missing species data for $constant at ${file.path}" }
    val data = json.parseToJsonElement(file.readText()).jsonObject

    val stats = data.getValue("base_stats").jsonObject
    val evs = data.getValue("ev_yields").jsonObject
    val heldItems = data.getValue("held_items").jsonObject
    val monTypes = data.getValue("types").jsonArray.map { it.jsonPrimitive.content }
    val monAbilities = data.getValue("abilities").jsonArray.map { it.jsonPrimitive.content }
    val monEggGroups = data.getValue("egg_groups").jsonArray.map { it.jsonPrimitive.content }
    require(monTypes.size == 2 && monAbilities.size == 2 && monEggGroups.size == 2) {
      "$constant has ${monTypes.size} types, ${monAbilities.size} abilities and " +
          "${monEggGroups.size} egg groups, the game holds two of each"
    }
    val ratio = data.getValue("gender_ratio").jsonPrimitive.content

    return ParsedSpecies(
        id = dexId,
        name =
            data
                .getValue("pokedex_data")
                .jsonObject
                .getValue("en")
                .jsonObject
                .getValue("name")
                .jsonPrimitive
                .content,
        baseHp = stats.getValue("hp").jsonPrimitive.int,
        baseAttack = stats.getValue("attack").jsonPrimitive.int,
        baseDefense = stats.getValue("defense").jsonPrimitive.int,
        baseSpeed = stats.getValue("speed").jsonPrimitive.int,
        baseSpAttack = stats.getValue("special_attack").jsonPrimitive.int,
        baseSpDefense = stats.getValue("special_defense").jsonPrimitive.int,
        type1 = ref(monTypes[0], "TYPE_", types, PokemonType.entries.map { it.name }, ::typeName),
        type2 = ref(monTypes[1], "TYPE_", types, PokemonType.entries.map { it.name }, ::typeName),
        catchRate = data.getValue("catch_rate").jsonPrimitive.int,
        expYield = data.getValue("base_exp_reward").jsonPrimitive.int,
        evYieldHp = evs.getValue("hp").jsonPrimitive.int,
        evYieldAttack = evs.getValue("attack").jsonPrimitive.int,
        evYieldDefense = evs.getValue("defense").jsonPrimitive.int,
        evYieldSpeed = evs.getValue("speed").jsonPrimitive.int,
        evYieldSpAttack = evs.getValue("special_attack").jsonPrimitive.int,
        evYieldSpDefense = evs.getValue("special_defense").jsonPrimitive.int,
        itemCommon = itemId(constant, heldItems.getValue("common").jsonPrimitive.content),
        itemRare = itemId(constant, heldItems.getValue("rare").jsonPrimitive.content),
        genderRatio =
            genderRatios[ratio]
                ?: error("$constant's gender ratio $ratio is not in the decomp's table"),
        eggCycles = data.getValue("hatch_cycles").jsonPrimitive.int,
        friendship = data.getValue("base_friendship").jsonPrimitive.int,
        growthRate =
            ref(
                data.getValue("exp_rate").jsonPrimitive.content,
                "EXP_RATE_",
                expRates,
                GrowthRate.entries.map { it.name },
            ),
        eggGroup1 = ref(monEggGroups[0], "EGG_GROUP_", eggGroups, eggGroupNames(), ::eggGroupName),
        eggGroup2 = ref(monEggGroups[1], "EGG_GROUP_", eggGroups, eggGroupNames(), ::eggGroupName),
        ability1 = ref(monAbilities[0], "ABILITY_", abilities, Ability.entries.map { it.name }),
        ability2 = ref(monAbilities[1], "ABILITY_", abilities, Ability.entries.map { it.name }),
        abilityHidden =
            "Ability." +
                (hiddenAbilities[dexId]
                    ?: error("$constant has no hidden ability row in the Gen 5 table")),
        safariZoneFleeRate = data.getValue("safari_flee_rate").jsonPrimitive.int,
        bodyColor =
            ref(
                data.getValue("body_color").jsonPrimitive.content,
                "MON_COLOR_",
                colors,
                BodyColor.entries.map { it.name },
            ),
        flipSprite = data.getValue("flip_sprite").jsonPrimitive.boolean,
    )
  }

  /** Held items are wire ids, the block every other server table names an item in. */
  private fun itemId(constant: String, token: String): Int {
    if (token == "ITEM_NONE") return 0
    val index = itemIds[token] ?: error("$constant holds $token, which is not in the item table")
    return ITEM_REGION_BLOCK + index
  }

  private fun ref(
      token: String,
      prefix: String,
      table: Map<String, Int>,
      ours: List<String>,
      rename: (String) -> String = { it },
  ): String {
    val theirs = table[token] ?: error("$token is not one of the decomp's ${enumOf(prefix)} values")
    val name = rename(token.removePrefix(prefix))
    val mine = ours.indexOf(name)
    require(mine == theirs) {
      "$token is slot $theirs in the decomp and slot $mine in ${enumOf(prefix)} (as $name)"
    }
    return "${enumOf(prefix)}.$name"
  }

  private fun enumTable(name: String): Map<String, Int> =
      generatedEnumTable(File(rootDir, "generated/$name"))

  /** The tables whose constants carry their own value rather than taking their line number. */
  private fun valueTable(name: String): Map<String, Int> {
    val file = File(rootDir, "generated/$name")
    require(file.exists()) { "Missing generated constant table at ${file.path}" }
    val regex = Regex("""^(\w+)\s*=\s*(\d+)$""")
    val table =
        file
            .readLines()
            .mapNotNull { regex.find(it.trim()) }
            .associate { it.groupValues[1] to it.groupValues[2].toInt() }
    require(table.isNotEmpty()) { "No valued constants in ${file.path}" }
    return table
  }

  private companion object {
    /** Entries in the species table that are not a monster and have no data of their own. */
    val NOT_A_SPECIES = setOf("SPECIES_NONE", "SPECIES_EGG", "SPECIES_BAD_EGG")

    fun eggGroupNames(): List<String> = EggGroup.entries.map { it.name }

    /** The one type we spell differently, because `???` is not an identifier. */
    fun typeName(name: String): String = if (name == "MYSTERY") "QUESTIONQUESTIONQUESTION" else name

    /** And the one egg group, whose rename the breeding table already carries. */
    fun eggGroupName(name: String): String =
        if (name == "UNDISCOVERED") "NO_EGGS_DISCOVERED" else name

    fun enumOf(prefix: String): String =
        when (prefix) {
          "TYPE_" -> "PokemonType"
          "ABILITY_" -> "Ability"
          "EXP_RATE_" -> "GrowthRate"
          "EGG_GROUP_" -> "EggGroup"
          "MON_COLOR_" -> "BodyColor"
          else -> error("No enum for $prefix")
        }
  }
}
