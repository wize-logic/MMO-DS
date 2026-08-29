package de.fiereu.openmmo.server.game.services

import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import java.time.Duration

class GrantBudgetTest :
    FunSpec({
      fun budget(clock: () -> Long, limit: Int = 100) =
          GrantBudget(
              GrantBudget.Limits(
                  window = Duration.ofMinutes(1),
                  moneyGained = limit,
                  itemsGained = limit,
                  monstersGranted = limit,
              ),
              clock,
          )

      test("a client may be given up to the allowance and no more") {
        var now = 0L
        val budget = budget({ now })

        budget.allow(1, GrantBudget.Kind.MONEY, 60) shouldBe true
        budget.allow(1, GrantBudget.Kind.MONEY, 40) shouldBe true
        budget.allow(1, GrantBudget.Kind.MONEY, 1) shouldBe false
      }

      /** Asking again inside the window must not hand back the difference. */
      test("a refused claim is still counted") {
        var now = 0L
        val budget = budget({ now })

        budget.allow(1, GrantBudget.Kind.ITEMS, 200) shouldBe false
        budget.allow(1, GrantBudget.Kind.ITEMS, 1) shouldBe false

        now += Duration.ofMinutes(1).toNanos()
        budget.allow(1, GrantBudget.Kind.ITEMS, 1) shouldBe true
      }

      test("the kinds are counted apart, and so are characters") {
        var now = 0L
        val budget = budget({ now })

        budget.allow(1, GrantBudget.Kind.MONEY, 100) shouldBe true
        budget.allow(1, GrantBudget.Kind.ITEMS, 100) shouldBe true
        budget.allow(2, GrantBudget.Kind.MONEY, 100) shouldBe true
        budget.allow(1, GrantBudget.Kind.MONEY, 1) shouldBe false
      }

      /**
       * Spending, consuming and releasing are the player giving something up. Counting those would
       * mean a player who shops enough can no longer shop.
       */
      test("giving something up is not a grant and costs no allowance") {
        var now = 0L
        val budget = budget({ now })

        repeat(10) { budget.allow(1, GrantBudget.Kind.MONEY, -1_000) shouldBe true }

        budget.allow(1, GrantBudget.Kind.MONEY, 100) shouldBe true
      }
    })
