package de.fiereu.openmmo.common

/**
 * Where a character wakes up after their whole party faints, taken from the decomp's heal location
 * table. Entering a map that sets a respawn (the decomp `setrespawn`) stores its entry here, and
 * blacking out warps back to it.
 */
data class HealLocation(
    val regionId: Byte,
    val bankId: Byte,
    val mapId: Byte,
    val x: Short,
    val y: Short,
)
