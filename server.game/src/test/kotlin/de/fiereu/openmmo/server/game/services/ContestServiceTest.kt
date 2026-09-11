package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.net.game.packets.ContestCommPacket
import de.fiereu.openmmo.server.game.session.CLIENT_RUNS_SCRIPTS
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import java.time.LocalDateTime
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

private const val POUND: Short = 1

private fun contestMon(ownerId: Long): Pokemon =
    Pokemon(
        id = EntityIdService().newMonsterId(),
        ownerId = ownerId,
        container = PokemonContainer.PARTY,
        containerSlot = 0,
        dexId = 25,
        seed = 0,
        ot = "Ash",
        nickname = "",
        level = 30,
        hp = 100,
        xp = 0,
        eVs = EVs(),
        iVs = IVs(),
        moves = listOf(PokemonMove(POUND, 35)),
        isShiny = false,
        hasHiddenAbility = false,
        isAlpha = false,
        isSecret = false,
        isFatefulEncounter = false,
        isRaidEncounter = false,
        caughtAt = LocalDateTime.now(),
    )

private data class Seated(val session: FakeSession, val id: Long)

private class ContestFixture(scope: CoroutineScope) {
  val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), scope)
  val sessions = SessionRegistry()
  val credits = ContestRibbonCredits()
  val contests = ContestService(sessions, store, credits)

  suspend fun player(name: String, userId: Int): Seated {
    val created = store.createCharacter(userId, name, CharacterGender.MALE, Region.SINNOH)
    store.addPokemon(created.info.id, contestMon(created.info.id))
    val session = FakeSession(created.info.id)
    session.attributes[CLIENT_RUNS_SCRIPTS] = true
    sessions.bindCharacter(session, created.info.id)
    return Seated(session, created.info.id)
  }

  fun comm(who: Seated, kind: Int, sessionId: Int = 0, payload: ByteArray = ByteArray(0)) =
      contests.onContestComm(
          PacketEvent(ContestCommPacket(kind, sessionId, -1, payload), who.session))

  /**
   * A full contest, which the lobby starts the moment it is full: a short group waits out a real
   * twenty-second window before it starts, and four never waits at all.
   */
  suspend fun startFull(): List<Seated> {
    val group = (1..ContestCommPacket.CONTEST_PARTICIPANTS).map { player("P$it", it) }
    for (p in group) comm(p, ContestCommPacket.KIND_QUEUE, payload = byteArrayOf(0, 0, 0))
    return group
  }
}

private fun FakeSession.seatFrame(): ContestCommPacket =
    sent.filterIsInstance<ContestCommPacket>().single { it.kind == ContestCommPacket.KIND_SEAT }

@OptIn(ExperimentalCoroutinesApi::class)
class ContestServiceTest :
    FunSpec({
      test("a seat that walks out without reporting settles the contest for the rest") {
        runTest {
          val fx = ContestFixture(backgroundScope)
          val group = fx.startFull()
          val contestId = group[0].session.seatFrame().sessionId
          val placements = byteArrayOf(0, 1, 2, 3)

          for (p in group.dropLast(1)) {
            fx.comm(p, ContestCommPacket.KIND_RESULT, contestId, placements)
          }
          // Three of four have said the same thing and the fourth never will. Nothing settled on
          // its own: the last report is what used to look, and it is not coming.
          for (p in group) fx.credits.held(p.id) shouldBe 0

          fx.comm(group.last(), ContestCommPacket.KIND_LEAVE, contestId)

          for (p in group.dropLast(1)) fx.credits.held(p.id) shouldBe 1
          fx.credits.held(group.last().id) shouldBe 0
          // And the three are out of the seat they held, so they can enter another contest.
          fx.comm(group[0], ContestCommPacket.KIND_QUEUE, payload = byteArrayOf(0, 0, 0))
          group[0].session.sent.filterIsInstance<ContestCommPacket>().none {
            it.kind == ContestCommPacket.KIND_CANCEL
          } shouldBe true
        }
      }

      test("a seat that reported before walking out is still paid for it") {
        runTest {
          val fx = ContestFixture(backgroundScope)
          val group = fx.startFull()
          val contestId = group[0].session.seatFrame().sessionId
          val placements = byteArrayOf(3, 2, 1, 0)

          // The client sends its placements and then closes the pipe, so a seat's result and its
          // leave arrive together and which lands first is a race.
          fx.comm(group[0], ContestCommPacket.KIND_RESULT, contestId, placements)
          fx.comm(group[0], ContestCommPacket.KIND_LEAVE, contestId)
          for (p in group.drop(1)) {
            fx.comm(p, ContestCommPacket.KIND_RESULT, contestId, placements)
          }

          for (p in group) fx.credits.held(p.id) shouldBe 1
        }
      }

      test("everybody walking out without reporting records nothing") {
        runTest {
          val fx = ContestFixture(backgroundScope)
          val group = fx.startFull()
          val contestId = group[0].session.seatFrame().sessionId

          for (p in group) fx.comm(p, ContestCommPacket.KIND_LEAVE, contestId)

          for (p in group) fx.credits.held(p.id) shouldBe 0
        }
      }

      test("a disagreement is refused however the last seat leaves") {
        runTest {
          val fx = ContestFixture(backgroundScope)
          val group = fx.startFull()
          val contestId = group[0].session.seatFrame().sessionId

          fx.comm(group[0], ContestCommPacket.KIND_RESULT, contestId, byteArrayOf(0, 1, 2, 3))
          fx.comm(group[1], ContestCommPacket.KIND_RESULT, contestId, byteArrayOf(1, 0, 2, 3))
          fx.comm(group[2], ContestCommPacket.KIND_LEAVE, contestId)
          fx.comm(group[3], ContestCommPacket.KIND_LEAVE, contestId)

          for (p in group) fx.credits.held(p.id) shouldBe 0
        }
      }
    })
