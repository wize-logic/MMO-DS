package de.fiereu.openmmo.net.game.packets.guild

import de.fiereu.bytecodec.Bool
import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.S32LE
import de.fiereu.bytecodec.S64LE
import de.fiereu.bytecodec.S8

/**
 * s2c 0x83. the official client `the official client` is one `f/uI0` member row: rank, id, joined-at, QL1, then the online
 * byte.
 */
data class GuildMemberAddPacket(
    val rank: Byte,
    val entityId: Long,
    val joinedAt: Int,
    val appearance: GuildAppearance,
    val online: Boolean,
)

object GuildMemberAddPacketCodec : PacketCodec<GuildMemberAddPacket>() {
  override fun CodecScope<GuildMemberAddPacket>.body(): GuildMemberAddPacket {
    val rank = field(S8, GuildMemberAddPacket::rank)
    val entityId = field(S64LE, GuildMemberAddPacket::entityId)
    val joinedAt = field(S32LE, GuildMemberAddPacket::joinedAt)
    val appearance = field(GuildAppearanceCodec, GuildMemberAddPacket::appearance)
    val online = field(Bool, GuildMemberAddPacket::online)
    return GuildMemberAddPacket(rank, entityId, joinedAt, appearance, online)
  }
}
