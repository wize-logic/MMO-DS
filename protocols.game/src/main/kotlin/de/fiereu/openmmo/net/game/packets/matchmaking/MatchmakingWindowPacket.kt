package de.fiereu.openmmo.net.game.packets.matchmaking

import de.fiereu.bytecodec.*

/** A ban or a suspect notice against one species, in one check group. */
data class TierNotice(
    val kind: Byte,
    val speciesId: Short,
    val short2: Short,
    val short3: Short,
    val short4: Short,
    val byte1: Byte,
    val clauseId: Byte,
    val groupId: Byte,
)

private val TierNoticeCodec: Codec<TierNotice> =
    object : PacketCodec<TierNotice>() {
      override fun CodecScope<TierNotice>.body(): TierNotice {
        val kind = field(S8) { it.kind }
        val speciesId = field(S16LE) { it.speciesId }
        val short2 = field(S16LE) { it.short2 }
        val short3 = field(S16LE) { it.short3 }
        val short4 = field(S16LE) { it.short4 }
        val byte1 = field(S8) { it.byte1 }
        val clauseId = field(S8) { it.clauseId }
        val groupId = field(S8) { it.groupId }
        return TierNotice(kind, speciesId, short2, short3, short4, byte1, clauseId, groupId)
      }
    }

/** Whether one queue is open, and when it next runs. */
data class QueueAvailability(
    val queueId: Byte,
    val enabled: Boolean,
    val nextRoundAt: Long,
)

private val QueueAvailabilityCodec: Codec<QueueAvailability> =
    object : PacketCodec<QueueAvailability>() {
      override fun CodecScope<QueueAvailability>.body(): QueueAvailability {
        val queueId = field(S8) { it.queueId }
        val enabled = field(Bool) { it.enabled }
        val nextRoundAt = field(S64LE) { it.nextRoundAt }
        field(S64LE) { 0L }
        return QueueAvailability(queueId, enabled, nextRoundAt)
      }
    }

/** One entry on a queue's ban or suspect list. */
data class BanEntry(
    val entryType: Byte,
    val short1: Short,
    val short2: Short,
    val short3: Short,
    val byte1: Byte,
    val byte2: Byte,
    val flag1: Boolean,
    val flag2: Boolean,
)

private val BanEntryCodec: Codec<BanEntry> =
    object : PacketCodec<BanEntry>() {
      override fun CodecScope<BanEntry>.body(): BanEntry {
        val entryType = field(S8) { it.entryType }
        return when (entryType.toInt()) {
          0 -> {
            val short1 = field(S16LE) { it.short1 }
            val short2 = field(S16LE) { it.short2 }
            val short3 = field(S16LE) { it.short3 }
            BanEntry(entryType, short1, short2, short3, 0, 0, false, false)
          }

          1 -> {
            val short1 = field(S16LE) { it.short1 }
            field(S8) { 0 }
            val byte1 = field(S8) { it.byte1 }
            val byte2 = field(S8) { it.byte2 }
            val flag1 = field(Bool) { it.flag1 }
            val flag2 = field(Bool) { it.flag2 }
            val short2 = field(S16LE) { it.short2 }
            BanEntry(entryType, short1, short2, 0, byte1, byte2, flag1, flag2)
          }

          else -> {
            val short1 = field(S16LE) { it.short1 }
            val short2 = field(S16LE) { it.short2 }
            BanEntry(entryType, short1, short2, 0, 0, 0, false, false)
          }
        }
      }
    }

/**
 * One queue as the window draws its tab: what it is, what it bans, and what it is suspect-testing.
 */
data class QueueDefinition(
    val int1: Int,
    val int2: Int,
    val short1: Short,
    val short2: Short,
    val queueId: Byte,
    val bans: List<BanEntry>,
    val suspects: List<BanEntry>,
)

private val QueueDefinitionCodec: Codec<QueueDefinition> =
    object : PacketCodec<QueueDefinition>() {
      override fun CodecScope<QueueDefinition>.body(): QueueDefinition {
        val int1 = field(S32LE) { it.int1 }
        val int2 = field(S32LE) { it.int2 }
        val short1 = field(S16LE) { it.short1 }
        val short2 = field(S16LE) { it.short2 }
        val queueId = field(S8) { it.queueId }
        val bans = field(BanEntryCodec.listPrefixed(U8)) { it.bans }
        val suspects = field(BanEntryCodec.listPrefixed(U8)) { it.suspects }
        return QueueDefinition(int1, int2, short1, short2, queueId, bans, suspects)
      }
    }

/** Open or close the matchmaking window, and seat everything it draws. */
data class MatchmakingWindowPacket(
    val open: Boolean,
    val tournamentPage: Boolean,
    val tournaments: List<TournamentRecord>,
    val queues: List<QueueAvailability>,
    val definitions: List<QueueDefinition>,
    val notices: List<TierNotice>,
)

object MatchmakingWindowPacketCodec : PacketCodec<MatchmakingWindowPacket>() {
  override fun CodecScope<MatchmakingWindowPacket>.body(): MatchmakingWindowPacket {
    val open = field(U8) { if (it.open) 1 else 0 } == 1
    if (!open) {
      return MatchmakingWindowPacket(
          false, false, emptyList(), emptyList(), emptyList(), emptyList())
    }
    val tournamentPage = field(U8) { if (it.tournamentPage) 1 else 0 } == 1
    var tournaments = emptyList<TournamentRecord>()
    var queues = emptyList<QueueAvailability>()
    var definitions = emptyList<QueueDefinition>()
    if (tournamentPage) {
      tournaments = field(TournamentRecordCodec.listPrefixed(U8)) { it.tournaments }
    } else {
      queues = field(QueueAvailabilityCodec.listPrefixed(U8)) { it.queues }
      definitions = field(QueueDefinitionCodec.listPrefixed(U8)) { it.definitions }
    }
    val notices = field(TierNoticeCodec.listPrefixed(U8)) { it.notices }
    return MatchmakingWindowPacket(open, tournamentPage, tournaments, queues, definitions, notices)
  }
}
