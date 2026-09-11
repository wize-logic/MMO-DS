package de.fiereu.openmmo.server.game.offline.verify

import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.PokemonStat
import de.fiereu.openmmo.server.game.offline.OfflineMonster
import de.fiereu.openmmo.server.game.offline.OfflineSaveWire
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.storage.ExportRepository
import de.fiereu.openmmo.server.game.storage.ExportVerdict
import de.fiereu.openmmo.server.game.storage.StoredCharacter
import de.fiereu.openmmo.server.game.storage.StoredExport
import io.github.oshai.kotlinlogging.KotlinLogging
import java.time.LocalDateTime
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit
import kotlinx.coroutines.withContext

private val log = KotlinLogging.logger {}

/**
 * One monster in the fields an edit to the image would change, and in nothing an honest export
 * changes on the way out and back.
 */
data class MonsterPrint(
    val pid: Int,
    val dexId: Int,
    val form: Int,
    val level: Int,
    val xp: Int,
    val ivs: List<Int>,
    val evs: List<Int>,
    val moves: List<Int>,
    val heldItemId: Int,
    val isEgg: Boolean,
)

/** The character as it was at the moment its session sent the copy. */
data class ExportExpectation(val monsters: List<MonsterPrint>, val money: Int) {
  companion object {
    /** The party and the boxes: the two homes the engine's save has, so the two a report reads. */
    fun of(stored: StoredCharacter): ExportExpectation =
        ExportExpectation(
            (stored.pokemon + stored.pcStorage).map { printOf(it) }, stored.info.money)

    fun printOf(p: Pokemon): MonsterPrint =
        MonsterPrint(
            pid = p.seed,
            dexId = p.dexId,
            form = p.form,
            level = p.level.toInt() and 0xFF,
            xp = p.xp,
            ivs = PokemonStat.entries.map { (p.iVs[it] ?: 0).toInt() and 0xFF },
            evs = PokemonStat.entries.map { (p.eVs[it] ?: 0).toInt() and 0xFF },
            moves = p.moves.map { it.id.toInt() and 0xFFFF }.filter { it != 0 },
            heldItemId = p.heldItemId,
            isEgg = p.isEgg,
        )

    fun printOf(m: OfflineMonster): MonsterPrint =
        MonsterPrint(
            pid = m.pid,
            dexId = m.dexId,
            form = m.form,
            level = m.level,
            xp = m.xp,
            ivs = PokemonStat.entries.map { m.ivs[it] ?: 0 },
            evs = PokemonStat.entries.map { m.evs[it] ?: 0 },
            moves = m.moves.map { it.moveId }.filter { it != 0 },
            heldItemId = m.heldItemId,
            isEgg = m.isEgg,
        )
  }
}

sealed interface KeepOutcome {
  data class Kept(val exportId: Long) : KeepOutcome

  data class Declined(val why: String) : KeepOutcome
}

