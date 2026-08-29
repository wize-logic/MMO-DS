package de.fiereu.openmmo.net.game.packets.matchmaking

import de.fiereu.bytecodec.*

/** One page of the tournament list. */
data class TournamentPagePacket(
    val tab: Byte,
    val active: Boolean,
    val page: Short,
    val total: Int,
    val tournaments: List<TournamentRecord>,
)

object TournamentPagePacketCodec : PacketCodec<TournamentPagePacket>() {
  override fun CodecScope<TournamentPagePacket>.body(): TournamentPagePacket {
    val tab = field(S8) { it.tab }
    val active = field(Bool) { it.active }
    val page = field(S16LE) { it.page }
    val total = field(S32LE) { it.total }
    val tournaments = field(TournamentRecordCodec.listPrefixed(U8)) { it.tournaments }
    return TournamentPagePacket(tab, active, page, total, tournaments)
  }
}
