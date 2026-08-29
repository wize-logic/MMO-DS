package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.*

/**
 * A local script gave the player a Pokemon, or a ball caught one, and this is the record of
 * it.
 */
data class ScriptGrantPacket(
    val dexId: Int,
    val level: Int,
    /** Current hp; -1 means full. A capture arrives with its battle damage. */
    val hp: Int,
    /** The PokemonMovePacket's container ids: 0 = the PC, 1 = the party. */
    val container: Int,
    /** Where in the container; -1 appends. A box catch names the engine's slot. */
    val slot: Int,
    /**
     * The engine's personality value, which is what its nature is read out of. 0 = not reported.
     */
    val seed: Int,
    /** The six IVs, packed five bits each in the order `PokemonCodec` packs them. */
    val ivBits: Int,
    val isShiny: Boolean,
    val nickname: String,
)

object ScriptGrantPacketCodec : PacketCodec<ScriptGrantPacket>() {
  override fun CodecScope<ScriptGrantPacket>.body(): ScriptGrantPacket {
    val dexId = field(U16LE) { it.dexId }
    val level = field(U8) { it.level }
    val hp = field(S16LE) { it.hp.toShort() }
    val container = field(U8) { it.container }
    val slot = field(S16LE) { it.slot.toShort() }
    val seed = field(S32LE) { it.seed }
    val ivBits = field(S32LE) { it.ivBits }
    val shiny = field(U8) { if (it.isShiny) 1 else 0 }
    val nickname = field(Utf16LeNullTerminated) { it.nickname }
    return ScriptGrantPacket(
        dexId, level, hp.toInt(), container, slot.toInt(), seed, ivBits, shiny != 0, nickname)
  }
}
