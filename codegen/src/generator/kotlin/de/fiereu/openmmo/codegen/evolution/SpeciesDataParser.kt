package de.fiereu.openmmo.codegen.evolution

import de.fiereu.openmmo.codegen.generatedEnumTable
import de.fiereu.openmmo.codegen.item.ITEM_REGION_BLOCK
import de.fiereu.openmmo.common.enums.EggGroup
import de.fiereu.openmmo.common.enums.EvolutionMethod
import de.fiereu.openmmo.common.enums.EvolutionParam
import java.io.File
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

/**
 * Reads the DS decomp's per species data for the parts the GBA decomps cannot express: how a
 * species evolves, and what its egg is. The species ids in this decomp are already national
 * dex numbers, so unlike the GBA data there is nothing to remap.
 */
class SpeciesDataParser(private val rootDir: File) {

  private val json = Json { ignoreUnknownKeys = true }

  fun parseAll(): ParsedSpeciesData {
    val speciesIds = enumTable("species.txt")
    val itemIds = enumTable("items.txt")
    val moveIds = enumTable("moves.txt")
    checkEvolutionMethods(enumTable("evolution_methods.txt"))
    checkEggGroups(enumTable("egg_groups.txt"))

    val evolutions = mutableListOf<ParsedSpeciesEvolutions>()
    val breeding = mutableListOf<ParsedBreeding>()

    for ((constant, dexId) in speciesIds) {
      if (constant in NOT_A_SPECIES) continue
      val file =
          File(rootDir, "res/pokemon/${constant.removePrefix("SPECIES_").lowercase()}/data.json")
      require(file.exists()) { "Missing species data for $constant at ${file.path}" }
      val data = json.parseToJsonElement(file.readText()).jsonObject

      val parsed =
          (data["evolutions"]?.jsonArray ?: JsonArray(emptyList())).map {
            evolutionOf(constant, it.jsonArray, speciesIds, itemIds, moveIds)
          }
      if (parsed.isNotEmpty()) evolutions += ParsedSpeciesEvolutions(dexId, parsed)

      val groups = data.getValue("egg_groups").jsonArray.map { it.jsonPrimitive.content }
      require(groups.size == 2) { "$constant has ${groups.size} egg groups, the game holds two" }
      breeding +=
          ParsedBreeding(
              dexId = dexId,
              offspringSpeciesId =
                  speciesIds.getValue(data.getValue("offspring").jsonPrimitive.content),
              eggGroup1 = eggGroupRef(groups[0]),
              eggGroup2 = eggGroupRef(groups[1]),
              hatchCycles = data.getValue("hatch_cycles").jsonPrimitive.int,
          )
    }

    return ParsedSpeciesData(
        evolutions = evolutions.sortedBy { it.dexId },
        breeding = breeding.sortedBy { it.dexId },
        incenseBabies = readIncenseBabies(speciesIds, itemIds),
        namedSpeciesIds = NAMED_SPECIES.associateWith { speciesIds.getValue("SPECIES_$it") },
    )
  }

  private fun evolutionOf(
      constant: String,
      entry: JsonArray,
      speciesIds: Map<String, Int>,
      itemIds: Map<String, Int>,
      moveIds: Map<String, Int>,
  ): ParsedEvolution {
    val methodName = entry[0].jsonPrimitive.content
    val method =
        EvolutionMethod.entries.find { "EVO_${it.name}" == methodName }
            ?: error("$constant evolves by $methodName, which EvolutionMethod does not have")
    val target = entry.last().jsonPrimitive.content
    val hasParam = entry.size == 3
    require(hasParam == (method.param != EvolutionParam.NONE)) {
      "$constant's $methodName entry is ${entry.size} fields wide, but its parameter is " +
          "${method.param}"
    }

    val param =
        when (method.param) {
          EvolutionParam.NONE -> 0
          EvolutionParam.LEVEL,
          EvolutionParam.BEAUTY -> numberOf(constant, methodName, entry[1])
          EvolutionParam.ITEM ->
              ITEM_REGION_BLOCK + lookup(constant, methodName, entry[1], "ITEM_", itemIds)
          EvolutionParam.MOVE -> lookup(constant, methodName, entry[1], "MOVE_", moveIds)
          EvolutionParam.SPECIES -> lookup(constant, methodName, entry[1], "SPECIES_", speciesIds)
        }
    return ParsedEvolution(
        method = "EvolutionMethod.${method.name}",
        param = param,
        targetSpeciesId = speciesIds.getValue(target),
    )
  }

