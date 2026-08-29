package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.S64LE
import de.fiereu.bytecodec.S8

/**
 * Asks what a pairing would produce. The game client sends this when the second parent is put down
 * and again on every one of its three gender buttons, which send -1 (leave it to the server), 0 and
 * 1 and nothing else.
 */
data class AssignBreedingSlotPacket(
    val ownPokemonEntityId: Long,
    val partnerPokemonEntityId: Long,
    val genderChoice: Byte,
)

object AssignBreedingSlotPacketCodec : PacketCodec<AssignBreedingSlotPacket>() {
  override fun CodecScope<AssignBreedingSlotPacket>.body(): AssignBreedingSlotPacket {
    val ownPokemonEntityId = field(S64LE) { it.ownPokemonEntityId }
    val partnerPokemonEntityId = field(S64LE) { it.partnerPokemonEntityId }
    val genderChoice = field(S8) { it.genderChoice }
    return AssignBreedingSlotPacket(ownPokemonEntityId, partnerPokemonEntityId, genderChoice)
  }
}
