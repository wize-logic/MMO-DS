package de.fiereu.openmmo.codegen.maps

/**
 * Sinnoh, out of pokeplatinum. Region 3 is the client's own numbering: it splits region ids into
 * GBA {0,1} and DS {2,3,4}, and orders them by generation, which puts Sinnoh third.
 */
object SinnohConstants :
    RegionConstants(
        name = "sinnoh",
        regionId = 3,
    )
