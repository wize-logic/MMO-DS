package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.S16LE

/**
 * the official client `the official client`: three shorts written onto `HQ.gV`, `HQ.NU0` and
 * `HQ.I`. The inbox widget prints `gV` against a hard 250; a rise in `NU0` is "you have received
 * new mail"; the sent widget pages against `I`.
 */
data class MailboxCountsPacket(
    val inbox: Short,
    val inboxSeen: Short,
    val sent: Short,
)

object MailboxCountsPacketCodec : PacketCodec<MailboxCountsPacket>() {
  override fun CodecScope<MailboxCountsPacket>.body(): MailboxCountsPacket {
    val inbox = field(S16LE) { it.inbox }
    val inboxSeen = field(S16LE) { it.inboxSeen }
    val sent = field(S16LE) { it.sent }
    return MailboxCountsPacket(inbox, inboxSeen, sent)
  }
}
