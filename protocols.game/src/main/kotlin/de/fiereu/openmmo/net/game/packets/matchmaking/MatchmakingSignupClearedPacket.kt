package de.fiereu.openmmo.net.game.packets.matchmaking

import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.reserved

/** The standing signup is gone: the window drops back to its idle state. */
data class MatchmakingSignupClearedPacket(
    val unit: Unit = Unit,
)

object MatchmakingSignupClearedPacketCodec : PacketCodec<MatchmakingSignupClearedPacket>() {
  override fun CodecScope<MatchmakingSignupClearedPacket>.body(): MatchmakingSignupClearedPacket {
    reserved(byte = 0)
    return MatchmakingSignupClearedPacket()
  }
}
