package de.fiereu.openmmo.server.game.script

/**
 * The eight gym badges, in the decomp's own `BADGE_ID_*` order, so an ordinal here is that id.
 */
enum class Badge {
  COAL,
  FOREST,
  COBBLE,
  FEN,
  RELIC,
  MINE,
  ICICLE,
  BEACON;

  /** The story key this badge is held under for a player in [regionName]. */
  fun keyIn(regionName: String): String = "$regionName/BADGE_ID_$name"

  companion object {
    /** The wire id of a badge, which is its decomp `BADGE_ID_*` value. */
    fun wireId(badge: Badge): Short = badge.ordinal.toShort()
  }
}
