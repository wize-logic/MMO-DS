package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.*
import de.fiereu.openmmo.common.enums.ChatType
import de.fiereu.openmmo.common.enums.Language

data class ChatMessagePacket(
    val type: ChatType,
    val language: Language?,
    val message: String,
    val sender: String?,
    val senderId: Long = 0L,
)

object ChatMessagePacketCodec : PacketCodec<ChatMessagePacket>() {
  override fun CodecScope<ChatMessagePacket>.body(): ChatMessagePacket {
    val type = ChatType.entries[field(U8) { it.type.ordinal }]
    // The official client f/lU1 short-forms XR0.jb0 (wire 16, system announcements): just
    // the message. Team (wire 8) is the full form. The S8 after language has
    // no established meaning; notices write -1 and we carry it as-is.
    return if (type == ChatType.SYSTEM_ANNOUNCEMENTS) {
      ChatMessagePacket(
          type = type,
          language = null,
          message = field(Utf16LeNullTerminated, ChatMessagePacket::message),
          sender = null,
          senderId = 0L,
      )
    } else {
      val senderId = field(S64LE) { it.senderId }
      val sender =
          field(Utf16LeNullTerminated) {
            it.sender ?: throw MalformedPacketException("sender must not be null")
          }
      val language =
          Language.entries[
                  field(U8) {
                    (it.language ?: throw MalformedPacketException("language must not be null"))
                        .ordinal
                  }]
      field(S8) { -1 }
      val message = field(Utf16LeNullTerminated, ChatMessagePacket::message)
      ChatMessagePacket(
          type = type,
          language = language,
          message = message,
          sender = sender,
          senderId = senderId,
      )
    }
  }
}
