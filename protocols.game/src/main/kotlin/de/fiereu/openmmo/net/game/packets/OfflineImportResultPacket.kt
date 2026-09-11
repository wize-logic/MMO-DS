package de.fiereu.openmmo.net.game.packets

import de.fiereu.bytecodec.*
import java.nio.charset.StandardCharsets

/** What the server did with a save, or with the evidence behind one, that it was offered. */
enum class OfflineImportStatus(val wire: Int) {
  /**
   * The save is this character now. [OfflineImportResultPacket.notes] is what changed on the way.
   */
  LANDED(0),

  /**
   * Something is mid-flight that the replacement would tear in half. Press it again in a moment.
   */
  TRY_AGAIN(1),

  /** A save no honest playthrough could have written. The file on disk is untouched. */
  REFUSED(2),

  /**
   * The session records behind the save that just landed are queued to be replayed.
   * [OfflineImportResultPacket.message] names what it cost, if anything.
   */
  CHECK_QUEUED(3),

  /**
   * They will not be. The save landed all the same and stays marked, which is the state it was
   * already in, so this is never a failure of the import, only the absence of the one thing that
   * could have lifted the mark.
   */
  CHECK_DECLINED(4),

  /**
   * The offline copy a session sent as it left is kept here, to be the point the play after it is
   * checked from. It is checked in the background; nothing about the session waits on it.
   */
  EXPORT_KEPT(5),

  /**
   * It was not kept: this server checks nothing, or the bytes were not a save image. The copy on
   * the player's disk is theirs either way.
   */
  EXPORT_DECLINED(6),

  /**
   * The session records behind the save that just landed are on file. Nothing is replayed until the
   * player asks for a monster, or for everything, and pays for the stretch that takes.
   */
  CHAIN_KEPT(7);

  companion object {
    fun byWire(value: Int): OfflineImportStatus? = entries.firstOrNull { it.wire == value }
  }
}

/** The one answer to a whole [OfflineSaveReportPacket] stream. */
data class OfflineImportResultPacket(
    val status: OfflineImportStatus,
    val message: String,
    val notes: List<String> = emptyList(),
    /** Set on a landed save when this server would replay the session records behind it. */
    val wantsChain: Boolean = false,
)

private val Text: Codec<String> = stringPrefixed(U16LE, StandardCharsets.UTF_8)

object OfflineImportResultPacketCodec : PacketCodec<OfflineImportResultPacket>() {
  override fun CodecScope<OfflineImportResultPacket>.body(): OfflineImportResultPacket {
    val status = field(U8) { it.status.wire }
    val message = field(Text) { it.message }
    val notes = field(Text.listPrefixed(U16LE)) { it.notes }
    val wantsChain = field(Bool) { it.wantsChain }
    return OfflineImportResultPacket(
        OfflineImportStatus.byWire(status)
            ?: throw MalformedPacketException("unknown offline import status: $status"),
        message,
        notes,
        wantsChain)
  }
}
