package de.fiereu.openmmo.codegen.offline

/** A species the cartridge names but cannot hand out, and the item that would be needed. */
data class ParsedEventSpecies(val dexId: Int, val name: String, val gateItem: String)

/** What one cartridge, played by itself, can produce and pay out. */
data class ParsedCartridgeLimits(
    val speciesIds: List<Int>,
    val directSpeciesIds: List<Int>,
    val eventSpecies: List<ParsedEventSpecies>,
    /** Item indices in the cartridge's own table; the wire adds its block base. */
    val eventItemIds: List<Int>,
    val moneyCap: Int,
    val trainerCount: Int,
)
