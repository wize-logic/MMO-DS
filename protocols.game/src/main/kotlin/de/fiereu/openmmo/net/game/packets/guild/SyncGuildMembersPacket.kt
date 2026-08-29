package de.fiereu.openmmo.net.game.packets.guild

import de.fiereu.bytecodec.*

/**
 * the official client `the official client` as `f/uI0` and `f/Od0` walk it. Same shape as a friend
 * `Prn`: utf16 name, a discarded byte, last-seen, kind, packed slots, four shorts.
 */
data class GuildAppearance(
    val name: String,
    val unk0: Byte,
    val lastSeen: Int,
    val kind: Byte,
    val packedSlots: Byte,
    val sprite: List<Short>,
)

/** One `f/KM` as `the official client` reads it: rank, id, joined-at, QL1, online. */
data class GuildMemberEntry(
    val rank: Byte,
    val entityId: Long,
    val joinedAt: Int,
    val appearance: GuildAppearance,
    val online: Boolean,
)

data class SyncGuildMembersPacket(
    val replace: Boolean,
    val members: List<GuildMemberEntry>,
)

internal object GuildAppearanceCodec : PacketCodec<GuildAppearance>() {
  override fun CodecScope<GuildAppearance>.body(): GuildAppearance {
    val name = field(Utf16LeNullTerminated, GuildAppearance::name)
    val unk0 = field(S8, GuildAppearance::unk0)
    val lastSeen = field(S32LE, GuildAppearance::lastSeen)
    val kind = field(S8, GuildAppearance::kind)
    val packedSlots = field(S8, GuildAppearance::packedSlots)
    val sprite = field(S16LE.repeat(4), GuildAppearance::sprite)
    return GuildAppearance(name, unk0, lastSeen, kind, packedSlots, sprite)
  }
}

internal object GuildMemberEntryCodec : PacketCodec<GuildMemberEntry>() {
  override fun CodecScope<GuildMemberEntry>.body(): GuildMemberEntry {
    val rank = field(S8, GuildMemberEntry::rank)
    val entityId = field(S64LE, GuildMemberEntry::entityId)
    val joinedAt = field(S32LE, GuildMemberEntry::joinedAt)
    val appearance = field(GuildAppearanceCodec, GuildMemberEntry::appearance)
    val online = field(Bool, GuildMemberEntry::online)
    return GuildMemberEntry(rank, entityId, joinedAt, appearance, online)
  }
}

object SyncGuildMembersPacketCodec : PacketCodec<SyncGuildMembersPacket>() {
  override fun CodecScope<SyncGuildMembersPacket>.body(): SyncGuildMembersPacket {
    val replace = field(Bool, SyncGuildMembersPacket::replace)
    val members = field(GuildMemberEntryCodec.listPrefixed(U8), SyncGuildMembersPacket::members)
    return SyncGuildMembersPacket(replace, members)
  }
}
