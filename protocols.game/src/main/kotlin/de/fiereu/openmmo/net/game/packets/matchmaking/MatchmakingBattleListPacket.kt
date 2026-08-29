package de.fiereu.openmmo.net.game.packets.matchmaking

import de.fiereu.bytecodec.*

/** How a trainer is drawn on a live-battle row: their name and the four appearance slots. */
data class BattleListTrainer(
    val name: String,
    val gender: Byte,
    val id: Int,
    val kind: Byte,
    val palettePack: Byte,
    val slots: List<Short>,
)

private val BattleListTrainerCodec: Codec<BattleListTrainer> =
    object : PacketCodec<BattleListTrainer>() {
      override fun CodecScope<BattleListTrainer>.body(): BattleListTrainer {
        val name = field(Utf16LeNullTerminated) { it.name }
        val gender = field(S8) { it.gender }
        val id = field(S32LE) { it.id }
        val kind = field(S8) { it.kind }
        val palettePack = field(S8) { it.palettePack }
        val slots = field(S16LE.repeat(4)) { it.slots }
        return BattleListTrainer(name, gender, id, kind, palettePack, slots)
      }
    }

/**
 * One battle in progress, as the window's Matches tab draws it: a queue, two scalars it prints
 * as a rating and a duration, and the trainers on each side.
 */
data class BattleListRow(
    val battleId: Int,
    val queueId: Byte,
    val int1: Int,
    val int2: Int,
    val trainers: List<BattleListTrainer>,
)

private val BattleListRowCodec: Codec<BattleListRow> =
    object : PacketCodec<BattleListRow>() {
      override fun CodecScope<BattleListRow>.body(): BattleListRow {
        val battleId = field(S32LE) { it.battleId }
        val queueId = field(S8) { it.queueId }
        val int1 = field(S32LE) { it.int1 }
        val int2 = field(S32LE) { it.int2 }
        val trainers = field(BattleListTrainerCodec.listPrefixed(U8)) { it.trainers }
        return BattleListRow(battleId, queueId, int1, int2, trainers)
      }
    }

/** The battles in progress, for one category of the window's Matches tab. */
data class MatchmakingBattleListPacket(
    val total: Short,
    val category: Byte,
    val rows: List<BattleListRow>,
)

object MatchmakingBattleListPacketCodec : PacketCodec<MatchmakingBattleListPacket>() {
  override fun CodecScope<MatchmakingBattleListPacket>.body(): MatchmakingBattleListPacket {
    val total = field(S16LE) { it.total }
    val category = field(S8) { it.category }
    val rows = field(BattleListRowCodec.listPrefixed(U8)) { it.rows }
    return MatchmakingBattleListPacket(total, category, rows)
  }
}
