package de.fiereu.openmmo.server.game.session

import de.fiereu.openmmo.server.game.testsupport.FakeSession
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

class SessionRegistryTest :
    FunSpec({
      test("an account is held by one session, and joining again takes it over") {
        val registry = SessionRegistry()
        val first = FakeSession(userId = 7)
        val second = FakeSession(userId = 7)

        registry.claimUser(7, first) shouldBe null
        registry.claimUser(7, second) shouldBe first
        registry.sessionForUser(7) shouldBe second
      }

      test("the session that was taken over does not release the account on its way out") {
        val registry = SessionRegistry()
        val first = FakeSession(userId = 7)
        val second = FakeSession(userId = 7)
        registry.claimUser(7, first)
        registry.claimUser(7, second)

        // The displaced session is cleaned up after the new one has already claimed the account.
        registry.unregister(first)

        registry.sessionForUser(7) shouldBe second
      }

      test("a session leaving gives its account and character back") {
        val registry = SessionRegistry()
        val only = FakeSession(characterId = 42, userId = 7)
        registry.register(only)
        registry.claimUser(7, only)
        registry.bindCharacter(only, 42)

        registry.unregister(only)

        registry.sessionForUser(7) shouldBe null
        registry.getByCharacterId(42) shouldBe null
      }
    })
