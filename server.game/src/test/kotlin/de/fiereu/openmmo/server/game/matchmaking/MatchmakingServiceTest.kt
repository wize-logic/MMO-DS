package de.fiereu.openmmo.server.game.matchmaking

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.common.pvp.Clause
import de.fiereu.openmmo.common.pvp.MatchmakingQueue
import de.fiereu.openmmo.common.pvp.SignupOutcome
import de.fiereu.openmmo.net.game.packets.ChatMessagePacket
import de.fiereu.openmmo.net.game.packets.LeaveMatchmakingQueuePacket
import de.fiereu.openmmo.net.game.packets.MatchmakingLanguagePrefsPacket
import de.fiereu.openmmo.net.game.packets.matchmaking.MatchmakingSignupClearedPacket
import de.fiereu.openmmo.net.game.packets.matchmaking.MatchmakingSignupPacket
import de.fiereu.openmmo.net.game.packets.matchmaking.MatchmakingSignupResultPacket
import de.fiereu.openmmo.net.game.packets.matchmaking.QueueSignup
import de.fiereu.openmmo.net.game.packets.matchmaking.QueueSlotSelection
import de.fiereu.openmmo.net.game.packets.matchmaking.TournamentSignup
import de.fiereu.openmmo.pokemon.EvolutionRegistry
import de.fiereu.openmmo.server.game.services.DuelService
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import de.fiereu.openmmo.server.game.testsupport.battleService
import de.fiereu.openmmo.server.game.world.interest.InterestManager
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldContain
import io.kotest.matchers.collections.shouldContainExactly
import io.kotest.matchers.shouldBe
import java.time.LocalDateTime
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.test.runTest

private val SIX = listOf(1, 4, 7, 25, 133, 143)

private fun mon(ownerId: Long, dexId: Int, level: Byte = 50, ot: String = "Red") =
    Pokemon(
        id = 0L,
        ownerId = ownerId,
        container = PokemonContainer.PARTY,
        containerSlot = 0,
        dexId = dexId,
        seed = 0,
        ot = ot,
        nickname = "",
        level = level,
        hp = 100,
        xp = 0,
        eVs = EVs(),
        iVs = IVs(),
        moves = listOf(PokemonMove(33, 35)),
        isShiny = false,
        hasHiddenAbility = false,
        isAlpha = false,
        isSecret = false,
        isFatefulEncounter = false,
        isRaidEncounter = false,
        caughtAt = LocalDateTime.now(),
    )

private class Fixture(scope: CoroutineScope) {
  val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), scope)
  val sessions = SessionRegistry()
  val duels = DuelService(sessions, store, battleService(store, InterestManager()))
  val service =
      MatchmakingService(store, TeamValidator(TierRegistry(), EvolutionRegistry()), sessions, duels)
  private var users = 0

  suspend fun seated(
      party: List<Int>,
      level: Byte = 50,
      name: String = "Red",
  ): Pair<FakeSession, Long> {
    val created = store.createCharacter(++users, name, CharacterGender.MALE, Region.SINNOH)
    party.forEach { store.addPokemon(created.info.id, mon(created.info.id, it, level)) }
    val session = FakeSession(created.info.id)
    sessions.bindCharacter(session, created.info.id)
    return session to created.info.id
  }

  fun signUp(session: FakeSession, vararg queues: MatchmakingQueue) =
      service.onSignup(
          PacketEvent(
              MatchmakingSignupPacket(QueueSignup(queues.map { QueueSlotSelection(it.id, 0) })),
              session))

  fun result(session: FakeSession) =
      session.sent.filterIsInstance<MatchmakingSignupResultPacket>().last()
}

