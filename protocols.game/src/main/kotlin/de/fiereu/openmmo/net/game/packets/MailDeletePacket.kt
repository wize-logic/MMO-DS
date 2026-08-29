package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.S16LE
import de.fiereu.bytecodec.S64LE

/** the official client `the official client`: mail id then the page the row sat on. */
data class MailDeletePacket(
    val mailId: Long,
    val page: Short,
)

object MailDeletePacketCodec : PacketCodec<MailDeletePacket>() {
  override fun CodecScope<MailDeletePacket>.body(): MailDeletePacket {
    val mailId = field(S64LE) { it.mailId }
    val page = field(S16LE) { it.page }
    return MailDeletePacket(mailId, page)
  }
}
