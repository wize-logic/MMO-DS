package de.fiereu.openmmo.server.game.offline.verify

import io.github.oshai.kotlinlogging.KotlinLogging
import kotlin.time.Duration
import kotlin.time.Duration.Companion.seconds
import kotlin.time.TimeSource
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

private val log = KotlinLogging.logger {}

/**
 * The thing that actually runs the queue: `VERIFY_CORES` lanes, each taking one request at a time.
 */
class ReplayWorker(
    private val service: ReplayVerificationService,
    private val limits: ReplayLimits,
    private val scope: CoroutineScope,
    private val idle: Duration = IDLE,
) {
  private val claims = Mutex()
  private val busy = mutableSetOf<Long>()
  private var started = false

  /** Idempotent: a second call does not start a second set of lanes. */
  fun start() {
    if (started) return
    started = true
    if (!service.replays()) {
      log.info { "Offline play is not replayed here, so no replay lane was started" }
      return
    }
    log.info {
      "Replaying offline play on ${limits.cores} lane(s), looking at the queue every $idle"
    }
    repeat(limits.cores) { lane -> scope.launch { while (isActive) pass(lane) } }
  }

  /** One look at the queue: run a request if there is one, else wait. */
  private suspend fun pass(lane: Int) {
    val next =
        try {
          claims.withLock { service.claimNext(busy)?.also { busy += it.id } }
        } catch (e: RuntimeException) {
          log.error(e) { "replay lane $lane could not read the queue" }
          null
        }
    if (next == null) {
      delay(idle)
      return
    }
    val began = TimeSource.Monotonic.markNow()
    try {
      service.replay(next)
      log.info {
        "replay lane $lane: request ${next.id} on chain ${next.chainId} (up to ${next.frameBudget}" +
            " frames) done in ${began.elapsedNow()}"
      }
    } catch (e: RuntimeException) {
      // The service settles what the replay throws; this is for what the settling itself throws,
      // which is the store, and a store that is down is not fixed by asking it again at once.
      log.error(e) { "replay lane $lane fell over on request ${next.id}" }
      delay(idle)
    } finally {
      claims.withLock { busy -= next.id }
    }
  }

  companion object {
    /** Between looks at an empty queue. An ask waits at most this before a lane has it. */
    val IDLE: Duration = 30.seconds
  }
}
