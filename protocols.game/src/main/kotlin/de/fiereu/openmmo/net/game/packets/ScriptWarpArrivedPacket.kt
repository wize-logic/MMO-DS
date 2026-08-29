package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.S16LE
import de.fiereu.bytecodec.S32LE
import de.fiereu.bytecodec.U8

/** The client has already changed map on its own and is saying where it landed. */
data class ScriptWarpArrivedPacket(
    val mapHeaderId: Int,
    val x: Short,
    val y: Short,
    val direction: Int,
)

object ScriptWarpArrivedPacketCodec : PacketCodec<ScriptWarpArrivedPacket>() {
  override fun CodecScope<ScriptWarpArrivedPacket>.body(): ScriptWarpArrivedPacket {
    val mapHeaderId = field(S32LE) { it.mapHeaderId }
    val x = field(S16LE) { it.x }
    val y = field(S16LE) { it.y }
    val direction = field(U8) { it.direction }
    return ScriptWarpArrivedPacket(mapHeaderId, x, y, direction)
  }
}
