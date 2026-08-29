package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.S64LE

/** the official client `the official client`: the mail id of the row that was opened. */
data class MailDetailRequestPacket(
    val mailId: Long,
)

object MailDetailRequestPacketCodec : PacketCodec<MailDetailRequestPacket>() {
  override fun CodecScope<MailDetailRequestPacket>.body(): MailDetailRequestPacket {
    val mailId = field(S64LE) { it.mailId }
    return MailDetailRequestPacket(mailId)
  }
}
