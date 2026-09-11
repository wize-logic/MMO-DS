package de.fiereu.openmmo.server.game.services

import de.fiereu.bytecodec.CodecException
import de.fiereu.network.PacketEvent
import de.fiereu.network.SessionAttribute
import de.fiereu.openmmo.net.game.packets.OfflineImportResultPacket
import de.fiereu.openmmo.net.game.packets.OfflineImportStatus
import de.fiereu.openmmo.net.game.packets.OfflineSaveReportPacket
import de.fiereu.openmmo.server.game.offline.ImportOutcome
import de.fiereu.openmmo.server.game.offline.ImportRequest
import de.fiereu.openmmo.server.game.offline.OfflineImportRequests
import de.fiereu.openmmo.server.game.offline.OfflineImportService
import de.fiereu.openmmo.server.game.offline.OfflineSaveWire
import de.fiereu.openmmo.server.game.offline.verify.ChainOffer
import de.fiereu.openmmo.server.game.offline.verify.ChainReading
import de.fiereu.openmmo.server.game.offline.verify.ExportAnchorService
import de.fiereu.openmmo.server.game.offline.verify.ExportImageWire
import de.fiereu.openmmo.server.game.offline.verify.ExportReading
import de.fiereu.openmmo.server.game.offline.verify.KeepOutcome
import de.fiereu.openmmo.server.game.offline.verify.OfferOutcome
import de.fiereu.openmmo.server.game.offline.verify.ReplayVerificationService
import de.fiereu.openmmo.server.game.offline.verify.SessionChainWire
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.ImportRepository
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/** The wire end of an offline save coming back online, and of the play behind it. */
@Singleton
class OfflineImportSessionService
@Inject
constructor(
    private val imports: OfflineImportService,
    private val importRecords: ImportRepository,
    private val verification: ReplayVerificationService,
    private val anchors: ExportAnchorService,
    private val characters: CharacterStore,
) {

  suspend fun onSaveReport(event: PacketEvent<OfflineSaveReportPacket>) {
    val session = event.session
    val state = session.attributes[PLAYER_STATE]
    val characterId = state?.characterId
    if (characterId == null) {
      answer(event, OfflineImportStatus.REFUSED, "pick a character before offering it a save")
      return
    }
    val msg = event.packet

    // A first piece always starts a fresh report, so a client that gave up half way through one can
    // simply begin again rather than being stuck behind its own remains. The pieces live on the
    // session, so a client that goes away mid-report leaves nothing behind at all.
    val incoming =
        if (msg.sequence == 0) PartialReport().also { session.attributes[PARTIAL_REPORT] = it }
        else session.attributes[PARTIAL_REPORT]
    if (incoming == null || incoming.next != msg.sequence) {
      session.attributes.remove(PARTIAL_REPORT)
      answer(event, OfflineImportStatus.REFUSED, "that save arrived out of order; try again")
      return
    }
    if (incoming.bytes.size() + msg.chunk.size > MAX_BLOB_BYTES) {
      session.attributes.remove(PARTIAL_REPORT)
      log.warn {
        "char=$characterId offered more than $MAX_BLOB_BYTES bytes on the offline channel"
      }
      answer(event, OfflineImportStatus.REFUSED, "that is larger than anything this game writes")
      return
    }
    incoming.bytes.write(msg.chunk)
    incoming.next = msg.sequence + 1
    if (!msg.last) return
    session.attributes.remove(PARTIAL_REPORT)

    val joined = incoming.bytes.toByteArray()
    if (SessionChainWire.looksLikeChain(joined)) {
      onChain(event, characterId, joined)
      return
    }
    if (ExportImageWire.looksLikeExport(joined)) {
      onExport(event, characterId, joined)
      return
    }
    if (!SessionChainWire.named(joined, OfflineSaveWire.MAGIC)) {
      log.warn { "char=$characterId offered ${joined.size} bytes that name no format this reads" }
      answer(event, OfflineImportStatus.REFUSED, "that is not a save this game wrote")
      return
    }
    // A report is a fraction of the channel's ceiling; the ceiling is the chain's. Held here rather
    // than at the join because the join does not yet know which of the two it has.
    if (joined.size > MAX_REPORT_BYTES) {
      log.warn { "char=$characterId offered a ${joined.size} byte save report" }
      answer(event, OfflineImportStatus.REFUSED, "that is larger than any save this game writes")
      return
    }

    val wire =
        try {
          OfflineSaveWire.decode(joined)
        } catch (e: CodecException) {
          log.warn {
            "char=$characterId offered ${joined.size} bytes that are not a save report: ${e.message}"
          }
          answer(event, OfflineImportStatus.REFUSED, e.message ?: "that is not a save report")
          return
        } catch (e: IndexOutOfBoundsException) {
          // A report that ends in the middle of a field. Same answer, different throw.
          log.warn { "char=$characterId offered a save report that ends early: ${e.message}" }
          answer(event, OfflineImportStatus.REFUSED, "that save report ends in the middle")
          return
        }

    // The character's own region, off the record, and never the session's live map address.
    val stored = characters.getOrLoadCharacter(characterId)
    if (stored == null) {
      answer(event, OfflineImportStatus.REFUSED, "that character could not be read")
      return
    }
    val save = OfflineImportRequests.toSave(wire, stored.info.positionRegionId)
    val outcome =
        imports.import(
            ImportRequest(
                characterId = characterId,
                save = save,
                saveSha256 = wire.saveSha256,
                clientRevision = wire.clientRevision,
            ))
    when (outcome) {
      is ImportOutcome.Landed -> {
        answer(
            event,
            OfflineImportStatus.LANDED,
            "your save is this character now: ${outcome.record.partyCount} in the party," +
                " ${outcome.record.boxCount} in the boxes. ${whyMarked(characterId)}",
            outcome.verdicts.map { it.toString() },
            // Asking for the session records only when there is something here that could replay
            // them. A client that is told no keeps its recordings and its uplink, and the import
            // stays marked, which is exactly where it already was.
            wantsChain = verification.replays())
        // The window is still the character the save replaced. The client leaves for the launcher
        // on this answer and the next Play reads the record back; this is the sentence the player
        // sees on the way out.
        session.send(
            notice(
                "Your save is this character now. The game returns to the launcher; press Play" +
                    " to carry on as it."))
      }
      is ImportOutcome.TryAgain -> answer(event, OfflineImportStatus.TRY_AGAIN, outcome.why)
      is ImportOutcome.Refused -> answer(event, OfflineImportStatus.REFUSED, outcome.why)
    }
  }

  /** The play behind the newest save this character brought online, offered to be replayed. */
  private suspend fun onChain(
      event: PacketEvent<OfflineSaveReportPacket>,
      characterId: Long,
      blob: ByteArray,
  ) {
    when (val read = SessionChainWire.decode(blob)) {
      is ChainReading.Unreadable -> {
        log.info { "char=$characterId offered session records that will not read: ${read.why}" }
        answer(event, OfflineImportStatus.CHECK_DECLINED, read.why)
      }
      is ChainReading.Read -> {
        val import = importRecords.newestStanding(characterId)
        if (import == null) {
          answer(
              event,
              OfflineImportStatus.CHECK_DECLINED,
              "there is no save of yours here for these sessions to be about")
          return
        }
        val outcome =
            verification.offer(
                ChainOffer(
                    characterId = characterId,
                    importId = import.id,
                    chain = read.chain,
                    uploadedSaveSha256 = import.saveSha256))
        when (outcome) {
          is OfferOutcome.Kept ->
              answer(
                  event,
                  OfflineImportStatus.CHAIN_KEPT,
                  "your offline play is on file; /verify <party slot> asks for one monster to be" +
                      " checked, /verify all for everything")
          is OfferOutcome.Unverifiable ->
              answer(event, OfflineImportStatus.CHECK_DECLINED, outcome.why)
        }
      }
    }
  }

  /** The copy a session wrote out for offline play, sent as it leaves. */
  private suspend fun onExport(
      event: PacketEvent<OfflineSaveReportPacket>,
      characterId: Long,
      blob: ByteArray,
  ) {
    when (val read = ExportImageWire.decode(blob)) {
      is ExportReading.Unreadable -> {
        log.info { "char=$characterId sent an offline copy that will not read: ${read.why}" }
        answer(event, OfflineImportStatus.EXPORT_DECLINED, read.why)
      }
      is ExportReading.Read ->
          when (val kept = anchors.keep(characterId, read.image)) {
            is KeepOutcome.Kept ->
                answer(
                    event,
                    OfflineImportStatus.EXPORT_KEPT,
                    "the play after it can be checked from here once the copy has been looked at")
            is KeepOutcome.Declined -> answer(event, OfflineImportStatus.EXPORT_DECLINED, kept.why)
          }
    }
  }

  /** Why what just landed is marked, in one sentence the player reads on the way out. */
  private suspend fun whyMarked(characterId: Long): String {
    if (!verification.replays()) {
      return "What it brought is marked as caught offline, and this server does not replay offline" +
          " play, so the mark stays."
    }
    val anchors = anchors.anchorsOf(characterId).size
    if (anchors == 0) {
      return "What it brought is marked as caught offline until the play behind it is checked." +
          " This character has never carried a game out from here, so only play from a new file" +
          " can be checked."
    }
    return "What it brought is marked as caught offline until the play behind it is checked" +
        " against the $anchors game(s) this character carried out from here."
  }

  private fun answer(
      event: PacketEvent<OfflineSaveReportPacket>,
      status: OfflineImportStatus,
      message: String,
      notes: List<String> = emptyList(),
      wantsChain: Boolean = false,
  ) {
    event.session.send(OfflineImportResultPacket(status, message, notes, wantsChain))
  }

  private companion object {
    /**
     * Twice the largest report this client writes. A Platinum save is 512 KiB of which the report
     * carries the parts that mean something, 546 monsters, a bag, the Pokedex and thirteen other
     * blocks, so a quarter of a megabyte is generous and still bounded.
     */
    const val MAX_REPORT_BYTES = 256 * 1024

    /** And what the channel itself will hold, which is the larger of the two blobs on it. */
    const val MAX_BLOB_BYTES = 16 * 1024 * 1024
  }
}

/** The pieces of one report, on the session that is sending them. */
class PartialReport {
  val bytes = java.io.ByteArrayOutputStream()
  var next = 0
}

/** Held on the session so a client that disconnects mid-report leaves nothing behind. */
val PARTIAL_REPORT = SessionAttribute.of<PartialReport>("offlineSaveReport")
