package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.server.game.storage.PcVisit
import java.time.LocalDateTime

/**
 * The PCs a character may travel between: two doors into the ported regions that every character is
 * given, then the machines they have actually stood at, newest first.
 */
object PcBook {
  /** What the client's list menu shows at once (mods/openmmo/src/openmmo_dialog.c). */
  const val SHOWN = 8

  /**
   * The tile in front of the PC in Cherrygrove City's and Viridian City's Pokemon Centers, read out
   * of the map's own zone_event record: the machine is the bg event at (4, 8), used from the tile
   * south of it, facing it.
   */
  val DOORS: List<PcVisit> =
      listOf(
          door(bank = 2, map = 151), // Cherrygrove City, Johto
          door(bank = 4, map = 71), // Viridian City, Kanto
      )

  private fun door(bank: Int, map: Int) =
      PcVisit(
          characterId = 0,
          region = Region.SINNOH.wireValue.toInt(),
          bank = bank,
          map = map,
          x = 4,
          y = 9,
          elevation = 0,
          lastUsed = LocalDateTime.MIN,
      )

  /** The rows to show, in order, for a character whose used PCs are [used] (newest first). */
  fun of(used: List<PcVisit>): List<PcVisit> {
    val out = ArrayList<PcVisit>(SHOWN)
    for (door in DOORS) {
      // A door the character has stood at keeps the tile they stood on.
      out.add(used.firstOrNull { it.samePlace(door) } ?: door)
    }
    for (visit in used) {
      if (out.size >= SHOWN) break
      if (out.none { it.samePlace(visit) }) out.add(visit)
    }
    return out
  }
}
