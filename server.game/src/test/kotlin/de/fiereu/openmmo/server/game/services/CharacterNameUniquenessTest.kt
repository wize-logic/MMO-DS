package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.net.game.codecs.SkinSet
import de.fiereu.openmmo.net.game.packets.CharactersListPacket
import de.fiereu.openmmo.net.game.packets.CreateCharacterPacket
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.PlayerState
import de.fiereu.openmmo.server.game.storage.CharacterNameTakenException
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import de.fiereu.openmmo.server.game.testsupport.loginService
import io.kotest.assertions.throwables.shouldThrow
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest

private fun session(userId: Int): FakeSession {
  val session = FakeSession()
  session.attributes[PLAYER_STATE] = PlayerState(userId = userId)
  return session
}

private fun creation(name: String) =
    CreateCharacterPacket(name, CharacterGender.MALE.wireValue, Region.SINNOH.wireValue, SkinSet())

/**
 * One name, one player. A name is how the world addresses a character, so a second character
 * answering to it makes a whisper, a trade and a friend row ambiguous.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class CharacterNameUniquenessTest :
    FunSpec({
      fun store(scope: CoroutineScope) =
          CharacterStore(FakeCharacterRepository(), EntityIdService(), scope)

      test("a name another account already has is refused") {
        runTest {
          val store = store(backgroundScope)
          val service = loginService(store)
          service.onCreateCharacter(PacketEvent(creation("Lucas"), session(7)))

          val second = session(8)
          service.onCreateCharacter(PacketEvent(creation("Lucas"), second))

          store.getCharactersByUser(8).size shouldBe 0
          // The refusal is the unchanged list: the packet carries no field for a reason.
          (second.sent.last() as CharactersListPacket).characters.size shouldBe 0
          store.getCharactersByUser(7).single().info.name shouldBe "Lucas"
        }
      }

      test("case is not a difference") {
        runTest {
          val store = store(backgroundScope)
          val service = loginService(store)
          service.onCreateCharacter(PacketEvent(creation("Lucas"), session(7)))

          service.onCreateCharacter(PacketEvent(creation("lUcAs"), session(8)))

          store.getCharactersByUser(8).size shouldBe 0
        }
      }

      // Only connected players are cached, so the name of somebody who has logged off is held by
      // the database or by nothing at all.
      test("a taken name is still refused once its owner has gone home") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), this)
          val created = store.createCharacter(7, "Dawn", CharacterGender.FEMALE, Region.SINNOH)
          store.unloadCharacterAsync(created.info.id)
          advanceUntilIdle()
          store.getCharacter(created.info.id) shouldBe null

          store.isNameTaken("dawn") shouldBe true
          val refused =
              shouldThrow<CharacterNameTakenException> {
                store.createCharacter(8, "Dawn", CharacterGender.FEMALE, Region.SINNOH)
              }
          refused.name shouldBe "Dawn"
        }
      }
    })
