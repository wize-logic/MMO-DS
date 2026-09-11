package de.fiereu.openmmo.pokemon

import de.fiereu.openmmo.pokemon.generated.GeneratedMoveSources
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Singleton

/**
 * Every move a species can know that is not on its level up list: the machines it is compatible
 * with, what it can hatch knowing, and what a tutor will teach it.
 */
data class MoveSources(
    val machineMoves: Set<Int>,
    val eggMoves: Set<Int>,
    val tutorMoves: Set<Int>,
) {
  fun teaches(moveId: Int): Boolean =
      moveId in machineMoves || moveId in eggMoves || moveId in tutorMoves
}

/** The three non level up move lists from the decomp, keyed by national dex id. */
@Singleton
class MoveSourceRegistry @Inject constructor() {

  private val sources = ConcurrentHashMap<Int, MoveSources>()

  init {
    GeneratedMoveSources.loadInto(this)
  }

  fun register(dexId: Int, moves: MoveSources) {
    sources[dexId] = moves
  }

  fun get(dexId: Int): MoveSources? = sources[dexId]

  fun size(): Int = sources.size
}
