package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.*
import de.fiereu.openmmo.common.enums.PokemonContainer

/** One monster picked up from [fromContainer]/[fromSlot] and put down on [toContainer]/[toSlot]. */
data class PokemonMove(
    val fromContainer: PokemonContainer,
    val fromSlot: Short,
    val toContainer: PokemonContainer,
    val toSlot: Short,
)

/**
 * A batch of container moves: deposits, withdrawals and reorders are all the same gesture, a
 * monster dragged from one slot onto another.
 */
data class PokemonMovePacket(val moves: List<PokemonMove>)

private val PokemonMoveCodec: Codec<PokemonMove> =
    object : PacketCodec<PokemonMove>() {
      override fun CodecScope<PokemonMove>.body(): PokemonMove {
        val fromContainer = PokemonContainer.entries[field(U8) { it.fromContainer.ordinal }]
        val fromSlot = field(S16LE) { it.fromSlot }
        val toContainer = PokemonContainer.entries[field(U8) { it.toContainer.ordinal }]
        val toSlot = field(S16LE) { it.toSlot }
        return PokemonMove(fromContainer, fromSlot, toContainer, toSlot)
      }
    }

object PokemonMovePacketCodec : PacketCodec<PokemonMovePacket>() {
  override fun CodecScope<PokemonMovePacket>.body(): PokemonMovePacket {
    val moves = field(PokemonMoveCodec.listPrefixed(U8)) { it.moves }
    return PokemonMovePacket(moves)
  }
}
