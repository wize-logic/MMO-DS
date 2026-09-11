package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.net.game.packets.PokemonContainerPacket
import de.fiereu.openmmo.net.game.packets.PokemonMove
import de.fiereu.openmmo.net.game.packets.PokemonMovePacket
import de.fiereu.openmmo.server.game.battle.BattleRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.Containers
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.storage.PC_STORAGE_SIZE
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import java.time.LocalDateTime
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

private fun mon(id: Long, owner: Long, container: PokemonContainer, slot: Short) =
    Pokemon(
        id = id,
        ownerId = owner,
        container = container,
        containerSlot = slot,
        dexId = id.toInt(),
        seed = 0,
        ot = "Ash",
        nickname = "",
        level = 5,
        hp = 20,
        xp = 100,
        eVs = EVs(),
        iVs = IVs(),
        moves = emptyList(),
        isShiny = false,
        hasHiddenAbility = false,
        isAlpha = false,
        isSecret = false,
        isFatefulEncounter = false,
        isRaidEncounter = false,
        caughtAt = LocalDateTime.of(2026, 1, 1, 0, 0),
    )

private class StorageFixture(scope: CoroutineScope) {
  val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), scope)
  val service = PokemonStorageService(store, BattleRegistry())

  suspend fun trainer(
      party: Int,
      pc: List<Short>,
      daycare: List<Short> = emptyList(),
  ): Pair<FakeSession, Long> {
    val created = store.createCharacter(1, "Ash", CharacterGender.MALE, Region.HOENN)
    val id = created.info.id
    store.rearrangeMonsters(id) { _, _, _ ->
      Containers(
          List(party) { mon(it + 1L, id, PokemonContainer.PARTY, it.toShort()) },
          pc.mapIndexed { i, slot -> mon(100L + i, id, PokemonContainer.PC, slot) },
          daycare.mapIndexed { i, slot -> mon(200L + i, id, PokemonContainer.DAYCARE, slot) })
    }
    val session = FakeSession(id)
    session.sent.clear()
    return session to id
  }

  fun move(session: FakeSession, vararg moves: PokemonMove) {
    service.onMove(PacketEvent(PokemonMovePacket(moves.toList()), session))
  }

  fun party(id: Long) = store.getCharacter(id)!!.pokemon

  fun pc(id: Long) = store.getCharacter(id)!!.pcStorage

  fun daycare(id: Long) = store.getCharacter(id)!!.daycare
}

