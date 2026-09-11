package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.db.game.tables.references.CHARACTER_EXPORTS
import java.time.LocalDateTime
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Named
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext
import org.jooq.DSLContext
import org.jooq.Record

/** What booting an offline copy here answered about it. */
enum class ExportVerdict {
  /** Kept and not yet booted. It anchors nothing until it has been. */
  PENDING,
  /** Booted here, and what it holds is the character it was handed over as. The anchor. */
  CHECKED,
  /** Booted here, and what it holds is not that character. Never an anchor. */
  MISMATCH,
  /** It could not be booted or read here. Not an anchor, and nothing about the player. */
  UNCHECKED,
}

/** One offline copy a session sent as it left, as a person reads it back. */
data class StoredExport(
    val id: Long,
    val characterId: Long,
    val exportedAt: LocalDateTime,
    val sha256: String,
    val verdict: ExportVerdict,
    val verdictReason: String? = null,
    val checkedAt: LocalDateTime? = null,
)

interface ExportRepository {

  suspend fun record(export: StoredExport, image: ByteArray)

  suspend fun find(id: Long): StoredExport?

  suspend fun image(id: Long): ByteArray?

  /** The newest copy of this character a check agreed with, or null. */
  suspend fun newestChecked(characterId: Long): StoredExport?

  /** The image of this character's checked copy with that hash, or null. */
  suspend fun checkedImage(characterId: Long, sha256: String): ByteArray?

  /** The verdict, once. False when the row is gone or was already answered. */
  suspend fun settle(id: Long, verdict: ExportVerdict, reason: String?, at: LocalDateTime): Boolean

  /** Drops all but the newest [keep] copies of a character. How many went. */
  suspend fun prune(characterId: Long, keep: Int): Int

  /** A character's copies, newest first. */
  suspend fun listFor(characterId: Long, limit: Int): List<StoredExport>
}

class JooqExportRepository
@Inject
constructor(
    private val dsl: DSLContext,
    @param:Named("db") private val dispatcher: CoroutineDispatcher,
) : ExportRepository {

  override suspend fun record(export: StoredExport, image: ByteArray) {
    withContext(dispatcher) {
      dsl.insertInto(CHARACTER_EXPORTS)
          .set(CHARACTER_EXPORTS.ID, export.id)
          .set(CHARACTER_EXPORTS.CHARACTER_ID, export.characterId)
          .set(CHARACTER_EXPORTS.EXPORTED_AT, export.exportedAt)
          .set(CHARACTER_EXPORTS.SHA256, export.sha256)
          .set(CHARACTER_EXPORTS.IMAGE, image)
          .set(CHARACTER_EXPORTS.VERDICT, export.verdict.name)
          .set(CHARACTER_EXPORTS.VERDICT_REASON, export.verdictReason?.take(REASON_MAX))
          .set(CHARACTER_EXPORTS.CHECKED_AT, export.checkedAt)
          .execute()
    }
  }

  override suspend fun find(id: Long): StoredExport? =
      withContext(dispatcher) { rows().where(CHARACTER_EXPORTS.ID.eq(id)).fetchOne()?.toExport() }

  override suspend fun image(id: Long): ByteArray? =
      withContext(dispatcher) {
        dsl.select(CHARACTER_EXPORTS.IMAGE)
            .from(CHARACTER_EXPORTS)
            .where(CHARACTER_EXPORTS.ID.eq(id))
            .fetchOne()
            ?.value1()
      }

  override suspend fun newestChecked(characterId: Long): StoredExport? =
      withContext(dispatcher) {
        rows()
            .where(CHARACTER_EXPORTS.CHARACTER_ID.eq(characterId))
            .and(CHARACTER_EXPORTS.VERDICT.eq(ExportVerdict.CHECKED.name))
            .orderBy(CHARACTER_EXPORTS.EXPORTED_AT.desc(), CHARACTER_EXPORTS.ID.desc())
            .limit(1)
            .fetchOne()
            ?.toExport()
      }

  override suspend fun checkedImage(characterId: Long, sha256: String): ByteArray? =
      withContext(dispatcher) {
        dsl.select(CHARACTER_EXPORTS.IMAGE)
            .from(CHARACTER_EXPORTS)
            .where(CHARACTER_EXPORTS.CHARACTER_ID.eq(characterId))
            .and(CHARACTER_EXPORTS.SHA256.eq(sha256))
            .and(CHARACTER_EXPORTS.VERDICT.eq(ExportVerdict.CHECKED.name))
            .orderBy(CHARACTER_EXPORTS.EXPORTED_AT.desc(), CHARACTER_EXPORTS.ID.desc())
            .limit(1)
            .fetchOne()
            ?.value1()
      }

  override suspend fun settle(
      id: Long,
      verdict: ExportVerdict,
      reason: String?,
      at: LocalDateTime,
  ): Boolean =
      withContext(dispatcher) {
        dsl.update(CHARACTER_EXPORTS)
            .set(CHARACTER_EXPORTS.VERDICT, verdict.name)
            .set(CHARACTER_EXPORTS.VERDICT_REASON, reason?.take(REASON_MAX))
            .set(CHARACTER_EXPORTS.CHECKED_AT, at)
            .where(CHARACTER_EXPORTS.ID.eq(id))
            .and(CHARACTER_EXPORTS.VERDICT.eq(ExportVerdict.PENDING.name))
            .execute() == 1
      }

  override suspend fun prune(characterId: Long, keep: Int): Int =
      withContext(dispatcher) {
        val stale =
            dsl.select(CHARACTER_EXPORTS.ID)
                .from(CHARACTER_EXPORTS)
                .where(CHARACTER_EXPORTS.CHARACTER_ID.eq(characterId))
                .orderBy(CHARACTER_EXPORTS.EXPORTED_AT.desc(), CHARACTER_EXPORTS.ID.desc())
                .offset(keep.coerceAtLeast(0))
                .fetch(CHARACTER_EXPORTS.ID)
                .filterNotNull()
        if (stale.isEmpty()) 0
        else dsl.deleteFrom(CHARACTER_EXPORTS).where(CHARACTER_EXPORTS.ID.`in`(stale)).execute()
      }

  override suspend fun listFor(characterId: Long, limit: Int): List<StoredExport> =
      withContext(dispatcher) {
        rows()
            .where(CHARACTER_EXPORTS.CHARACTER_ID.eq(characterId))
            .orderBy(CHARACTER_EXPORTS.EXPORTED_AT.desc(), CHARACTER_EXPORTS.ID.desc())
            .limit(limit.coerceAtLeast(1))
            .fetch()
            .map { it.toExport() }
      }

  /** Everything but the image, which is what every reader that is not booting one wants. */
  private fun rows() =
      dsl.select(
              CHARACTER_EXPORTS.ID,
              CHARACTER_EXPORTS.CHARACTER_ID,
              CHARACTER_EXPORTS.EXPORTED_AT,
              CHARACTER_EXPORTS.SHA256,
              CHARACTER_EXPORTS.VERDICT,
              CHARACTER_EXPORTS.VERDICT_REASON,
              CHARACTER_EXPORTS.CHECKED_AT)
          .from(CHARACTER_EXPORTS)

  private fun Record.toExport(): StoredExport =
      StoredExport(
          id = checkNotNull(get(CHARACTER_EXPORTS.ID)),
          characterId = checkNotNull(get(CHARACTER_EXPORTS.CHARACTER_ID)),
          exportedAt = checkNotNull(get(CHARACTER_EXPORTS.EXPORTED_AT)),
          sha256 = checkNotNull(get(CHARACTER_EXPORTS.SHA256)),
          verdict =
              runCatching { ExportVerdict.valueOf(checkNotNull(get(CHARACTER_EXPORTS.VERDICT))) }
                  .getOrDefault(ExportVerdict.UNCHECKED),
          verdictReason = get(CHARACTER_EXPORTS.VERDICT_REASON),
          checkedAt = get(CHARACTER_EXPORTS.CHECKED_AT),
      )

  private companion object {
    /** The column's own width. */
    const val REASON_MAX = 256
  }
}

