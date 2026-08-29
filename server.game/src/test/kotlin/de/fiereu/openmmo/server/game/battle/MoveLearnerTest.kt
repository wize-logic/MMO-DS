package de.fiereu.openmmo.server.game.battle

import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.moves.MoveRegistry
import de.fiereu.openmmo.pokemon.LearnsetRegistry
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldBeEmpty
import io.kotest.matchers.shouldBe

private const val BULBASAUR = 1
private const val TACKLE: Short = 33
private const val GROWL: Short = 45
private const val LEECH_SEED: Short = 73
private const val VINE_WHIP: Short = 22
private const val POISON_POWDER: Short = 77
private const val SLEEP_POWDER: Short = 79

private fun slots(vararg ids: Short) = ids.map { PokemonMove(it, 0) }.toMutableList()

class MoveLearnerTest :
    FunSpec({
      val moves = MoveRegistry()
      val learner = MoveLearner(LearnsetRegistry(), moves)

      test("a move learned at the new level goes into a free slot") {
        val known = slots(TACKLE, 0, 0, 0)
        val outcome = learner.learn(known, BULBASAUR, 2, 3)
        outcome.learned.map { it.moveId } shouldBe listOf(GROWL.toInt())
        outcome.offered.shouldBeEmpty()
        known.map { it.id } shouldBe listOf(TACKLE, GROWL, 0, 0)
      }

      test("the new move gets its registry pp") {
        val known = slots(TACKLE, 0, 0, 0)
        learner.learn(known, BULBASAUR, 2, 3)
        known[1].pp shouldBe moves.get(GROWL.toInt())!!.pp.toByte()
      }

      test("skipping levels learns every move in between") {
        val known = slots(TACKLE, 0, 0, 0)
        val outcome = learner.learn(known, BULBASAUR, 1, 10)
        outcome.learned.map { it.moveId.toShort() } shouldBe listOf(GROWL, LEECH_SEED, VINE_WHIP)
        known.map { it.id } shouldBe listOf(TACKLE, GROWL, LEECH_SEED, VINE_WHIP)
      }

      test("a full moveset offers what it cannot fit and keeps its moves") {
        val known = slots(TACKLE, GROWL, LEECH_SEED, VINE_WHIP)
        val outcome = learner.learn(known, BULBASAUR, 12, 13)
        outcome.learned.shouldBeEmpty()
        outcome.offered.map { it.moveId.toShort() } shouldBe listOf(POISON_POWDER, SLEEP_POWDER)
        known.map { it.id } shouldBe listOf(TACKLE, GROWL, LEECH_SEED, VINE_WHIP)
      }

      // Bulbasaur learns Poison Powder and Sleep Powder at 13, only the first one fits.
      test("a partly full moveset learns until it runs out of slots") {
        val known = slots(TACKLE, GROWL, LEECH_SEED, 0)
        val outcome = learner.learn(known, BULBASAUR, 12, 13)
        outcome.learned.map { it.moveId.toShort() } shouldBe listOf(POISON_POWDER)
        outcome.offered.map { it.moveId.toShort() } shouldBe listOf(SLEEP_POWDER)
        known.map { it.id } shouldBe listOf(TACKLE, GROWL, LEECH_SEED, POISON_POWDER)
      }

      test("a known move is not learned twice") {
        val known = slots(GROWL, 0, 0, 0)
        val outcome = learner.learn(known, BULBASAUR, 2, 3)
        outcome.learned.shouldBeEmpty()
        outcome.offered.shouldBeEmpty()
        known.map { it.id } shouldBe listOf(GROWL, 0, 0, 0)
      }

      test("no level up learns nothing") {
        val known = slots(TACKLE, 0, 0, 0)
        learner.learn(known, BULBASAUR, 5, 5).learned.shouldBeEmpty()
      }

      test("an unknown species learns nothing") {
        val known = slots(TACKLE, 0, 0, 0)
        learner.learn(known, 9999, 1, 100).learned.shouldBeEmpty()
      }

      test("a moveset shorter than four slots grows") {
        val known = slots(TACKLE)
        learner.learn(known, BULBASAUR, 2, 3)
        known.map { it.id } shouldBe listOf(TACKLE, GROWL)
      }

      test("a move that fitted reports the slot it went into") {
        val known = slots(TACKLE, 0, 0, 0)
        val outcome = learner.learn(known, BULBASAUR, 2, 3)
        outcome.learned.map { it.slot } shouldBe listOf(1)
      }

      test("a move that has to wait for an answer has no slot") {
        val known = slots(TACKLE, GROWL, LEECH_SEED, VINE_WHIP)
        learner.learn(known, BULBASAUR, 12, 13).offered.map { it.slot } shouldBe listOf(-1, -1)
      }

      test("an offered move takes the slot the player picked") {
        val known = slots(TACKLE, GROWL, LEECH_SEED, VINE_WHIP)
        learner.apply(known, 0, POISON_POWDER, POISON_POWDER) shouldBe true
        known.map { it.id } shouldBe listOf(POISON_POWDER, GROWL, LEECH_SEED, VINE_WHIP)
        known[0].pp shouldBe moves.get(POISON_POWDER.toInt())!!.pp.toByte()
      }

      test("the moves it did not take hold on to their pp") {
        val known = mutableListOf(PokemonMove(TACKLE, 7), PokemonMove(GROWL, 3))
        learner.apply(known, 0, POISON_POWDER, POISON_POWDER) shouldBe true
        known.map { it.id to it.pp } shouldBe listOf(POISON_POWDER to 35.toByte(), GROWL to 3)
      }

      test("a move that was not the one offered is refused") {
        val known = slots(TACKLE, GROWL, LEECH_SEED, VINE_WHIP)
        learner.apply(known, 0, SLEEP_POWDER, POISON_POWDER) shouldBe false
        known.map { it.id } shouldBe listOf(TACKLE, GROWL, LEECH_SEED, VINE_WHIP)
      }

      test("a move the monster already knows is refused") {
        val known = slots(TACKLE, GROWL, LEECH_SEED, VINE_WHIP)
        learner.apply(known, 0, GROWL, GROWL) shouldBe false
        known.map { it.id } shouldBe listOf(TACKLE, GROWL, LEECH_SEED, VINE_WHIP)
      }

      test("a slot the moveset does not have is refused") {
        val known = slots(TACKLE, GROWL)
        learner.apply(known, 2, POISON_POWDER, POISON_POWDER) shouldBe false
        learner.apply(known, -1, POISON_POWDER, POISON_POWDER) shouldBe false
        known.map { it.id } shouldBe listOf(TACKLE, GROWL)
      }
    })
