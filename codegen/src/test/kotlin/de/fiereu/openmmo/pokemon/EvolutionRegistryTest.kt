package de.fiereu.openmmo.pokemon

import de.fiereu.openmmo.common.enums.EvolutionMethod
import de.fiereu.openmmo.common.enums.EvolutionParam
import de.fiereu.openmmo.common.enums.EvolutionTrigger
import de.fiereu.openmmo.common.enums.MonsterGender
import de.fiereu.openmmo.items.ItemRegistry
import de.fiereu.openmmo.moves.MoveRegistry
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldBeEmpty
import io.kotest.matchers.collections.shouldContain
import io.kotest.matchers.ints.shouldBeGreaterThan
import io.kotest.matchers.nulls.shouldBeNull
import io.kotest.matchers.shouldBe

private const val BULBASAUR = 1
private const val IVYSAUR = 2
private const val KADABRA = 64
private const val ALAKAZAM = 65
private const val POLIWHIRL = 61
private const val POLIWRATH = 62
private const val POLITOED = 186
private const val EEVEE = 133
private const val VAPOREON = 134
private const val JOLTEON = 135
private const val FLAREON = 136
private const val ESPEON = 196
private const val UMBREON = 197
private const val LEAFEON = 470
private const val GLACEON = 471
private const val WURMPLE = 265
private const val SILCOON = 266
private const val CASCOON = 268
private const val TYROGUE = 236
private const val HITMONLEE = 106
private const val HITMONCHAN = 107
private const val HITMONTOP = 237
private const val NINCADA = 290
private const val NINJASK = 291

private const val WATER_STONE = 5084
private const val THUNDERSTONE = 5083
private const val FIRE_STONE = 5082
private const val KINGS_ROCK = 5221

/** The last move the GBA decomp our move table comes from knows about. */
private const val LAST_GBA_MOVE = 354

