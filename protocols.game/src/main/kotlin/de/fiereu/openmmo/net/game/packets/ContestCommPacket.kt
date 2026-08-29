package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.S32LE
import de.fiereu.bytecodec.S8
import de.fiereu.bytecodec.U16LE
import de.fiereu.bytecodec.U8
import de.fiereu.bytecodec.bytesPrefixed

/**
 * A link Super Contest: the queue that fills one, and the engine traffic that runs it.
 * @param kind which of the eight below.
 * @param sessionId the contest this belongs to. 0 before a seat exists.
 * @param seat which of the up-to-four places this is about: the sender on the way up, the sender
 * @param payload the kind's own bytes; see each constant.
 */
data class ContestCommPacket(
    val kind: Int,
    val sessionId: Int,
    val seat: Int,
    val payload: ByteArray,
) {
  override fun equals(other: Any?): Boolean =
      other is ContestCommPacket &&
          kind == other.kind &&
          sessionId == other.sessionId &&
          seat == other.seat &&
          payload.contentEquals(other.payload)

  override fun hashCode(): Int =
      ((kind * 31 + sessionId) * 31 + seat) * 31 + payload.contentHashCode()

  companion object {
    /**
     * c2s: put me in the queue for a link contest. `payload[0]` is the contest rank,
     * `payload[1]` the contest type and `payload[2]` the party slot of the Pokemon being
     * entered, the same three the registration desk asks for before an official contest.
     */
    const val KIND_QUEUE = 0

    /** bidi: this player is not in the queue. */
    const val KIND_CANCEL = 1

    /**
     * s2c: still waiting. `payload[0]` is how many are in this queue so far and `payload[1]` how
     * many the contest will start with at most, so a screen can say "1 of 4" rather than nothing.
     */
    const val KIND_WAITING = 2

    /**
     * s2c: the contest is starting and this is your place in it. [seat] is this player's own
     * net id.
     */
    const val KIND_SEAT = 3

    /**
     * bidi: one of the contest's own comm commands, relayed verbatim. `payload[0]` is the
     * command id (22, 37, the table `CommCmd_Init` registers for a contest) and the rest is its
     * body.
     */
    const val KIND_DATA = 4

    /** bidi: a `CommTiming` barrier. `payload[0]` is the sync number. */
    const val KIND_SYNC = 5

    /**
     * bidi: a player is out. c2s when this client leaves a contest that has not finished; s2c
     * to everyone else, with [seat] naming who.
     */
    const val KIND_LEAVE = 6

    /**
     * c2s: what this client computed. The payload is one byte per human seat, in seat order:
     * that seat's placement, 0 for the winner.
     */
    const val KIND_RESULT = 7

    /** [KIND_SEAT] flags: this seat's player has finished the main story. */
    const val SEAT_STORY_CLEARED = 0x01

    /** [KIND_SEAT] flags: this seat's player has the National Dex. */
    const val SEAT_NATIONAL_DEX = 0x02

    /** The engine's own number of contestants, human and rival together. */
    const val CONTEST_PARTICIPANTS = 4

    /** The engine's own ceiling on one relayed command, asserted in `sub_02095B04`. */
    const val MAX_DATA_BYTES = 1024
  }
}

object ContestCommPacketCodec : PacketCodec<ContestCommPacket>() {
  override fun CodecScope<ContestCommPacket>.body(): ContestCommPacket {
    val kind = field(U8) { it.kind }
    val sessionId = field(S32LE) { it.sessionId }
    val seat = field(S8) { it.seat.toByte() }
    val payload = field(bytesPrefixed(U16LE)) { it.payload }
    return ContestCommPacket(kind, sessionId, seat.toInt(), payload)
  }
}
