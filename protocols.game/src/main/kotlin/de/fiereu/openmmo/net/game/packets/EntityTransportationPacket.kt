package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.S64LE
import de.fiereu.bytecodec.S8

/** What an entity is riding, sent when it changes. */
data class EntityTransportationPacket(
    val entityId: Long,
    val transportation: Byte,
)

const val TRANSPORTATION_NONE: Byte = 0x00
const val TRANSPORTATION_SURFING: Byte = 0x01
const val TRANSPORTATION_BIKE: Byte = 0x02
const val TRANSPORTATION_DIVING: Byte = 0x10

object EntityTransportationPacketCodec : PacketCodec<EntityTransportationPacket>() {
  override fun CodecScope<EntityTransportationPacket>.body(): EntityTransportationPacket {
    val entityId = field(S64LE) { it.entityId }
    val transportation = field(S8) { it.transportation }
    return EntityTransportationPacket(entityId, transportation)
  }
}
