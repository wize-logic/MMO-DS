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
import de.fiereu.openmmo.net.game.packets.DuelInvitePacket
import de.fiereu.openmmo.net.game.packets.MailComposeSendPacket
import de.fiereu.openmmo.net.game.packets.MailResultPacket
import de.fiereu.openmmo.net.game.packets.PokemonMailAttachment
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.storage.InMemoryMailRepository
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import de.fiereu.openmmo.server.game.testsupport.battleService
import de.fiereu.openmmo.server.game.world.interest.InterestManager
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldBeEmpty
import io.kotest.matchers.collections.shouldHaveSize
import io.kotest.matchers.shouldBe
import io.kotest.matchers.string.shouldContain
import java.time.LocalDateTime
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

private const val TACKLE: Short = 33

private fun fenceMon(ownerId: Long, dexId: Int, offlineOrigin: Boolean): Pokemon =
    Pokemon(
        id = EntityIdService().newMonsterId(),
        ownerId = ownerId,
        container = PokemonContainer.PARTY,
        containerSlot = 0,
        dexId = dexId,
        seed = 0,
        ot = "Ash",
        nickname = "",
        level = 50,
        hp = 100,
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
        offlineOrigin = offlineOrigin,
    )

private class FenceFixture(scope: CoroutineScope) {
  val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), scope)
  val sessions = SessionRegistry()
  val duels = DuelService(sessions, store, battleService(store, InterestManager(), MapManager()))
  val mail = MailService(InMemoryMailRepository(), store, sessions)

  suspend fun seated(
      name: String,
      userId: Int,
      dexId: Int,
      offlineOrigin: Boolean = false,
  ): Pair<FakeSession, Long> {
    val created = store.createCharacter(userId, name, CharacterGender.MALE, Region.HOENN)
    store.addPokemon(created.info.id, fenceMon(created.info.id, dexId, offlineOrigin))
    val session = FakeSession(created.info.id)
    sessions.bindCharacter(session, created.info.id)
    return session to created.info.id
  }
}

private fun FakeSession.lines() = sent.filterIsInstance<ChatMessagePacket>().map { it.message }

/**
 * The two doors with nowhere else to be tested: the duel, which tells rather than refuses, and the
 * letter, which is shut by a wider rule and has to stay shut when that rule opens.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class OfflineOriginFenceTest :
    FunSpec({
      test("a challenge tells each player about the other's offline monsters") {
        runTest {
          val fx = FenceFixture(backgroundScope)
          val (red, _) = fx.seated("Red", 1, 1, offlineOrigin = true)
          val (blue, _) = fx.seated("Blue", 2, 4)
          fx.duels.challenge(red, blue)

          // The challenged player hears about the challenger's team, and hears it before the box.
          val notice = blue.lines().first()
          notice shouldContain "Red's team"
          notice shouldContain "offline save"
          blue.sent.indexOfFirst { it is ChatMessagePacket } shouldBe
              blue.sent.indexOfFirst { it is DuelInvitePacket } - 1
          // Nothing is refused: a friendly battle moves nothing between the two parties.
          blue.sent.filterIsInstance<DuelInvitePacket>().shouldHaveSize(1)
          // Blue's own team is clean, so Red is told nothing about it.
          red.lines().none { it.contains("offline save") } shouldBe true
        }
      }

      test("the challenger is told about the other party too") {
        runTest {
          val fx = FenceFixture(backgroundScope)
          val (red, _) = fx.seated("Red", 1, 1)
          val (blue, _) = fx.seated("Blue", 2, 4, offlineOrigin = true)
          fx.duels.challenge(red, blue)
          red.lines().first() shouldContain "Blue's team"
          blue.lines().none { it.contains("offline save") } shouldBe true
        }
      }

      test("a clean pair of parties hears nothing about offline saves") {
        runTest {
          val fx = FenceFixture(backgroundScope)
          val (red, _) = fx.seated("Red", 1, 1)
          val (blue, _) = fx.seated("Blue", 2, 4)
          fx.duels.challenge(red, blue)
          (red.lines() + blue.lines()).filter { it.contains("offline save") }.shouldBeEmpty()
        }
      }

      test("a letter carries no monster, marked or not") {
        runTest {
          val fx = FenceFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1, offlineOrigin = true)
          fx.seated("Blue", 2, 4)
          val marked = fx.store.getCharacter(redId)!!.pokemon.single()
          fx.mail.onCompose(
              PacketEvent(
                  MailComposeSendPacket(
                      recipientName = "Blue",
                      subject = "Here",
                      body = "Have this one.",
                      attachments = listOf(PokemonMailAttachment(2, marked.id)),
                  ),
                  red))
          // Result 8 is "you cannot attach that", and it is the whole rule today: nothing rides on
          // a letter yet. The fence needs it to stay that way for a marked monster when they open.
          red.sent.filterIsInstance<MailResultPacket>().single().code shouldBe 8
        }
      }
    })
