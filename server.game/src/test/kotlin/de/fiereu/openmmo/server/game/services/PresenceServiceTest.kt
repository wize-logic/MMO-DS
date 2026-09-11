package de.fiereu.openmmo.server.game.services

import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.net.game.packets.EntityLeavePacket
import de.fiereu.openmmo.net.game.packets.EntityMovePacket
import de.fiereu.openmmo.net.game.packets.LoadEntityPacket
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import de.fiereu.openmmo.server.game.world.interest.InterestManager
import de.fiereu.openmmo.server.game.world.interest.PassThroughInterestPolicy
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import java.util.concurrent.CountDownLatch
import java.util.concurrent.CyclicBarrier
import kotlin.concurrent.thread
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob

class PresenceServiceTest :
    FunSpec({
      val mapManager = MapManager()

      fun freshPresence(): Pair<PresenceService, CharacterStore> {
        val store =
            CharacterStore(
                FakeCharacterRepository(),
                EntityIdService(),
                CoroutineScope(SupervisorJob() + Dispatchers.Unconfined),
            )
        val presence =
            PresenceService(
                InterestManager(),
                PassThroughInterestPolicy(),
                MapLoadService(mapManager, SpeciesRegistry()),
                store)
        return presence to store
      }

      suspend fun CharacterStore.session(
          name: String,
          mapId: Int = 3,
          gender: CharacterGender = CharacterGender.MALE,
      ): FakeSession {
        val character = createCharacter(1, name, gender, Region.HOENN)
        return FakeSession(characterId = character.info.id, mapId = mapId)
      }

      fun FakeSession.spawnedEntityIds(): List<Long> =
          sent.filterIsInstance<LoadEntityPacket>().map { it.entityId }

      fun FakeSession.id(): Long = state().characterId!!

      // The client picks the body it draws a peer from out of the spawn packet, so a
      // spawn that leaves the field at its default renders every player male.
      test("a spawn tells observers which body to draw the player with") {
        val (presence, store) = freshPresence()
        val a = store.session("A")
        val b = store.session("B", gender = CharacterGender.FEMALE)

        presence.enter(a)
        presence.enter(b)

        a.sent.filterIsInstance<LoadEntityPacket>().single().gender shouldBe
            CharacterGender.FEMALE.wireValue
        b.sent.filterIsInstance<LoadEntityPacket>().single().gender shouldBe
            CharacterGender.MALE.wireValue
      }

      test("entering exchanges entity snapshots with co-located observers") {
        val (presence, store) = freshPresence()
        val a = store.session("A")
        val b = store.session("B")

        presence.enter(a)
        a.sent shouldBe emptyList() // nobody else here yet

        presence.enter(b)
        a.spawnedEntityIds() shouldBe listOf(b.id())
        b.spawnedEntityIds() shouldBe listOf(a.id())
      }

      // pair joins both sessions in the same instant. The sequential test above cannot see the
      // old "read observers, then join" order: the second enter always found the first already
      // in the bucket.
      test("simultaneous enters still exchange snapshots") {
        repeat(32) { n ->
          val (presence, store) = freshPresence()
          val a = store.session("A$n")
          val b = store.session("B$n")
          val go = CyclicBarrier(2)
          val done = CountDownLatch(2)
          thread {
            go.await()
            presence.enter(a)
            done.countDown()
          }
          thread {
            go.await()
            presence.enter(b)
            done.countDown()
          }
          done.await()
          // Concurrent enters each push and pull, so a peer can land twice.
          a.spawnedEntityIds().toSet() shouldBe setOf(b.id())
          b.spawnedEntityIds().toSet() shouldBe setOf(a.id())
        }
      }

      test("movement propagates only to observers on the same map") {
        val (presence, store) = freshPresence()
        val a = store.session("A", mapId = 3)
        val b = store.session("B", mapId = 3)
        val c = store.session("C", mapId = 9)
        presence.enter(a)
        presence.enter(b)
        presence.enter(c)
        listOf(a, b, c).forEach { it.sent.clear() }

        val move = EntityMovePacket(entityId = a.id(), x = 1, y = 1, direction = Direction.DOWN)
        presence.broadcastToObservers(a, move)

        b.sent shouldBe listOf(move)
        c.sent shouldBe emptyList()
        a.sent shouldBe emptyList()
      }

      test("leaving despawns the player from its observers") {
        val (presence, store) = freshPresence()
        val a = store.session("A")
        val b = store.session("B")
        presence.enter(a)
        presence.enter(b)
        listOf(a, b).forEach { it.sent.clear() }

        presence.leave(b)

        a.sent shouldBe listOf(EntityLeavePacket(b.id()))

        val move = EntityMovePacket(entityId = a.id(), x = 1, y = 1, direction = Direction.DOWN)
        presence.broadcastToObservers(a, move)
        b.sent shouldBe emptyList() // no longer observing
      }

      test("refresh relocates the player between maps") {
        val (presence, store) = freshPresence()
        val a = store.session("A", mapId = 3)
        val d = store.session("D", mapId = 4)
        val b = store.session("B", mapId = 3)
        presence.enter(a)
        presence.enter(d)
        presence.enter(b)
        listOf(a, b, d).forEach { it.sent.clear() }

        b.state().mapId = 4
        presence.refresh(b)

        a.sent shouldBe listOf(EntityLeavePacket(b.id())) // despawned from the old map
        d.spawnedEntityIds() shouldBe listOf(b.id()) // spawned to the new map
        b.spawnedEntityIds() shouldBe listOf(d.id())
      }
    })
