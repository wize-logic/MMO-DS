@import de.fiereu.openmmo.codegen.offline.ParsedCartridgeLimits
@import de.fiereu.openmmo.codegen.offline.RenderUtil

@param ParsedCartridgeLimits limits
package de.fiereu.openmmo.offline.generated

internal object GeneratedCartridgeLimits {

  /** The most money one playthrough of the cartridge can be holding. */
  const val MONEY_CAP = ${limits.getMoneyCap()}

  /**
   * Every species the cartridge can produce by itself, closed over evolution and breeding.
@for(var event : limits.getEventSpecies())
   *
   * Not here: ${event.getName()}, which needs ${event.getGateItem()}.
@endfor
   */
  val SPECIES: Set<Int> =
      setOf(
${RenderUtil.INSTANCE.speciesRows(limits.getSpeciesIds())}
      )

  /**
   * The items the cartridge asks after and cannot put in a bag, as indices into its own item table.
   */
  val EVENT_ITEMS: Set<Int> =
      setOf(
${RenderUtil.INSTANCE.speciesRows(limits.getEventItemIds())}
      )
}
