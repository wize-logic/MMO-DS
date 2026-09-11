package de.fiereu.openmmo.server.game.services

import io.github.oshai.kotlinlogging.KotlinLogging
import java.time.Duration
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/** How much a session may be *given* by its own client in a stretch of play. */
@Singleton
class GrantBudget(private val limits: Limits, private val clock: () -> Long) {

  /** What the server builds. The other constructor is for a test that drives its own clock. */
  @Inject constructor() : this(Limits(), System::nanoTime)

  /** The ceilings, per character per window. */
  data class Limits(
      val window: Duration = Duration.ofMinutes(1),
      val moneyGained: Int = 1_000_000,
      val itemsGained: Int = 2_000,
      val monstersGranted: Int = 20,
      /** Monsters an hour, over the top of the minute above. */
      val monstersPerHour: Int = 300,
      /** Levels one monster may gain in a single reported battle outcome. */
      val levelsPerOutcome: Int = 10,
      /**
       * Levels a character's whole party may gain per window. The per-outcome cap above bounds one
       * report; this bounds how many reports are worth sending.
       */
      val levelsGained: Int = 30,
      /**
       * Contest condition and sheen points per window. A Poffin raises one condition and the sheen
       * that caps it; a tray of them is a few hundred points, so a thousand a minute is a player
       * cooking as fast as the screen allows.
       */
      val contestPointsGained: Int = 1_000,
      /**
       * Super Contest ribbons per window. A contest is minutes of play and awards exactly one, so
       * four a minute cannot be reached by playing and an unearned set cannot be claimed at once.
       */
      val ribbonsWon: Int = 4,
      /** Friendship points a character's whole party may gain per window. */
      val friendshipGained: Int = 500,
      /** Shiny monsters per [shinyWindow]. */
      val shinyGranted: Int = 8,
      val shinyWindow: Duration = Duration.ofHours(1),
      /**
       * Story flag and var writes per window. These gate no server behaviour today, so the bound is
       * not on what they buy but on what they cost: each one is a write to the character's own
       * story rows, and a full packet of them is twenty thousand.
       */
      val storyWrites: Int = 2_000,
      /** Offline saves one character may bring in per [shinyWindow]. */
      val importsGranted: Int = 24,
  )

  enum class Kind {
    MONEY,
    ITEMS,
    MONSTERS,
    MONSTERS_HOURLY,
    LEVELS,
    CONTEST_POINTS,
    RIBBONS,
    FRIENDSHIP,
    SHINY,
    STORY_WRITES,
    IMPORT,
  }

  private data class Window(val startedAt: Long, val spent: Long)

  private val windows = ConcurrentHashMap<Pair<Long, Kind>, Window>()

  val levelsPerOutcome: Int
    get() = limits.levelsPerOutcome

  /**
   * Counts [amount] of [kind] against [characterId] and answers whether it is still inside the
   * window's allowance. A refused claim is still counted: a client that keeps asking stays refused
   * for the rest of its window rather than being handed the difference.
   */
  fun allow(characterId: Long, kind: Kind, amount: Int): Boolean {
    if (amount <= 0) return true
    val limit = limitFor(kind)
    val now = clock()
    val windowNanos = windowFor(kind).toNanos()
    val updated =
        windows.compute(characterId to kind) { _, current ->
          if (current == null || now - current.startedAt >= windowNanos)
              Window(now, amount.toLong())
          else current.copy(spent = current.spent + amount)
        }!!
    prune(now)
    if (updated.spent <= limit) return true
    log.warn {
      "char=$characterId claimed $amount more $kind than its client may grant" +
          " (${updated.spent} against $limit per ${windowFor(kind).toSeconds()}s), refused"
    }
    return false
  }

  private fun limitFor(kind: Kind): Long =
      when (kind) {
        Kind.MONEY -> limits.moneyGained.toLong()
        Kind.ITEMS -> limits.itemsGained.toLong()
        Kind.MONSTERS -> limits.monstersGranted.toLong()
        Kind.MONSTERS_HOURLY -> limits.monstersPerHour.toLong()
        Kind.LEVELS -> limits.levelsGained.toLong()
        Kind.CONTEST_POINTS -> limits.contestPointsGained.toLong()
        Kind.RIBBONS -> limits.ribbonsWon.toLong()
        Kind.FRIENDSHIP -> limits.friendshipGained.toLong()
        Kind.SHINY -> limits.shinyGranted.toLong()
        Kind.STORY_WRITES -> limits.storyWrites.toLong()
        Kind.IMPORT -> limits.importsGranted.toLong()
      }

  /**
   * A shiny is rare enough that a minute says nothing about it, and a grind is the same shape, so
   * both are measured over the hour. Every other kind uses the minute.
   */
  private fun windowFor(kind: Kind): Duration =
      if (kind == Kind.SHINY || kind == Kind.MONSTERS_HOURLY || kind == Kind.IMPORT)
          limits.shinyWindow
      else limits.window

  /**
   * Keeps the table from holding a row for every character the server has ever seen. Each row is
   * measured against its own kind's window, so pruning cannot drop a shiny count that is still
   * inside its hour because the minute-long kinds have turned over.
   */
  private fun prune(now: Long) {
    if (windows.size < PRUNE_THRESHOLD) return
    windows.entries.removeIf { now - it.value.startedAt >= windowFor(it.key.second).toNanos() }
  }

  private companion object {
    const val PRUNE_THRESHOLD = 1024
  }
}
