package de.fiereu.openmmo.server.login.auth

import de.fiereu.openmmo.common.enums.LoginState
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import io.kotest.matchers.types.shouldBeInstanceOf

class CreateAccountTest :
    FunSpec({
      test("a new name is created and can then authenticate") {
        val store = InMemoryUserStore()
        val outcome = CreateAccount.create(store, "Alice", "hunter2")

        val created = outcome.shouldBeInstanceOf<CreateAccount.Outcome.Created>()
        created.username shouldBe "alice"
        store.getUserId("alice") shouldBe created.userId
        store.authenticate("alice", sha1Hex("hunter2")).state shouldBe LoginState.AUTHED
      }

      test("a name that is already taken is refused") {
        val store = InMemoryUserStore()

        CreateAccount.create(store, "admin", "whatever") shouldBe
            CreateAccount.Outcome.Rejected("account 'admin' already exists")
      }

      test("a blank or oversized username is refused before the store is asked") {
        CreateAccount.validate("", "pw") shouldBe "username must not be blank"
        CreateAccount.validate("   ", "pw") shouldBe "username must not be blank"
        CreateAccount.validate("a".repeat(33), "pw") shouldBe
            "username is longer than ${CreateAccount.USERNAME_MAX} characters"
        CreateAccount.validate("alice smith", "pw") shouldBe
            "username cannot contain spaces or control characters"
        CreateAccount.validate("alice", " ") shouldBe "password must not be blank"
      }

      test("the stored username is lowercase even when the operator typed mixed case") {
        val store = InMemoryUserStore()
        val outcome = CreateAccount.create(store, "Bob", "pw")

        outcome.shouldBeInstanceOf<CreateAccount.Outcome.Created>().username shouldBe "bob"
        store.authenticate("BOB", sha1Hex("pw")).state shouldBe LoginState.AUTHED
      }
    })
