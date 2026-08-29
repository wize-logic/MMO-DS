package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.S32LE
import de.fiereu.bytecodec.U16LE
import de.fiereu.bytecodec.U8
import de.fiereu.bytecodec.bytesPrefixed

/** One blob of a native link battle, relayed verbatim between the two clients. */
data class LinkBattleDataPacket(
    val battleId: Int,
    val kind: Int,
    val payload: ByteArray,
) {
  override fun equals(other: Any?): Boolean =
      other is LinkBattleDataPacket &&
          battleId == other.battleId &&
          kind == other.kind &&
          payload.contentEquals(other.payload)

  override fun hashCode(): Int = (battleId * 31 + kind) * 31 + payload.contentHashCode()

  companion object {
    /** A BattleMessageInfo plus its body: what the engine's own link transport carries. */
    const val KIND_MESSAGE = 0

    /** A CommTiming sync tag. The payload is one byte, the sync number. */
    const val KIND_SYNC = 1

    /** The sender left the fight before it ended. No payload. */
    const val KIND_LEAVE = 2

    /** The sender's scene ended. The payload is one byte, the engine's BATTLE_RESULT mask. */
    const val KIND_RESULT = 3

    /**
     * The computing side's "the fight is over", the engine's comm command 22, re-carried. No
     * payload; relayed like any other blob.
     */
    const val KIND_ENDWAIT = 4
  }
}

object LinkBattleDataPacketCodec : PacketCodec<LinkBattleDataPacket>() {
  override fun CodecScope<LinkBattleDataPacket>.body(): LinkBattleDataPacket {
    val battleId = field(S32LE) { it.battleId }
    val kind = field(U8) { it.kind }
    val payload = field(bytesPrefixed(U16LE)) { it.payload }
    return LinkBattleDataPacket(battleId, kind, payload)
  }
}
