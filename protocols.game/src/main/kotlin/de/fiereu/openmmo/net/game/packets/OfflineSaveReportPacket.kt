package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.*

/** One piece of a blob a client is offering about its offline play. */
data class OfflineSaveReportPacket(
    /** 0 for the first piece, then one up. */
    val sequence: Int,
    /** True on the piece that completes the report. */
    val last: Boolean,
    val chunk: ByteArray,
) {
  override fun equals(other: Any?): Boolean =
      other is OfflineSaveReportPacket &&
          sequence == other.sequence &&
          last == other.last &&
          chunk.contentEquals(other.chunk)

  override fun hashCode(): Int = (sequence * 31 + last.hashCode()) * 31 + chunk.contentHashCode()
}

object OfflineSaveReportPacketCodec : PacketCodec<OfflineSaveReportPacket>() {
  override fun CodecScope<OfflineSaveReportPacket>.body(): OfflineSaveReportPacket {
    val sequence = field(U16LE) { it.sequence }
    val last = field(Bool) { it.last }
    val chunk = field(bytesPrefixed(U16LE)) { it.chunk }
    return OfflineSaveReportPacket(sequence, last, chunk)
  }
}
