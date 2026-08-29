package de.fiereu.openmmo.pokemon

import de.fiereu.openmmo.common.enums.EvolutionMethod
import de.fiereu.openmmo.common.enums.MonsterGender

/**
 * One way a species can evolve. [param] is read according to [EvolutionMethod.param] and is 0
 * for the methods that take none.
 */
data class EvolutionDef(
    val method: EvolutionMethod,
    val param: Int,
    val targetSpeciesId: Int,
)

/**
 * Everything the game reads off a monster when it decides whether it evolves. Defaults are the
 * "nothing is true" case so a caller only fills in what its trigger needs.
 */
data class EvolutionCandidate(
    val level: Int = 0,
    val friendship: Int = 0,
    val beauty: Int = 0,
    val attack: Int = 0,
    val defense: Int = 0,
    val personality: Long = 0,
    val gender: MonsterGender = MonsterGender.GENDERLESS,
    val heldItemId: Int = 0,
    val heldItemPreventsEvolution: Boolean = false,
    val knownMoveIds: List<Int> = emptyList(),
    val partySpeciesIds: List<Int> = emptyList(),
    val night: Boolean = false,
    val locationTrigger: EvolutionMethod? = null,
    val usedItemId: Int = 0,
) {
  init {
    require(locationTrigger == null || locationTrigger.isLocationBound) {
      "$locationTrigger is not one of the location bound evolution methods"
    }
  }

  /** The game tests the upper half of the personality value, not the whole of it. */
  val personalityUpper: Int
    get() = ((personality ushr 16) and 0xFFFF).toInt()

  internal fun satisfies(evolution: EvolutionDef): Boolean =
      when (evolution.method) {
        EvolutionMethod.NONE -> false
        EvolutionMethod.LEVEL_HAPPINESS -> friendship >= FRIENDSHIP_TO_EVOLVE
        EvolutionMethod.LEVEL_HAPPINESS_DAY -> !night && friendship >= FRIENDSHIP_TO_EVOLVE
        EvolutionMethod.LEVEL_HAPPINESS_NIGHT -> night && friendship >= FRIENDSHIP_TO_EVOLVE
        EvolutionMethod.LEVEL -> evolution.param <= level
        EvolutionMethod.LEVEL_ATK_GT_DEF -> evolution.param <= level && attack > defense
        EvolutionMethod.LEVEL_ATK_EQ_DEF -> evolution.param <= level && attack == defense
        EvolutionMethod.LEVEL_ATK_LT_DEF -> evolution.param <= level && attack < defense
        EvolutionMethod.LEVEL_PID_LOW -> evolution.param <= level && personalityUpper % 10 < 5
        EvolutionMethod.LEVEL_PID_HIGH -> evolution.param <= level && personalityUpper % 10 >= 5
        EvolutionMethod.LEVEL_NINJASK -> evolution.param <= level
        // Nincada's second entry names Shedinja but the game leaves the target unset here: the
        // spare monster is made by the level up path itself, not by evolving into it.
        EvolutionMethod.LEVEL_SHEDINJA -> false
        EvolutionMethod.LEVEL_BEAUTY -> evolution.param <= beauty
        EvolutionMethod.LEVEL_WITH_HELD_ITEM_DAY -> !night && evolution.param == heldItemId
        EvolutionMethod.LEVEL_WITH_HELD_ITEM_NIGHT -> night && evolution.param == heldItemId
        EvolutionMethod.LEVEL_KNOW_MOVE -> evolution.param in knownMoveIds
        EvolutionMethod.LEVEL_SPECIES_IN_PARTY -> evolution.param in partySpeciesIds
        EvolutionMethod.LEVEL_MALE -> gender == MonsterGender.MALE && evolution.param <= level
        EvolutionMethod.LEVEL_FEMALE -> gender == MonsterGender.FEMALE && evolution.param <= level
        EvolutionMethod.LEVEL_MAGNETIC_FIELD,
        EvolutionMethod.LEVEL_MOSS_ROCK,
        EvolutionMethod.LEVEL_ICE_ROCK -> locationTrigger == evolution.method
        EvolutionMethod.TRADE -> true
        EvolutionMethod.TRADE_WITH_HELD_ITEM -> evolution.param == heldItemId
        EvolutionMethod.USE_ITEM -> evolution.param == usedItemId
        EvolutionMethod.USE_ITEM_MALE ->
            gender == MonsterGender.MALE && evolution.param == usedItemId
        EvolutionMethod.USE_ITEM_FEMALE ->
            gender == MonsterGender.FEMALE && evolution.param == usedItemId
      }

  companion object {
    /** `EVOLVE_FRIENDSHIP_THRESHOLD`. */
    const val FRIENDSHIP_TO_EVOLVE = 220
  }
}
