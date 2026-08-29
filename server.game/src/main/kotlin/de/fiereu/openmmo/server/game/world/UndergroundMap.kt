package de.fiereu.openmmo.server.game.world

import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.Region

/** Sinnoh's Underground, as an address, and the one rule the server keeps about it. */
object UndergroundMap {

  val REGION_ID: Byte = Region.SINNOH.wireValue
  const val BANK_ID: Byte = 0
  const val MAP_ID: Byte = 2

  fun isUnderground(regionId: Byte, bankId: Byte, mapId: Byte): Boolean =
      regionId == REGION_ID && bankId == BANK_ID && mapId == MAP_ID
}

/** Where a descent came from, so the climb has somewhere to land even after a disconnect. */
data class UndergroundExit(
    val regionId: Byte,
    val bankId: Byte,
    val mapId: Byte,
    val x: Short,
    val y: Short,
    val facing: Direction,
)