class EvolutionRegistryTest :
    FunSpec({
      val evolutions = EvolutionRegistry()

      test("every species that evolves has its ways in the order the game tests them") {
        evolutions.size() shouldBe 229
        evolutions.get(BULBASAUR) shouldBe listOf(EvolutionDef(EvolutionMethod.LEVEL, 16, IVYSAUR))
        evolutions.get(EEVEE).map { it.targetSpeciesId } shouldBe
            listOf(LEAFEON, GLACEON, JOLTEON, VAPOREON, FLAREON, ESPEON, UMBREON)
      }

      test("a species that does not evolve has no entries") {
        evolutions.get(ESPEON).shouldBeEmpty()
        evolutions.get(9999).shouldBeEmpty()
      }

      test("the pre-evolution direction the game has no table for") {
        evolutions.preEvolutionsOf(ESPEON) shouldBe listOf(EEVEE)
        evolutions.preEvolutionsOf(BULBASAUR).shouldBeEmpty()
      }

      test("a level evolution waits for its level") {
        val below = EvolutionCandidate(level = 15)
        val at = EvolutionCandidate(level = 16)
        evolutions.evolutionTarget(BULBASAUR, EvolutionTrigger.LEVEL_UP, below).shouldBeNull()
        evolutions.evolutionTarget(BULBASAUR, EvolutionTrigger.LEVEL_UP, at) shouldBe IVYSAUR
      }

      test("friendship evolutions read the time of day") {
        val day = EvolutionCandidate(level = 20, friendship = 220, night = false)
        val night = day.copy(night = true)
        evolutions.evolutionTarget(EEVEE, EvolutionTrigger.LEVEL_UP, day) shouldBe ESPEON
        evolutions.evolutionTarget(EEVEE, EvolutionTrigger.LEVEL_UP, night) shouldBe UMBREON
        evolutions
            .evolutionTarget(EEVEE, EvolutionTrigger.LEVEL_UP, day.copy(friendship = 219))
            .shouldBeNull()
      }

      test("a stone only evolves the species it belongs to") {
        val water = EvolutionCandidate(usedItemId = WATER_STONE)
        evolutions.evolutionTarget(EEVEE, EvolutionTrigger.ITEM_USED, water) shouldBe VAPOREON
        evolutions.evolutionTarget(
            EEVEE,
            EvolutionTrigger.ITEM_USED,
            EvolutionCandidate(usedItemId = THUNDERSTONE)) shouldBe JOLTEON
        evolutions.evolutionTarget(BULBASAUR, EvolutionTrigger.ITEM_USED, water).shouldBeNull()
        evolutions.evolutionTarget(POLIWHIRL, EvolutionTrigger.ITEM_USED, water) shouldBe POLIWRATH
      }

      test("a trade evolution can want a held item") {
        val plain = EvolutionCandidate()
        val holding = EvolutionCandidate(heldItemId = KINGS_ROCK)
        evolutions.evolutionTarget(POLIWHIRL, EvolutionTrigger.TRADE, plain).shouldBeNull()
        evolutions.evolutionTarget(POLIWHIRL, EvolutionTrigger.TRADE, holding) shouldBe POLITOED
        evolutions.evolutionTarget(KADABRA, EvolutionTrigger.TRADE, plain) shouldBe ALAKAZAM
        evolutions.evolutionTarget(KADABRA, EvolutionTrigger.LEVEL_UP, plain).shouldBeNull()
      }

      test("an Everstone stops everything but a stone, and Kadabra ignores it") {
        val held = EvolutionCandidate(level = 16, heldItemPreventsEvolution = true)
        evolutions.evolutionTarget(BULBASAUR, EvolutionTrigger.LEVEL_UP, held).shouldBeNull()
        evolutions.evolutionTarget(KADABRA, EvolutionTrigger.TRADE, held) shouldBe ALAKAZAM
        evolutions.evolutionTarget(
            EEVEE,
            EvolutionTrigger.ITEM_USED,
            held.copy(usedItemId = FIRE_STONE),
        ) shouldBe FLAREON
      }

      test("the personality value splits Wurmple in two") {
        val low = EvolutionCandidate(level = 7, personality = 0x0004_0000L)
        val high = EvolutionCandidate(level = 7, personality = 0x0005_0000L)
        evolutions.evolutionTarget(WURMPLE, EvolutionTrigger.LEVEL_UP, low) shouldBe SILCOON
        evolutions.evolutionTarget(WURMPLE, EvolutionTrigger.LEVEL_UP, high) shouldBe CASCOON
      }

      test("Tyrogue reads its own stats") {
        val base = EvolutionCandidate(level = 20)
        evolutions.evolutionTarget(
            TYROGUE, EvolutionTrigger.LEVEL_UP, base.copy(attack = 10, defense = 20)) shouldBe
            HITMONCHAN
        evolutions.evolutionTarget(
            TYROGUE, EvolutionTrigger.LEVEL_UP, base.copy(attack = 20, defense = 10)) shouldBe
            HITMONLEE
        evolutions.evolutionTarget(
            TYROGUE, EvolutionTrigger.LEVEL_UP, base.copy(attack = 15, defense = 15)) shouldBe
            HITMONTOP
      }

      test("Nincada's second entry names Shedinja but does not evolve into it") {
        evolutions.get(NINCADA).map { it.method } shouldContain EvolutionMethod.LEVEL_SHEDINJA
        evolutions.evolutionTarget(
            NINCADA, EvolutionTrigger.LEVEL_UP, EvolutionCandidate(level = 20)) shouldBe NINJASK
      }

      test("a location bound evolution needs to be standing there") {
        val nowhere = EvolutionCandidate(level = 20)
        evolutions.evolutionTarget(EEVEE, EvolutionTrigger.LEVEL_UP, nowhere).shouldBeNull()
        evolutions.evolutionTarget(
            EEVEE,
            EvolutionTrigger.LEVEL_UP,
            nowhere.copy(locationTrigger = EvolutionMethod.LEVEL_MOSS_ROCK),
        ) shouldBe LEAFEON
      }

      test("a gendered stone only works on that gender") {
        val dawnStone = evolutions.get(361).first { it.method == EvolutionMethod.USE_ITEM_FEMALE }
        val female = EvolutionCandidate(gender = MonsterGender.FEMALE, usedItemId = dawnStone.param)
        evolutions.evolutionTarget(361, EvolutionTrigger.ITEM_USED, female) shouldBe
            dawnStone.targetSpeciesId
        evolutions
            .evolutionTarget(
                361, EvolutionTrigger.ITEM_USED, female.copy(gender = MonsterGender.MALE))
            .shouldBeNull()
      }

      test("every evolution names a species the breeding table has") {
        val breeding = BreedingRegistry()
        val targets = (1..MAX_DEX_ID).flatMap { evolutions.get(it) }.map { it.targetSpeciesId }
        targets.size shouldBe 246
        targets.filter { breeding.get(it) == null }.shouldBeEmpty()
      }

      test("every item an evolution needs is in the item table") {
        val items = ItemRegistry()
        val needed = paramsOf(evolutions, EvolutionParam.ITEM)
        needed.size shouldBeGreaterThan 0
        needed.filter { items.get(it) == null }.shouldBeEmpty()
      }

      test("the moves an evolution needs are the GBA table's, bar the gen 4 one") {
        val moves = MoveRegistry()
        val needed = paramsOf(evolutions, EvolutionParam.MOVE)
        needed.size shouldBeGreaterThan 0
        // Ambipom wants Double Hit, which the GBA decomp our move table comes from never had.
        needed.filter { moves.get(it) == null }.forEach { it shouldBeGreaterThan LAST_GBA_MOVE }
      }

      test("every species an evolution needs in the party is a real one") {
        val breeding = BreedingRegistry()
        paramsOf(evolutions, EvolutionParam.SPECIES)
            .filter { breeding.get(it) == null }
            .shouldBeEmpty()
      }
    })

private const val MAX_DEX_ID = 493

private fun paramsOf(evolutions: EvolutionRegistry, param: EvolutionParam): List<Int> =
    (1..MAX_DEX_ID)
        .flatMap { evolutions.get(it) }
        .filter { it.method.param == param }
        .map { it.param }
        .distinct()