  private fun numberOf(constant: String, method: String, value: JsonElement): Int =
      value.jsonPrimitive.content.toIntOrNull()
          ?: error("$constant's $method parameter is $value, expected a number")

  private fun lookup(
      constant: String,
      method: String,
      value: JsonElement,
      prefix: String,
      table: Map<String, Int>,
  ): Int {
    val name = value.jsonPrimitive.content
    require(name.startsWith(prefix)) { "$constant's $method parameter $name is not a $prefix id" }
    return table[name] ?: error("$constant's $method parameter $name is not in the decomp's table")
  }

  /**
   * The day care's incense table, which is a literal in its source rather than data. Kept as one
   * source of truth with the game rather than copied into the rules that read it.
   */
  private fun readIncenseBabies(
      speciesIds: Map<String, Int>,
      itemIds: Map<String, Int>,
  ): List<ParsedIncenseBaby> {
    val file = File(rootDir, INCENSE_TABLE_SOURCE)
    require(file.exists()) { "Missing the day care source at ${file.path}" }
    val table =
        Regex("""sIncenseBabyTable\[]\[3]\s*=\s*\{(.*?)};""", RegexOption.DOT_MATCHES_ALL)
            .find(file.readText())
            ?.groupValues
            ?.get(1) ?: error("No sIncenseBabyTable in ${file.path}")
    val rows =
        Regex("""\{\s*(SPECIES_\w+)\s*,\s*(ITEM_\w+)\s*,\s*(SPECIES_\w+)\s*}""")
            .findAll(table)
            .map {
              ParsedIncenseBaby(
                  babySpeciesId = speciesIds.getValue(it.groupValues[1]),
                  incenseItemId = ITEM_REGION_BLOCK + itemIds.getValue(it.groupValues[2]),
                  grownSpeciesId = speciesIds.getValue(it.groupValues[3]),
              )
            }
            .toList()
    require(rows.isNotEmpty()) { "sIncenseBabyTable in ${file.path} parsed as empty" }
    return rows
  }

  /** The evolution method table this build was written against, name for name and in order. */
  private fun checkEvolutionMethods(table: Map<String, Int>) {
    val ours = EvolutionMethod.entries.associate { "EVO_${it.name}" to it.id }
    require(table == ours) {
      "The decomp's evolution methods have moved. Only in the decomp: " +
          "${table.entries - ours.entries}; only in EvolutionMethod: ${ours.entries - table.entries}"
    }
  }

  /** Same for the egg groups, whose one rename we carry deliberately. */
  private fun checkEggGroups(table: Map<String, Int>) {
    val ours =
        EggGroup.entries.associate {
          val name = if (it == EggGroup.NO_EGGS_DISCOVERED) "UNDISCOVERED" else it.name
          "EGG_GROUP_$name" to it.ordinal
        }
    require(table == ours) {
      "The decomp's egg groups have moved. Only in the decomp: ${table.entries - ours.entries}; " +
          "only in EggGroup: ${ours.entries - table.entries}"
    }
  }

  private fun eggGroupRef(token: String): String {
    val name = token.removePrefix("EGG_GROUP_")
    return "EggGroup.${if (name == "UNDISCOVERED") "NO_EGGS_DISCOVERED" else name}"
  }

  private fun enumTable(name: String): Map<String, Int> =
      generatedEnumTable(File(rootDir, "generated/$name"))

  private companion object {
    const val INCENSE_TABLE_SOURCE = "src/overlay005/daycare.c"

    /** Entries in the species table that are not a monster and have no data of their own. */
    val NOT_A_SPECIES = setOf("SPECIES_NONE", "SPECIES_EGG", "SPECIES_BAD_EGG")

    /** The species the hand written breeding and evolution rules name. */
    val NAMED_SPECIES =
        listOf(
            "DITTO",
            "ILLUMISE",
            "KADABRA",
            "MANAPHY",
            "NIDORAN_F",
            "NIDORAN_M",
            "PHIONE",
            "VOLBEAT",
        )
  }
}
