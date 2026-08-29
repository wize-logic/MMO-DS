package de.fiereu.openmmo.server.web

import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import java.time.Duration

class RateLimiterTest :
    FunSpec({
      test("the window rolls over and the caller starts again") {
        var now = 0L
        val limiter = RateLimiter(2, Duration.ofHours(1), clock = { now })

        limiter.allow("a") shouldBe true
        limiter.allow("a") shouldBe true
        limiter.allow("a") shouldBe false

        now += Duration.ofHours(1).toNanos()
        limiter.allow("a") shouldBe true
      }

      test("a key never seen before is refused once the table is full") {
        var now = 0L
        val limiter = RateLimiter(10, Duration.ofHours(1), maxKeys = 2, clock = { now })

        limiter.allow("a") shouldBe true
        limiter.allow("b") shouldBe true
        // Two addresses already hold the table, so a third is turned away rather than stored.
        limiter.allow("c") shouldBe false
        // The two that got in are still served.
        limiter.allow("a") shouldBe true

        // The window empties on its own, and the table with it.
        now += Duration.ofHours(1).toNanos()
        limiter.allow("c") shouldBe true
      }

      test("keys are counted apart") {
        val limiter = RateLimiter(1, clock = { 0 })

        limiter.allow("a") shouldBe true
        limiter.allow("b") shouldBe true
        limiter.allow("a") shouldBe false
      }
    })
