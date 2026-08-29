package de.fiereu.openmmo.net.game.packets.matchmaking

import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.S16LE
import de.fiereu.bytecodec.S64LE

/** How many have entered one tournament, and how many of those have checked in. */
data class TournamentEntrantCountPacket(
    val tournamentId: Long,
    val entered: Short,
    val checkedIn: Short,
)

object TournamentEntrantCountPacketCodec : PacketCodec<TournamentEntrantCountPacket>() {
  override fun CodecScope<TournamentEntrantCountPacket>.body(): TournamentEntrantCountPacket {
    val tournamentId = field(S64LE) { it.tournamentId }
    val entered = field(S16LE) { it.entered }
    val checkedIn = field(S16LE) { it.checkedIn }
    return TournamentEntrantCountPacket(tournamentId, entered, checkedIn)
  }
}
