package de.fiereu.openmmo.net.game.packets.guild

import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec

/**
 * s2c 0x81. the official client `the official client` is just `gs()`, the same profile 0x80 carries
 * when the membership flag is set.
 */
data class GuildProfileSyncPacket(
    val profile: GuildProfileData,
)

object GuildProfileSyncPacketCodec : PacketCodec<GuildProfileSyncPacket>() {
  override fun CodecScope<GuildProfileSyncPacket>.body(): GuildProfileSyncPacket {
    val profile = field(GuildProfileDataCodec, GuildProfileSyncPacket::profile)
    return GuildProfileSyncPacket(profile)
  }
}
