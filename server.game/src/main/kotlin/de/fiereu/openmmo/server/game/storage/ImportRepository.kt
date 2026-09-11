package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.db.game.tables.records.CharacterImportsRecord
import de.fiereu.openmmo.db.game.tables.references.CHARACTER_IMPORTS
import java.time.LocalDateTime
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Named
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext
import org.jooq.DSLContext

/** One import, as a person reads it back. */
data class ImportRecord(
    val id: Long,
    val characterId: Long,
    val importedAt: LocalDateTime,
    val playTimeSeconds: Int,
    /** The client's hash of the file it read. Recognises the same save twice; decides nothing. */
    val saveSha256: String,
    val clientRevision: Int,
    /** The trainer id this character answers to, which the first import fixes. */
    val trainerId: Int,
    val partyCount: Int,
    val boxCount: Int,
    val speciesCount: Int,
    val levelTotal: Int,
    val levelMax: Int,
    val moneyBefore: Int,
    val moneyAfter: Int,
    val badgesBefore: Int,
    val badgesAfter: Int,
    /** Every clamp, drop and allow-and-log the door made, in the order it made them. */
    val verdicts: List<String>,
    val snapshotVersion: Int,
    val rolledBackAt: LocalDateTime? = null,
    val rolledBackBy: String? = null,
    val sealedAt: LocalDateTime? = null,
    val sealedReason: String? = null,
    /** What a replay of the play behind this import answered, once one has. */
    val replayVerdict: String? = null,
    /** The frame a divergence was found at, and null for every other answer. */
    val replayFrame: Long? = null,
) {
  /** What this import added to the wallet, which is what the weekly cap counts. */
  val moneyGained: Int
    get() = (moneyAfter - moneyBefore).coerceAtLeast(0)

  val undone: Boolean
    get() = rolledBackAt != null
}

interface ImportRepository {

  /** Writes the import and the character it replaced, or neither. */
  suspend fun record(record: ImportRecord, snapshot: ByteArray)

  /** This character's imports, newest first. */
  suspend fun listFor(characterId: Long, limit: Int): List<ImportRecord>

  suspend fun find(id: Long): ImportRecord?

  /** The blob and the version it was written at, for whoever is putting it back. */
  suspend fun loadSnapshot(id: Long): Pair<ByteArray, Int>?

  /**
   * What the character has brought in through this door since [since], counting only imports that
   * still stand. A rolled back import took its money away again, so it must not keep occupying the
   * week's allowance.
   */
  /** Answers false when somebody already rolled it back, so two moderators cannot both do it. */
  suspend fun markRolledBack(id: Long, at: LocalDateTime, by: String): Boolean

  /**
   * Marks every import of this character that still stands as one whose undo is no longer safe.
   * Called the moment something leaves the character: what an undo would put back is by then a copy
   * of a monster somebody else is holding.
   */
  suspend fun seal(characterId: Long, at: LocalDateTime, reason: String)

  /** The verdict a replay reached, on the import it is about. */
  suspend fun markReplay(id: Long, verdict: String, frame: Long?): Boolean

  /** Imports of this character that still stand, newest first. */
  suspend fun newestStanding(characterId: Long): ImportRecord?

  /** The other characters this exact file has already landed on, by the client's hash of it. */
  suspend fun charactersWithSave(sha256: String, exceptCharacterId: Long, limit: Int): List<Long>
}

