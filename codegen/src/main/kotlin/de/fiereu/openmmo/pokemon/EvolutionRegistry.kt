package de.fiereu.openmmo.pokemon

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

  companion object {
    private const val KADABRA = 64
  }
}
