package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.*

/** The engine's own arithmetic spent or earned cash, and this is the record of it. */
data class MoneyDeltaPacket(
    val delta: Int,
)

object MoneyDeltaPacketCodec : PacketCodec<MoneyDeltaPacket>() {
  override fun CodecScope<MoneyDeltaPacket>.body(): MoneyDeltaPacket {
    val delta = field(S32LE) { it.delta }
    return MoneyDeltaPacket(delta)
  }
}
