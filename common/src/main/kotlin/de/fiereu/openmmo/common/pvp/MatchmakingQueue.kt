package de.fiereu.openmmo.common.pvp

/** One line on the matchmaking window's tab strip. */
enum class MatchmakingQueue(
    val id: Byte,
    val family: QueueFamily,
    val ranked: Boolean,
    val displayOrder: Int,
    val nameStringId: Int,
    val enabled: Boolean,
    val ownParty: Boolean,
) {
  OVER_USED(0, QueueFamily.OVER_USED, false, 0, 5751, true, true),
  OVER_USED_RANKED(1, QueueFamily.OVER_USED, true, 0, 5761, true, true),
  DOUBLES(2, QueueFamily.DOUBLES, false, 4, 5756, false, true),
  DOUBLES_RANKED(3, QueueFamily.DOUBLES, true, 4, 5764, false, true),
  UNDER_USED(4, QueueFamily.UNDER_USED, false, 1, 5752, true, true),
  UNDER_USED_RANKED(5, QueueFamily.UNDER_USED, true, 1, 5762, true, true),
  NEVER_USED(6, QueueFamily.NEVER_USED, false, 2, 5753, true, true),
  NEVER_USED_RANKED(7, QueueFamily.NEVER_USED, true, 2, 5763, true, true),
  RANDOMS(8, QueueFamily.RANDOMS, false, 5, 5757, true, false),
  RANDOMS_RANKED(9, QueueFamily.RANDOMS, true, 5, 5765, true, false),
  DOUBLES_OFFICIAL(10, QueueFamily.DOUBLES_OFFICIAL, false, 3, 5758, true, true),
  DOUBLES_OFFICIAL_RANKED(11, QueueFamily.DOUBLES_OFFICIAL, true, 3, 5766, true, true),
  RANDOMS_HALLOWEEN(12, QueueFamily.RANDOMS_HALLOWEEN, false, 6, 5759, false, false),
  RANDOMS_HALLOWEEN_RANKED(13, QueueFamily.RANDOMS_HALLOWEEN, true, 6, 5767, false, false),
  RANDOMS_XMAS(14, QueueFamily.RANDOMS_XMAS, false, 7, 5754, false, false),
  RANDOMS_XMAS_RANKED(15, QueueFamily.RANDOMS_XMAS, true, 7, 5768, false, false),
  RANDOMS_EVENT(16, QueueFamily.RANDOMS_EVENT, false, 8, 7200, false, false),
  RANDOMS_EVENT_RANKED(17, QueueFamily.RANDOMS_EVENT, true, 8, 7250, false, false),
  RANDOMS_LNY(18, QueueFamily.RANDOMS_LNY, false, 9, 7201, false, false),
  RANDOMS_LNY_RANKED(19, QueueFamily.RANDOMS_LNY, true, 9, 7251, false, false);

  /** True while a finished match here moves a rating and is recorded on a board. */
  val recorded: Boolean
    get() = ranked

  companion object {
    fun fromId(id: Byte): MatchmakingQueue? = entries.firstOrNull { it.id == id }

    /** The pair a family offers, unranked first. */
    fun of(family: QueueFamily): List<MatchmakingQueue> =
        entries.filter { it.family == family }.sortedBy { it.ranked }
  }
}

/** The ten queue families, and whether each is a standing queue or a seasonal one. */
enum class QueueFamily(val seasonal: Boolean) {
  OVER_USED(false),
  UNDER_USED(false),
  NEVER_USED(false),
  DOUBLES(false),
  DOUBLES_OFFICIAL(false),
  RANDOMS(false),
  RANDOMS_HALLOWEEN(true),
  RANDOMS_XMAS(true),
  RANDOMS_EVENT(true),
  RANDOMS_LNY(true),
}
