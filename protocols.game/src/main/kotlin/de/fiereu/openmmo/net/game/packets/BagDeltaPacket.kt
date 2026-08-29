package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.*

/** The engine's own arithmetic consumed or gained an item, and this is the record of it. */
data class BagDeltaPacket(
    val itemId: Int,
    val delta: Int,
)

object BagDeltaPacketCodec : PacketCodec<BagDeltaPacket>() {
  override fun CodecScope<BagDeltaPacket>.body(): BagDeltaPacket {
    val itemId = field(U16LE) { it.itemId }
    val delta = field(S16LE) { it.delta.toShort() }
    return BagDeltaPacket(itemId, delta.toInt())
  }
}
