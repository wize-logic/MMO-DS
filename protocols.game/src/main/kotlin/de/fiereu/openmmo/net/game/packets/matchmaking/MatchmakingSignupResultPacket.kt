package de.fiereu.openmmo.net.game.packets.matchmaking

import de.fiereu.bytecodec.*

/** What a signup was answered with. */
data class MatchmakingSignupResultPacket(
    val queueCount: Byte,
    val queues: List<Byte>,
    val tournamentId: Long,
    val value: Int,
    val outcome: Byte,
    val clause: Byte?,
) {
  companion object {
    /** The one outcome whose body carries a clause; the official client's own `PA0` byte for it. */
    const val OUTCOME_CLAUSE_VIOLATED: Int = 5
  }
}

object MatchmakingSignupResultPacketCodec : PacketCodec<MatchmakingSignupResultPacket>() {
  override fun CodecScope<MatchmakingSignupResultPacket>.body(): MatchmakingSignupResultPacket {
    val queueCount = field(S8) { it.queueCount }
    val queues = ArrayList<Byte>()
    var tournamentId = 0L
    if (queueCount < 0) {
      tournamentId = field(S64LE) { it.tournamentId }
    } else {
      repeat(queueCount.toInt()) { i -> queues.add(field(S8) { p -> p.queues[i] }) }
    }
    val value = field(S32LE) { it.value }
    val outcome = field(S8) { it.outcome }
    val clause =
        if (outcome.toInt() == MatchmakingSignupResultPacket.OUTCOME_CLAUSE_VIOLATED)
            field(S8) { it.clause!! }
        else null
    return MatchmakingSignupResultPacket(queueCount, queues, tournamentId, value, outcome, clause)
  }
}
