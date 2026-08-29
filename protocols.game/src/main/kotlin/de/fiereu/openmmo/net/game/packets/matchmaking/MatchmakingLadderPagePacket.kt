package de.fiereu.openmmo.net.game.packets.matchmaking

import de.fiereu.bytecodec.*

/** How a trainer is drawn on a ladder row: their name and the four appearance slots. */
data class LadderTrainer(
    val name: String,
    val gender: Byte,
    val id: Int,
    val kind: Byte,
    val palettePack: Byte,
    val slots: List<Short>,
)

private val LadderTrainerCodec: Codec<LadderTrainer> =
    object : PacketCodec<LadderTrainer>() {
      override fun CodecScope<LadderTrainer>.body(): LadderTrainer {
        val name = field(Utf16LeNullTerminated) { it.name }
        val gender = field(S8) { it.gender }
        val id = field(S32LE) { it.id }
        val kind = field(S8) { it.kind }
        val palettePack = field(S8) { it.palettePack }
        val slots = field(S16LE.repeat(4)) { it.slots }
        return LadderTrainer(name, gender, id, kind, palettePack, slots)
      }
    }

/** One row of a queue's ladder: who, at what rating, with two scalars the window prints. */
data class LadderRow(
    val entityId: Long,
    val rating: Float,
    val short1: Short,
    val int1: Int,
    val int2: Int,
    val trainer: LadderTrainer,
)

private val LadderRowCodec: Codec<LadderRow> =
    object : PacketCodec<LadderRow>() {
      override fun CodecScope<LadderRow>.body(): LadderRow {
        val entityId = field(S64LE) { it.entityId }
        val rating = field(F32LE) { it.rating }
        val short1 = field(S16LE) { it.short1 }
        val int1 = field(S32LE) { it.int1 }
        val int2 = field(S32LE) { it.int2 }
        val trainer = field(LadderTrainerCodec) { it.trainer }
        return LadderRow(entityId, rating, short1, int1, int2, trainer)
      }
    }

/** One page of one queue's ladder, for one of the player's parties. */
data class MatchmakingLadderPagePacket(
    val total: Int,
    val partySlot: Byte,
    val queueId: Byte,
    val rows: List<LadderRow>,
)

object MatchmakingLadderPagePacketCodec : PacketCodec<MatchmakingLadderPagePacket>() {
  override fun CodecScope<MatchmakingLadderPagePacket>.body(): MatchmakingLadderPagePacket {
    val total = field(S32LE) { it.total }
    val partySlot = field(S8) { it.partySlot }
    val queueId = field(S8) { it.queueId }
    val rows = field(LadderRowCodec.listPrefixed(U16LE)) { it.rows }
    return MatchmakingLadderPagePacket(total, partySlot, queueId, rows)
  }
}
