package de.fiereu.openmmo.offline

import de.fiereu.openmmo.offline.generated.GeneratedCartridgeLimits
import javax.inject.Inject
import javax.inject.Singleton

/**
 * What one cartridge, played by itself, can produce, the table an offline save is measured against
 * when it comes back online.
 */
@Singleton
class CartridgeLimits @Inject constructor() {

  /** Every species the cartridge can produce by itself. */
  val species: Set<Int> = GeneratedCartridgeLimits.SPECIES

  /** The most money one playthrough can be holding: every trainer once, with the Amulet Coin. */
  val moneyCap: Int = GeneratedCartridgeLimits.MONEY_CAP

  /**
   * The items the cartridge asks after and has no way of granting, as indices into its own item
   * table. Holding one is not something a playthrough can do.
   */
  val eventItems: Set<Int> = GeneratedCartridgeLimits.EVENT_ITEMS

  fun canProduce(dexId: Int): Boolean = dexId in species
}
