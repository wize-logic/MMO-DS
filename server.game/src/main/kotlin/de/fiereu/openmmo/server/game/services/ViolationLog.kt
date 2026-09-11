package de.fiereu.openmmo.server.game.services

import de.fiereu.openmmo.server.game.storage.InMemoryViolationRepository
import de.fiereu.openmmo.server.game.storage.ViolationRepository
import io.github.oshai.kotlinlogging.KotlinLogging
import java.time.Duration
import java.time.LocalDateTime
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicLong
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/** The refusals, counted. */
@Singleton
class ViolationLog(
    private val limits: Limits = Limits(),
    private val clock: () -> Long = System::nanoTime,
    /**
     * Where the same refusals go to outlive the process. The counters below turn over in ten
     * minutes and a restart used to be an amnesty; a moderator asking about an account a day later
     * is asking [durable].
     */
    private val durable: ViolationRepository = InMemoryViolationRepository(),
) {

  /** What the server builds. The primary is for a test that drives its own clock. */
  @Inject constructor(durable: ViolationRepository) : this(Limits(), System::nanoTime, durable)

  data class Limits(
      /** How long a refusal keeps counting against the character that made it. */
      val window: Duration = Duration.ofMinutes(10),
      /** Refusals in one window past which the tally is said at error level. */
      val loudAt: Int = 12,
      /** Recent entries kept for [recent], across all characters. */
      val ringSize: Int = 512,
  )

  /** The kinds of refusal worth adding up. */
  enum class Kind {
    /** A position claim for somewhere the player could not have walked to. */
    IMPOSSIBLE_POSITION,
    /** Steps arriving faster than the game moves a player. */
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
    /** A packet sent out of the order or the phase its feature has. */
    OUT_OF_SEQUENCE,
  }

  /** One refusal, as an operator reads it back. */
  data class Entry(
      val at: Long,
      val characterId: Long?,
      val kind: Kind,
      val detail: String,
  )

  private data class Window(val startedAt: Long, val count: Int)

  private val windows = ConcurrentHashMap<Pair<Long, Kind>, Window>()
  private val ring = ArrayDeque<Entry>()
  private val loudAgainAt = ConcurrentHashMap<Long, Long>()
  private val total = AtomicLong()

  /** Writes one refusal down. */
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
    durable.bump(characterId, kind.name, detail, LocalDateTime.now())
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
    // Said once per window rather than once per refusal past the line, so a client that keeps
    // going does not become the log.
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

  /**
   * Keeps the table from holding a row for every character the server has ever seen. The same shape
   * [GrantBudget] uses, and for the same reason.
   */
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
