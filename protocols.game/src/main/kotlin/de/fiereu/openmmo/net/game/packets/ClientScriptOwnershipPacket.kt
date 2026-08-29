package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.Bool
import de.fiereu.bytecodec.CodecScope
import de.fiereu.bytecodec.PacketCodec

/** Who runs a field script for this session. */
data class ClientScriptOwnershipPacket(val clientRunsFieldScripts: Boolean)

object ClientScriptOwnershipPacketCodec : PacketCodec<ClientScriptOwnershipPacket>() {
  override fun CodecScope<ClientScriptOwnershipPacket>.body(): ClientScriptOwnershipPacket {
    val runs = field(Bool) { it.clientRunsFieldScripts }
    return ClientScriptOwnershipPacket(runs)
  }
}
