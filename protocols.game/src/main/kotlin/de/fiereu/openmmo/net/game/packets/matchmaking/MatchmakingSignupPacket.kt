package de.fiereu.openmmo.net.game.packets.matchmaking

import de.fiereu.bytecodec.*

/** One queue the player is signing up for, and which of their parties they are bringing. */
data class QueueSlotSelection(val queueId: Byte, val partySlot: Byte)

/** What a signup asks for: a seat in a tournament, or a place in one or more queues. */
sealed class SignupRequest

/**
 * A tournament seat. The leading byte on the wire is 0 rather than a count, which is what tells the
 * two shapes apart.
 */
data class TournamentSignup(val tournamentId: Long, val partySlot: Byte) : SignupRequest()

/** One or more queues, each with the party being entered. An empty list withdraws. */
data class QueueSignup(val selections: List<QueueSlotSelection>) : SignupRequest()

private val SignupRequestCodec: Codec<SignupRequest> =
    object : Codec<SignupRequest> {
      override fun read(buf: ReadBuffer): SignupRequest {
        val first = buf.readByte().toInt() and 0xFF
        return if (first == 0) {
          var id = 0L
          for (i in 0 until 8) {
            id = id or ((buf.readByte().toLong() and 0xFF) shl (i * 8))
          }
          TournamentSignup(id, buf.readByte())
        } else {
          val selections = ArrayList<QueueSlotSelection>(first)
          repeat(first) { selections.add(QueueSlotSelection(buf.readByte(), buf.readByte())) }
          QueueSignup(selections)
        }
      }

      override fun write(buf: WriteBuffer, value: SignupRequest) {
        when (value) {
          is TournamentSignup -> {
            buf.writeByte(0.toByte())
            var v = value.tournamentId
            for (i in 0 until 8) {
              buf.writeByte((v and 0xFF).toByte())
              v = v ushr 8
            }
            buf.writeByte(value.partySlot)
          }

          is QueueSignup -> {
            buf.writeByte(value.selections.size.toByte())
            for (s in value.selections) {
              buf.writeByte(s.queueId)
              buf.writeByte(s.partySlot)
            }
          }
        }
      }
    }

/** Sign up for matchmaking. */
data class MatchmakingSignupPacket(val request: SignupRequest)

object MatchmakingSignupPacketCodec : PacketCodec<MatchmakingSignupPacket>() {
  override fun CodecScope<MatchmakingSignupPacket>.body(): MatchmakingSignupPacket {
    val request = field(SignupRequestCodec) { it.request }
    return MatchmakingSignupPacket(request)
  }
}
