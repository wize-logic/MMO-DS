package de.fiereu.openmmo.codegen.move

import de.fiereu.openmmo.common.enums.MoveEffect

fun typeRef(token: String): String {
  val name = token.removePrefix("TYPE_")
  val mapped = if (name == "MYSTERY") "QUESTIONQUESTIONQUESTION" else name
  return "PokemonType.$mapped"
}

/**
 * The DS tree stores a move's effect as a name; its position in `move_battle_effects.txt` is the
 * number, and that number is [MoveEffect]'s own ordinal. See the enum for what pins the two lists
 * together.
 */
fun effectRef(effects: Map<String, Int>, token: String): String {
  val ordinal = effects[token]
  requireNotNull(ordinal) { "$token is not in the decomp's move_battle_effects.txt" }
  require(ordinal < MoveEffect.entries.size) {
    "$token is effect $ordinal and MoveEffect stops at ${MoveEffect.entries.size - 1}"
  }
  return "MoveEffect.${MoveEffect.entries[ordinal].name}"
}

/**
 * The DS range a move is aimed at, as the target the GBA tree gives the same move. Every pair here
 * but the last two is read off the 351 moves both trees carry, with how many witness it; the last
 * two are ranges Gen 4 added, which no shared move can witness.
 */
private val TARGET_BY_RANGE =
    mapOf(
        "RANGE_SINGLE_TARGET" to "SELECTED", // 245 shared moves
        "RANGE_SINGLE_TARGET_SPECIAL" to "DEPENDS", // 9
        "RANGE_RANDOM_OPPONENT" to "RANDOM", // 4
        "RANGE_ADJACENT_OPPONENTS" to "BOTH", // 21
        "RANGE_ALL_ADJACENT" to "FOES_AND_ALLY", // 4, against 1 the GBA tree aims at both
        "RANGE_USER" to "USER", // 51
        "RANGE_USER_SIDE" to "USER", // 6
        "RANGE_FIELD" to "USER", // 8
        "RANGE_ALLY" to "USER", // 1
        "RANGE_OPPONENT_SIDE" to "OPPONENTS_FIELD", // 1
        "RANGE_USER_OR_ALLY" to "USER_OR_ALLY", // Gen 4 only
        "RANGE_SINGLE_TARGET_ME_FIRST" to "SELECTED_ME_FIRST", // Gen 4 only
    )

fun targetRef(token: String): String {
  val target = TARGET_BY_RANGE[token]
  requireNotNull(target) { "$token has no target this generator knows" }
  return "MoveTarget.$target"
}

/**
 * The DS flag names against the GBA ones. The first six hold for every move the two trees share;
 * the last two are the DS tree's presentation flags, which the GBA tree does not have.
 */
private val FLAG_NAMES =
    mapOf(
        "MOVE_FLAG_MAKES_CONTACT" to "MAKES_CONTACT",
        "MOVE_FLAG_CAN_PROTECT" to "PROTECT_AFFECTED",
        "MOVE_FLAG_CAN_MAGIC_COAT" to "MAGIC_COAT_AFFECTED",
        "MOVE_FLAG_CAN_SNATCH" to "SNATCH_AFFECTED",
        "MOVE_FLAG_CAN_MIRROR_MOVE" to "MIRROR_MOVE_AFFECTED",
        "MOVE_FLAG_TRIGGERS_KINGS_ROCK" to "KINGS_ROCK_AFFECTED",
        "MOVE_FLAG_HIDES_HP_GAUGES" to "HIDES_HP_GAUGES",
        "MOVE_FLAG_HIDES_SHADOWS" to "HIDES_SHADOWS",
    )

fun flagRefs(tokens: List<String>): List<String> =
    tokens.map { token ->
      val flag = FLAG_NAMES[token]
      requireNotNull(flag) { "$token has no flag this generator knows" }
      "MoveFlag.$flag"
    }
