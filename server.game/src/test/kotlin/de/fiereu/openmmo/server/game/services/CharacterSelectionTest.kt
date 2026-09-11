package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.auth.SessionTokenIssuer
import de.fiereu.openmmo.common.enums.Arch
import de.fiereu.openmmo.common.enums.Bitness
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.Platform
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.net.game.packets.JoinPacket
import de.fiereu.openmmo.net.game.packets.NewAuthData
import de.fiereu.openmmo.net.game.packets.SelectCharacterPacket
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import de.fiereu.openmmo.server.game.testsupport.loginService
import de.fiereu.openmmo.server.game.testsupport.loginTestSecret
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

/** A join carrying [userId]'s ticket. Nothing but the auth data is read on the way in. */
private fun joinAs(userId: Int, key: ByteArray) =
    JoinPacket(
        authData = NewAuthData(userId, key),
        mac = ByteArray(6),
        clientRevision = 0,
        installationRevision = 0,
        currentChatLanguage = 0,
        chatLanguages = 0,
        matchmakingLanguages = 0,
        romMask = 0,
        roms = emptyList(),
        clientInfo = emptyMap(),
        platform = Platform.LINUX,
        arch = Arch.X86,
        bitness = Bitness._32,
        unk1 = ByteArray(0),
        unk2 = ByteArray(0),
    )

/** One join and one character per session. */
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

      test("a session that has already joined refuses a second join") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val mine = store.createCharacter(7, "Lucas", CharacterGender.MALE, Region.SINNOH)
          val sessions = SessionRegistry()
          val service = loginService(store, sessions = sessions)
          val session = FakeSession(characterId = null, userId = 7)
          // A connection nobody has joined on yet.
          session.attributes.remove(PLAYER_STATE)

          val issuer = SessionTokenIssuer(loginTestSecret)
          service.onJoinGame(PacketEvent(joinAs(7, issuer.issue(7).bytes), session))
          service.onCharacterSelected(PacketEvent(SelectCharacterPacket(mine.info.id, 0), session))

          // A ticket minted for another account, on the connection somebody is already playing on.
          service.onJoinGame(PacketEvent(joinAs(9, issuer.issue(9).bytes), session))

          session.attributes[PLAYER_STATE]!!.userId shouldBe 7
          session.attributes[PLAYER_STATE]!!.characterId shouldBe mine.info.id
          sessions.getByCharacterId(mine.info.id) shouldBe session
        }
      }
    })
