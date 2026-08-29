package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.net.game.codecs.SkinSet
import de.fiereu.openmmo.net.game.packets.CharactersListPacket
import de.fiereu.openmmo.net.game.packets.CreateCharacterPacket
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.PlayerState
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import de.fiereu.openmmo.server.game.testsupport.loginService
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldBeEmpty
import io.kotest.matchers.shouldBe
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

private const val USER = 7

private fun session(): FakeSession {
  val session = FakeSession()
  session.attributes[PLAYER_STATE] = PlayerState(userId = USER)
  return session
}

private fun creation(name: String, region: Region) =
    CreateCharacterPacket(name, CharacterGender.MALE.wireValue, region.wireValue, SkinSet())

/** A character may only be made where somebody could play it. */
@OptIn(ExperimentalCoroutinesApi::class)
class CharacterCreationRegionTest :
    FunSpec({
      fun store(scope: CoroutineScope) =
          CharacterStore(FakeCharacterRepository(), EntityIdService(), scope)

      test("a Sinnoh character is created") {
        runTest {
          val store = store(backgroundScope)
          val session = session()

          loginService(store)
              .onCreateCharacter(PacketEvent(creation("Lucas", Region.SINNOH), session))

          val characters = store.getCharactersByUser(USER)
          characters.size shouldBe 1
          characters.single().info.positionRegionId shouldBe Region.SINNOH.wireValue
          (session.sent.last() as CharactersListPacket).characters.size shouldBe 1
        }
      }

      test("a character in a region no client can draw is refused") {
        runTest {
          val store = store(backgroundScope)
          val service = loginService(store)

          for (region in Region.entries.filterNot { it.creatable }) {
            val session = session()

            service.onCreateCharacter(PacketEvent(creation("Brendan", region), session))

            store.getCharactersByUser(USER).shouldBeEmpty()
            // The reply is still the character list: the packet carries no field for a reason, so
            // the player is told by the client, which never offered the region in the first place.
            (session.sent.last() as CharactersListPacket).characters.shouldBeEmpty()
          }
        }
      }
    })
