package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.Bool
import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.S16LE

/** the official client `the official client`: page then the sent-box flag. */
data class MailPageRequestPacket(
    val page: Short,
    val sent: Boolean,
)

object MailPageRequestPacketCodec : PacketCodec<MailPageRequestPacket>() {
  override fun CodecScope<MailPageRequestPacket>.body(): MailPageRequestPacket {
    val page = field(S16LE) { it.page }
    val sent = field(Bool) { it.sent }
    return MailPageRequestPacket(page, sent)
  }
}
