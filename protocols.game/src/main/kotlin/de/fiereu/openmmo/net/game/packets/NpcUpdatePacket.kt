package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.*

/** Opcode 0x11 (s2c). Seats a whole pose on one entity at once. */
data class NpcUpdatePacket(
    val entityId: Long,
    val regionId: Int,
    val bankId: Int,
    val mapId: Int,
    val x: Int,
    val y: Int,
    val movementMode: Int,
    val heading: Int,
)

object NpcUpdatePacketCodec : PacketCodec<NpcUpdatePacket>() {
  override fun CodecScope<NpcUpdatePacket>.body(): NpcUpdatePacket {
    val entityId = field(S64LE) { it.entityId }
    val regionId = field(U8) { it.regionId }
    val bankId = field(U8) { it.bankId }
    val mapId = field(U8) { it.mapId }
    val x = field(U16LE) { it.x }
    val y = field(U16LE) { it.y }
    val movementMode = field(U8) { it.movementMode }
    val heading = field(U8) { it.heading }
    return NpcUpdatePacket(entityId, regionId, bankId, mapId, x, y, movementMode, heading)
  }
}
