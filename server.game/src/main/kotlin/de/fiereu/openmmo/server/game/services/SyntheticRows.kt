package de.fiereu.openmmo.server.game.services

import de.fiereu.openmmo.server.game.script.Badge

/** Rows of the 0xCB report and seat that are not the engine's `VarsFlags` block. */
internal object SyntheticRows {
  /** The running shoes, stored as a plain vm flag; named here only so the band reads whole. */
  const val RUNNING_SHOES = 3000

  /** Flag ids of the eight Sinnoh badges, in `BADGE_ID_*` order: this plus the badge's ordinal. */
  const val BADGE_BASE = 3001

  /** The var id carrying the engine's black-out warp id, `spawn_locations.c`'s 1-based row. */
  const val RESPAWN = 3009

  /** The badge a flag id names, or null for a flag that is not one. */
  fun badgeOf(flagId: Int): Badge? =
      (flagId - BADGE_BASE).takeIf { it in 0 until Badge.PER_REGION }?.let { Badge.entries[it] }
}
