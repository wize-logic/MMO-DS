package de.fiereu.openmmo.server.game.services

import java.util.concurrent.ConcurrentHashMap

/**
 * How often one character may do a thing whose honest rate the game itself decides.
 *
 * [GrantBudget] bounds how much a client may claim; this bounds how fast it may act, which had no
 * answer above the socket. The pipeline's own limiter holds a session to a few hundred frames a
 * second, which is the right ceiling for a flood and tens of times faster than the game moves a
 * player, so everything paced by an animation was free to arrive as fast as a socket allows.
 *
 * A token bucket per character, with one difference from the network layer's: an empty bucket
 * refuses rather than waits, because the caller is a game rule and the honest answer to "you cannot
 * have walked that fast" is not to move, not to move late.
 *
 * Keyed per character, so reconnecting does not hand back a fresh allowance.
 */
class PaceLimit(
    private val burst: Double,
    private val perSecond: Double,
    private val clock: () -> Long = System::nanoTime,
) {

  private data class Bucket(val tokens: Double, val at: Long)

  private val buckets = ConcurrentHashMap<Long, Bucket>()

  /**
   * Spends one token, answering whether there was one. A refusal costs nothing: deducting anyway
   * would push the bucket below empty and make the next token take longer than the rate says.
   */
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

  /** A full bucket is the same as no bucket, so a character quiet long enough is dropped. */
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
