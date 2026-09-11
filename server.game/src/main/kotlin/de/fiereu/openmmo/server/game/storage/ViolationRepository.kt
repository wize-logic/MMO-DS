package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.db.game.tables.references.VIOLATIONS
import io.github.oshai.kotlinlogging.KotlinLogging
import java.time.LocalDateTime
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Named
import javax.inject.Singleton
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.jooq.DSLContext

private val log = KotlinLogging.logger {}

/** What one character has been refused for, of one kind, across every session it has ever had. */
data class DurableViolation(
    val characterId: Long,
    val kind: String,
    val total: Long,
    val firstAt: LocalDateTime,
    val lastAt: LocalDateTime,
    val lastDetail: String,
)

/** The refusals that outlive the process. */
interface ViolationRepository {

  fun bump(characterId: Long, kind: String, detail: String, at: LocalDateTime)

  suspend fun countsFor(characterId: Long): List<DurableViolation>

  /** The heaviest tallies on the server, which is where a person starts looking. */
  suspend fun worst(limit: Int): List<DurableViolation>
}

@Singleton
class JooqViolationRepository
@Inject
constructor(
    private val dsl: DSLContext,
    @param:Named("db") private val dispatcher: CoroutineDispatcher,
) : ViolationRepository {

  private val scope = CoroutineScope(dispatcher + SupervisorJob())

  override fun bump(characterId: Long, kind: String, detail: String, at: LocalDateTime) {
    scope.launch {
      try {
        dsl.insertInto(VIOLATIONS)
            .set(VIOLATIONS.CHARACTER_ID, characterId)
            .set(VIOLATIONS.KIND, kind)
            .set(VIOLATIONS.TOTAL, 1L)
            .set(VIOLATIONS.FIRST_AT, at)
            .set(VIOLATIONS.LAST_AT, at)
            .set(VIOLATIONS.LAST_DETAIL, detail.take(DETAIL_MAX))
            .onConflict(VIOLATIONS.CHARACTER_ID, VIOLATIONS.KIND)
            .doUpdate()
            .set(VIOLATIONS.TOTAL, VIOLATIONS.TOTAL.plus(1L))
            .set(VIOLATIONS.LAST_AT, at)
            .set(VIOLATIONS.LAST_DETAIL, detail.take(DETAIL_MAX))
            .execute()
      } catch (e: Exception) {
        // A refusal that cannot be written down is still a refusal that was made. Losing the tally
        // must never take the connection with it, and a character deleted between the refusal and
        // this write is the ordinary way it happens.
        log.warn(e) { "could not record a $kind refusal for char=$characterId" }
      }
    }
  }

  override suspend fun countsFor(characterId: Long): List<DurableViolation> =
      withContext(dispatcher) {
        dsl.selectFrom(VIOLATIONS)
            .where(VIOLATIONS.CHARACTER_ID.eq(characterId))
            .orderBy(VIOLATIONS.TOTAL.desc())
            .fetch()
            .map {
              DurableViolation(
                  it.characterId, it.kind, it.total, it.firstAt, it.lastAt, it.lastDetail)
            }
      }

  override suspend fun worst(limit: Int): List<DurableViolation> =
      withContext(dispatcher) {
        dsl.selectFrom(VIOLATIONS)
            .orderBy(VIOLATIONS.TOTAL.desc(), VIOLATIONS.LAST_AT.desc())
            .limit(limit.coerceAtLeast(1))
            .fetch()
            .map {
              DurableViolation(
                  it.characterId, it.kind, it.total, it.firstAt, it.lastAt, it.lastDetail)
            }
      }

  private companion object {
    /** The column's own width. Truncated here so a long sentence is a short row, not a failure. */
    const val DETAIL_MAX = 256
  }
}

/** What a test gets, and what a [ViolationLog] built without a database writes into. */
class InMemoryViolationRepository : ViolationRepository {
  private val rows = ConcurrentHashMap<Pair<Long, String>, DurableViolation>()

  override fun bump(characterId: Long, kind: String, detail: String, at: LocalDateTime) {
    rows.compute(characterId to kind) { _, current ->
      current?.copy(total = current.total + 1, lastAt = at, lastDetail = detail)
          ?: DurableViolation(characterId, kind, 1, at, at, detail)
    }
  }

  override suspend fun countsFor(characterId: Long): List<DurableViolation> =
      rows.values.filter { it.characterId == characterId }.sortedByDescending { it.total }

  override suspend fun worst(limit: Int): List<DurableViolation> =
      rows.values.sortedByDescending { it.total }.take(limit.coerceAtLeast(1))
}
