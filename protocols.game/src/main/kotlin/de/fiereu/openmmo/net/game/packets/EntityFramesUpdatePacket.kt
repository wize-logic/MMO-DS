package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.*

data class EntityFramesUpdatePacket(
    val entityId: Long,
    val frames: GroupListFrameSet,
)

object EntityFramesUpdatePacketCodec : PacketCodec<EntityFramesUpdatePacket>() {
  override fun CodecScope<EntityFramesUpdatePacket>.body(): EntityFramesUpdatePacket {
    val entityId = field(S64LE) { it.entityId }
    val frames = field(GroupListFrameSetCodec) { it.frames }
    return EntityFramesUpdatePacket(entityId, frames)
  }
}
