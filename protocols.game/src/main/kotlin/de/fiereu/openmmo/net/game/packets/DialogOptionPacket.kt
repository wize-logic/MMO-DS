package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.S32LE
import de.fiereu.bytecodec.S64LE
import de.fiereu.bytecodec.U16LE

/**
 * Sent when the player picks a target for an item chosen from the bag outside battle, the
 * party member the item is being used on.
 */
data class DialogOptionPacket(
    val itemId: Int,
    val targetEntityId: Long,
    val trailer: Int = TRAILER_SAMPLE,
) {
  companion object {
    /** The only value observed so far, from the Potion capture. */
    const val TRAILER_SAMPLE: Int = 0x00FF0001
  }
}

object DialogOptionPacketCodec : PacketCodec<DialogOptionPacket>() {
  override fun CodecScope<DialogOptionPacket>.body(): DialogOptionPacket {
    val itemId = field(U16LE) { it.itemId }
    val targetEntityId = field(S64LE) { it.targetEntityId }
    val trailer = field(S32LE) { it.trailer }
    return DialogOptionPacket(itemId, targetEntityId, trailer)
  }
}
