package de.fiereu.openmmo.common.enums

/**
 * The four seasons, rotating with the real calendar the way Gen 5 rotates them: every month is the
 * next season, so January, May and September are spring and a season is never more than a month
 * away.
 */
enum class Season {
  SPRING,
  SUMMER,
  AUTUMN,
  WINTER;

  companion object {
    /** The season of [month] (1..12), by the Gen 5 rule. */
    fun forMonth(month: Int): Season {
      require(month in 1..12) { "month out of range: $month" }
      return entries[(month - 1) % 4]
    }
  }
}
