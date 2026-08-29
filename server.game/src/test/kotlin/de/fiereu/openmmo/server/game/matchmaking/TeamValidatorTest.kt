package de.fiereu.openmmo.server.game.matchmaking

import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.pvp.Clause
import de.fiereu.openmmo.common.pvp.MatchmakingQueue
import de.fiereu.openmmo.common.pvp.SignupOutcome
import de.fiereu.openmmo.common.pvp.TierGroup
import de.fiereu.openmmo.pokemon.EvolutionRegistry
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldContainExactly
import io.kotest.matchers.shouldBe
import io.kotest.matchers.types.shouldBeInstanceOf
import java.time.LocalDateTime

private const val BULBASAUR = 1
private const val IVYSAUR = 2
private const val CHARMANDER = 4
private const val SQUIRTLE = 7
private const val PIKACHU = 25
private const val EEVEE = 133
private const val MEWTWO = 150
private const val ARCEUS = 493

private var nextId = 1L

private fun mon(dexId: Int, level: Byte = 50, ot: String = "Red", form: Int = 0): Pokemon =
    Pokemon(
        id = nextId++,
        ownerId = 1L,
        container = PokemonContainer.PARTY,
        containerSlot = 0,
        dexId = dexId,
        seed = 0,
        ot = ot,
        nickname = "",
        level = level,
        hp = 100,
        xp = 0,
        eVs = EVs(),
        iVs = IVs(),
        moves = listOf(PokemonMove(33, 35)),
        isShiny = false,
        hasHiddenAbility = false,
        isAlpha = false,
        isSecret = false,
        isFatefulEncounter = false,
        isRaidEncounter = false,
        caughtAt = LocalDateTime.now(),
        form = form,
    )

/** Six distinct, unrelated, level-50 species, a party that passes everything by default. */
private fun legalSix(ot: String = "Red") =
    listOf(BULBASAUR, CHARMANDER, SQUIRTLE, PIKACHU, EEVEE, 143).map { mon(it, ot = ot) }

private fun rules(
    group: TierGroup = TierGroup.OVER_USED,
    clauses: List<ClauseSetting>,
) = QueueRules(MatchmakingQueue.OVER_USED, group, clauses)

private val STANDARD =
    listOf(
        ClauseSetting(Clause.UNIQUE_SPECIES),
        ClauseSetting(Clause.EXACT_PARTY_SIZE, 6),
        ClauseSetting(Clause.MINIMUM_LEVEL, 50),
        ClauseSetting(Clause.MAXIMUM_LEVEL, 50),
    )

class TeamValidatorTest :
    FunSpec({
      val tiers = TierRegistry()
      val validator = TeamValidator(tiers, EvolutionRegistry())

      test("a legal six enters") {
        validator.validate(legalSix(), rules(clauses = STANDARD), "Red") shouldBe
            TeamVerdict.Accepted
      }

      test("an empty party is a party-size refusal, not a clause one") {
        validator.validate(emptyList(), rules(clauses = STANDARD), "Red") shouldBe
            TeamVerdict.Refused(SignupOutcome.PARTY_SIZE)
      }

      test("eggs are not fighters and do not fill the party") {
        val five = legalSix().dropLast(1)
        val withEgg = five + mon(BULBASAUR).copy(isEgg = true)
        val verdict = validator.validate(withEgg, rules(clauses = STANDARD), "Red")
        verdict shouldBe
            TeamVerdict.Refused(SignupOutcome.CLAUSE_VIOLATED, Clause.EXACT_PARTY_SIZE, 6)
      }

      test("a level under the floor names the level, not a clause") {
        val party = legalSix().toMutableList().also { it[0] = mon(BULBASAUR, level = 49) }
        validator.validate(party, rules(clauses = STANDARD), "Red") shouldBe
            TeamVerdict.Refused(SignupOutcome.INVALID_LEVEL, value = 50)
      }

      test("an Uber is refused by the tiering, and taken by an unlimited queue") {
        val party = legalSix().toMutableList().also { it[0] = mon(MEWTWO) }
        validator.validate(party, rules(clauses = STANDARD), "Red") shouldBe
            TeamVerdict.Refused(SignupOutcome.TIERING_OR_BAN)
        validator.validate(party, rules(TierGroup.UNLIMITED, STANDARD), "Red") shouldBe
            TeamVerdict.Accepted
      }

      test("the tiering covers every forme when the row says so") {
        // Arceus is listed with form -1, so a plate forme is refused the same as the ordinary one.
        val party = legalSix().toMutableList().also { it[0] = mon(ARCEUS, form = 11) }
        validator.validate(party, rules(clauses = STANDARD), "Red") shouldBe
            TeamVerdict.Refused(SignupOutcome.TIERING_OR_BAN)
      }

      test("two of one species violate the species clause and name it") {
        val party = legalSix().toMutableList().also { it[1] = mon(BULBASAUR) }
        validator.validate(party, rules(clauses = STANDARD), "Red") shouldBe
            TeamVerdict.Refused(SignupOutcome.CLAUSE_VIOLATED, Clause.UNIQUE_SPECIES)
      }

      test("two from one line pass the species clause and fail the tree clause") {
        val party = legalSix().toMutableList().also { it[1] = mon(IVYSAUR) }
        validator.validate(party, rules(clauses = STANDARD), "Red") shouldBe TeamVerdict.Accepted
        val withTree = rules(clauses = STANDARD + ClauseSetting(Clause.UNIQUE_EVOLUTION_TREE))
        validator.validate(party, withTree, "Red") shouldBe
            TeamVerdict.Refused(SignupOutcome.CLAUSE_VIOLATED, Clause.UNIQUE_EVOLUTION_TREE)
      }

      test("the own-caught clause counts by original trainer and carries its number") {
        val party = legalSix(ot = "Blue")
        val withOt = rules(clauses = STANDARD + ClauseSetting(Clause.MINIMUM_OWN_CAUGHT, 3))
        validator.validate(party, withOt, "Red") shouldBe
            TeamVerdict.Refused(SignupOutcome.CLAUSE_VIOLATED, Clause.MINIMUM_OWN_CAUGHT, 3)
        validator.validate(legalSix(ot = "Red"), withOt, "Red") shouldBe TeamVerdict.Accepted
      }

      test("a clause no record can answer refuses instead of passing") {
        val withItem = rules(clauses = STANDARD + ClauseSetting(Clause.UNIQUE_ITEM))
        validator.unsupported(withItem) shouldContainExactly listOf(Clause.UNIQUE_ITEM)
        validator.validate(legalSix(), withItem, "Red") shouldBe
            TeamVerdict.Refused(SignupOutcome.UNKNOWN)
      }

      test("the queues this server opens ask for nothing it cannot check") {
        QueueRules.ALL.forEach { validator.unsupported(it) shouldBe emptyList() }
      }

      test("a battle clause is not the team check's business") {
        val withSleep = rules(clauses = STANDARD + ClauseSetting(Clause.SLEEP))
        withSleep.teamClauses.map { it.clause } shouldContainExactly STANDARD.map { it.clause }
        validator.validate(legalSix(), withSleep, "Red").shouldBeInstanceOf<TeamVerdict.Accepted>()
      }
    })
