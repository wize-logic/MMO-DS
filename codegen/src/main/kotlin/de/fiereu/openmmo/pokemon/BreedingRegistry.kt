package de.fiereu.openmmo.pokemon

import de.fiereu.openmmo.common.enums.EggGroup
import de.fiereu.openmmo.common.enums.MonsterGender
import de.fiereu.openmmo.pokemon.generated.GeneratedBreeding
import de.fiereu.openmmo.pokemon.generated.NamedIds
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Singleton

/**
 * The breeding half of the species data, egg groups, egg cycles and what a species' egg hatches
 * into, keyed by national dex id, and the day care's own rules over it.
 */
@Singleton
class BreedingRegistry @Inject constructor() {

  private val breeding = ConcurrentHashMap<Int, BreedingDef>()

  init {
    GeneratedBreeding.loadInto(this)
  }

  fun register(dexId: Int, def: BreedingDef) {
    breeding[dexId] = def
  }

  fun get(dexId: Int): BreedingDef? = breeding[dexId]

  fun size(): Int = breeding.size

  /** The incense table, in the order the game walks it. */
  fun incenseBabies(): List<IncenseBaby> = GeneratedBreeding.INCENSE_BABIES

  /**
   * How well the two would breed. Two monsters of the same species and different trainers are
   * the best pairing there is; a Ditto pairs with anything but another Ditto, and never at the
   * top score.
   */
  fun compatibility(first: BreedingParent, second: BreedingParent): BreedingCompatibility {
    val groups =
        listOf(first, second).map {
          get(it.speciesId)?.eggGroups ?: return BreedingCompatibility.INCOMPATIBLE
        }
    val sameTrainer = first.trainerId == second.trainerId

    if (groups[0][0] == EggGroup.NO_EGGS_DISCOVERED ||
        groups[1][0] == EggGroup.NO_EGGS_DISCOVERED) {
      return BreedingCompatibility.INCOMPATIBLE
    }
    if (groups[0][0] == EggGroup.DITTO && groups[1][0] == EggGroup.DITTO)
        return BreedingCompatibility.INCOMPATIBLE
    if (groups[0][0] == EggGroup.DITTO || groups[1][0] == EggGroup.DITTO) {
      return if (sameTrainer) BreedingCompatibility.LOW else BreedingCompatibility.MEDIUM
    }
    if (first.gender == second.gender) return BreedingCompatibility.INCOMPATIBLE
    if (first.gender == MonsterGender.GENDERLESS || second.gender == MonsterGender.GENDERLESS) {
      return BreedingCompatibility.INCOMPATIBLE
    }
    if (groups[0].none { it in groups[1] }) return BreedingCompatibility.INCOMPATIBLE

    return if (first.speciesId == second.speciesId) {
      if (sameTrainer) BreedingCompatibility.MEDIUM else BreedingCompatibility.MAX
    } else {
      if (sameTrainer) BreedingCompatibility.LOW else BreedingCompatibility.MEDIUM
    }
  }

  /**
   * The species a pairing's egg would be, or null when neither monster can be the mother, a
   * Ditto or a female is what makes one, and a pair with neither never gets this far in the
   * game.
   */
  fun eggSpecies(
      first: BreedingParent,
      second: BreedingParent,
      offspringPersonalityMale: Boolean = false,
  ): Int? {
    val mother = motherOf(first, second) ?: return null
    val fromMother = get(mother.speciesId)?.offspringSpeciesId ?: return null

    val species =
        when (fromMother) {
          NamedIds.NIDORAN_F -> if (offspringPersonalityMale) NamedIds.NIDORAN_M else fromMother
          NamedIds.ILLUMISE -> if (offspringPersonalityMale) NamedIds.VOLBEAT else fromMother
          NamedIds.MANAPHY -> NamedIds.PHIONE
          else -> fromMother
        }

    val incense = incenseBabies().firstOrNull { it.babySpeciesId == species } ?: return species
    val held = listOf(first.heldItemId, second.heldItemId)
    return if (incense.incenseItemId in held) species else incense.grownSpeciesId
  }

  /**
   * Ditto stands in for the mother; otherwise the female is the one the egg's species comes from.
   */
  private fun motherOf(first: BreedingParent, second: BreedingParent): BreedingParent? {
    var mother: BreedingParent? = null
    for ((self, other) in listOf(first to second, second to first)) {
      if (self.speciesId == NamedIds.DITTO) {
        mother = other
      } else if (self.gender == MonsterGender.FEMALE) {
        mother = self
      }
    }
    return mother
  }
}
