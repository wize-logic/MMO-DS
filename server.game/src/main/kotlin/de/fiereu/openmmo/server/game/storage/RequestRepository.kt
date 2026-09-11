package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.db.game.tables.references.VERIFY_REQUESTS
import de.fiereu.openmmo.server.game.offline.verify.ReplayVerdict
import java.time.LocalDateTime
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Named
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext
import org.jooq.DSLContext
import org.jooq.impl.DSL

/** One ask to have offline play checked, as a person reads it back. */
data class StoredRequest(
    val id: Long,
    val chainId: Long,
    val characterId: Long,
    /** The monster asked about, or null for the whole chain. */
    val monsterPid: Int?,
    val requestedAt: LocalDateTime,
    /** Every frame between the frontier and the end of the chain when the ask was made. */
    val frameBudget: Long,
    /** The free allowance the character had left at the ask. The fee for what ran is on this. */
    val freeFramesLeft: Long,
    val feePaid: Int,
    /** A running total: once a verdict has landed, what is owed back has gone back. */
    val feeRefunded: Int = 0,
    val framesRun: Long = 0,
    val verdict: ReplayVerdict = ReplayVerdict.PENDING,
    val verdictReason: String? = null,
    /** The session the walk stopped on: the birth, the end, or where it broke. */
    val stoppedLink: Int? = null,
    val settledAt: LocalDateTime? = null,
    /** When a moderator put this settled request back, and who. Once in its life. */
    val requeuedAt: LocalDateTime? = null,
    val requeuedBy: String? = null,
) {
  /** When this request's wait began: the ask, or the moment it was put back. */
  val waitingSince: LocalDateTime
    get() = requeuedAt ?: requestedAt

  /** What the server still holds of the fee. */
  val feeHeld: Int
    get() = (feePaid - feeRefunded).coerceAtLeast(0)
}

interface RequestRepository {

  suspend fun record(request: StoredRequest)

  suspend fun find(id: Long): StoredRequest?

  /** Requests nobody has answered yet, oldest ask first. */
  suspend fun pending(limit: Int): List<StoredRequest>

  /** The last word on a request. False when the row is gone or was already answered. */
  suspend fun settle(
      id: Long,
      verdict: ReplayVerdict,
      reason: String?,
      stoppedLink: Int?,
      framesRun: Long,
      refunded: Int,
      at: LocalDateTime,
  ): Boolean

  /** Requests of this character still waiting. One at a time is the rule. */
  suspend fun inFlightFor(characterId: Long): Int

  /**
   * Replay this character has committed this server to since [since]: what settled requests ran,
   * and the whole budget of those still waiting. Refusals never reach a row and so never count.
   */
  suspend fun framesSince(characterId: Long, since: LocalDateTime): Long

  /** Requests waiting, across everybody. */
  suspend fun depth(): Int

  /** A character's requests, newest ask first. */
  suspend fun listFor(characterId: Long, limit: Int): List<StoredRequest>

  /**
   * Puts a settled request back to be run again, once. The verdict goes; the chain's frontier is
   * untouched, so the walk resumes.
   */
  suspend fun requeue(id: Long, by: String, at: LocalDateTime): Boolean
}

