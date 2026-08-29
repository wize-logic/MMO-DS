package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.U8

/**
 * the official client `the official client`: one `sj1` byte. The values are `sj1.Y90`; 0 is "sent successfully", 4 is
 * "recipient not found", 15 is "cannot send to yourself".
 */
data class MailResultPacket(
    val code: Int,
)

object MailResultPacketCodec : PacketCodec<MailResultPacket>() {
  override fun CodecScope<MailResultPacket>.body(): MailResultPacket {
    val code = field(U8) { it.code }
    return MailResultPacket(code)
  }
}
