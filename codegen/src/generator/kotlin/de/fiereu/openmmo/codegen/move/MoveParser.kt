package de.fiereu.openmmo.codegen.move

import de.fiereu.openmmo.codegen.generatedEnumTable
import java.io.File
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

/**
 * Reads the move table out of the DS decomp, which is the only one of the trees that has all
 * of it: the GBA table stops at Psycho Boost (354) and the game the client draws has 467
 * moves, so Ambipom's Double Hit and everything else Gen 4 added had no entry at all.
 */
class MoveParser(private val rootDir: File) {

  private val json = Json { ignoreUnknownKeys = true }

  fun parseAll(): List<ParsedMove> {
    val effects = constantTable("move_battle_effects.txt")

    return constantTable("moves.txt")
        .entries
        .sortedBy { it.value }
        .filterNot { (constant, id) -> id == 0 || constant in NOT_A_MOVE }
        .map { (constant, id) -> parseMove(id, constant, effects) }
  }

  private fun parseMove(id: Int, constant: String, effects: Map<String, Int>): ParsedMove {
    val name = constant.removePrefix("MOVE_").lowercase()
    val file = File(rootDir, "res/moves/$name/data.json")
    require(file.exists()) { "Missing move data for $constant at ${file.path}" }
    val data = json.parseToJsonElement(file.readText()).jsonObject
    val effect = data.getValue("effect").jsonObject

    return ParsedMove(
        id = id,
        name = data.getValue("name").jsonPrimitive.content,
        effect = effectRef(effects, effect.getValue("type").jsonPrimitive.content),
        power = data.getValue("power").jsonPrimitive.int,
        type = typeRef(data.getValue("type").jsonPrimitive.content),
        // The DS tree writes a move that never misses as accuracy 0, as the GBA one does.
        accuracy = data.getValue("accuracy").jsonPrimitive.int,
        pp = data.getValue("pp").jsonPrimitive.int,
        secondaryEffectChance = effect.getValue("chance").jsonPrimitive.int,
        target = targetRef(data.getValue("range").jsonPrimitive.content),
        priority = data.getValue("priority").jsonPrimitive.int,
        flags = flagRefs(data.getValue("flags").jsonArray.map { it.jsonPrimitive.content }),
    )
  }

  /** One of the decomp's constant lists, each name against the value it compiles to. */
  private fun constantTable(table: String): Map<String, Int> {
    val file = File(rootDir, "generated/$table")
    require(file.exists()) {
      "Decomp not initialized at $rootDir (missing ${file.path}). " +
          "Run: git submodule update --init --recursive"
    }
    return generatedEnumTable(file)
  }

  private companion object {
    /** The table's own end marker, not a move. */
    val NOT_A_MOVE = setOf("MAX_MOVES")
  }
}
