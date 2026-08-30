package de.fiereu.openmmo.server.game.services

import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicInteger
import javax.inject.Inject
import javax.inject.Singleton

/**
 * Contests this character has finished and not yet been paid a ribbon for.
 *
 * The ribbon is the rank gate, so it is the one contest value that buys entry to something rather
 * than describing a monster. It reaches this server on the battle outcome report like everything
 * else the client's engine writes, and the only thing behind it was a rate: one new bit per report,
 * four a minute. That bounds how fast the set can be claimed, not whether a contest happened, so a
 * session that never opened the Contest Hall could still fill the mask.
 *
 * [ContestService] already knows, the only honest way there is: every seat computes the contest
 * from the same relayed inputs and the placements are taken only when they all agree. That
 * agreement is the event, and this is the note it leaves.
 *
 * Kept in memory: a credit is worth one report that follows within the same play, and a restart
 * that loses one costs what a disconnect before the award already costs.
 */
@Singleton
class ContestRibbonCredits @Inject constructor() {

  private val credits = ConcurrentHashMap<Long, AtomicInteger>()

  /** A contest settled with this character in a seat. */
  fun award(characterId: Long) {
    credits.computeIfAbsent(characterId) { AtomicInteger() }.incrementAndGet()
  }

  /** Spends one credit, answering whether there was one. */
  fun spend(characterId: Long): Boolean {
    val held = credits[characterId] ?: return false
    while (true) {
      val current = held.get()
      if (current <= 0) return false
      if (held.compareAndSet(current, current - 1)) return true
    }
  }

  /** What this character is owed, for the log line that says why a ribbon was refused. */
  fun held(characterId: Long): Int = credits[characterId]?.get() ?: 0

  /** Nothing is owed to a character that has left. */
  fun forget(characterId: Long) {
    credits.remove(characterId)
  }
}
