package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.*

/**
 * the official client `f/Prn` as `the official client` reads it. The first byte is consumed and discarded by the
 * constructor; `lastSeen` is `Prn.K5`. The four shorts are `ne0.nV`'s length.
 */
data class FriendAppearance(
    val name: String,
    val unk0: Byte,
    val lastSeen: Int,
    val kind: Byte,
    val packedSlots: Byte,
    val sprite: List<Short>,
)

/** One `f/lPt6` row. `unknown` is `IL1`; nothing in the official client names it. */
data class FriendListEntry(
    val player: Long,
    val unknown: Int,
    val online: Boolean,
    val appearance: FriendAppearance,
)

data class FriendListPacket(
    val mode: Int,
    val entries: List<FriendListEntry>,
)

private val FriendAppearanceCodec: Codec<FriendAppearance> =
    object : PacketCodec<FriendAppearance>() {
      override fun CodecScope<FriendAppearance>.body(): FriendAppearance {
        val name = field(Utf16LeNullTerminated) { it.name }
        val unk0 = field(S8) { it.unk0 }
        val lastSeen = field(S32LE) { it.lastSeen }
        val kind = field(S8) { it.kind }
        val packedSlots = field(S8) { it.packedSlots }
        val sprite = field(S16LE.repeat(4)) { it.sprite }
        return FriendAppearance(name, unk0, lastSeen, kind, packedSlots, sprite)
      }
    }

private val FriendListEntryCodec: Codec<FriendListEntry> =
    object : PacketCodec<FriendListEntry>() {
      override fun CodecScope<FriendListEntry>.body(): FriendListEntry {
        val player = field(S64LE) { it.player }
        val unknown = field(S32LE) { it.unknown }
        val online = field(U8) { if (it.online) 1 else 0 } == 1
        val appearance = field(FriendAppearanceCodec) { it.appearance }
        return FriendListEntry(player, unknown, online, appearance)
      }
    }

object FriendListPacketCodec : PacketCodec<FriendListPacket>() {
  override fun CodecScope<FriendListPacket>.body(): FriendListPacket {
    val mode = field(U8) { it.mode }
    val entries = field(FriendListEntryCodec.listPrefixed(U8)) { it.entries }
    return FriendListPacket(mode, entries)
  }
}
