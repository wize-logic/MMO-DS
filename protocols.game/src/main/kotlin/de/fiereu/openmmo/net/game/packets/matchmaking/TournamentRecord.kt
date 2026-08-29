package de.fiereu.openmmo.net.game.packets.matchmaking

import de.fiereu.bytecodec.*

/** One tournament, as the matchmaking window lists it. */
data class TournamentRecord(
    val id: Long,
    val kind: Byte,
    val type: Byte,
    val name: String,
    val capacity: Short,
    val formatId: Short,
    val startTime: Long,
    val byte1: Byte,
    val int1: Int,
    val byte2: Byte,
    val flag1: Boolean,
    val flag2: Boolean,
)

val TournamentRecordCodec: Codec<TournamentRecord> =
    object : PacketCodec<TournamentRecord>() {
      override fun CodecScope<TournamentRecord>.body(): TournamentRecord {
        val id = field(S64LE) { it.id }
        val kind = field(S8) { it.kind }
        val type = field(S8) { it.type }
        val name = field(Utf16LeNullTerminated) { it.name }
        val capacity = field(S16LE) { it.capacity }
        val formatId = field(S16LE) { it.formatId }
        reserved(byte = 0)
        val startTime = field(S64LE) { it.startTime }
        val byte1 = field(S8) { it.byte1 }
        val int1 = field(S32LE) { it.int1 }
        val byte2 = field(S8) { it.byte2 }
        val flag1 = field(Bool) { it.flag1 }
        val flag2 = field(Bool) { it.flag2 }
        reserved(byte = 0)
        return TournamentRecord(
            id,
            kind,
            type,
            name,
            capacity,
            formatId,
            startTime,
            byte1,
            int1,
            byte2,
            flag1,
            flag2,
        )
      }
    }
