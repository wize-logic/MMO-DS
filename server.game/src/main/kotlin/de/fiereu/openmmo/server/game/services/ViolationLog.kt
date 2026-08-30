package de.fiereu.openmmo.server.game.services

import io.github.oshai.kotlinlogging.KotlinLogging
import java.time.Duration
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicLong
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/**
 * The refusals, counted.
 *
 * Every service here already turns a claim it does not believe into a warning and a return, and
 * there are ninety odd of them. That stops the claim, but nothing added two refusals together: a
 * client that is probing wrote one line per attempt into the same log as somebody who mistyped a
 * name once. This counts them per character inside a rolling window, keeps a small ring an operator
 * can read, and says the tally once, loudly, past a threshold.
 *
 * It does not punish, and that is deliberate. This code is public, so a threshold that acted on its
 * own would be an oracle: a cheat author would search for the boundary until their client sat one
 * under it. Held as a record, the same counters answer a question a person asks later, about a
 * whole account rather than one packet.
 *
 * Counted per character rather than per session, for the reason [GrantBudget] is: reconnecting must
 * not wipe the tally that says why.
 */
@Singleton
class ViolationLog(private val limits: Limits, private val clock: () -> Long) {

  /** What the server builds. The other constructor is for a test that drives its own clock. */
  @Inject constructor() : this(Limits(), System::nanoTime)

  data class Limits(
      /** How long a refusal keeps counting against the character that made it. */
      val window: Duration = Duration.ofMinutes(10),
      /**
       * Refusals in one window past which the tally is said at error level. Honest play reaches a
       * handful: a lost race or a stale menu can trip one or two without anything being wrong.
       */
      val loudAt: Int = 12,
      /** Recent entries kept for [recent], across all characters. */
      val ringSize: Int = 512,
  )

  /** One per thing a client can be caught at, so a tally reads as a description of what it did. */
  enum class Kind {
    /** A position claim for somewhere the player could not have walked to. */
    IMPOSSIBLE_POSITION,
    /** Steps, battles or messages arriving faster than the game produces them. */
    IMPOSSIBLE_PACE,
    /** A report of gaining more than a window of honest play gives. */
    PAST_ALLOWANCE,
    /** A claim about a monster that no fight could have produced. */
    IMPOSSIBLE_MONSTER,
    /** A moveset holding a move the species cannot have learned. */
    ILLEGAL_MOVESET,
    /** A claim on an item the wire is not allowed to mint. */
    FORBIDDEN_ITEM,
    /** Acting on an entity or a tile too far away to act on. */
    OUT_OF_REACH,
    /** Acting on something owned by somebody else. */
    NOT_YOURS,
    /** A packet sent out of the order its feature has. */
    OUT_OF_SEQUENCE,
  }

  /** One refusal, as an operator reads it back. */
  data class Entry(val at: Long, val characterId: Long?, val kind: Kind, val detail: String)

  private data class Window(val startedAt: Long, val count: Int)

  private val windows = ConcurrentHashMap<Pair<Long, Kind>, Window>()
  private val ring = ArrayDeque<Entry>()
  private val loudAgainAt = ConcurrentHashMap<Long, Long>()
  private val total = AtomicLong()

  /**
   * Writes one refusal down. [detail] is the sentence that used to be the warning, so a call site
   * loses nothing by moving here. A null character still logs and still fills the ring.
   */
  fun record(characterId: Long?, kind: Kind, detail: String) {
    val now = clock()
    total.incrementAndGet()
    val entry = Entry(now, characterId, kind, detail)
    synchronized(ring) {
      ring.addLast(entry)
      while (ring.size > limits.ringSize) ring.removeFirst()
    }
    if (characterId == null) {
      log.warn { "$kind: $detail" }
      return
    }
    val windowNanos = limits.window.toNanos()
    val updated =
        windows.compute(characterId to kind) { _, current ->
          if (current == null || now - current.startedAt >= windowNanos) Window(now, 1)
          else current.copy(count = current.count + 1)
        }!!
    prune(now)
    log.warn { "$kind for char=$characterId (${updated.count} this window): $detail" }
    val tally = countsFor(characterId)
    val sum = tally.values.sum()
    if (sum < limits.loudAt) return
    // Once per window rather than once per refusal, so a client that keeps going is not the log.
    val previous = loudAgainAt[characterId]
    if (previous != null && now < previous) return
    loudAgainAt[characterId] = now + windowNanos
    log.error {
      "char=$characterId has $sum refusals inside ${limits.window.toMinutes()} minutes: " +
          tally.entries.sortedByDescending { it.value }.joinToString { "${it.key}=${it.value}" }
    }
  }

  /** What this character has been refused for, inside the window, by kind. */
  fun countsFor(characterId: Long): Map<Kind, Int> {
    val now = clock()
    val windowNanos = limits.window.toNanos()
    return Kind.entries
        .mapNotNull { kind ->
          val window = windows[characterId to kind] ?: return@mapNotNull null
          if (now - window.startedAt >= windowNanos) null else kind to window.count
        }
        .toMap()
  }

  /** The most recent refusals across every character, newest last. */
  fun recent(limit: Int = 40): List<Entry> =
      synchronized(ring) { ring.toList() }.takeLast(limit.coerceAtLeast(1))

  /** Everything recorded since the server started, which is what a graph wants. */
  fun totalRecorded(): Long = total.get()

  /** Keeps the table from holding a row for every character ever seen. */
  private fun prune(now: Long) {
    if (windows.size < PRUNE_THRESHOLD) return
    val windowNanos = limits.window.toNanos()
    windows.entries.removeIf { now - it.value.startedAt >= windowNanos }
    loudAgainAt.entries.removeIf { now >= it.value }
  }

  private companion object {
    const val PRUNE_THRESHOLD = 1024
  }
}
