package de.fiereu.openmmo.server.game.storage

import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

class LinkStoreTest :
    FunSpec({
      test("creating a link binds both members and names the leader") {
        val store = LinkStore()
        store.get(100L) shouldBe null

        val link = store.create(100L, "Ash", 200L, "Gary")
        link.leaderId shouldBe 100L
        link.members.map { it.name } shouldBe listOf("Ash", "Gary")
        store.get(100L) shouldBe link
        store.get(200L) shouldBe link
      }

      test("a third member appends until the cap") {
        val store = LinkStore()
        val link = store.create(100L, "Ash", 200L, "Gary")
        store.add(link, LinkMember(300L, "Oak"))
        link.members.map { it.id } shouldBe listOf(100L, 200L, 300L)
        store.get(300L) shouldBe link
        link.members.size shouldBe 3
      }

      test("remove unbinds and setLeader only accepts a member") {
        val store = LinkStore()
        val link = store.create(100L, "Ash", 200L, "Gary")
        store.add(link, LinkMember(300L, "Oak"))

        store.setLeader(link, 200L)
        link.leaderId shouldBe 200L
        store.setLeader(link, 999L)
        link.leaderId shouldBe 200L

        store.remove(link, 300L)
        link.members.map { it.id } shouldBe listOf(100L, 200L)
        store.get(300L) shouldBe null
      }

      test("disband unbinds every member") {
        val store = LinkStore()
        val link = store.create(100L, "Ash", 200L, "Gary")
        store.disband(link)
        store.get(100L) shouldBe null
        store.get(200L) shouldBe null
        link.members shouldBe emptyList()
      }
    })
