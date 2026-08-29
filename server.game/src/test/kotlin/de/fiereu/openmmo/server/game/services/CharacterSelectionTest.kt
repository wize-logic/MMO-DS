package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.net.game.packets.SelectCharacterPacket
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import de.fiereu.openmmo.server.game.testsupport.loginService
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

/** One character per session. */
@OptIn(ExperimentalCoroutinesApi::class)
class CharacterSelectionTest :
    FunSpec({
      test("a session that already has a character refuses a second selection") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val first = store.createCharacter(7, "Lucas", CharacterGender.MALE, Region.SINNOH)
          val second = store.createCharacter(7, "Dawn", CharacterGender.FEMALE, Region.SINNOH)
          val session = FakeSession(characterId = null, userId = 7)
          val service = loginService(store)

          service.onCharacterSelected(PacketEvent(SelectCharacterPacket(first.info.id, 0), session))
          service.onCharacterSelected(
              PacketEvent(SelectCharacterPacket(second.info.id, 0), session))

          session.attributes[PLAYER_STATE]!!.characterId shouldBe first.info.id
        }
      }
    })
