package de.fiereu.openmmo.common.enums

/**
 * The five daylight buckets a Gen 4 clock divides a day into. The order is the game's own, so an
 * ordinal here means the same thing it means there.
 */
enum class TimeOfDay {
  MORNING,
  DAY,
  TWILIGHT,
  NIGHT,
  LATE_NIGHT;

  companion object {
    // rtc.c TimeOfDayForHour's 24 entry lookup, hour by hour.
    private val BY_HOUR =
        listOf(
            LATE_NIGHT,
            LATE_NIGHT,
            LATE_NIGHT,
            LATE_NIGHT,
            MORNING,
            MORNING,
            MORNING,
            MORNING,
            MORNING,
            MORNING,
            DAY,
            DAY,
            DAY,
            DAY,
            DAY,
            DAY,
            DAY,
            TWILIGHT,
            TWILIGHT,
            TWILIGHT,
            NIGHT,
            NIGHT,
            NIGHT,
            NIGHT,
        )

    /** The bucket [hour] (0..23) falls in, on the cartridge's own spring-shaped day. */
    fun forHour(hour: Int): TimeOfDay {
      require(hour in 0..23) { "hour out of range: $hour" }
      return BY_HOUR[hour]
    }

    /**
     * When each bucket of [season]'s day begins: morning, day, twilight, night, in hours. Late
     * night is what is left before morning.
     */
    fun boundaries(season: Season): IntArray =
        when (season) {
          Season.SPRING -> intArrayOf(4, 10, 17, 20)
          Season.SUMMER -> intArrayOf(3, 9, 18, 21)
          Season.AUTUMN -> intArrayOf(5, 11, 16, 19)
          Season.WINTER -> intArrayOf(6, 12, 15, 18)
        }

    /** The bucket [hour] (0..23) falls in during [season]. */
    fun forHour(hour: Int, season: Season): TimeOfDay {
      require(hour in 0..23) { "hour out of range: $hour" }
      val (morning, day, twilight, night) =
          boundaries(season).let { listOf(it[0], it[1], it[2], it[3]) }
      return when {
        hour < morning -> LATE_NIGHT
        hour < day -> MORNING
        hour < twilight -> DAY
        hour < night -> TWILIGHT
        else -> NIGHT
      }
    }
  }
}
