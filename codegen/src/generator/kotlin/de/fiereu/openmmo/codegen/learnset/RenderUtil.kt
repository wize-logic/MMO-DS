package de.fiereu.openmmo.codegen.learnset

object RenderUtil {

  fun learnset(l: ParsedLearnset): String {
    val moves = l.moves.joinToString(", ") { "LevelUpMove(${it.level}, ${it.moveId})" }
    return "reg.register(${l.dexId}, listOf($moves))"
  }

  fun moveSources(s: ParsedMoveSources): String {
    val machines = s.machineMoveIds.joinToString(", ")
    val egg = s.eggMoveIds.joinToString(", ")
    val tutor = s.tutorMoveIds.joinToString(", ")
    return "reg.register(${s.dexId}, MoveSources(" +
        "setOf($machines), setOf($egg), setOf($tutor)))"
  }
}