class JooqRequestRepository
@Inject
constructor(
    private val dsl: DSLContext,
    @param:Named("db") private val dispatcher: CoroutineDispatcher,
) : RequestRepository {

  override suspend fun record(request: StoredRequest) {
    withContext(dispatcher) {
      dsl.insertInto(VERIFY_REQUESTS)
          .set(VERIFY_REQUESTS.ID, request.id)
          .set(VERIFY_REQUESTS.CHAIN_ID, request.chainId)
          .set(VERIFY_REQUESTS.CHARACTER_ID, request.characterId)
          .set(VERIFY_REQUESTS.MONSTER_PID, request.monsterPid)
          .set(VERIFY_REQUESTS.REQUESTED_AT, request.requestedAt)
          .set(VERIFY_REQUESTS.FRAME_BUDGET, request.frameBudget)
          .set(VERIFY_REQUESTS.FREE_FRAMES_LEFT, request.freeFramesLeft)
          .set(VERIFY_REQUESTS.FEE_PAID, request.feePaid)
          .set(VERIFY_REQUESTS.FEE_REFUNDED, request.feeRefunded)
          .set(VERIFY_REQUESTS.FRAMES_RUN, request.framesRun)
          .set(VERIFY_REQUESTS.VERDICT, request.verdict.name)
          .set(VERIFY_REQUESTS.VERDICT_REASON, request.verdictReason?.take(REASON_MAX))
          .set(VERIFY_REQUESTS.STOPPED_LINK, request.stoppedLink)
          .set(VERIFY_REQUESTS.SETTLED_AT, request.settledAt)
          .set(VERIFY_REQUESTS.REQUEUED_AT, request.requeuedAt)
          .set(VERIFY_REQUESTS.REQUEUED_BY, request.requeuedBy)
          .execute()
    }
  }

  override suspend fun find(id: Long): StoredRequest? =
      withContext(dispatcher) {
        dsl.selectFrom(VERIFY_REQUESTS).where(VERIFY_REQUESTS.ID.eq(id)).fetchOne()?.toRequest()
      }

  override suspend fun pending(limit: Int): List<StoredRequest> =
      withContext(dispatcher) {
        dsl.selectFrom(VERIFY_REQUESTS)
            .where(VERIFY_REQUESTS.VERDICT.eq(ReplayVerdict.PENDING.name))
            .orderBy(VERIFY_REQUESTS.REQUESTED_AT.asc(), VERIFY_REQUESTS.ID.asc())
            .limit(limit.coerceAtLeast(1))
            .fetch()
            .map { it.toRequest() }
      }

  override suspend fun settle(
      id: Long,
      verdict: ReplayVerdict,
      reason: String?,
      stoppedLink: Int?,
      framesRun: Long,
      refunded: Int,
      at: LocalDateTime,
  ): Boolean =
      withContext(dispatcher) {
        dsl.update(VERIFY_REQUESTS)
            .set(VERIFY_REQUESTS.VERDICT, verdict.name)
            .set(VERIFY_REQUESTS.VERDICT_REASON, reason?.take(REASON_MAX))
            .set(VERIFY_REQUESTS.STOPPED_LINK, stoppedLink)
            .set(VERIFY_REQUESTS.FRAMES_RUN, framesRun)
            .set(VERIFY_REQUESTS.FEE_REFUNDED, refunded)
            .set(VERIFY_REQUESTS.SETTLED_AT, at)
            .where(VERIFY_REQUESTS.ID.eq(id))
            .and(VERIFY_REQUESTS.VERDICT.eq(ReplayVerdict.PENDING.name))
            .execute() == 1
      }

  override suspend fun inFlightFor(characterId: Long): Int =
      withContext(dispatcher) {
        dsl.fetchCount(
            VERIFY_REQUESTS,
            VERIFY_REQUESTS.CHARACTER_ID.eq(characterId)
                .and(VERIFY_REQUESTS.VERDICT.eq(ReplayVerdict.PENDING.name)))
      }

  override suspend fun framesSince(characterId: Long, since: LocalDateTime): Long =
      withContext(dispatcher) {
        val committed =
            DSL.`when`(
                    VERIFY_REQUESTS.VERDICT.eq(ReplayVerdict.PENDING.name),
                    VERIFY_REQUESTS.FRAME_BUDGET)
                .otherwise(VERIFY_REQUESTS.FRAMES_RUN)
        dsl.select(DSL.sum(committed))
            .from(VERIFY_REQUESTS)
            .where(VERIFY_REQUESTS.CHARACTER_ID.eq(characterId))
            .and(VERIFY_REQUESTS.REQUESTED_AT.ge(since))
            .fetchOne()
            ?.value1()
            ?.toLong() ?: 0L
      }

  override suspend fun depth(): Int =
      withContext(dispatcher) {
        dsl.fetchCount(VERIFY_REQUESTS, VERIFY_REQUESTS.VERDICT.eq(ReplayVerdict.PENDING.name))
      }

  override suspend fun listFor(characterId: Long, limit: Int): List<StoredRequest> =
      withContext(dispatcher) {
        dsl.selectFrom(VERIFY_REQUESTS)
            .where(VERIFY_REQUESTS.CHARACTER_ID.eq(characterId))
            .orderBy(VERIFY_REQUESTS.REQUESTED_AT.desc(), VERIFY_REQUESTS.ID.desc())
            .limit(limit.coerceAtLeast(1))
            .fetch()
            .map { it.toRequest() }
      }

  override suspend fun requeue(id: Long, by: String, at: LocalDateTime): Boolean =
      withContext(dispatcher) {
        dsl.update(VERIFY_REQUESTS)
            .set(VERIFY_REQUESTS.VERDICT, ReplayVerdict.PENDING.name)
            .setNull(VERIFY_REQUESTS.VERDICT_REASON)
            .setNull(VERIFY_REQUESTS.STOPPED_LINK)
            .setNull(VERIFY_REQUESTS.SETTLED_AT)
            .set(VERIFY_REQUESTS.REQUEUED_AT, at)
            .set(VERIFY_REQUESTS.REQUEUED_BY, by.take(NAME_MAX))
            .where(VERIFY_REQUESTS.ID.eq(id))
            .and(
                VERIFY_REQUESTS.VERDICT.notIn(
                    ReplayVerdict.PENDING.name, ReplayVerdict.VERIFIED.name))
            .and(VERIFY_REQUESTS.REQUEUED_AT.isNull)
            .execute() == 1
      }

  private fun de.fiereu.openmmo.db.game.tables.records.VerifyRequestsRecord.toRequest() =
      StoredRequest(
          id = id,
          chainId = chainId,
          characterId = characterId,
          monsterPid = monsterPid,
          requestedAt = requestedAt,
          frameBudget = frameBudget,
          freeFramesLeft = freeFramesLeft,
          feePaid = feePaid,
          feeRefunded = feeRefunded,
          framesRun = framesRun,
          verdict =
              runCatching { ReplayVerdict.valueOf(verdict) }.getOrDefault(ReplayVerdict.PENDING),
          verdictReason = verdictReason,
          stoppedLink = stoppedLink,
          settledAt = settledAt,
          requeuedAt = requeuedAt,
          requeuedBy = requeuedBy,
      )

  private companion object {
    /** The columns' own widths. */
    const val REASON_MAX = 256
    const val NAME_MAX = 64
  }
}