class JooqImportRepository
@Inject
constructor(
    private val dsl: DSLContext,
    @param:Named("db") private val dispatcher: CoroutineDispatcher,
) : ImportRepository {

  override suspend fun record(record: ImportRecord, snapshot: ByteArray) {
    withContext(dispatcher) {
      dsl.insertInto(CHARACTER_IMPORTS).set(record.fill(dsl, snapshot)).execute()
    }
  }

  override suspend fun listFor(characterId: Long, limit: Int): List<ImportRecord> =
      withContext(dispatcher) {
        dsl.selectFrom(CHARACTER_IMPORTS)
            .where(CHARACTER_IMPORTS.CHARACTER_ID.eq(characterId))
            .orderBy(CHARACTER_IMPORTS.IMPORTED_AT.desc(), CHARACTER_IMPORTS.ID.desc())
            .limit(limit.coerceAtLeast(1))
            .fetch()
            .map { it.toImport() }
      }

  override suspend fun find(id: Long): ImportRecord? =
      withContext(dispatcher) {
        dsl.selectFrom(CHARACTER_IMPORTS).where(CHARACTER_IMPORTS.ID.eq(id)).fetchOne()?.toImport()
      }

  override suspend fun loadSnapshot(id: Long): Pair<ByteArray, Int>? =
      withContext(dispatcher) {
        dsl.select(CHARACTER_IMPORTS.SNAPSHOT, CHARACTER_IMPORTS.SNAPSHOT_VERSION)
            .from(CHARACTER_IMPORTS)
            .where(CHARACTER_IMPORTS.ID.eq(id))
            .fetchOne()
            ?.let { (blob, version) -> blob!! to version!! }
      }

  override suspend fun markRolledBack(id: Long, at: LocalDateTime, by: String): Boolean =
      withContext(dispatcher) {
        dsl.update(CHARACTER_IMPORTS)
            .set(CHARACTER_IMPORTS.ROLLED_BACK_AT, at)
            .set(CHARACTER_IMPORTS.ROLLED_BACK_BY, by)
            .where(CHARACTER_IMPORTS.ID.eq(id))
            .and(CHARACTER_IMPORTS.ROLLED_BACK_AT.isNull)
            .execute() == 1
      }

  override suspend fun seal(characterId: Long, at: LocalDateTime, reason: String) {
    withContext(dispatcher) {
      dsl.update(CHARACTER_IMPORTS)
          .set(CHARACTER_IMPORTS.SEALED_AT, at)
          .set(CHARACTER_IMPORTS.SEALED_REASON, reason)
          .where(CHARACTER_IMPORTS.CHARACTER_ID.eq(characterId))
          .and(CHARACTER_IMPORTS.SEALED_AT.isNull)
          .and(CHARACTER_IMPORTS.ROLLED_BACK_AT.isNull)
          .execute()
    }
  }

  override suspend fun markReplay(id: Long, verdict: String, frame: Long?): Boolean =
      withContext(dispatcher) {
        dsl.update(CHARACTER_IMPORTS)
            .set(CHARACTER_IMPORTS.REPLAY_VERDICT, verdict)
            .set(CHARACTER_IMPORTS.REPLAY_FRAME, frame)
            .where(CHARACTER_IMPORTS.ID.eq(id))
            .execute() == 1
      }

  override suspend fun newestStanding(characterId: Long): ImportRecord? =
      withContext(dispatcher) {
        dsl.selectFrom(CHARACTER_IMPORTS)
            .where(CHARACTER_IMPORTS.CHARACTER_ID.eq(characterId))
            .and(CHARACTER_IMPORTS.ROLLED_BACK_AT.isNull)
            .orderBy(CHARACTER_IMPORTS.IMPORTED_AT.desc(), CHARACTER_IMPORTS.ID.desc())
            .limit(1)
            .fetchOne()
            ?.toImport()
      }

  override suspend fun charactersWithSave(
      sha256: String,
      exceptCharacterId: Long,
      limit: Int,
  ): List<Long> =
      withContext(dispatcher) {
        dsl.selectDistinct(CHARACTER_IMPORTS.CHARACTER_ID)
            .from(CHARACTER_IMPORTS)
            .where(CHARACTER_IMPORTS.SAVE_SHA256.eq(sha256))
            .and(CHARACTER_IMPORTS.CHARACTER_ID.ne(exceptCharacterId))
            .and(CHARACTER_IMPORTS.ROLLED_BACK_AT.isNull)
            .limit(limit.coerceAtLeast(1))
            .fetch()
            .map { it.value1() }
      }

  private fun ImportRecord.fill(tx: DSLContext, snapshot: ByteArray): CharacterImportsRecord =
      tx.newRecord(CHARACTER_IMPORTS).also {
        it.id = id
        it.characterId = characterId
        it.importedAt = importedAt
        it.playTimeSeconds = playTimeSeconds
        it.saveSha256 = saveSha256
        it.clientRevision = clientRevision
        it.trainerId = trainerId
        it.partyCount = partyCount
        it.boxCount = boxCount
        it.speciesCount = speciesCount
        it.levelTotal = levelTotal
        it.levelMax = levelMax
        it.moneyBefore = moneyBefore
        it.moneyAfter = moneyAfter
        it.badgesBefore = badgesBefore
        it.badgesAfter = badgesAfter
        it.verdicts = verdicts.joinToString("\n")
        it.snapshot = snapshot
        it.snapshotVersion = snapshotVersion
        it.rolledBackAt = rolledBackAt
        it.rolledBackBy = rolledBackBy
        it.sealedAt = sealedAt
        it.sealedReason = sealedReason
        it.replayVerdict = replayVerdict
        it.replayFrame = replayFrame
      }

  private fun CharacterImportsRecord.toImport(): ImportRecord =
      ImportRecord(
          id = id,
          characterId = characterId,
          importedAt = importedAt,
          playTimeSeconds = playTimeSeconds,
          saveSha256 = saveSha256,
          clientRevision = clientRevision,
          trainerId = trainerId,
          partyCount = partyCount,
          boxCount = boxCount,
          speciesCount = speciesCount,
          levelTotal = levelTotal,
          levelMax = levelMax,
          moneyBefore = moneyBefore,
          moneyAfter = moneyAfter,
          badgesBefore = badgesBefore,
          badgesAfter = badgesAfter,
          verdicts = verdicts.lines().filter { it.isNotBlank() },
          snapshotVersion = snapshotVersion,
          rolledBackAt = rolledBackAt,
          rolledBackBy = rolledBackBy,
          sealedAt = sealedAt,
          sealedReason = sealedReason,
          replayVerdict = replayVerdict,
          replayFrame = replayFrame,
      )
}

