package de.fiereu.openmmo.common.pvp

import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldContainExactly
import io.kotest.matchers.shouldBe

/**
 * The competitive tables are wire values, not orderings of ours, and every one of them is a number
 * somebody could "tidy" into its ordinal without noticing. These are the claims that would break
 * silently if they did.
 */
class PvpRulesTest :
    FunSpec({
      test("a queue's wire byte is not its tab position") {
        // The trap: the source table's construction order and its display order are
        // different fields, and reading either as the other puts Doubles where Under
        // Used should be.
        MatchmakingQueue.UNDER_USED.id shouldBe 4
        MatchmakingQueue.UNDER_USED.displayOrder shouldBe 1
        MatchmakingQueue.DOUBLES.id shouldBe 2
        MatchmakingQueue.DOUBLES.displayOrder shouldBe 4
        MatchmakingQueue.fromId(4) shouldBe MatchmakingQueue.UNDER_USED
      }

      test("every queue byte is distinct and every one round-trips") {
        MatchmakingQueue.entries.map { it.id }.toSet().size shouldBe 20
        MatchmakingQueue.entries.forEach { MatchmakingQueue.fromId(it.id) shouldBe it }
      }

      test("a family offers an unranked queue and a ranked one, in that order") {
        MatchmakingQueue.of(QueueFamily.OVER_USED) shouldContainExactly
            listOf(MatchmakingQueue.OVER_USED, MatchmakingQueue.OVER_USED_RANKED)
        MatchmakingQueue.entries.count { it.ranked } shouldBe 10
      }

      test("only the Randoms families hand out a team") {
        MatchmakingQueue.entries.filterNot { it.ownParty }.map { it.family }.toSet() shouldBe
            setOf(
                QueueFamily.RANDOMS,
                QueueFamily.RANDOMS_HALLOWEEN,
                QueueFamily.RANDOMS_XMAS,
                QueueFamily.RANDOMS_EVENT,
                QueueFamily.RANDOMS_LNY,
            )
      }

      test("a clause's strings are its byte plus the table's two offsets") {
        Clause.EVASION.nameStringId shouldBe 5700
        Clause.EVASION.descriptionStringId shouldBe 5720
        Clause.OPEN_TEAM_SHEET.id shouldBe 12
        Clause.OPEN_TEAM_SHEET.nameStringId shouldBe 5712
        Clause.entries.size shouldBe 13
      }

      test("exactly four clauses take a number, and they are the four that print one") {
        Clause.entries.filter { it.takesNumber } shouldContainExactly
            listOf(
                Clause.MINIMUM_OWN_CAUGHT,
                Clause.EXACT_PARTY_SIZE,
                Clause.MINIMUM_LEVEL,
                Clause.MAXIMUM_LEVEL,
            )
      }

      test("three clauses belong to the running fight and one to team preview") {
        Clause.entries.filter { it.enforcedBy == ClauseSite.BATTLE } shouldContainExactly
            listOf(Clause.EVASION, Clause.SLEEP, Clause.OHKO)
        Clause.entries.filter { it.enforcedBy == ClauseSite.TEAM_PREVIEW } shouldContainExactly
            listOf(Clause.OPEN_TEAM_SHEET)
      }

      test("only the accepted outcome opens a signup") {
        SignupOutcome.entries.filter { it.accepted } shouldContainExactly
            listOf(SignupOutcome.ACCEPTED)
      }

      test("byte 13 is the requirements sentence, which is the one the lookup keeps") {
        SignupOutcome.fromId(13) shouldBe SignupOutcome.REQUIREMENTS_NOT_MET
        SignupOutcome.REQUIREMENTS_NOT_MET.stringId shouldBe 5608
        SignupOutcome.entries.map { it.id } shouldContainExactly (0..17).map { it.toByte() }
      }

      test("a series names the wins that take it, and normal is one battle") {
        SeriesRule.NORMAL.wins shouldBe 0
        SeriesRule.NORMAL.maxBattles shouldBe 1
        SeriesRule.BEST_OF_3.wins shouldBe 2
        SeriesRule.BEST_OF_3.maxBattles shouldBe 3
        SeriesRule.BEST_OF_7.maxBattles shouldBe 7
      }

      test("the timer bytes start at one, not zero") {
        TimerRule.NONE.id shouldBe 1
        TimerRule.DOUBLES_OFFICIAL.id shouldBe 7
        TimerRule.entries.filter { it.timed }.size shouldBe 6
      }

      test("a species' tier mask reads lowest bit first") {
        val mask = TierGroup.UNDER_USED.bit or TierGroup.OVER_USED.bit
        TierGroup.fromMask(mask) shouldContainExactly
            listOf(TierGroup.OVER_USED, TierGroup.UNDER_USED)
        TierGroup.fromMask(0) shouldBe emptyList()
      }

      test("the long name is twenty past the short one, or a hundred past a 7200") {
        TierGroup.UBERS.descriptionStringId shouldBe 5770
        TierGroup.RANDOMS_EVENT.nameStringId shouldBe 7200
        TierGroup.RANDOMS_EVENT.descriptionStringId shouldBe 7300
      }

      test("six of the sixteen groups name a species tier") {
        TierGroup.entries.filter { it.speciesTier } shouldContainExactly
            listOf(
                TierGroup.UBERS,
                TierGroup.OVER_USED,
                TierGroup.UNDER_USED,
                TierGroup.NEVER_USED,
                TierGroup.UNLIMITED,
                TierGroup.UNTIERED,
            )
      }
    })
