package de.fiereu.openmmo.server.game.storage

import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

class SocialStoreTest :
    FunSpec({
      test("an account starts with nobody on either list") {
        val store = SocialStore(InMemorySocialRepository())
        store.load(1)
        store.getFriends(1) shouldBe emptySet()
        store.getBlocked(1) shouldBe emptySet()
      }

      test("friends survive add and remove") {
        val store = SocialStore(InMemorySocialRepository())
        store.addFriend(1, "Yellow")
        store.getFriends(1) shouldBe setOf("Yellow")

        store.removeFriend(1, "Yellow") shouldBe true
        store.getFriends(1) shouldBe emptySet()
        store.removeFriend(1, "Yellow") shouldBe false
      }

      test("block list is independent per user") {
        val store = SocialStore(InMemorySocialRepository())
        store.block(1, "Troll")
        store.getBlocked(1) shouldBe setOf("Troll")
        store.getBlocked(2) shouldBe emptySet()

        store.unblock(1, "Troll") shouldBe true
        store.getBlocked(1) shouldBe emptySet()
      }

      /**
       * What the maps could not answer for. Both lists used to live only in this object, so a
       * friend added on Monday was gone by Tuesday's restart, and every account was handed the same
       * three invented names on the way in to hide it.
       */
      test("a second store over the same table reads back both lists") {
        val table = InMemorySocialRepository()
        val before = SocialStore(table)
        before.addFriend(7, "Cynthia")
        before.addFriend(7, "Barry")
        before.block(7, "Troll")

        val after = SocialStore(table)
        after.load(7)
        after.getFriends(7) shouldBe setOf("Cynthia", "Barry")
        after.getBlocked(7) shouldBe setOf("Troll")
      }

      /** A read before the account has been loaded must not be mistaken for an empty list. */
      test("an unloaded account is not written back as empty") {
        val table = InMemorySocialRepository()
        SocialStore(table).addFriend(7, "Cynthia")

        val after = SocialStore(table)
        after.getFriends(7) shouldBe emptySet()
        after.addFriend(7, "Barry")
        after.getFriends(7) shouldBe setOf("Cynthia", "Barry")
      }
    })
