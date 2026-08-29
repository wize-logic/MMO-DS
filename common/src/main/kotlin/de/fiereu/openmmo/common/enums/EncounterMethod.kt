package de.fiereu.openmmo.common.enums

/** How a wild monster is met on a map. Each method has its own table and slot weights. */
enum class EncounterMethod {
  LAND,
  WATER,
  ROCK_SMASH,
  FISHING,
  // Gen 4 gives each rod its own table and its own slot weights, so collapsing the three onto
  // FISHING would throw two of them away.
  OLD_ROD,
  GOOD_ROD,
  SUPER_ROD,
}
