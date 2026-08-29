package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.S16LE
import de.fiereu.bytecodec.S8
import de.fiereu.bytecodec.U16LE
import de.fiereu.bytecodec.bytesPrefixed

/** One relayed message of the engine's own trade scene (bidi 0xBF, ours). */
data class TradeCommPacket(
    val channel: Byte,
    val command: Short,
    val payload: ByteArray,
) {
  override fun equals(other: Any?): Boolean =
      other is TradeCommPacket &&
          channel == other.channel &&
          command == other.command &&
          payload.contentEquals(other.payload)

  override fun hashCode(): Int =
      (channel.toInt() * 31 + command.toInt()) * 31 + payload.contentHashCode()

  companion object {
    const val CHANNEL_COMMAND: Byte = 0
    const val CHANNEL_SYNC: Byte = 1
  }
}

object TradeCommPacketCodec : PacketCodec<TradeCommPacket>() {
  override fun CodecScope<TradeCommPacket>.body(): TradeCommPacket {
    val channel = field(S8) { it.channel }
    val command = field(S16LE) { it.command }
    val payload = field(bytesPrefixed(U16LE)) { it.payload }
    return TradeCommPacket(channel, command, payload)
  }
}
