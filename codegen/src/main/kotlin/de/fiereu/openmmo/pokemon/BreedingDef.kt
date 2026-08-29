package de.fiereu.openmmo.pokemon

import de.fiereu.openmmo.common.enums.EggGroup
import de.fiereu.openmmo.common.enums.MonsterGender

/**
 * What a species contributes to breeding. [offspringSpeciesId] is the species its egg hatches
 * into before any of the special cases, which for most of them is itself; [hatchCycles] is the
 * number of egg cycles, not steps.
 */
data class BreedingDef(
    val offspringSpeciesId: Int,
    val eggGroup1: EggGroup,
    val eggGroup2: EggGroup,
    val hatchCycles: Int,
) {
  val eggGroups: List<EggGroup> = listOf(eggGroup1, eggGroup2)
}

/**
 * A baby species that only hatches while a parent holds the matching incense. [incenseItemId] is
 * the wire id, so it compares directly against a held item.
 */
data class IncenseBaby(val babySpeciesId: Int, val incenseItemId: Int, val grownSpeciesId: Int)

/** One side of a pairing, as much of it as the breeding rules read. */
data class BreedingParent(
    val speciesId: Int,
    val gender: MonsterGender,
    val trainerId: Long,
    val heldItemId: Int = 0,
)

/**
 * How well a pair breeds, which the game shows as the day care man's line about them. The numbers
 * are the game's own, not an ordering of our own making.
 */
enum class BreedingCompatibility(val score: Int) {
  INCOMPATIBLE(0),
  LOW(20),
  MEDIUM(50),
  MAX(70),
}