/** For the tests that drive the import without a database under them. */
class InMemoryImportRepository : ImportRepository {
  private val rows = ConcurrentHashMap<Long, ImportRecord>()
  private val snapshots = ConcurrentHashMap<Long, ByteArray>()

  override suspend fun record(record: ImportRecord, snapshot: ByteArray) {
    rows[record.id] = record
    snapshots[record.id] = snapshot
  }

  override suspend fun listFor(characterId: Long, limit: Int): List<ImportRecord> =
      rows.values
          .filter { it.characterId == characterId }
          .sortedWith(
              compareByDescending<ImportRecord> { it.importedAt }.thenByDescending { it.id })
          .take(limit.coerceAtLeast(1))

  override suspend fun find(id: Long): ImportRecord? = rows[id]

  override suspend fun loadSnapshot(id: Long): Pair<ByteArray, Int>? {
    val blob = snapshots[id] ?: return null
    return blob to (rows[id]?.snapshotVersion ?: return null)
  }

  override suspend fun markRolledBack(id: Long, at: LocalDateTime, by: String): Boolean {
    val row = rows[id] ?: return false
    if (row.undone) return false
    rows[id] = row.copy(rolledBackAt = at, rolledBackBy = by)
    return true
  }

  override suspend fun seal(characterId: Long, at: LocalDateTime, reason: String) {
    for ((id, row) in rows) {
      if (row.characterId != characterId || row.sealedAt != null || row.undone) continue
      rows[id] = row.copy(sealedAt = at, sealedReason = reason)
    }
  }

  override suspend fun charactersWithSave(
      sha256: String,
      exceptCharacterId: Long,
      limit: Int,
  ): List<Long> =
      rows.values
          .filter { it.saveSha256 == sha256 && it.characterId != exceptCharacterId && !it.undone }
          .map { it.characterId }
          .distinct()
          .take(limit.coerceAtLeast(1))

  override suspend fun markReplay(id: Long, verdict: String, frame: Long?): Boolean {
    val row = rows[id] ?: return false
    rows[id] = row.copy(replayVerdict = verdict, replayFrame = frame)
    return true
  }

  override suspend fun newestStanding(characterId: Long): ImportRecord? =
      rows.values
          .filter { it.characterId == characterId && !it.undone }
          .maxWithOrNull(compareBy<ImportRecord> { it.importedAt }.thenBy { it.id })
}
