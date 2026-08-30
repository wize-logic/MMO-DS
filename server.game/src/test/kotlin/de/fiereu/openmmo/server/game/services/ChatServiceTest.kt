package de.fiereu.openmmo.server.game.services

import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.ChatType
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.net.game.packets.ChatMessagePacket
import de.fiereu.openmmo.net.game.packets.ChatMessageSendPacket
import de.fiereu.openmmo.server.game.services.command.ChatCommandService
import de.fiereu.openmmo.server.game.services.command.HelpCommand
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import io.kotest.matchers.string.shouldContain
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

@OptIn(ExperimentalCoroutinesApi::class)
class ChatServiceTest :
    FunSpec({
      fun FakeSession.lines() = sent.filterIsInstance<ChatMessagePacket>()

      test("ordinary chat is echoed to the sender and everyone else online") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val red = store.createCharacter(1, "Red", CharacterGender.MALE, Region.SINNOH).info.id
          val blue = store.createCharacter(2, "Blue", CharacterGender.FEMALE, Region.SINNOH).info.id
          val a = FakeSession(characterId = red)
          val b = FakeSession(characterId = blue)
          val registry = SessionRegistry()
          registry.bindCharacter(a, red)
          registry.bindCharacter(b, blue)
          val chat =
              ChatService(
                  ChatCommandService(store, setOf(HelpCommand())), registry, store, ViolationLog())

          chat.onSend(a, ChatMessageSendPacket(mode = 0, target = "hello", message = null))

          val fromA = a.lines().single()
          val fromB = b.lines().single()
          fromA.type shouldBe ChatType.NORMAL
          fromA.message shouldBe "hello"
          fromA.sender shouldBe "Red"
          fromA.senderId shouldBe red
          fromB shouldBe fromA
        }
      }

      test("local chat reaches the sender's map and no further") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val red = store.createCharacter(1, "Red", CharacterGender.MALE, Region.SINNOH).info.id
          val blue = store.createCharacter(2, "Blue", CharacterGender.FEMALE, Region.SINNOH).info.id
          val green = store.createCharacter(3, "Green", CharacterGender.MALE, Region.SINNOH).info.id
          val a = FakeSession(characterId = red, mapId = 3)
          val sameMap = FakeSession(characterId = green, mapId = 3)
          val elsewhere = FakeSession(characterId = blue, mapId = 16)
          val registry = SessionRegistry()
          registry.bindCharacter(a, red)
          registry.bindCharacter(sameMap, green)
          registry.bindCharacter(elsewhere, blue)
          val chat =
              ChatService(
                  ChatCommandService(store, setOf(HelpCommand())), registry, store, ViolationLog())

          chat.onSend(a, ChatMessageSendPacket(mode = 0, target = "anyone here?", message = null))

          a.lines().single().type shouldBe ChatType.NORMAL
          sameMap.lines().single().message shouldBe "anyone here?"
          elsewhere.lines() shouldBe emptyList()
        }
      }

      test("the global and trade channels reach every map") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val red = store.createCharacter(1, "Red", CharacterGender.MALE, Region.SINNOH).info.id
          val blue = store.createCharacter(2, "Blue", CharacterGender.FEMALE, Region.SINNOH).info.id
          val a = FakeSession(characterId = red, mapId = 3)
          val elsewhere = FakeSession(characterId = blue, mapId = 16)
          val registry = SessionRegistry()
          registry.bindCharacter(a, red)
          registry.bindCharacter(elsewhere, blue)
          val chat =
              ChatService(
                  ChatCommandService(store, setOf(HelpCommand())), registry, store, ViolationLog())

          chat.onSend(a, ChatMessageSendPacket(mode = 6, target = "hello world", message = null))
          chat.onSend(a, ChatMessageSendPacket(mode = 5, target = "wts pearls", message = null))

          elsewhere.lines().map { it.type } shouldBe listOf(ChatType.GLOBAL, ChatType.TRADE)
          elsewhere.lines().map { it.message } shouldBe listOf("hello world", "wts pearls")
        }
      }

      test("a slash command is not broadcast") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val red = store.createCharacter(1, "Red", CharacterGender.MALE, Region.SINNOH).info.id
          val blue = store.createCharacter(2, "Blue", CharacterGender.FEMALE, Region.SINNOH).info.id
          val a = FakeSession(characterId = red)
          val b = FakeSession(characterId = blue)
          val registry = SessionRegistry()
          registry.bindCharacter(a, red)
          registry.bindCharacter(b, blue)
          val chat =
              ChatService(
                  ChatCommandService(store, setOf(HelpCommand())), registry, store, ViolationLog())

          chat.onSend(a, ChatMessageSendPacket(mode = 0, target = "/help", message = null))

          a.lines().single().message shouldContain "help"
          b.lines() shouldBe emptyList()
        }
      }

      test("a whisper goes to the named player and the sender") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val red = store.createCharacter(1, "Red", CharacterGender.MALE, Region.SINNOH).info.id
          val blue = store.createCharacter(2, "Blue", CharacterGender.FEMALE, Region.SINNOH).info.id
          val a = FakeSession(characterId = red)
          val b = FakeSession(characterId = blue)
          val registry = SessionRegistry()
          registry.bindCharacter(a, red)
          registry.bindCharacter(b, blue)
          val chat =
              ChatService(
                  ChatCommandService(store, setOf(HelpCommand())), registry, store, ViolationLog())

          chat.onSend(a, ChatMessageSendPacket(mode = 4, target = "Blue", message = "psst"))

          a.lines().single().let {
            it.type shouldBe ChatType.WHISPER
            it.message shouldBe "psst"
            it.sender shouldBe "Red"
          }
          b.lines().single().message shouldBe "psst"
        }
      }

      /**
       * Two of the types are the voice this server speaks in, and the codec short-forms both with
       * no sender field, so a line sent on one arrived looking like a real announcement.
       */
      test("a client cannot speak in the server's own voice") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val red = store.createCharacter(1, "Red", CharacterGender.MALE, Region.SINNOH).info.id
          val blue = store.createCharacter(2, "Blue", CharacterGender.FEMALE, Region.SINNOH).info.id
          val a = FakeSession(characterId = red)
          val b = FakeSession(characterId = blue)
          val registry = SessionRegistry()
          registry.bindCharacter(a, red)
          registry.bindCharacter(b, blue)
          val chat =
              ChatService(
                  ChatCommandService(store, setOf(HelpCommand())), registry, store, ViolationLog())

          // 16 and 17 are the server's own channels, 18 is the battle one, which is scoped by its
          // own packet and would arrive here only to get around that.
          for (mode in listOf<Byte>(16, 17, 18)) {
            chat.onSend(
                a, ChatMessageSendPacket(mode = mode, target = "free items!", message = null))
          }

          a.lines() shouldBe emptyList()
          b.lines() shouldBe emptyList()
        }
      }

      /** A line is copied to every session on its channel, and the codec has no ceiling. */
      test("a line longer than the client can type is cut to what it can") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val red = store.createCharacter(1, "Red", CharacterGender.MALE, Region.SINNOH).info.id
          val a = FakeSession(characterId = red)
          val registry = SessionRegistry()
          registry.bindCharacter(a, red)
          val chat =
              ChatService(
                  ChatCommandService(store, setOf(HelpCommand())), registry, store, ViolationLog())

          chat.onSend(
              a, ChatMessageSendPacket(mode = 6, target = "A".repeat(30_000), message = null))

          a.lines().single().message.length shouldBe 128
        }
      }

      test("a whisper to nobody online is a notice, not a broadcast") {
        runTest {
          val store = CharacterStore(FakeCharacterRepository(), EntityIdService(), backgroundScope)
          val red = store.createCharacter(1, "Red", CharacterGender.MALE, Region.SINNOH).info.id
          val blue = store.createCharacter(2, "Blue", CharacterGender.FEMALE, Region.SINNOH).info.id
          val a = FakeSession(characterId = red)
          val b = FakeSession(characterId = blue)
          val registry = SessionRegistry()
          registry.bindCharacter(a, red)
          registry.bindCharacter(b, blue)
          val chat =
              ChatService(
                  ChatCommandService(store, setOf(HelpCommand())), registry, store, ViolationLog())

          chat.onSend(a, ChatMessageSendPacket(mode = 4, target = "Missing", message = "hi"))

          a.lines().single().message shouldContain "No one"
          b.lines() shouldBe emptyList()
        }
      }
    })
