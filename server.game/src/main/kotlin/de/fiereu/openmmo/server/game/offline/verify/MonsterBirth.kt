package de.fiereu.openmmo.server.game.offline.verify

import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.enums.PokemonStat
import de.fiereu.openmmo.pokemon.EvolutionRegistry
import de.fiereu.openmmo.server.game.offline.OfflineMonster
import de.fiereu.openmmo.server.game.offline.OfflineSaveWire

/** A monster at its birth, against the monster the character holds now. */
object MonsterBirth {

  /** Whether [report], a replay's quit, as the game writes it down, holds a monster of [pid]. */
  fun holds(report: ByteArray, pid: Int): Boolean = find(report, pid) != null

  /** The monster of [pid] in [report], or null when it is not there or the report will not read. */
  fun find(report: ByteArray, pid: Int): OfflineMonster? =
      monsters(report)?.firstOrNull { it.pid == pid }

  /** Every personality [report] holds, or null when the report will not read. */
  fun pids(report: ByteArray): Set<Int>? = monsters(report)?.mapTo(mutableSetOf()) { it.pid }

  private fun monsters(report: ByteArray): List<OfflineMonster>? =
      try {
        OfflineSaveWire.decode(report).monsters
      } catch (_: RuntimeException) {
        null
      }

  /**
   * What the monster held now has that the one born in the replay did not, in a sentence, or null
   * for nothing that honest play could not account for.
   */
  fun differences(now: Pokemon, born: OfflineMonster, evolutions: EvolutionRegistry): String? {
    val problems = mutableListOf<String>()
    val ivsNow = PokemonStat.entries.map { (now.iVs[it] ?: 0).toInt() and 0xFF }
    val ivsBorn = PokemonStat.entries.map { born.ivs[it] ?: 0 }
    if (ivsNow != ivsBorn) problems += "its hidden stats are $ivsNow, and it was born with $ivsBorn"
    if (now.ot != born.otName) {
      problems += "its trainer is named ${now.ot}, and it was born to ${born.otName}"
    }
    if (now.dexId != born.dexId && !grows(born.dexId, now.dexId, evolutions)) {
      problems += "it is species ${now.dexId}, which species ${born.dexId} does not grow into"
    }
    return problems.takeIf { it.isNotEmpty() }?.joinToString("; ")
  }

  /** Whether [from] evolves into [into], in as many steps as any species takes. */
  fun grows(from: Int, into: Int, evolutions: EvolutionRegistry): Boolean {
    var frontier = listOf(from)
    val seen = mutableSetOf(from)
    repeat(EvolutionRegistry.MAX_EVOLUTION_STEPS) {
      frontier =
          frontier
              .flatMap { evolutions.get(it).map { def -> def.targetSpeciesId } }
              .filter { seen.add(it) }
      if (into in frontier) return true
    }
    return false
  }
}
