package de.fiereu.openmmo.server.game.services

import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicInteger
import javax.inject.Inject
import javax.inject.Singleton

/** Contests this character has actually finished and not yet been paid a ribbon for. */
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
