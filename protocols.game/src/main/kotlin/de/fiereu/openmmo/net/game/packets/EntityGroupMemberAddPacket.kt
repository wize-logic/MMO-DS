package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.*

data class EntityGroupMemberAddPacket(
    val member: EntityGroupMember,
)

object EntityGroupMemberAddPacketCodec : PacketCodec<EntityGroupMemberAddPacket>() {
  override fun CodecScope<EntityGroupMemberAddPacket>.body(): EntityGroupMemberAddPacket {
    val member = field(EntityGroupMemberCodec) { it.member }
    return EntityGroupMemberAddPacket(member)
  }
}
