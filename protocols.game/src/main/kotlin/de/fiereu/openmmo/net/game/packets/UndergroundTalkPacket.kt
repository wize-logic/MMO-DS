package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.S64LE
import de.fiereu.bytecodec.U16LE
import de.fiereu.bytecodec.U8
import de.fiereu.bytecodec.bytesPrefixed

/**
 * Talking to another player in Sinnoh's Underground.
 * @param kind which of the five below.
 * @param entityId the player this is about. c2s on a [KIND_REQUEST] it is the player being faced;
 * @param payload the kind's own bytes; see each constant.
 */
data class UndergroundTalkPacket(
    val kind: Int,
    val entityId: Long,
    val payload: ByteArray,
) {
  override fun equals(other: Any?): Boolean =
      other is UndergroundTalkPacket &&
          kind == other.kind &&
          entityId == other.entityId &&
          payload.contentEquals(other.payload)

  override fun hashCode(): Int = (kind * 31 + entityId.hashCode()) * 31 + payload.contentHashCode()

  companion object {
    /** c2s: "I pressed A facing [entityId]." No payload. */
    const val KIND_REQUEST = 0

    /**
     * c2s: this player's availability changed. `payload[0]` is one of [STATE_FREE],
     * [STATE_BUSY], [STATE_MINING].
     */
    const val KIND_STATE = 1

    /**
     * bidi: one of the conversation's own comm commands, relayed verbatim to the other party.
     * `payload[0]` is the command id (75, 76, 78, 80 or 82, the broadcast half of
     * `src/underground/player_talk.c` and `src/underground/records.c`) and the rest is its
     * body.
     */
    const val KIND_DATA = 2

    /**
     * s2c: the answer to a [KIND_REQUEST]. `payload[0]` is the engine's `enum TalkResult`, 
     * [TALK_SUCCESS], [TALK_FAIL] or [TALK_MINING], and `payload[1]` is which end of a
     * successful pairing this player is: [ROLE_INITIATOR] or [ROLE_RESPONDER].
     */
    const val KIND_RESULT = 3

    /**
     * bidi: the conversation is over. c2s when this player's menu closed; s2c when the other
     * party dropped, or left the cavern with a box still open on this screen.
     */
    const val KIND_END = 4

    /**
     * [KIND_END]: an orderly goodbye, which needs no telling. Both exits in `player_talk.c`
     * announce themselves to the other side through the relay first, and the menu still on the
     * other screen is printing "OK, see you!" and waiting for its own A press.
     */
    const val END_DONE = 0

    /**
     * [KIND_END]: this client would not take the pairing. Availability is a report, so it is a
     * round trip old, and the player who was faced may have opened a menu or started digging
     * since.
     */
    const val END_REFUSED = 1

    /** [KIND_STATE]: free to be talked to. */
    const val STATE_FREE = 0

    /** [KIND_STATE]: in a menu, mid-step, trapped or already talking. */
    const val STATE_BUSY = 1

    /** [KIND_STATE]: inside the mining game, which has its own refusal line. */
    const val STATE_MINING = 2

    /** `TALK_RESULT_SUCCESS`: the conversation opens on both screens. */
    const val TALK_SUCCESS = 1

    /** `TALK_RESULT_FAIL`: "That person seems occupied." */
    const val TALK_FAIL = 2

    /** `TALK_RESULT_MINING`: "I'm digging right now!" */
    const val TALK_MINING = 4

    /** The player who pressed A. Runs `UndergroundTalk_Start`. */
    const val ROLE_INITIATOR = 0

    /** The player who was faced. Runs `UndergroundTalkResponse_Start`. */
    const val ROLE_RESPONDER = 1
  }
}

object UndergroundTalkPacketCodec : PacketCodec<UndergroundTalkPacket>() {
  override fun CodecScope<UndergroundTalkPacket>.body(): UndergroundTalkPacket {
    val kind = field(U8) { it.kind }
    val entityId = field(S64LE) { it.entityId }
    val payload = field(bytesPrefixed(U16LE)) { it.payload }
    return UndergroundTalkPacket(kind, entityId, payload)
  }
}
