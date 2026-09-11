package de.fiereu.openmmo.server.game.offline.verify

/**
 * Everything the operator gets to set about replay verification, in the units the thing is actually
 * measured in.
 */
data class ReplayLimits(
    /** One session. 12 hours; past it the client tells the player to save and start a new one. */
    val linkFrames: Long = hours(12),
    /** One chain, which is every session between the anchor and the save being asked about. */
    val chainFrames: Long = hours(60),
    /** What one account may spend of the server's replay in a rolling week. */
    val accountFramesPerWeek: Long = hours(60),
    /** Recordings are tiny, a heavy week is fifteen megabytes, so these are ceilings, not fits. */
    val recordingBytes: Int = 16 * 1024 * 1024,
    val chainBytes: Long = 16L * 1024 * 1024,
    /** Chains waiting at once, across everybody. */
    val queueDepth: Int = 100,
    /**
     * Replays at a time. A banded replay spends many cores for a doubling, so single-threaded runs
     * side by side are the better use of the same machine.
     */
    val cores: Int = 1,
    /** A request older than this answers UNVERIFIABLE with "the queue", and may be asked again. */
    val queueDays: Int = 7,
    /** Replay a player may have for nothing in a rolling week: a session or two. */
    val freeHours: Int = 6,
    /** And what an hour costs after that, in game money. A sink, and a rate limit that says so. */
    val feePerHour: Int = 5_000,
    /**
     * How far past its last input a crashed session is run before its image is compared. An in-game
     * save completes within this of the press that confirmed it, and the file cannot change again
     * without input.
     */
    val crashMargin: Long = 600,
) {

  init {
    require(linkFrames > 0 && chainFrames > 0) { "a cap of no frames verifies nothing" }
    require(cores >= 0) { "cores is a count" }
  }

  /** Whether this account has already spent its week. Frames, so idle time is priced like play. */
  fun weeklyBudgetLeft(spentThisWeek: Long): Long =
      (accountFramesPerWeek - spentThisWeek).coerceAtLeast(0)

  /**
   * What replaying [frames] costs the player, rounded up to the hour and after the free allowance.
   */
  fun fee(frames: Long, freeFramesLeft: Long): Int {
    val charged = (frames - freeFramesLeft.coerceAtLeast(0)).coerceAtLeast(0)
    if (charged == 0L) return 0
    val perHour = hours(1)
    val chargedHours = (charged + perHour - 1) / perHour
    return (chargedHours * feePerHour).coerceAtMost(Int.MAX_VALUE.toLong()).toInt()
  }

  val freeFrames: Long
    get() = hours(freeHours.toLong())

  companion object {
    /** The console's rate, which is the frame the recording's numbers count. */
    const val FRAMES_PER_SECOND = 60L

    fun hours(n: Long): Long = n * 3600L * FRAMES_PER_SECOND

    /**
     * The operator's own numbers, or these. An unreadable one is the default and a line in the log:
     * a verifier that refused to start over a typo would take the whole game with it, and every one
     * of these is a bound on work this server chooses to do for free.
     */
    fun fromEnvironment(env: (String) -> String? = System::getenv): ReplayLimits {
      fun hoursOf(name: String, fallback: Long): Long =
          env(name)?.trim()?.toLongOrNull()?.takeIf { it > 0 }?.let { hours(it) } ?: fallback

      fun intOf(name: String, fallback: Int, min: Int): Int =
          env(name)?.trim()?.toIntOrNull()?.takeIf { it >= min } ?: fallback

      val d = ReplayLimits()
      return d.copy(
          linkFrames = hoursOf("VERIFY_LINK_HOURS", d.linkFrames),
          chainFrames = hoursOf("VERIFY_CHAIN_HOURS", d.chainFrames),
          accountFramesPerWeek = hoursOf("VERIFY_ACCOUNT_HOURS", d.accountFramesPerWeek),
          queueDepth = intOf("VERIFY_QUEUE_DEPTH", d.queueDepth, 0),
          cores = intOf("VERIFY_CORES", d.cores, 0),
          queueDays = intOf("VERIFY_QUEUE_DAYS", d.queueDays, 1),
          freeHours = intOf("VERIFY_FREE_HOURS", d.freeHours, 0),
          feePerHour = intOf("VERIFY_FEE_PER_HOUR", d.feePerHour, 0),
      )
    }
  }
}
