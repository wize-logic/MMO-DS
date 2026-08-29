package de.fiereu.openmmo.server.game.battle

import de.fiereu.openmmo.common.MAX_MOVE_SLOTS
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.moves.MoveRegistry
import de.fiereu.openmmo.pokemon.LearnsetRegistry
import javax.inject.Inject
import javax.inject.Singleton

/**
 * A move learned on level up, ready to be announced to the player. [slot] is the move slot it went
 * into, or -1 when the moveset was full and the player has to drop one first.
 */
data class LearnedMove(val level: Int, val moveId: Int, val name: String, val slot: Int)

/** [learned] went into a free slot, [offered] needs the player to drop a move first. */
data class MoveLearnOutcome(
    val learned: List<LearnedMove>,
    val offered: List<LearnedMove>,
)

/** Teaches the level up moves a monster gained from a level range. */
@Singleton
class MoveLearner
@Inject
constructor(
    private val learnsets: LearnsetRegistry,
    private val moves: MoveRegistry,
) {

  /** Adds every move learned above [fromLevel] up to [toLevel] that fits a free slot. */
  fun learn(
      known: MutableList<PokemonMove>,
      dexId: Int,
      fromLevel: Int,
      toLevel: Int,
  ): MoveLearnOutcome {
    val learned = mutableListOf<LearnedMove>()
    val offered = mutableListOf<LearnedMove>()
    for (level in fromLevel + 1..toLevel) {
      for (moveId in learnsets.movesAt(dexId, level)) {
        if (known.any { it.id.toInt() == moveId }) continue
        if (offered.any { it.moveId == moveId }) continue
        val def = moves.get(moveId) ?: continue
        val free = known.indexOfFirst { it.id.toInt() == 0 }
        if (free < 0 && known.size >= MAX_MOVE_SLOTS) {
          offered += LearnedMove(level, moveId, def.name, -1)
          continue
        }
        val move = PokemonMove(moveId.toShort(), def.pp.toByte())
        val slot = if (free < 0) known.size else free
        if (free < 0) known += move else known[free] = move
        learned += LearnedMove(level, moveId, def.name, slot)
      }
    }
    return MoveLearnOutcome(learned, offered)
  }

  /**
   * Puts [moveId] into move [slot], dropping whatever was there. False, and nothing written, for a
   * slot outside the moveset, a move the monster already knows, or a move that was not the one
   * offered.
   */
  fun apply(
      known: MutableList<PokemonMove>,
      slot: Int,
      moveId: Short,
      offered: Short,
  ): Boolean {
    if (slot < 0 || slot >= known.size || slot >= MAX_MOVE_SLOTS) return false
    if (moveId != offered) return false
    if (known.any { it.id == moveId }) return false
    known[slot] = PokemonMove(moveId, moves.get(moveId.toInt())?.pp?.toByte() ?: 0)
    return true
  }
}
