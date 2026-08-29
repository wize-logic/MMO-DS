package de.fiereu.openmmo.net.game.packets.battle.moves

import de.fiereu.bytecodec.*

/** The slot a move learn carries when there was no room for it, or when the player refused it. */
const val MOVE_LEARN_NO_SLOT: Byte = -1

/**
 * One move a monster just gained. [slot] is the move slot it went into, or
 * [MOVE_LEARN_NO_SLOT] when every slot was taken, then the client asks which move to drop and
 * answers with a [MoveLearnReplyPacket].
 */
data class MoveLearnPromptPacket(
    val entityId: Long,
    val slot: Byte,
    val moveId: Short,
)

object MoveLearnPromptPacketCodec : PacketCodec<MoveLearnPromptPacket>() {
  override fun CodecScope<MoveLearnPromptPacket>.body(): MoveLearnPromptPacket {
    val entityId = field(S64LE) { it.entityId }
    val slot = field(S8) { it.slot }
    val moveId = field(S16LE) { it.moveId }
    return MoveLearnPromptPacket(entityId, slot, moveId)
  }
}
