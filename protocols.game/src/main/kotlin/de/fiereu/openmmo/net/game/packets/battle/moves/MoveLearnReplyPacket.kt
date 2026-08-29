package de.fiereu.openmmo.net.game.packets.battle.moves

import de.fiereu.bytecodec.*

/**
 * The answer to a [MoveLearnPromptPacket]. [slot] is the move slot [moveId] takes over, or
 * [MOVE_LEARN_NO_SLOT] when the player kept the moveset it had.
 */
data class MoveLearnReplyPacket(
    val entityId: Long,
    val slot: Byte,
    val moveId: Short,
)

object MoveLearnReplyPacketCodec : PacketCodec<MoveLearnReplyPacket>() {
  override fun CodecScope<MoveLearnReplyPacket>.body(): MoveLearnReplyPacket {
    val entityId = field(S64LE) { it.entityId }
    val slot = field(S8) { it.slot }
    val moveId = field(S16LE) { it.moveId }
    return MoveLearnReplyPacket(entityId, slot, moveId)
  }
}