@OptIn(ExperimentalCoroutinesApi::class)
class PokemonStorageServiceTest :
    FunSpec({
      test("a deposit leaves the party contiguous and reseats the monster in the PC") {
        runTest {
          val fx = StorageFixture(this)
          val (session, id) = fx.trainer(party = 3, pc = emptyList())
          fx.move(session, PokemonMove(PokemonContainer.PARTY, 0, PokemonContainer.PC, 4))

          fx.party(id).map { it.id } shouldBe listOf(2L, 3L)
          fx.party(id).map { it.containerSlot } shouldBe listOf<Short>(0, 1)
          fx.pc(id).map { it.id to it.containerSlot } shouldBe listOf(1L to 4.toShort())
          fx.pc(id).single().container shouldBe PokemonContainer.PC
        }
      }

      test("a withdrawal onto an occupied party slot swaps") {
        runTest {
          val fx = StorageFixture(this)
          val (session, id) = fx.trainer(party = 2, pc = listOf(7))
          fx.move(session, PokemonMove(PokemonContainer.PC, 7, PokemonContainer.PARTY, 1))

          fx.party(id).map { it.id } shouldBe listOf(1L, 100L)
          fx.pc(id).map { it.id to it.containerSlot } shouldBe listOf(2L to 7.toShort())
        }
      }

      test("a batch runs in order, so a swap through a free slot is two pairs") {
        runTest {
          val fx = StorageFixture(this)
          val (session, id) = fx.trainer(party = 3, pc = emptyList())
          fx.move(
              session,
              PokemonMove(PokemonContainer.PARTY, 0, PokemonContainer.PC, 0),
              PokemonMove(PokemonContainer.PARTY, 2, PokemonContainer.PARTY, 0),
          )
          fx.party(id).map { it.id } shouldBe listOf(3L, 2L)
          fx.pc(id).map { it.id } shouldBe listOf(1L)
        }
      }

      test("the last party member cannot be deposited") {
        runTest {
          val fx = StorageFixture(this)
          val (session, id) = fx.trainer(party = 1, pc = emptyList())
          fx.move(session, PokemonMove(PokemonContainer.PARTY, 0, PokemonContainer.PC, 0))

          fx.party(id).map { it.id } shouldBe listOf(1L)
          fx.pc(id) shouldBe emptyList()
        }
      }

      test("a container the storage does not own is refused whole") {
        runTest {
          val fx = StorageFixture(this)
          val (session, id) = fx.trainer(party = 2, pc = emptyList())
          fx.move(
              session,
              PokemonMove(PokemonContainer.PARTY, 0, PokemonContainer.PC, 0),
              PokemonMove(PokemonContainer.PARTY, 0, PokemonContainer.BATTLE_BOX_1, 0),
          )
          fx.party(id).map { it.id } shouldBe listOf(1L, 2L)
          fx.pc(id) shouldBe emptyList()
        }
      }

      test("a monster handed over the day care counter leaves the party for the day care") {
        runTest {
          val fx = StorageFixture(this)
          val (session, id) = fx.trainer(party = 3, pc = emptyList())
          fx.move(session, PokemonMove(PokemonContainer.PARTY, 2, PokemonContainer.DAYCARE, 0))

          fx.party(id).map { it.id } shouldBe listOf(1L, 2L)
          fx.daycare(id).map { it.id to it.containerSlot } shouldBe listOf(3L to 0.toShort())
          fx.daycare(id).single().container shouldBe PokemonContainer.DAYCARE
          fx.pc(id) shouldBe emptyList()
        }
      }

      test("a boarder taken back is the same monster, not a second one") {
        runTest {
          val fx = StorageFixture(this)
          val (session, id) = fx.trainer(party = 2, pc = emptyList(), daycare = listOf(0))
          val boarding = fx.daycare(id).single().id
          fx.move(session, PokemonMove(PokemonContainer.DAYCARE, 0, PokemonContainer.PARTY, 2))

          fx.daycare(id) shouldBe emptyList()
          fx.party(id).map { it.id } shouldBe listOf(1L, 2L, boarding)
          fx.party(id).last().container shouldBe PokemonContainer.PARTY
        }
      }

      test("the day care holds two, and a third is refused") {
        runTest {
          val fx = StorageFixture(this)
          val (session, id) = fx.trainer(party = 3, pc = emptyList(), daycare = listOf(0, 1))
          fx.move(session, PokemonMove(PokemonContainer.PARTY, 2, PokemonContainer.DAYCARE, 2))

          fx.party(id).map { it.id } shouldBe listOf(1L, 2L, 3L)
          fx.daycare(id).map { it.containerSlot } shouldBe listOf<Short>(0, 1)
        }
      }

      test("the day care keeps the slot it was given rather than being packed down") {
        runTest {
          val fx = StorageFixture(this)
          val (session, id) = fx.trainer(party = 2, pc = emptyList(), daycare = listOf(0, 1))
          val second = fx.daycare(id).last().id
          fx.move(session, PokemonMove(PokemonContainer.DAYCARE, 0, PokemonContainer.PARTY, 2))

          fx.daycare(id).map { it.id to it.containerSlot } shouldBe listOf(second to 1.toShort())
        }
      }

      test("the last party member may not be handed over the counter") {
        runTest {
          val fx = StorageFixture(this)
          val (session, id) = fx.trainer(party = 1, pc = emptyList())
          fx.move(session, PokemonMove(PokemonContainer.PARTY, 0, PokemonContainer.DAYCARE, 0))

          fx.party(id).map { it.id } shouldBe listOf(1L)
          fx.daycare(id) shouldBe emptyList()
        }
      }

      test("an out-of-range slot is refused") {
        runTest {
          val fx = StorageFixture(this)
          val (session, id) = fx.trainer(party = 2, pc = emptyList())
          fx.move(
              session,
              PokemonMove(
                  PokemonContainer.PARTY, 0, PokemonContainer.PC, PC_STORAGE_SIZE.toShort()))
          fx.party(id).map { it.id } shouldBe listOf(1L, 2L)
        }
      }

      test("every container comes back on the wire, refused or not") {
        runTest {
          val fx = StorageFixture(this)
          val (session, _) = fx.trainer(party = 2, pc = emptyList(), daycare = listOf(0))
          fx.move(session, PokemonMove(PokemonContainer.PARTY, 0, PokemonContainer.PC, 700))

          val containers = session.sent.filterIsInstance<PokemonContainerPacket>()
          containers.map { it.container } shouldBe
              listOf(PokemonContainer.PARTY, PokemonContainer.DAYCARE, PokemonContainer.PC)
          containers.all { it.hasChange } shouldBe true
          containers.first().pokemon.map { it.id } shouldBe listOf(1L, 2L)
          containers[1].pokemon.map { it.id } shouldBe listOf(200L)
        }
      }
    })
