package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec
import de.fiereu.bytecodec.U16LE

/** The Y-registered key item, [itemId] 0 for none. */
data class RegisteredItemPacket(
    val itemId: Int,
)

object RegisteredItemPacketCodec : PacketCodec<RegisteredItemPacket>() {
  override fun CodecScope<RegisteredItemPacket>.body(): RegisteredItemPacket {
    val itemId = field(U16LE) { it.itemId }
    return RegisteredItemPacket(itemId)
  }
}
