package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.net.game.packets.ChatMessagePacket
import de.fiereu.openmmo.net.game.packets.InGameChallengeResponsePacket
import de.fiereu.openmmo.net.game.packets.LinkBattleDataPacket
import de.fiereu.openmmo.net.game.packets.LinkBattleOpenPacket
import de.fiereu.openmmo.server.game.session.CLIENT_RUNS_SCRIPTS
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import de.fiereu.openmmo.server.game.testsupport.battleService
import de.fiereu.openmmo.server.game.world.interest.InterestManager
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldHaveSize
import io.kotest.matchers.shouldBe
import io.kotest.matchers.string.shouldContain
import java.time.LocalDateTime
import java.util.concurrent.CyclicBarrier
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

private const val TACKLE: Short = 33

private fun duelMon(ownerId: Long): Pokemon =
    Pokemon(
        id = EntityIdService().newMonsterId(),
        ownerId = ownerId,
        container = PokemonContainer.PARTY,
        containerSlot = 0,
        dexId = 1,
        seed = 0,
        ot = "Ash",
        nickname = "",
        level = 50,
        hp = 999,
        xp = 0,
        eVs = EVs(),
        iVs = IVs(),
        moves = listOf(PokemonMove(TACKLE, 35)),
        isShiny = false,
        hasHiddenAbility = false,
        isAlpha = false,
        isSecret = false,
        isFatefulEncounter = false,
        isRaidEncounter = false,
        caughtAt = LocalDateTime.now(),
    )

private class DuelFixture(scope: CoroutineScope) {
  val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), scope)
  val sessions = SessionRegistry()
  val battles = battleService(store, InterestManager(), MapManager())
  val duels = DuelService(sessions, store, battles)

  /** A seated player whose client runs the engine, so a duel between two is a link battle. */
  suspend fun seated(name: String, userId: Int, runsScripts: Boolean = true): Player {
    val created = store.createCharacter(userId, name, CharacterGender.MALE, Region.HOENN)
    store.addPokemon(created.info.id, duelMon(created.info.id))
    val session = FakeSession(created.info.id)
    session.attributes[CLIENT_RUNS_SCRIPTS] = runsScripts
    sessions.bindCharacter(session, created.info.id)
    return Player(session, created.info.id)
  }

  fun answer(player: Player, accepted: Boolean) =
      duels.onResponse(PacketEvent(InGameChallengeResponsePacket(accepted, ""), player.session))

  fun report(player: Player, battleId: Int, result: Int) =
      duels.onLinkData(
          PacketEvent(
              LinkBattleDataPacket(
                  battleId, LinkBattleDataPacket.KIND_RESULT, byteArrayOf(result.toByte())),
              player.session))
}

private data class Player(val session: FakeSession, val id: Long)

private fun FakeSession.replies() = sent.filterIsInstance<ChatMessagePacket>().map { it.message }

private fun FakeSession.opened() = sent.filterIsInstance<LinkBattleOpenPacket>()

@OptIn(ExperimentalCoroutinesApi::class)
class DuelServiceTest :
    FunSpec({
      test("a standing challenge accepted mid-battle does not orphan the running one") {
        runTest {
          val fx = DuelFixture(backgroundScope)
          val ash = fx.seated("Ash", 1)
          val misty = fx.seated("Misty", 2)
          val brock = fx.seated("Brock", 3)

          // Brock's challenge to Ash is offered while Ash is free, and stands.
          fx.duels.challenge(brock.session, ash.session)
          // Ash starts a link battle with Misty before answering it.
          fx.duels.challenge(ash.session, misty.session)
          fx.answer(misty, accepted = true)
          fx.duels.inLinkBattle(ash.id) shouldBe true
          fx.duels.inLinkBattle(misty.id) shouldBe true

          // Answering the standing challenge must not move Ash into a second battle: that would
          // leave Misty holding one with nobody on the other side of it.
          fx.answer(ash, accepted = true)

          fx.duels.inLinkBattle(brock.id) shouldBe false
          brock.session.opened().shouldHaveSize(0)
          ash.session.opened().shouldHaveSize(1)
          misty.session.opened().shouldHaveSize(1)

          // Misty's battle is still Misty's, and ending it lets her out again.
          val battleId = misty.session.opened().single().battleId
          fx.report(ash, battleId, 1)
          fx.report(misty, battleId, 2)
          fx.duels.inLinkBattle(misty.id) shouldBe false
          fx.duels.inLinkBattle(ash.id) shouldBe false
        }
      }

      test("seating a player who is already in a link battle is refused") {
        runTest {
          val fx = DuelFixture(backgroundScope)
          val ash = fx.seated("Ash", 1)
          val misty = fx.seated("Misty", 2)
          val brock = fx.seated("Brock", 3)

          fx.duels.seat(ash.session, ash.id, misty.session, misty.id) shouldBe true
          fx.duels.seat(brock.session, brock.id, ash.session, ash.id) shouldBe false

          fx.duels.inLinkBattle(brock.id) shouldBe false
          brock.session.replies().last() shouldContain "already in a battle"
          // The first battle is untouched: one open packet each, and the pair still holds it.
          ash.session.opened().shouldHaveSize(1)
          misty.session.opened().shouldHaveSize(1)
          fx.duels.inLinkBattle(ash.id) shouldBe true
          fx.duels.inLinkBattle(misty.id) shouldBe true
        }
      }

      test("seating a player who is already in a server-side battle is refused") {
        runTest {
          val fx = DuelFixture(backgroundScope)
          val ash = fx.seated("Ash", 1, runsScripts = false)
          val misty = fx.seated("Misty", 2, runsScripts = false)
          val brock = fx.seated("Brock", 3)

          // Two clients that do not run the engine fall back to the server's own turn engine.
          fx.duels.seat(ash.session, ash.id, misty.session, misty.id) shouldBe true
          fx.battles.inBattle(ash.id) shouldBe true

          fx.duels.seat(brock.session, brock.id, ash.session, ash.id) shouldBe false
          fx.duels.inLinkBattle(brock.id) shouldBe false
          brock.session.replies().last() shouldContain "already in a battle"
        }
      }

      test("two results arriving at once end the battle once") {
        runTest {
          repeat(200) {
            val fx = DuelFixture(backgroundScope)
            val ash = fx.seated("Ash", 1)
            val misty = fx.seated("Misty", 2)
            fx.duels.seat(ash.session, ash.id, misty.session, misty.id) shouldBe true
            val battleId = ash.session.opened().single().battleId

            // The two reports arrive on different event-loop threads, which is what lets a
            // check-then-act on the pair's completeness run twice.
            val barrier = CyclicBarrier(2)
            val threads =
                listOf(ash to 1, misty to 2).map { (player, result) ->
                  Thread {
                    barrier.await()
                    fx.report(player, battleId, result)
                  }
                }
            threads.forEach { it.start() }
            threads.forEach { it.join() }

            ash.session.replies().count { it.contains("You won") } shouldBe 1
            misty.session.replies().count { it.contains("You lost") } shouldBe 1
          }
        }
      }
    })