/** For the checks that drive the anchor without a database under them. */
class InMemoryExportRepository : ExportRepository {
  private val rows = ConcurrentHashMap<Long, StoredExport>()
  private val images = ConcurrentHashMap<Long, ByteArray>()

  override suspend fun record(export: StoredExport, image: ByteArray) {
    rows[export.id] = export
    images[export.id] = image
  }

  override suspend fun find(id: Long): StoredExport? = rows[id]

  override suspend fun image(id: Long): ByteArray? = images[id]

  override suspend fun newestChecked(characterId: Long): StoredExport? =
      listFor(characterId, Int.MAX_VALUE).firstOrNull { it.verdict == ExportVerdict.CHECKED }

  override suspend fun checkedImage(characterId: Long, sha256: String): ByteArray? =
      listFor(characterId, Int.MAX_VALUE)
          .firstOrNull { it.verdict == ExportVerdict.CHECKED && it.sha256 == sha256 }
          ?.let { images[it.id] }

  override suspend fun settle(
      id: Long,
      verdict: ExportVerdict,
      reason: String?,
      at: LocalDateTime,
  ): Boolean {
    val row = rows[id] ?: return false
    if (row.verdict != ExportVerdict.PENDING) return false
    rows[id] = row.copy(verdict = verdict, verdictReason = reason, checkedAt = at)
    return true
  }

  override suspend fun prune(characterId: Long, keep: Int): Int {
    val stale = listFor(characterId, Int.MAX_VALUE).drop(keep.coerceAtLeast(0))
    stale.forEach {
      rows.remove(it.id)
      images.remove(it.id)
    }
    return stale.size
  }

  override suspend fun listFor(characterId: Long, limit: Int): List<StoredExport> =
      rows.values
          .filter { it.characterId == characterId }
          .sortedWith(
              compareByDescending<StoredExport> { it.exportedAt }.thenByDescending { it.id })
          .take(limit.coerceAtLeast(1))
}
