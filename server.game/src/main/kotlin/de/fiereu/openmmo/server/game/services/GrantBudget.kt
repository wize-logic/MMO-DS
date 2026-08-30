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

  /** The ceilings, per character per [window]. */
  data class Limits(
      val window: Duration = Duration.ofMinutes(1),
      val moneyGained: Int = 1_000_000,
      val itemsGained: Int = 2_000,
      val monstersGranted: Int = 20,
      /**
       * Monsters an hour, over the top of the minute above. A minute is the right window for a
       * burst and the wrong one for a grind, and it matters more since the individual behind a
       * reported capture became the server's draw ([ReportedIndividual]): the one way left to hunt
       * a shiny is to take a monster, let it go and report another, which costs a slot here every
       * time.
       */
      val monstersPerHour: Int = 300,
      /** Levels one monster may gain in a single reported battle outcome. */
      val levelsPerOutcome: Int = 10,
      /** Levels a whole party may gain per window. Well above a hard hour of grinding. */
      val levelsGained: Int = 30,
      /** Contest condition and sheen points per window. A tray of Poffins is a few hundred. */
      val contestPointsGained: Int = 1_000,
      /** Super Contest ribbons per window. A contest takes minutes and awards one. */
      val ribbonsWon: Int = 4,
      /**
       * Shiny monsters per [shinyWindow]. The roll is the server's now ([ReportedIndividual]), so a
       * shiny that gets here is real and is never taken away. The count is for the grind: at one in
       * 8,192, eight in an hour is about sixty-five thousand encounters, so passing it says
       * captures are being turned over rather than met.
       */
      val shinyGranted: Int = 8,
      val shinyWindow: Duration = Duration.ofHours(1),
      /** Story flag and var writes per window. A bound on the cost, not on what they buy. */
      val storyWrites: Int = 2_000,
  )

  enum class Kind {
    MONEY,
    ITEMS,
    MONSTERS,
    MONSTERS_HOURLY,
    LEVELS,
    CONTEST_POINTS,
    RIBBONS,
    SHINY,
    STORY_WRITES,
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
        Kind.SHINY -> limits.shinyGranted.toLong()
        Kind.STORY_WRITES -> limits.storyWrites.toLong()
      }

  /**
   * A shiny is rare enough that a minute says nothing about it, and a grind is the same shape, so
   * both take the hour. Everything else shares a window.
   */
  private fun windowFor(kind: Kind): Duration =
      if (kind == Kind.SHINY || kind == Kind.MONSTERS_HOURLY) limits.shinyWindow else limits.window

  /**
   * Keeps the table from holding a row for every character the server has ever seen. Each row is
   * measured against its own kind's window, so a shiny count inside its hour survives.
   */
  private fun prune(now: Long) {
    if (windows.size < PRUNE_THRESHOLD) return
    windows.entries.removeIf { now - it.value.startedAt >= windowFor(it.key.second).toNanos() }
  }

  private companion object {
    const val PRUNE_THRESHOLD = 1024
  }
}
