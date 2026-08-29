package de.fiereu.openmmo.common.pvp

/** How many battlers a side puts out, as the competitive wire numbers it. */
enum class BattleKind(val id: Byte, val nameStringId: Int) {
  SINGLE(0, 5540),
  DOUBLE(1, 5541),
  MULTI(2, 5542),
  MULTI_COOP(3, 5543),
  UNNAMED(4, 0);

  companion object {
    fun fromId(id: Byte): BattleKind? = entries.firstOrNull { it.id == id }
  }
}

/** How many battles a match is. */
enum class SeriesRule(val id: Byte, val wins: Int, val nameStringId: Int) {
  NORMAL(0, 0, 115),
  BEST_OF_3(1, 2, 117),
  BEST_OF_5(2, 3, 118),
  BEST_OF_7(3, 4, 119);

  /** How many battles this series can run to at most. */
  val maxBattles: Int
    get() = if (wins == 0) 1 else wins * 2 - 1

  companion object {
    fun fromId(id: Byte): SeriesRule? = entries.firstOrNull { it.id == id }
  }
}

/** The clock a format puts on a side. */
enum class TimerRule(val id: Byte, val nameStringId: Int) {
  NONE(1, 129),
  NORMAL(2, 130),
  UNNAMED_3(3, 0),
  UNNAMED_4(4, 0),
  UNNAMED_5(5, 0),
  UNNAMED_6(6, 0),
  DOUBLES_OFFICIAL(7, 131);

  /** True while this format runs a clock at all. */
  val timed: Boolean
    get() = this != NONE

  companion object {
    fun fromId(id: Byte): TimerRule? = entries.firstOrNull { it.id == id }
  }
}

/**
 * The sixteen groups a format checks a party against, and the first six of which are also the tiers
 * a species belongs to.
 */
enum class TierGroup(val id: Byte, val nameStringId: Int, val speciesTier: Boolean) {
  UBERS(0, 5750, true),
  OVER_USED(1, 5751, true),
  UNDER_USED(2, 5752, true),
  NEVER_USED(3, 5753, true),
  UNLIMITED(4, 5797, true),
  UNTIERED(5, 5755, true),
  RANDOMS(6, 5757, false),
  DOUBLES(7, 5756, false),
  DOUBLES_OFFICIAL(8, 5758, false),
  ROTATION(9, 107, false),
  TRIPLE(10, 104, false),
  RANDOMS_HALLOWEEN(11, 5759, false),
  RANDOMS_XMAS(12, 5754, false),
  UNLIMITED_ALT(13, 5797, false),
  RANDOMS_EVENT(14, 7200, false),
  RANDOMS_LNY(15, 7201, false);

  /** This group's bit in a species' tier mask. */
  val bit: Int
    get() = 1 shl id.toInt()

  /** The long name's string id, by the source table's own offset rule. */
  val descriptionStringId: Int
    get() = if (nameStringId >= 7200) nameStringId + 100 else nameStringId + 20

  companion object {
    fun fromId(id: Byte): TierGroup? = entries.firstOrNull { it.id == id }

    /**
     * The groups a species' sixteen-bit mask names, lowest bit first. The first of these is the
     * primary, which is the one the client prints.
     */
    fun fromMask(mask: Int): List<TierGroup> = entries.filter { mask and it.bit != 0 }
  }
}
