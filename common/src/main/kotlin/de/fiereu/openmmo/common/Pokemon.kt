package de.fiereu.openmmo.common

import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.PokemonNature
import java.time.LocalDateTime

const val MAX_MOVE_SLOTS = 4

const val MAX_PARTY_SIZE = 6

/** What a monster starts liking its trainer at when nothing better is known. */
const val DEFAULT_FRIENDSHIP = 70

/** The ceiling the game clamps friendship to, and what Return reads as full power. */
const val MAX_FRIENDSHIP = 255

/**
 * The bits the client engine's condition word defines: the sleep counter, the five conditions, and
 * the counter the bad poison keeps above them.
 */
const val MON_STATUS_MASK = 0x0FFF

data class Pokemon(
    val id: Long,
    val ownerId: Long,
    val container: PokemonContainer,
    val containerSlot: Short,
    val dexId: Int,
    val seed: Int,
    val ot: String,
    val nickname: String,
    val level: Byte,
    val hp: Short,
    val xp: Int,
    val eVs: EVs,
    val iVs: IVs,
    val moves: List<PokemonMove>,
    val isShiny: Boolean,
    val hasHiddenAbility: Boolean,
    val isAlpha: Boolean,
    val isSecret: Boolean,
    val isFatefulEncounter: Boolean,
    val isRaidEncounter: Boolean,
    val caughtAt: LocalDateTime,
    val isEgg: Boolean = false,
    /** Alternate forme. 0 is the ordinary one; Giratina Origin is 1. */
    val form: Int = 0,
    /** What a Super Contest scores this monster on. Raised by Poffins and nothing else. */
    val conditions: ContestConditions = ContestConditions.NONE,
    /** How saturated with Poffins the monster is; it caps how much more they can raise. */
    val sheen: Int = 0,
    /**
     * Super Contest ribbons, a bit per (type, rank) pair, see [superContestRibbonBit]. Not a
     * trophy: the contest hall refuses a rank above the number of that type's ribbons the monster
     * already holds, so this is the whole of a player's contest progression.
     */
    val superContestRibbons: Long = 0L,
    /** Where this monster was caught: the region, bank and map the trainer was standing on. */
    val caughtRegionId: Int = -1,
    val caughtBankId: Int = -1,
    val caughtMapId: Int = -1,
    /** The engine's own location label for where this monster was caught, or 0 for none. */
    val caughtLocationLabel: Int = 0,
    /** How much this monster likes its trainer, 0..[MAX_FRIENDSHIP]. */
    val friendship: Int = DEFAULT_FRIENDSHIP,
    /** The item this monster is carrying, as a wire item id, or 0 for nothing. */
    val heldItemId: Int = 0,
    /**
     * What this monster is suffering from: the client engine's own condition word, 0 for a healthy
     * one.
     */
    val status: Int = 0,
    /** True while this monster arrived from a save file rather than from this server's own dice. */
    val offlineOrigin: Boolean = false,
) {
  // seed is an unsigned 32-bit value on the wire, so mask before the modulo to avoid a negative
  // index when the high bit is set.
  val nature: PokemonNature =
      PokemonNature.entries[((seed.toLong() and 0xFFFFFFFFL) % PokemonNature.entries.size).toInt()]

  init {
    require(moves.size <= MAX_MOVE_SLOTS) { "A Pokemon can't have more than $MAX_MOVE_SLOTS moves" }
  }
}

data class PokemonMove(val id: Short, var pp: Byte)
