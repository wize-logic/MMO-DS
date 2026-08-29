package de.fiereu.openmmo.pokemon

import de.fiereu.openmmo.common.enums.EggGroup
import de.fiereu.openmmo.common.enums.MonsterGender
import de.fiereu.openmmo.items.ItemRegistry
import io.kotest.assertions.withClue
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldBeEmpty
import io.kotest.matchers.nulls.shouldBeNull
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe

private const val PIKACHU = 25
private const val PICHU = 172
private const val MARILL = 183
private const val AZURILL = 298
private const val DITTO = 132
private const val MAGNEMITE = 81
private const val MANAPHY = 490
private const val PHIONE = 489
private const val NIDORAN_F = 29
private const val NIDORAN_M = 32
private const val NIDORINO = 33
private const val HAPPINY = 440
private const val SEA_INCENSE = 5254

private const val ALICE = 1L
private const val BOB = 2L

private fun parent(
    speciesId: Int,
    gender: MonsterGender = MonsterGender.FEMALE,
    trainerId: Long = ALICE,
    heldItemId: Int = 0,
) = BreedingParent(speciesId, gender, trainerId, heldItemId)

class BreedingRegistryTest :
    FunSpec({
      val breeding = BreedingRegistry()

      test("every species in the national dex has a breeding row") { breeding.size() shouldBe 493 }

      // Both tables are read out of the same decomp now, so this is no longer two sources
      // checking each other, it is the two readers of one source, which still catches a
      // parser that takes the wrong field.
      test("the breeding table agrees with the species table on every species") {
        val species = SpeciesRegistry()
        species.size() shouldBe 493
        for (def in species.all()) {
          val row = breeding.get(def.id).shouldNotBeNull()
          withClue(def.name) {
            row.hatchCycles shouldBe def.eggCycles
            row.eggGroups shouldBe listOf(def.eggGroup1, def.eggGroup2)
          }
        }
      }

      test("a species that hatches into something else says so") {
        breeding.get(PIKACHU)!!.offspringSpeciesId shouldBe PICHU
        breeding.get(MARILL)!!.offspringSpeciesId shouldBe AZURILL
        breeding.get(MAGNEMITE)!!.offspringSpeciesId shouldBe MAGNEMITE
      }

      test("every incense in the table is an item we have") {
        val items = ItemRegistry()
        breeding.incenseBabies().filter { items.get(it.incenseItemId) == null }.shouldBeEmpty()
      }

      test("a pair of the same species from two trainers is the best there is") {
        breeding.compatibility(
            parent(PIKACHU, MonsterGender.FEMALE, ALICE),
            parent(PIKACHU, MonsterGender.MALE, BOB),
        ) shouldBe BreedingCompatibility.MAX
        breeding.compatibility(
            parent(PIKACHU, MonsterGender.FEMALE, ALICE),
            parent(PIKACHU, MonsterGender.MALE, ALICE),
        ) shouldBe BreedingCompatibility.MEDIUM
      }

      test("Ditto pairs with anything but another Ditto, and never at the top") {
        breeding.compatibility(
            parent(DITTO, MonsterGender.GENDERLESS, ALICE),
            parent(PIKACHU, MonsterGender.MALE, BOB),
        ) shouldBe BreedingCompatibility.MEDIUM
        breeding.compatibility(
            parent(DITTO, MonsterGender.GENDERLESS, ALICE),
            parent(DITTO, MonsterGender.GENDERLESS, BOB),
        ) shouldBe BreedingCompatibility.INCOMPATIBLE
      }

      test("two of a gender, a genderless pair or an undiscovered group breed nothing") {
        breeding.compatibility(
            parent(PIKACHU, MonsterGender.FEMALE, ALICE),
            parent(PIKACHU, MonsterGender.FEMALE, BOB),
        ) shouldBe BreedingCompatibility.INCOMPATIBLE
        breeding.compatibility(
            parent(MAGNEMITE, MonsterGender.GENDERLESS, ALICE),
            parent(MAGNEMITE, MonsterGender.GENDERLESS, BOB),
        ) shouldBe BreedingCompatibility.INCOMPATIBLE
        breeding.get(HAPPINY)!!.eggGroup1 shouldBe EggGroup.NO_EGGS_DISCOVERED
        breeding.compatibility(
            parent(HAPPINY, MonsterGender.FEMALE, ALICE),
            parent(PIKACHU, MonsterGender.MALE, BOB),
        ) shouldBe BreedingCompatibility.INCOMPATIBLE
      }

      test("species with no egg group in common breed nothing") {
        breeding.compatibility(
            parent(PIKACHU, MonsterGender.FEMALE, ALICE),
            parent(MAGNEMITE, MonsterGender.MALE, BOB),
        ) shouldBe BreedingCompatibility.INCOMPATIBLE
      }

      test("the egg is the mother's, whichever side she is on") {
        val mother = parent(PIKACHU, MonsterGender.FEMALE, ALICE)
        val father = parent(PIKACHU, MonsterGender.MALE, BOB)
        breeding.eggSpecies(mother, father) shouldBe PICHU
        breeding.eggSpecies(father, mother) shouldBe PICHU
      }

      test("Ditto stands in for the mother") {
        breeding.eggSpecies(
            parent(DITTO, MonsterGender.GENDERLESS, ALICE),
            parent(PIKACHU, MonsterGender.MALE, BOB),
        ) shouldBe PICHU
      }

      test("a pair with no mother makes no egg") {
        breeding
            .eggSpecies(
                parent(PIKACHU, MonsterGender.MALE, ALICE),
                parent(PIKACHU, MonsterGender.MALE, BOB),
            )
            .shouldBeNull()
      }

      test("the baby only hatches while a parent holds its incense") {
        val mother = parent(MARILL, MonsterGender.FEMALE, ALICE)
        val father = parent(MARILL, MonsterGender.MALE, BOB)
        breeding.eggSpecies(mother, father) shouldBe MARILL
        breeding.eggSpecies(mother.copy(heldItemId = SEA_INCENSE), father) shouldBe AZURILL
        breeding.eggSpecies(mother, father.copy(heldItemId = SEA_INCENSE)) shouldBe AZURILL
      }

      test("the two species that breed either way follow the egg's own gender bit") {
        val mother = parent(NIDORAN_F, MonsterGender.FEMALE, ALICE)
        val father = parent(NIDORINO, MonsterGender.MALE, BOB)
        breeding.eggSpecies(mother, father, offspringPersonalityMale = false) shouldBe NIDORAN_F
        breeding.eggSpecies(mother, father, offspringPersonalityMale = true) shouldBe NIDORAN_M
      }

      test("Manaphy lays a Phione") {
        breeding.eggSpecies(
            parent(MANAPHY, MonsterGender.GENDERLESS, ALICE),
            parent(DITTO, MonsterGender.GENDERLESS, BOB),
        ) shouldBe PHIONE
      }
    })