/** The offline copies sessions send as they leave, and which of them may anchor a replay. */
class ExportAnchorService(
    private val exports: ExportRepository,
    private val characters: CharacterStore,
    private val runner: ReplayRunner,
    private val entityIds: EntityIdService,
    private val scope: CoroutineScope,
    /** Where the boot's blocking wait runs, off the dispatcher the sessions are served on. */
    private val blocking: CoroutineDispatcher,
    cores: Int = 1,
    private val clock: () -> LocalDateTime = LocalDateTime::now,
    /**
     * How a check is put behind the session. The default is the scope; a test hands in a list to
     * run the checks itself, in order, rather than racing a background coroutine.
     */
    private val background: (suspend () -> Unit) -> Unit = { work -> scope.launch { work() } },
) {

  /** One boot at a time per core the replay worker was given. */
  private val slots = Semaphore(cores.coerceAtLeast(1))

  /** Whether a copy sent here would be kept at all, which is a worker being configured. */
  fun keeps(): Boolean = runner.revisions().isNotEmpty()

  /** Keep a copy and put its check behind the session. */
  suspend fun keep(characterId: Long, image: ByteArray): KeepOutcome {
    if (!keeps()) {
      return KeepOutcome.Declined(
          "this server is not checking offline play, so the copy was not kept")
    }
    val stored =
        characters.getOrLoadCharacter(characterId)
            ?: return KeepOutcome.Declined("that character is not on this server")
    val expected = ExportExpectation.of(stored)
    val export =
        StoredExport(
            id = entityIds.newExportId(),
            characterId = characterId,
            exportedAt = clock(),
            sha256 = ProcessReplayRunner.sha256(image),
            verdict = ExportVerdict.PENDING,
        )
    exports.record(export, image)
    val pruned = exports.prune(characterId, KEEP)
    log.info {
      "char=$characterId sent an offline copy ${export.sha256.take(12)}, kept for checking" +
          (if (pruned > 0) "; $pruned older one(s) dropped" else "")
    }
    background { slots.withPermit { check(export, image, expected) } }
    return KeepOutcome.Kept(export.id)
  }

  /** The hash of the newest copy a check agreed with, or null: the character's anchor. */
  suspend fun anchorOf(characterId: Long): String? = exports.newestChecked(characterId)?.sha256

  /**
   * Every copy a check agreed with, newest first, each of them a point a chain may honestly start
   * from.
   */
  suspend fun anchorsOf(characterId: Long): Set<String> =
      exports
          .listFor(characterId, KEEP)
          .filter { it.verdict == ExportVerdict.CHECKED }
          .map { it.sha256 }
          .toSet()

  /** Boot the copy, read what it holds, and say whether that is the character. */
  suspend fun check(export: StoredExport, image: ByteArray, expected: ExportExpectation) {
    val (verdict, reason) =
        try {
          judge(export, image, expected)
        } catch (e: RuntimeException) {
          log.warn(e) { "char=${export.characterId}: checking an offline copy failed" }
          ExportVerdict.UNCHECKED to "the check itself failed: ${e.message}"
        }
    if (!exports.settle(export.id, verdict, reason, clock())) return
    when (verdict) {
      ExportVerdict.CHECKED ->
          log.info {
            "char=${export.characterId}: offline copy ${export.sha256.take(12)} is the character" +
                " and anchors what follows it"
          }
      ExportVerdict.MISMATCH ->
          log.warn {
            "char=${export.characterId}: offline copy ${export.sha256.take(12)} is not the" +
                " character it was handed over as: $reason"
          }
      else -> log.info { "char=${export.characterId}: offline copy not checked: $reason" }
    }
  }

  private suspend fun judge(
      export: StoredExport,
      image: ByteArray,
      expected: ExportExpectation,
  ): Pair<ExportVerdict, String?> {
    val revision =
        runner.revisions().maxOrNull()
            ?: return ExportVerdict.UNCHECKED to "no build here to boot it with"
    val outcome =
        withContext(blocking) {
          runner.run(
              ReplayRun(
                  revision = revision,
                  bootImage = image,
                  rtc = export.exportedAt,
                  recording = ByteArray(0),
                  frames = CHECK_FRAMES,
              ))
        }
    return when (outcome) {
      is ReplayOutcome.Broke -> ExportVerdict.UNCHECKED to outcome.why
      is ReplayOutcome.Quit -> {
        // Nothing was pressed, so nothing may have been written: a boot that changed the file is
        // not a boot of the file that arrived.
        if (outcome.sha256 != export.sha256) {
          return ExportVerdict.MISMATCH to "booting the copy for $CHECK_FRAMES frames changed it"
        }
        val report =
            outcome.report
                ?: return ExportVerdict.UNCHECKED to "the boot wrote no report of the copy"
        val wire =
            try {
              OfflineSaveWire.decode(report)
            } catch (e: RuntimeException) {
              return ExportVerdict.UNCHECKED to "the boot's report will not read: ${e.message}"
            }
        // The reporter hashes the file it read, so this binds the report to the copy.
        if (wire.saveSha256 != export.sha256) {
          return ExportVerdict.MISMATCH to "the boot reported on a file that is not the copy"
        }
        val why = differences(expected, wire)
        if (why == null) ExportVerdict.CHECKED to null else ExportVerdict.MISMATCH to why
      }
    }
  }

  companion object {
    /**
     * Frames the copy is booted for. One is enough, the save is loaded before the first frame and
     * the reporter runs at exit, measured 2026-09-04 at 1, 5, 30 and 120 frames, each writing the
     * same report of the same untouched file.
     */
    const val CHECK_FRAMES = 5L

    /** Copies a character keeps. Every checked one of them is an anchor a chain may start from. */
    const val KEEP = 10

    /** What the copy holds that the character did not, in a sentence, or null for nothing. */
    fun differences(expected: ExportExpectation, wire: OfflineSaveWire): String? {
      val problems = mutableListOf<String>()
      if (wire.money != expected.money) {
        problems += "money ¥${wire.money} in the copy, ¥${expected.money} here"
      }
      val theirs =
          wire.monsters
              .filter {
                it.container == PokemonContainer.PARTY || it.container == PokemonContainer.PC
              }
              .map { ExportExpectation.printOf(it) }
      val missing = minus(expected.monsters, theirs).filterNot { seatSkips(it) }
      val extra = minus(theirs, expected.monsters)
      if (missing.isNotEmpty() || extra.isNotEmpty()) {
        val first = missing.firstOrNull() ?: extra.first()
        // The same monster on both sides, differing in a field: name the field, not only the
        // monster.
        val twin = missing.firstOrNull()?.let { m -> extra.firstOrNull { it.pid == m.pid } }
        problems +=
            "${missing.size} monster(s) here are not in the copy as they are here and" +
                " ${extra.size} in the copy are not here; first, personality" +
                " ${"%08x".format(first.pid)} species ${first.dexId} level ${first.level}" +
                (twin?.let { ": " + fields(first, it) } ?: "")
      }
      return problems.takeIf { it.isNotEmpty() }?.joinToString("; ")
    }

    /**
     * Which fields of one monster the two sides disagree on, so the sentence names the byte and not
     * only the monster. What the engine rewrites on the way out that this server did not expect is
     * a reader disagreeing, and that is found by reading this, not by guessing.
     */
    fun fields(here: MonsterPrint, copy: MonsterPrint): String =
        listOfNotNull(
                differ("species", here.dexId, copy.dexId),
                differ("form", here.form, copy.form),
                differ("level", here.level, copy.level),
                differ("xp", here.xp, copy.xp),
                differ("ivs", here.ivs, copy.ivs),
                differ("evs", here.evs, copy.evs),
                differ("moves", here.moves, copy.moves),
                differ("held item", here.heldItemId, copy.heldItemId),
                differ("egg", here.isEgg, copy.isEgg),
            )
            .joinToString(", ")
            .ifEmpty { "no field differs" }

    private fun differ(name: String, here: Any, copy: Any): String? =
        if (here == copy) null else "$name $here here, $copy in the copy"

    /**
     * The engine's own species tables run this far. A species past it is seated only where the
     * player's imports fill serves it, which the copy's writer knew and this server does not.
     */
    const val ENGINE_SPECIES = 493

    /**
     * A monster the copy cannot be held to however honest it is: an egg, whose bit the engine
     * clears itself the moment the session walks the last of its cycles off, so the file may
     * honestly hold the hatched monster where this record still holds the egg; and a species past
     * the engine's own tables, which the field seat never puts in the party or the boxes at all.
     */
    fun seatSkips(print: MonsterPrint): Boolean = print.isEgg || print.dexId > ENGINE_SPECIES

    /** [a] without one occurrence of each element of [b]: a multiset difference. */
    private fun minus(a: List<MonsterPrint>, b: List<MonsterPrint>): List<MonsterPrint> {
      val left = b.groupingBy { it }.eachCount().toMutableMap()
      return a.filter { print ->
        val n = left[print] ?: 0
        if (n > 0) {
          left[print] = n - 1
          false
        } else true
      }
    }
  }
}
