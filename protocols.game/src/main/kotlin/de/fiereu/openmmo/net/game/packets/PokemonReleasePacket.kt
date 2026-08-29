package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.*

/** The box screen let this monster go, and this is the record of it. */
data class PokemonReleasePacket(
    val monsterId: Long,
)

object PokemonReleasePacketCodec : PacketCodec<PokemonReleasePacket>() {
  override fun CodecScope<PokemonReleasePacket>.body(): PokemonReleasePacket {
    val monsterId = field(S64LE) { it.monsterId }
    return PokemonReleasePacket(monsterId)
  }
}
