package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.*

/**
 * One flag of the local script VM's state. [id] is the engine's own flag number, from
 * Platinum's `generated/vars_flags.h`; it is not the GBA id space [StoryFlagUpdatePacket]
 * carries and the two must not be mixed.
 */
data class ScriptFlagEntry(
    val id: Short,
    val on: Boolean,
)

/** One variable of that state. [id] is the un-rebased engine var number (`VARS_START`-based). */
data class ScriptVarEntry(
    val id: Short,
    val value: Short,
)

/** One save block of the game's, carried whole and as opaque bytes. */
data class SaveBlockEntry(
    val id: Int,
    val data: ByteArray,
) {
  override fun equals(other: Any?): Boolean =
      other is SaveBlockEntry && id == other.id && data.contentEquals(other.data)

  override fun hashCode(): Int = id * 31 + data.contentHashCode()
}

/**
 * The state the client's own script VM reads and writes: the engine's `VarsFlags` save block,
 * sent as a sparse list either way, and the whole save blocks that hang off the same
 * ownership.
 */
data class ScriptStatePacket(
    val flags: List<ScriptFlagEntry>,
    val vars: List<ScriptVarEntry>,
    /**
     * Save blocks, on the same terms as the two lists above: absolute coming down, only what
     * changed going up. Empty by default so a body that ends before the block count still
     * decodes as the seat it is.
     */
    val blocks: List<SaveBlockEntry> = emptyList(),
)

private val ScriptFlagEntryCodec: Codec<ScriptFlagEntry> =
    object : PacketCodec<ScriptFlagEntry>() {
      override fun CodecScope<ScriptFlagEntry>.body(): ScriptFlagEntry {
        val id = field(S16LE) { it.id }
        val on = field(Bool) { it.on }
        return ScriptFlagEntry(id, on)
      }
    }

private val ScriptVarEntryCodec: Codec<ScriptVarEntry> =
    object : PacketCodec<ScriptVarEntry>() {
      override fun CodecScope<ScriptVarEntry>.body(): ScriptVarEntry {
        val id = field(S16LE) { it.id }
        val value = field(S16LE) { it.value }
        return ScriptVarEntry(id, value)
      }
    }

private val SaveBlockEntryCodec: Codec<SaveBlockEntry> =
    object : PacketCodec<SaveBlockEntry>() {
      override fun CodecScope<SaveBlockEntry>.body(): SaveBlockEntry {
        val id = field(U8) { it.id }
        val data = field(bytesPrefixed(U16LE)) { it.data }
        return SaveBlockEntry(id, data)
      }
    }

object ScriptStatePacketCodec : PacketCodec<ScriptStatePacket>() {
  override fun CodecScope<ScriptStatePacket>.body(): ScriptStatePacket {
    val flags = field(ScriptFlagEntryCodec.listPrefixed(U16LE)) { it.flags }
    val vars = field(ScriptVarEntryCodec.listPrefixed(U16LE)) { it.vars }
    val blocks = field(SaveBlockEntryCodec.listPrefixed(U8)) { it.blocks }
    return ScriptStatePacket(flags, vars, blocks)
  }
}
