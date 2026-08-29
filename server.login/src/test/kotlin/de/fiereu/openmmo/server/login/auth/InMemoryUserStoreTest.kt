package de.fiereu.openmmo.server.login.auth

import de.fiereu.openmmo.common.enums.LoginState
import de.fiereu.openmmo.common.utils.toHex
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import io.kotest.matchers.shouldNotBe
import java.time.Duration

class InMemoryUserStoreTest :
    FunSpec({
      test("seeds the admin and test users") {
        val store = InMemoryUserStore()
        store.getUserId("admin") shouldNotBe null
        store.getUserId("test") shouldNotBe null
      }

      test("authenticate succeeds when password hash matches SHA-1 hex") {
        val store = InMemoryUserStore()
        val sha1Hex = sha1HexOf("admin")
        val result = store.authenticate("admin", sha1Hex)
        result.state shouldBe LoginState.AUTHED
        result.userId shouldBe store.getUserId("admin")
      }

      test("authenticate is case-insensitive on username") {
        val store = InMemoryUserStore()
        val sha1Hex = sha1HexOf("test")
        val result = store.authenticate("TEST", sha1Hex)
        result.state shouldBe LoginState.AUTHED
      }

      test("authenticate fails for unknown user") {
        val store = InMemoryUserStore()
        store.authenticate("nobody", "whatever").state shouldBe LoginState.INVALID_PASSWORD
      }

      test("authenticate fails for wrong password") {
        val store = InMemoryUserStore()
        store.authenticate("admin", "wrong").state shouldBe LoginState.INVALID_PASSWORD
      }

      test("addUser assigns ascending ids") {
        val store = InMemoryUserStore()
        val a = store.addUser("alice", "pw")
        val b = store.addUser("bob", "pw")
        (b > a) shouldBe true
      }
    })

private fun sha1HexOf(input: String): String =
    java.security.MessageDigest.getInstance("SHA-1").digest(input.toByteArray()).toHex()

/** What stops somebody working through a password list. It runs before the password is checked. */
class LoginAttemptLimiterTest :
    FunSpec({
      var now = 0L
      fun limiter(perAccount: Int = 3, perAddress: Int = 5) =
          LoginAttemptLimiter(
              perAccount = perAccount,
              perAddress = perAddress,
              window = Duration.ofMinutes(15),
              maxKeys = 1_000,
          ) {
            now
          }

      test("an account is turned down after enough wrong passwords from one address") {
        now = 0
        val limiter = limiter()

        repeat(3) {
          limiter.allow("ash", "10.0.0.1") shouldBe true
          limiter.recordFailure("ash", "10.0.0.1")
        }

        limiter.allow("ash", "10.0.0.1") shouldBe false
      }

      /** Why the account counter is paired with an address: alone it would be a lockout lever. */
      test("one address guessing an account does not lock its owner out") {
        now = 0
        val limiter = limiter()

        repeat(5) { limiter.recordFailure("ash", "10.0.0.1") }

        limiter.allow("ash", "10.0.0.1") shouldBe false
        limiter.allow("ash", "10.0.0.2") shouldBe true
      }

      test("an address working through a list of accounts is turned down") {
        now = 0
        val limiter = limiter()

        for (name in listOf("a", "b", "c", "d", "e")) limiter.recordFailure(name, "10.0.0.9")

        limiter.allow("f", "10.0.0.9") shouldBe false
      }

      test("getting it right clears what the pair has spent") {
        now = 0
        val limiter = limiter()
        repeat(3) { limiter.recordFailure("ash", "10.0.0.1") }

        limiter.recordSuccess("ash", "10.0.0.1")

        limiter.allow("ash", "10.0.0.1") shouldBe true
      }

      test("the window lets a real player back in") {
        now = 0
        val limiter = limiter()
        repeat(3) { limiter.recordFailure("ash", "10.0.0.1") }
        limiter.allow("ash", "10.0.0.1") shouldBe false

        now += Duration.ofMinutes(16).toNanos()

        limiter.allow("ash", "10.0.0.1") shouldBe true
      }
    })
