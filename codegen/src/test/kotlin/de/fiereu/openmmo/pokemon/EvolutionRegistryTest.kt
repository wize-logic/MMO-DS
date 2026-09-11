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
private const val SHEDINJA = 292
private const val CHARMANDER = 4
private const val CHARMELEON = 5
private const val CHARIZARD = 6
private const val CATERPIE = 10
private const val BUTTERFREE = 12
private const val PIKACHU = 25
private const val RAICHU = 26
private const val MAGIKARP = 129
private const val GYARADOS = 130
private const val DRAGONITE = 149
private const val FEEBAS = 349
private const val MILOTIC = 350
private const val GOLBAT = 42
private const val CROBAT = 169
private const val CHANSEY = 113
private const val BLISSEY = 242
private const val BUDEW = 406
private const val ROSELIA = 315
private const val RIOLU = 447
private const val LUCARIO = 448
private const val MUNCHLAX = 446
private const val SNORLAX = 143
private const val SNEASEL = 215
private const val WEAVILE = 461
private const val ONIX = 95
private const val STEELIX = 208

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
        evolutions.size() shouldBe 301
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

      test("a claimed evolution has to be one the species really makes") {
        evolutions.isReachable(CHARMANDER, CHARMELEON, level = 16) shouldBe true
        evolutions.isReachable(CHARMANDER, CHARMELEON, level = 15) shouldBe false
        evolutions.isReachable(MAGIKARP, GYARADOS, level = 20) shouldBe true
        evolutions.isReachable(MAGIKARP, GYARADOS, level = 19) shouldBe false
        // Not a species it becomes at any level, and not one it becomes at all.
        evolutions.isReachable(MAGIKARP, DRAGONITE, level = 100) shouldBe false
        evolutions.isReachable(CHARMANDER, CHARMANDER, level = 100) shouldBe false
      }

      test("one fight can carry a monster through two evolutions") {
        // Caterpie is a Metapod at 7 and a Butterfree at 10, and a single battle can cross both.
        evolutions.isReachable(CATERPIE, BUTTERFREE, level = 12) shouldBe true
        evolutions.isReachable(CATERPIE, BUTTERFREE, level = 9) shouldBe false
        // The second step still has to be reached, so the level rule survives the chain.
        evolutions.isReachable(CHARMANDER, CHARIZARD, level = 36) shouldBe true
        evolutions.isReachable(CHARMANDER, CHARIZARD, level = 35) shouldBe false
      }

      test("a condition the record cannot answer is allowed, one it can is checked") {
        // A stone names no level and the record does not hold which one was used, so the claim
        // stands on the shape of the evolution alone.
        evolutions.isReachable(PIKACHU, RAICHU, level = 5) shouldBe true
        // Beauty is on the record, so Milotic is held to it.
        val needed =
            evolutions.get(FEEBAS).first { it.method == EvolutionMethod.LEVEL_BEAUTY }.param
        evolutions.isReachable(FEEBAS, MILOTIC, level = 30, beauty = needed) shouldBe true
        evolutions.isReachable(FEEBAS, MILOTIC, level = 30, beauty = needed - 1) shouldBe false
      }

      test("friendship is on the record, so the ones that turn on it are held to it") {
        val enough = EvolutionCandidate.FRIENDSHIP_TO_EVOLVE
        // None of these has any other trigger, so before the record held friendship each of them
        // was either refused outright or waved through on the shape of the entry alone.
        val pairs =
            listOf(
                GOLBAT to CROBAT,
                CHANSEY to BLISSEY,
                BUDEW to ROSELIA,
                RIOLU to LUCARIO,
                MUNCHLAX to SNORLAX,
            )
        for ((from, to) in pairs) {
          evolutions.isReachable(from, to, level = 30, friendship = enough) shouldBe true
          evolutions.isReachable(from, to, level = 30, friendship = enough - 1) shouldBe false
        }
        // Both of Eevee's are the same threshold and differ only by the hour, which the report does
        // not carry, so both stand or neither does.
        evolutions.isReachable(EEVEE, ESPEON, level = 5, friendship = enough) shouldBe true
        evolutions.isReachable(EEVEE, UMBREON, level = 5, friendship = enough) shouldBe true
        evolutions.isReachable(EEVEE, UMBREON, level = 5, friendship = enough - 1) shouldBe false
        // A stone form of the same monster is not friendship's to refuse.
        evolutions.isReachable(EEVEE, VAPOREON, level = 5, friendship = 0) shouldBe true
      }

      test("the item the record was holding is what a held-item evolution is held to") {
        val razorClaw =
            evolutions
                .get(SNEASEL)
                .first { it.method == EvolutionMethod.LEVEL_WITH_HELD_ITEM_NIGHT }
                .param
        // The item is the one the record held before the report, because the game clears it as the
        // monster evolves: reading the reported item would refuse every one of these.
        evolutions.isReachable(SNEASEL, WEAVILE, level = 30, heldItemId = razorClaw) shouldBe true
        evolutions.isReachable(SNEASEL, WEAVILE, level = 30, heldItemId = KINGS_ROCK) shouldBe false
        evolutions.isReachable(SNEASEL, WEAVILE, level = 30) shouldBe false

        // A trade with a held item is the same check on a different trigger.
        val metalCoat =
            evolutions.get(ONIX).first { it.method == EvolutionMethod.TRADE_WITH_HELD_ITEM }.param
        evolutions.isReachable(ONIX, STEELIX, level = 30, heldItemId = metalCoat) shouldBe true
        evolutions.isReachable(ONIX, STEELIX, level = 30) shouldBe false
      }

      test("an Everstone on the record refuses everything but a stone") {
        // The gate the game applies, in its own order: the species first, the hold effect second,
        // an item used on the monster exempt.
        evolutions.isReachable(
            CHARMANDER, CHARMELEON, level = 16, heldItemPreventsEvolution = true) shouldBe false
        evolutions.isReachable(CHARMANDER, CHARMELEON, level = 16) shouldBe true
        // A stone is not the Everstone's to refuse: the bag path never asks it.
        evolutions.isReachable(
            PIKACHU, RAICHU, level = 5, heldItemPreventsEvolution = true) shouldBe true
        // Kadabra ignores it outright.
        evolutions.isReachable(
            KADABRA, ALAKAZAM, level = 30, heldItemPreventsEvolution = true) shouldBe true
      }

      test("a Nincada does not become the Shedinja its table names") {
        evolutions.get(NINCADA).map { it.targetSpeciesId } shouldContain SHEDINJA
        // The game makes that one as a second monster, so a record following it loses the Nincada.
        evolutions.isReachable(NINCADA, SHEDINJA, level = 20) shouldBe false
        evolutions.isReachable(NINCADA, NINJASK, level = 20) shouldBe true
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
