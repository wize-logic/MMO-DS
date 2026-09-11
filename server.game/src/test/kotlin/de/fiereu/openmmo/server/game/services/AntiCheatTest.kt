package de.fiereu.openmmo.server.game.services

import de.fiereu.openmmo.items.ItemDef
import de.fiereu.openmmo.server.game.battle.BattleRng
import de.fiereu.openmmo.server.game.battle.CatchRoll
import de.fiereu.openmmo.server.game.testsupport.testGameConfig
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.booleans.shouldBeTrue
import io.kotest.matchers.collections.shouldContain
import io.kotest.matchers.ints.shouldBeGreaterThan
import io.kotest.matchers.ints.shouldBeLessThan
import io.kotest.matchers.nulls.shouldBeNull
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe
import java.time.Duration

/** The pieces the anti-cheat work added, tested where each can be tested alone. */
class AntiCheatTest :
    FunSpec({
      context("a reported capture is the server's monster, not the client's") {
        val roller = ReportedIndividual(testGameConfig("a-server-secret"))

        test("the same claim always names the same individual, so a retry is not a second catch") {
          roller.forToken(characterId = 7, dexId = 25, token = 0x1234) shouldBe
              roller.forToken(characterId = 7, dexId = 25, token = 0x1234)
        }

        test("a different claim, character or species is a different individual") {
          val base = roller.forToken(7, 25, 0x1234)
          (roller.forToken(7, 25, 0x1235) == base) shouldBe false
          (roller.forToken(8, 25, 0x1234) == base) shouldBe false
          (roller.forToken(7, 26, 0x1234) == base) shouldBe false
        }

        test("a second server does not derive the first server's monsters") {
          val other = ReportedIndividual(testGameConfig("a-different-secret"))
          (other.forToken(7, 25, 0x1234) == roller.forToken(7, 25, 0x1234)) shouldBe false
        }

        test("every iv is inside the range the record can hold") {
          for (token in 1..500) {
            val ivBits = roller.forToken(1, 1, token).ivBits
            for (slot in 0 until 6) {
              val iv = (ivBits shr (slot * 5)) and 31
              iv shouldBeGreaterThan -1
              iv shouldBeLessThan 32
            }
          }
        }

        /**
         * The claim cannot buy a shiny. A thousand captures at one in 8,192 turn up a handful at
         * most, and the point of the assertion is that it is nowhere near a thousand: before this
         * the client said so and every one of them was.
         */
        test("shininess is a rare draw rather than something the report decides") {
          (1..1000).count { roller.forToken(1, 1, it).isShiny } shouldBeLessThan 10
        }
      }

      context("refusals are counted, not only logged") {
        test("a tally is kept per character and per kind, inside the window") {
          var now = 0L
          val log = ViolationLog(ViolationLog.Limits(window = Duration.ofMinutes(10)), { now })

          log.record(1, ViolationLog.Kind.IMPOSSIBLE_PACE, "too fast")
          log.record(1, ViolationLog.Kind.IMPOSSIBLE_PACE, "too fast again")
          log.record(1, ViolationLog.Kind.FORBIDDEN_ITEM, "a bicycle it already has")
          log.record(2, ViolationLog.Kind.IMPOSSIBLE_PACE, "somebody else")

          log.countsFor(1) shouldBe
              mapOf(
                  ViolationLog.Kind.IMPOSSIBLE_PACE to 2,
                  ViolationLog.Kind.FORBIDDEN_ITEM to 1,
              )
          log.countsFor(2) shouldBe mapOf(ViolationLog.Kind.IMPOSSIBLE_PACE to 1)
          log.totalRecorded() shouldBe 4
        }

        test("the window turns over") {
          var now = 0L
          val log = ViolationLog(ViolationLog.Limits(window = Duration.ofMinutes(10)), { now })
          log.record(1, ViolationLog.Kind.NOT_YOURS, "somebody else's listing")
          now += Duration.ofMinutes(11).toNanos()
          log.countsFor(1) shouldBe emptyMap()
        }

        test("a session with no character still leaves an entry to read") {
          val log = ViolationLog()
          log.record(null, ViolationLog.Kind.OUT_OF_SEQUENCE, "before it said who it was")
          log.recent(5).map { it.detail } shouldContain "before it said who it was"
        }

        test("the ring keeps the newest and forgets the oldest") {
          val log = ViolationLog(ViolationLog.Limits(ringSize = 4), System::nanoTime)
          for (i in 1..10) log.record(1, ViolationLog.Kind.IMPOSSIBLE_PACE, "step $i")
          val kept = log.recent(10).map { it.detail }
          kept.size shouldBe 4
          kept shouldContain "step 10"
        }
      }

      context("a pace nobody can play faster than") {
        test("the burst is spendable and then the bucket is empty") {
          var now = 0L
          val pace = PaceLimit(burst = 3.0, perSecond = 1.0, clock = { now })
          pace.allow(1) shouldBe true
          pace.allow(1) shouldBe true
          pace.allow(1) shouldBe true
          pace.allow(1) shouldBe false
        }

        test("waiting refills it") {
          var now = 0L
          val pace = PaceLimit(burst = 2.0, perSecond = 1.0, clock = { now })
          pace.allow(1) shouldBe true
          pace.allow(1) shouldBe true
          pace.allow(1) shouldBe false
          now += 1_000_000_000L
          pace.allow(1) shouldBe true
        }

        test("one character's flood does not cost another their step") {
          var now = 0L
          val pace = PaceLimit(burst = 1.0, perSecond = 1.0, clock = { now })
          pace.allow(1) shouldBe true
          pace.allow(1) shouldBe false
          pace.allow(2) shouldBe true
        }
      }

      context("a ball has to hold") {
        fun ball(name: String) =
            ItemDef(name = name, price = 200, useClass = ItemDef.USE_CLASS_BALL)

        test("a master ball never fails") {
          CatchRoll.holds(
              ball("Master Ball"),
              catchRate = 3,
              currentHp = 100,
              maxHp = 100,
              rng = BattleRng(1),
          ) shouldBe true
        }

        test("a poke ball does not simply take a healthy hard-to-catch monster") {
          (1..200).count {
            CatchRoll.holds(
                ball("Poke Ball"),
                catchRate = 3,
                currentHp = 100,
                maxHp = 100,
                rng = BattleRng(it.toLong()),
            )
          } shouldBeLessThan 20
        }

        test("a weakened easy monster is always taken") {
          (1..200).count {
            CatchRoll.holds(
                ball("Ultra Ball"),
                catchRate = 255,
                currentHp = 1,
                maxHp = 100,
                rng = BattleRng(it.toLong()),
            )
          } shouldBe 200
        }
      }

      context("a name is drawn on everybody else's screen") {
        test("an ordinary name is fine") {
          CharacterNames.refuse("Barry").shouldBeNull()
          CharacterNames.refuse("Anne-Marie O'Hara").shouldBeNull()
        }

        /**
         * Written as escapes so this file stays ascii: a Cyrillic A in front of dmin, an acute e,
         * and a right to left override, all of which draw as something they are not.
         */
        test("anything outside the alphabet a client can draw is refused") {
          CharacterNames.refuse("\u0410dmin").shouldNotBeNull()
          CharacterNames.refuse("Barr\u00e9").shouldNotBeNull()
          CharacterNames.refuse("Barry\u202e").shouldNotBeNull()
        }

        test("a word this server speaks with is not a player") {
          CharacterNames.refuse("Admin").shouldNotBeNull()
          CharacterNames.refuse("G M").shouldNotBeNull()
          CharacterNames.refuse("moderator").shouldNotBeNull()
          // Not a ban on the letters, only on the whole name.
          CharacterNames.refuse("Modest Mouse").shouldBeNull()
        }

        test("empty, over-long, badly started and badly ended names are refused") {
          CharacterNames.refuse("").shouldNotBeNull()
          CharacterNames.refuse("a".repeat(CharacterNames.MAX_LENGTH + 1)).shouldNotBeNull()
          CharacterNames.refuse("Barry ").shouldNotBeNull()
          CharacterNames.refuse("Bar  ry").shouldNotBeNull()
          CharacterNames.refuse("1Barry").shouldNotBeNull()
        }
      }

      context("a contest ribbon has to have been won") {
        test("a credit is issued once and spent once") {
          val credits = ContestRibbonCredits()
          credits.spend(1) shouldBe false
          credits.award(1)
          credits.held(1) shouldBe 1
          credits.spend(1).shouldBeTrue()
          credits.spend(1) shouldBe false
        }

        test("one player's contest does not pay another player's ribbon") {
          val credits = ContestRibbonCredits()
          credits.award(1)
          credits.spend(2) shouldBe false
          credits.spend(1).shouldBeTrue()
        }
      }
    })
