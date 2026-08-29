package de.fiereu.openmmo.server.game.script

/**
 * The 25 stock Poketch app ids, in the decomp's `POKETCH_APPID_*` order, so an ordinal here is that
 * id. Cycle order on the device is this order: `Poketch_IncrementAppID` walks the registry by
 * index, not the overlay-lookup table.
 */
enum class PoketchApp {
  DIGITALWATCH,
  CALCULATOR,
  MEMOPAD,
  PEDOMETER,
  PARTYSTATUS,
  FRIENDSHIPCHECKER,
  DOWSINGMACHINE,
  BERRYSEARCHER,
  DAYCARECHECKER,
  POKEMONHISTORY,
  COUNTER,
  ANALOGWATCH,
  MARKINGMAP,
  LINKSEARCHER,
  COINTOSS,
  MOVETESTER,
  CALENDAR,
  DOTART,
  ROULETTE,
  TRAINERCOUNTER,
  KITCHENTIMER,
  COLORCHANGER,
  MATCHUPCHECKER,
  STOPWATCH,
  ALARMCLOCK;

  /** The decomp `POKETCH_APPID_*` value. */
  val id: Int
    get() = ordinal

  /** The story key this app is held under for a player in [regionName]. */
  fun keyIn(regionName: String): String = "$regionName/POKETCH_APP_$name"

  companion object {
    /**
     * The six ids a session registers, in cycle order. Chat takes 0, party keeps 4 as the stock
     * monitor, and 1 / 2 / 3 / 5 follow so Increment walks chat → friends → guild → map → party →
     * net.
     */
    val ROSTER: List<PoketchApp> =
        listOf(
            DIGITALWATCH,
            CALCULATOR,
            MEMOPAD,
            PEDOMETER,
            PARTYSTATUS,
            FRIENDSHIPCHECKER,
        )

    /** The roster minus party, which stays the engine's until the party monitor is fed. */
    val TAKEN: Set<PoketchApp> =
        setOf(DIGITALWATCH, CALCULATOR, MEMOPAD, PEDOMETER, FRIENDSHIPCHECKER)

    fun byId(id: Int): PoketchApp? = entries.getOrNull(id)
  }
}
