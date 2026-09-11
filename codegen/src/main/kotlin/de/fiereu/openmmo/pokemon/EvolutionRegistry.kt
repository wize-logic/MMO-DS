package de.fiereu.openmmo.pokemon

import de.fiereu.openmmo.common.enums.EvolutionMethod
import de.fiereu.openmmo.common.enums.EvolutionParam
import de.fiereu.openmmo.common.enums.EvolutionTrigger
import de.fiereu.openmmo.pokemon.generated.GeneratedEvolutions
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Singleton

/**
 * Every way each species can evolve, keyed by national dex id and in the order the game stores
 * them, which is the order it tests them in.
 */
@Singleton
class EvolutionRegistry @Inject constructor() {

  private val evolutions = ConcurrentHashMap<Int, List<EvolutionDef>>()

  init {
    GeneratedEvolutions.loadInto(this)
  }

  fun register(dexId: Int, defs: List<EvolutionDef>) {
    evolutions[dexId] = defs
  }

  fun get(dexId: Int): List<EvolutionDef> = evolutions[dexId] ?: emptyList()

  fun size(): Int = evolutions.size

  /** Every species that evolves into [dexId], the direction the game has no table for. */
  fun preEvolutionsOf(dexId: Int): List<Int> =
      evolutions
          .filterValues { defs -> defs.any { it.targetSpeciesId == dexId } }
          .keys
          .sorted()
          .toList()

  /**
   * What [dexId] becomes, or null for nothing. The first entry whose condition holds wins, an
   * Everstone stops everything but an item being used on the monster, and Kadabra ignores the
   * Everstone.
   */
  fun evolutionTarget(dexId: Int, trigger: EvolutionTrigger, mon: EvolutionCandidate): Int? {
    if (dexId != KADABRA &&
        mon.heldItemPreventsEvolution &&
        trigger != EvolutionTrigger.ITEM_USED) {
      return null
    }
    return get(dexId)
        .firstOrNull { it.method.trigger == trigger && mon.satisfies(it) }
        ?.targetSpeciesId
  }

  /**
   * Whether a monster stored as [from] could have become [to], at [level] and carrying [beauty].
   */
  fun isReachable(
      from: Int,
      to: Int,
      level: Int,
      beauty: Int = 0,
      friendship: Int = 0,
      heldItemId: Int = 0,
      heldItemPreventsEvolution: Boolean = false,
  ): Boolean {
    if (from == to || to <= 0) return false
    // The Everstone gate, in the shape the game applies it: the species is checked first, the hold
    // effect second, and a stone used on the monster is exempt. So a monster reported as evolved
    // while the record still has an Everstone on it can only have got there by a stone.
    val everstone = heldItemPreventsEvolution && from != KADABRA
    val seen = mutableSetOf(from)
    var frontier = setOf(from)
    repeat(MAX_EVOLUTION_STEPS) {
      val next = mutableSetOf<Int>()
      for (dexId in frontier) {
        for (evolution in get(dexId)) {
          val target = evolution.targetSpeciesId
          if (target <= 0 || target in seen) continue
          if (everstone && evolution.method.trigger != EvolutionTrigger.ITEM_USED) continue
          if (!permits(evolution, level, beauty, friendship, heldItemId)) continue
          if (target == to) return true
          next += target
        }
      }
      if (next.isEmpty()) return false
      seen += next
      frontier = next
    }
    return false
  }

  /** What the record alone can say about one entry. Anything it cannot speak to is allowed. */
  private fun permits(
      evolution: EvolutionDef,
      level: Int,
      beauty: Int,
      friendship: Int,
      heldItemId: Int,
  ): Boolean =
      when (evolution.method) {
        EvolutionMethod.NONE,
        EvolutionMethod.LEVEL_SHEDINJA -> false
        EvolutionMethod.LEVEL_HAPPINESS,
        EvolutionMethod.LEVEL_HAPPINESS_DAY,
        EvolutionMethod.LEVEL_HAPPINESS_NIGHT ->
            friendship >= EvolutionCandidate.FRIENDSHIP_TO_EVOLVE
        // The day and night halves are one entry each and the report carries no clock, so a Gliscor
        // and a Weavile are both held to their own item and not to the hour.
        EvolutionMethod.TRADE_WITH_HELD_ITEM,
        EvolutionMethod.LEVEL_WITH_HELD_ITEM_DAY,
        EvolutionMethod.LEVEL_WITH_HELD_ITEM_NIGHT -> evolution.param == heldItemId
        else ->
            when (evolution.method.param) {
              EvolutionParam.LEVEL -> evolution.param <= level
              EvolutionParam.BEAUTY -> evolution.param <= beauty
              else -> true
            }
      }

  companion object {
    private const val KADABRA = 64

    /**
     * How many evolutions deep one report may reach. No species is more than two evolutions from
     * its first stage, so this is that plus room, and it is what stops the walk above rather than
     * the data: a table with a cycle in it would otherwise never finish.
     */
    const val MAX_EVOLUTION_STEPS = 3
  }
}
