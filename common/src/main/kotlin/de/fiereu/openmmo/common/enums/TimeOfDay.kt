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

    /** The bucket [hour] (0..23) falls in. */
    fun forHour(hour: Int): TimeOfDay {
      require(hour in 0..23) { "hour out of range: $hour" }
      return BY_HOUR[hour]
    }
  }
}