/** For the checks that drive the worker without a database under them. */
class InMemoryRequestRepository : RequestRepository {
  private val rows = ConcurrentHashMap<Long, StoredRequest>()

  override suspend fun record(request: StoredRequest) {
    rows[request.id] = request
  }

  override suspend fun find(id: Long): StoredRequest? = rows[id]

  override suspend fun pending(limit: Int): List<StoredRequest> =
      rows.values
          .filter { it.verdict == ReplayVerdict.PENDING }
          .sortedWith(compareBy<StoredRequest> { it.requestedAt }.thenBy { it.id })
          .take(limit.coerceAtLeast(1))

  override suspend fun settle(
      id: Long,
      verdict: ReplayVerdict,
      reason: String?,
      stoppedLink: Int?,
      framesRun: Long,
      refunded: Int,
      at: LocalDateTime,
  ): Boolean {
    val row = rows[id] ?: return false
    if (row.verdict != ReplayVerdict.PENDING) return false
    rows[id] =
        row.copy(
            verdict = verdict,
            verdictReason = reason,
            stoppedLink = stoppedLink,
            framesRun = framesRun,
            feeRefunded = refunded,
            settledAt = at)
    return true
  }

  override suspend fun inFlightFor(characterId: Long): Int =
      rows.values.count { it.characterId == characterId && it.verdict == ReplayVerdict.PENDING }

  override suspend fun framesSince(characterId: Long, since: LocalDateTime): Long =
      rows.values
          .filter { it.characterId == characterId && !it.requestedAt.isBefore(since) }
          .sumOf { if (it.verdict == ReplayVerdict.PENDING) it.frameBudget else it.framesRun }

  override suspend fun depth(): Int = rows.values.count { it.verdict == ReplayVerdict.PENDING }

  override suspend fun listFor(characterId: Long, limit: Int): List<StoredRequest> =
      rows.values
          .filter { it.characterId == characterId }
          .sortedWith(
              compareByDescending<StoredRequest> { it.requestedAt }.thenByDescending { it.id })
          .take(limit.coerceAtLeast(1))

  override suspend fun requeue(id: Long, by: String, at: LocalDateTime): Boolean {
    val row = rows[id] ?: return false
    if (row.verdict == ReplayVerdict.PENDING || row.verdict == ReplayVerdict.VERIFIED) return false
    if (row.requeuedAt != null) return false
    rows[id] =
        row.copy(
            verdict = ReplayVerdict.PENDING,
            verdictReason = null,
            stoppedLink = null,
            settledAt = null,
            requeuedAt = at,
            requeuedBy = by)
    return true
  }
}
