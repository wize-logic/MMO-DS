package de.fiereu.openmmo.server.login.auth

import de.fiereu.openmmo.common.auth.AccountRole
import de.fiereu.openmmo.common.auth.AccountRoles
import de.fiereu.openmmo.common.enums.LoginState
import de.fiereu.openmmo.common.utils.toHex
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import java.time.Duration

class InMemoryUserStoreTest :
    FunSpec({
      test("a new store holds no account at all") {
        val store = InMemoryUserStore()
        store.hasAnyUser() shouldBe false
        store.getUserId("admin") shouldBe null
        store.getUserId("test") shouldBe null
      }

      test("authenticate succeeds when password hash matches SHA-1 hex") {
        val store = InMemoryUserStore()
        store.addUser("admin", "admin")
        val sha1Hex = sha1HexOf("admin")
        val result = store.authenticate("admin", sha1Hex)
        result.state shouldBe LoginState.AUTHED
        result.userId shouldBe store.getUserId("admin")
      }

      test("authenticate is case-insensitive on username") {
        val store = InMemoryUserStore()
        store.addUser("test", "test")
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
        store.addUser("admin", "admin")
        store.authenticate("admin", "wrong").state shouldBe LoginState.INVALID_PASSWORD
      }

      test("addUser assigns ascending ids") {
        val store = InMemoryUserStore()
        val a = store.addUser("alice", "pw")
        val b = store.addUser("bob", "pw")
        (b > a) shouldBe true
      }

      test("the first account is a developer and every one after it is not") {
        val store = InMemoryUserStore()

        val first = store.addUser("alice", "pw")
        val second = store.addUser("bob", "pw")

        store.rolesOf(first).has(AccountRole.DEVELOPER) shouldBe true
        store.rolesOf(second) shouldBe AccountRoles.NONE
      }

      test("a developer reaches every role below it") {
        val store = InMemoryUserStore()
        val first = store.addUser("alice", "pw")

        val roles = store.rolesOf(first)

        roles.has(AccountRole.ADMIN) shouldBe true
        roles.has(AccountRole.MODERATOR) shouldBe true
      }

      test("roles are handed out and taken back one at a time") {
        val store = InMemoryUserStore()
        store.addUser("alice", "pw")
        val id = store.addUser("bob", "pw")

        store.setRoles(id, AccountRoles.of(AccountRole.MODERATOR)) shouldBe true
        store.rolesOf(id).has(AccountRole.MODERATOR) shouldBe true
        store.rolesOf(id).has(AccountRole.ADMIN) shouldBe false

        store.setRoles(id, store.rolesOf(id) - AccountRole.MODERATOR) shouldBe true
        store.rolesOf(id) shouldBe AccountRoles.NONE
      }

      test("a new password replaces the old one, which stops working") {
        val store = InMemoryUserStore()
        val id = store.addUser("alice", "old")

        store.setPassword(id, "new") shouldBe true

        store.authenticate("alice", sha1HexOf("new")).state shouldBe LoginState.AUTHED
        store.authenticate("alice", sha1HexOf("old")).state shouldBe LoginState.INVALID_PASSWORD
      }

      test("setPassword reports that an account it cannot find was not written") {
        InMemoryUserStore().setPassword(404, "pw") shouldBe false
      }

      test("setRoles reports that an account it cannot find was not written") {
        InMemoryUserStore().setRoles(404, AccountRoles.of(AccountRole.ADMIN)) shouldBe false
      }

      test("an account nobody has heard of has no roles rather than failing") {
        InMemoryUserStore().rolesOf(404) shouldBe AccountRoles.NONE
      }
    })

private fun sha1HexOf(input: String): String =
    java.security.MessageDigest.getInstance("SHA-1").digest(input.toByteArray()).toHex()

/**
 * What stops somebody working through a password list. It runs before the password is checked, so
 * an attempt nobody is allowed to make buys none of the hashing either.
 */
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

      /**
       * The reason the account counter is paired with an address rather than standing alone: on its
       * own it would let anybody lock any player out by failing a few logins on their behalf.
       */
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
