package de.fiereu.openmmo.server.web

import de.fiereu.openmmo.common.enums.LoginState
import de.fiereu.openmmo.server.login.auth.InMemoryUserStore
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import io.kotest.matchers.types.shouldBeInstanceOf
import java.security.MessageDigest

private fun sha1Hex(value: String) =
    MessageDigest.getInstance("SHA-1").digest(value.toByteArray()).joinToString("") {
      "%02x".format(it)
    }

class RegistrationServiceTest :
    FunSpec({
      fun service(perHour: Int = 5, attemptsPerHour: Int = 100, total: Int = 1000) =
          InMemoryUserStore().let {
            it to
                RegistrationService(
                    it,
                    limiter = RateLimiter(perHour),
                    attempts = RateLimiter(attemptsPerHour),
                    global = RateLimiter(total),
                )
          }

      test("a filled in form creates the account the client can then log in with") {
        val (store, service) = service()

        val outcome = service.register("Ash", "pikachu-1", "pikachu-1", "203.0.113.5")

        outcome.shouldBeInstanceOf<RegistrationService.Outcome.Created>().username shouldBe "ash"
        store.authenticate("ash", sha1Hex("pikachu-1")).state shouldBe LoginState.AUTHED
      }

      test("the confirmation has to match, and nothing is written when it does not") {
        val (store, service) = service()

        service.register("misty", "staryu-42", "starmie-42", "203.0.113.5") shouldBe
            RegistrationService.Outcome.Rejected("the two passwords do not match")
        store.getUserId("misty") shouldBe null
      }

      test("weak or careless passwords are refused") {
        val (_, service) = service()

        service.register("brock", "short", "short", "203.0.113.5") shouldBe
            RegistrationService.Outcome.Rejected(
                "password must be at least ${RegistrationService.PASSWORD_MIN} characters")
        service.register("brock", "BrockBrock", "BrockBrock", "203.0.113.5") shouldBe
            RegistrationService.Outcome.Rejected("password must not contain the account name")
        service.register("brock brock", "onix-rocks", "onix-rocks", "203.0.113.5") shouldBe
            RegistrationService.Outcome.Rejected(
                "username cannot contain spaces or control characters")
        service.register("<b>", "onix-rocks", "onix-rocks", "203.0.113.5") shouldBe
            RegistrationService.Outcome.Rejected(
                "account name may only use letters, digits, underscore and hyphen")
        service.register("ab", "onix-rocks", "onix-rocks", "203.0.113.5") shouldBe
            RegistrationService.Outcome.Rejected(
                "account name must be at least ${RegistrationService.NAME_MIN} characters")
      }

      test("a name already in the database is refused however it is capitalised") {
        val (_, service) = service()

        service.register("ADMIN", "correct-horse", "correct-horse", "203.0.113.5") shouldBe
            RegistrationService.Outcome.Taken
      }

      test("one address only gets so many accounts an hour") {
        val (_, service) = service(perHour = 2)

        service.register("one", "password-1", "password-1", "203.0.113.5")
        service.register("two", "password-2", "password-2", "203.0.113.5")

        service.register("three", "password-3", "password-3", "203.0.113.5") shouldBe
            RegistrationService.Outcome.RateLimited
        // The limit is per address, so the next visitor is unaffected.
        service
            .register("four", "password-4", "password-4", "198.51.100.7")
            .shouldBeInstanceOf<RegistrationService.Outcome.Created>()
      }

      test("a refused form does not spend the visitor's allowance") {
        val (_, service) = service(perHour = 1)

        service.register("five", "nope", "nope", "203.0.113.5")

        service
            .register("five", "password-5", "password-5", "203.0.113.5")
            .shouldBeInstanceOf<RegistrationService.Outcome.Created>()
      }

      test("forms that are never valid still run the address out of attempts") {
        val (_, service) = service(attemptsPerHour = 3)

        repeat(3) { service.register("", "", "", "203.0.113.5") }

        // Nothing above was an account, and the address is still done for the hour.
        service.register("valid", "password-9", "password-9", "203.0.113.5") shouldBe
            RegistrationService.Outcome.RateLimited
        service
            .register("valid", "password-9", "password-9", "198.51.100.7")
            .shouldBeInstanceOf<RegistrationService.Outcome.Created>()
      }

      test("the whole site only creates so many accounts an hour, whoever asks") {
        val (_, service) = service(total = 1)

        service
            .register("first", "password-1", "password-1", "203.0.113.5")
            .shouldBeInstanceOf<RegistrationService.Outcome.Created>()

        // A different address, well inside its own allowance, and the site has still had enough.
        service.register("second", "password-2", "password-2", "198.51.100.7") shouldBe
            RegistrationService.Outcome.RateLimited
      }
    })
