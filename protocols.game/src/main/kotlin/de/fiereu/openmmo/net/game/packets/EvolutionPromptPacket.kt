package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.Bool
import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.S16LE
import de.fiereu.bytecodec.S64LE

/**
 * A monster the server has decided is evolving. [species] is the National Dex id it becomes,
 * and [cancelable] is whether the player is allowed to stop it, the client hides its cancel
 * button without it, and closing the screen on one then counts as accepting.
 */
data class EvolutionPromptPacket(
    val pokemonEntityId: Long,
    val species: Short,
    val cancelable: Boolean,
)

object EvolutionPromptPacketCodec : PacketCodec<EvolutionPromptPacket>() {
  override fun CodecScope<EvolutionPromptPacket>.body(): EvolutionPromptPacket {
    val pokemonEntityId = field(S64LE) { it.pokemonEntityId }
    val species = field(S16LE) { it.species }
    val cancelable = field(Bool) { it.cancelable }
    return EvolutionPromptPacket(pokemonEntityId, species, cancelable)
  }
}
