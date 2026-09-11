package de.fiereu.openmmo.offline

import de.fiereu.openmmo.pokemon.MoveSourceRegistry
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldContain
import io.kotest.matchers.collections.shouldNotBeEmpty
import io.kotest.matchers.collections.shouldNotContain
import io.kotest.matchers.ints.shouldBeGreaterThan
import io.kotest.matchers.ints.shouldBeLessThan
import io.kotest.matchers.nulls.shouldBeNull
import io.kotest.matchers.shouldBe

private const val BULBASAUR = 1
private const val PIKACHU = 25
private const val MACHAMP = 68
private const val ARTICUNO = 144
private const val MEWTWO = 150
private const val CELEBI = 251
private const val KYOGRE = 382
private const val TURTWIG = 387
private const val SPIRITOMB = 442
private const val GIRATINA = 487
private const val PHIONE = 489
private const val MANAPHY = 490
private const val DARKRAI = 491
private const val SHAYMIN = 492
private const val ARCEUS = 493
private const val SNIVY = 495
private const val TYNAMO = 602

private const val CUT = 15
private const val LEECH_SEED = 73
private const val LIGHT_SCREEN = 113
private const val KNOCK_OFF = 282
private const val BRICK_BREAK = 280
private const val EARTHQUAKE = 89

/**
 * The two tables an offline save is measured against, checked against what the cartridge is known
 * to do rather than against themselves.
 */
class CartridgeLimitsTest :
    FunSpec({
      val limits = CartridgeLimits()

      test("the set is the size of a national dex a Sinnoh cartridge can fill on its own") {
        limits.species.size shouldBeGreaterThan 350
        limits.species.size shouldBeLessThan SpeciesRegistry().size()
      }

      test("what the cartridge hands out itself is in it") {
        limits.species shouldContain TURTWIG
        limits.species shouldContain SPIRITOMB
        limits.species shouldContain GIRATINA
        // The roaming birds, which follow the starter once the national dex is in hand.
        limits.species shouldContain ARTICUNO
      }

      test(
          "an evolution nobody can reach alone is still in it, because a link trade is not alone") {
            // Machoke becomes Machamp on a trade, and two people who each own the game can do that.
            // A
            // table that dropped it would turn away an honest save.
            limits.species shouldContain MACHAMP
          }

      test("a species only another cartridge has is not in it") {
        limits.species shouldNotContain BULBASAUR
        limits.species shouldNotContain MEWTWO
        limits.species shouldNotContain CELEBI
        limits.species shouldNotContain KYOGRE
        limits.species shouldNotContain SNIVY
      }

      test("the distributions are not in it, nor what only they could breed") {
        limits.species shouldNotContain DARKRAI
        limits.species shouldNotContain SHAYMIN
        limits.species shouldNotContain ARCEUS
        limits.species shouldNotContain MANAPHY
        limits.species shouldNotContain PHIONE
      }

      test("the money cap is a playthrough's worth, not a typed number") {
        // Every trainer once with the Amulet Coin comes to more than one wallet holds, which is
        // what makes the wallet the binding limit and this the weekly one.
        limits.moneyCap shouldBeGreaterThan 999_999
      }

      test("the items the game asks after and cannot grant are named") {
        // The Suite Key and the four Mystery Gift items: checked by a script, removed by a script,
        // handed over by nothing.
        limits.eventItems.size shouldBe 5
        limits.eventItems shouldContain 455 // the Azure Flute
      }
    })

/** The three lists that say what a species may know besides what a level taught it. */
class MoveSourceRegistryTest :
    FunSpec({
      val sources = MoveSourceRegistry()

      test("every species the cartridge has data for has a list") {
        sources.size() shouldBeGreaterThan 480
      }

      test("Bulbasaur's machines, egg moves and tutors are the ones it has in the games") {
        val bulbasaur = checkNotNull(sources.get(BULBASAUR))
        bulbasaur.machineMoves shouldContain CUT // HM01
        bulbasaur.eggMoves shouldContain LIGHT_SCREEN
        bulbasaur.tutorMoves shouldContain KNOCK_OFF
        bulbasaur.teaches(LEECH_SEED) shouldBe false // its own, but by level
      }

      test("a species from another cartridge is held to the machines that cartridge gives it") {
        // Until 2026-09-06 this asked for null here, and null was right while nothing knew
        // what a ported species could be taught.
        val snivy = checkNotNull(sources.get(SNIVY))
        snivy.machineMoves.shouldNotBeEmpty()
        snivy.teaches(LIGHT_SCREEN) shouldBe true // TM16 in both games
        snivy.teaches(CUT) shouldBe true // HM01 in both, and a Grass snake can cut
        snivy.teaches(EARTHQUAKE) shouldBe false // TM26, and no machine gives it to Snivy
      }

      test("a species nothing knows about still has no list rather than an empty one") {
        // "cannot learn that" and "nothing here knows what it can learn" are different
        // answers, and the second one still has to be sayable.
        sources.get(TYNAMO).shouldBeNull()
        sources.get(SpeciesRegistry().size() + 1).shouldBeNull()
      }

      test("a machine move is a move the species may know, and a machine it lacks is not") {
        val pikachu = checkNotNull(sources.get(PIKACHU))
        pikachu.teaches(BRICK_BREAK) shouldBe true // TM31
        pikachu.teaches(CUT) shouldBe false // Gen 4 took HM01 off it
      }
    })
