package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.*
import de.fiereu.openmmo.common.ContestConditions
import de.fiereu.openmmo.common.ContestType

/** What an engine-run scene left each party member as. */
data class BattleOutcomeMove(val id: Int, val pp: Int)

data class BattleOutcomeMon(
    val id: Long,
    val level: Int,
    val xp: Int,
    val hp: Int,
    val moves: List<BattleOutcomeMove>,
    /**
     * What a contest or a Poffin left on this monster. A battle leaves all three exactly as
     * they were seated, so the client sends what the engine currently holds on every report
     * rather than trying to say which scene ran.
     */
    val conditions: ContestConditions = ContestConditions.NONE,
    val sheen: Int = 0,
    val superContestRibbons: Long = 0L,
)

data class BattleOutcomePacket(
    val mons: List<BattleOutcomeMon>,
)

private val BattleOutcomeMonCodec =
    object : PacketCodec<BattleOutcomeMon>() {
      override fun CodecScope<BattleOutcomeMon>.body(): BattleOutcomeMon {
        val id = field(S64LE) { it.id }
        val level = field(U8) { it.level }
        val xp = field(S32LE) { it.xp }
        val hp = field(S16LE) { it.hp.toShort() }
        val m1 = field(U16LE) { it.moves[0].id }
        val p1 = field(U8) { it.moves[0].pp }
        val m2 = field(U16LE) { it.moves[1].id }
        val p2 = field(U8) { it.moves[1].pp }
        val m3 = field(U16LE) { it.moves[2].id }
        val p3 = field(U8) { it.moves[2].pp }
        val m4 = field(U16LE) { it.moves[3].id }
        val p4 = field(U8) { it.moves[3].pp }
        val conditions = ContestType.entries.map { t -> field(U8) { it.conditions[t] and 0xFF } }
        val sheen = field(U8) { it.sheen and 0xFF }
        val superContestRibbons = field(S64LE) { it.superContestRibbons }
        return BattleOutcomeMon(
            id,
            level,
            xp,
            hp.toInt(),
            listOf(
                BattleOutcomeMove(m1, p1),
                BattleOutcomeMove(m2, p2),
                BattleOutcomeMove(m3, p3),
                BattleOutcomeMove(m4, p4),
            ),
            conditions = ContestConditions.ofList(conditions),
            sheen = sheen,
            superContestRibbons = superContestRibbons,
        )
      }
    }

object BattleOutcomePacketCodec : PacketCodec<BattleOutcomePacket>() {
  override fun CodecScope<BattleOutcomePacket>.body(): BattleOutcomePacket {
    val mons = field(BattleOutcomeMonCodec.listPrefixed(U8)) { it.mons }
    return BattleOutcomePacket(mons)
  }
}
