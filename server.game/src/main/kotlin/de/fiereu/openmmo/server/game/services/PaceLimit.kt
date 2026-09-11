package de.fiereu.openmmo.server.game.services

import java.util.concurrent.ConcurrentHashMap

/** How often one character may do a thing whose honest rate the game itself decides. */
class PaceLimit(
    private val burst: Double,
    private val perSecond: Double,
    private val clock: () -> Long = System::nanoTime,
) {

  private data class Bucket(val tokens: Double, val at: Long)

  private val buckets = ConcurrentHashMap<Long, Bucket>()

  /** Spends one token, answering whether there was one. */
  fun allow(characterId: Long): Boolean {
    val now = clock()
    var allowed = false
    buckets.compute(characterId) { _, current ->
      val available =
          if (current == null) burst
          else {
            val elapsed = (now - current.at).coerceAtLeast(0L) / NANOS_PER_SECOND
            (current.tokens + elapsed * perSecond).coerceAtMost(burst)
          }
      allowed = available >= 1.0
      Bucket(if (allowed) available - 1.0 else available, now)
    }
    prune(now)
    return allowed
  }

  /** Drops the bucket for a character that has left, so the table follows the players. */
  fun forget(characterId: Long) {
    buckets.remove(characterId)
  }

  /**
   * A full bucket is indistinguishable from no bucket, so a character that has been quiet for long
   * enough to refill is dropped rather than kept.
   */
  private fun prune(now: Long) {
    if (buckets.size < PRUNE_THRESHOLD) return
    val full = burst / perSecond
    buckets.entries.removeIf { (now - it.value.at) / NANOS_PER_SECOND >= full }
  }

  private companion object {
    const val NANOS_PER_SECOND = 1_000_000_000.0
    const val PRUNE_THRESHOLD = 1024
  }
}