class MatchmakingServiceTest :
    FunSpec({
      test("a legal party is signed up and told which queues it entered") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (session, charId) = fx.seated(SIX)
          fx.signUp(session, MatchmakingQueue.OVER_USED)
          val answer = fx.result(session)
          answer.outcome shouldBe SignupOutcome.ACCEPTED.id
          answer.queues shouldContainExactly listOf(MatchmakingQueue.OVER_USED.id)
          fx.service.signupOf(charId) shouldBe setOf(MatchmakingQueue.OVER_USED)
        }
      }

      test("signing up twice is refused and does not disturb the standing one") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (session, charId) = fx.seated(SIX)
          fx.signUp(session, MatchmakingQueue.OVER_USED)
          fx.signUp(session, MatchmakingQueue.NEVER_USED)
          fx.result(session).outcome shouldBe SignupOutcome.ALREADY_REGISTERED.id
          fx.service.signupOf(charId) shouldBe setOf(MatchmakingQueue.OVER_USED)
        }
      }

      test("a short party is refused with the clause and its number, and nothing stands") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (session, charId) = fx.seated(SIX.take(3))
          fx.signUp(session, MatchmakingQueue.OVER_USED)
          val answer = fx.result(session)
          answer.outcome shouldBe SignupOutcome.CLAUSE_VIOLATED.id
          answer.clause shouldBe Clause.EXACT_PARTY_SIZE.id
          answer.value shouldBe 6
          fx.service.signupOf(charId) shouldBe emptySet()
        }
      }

      test("an under-levelled party names the level rather than a clause") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (session, _) = fx.seated(SIX, level = 40)
          fx.signUp(session, MatchmakingQueue.OVER_USED)
          val answer = fx.result(session)
          answer.outcome shouldBe SignupOutcome.INVALID_LEVEL.id
          answer.value shouldBe 50
          answer.clause shouldBe null
        }
      }

      test("a signup is all or nothing across the queues it names") {
        runTest {
          val fx = Fixture(backgroundScope)
          // Mewtwo passes an unlimited queue and fails a tiered one; every queue this server
          // opens is tiered, so a party carrying it enters none of them.
          val (session, charId) = fx.seated(listOf(150, 4, 7, 25, 133, 143))
          fx.signUp(session, MatchmakingQueue.OVER_USED, MatchmakingQueue.NEVER_USED)
          fx.result(session).outcome shouldBe SignupOutcome.TIERING_OR_BAN.id
          fx.service.signupOf(charId) shouldBe emptySet()
        }
      }

      test("withdrawing clears the signup and says so") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (session, charId) = fx.seated(SIX)
          fx.signUp(session, MatchmakingQueue.OVER_USED)
          fx.service.onWithdraw(PacketEvent(LeaveMatchmakingQueuePacket(), session))
          session.sent.filterIsInstance<MatchmakingSignupClearedPacket>().size shouldBe 1
          fx.service.signupOf(charId) shouldBe emptySet()
        }
      }

      test("an empty selection is treated as a withdrawal, though no client sends one") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (session, charId) = fx.seated(SIX)
          fx.signUp(session, MatchmakingQueue.OVER_USED)
          fx.signUp(session)
          session.sent.filterIsInstance<MatchmakingSignupClearedPacket>().size shouldBe 1
          fx.service.signupOf(charId) shouldBe emptySet()
        }
      }

      test("a tournament seat is refused, because nothing here runs one") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (session, _) = fx.seated(SIX)
          fx.service.onSignup(
              PacketEvent(MatchmakingSignupPacket(TournamentSignup(7L, 0)), session))
          fx.result(session).outcome shouldBe SignupOutcome.TOURNAMENT_INVALID.id
        }
      }

      test("a queue this server does not open is refused rather than accepted quietly") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (session, _) = fx.seated(SIX)
          fx.signUp(session, MatchmakingQueue.RANDOMS)
          fx.result(session).outcome shouldBe SignupOutcome.UNKNOWN.id
        }
      }

      test("a disconnect takes the signup with it") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (session, charId) = fx.seated(SIX)
          fx.signUp(session, MatchmakingQueue.OVER_USED)
          fx.service.onDisconnect(session)
          fx.service.signupOf(charId) shouldBe emptySet()
        }
      }

      test("a round pairs two and takes them both out of the queue") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (red, redId) = fx.seated(SIX, name = "Red")
          val (blue, blueId) = fx.seated(SIX, name = "Blue")
          fx.signUp(red, MatchmakingQueue.OVER_USED)
          fx.signUp(blue, MatchmakingQueue.OVER_USED)
          fx.service.waitingIn(MatchmakingQueue.OVER_USED) shouldContainExactly
              listOf(redId, blueId)

          fx.service.runRound(MatchmakingQueue.OVER_USED) shouldBe 1
          fx.service.waitingIn(MatchmakingQueue.OVER_USED) shouldBe emptyList()
          fx.service.signupOf(redId) shouldBe emptySet()
          fx.service.signupOf(blueId) shouldBe emptySet()
        }
      }

      test("a round pairs in the order they signed up") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (a, aId) = fx.seated(SIX, name = "A")
          val (b, bId) = fx.seated(SIX, name = "B")
          val (c, cId) = fx.seated(SIX, name = "C")
          fx.signUp(a, MatchmakingQueue.OVER_USED)
          fx.signUp(b, MatchmakingQueue.OVER_USED)
          fx.signUp(c, MatchmakingQueue.OVER_USED)

          fx.service.runRound(MatchmakingQueue.OVER_USED) shouldBe 1
          // A met B because they were first; C keeps its place for the next round.
          fx.service.signupOf(aId) shouldBe emptySet()
          fx.service.signupOf(bId) shouldBe emptySet()
          fx.service.waitingIn(MatchmakingQueue.OVER_USED) shouldContainExactly listOf(cId)
        }
      }

      test("the one a round cannot place is told, and keeps its place") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (red, redId) = fx.seated(SIX)
          fx.signUp(red, MatchmakingQueue.OVER_USED)
          fx.service.runRound(MatchmakingQueue.OVER_USED) shouldBe 0
          red.sent.filterIsInstance<ChatMessagePacket>().map { it.message } shouldContain
              "No suitable match could be found for this round."
          fx.service.signupOf(redId) shouldBe setOf(MatchmakingQueue.OVER_USED)
        }
      }

      test("being paired in one queue ends the signup in all of them") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (a, aId) = fx.seated(SIX, name = "A")
          val (b, bId) = fx.seated(SIX, name = "B")
          fx.signUp(a, MatchmakingQueue.OVER_USED, MatchmakingQueue.NEVER_USED)
          fx.signUp(b, MatchmakingQueue.OVER_USED, MatchmakingQueue.NEVER_USED)

          fx.service.runRound(MatchmakingQueue.OVER_USED) shouldBe 1
          fx.service.waitingIn(MatchmakingQueue.NEVER_USED) shouldBe emptyList()
          fx.service.signupOf(aId) shouldBe emptySet()
          fx.service.signupOf(bId) shouldBe emptySet()
        }
      }

      test("somebody who went offline is not paired, and does not take a partner with them") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (gone, goneId) = fx.seated(SIX, name = "Gone")
          val (here, hereId) = fx.seated(SIX, name = "Here")
          fx.signUp(gone, MatchmakingQueue.OVER_USED)
          fx.signUp(here, MatchmakingQueue.OVER_USED)
          fx.sessions.unbindCharacter(goneId, gone)

          fx.service.runRound(MatchmakingQueue.OVER_USED) shouldBe 0
          fx.service.signupOf(hereId) shouldBe setOf(MatchmakingQueue.OVER_USED)
        }
      }

      test("a round of every queue is one call") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (a, _) = fx.seated(SIX, name = "A")
          val (b, _) = fx.seated(SIX, name = "B")
          val (c, _) = fx.seated(SIX, name = "C")
          val (d, _) = fx.seated(SIX, name = "D")
          fx.signUp(a, MatchmakingQueue.OVER_USED)
          fx.signUp(b, MatchmakingQueue.OVER_USED)
          fx.signUp(c, MatchmakingQueue.NEVER_USED)
          fx.signUp(d, MatchmakingQueue.NEVER_USED)
          fx.service.runAllRounds() shouldBe 2
        }
      }

      test("the window names every open queue and only those") {
        runTest {
          val fx = Fixture(backgroundScope)
          fx.service.window().queues.map { it.queueId } shouldContainExactly
              QueueRules.ALL.map { it.queue.id }
          fx.service.window().queues.all { it.enabled } shouldBe true
        }
      }

      test("a language preference is kept, though nothing pairs by it yet") {
        runTest {
          val fx = Fixture(backgroundScope)
          val (session, charId) = fx.seated(SIX)
          fx.service.onLanguagePrefs(
              PacketEvent(MatchmakingLanguagePrefsPacket(listOf(0, 3)), session))
          fx.service.languagesOf(charId) shouldContainExactly listOf<Byte>(0, 3)
        }
      }
    })
