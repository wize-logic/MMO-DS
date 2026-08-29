package de.fiereu.openmmo.server.game.matchmaking

import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

private val log = KotlinLogging.logger {}

/** How often every open queue tries to pair what is waiting in it. */
private const val ROUND_INTERVAL_MS = 30_000L

/** The clock behind the queue. */
@Singleton
class MatchmakingRounds
@Inject
constructor(
    private val service: MatchmakingService,
    private val scope: CoroutineScope,
) {
  private var started = false

  /** Idempotent: a second call does not start a second loop. */
  fun start() {
    if (started) return
    started = true
    scope.launch {
      log.info { "Matchmaking rounds every ${ROUND_INTERVAL_MS / 1000}s" }
      while (isActive) {
        delay(ROUND_INTERVAL_MS)
        try {
          val made = service.runAllRounds()
          if (made > 0) log.info { "Matchmaking round made $made match(es)" }
        } catch (e: Exception) {
          // A round that throws must not take the timer down with it; the next one is 30s away.
          log.error(e) { "Matchmaking round failed" }
        }
      }
    }
  }
}
