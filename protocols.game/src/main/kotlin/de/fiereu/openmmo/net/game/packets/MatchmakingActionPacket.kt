package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.*

/**
 * The three actions the queue can send, as the wire byte names them. the official client's `the
 * official client` writes the `pm1` byte and then the tail that byte's own case takes: 0 an int, 1
 * a long and a short, 2 nothing at all.
 */
sealed class MatchmakingActionKind {
  abstract val actionId: Byte
}

data class TeleportAction(override val actionId: Byte) : MatchmakingActionKind()

data class SelectTierAction(override val actionId: Byte, val tierId: Int) : MatchmakingActionKind()

data class SelectTargetAction(
    override val actionId: Byte,
    val targetEntityId: Long,
    val targetSlot: Short,
) : MatchmakingActionKind()

private val MatchmakingActionCodec: Codec<MatchmakingActionKind> =
    object : Codec<MatchmakingActionKind> {
      override fun read(buf: ReadBuffer): MatchmakingActionKind {
        val actionId = buf.readByte()
        return when (actionId.toInt()) {
          0 -> {
            var v = 0
            for (i in 0 until 4) {
              v = v or ((buf.readByte().toInt() and 0xFF) shl (i * 8))
            }
            SelectTierAction(actionId, v)
          }

          1 -> {
            var id = 0L
            for (i in 0 until 8) {
              id = id or ((buf.readByte().toLong() and 0xFF) shl (i * 8))
            }
            val lo = buf.readByte().toInt() and 0xFF
            val hi = buf.readByte().toInt() and 0xFF
            SelectTargetAction(actionId, id, ((hi shl 8) or lo).toShort())
          }

          else -> TeleportAction(actionId)
        }
      }

      override fun write(buf: WriteBuffer, value: MatchmakingActionKind) {
        // The byte and the tail are one decision, so the kind writes its own
        // byte rather than whatever a caller happened to put in actionId.
        when (value) {
          is TeleportAction -> buf.writeByte(2)
          is SelectTierAction -> {
            buf.writeByte(0)
            var v = value.tierId
            for (i in 0 until 4) {
              buf.writeByte((v and 0xFF).toByte())
              v = v ushr 8
            }
          }

          is SelectTargetAction -> {
            buf.writeByte(1)
            var id = value.targetEntityId
            for (i in 0 until 8) {
              buf.writeByte((id and 0xFF).toByte())
              id = id ushr 8
            }
            val s = value.targetSlot.toInt()
            buf.writeByte((s and 0xFF).toByte())
            buf.writeByte(((s ushr 8) and 0xFF).toByte())
          }
        }
      }
    }

data class MatchmakingActionPacket(val action: MatchmakingActionKind)

object MatchmakingActionPacketCodec : PacketCodec<MatchmakingActionPacket>() {
  override fun CodecScope<MatchmakingActionPacket>.body(): MatchmakingActionPacket {
    val action = field(MatchmakingActionCodec) { it.action }
    return MatchmakingActionPacket(action)
  }
}
