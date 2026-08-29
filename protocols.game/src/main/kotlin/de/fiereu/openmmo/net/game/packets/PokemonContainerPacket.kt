package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.*
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.net.game.codecs.PokemonCodec

/** One monster container and what is in it. */
data class PokemonContainerPacket(
    val container: PokemonContainer,
    val hasChange: Boolean,
    val delete: Boolean,
    val pokemon: List<Pokemon>,
    val unknown: Int? = null,
) {
  init {
    if (delete)
        require(hasChange) {
          "Pokemon container cannot be deleted if the packet doesn't change anything"
        }
    if (delete)
        require(pokemon.isEmpty()) { "Deleting a Pokemon container shouldn't have any pokemon" }
  }
}

private val PokemonListPrefixedU8: Codec<List<Pokemon>> =
    object : Codec<List<Pokemon>> {
      override fun read(buf: ReadBuffer): List<Pokemon> {
        val n = U8.read(buf)
        return List(n) { PokemonCodec.read(buf) }
      }

      override fun write(buf: WriteBuffer, value: List<Pokemon>) {
        U8.write(buf, value.size)
        value.forEach { PokemonCodec.write(buf, it) }
      }
    }

object PokemonContainerPacketCodec : PacketCodec<PokemonContainerPacket>() {
  override fun CodecScope<PokemonContainerPacket>.body(): PokemonContainerPacket {
    val container = PokemonContainer.entries[field(U8) { it.container.ordinal }]
    val flags =
        field(U8) {
          var f = 0
          if (it.hasChange) f = f or 1
          if (it.delete) f = f or 2
          if (it.unknown != null) f = f or 4
          f
        }
    val hasChange = (flags and 1) != 0
    val delete = (flags and 2) != 0
    if (delete) {
      // A delete stops the client's reader here: no word, no count, no records.
      return PokemonContainerPacket(container, hasChange, true, emptyList())
    }
    val unknown = optionalField((flags and 4) != 0, U16LE) { it.unknown }
    val pokemon = field(PokemonListPrefixedU8, PokemonContainerPacket::pokemon)
    return PokemonContainerPacket(container, hasChange, false, pokemon, unknown)
  }
}
