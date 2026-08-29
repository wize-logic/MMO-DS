package de.fiereu.openmmo.net.game.packets.guild

import de.fiereu.bytecodec.*

/**
 * the official client `the official client` as `the official client` and `the official client` walk it. The int after the message is
 * constructor argument 6; `d81` does not store it.
 */
data class GuildProfileData(
    val guildId: Long,
    val name: String,
    val tag: String,
    val foundedAt: Int,
    val message: String,
    val unknown: Int,
    val perms: List<Short>,
    val expiry: Int,
    val rankNames: List<String>,
)

data class GuildMembershipPacket(
    val inGuild: Boolean,
    val profile: GuildProfileData?,
)

object GuildProfileDataCodec : PacketCodec<GuildProfileData>() {
  override fun CodecScope<GuildProfileData>.body(): GuildProfileData {
    val guildId = field(S64LE, GuildProfileData::guildId)
    val name = field(Utf16LeNullTerminated, GuildProfileData::name)
    val tag = field(Utf16LeNullTerminated, GuildProfileData::tag)
    val foundedAt = field(S32LE, GuildProfileData::foundedAt)
    val message = field(Utf16LeNullTerminated, GuildProfileData::message)
    val unknown = field(S32LE, GuildProfileData::unknown)
    val perms = field(S16LE.repeat(5), GuildProfileData::perms)
    val expiry = field(S32LE, GuildProfileData::expiry)
    val rankCount = field(U8) { it.rankNames.size }
    val rankNames =
        List(rankCount and 0xff) { i -> field(Utf16LeNullTerminated) { it.rankNames[i] } }
    return GuildProfileData(
        guildId,
        name,
        tag,
        foundedAt,
        message,
        unknown,
        perms,
        expiry,
        rankNames,
    )
  }
}

object GuildMembershipPacketCodec : PacketCodec<GuildMembershipPacket>() {
  override fun CodecScope<GuildMembershipPacket>.body(): GuildMembershipPacket {
    val inGuild = field(Bool, GuildMembershipPacket::inGuild)
    val profile = if (inGuild) field(GuildProfileDataCodec) { it.profile!! } else null
    return GuildMembershipPacket(inGuild, profile)
  }
}
