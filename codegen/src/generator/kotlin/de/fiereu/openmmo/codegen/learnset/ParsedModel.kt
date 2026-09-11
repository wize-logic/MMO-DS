package de.fiereu.openmmo.codegen.learnset

data class ParsedLearnset(val dexId: Int, val moves: List<ParsedLevelUpMove>)

data class ParsedLevelUpMove(val level: Int, val moveId: Int)

/**
 * Every move a species can know that its level up list does not carry: the machines it is
 * compatible with, the moves it hatches knowing, and what the tutors will teach it.
 */
data class ParsedMoveSources(
    val dexId: Int,
    val machineMoveIds: List<Int>,
    val eggMoveIds: List<Int>,
    val tutorMoveIds: List<Int>,
)
