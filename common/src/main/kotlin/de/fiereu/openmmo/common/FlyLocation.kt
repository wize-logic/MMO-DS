package de.fiereu.openmmo.common

/** Where Fly lands, taken from the same decomp table [HealLocation] comes from. */
data class FlyLocation(
    val mapHeaderId: Int,
    val x: Int,
    val z: Int,
    val firstArrivalFlagId: Int,
)
