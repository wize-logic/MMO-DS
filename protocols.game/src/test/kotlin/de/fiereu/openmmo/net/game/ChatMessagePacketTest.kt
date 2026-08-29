package de.fiereu.openmmo.net.game

import de.fiereu.openmmo.common.enums.ChatType
import de.fiereu.openmmo.common.enums.Language
import de.fiereu.openmmo.common.test.decodeBytes
import de.fiereu.openmmo.common.test.encodeToBytes
import de.fiereu.openmmo.net.game.packets.ChatMessagePacket
import de.fiereu.openmmo.net.game.packets.ChatMessagePacketCodec
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

class ChatMessagePacketTest :
    FunSpec({
      test("a player line is type, sender id, name, language, unknown byte, message") {
        val pkt =
            ChatMessagePacket(
                type = ChatType.NORMAL,
                language = Language.EN,
                message = "hello",
                sender = "Dawn",
                senderId = 0x1234L,
            )
        val bytes = ChatMessagePacketCodec.encodeToBytes(pkt)
        // Laid out to the official client's the official client (the non-system branch).
        bytes shouldBe
            byteArrayOf(
                0x00,
                0x34,
                0x12,
                0x00,
                0x00,
                0x00,
                0x00,
                0x00,
                0x00,
                0x44,
                0x00,
                0x61,
                0x00,
                0x77,
                0x00,
                0x6E,
                0x00,
                0x00,
                0x00,
                0x00,
                0xFF.toByte(),
                0x68,
                0x00,
                0x65,
                0x00,
                0x6C,
                0x00,
                0x6C,
                0x00,
                0x6F,
                0x00,
                0x00,
                0x00,
            )
        ChatMessagePacketCodec.decodeBytes(bytes) shouldBe pkt
      }

      test("system announcements are just the type byte and the message") {
        val pkt =
            ChatMessagePacket(
                type = ChatType.SYSTEM_ANNOUNCEMENTS,
                language = null,
                message = "hi",
                sender = null,
            )
        val bytes = ChatMessagePacketCodec.encodeToBytes(pkt)
        bytes shouldBe
            byteArrayOf(
                0x10,
                0x68,
                0x00,
                0x69,
                0x00,
                0x00,
                0x00,
            )
        ChatMessagePacketCodec.decodeBytes(bytes) shouldBe pkt
      }

      test("a game notice keeps the full form and a zero sender id") {
        val pkt =
            ChatMessagePacket(
                type = ChatType.GAME_NOTIFICATIONS,
                language = Language.EN,
                message = "Welcome to OpenMMO!",
                sender = "",
            )
        val decoded = ChatMessagePacketCodec.decodeBytes(ChatMessagePacketCodec.encodeToBytes(pkt))
        decoded.type shouldBe ChatType.GAME_NOTIFICATIONS
        decoded.senderId shouldBe 0L
        decoded.sender shouldBe ""
        decoded.message shouldBe "Welcome to OpenMMO!"
      }
    })
