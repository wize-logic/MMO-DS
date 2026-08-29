package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.S8
import de.fiereu.bytecodec.Utf16LeNullTerminated

/**
 * Where a direct trade stands. The official client's trade screen speaks only 0x50 (an action byte up) and
 * 0x52 (a slot up, a monster record down), so the session's own state has no official packet;
 * this one is ours, on an s2c slot the official client leaves empty.
 */
data class TradeStatePacket(
    val state: Byte,
    val role: Byte,
    val peerGender: Byte,
    val peerName: String,
) {
  companion object {
    /** Both sides agreed; the table is open. */
    const val STATE_OPEN: Byte = 1

    /** The peer locked in the current pair of selections. */
    const val STATE_PEER_CONFIRMED: Byte = 2

    /** The trade is gone: declined, cancelled, lapsed, or a side disconnected. */
    const val STATE_CANCELLED: Byte = 3

    /** Both confirmed and the swap is written; the containers follow. */
    const val STATE_COMPLETED: Byte = 4
  }
}

object TradeStatePacketCodec : PacketCodec<TradeStatePacket>() {
  override fun CodecScope<TradeStatePacket>.body(): TradeStatePacket {
    val state = field(S8) { it.state }
    val role = field(S8) { it.role }
    val peerGender = field(S8) { it.peerGender }
    val peerName = field(Utf16LeNullTerminated) { it.peerName }
    return TradeStatePacket(state, role, peerGender, peerName)
  }
}
