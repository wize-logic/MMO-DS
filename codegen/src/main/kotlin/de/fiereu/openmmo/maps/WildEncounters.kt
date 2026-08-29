package de.fiereu.openmmo.maps

import de.fiereu.openmmo.common.enums.EncounterMethod
import de.fiereu.openmmo.common.enums.TimeOfDay

/** One slot in a wild encounter table: a species, its level range, and its pick weight. */
data class WildEncounterSlot(
    val speciesId: Int,
    val minLevel: Int,
    val maxLevel: Int,
    val weight: Int,
)

/**
 * What makes a [WildEncounterOverride] apply. A Gen 4 grass table is one fixed list of twelve
 * slots whose *species* are swapped out before the roll; the level and the weight of the slot
 * stay the table's own either way.
 */
enum class EncounterVariant {
  /** Replaces the morning species while it is day or twilight. */
  DAY,
  /** Replaces the morning species while it is night or late night. */
  NIGHT,
  /** The day's swarm, on the one map the swarm is on. */
  SWARM,
  /** The Poke Radar's patch, when the patch shook rather than rustled. */
  RADAR,
  /** A GBA cartridge in the console's second slot, once the national dex is obtained. */
  DUAL_SLOT_RUBY,
  DUAL_SLOT_SAPPHIRE,
  DUAL_SLOT_EMERALD,
  DUAL_SLOT_FIRERED,
  DUAL_SLOT_LEAFGREEN,
}

/**
 * A conditional swap into a table's slots: [speciesIds] replace the species of [slots],
 * position for position, while [variant] holds. The slot indices are the game's own, it
 * writes into fixed positions of the table it just built, not into positions it looks up.
 */
data class WildEncounterOverride(
    val variant: EncounterVariant,
    val slots: List<Int>,
    val speciesIds: List<Int>,
)

/**
 * Form selectors that belong to a map's whole encounter archive rather than to one of its
 * tables. The game reads them off the same file the tables come from and applies them to
 * whatever the roll produced.
 */
data class WildEncounterForms(
    /** 0 for the west sea Shellos, non-zero for the east. */
    val shellosForm: Int = 0,
    /** 0 for the west sea Gastrodon, non-zero for the east. */
    val gastrodonForm: Int = 0,
    /** Which group of Unown letters this map spawns; 0 is "no Unown here". */
    val unownTableId: Int = 0,
    /**
     * The three form words the archive carries that the game itself never reads, kept in file order
     * so a later reader has them rather than a guess.
     */
    val unusedFormWords: List<Int> = emptyList(),
)

/**
 * The wild monsters met by one [method] on a map. [encounterRate] drives how often a step
 * meets anything, the per-slot weights decide which one.
 */
data class WildEncounterTable(
    val method: EncounterMethod,
    val encounterRate: Int,
    val slots: List<WildEncounterSlot>,
    val overrides: List<WildEncounterOverride> = emptyList(),
) {

  /**
   * The table as it stands at [timeOfDay]. Morning is the table as authored; the other buckets
   * swap in their own species and keep the slot's level and weight.
   */
  fun slotsAt(timeOfDay: TimeOfDay): List<WildEncounterSlot> {
    val variant =
        when (timeOfDay) {
          TimeOfDay.DAY,
          TimeOfDay.TWILIGHT -> EncounterVariant.DAY
          TimeOfDay.NIGHT,
          TimeOfDay.LATE_NIGHT -> EncounterVariant.NIGHT
          TimeOfDay.MORNING -> return slots
        }
    val override = overrides.firstOrNull { it.variant == variant } ?: return slots
    val swapped = slots.toMutableList()
    override.slots.forEachIndexed { i, slot ->
      val species = override.speciesIds.getOrNull(i) ?: return@forEachIndexed
      val current = swapped.getOrNull(slot) ?: return@forEachIndexed
      swapped[slot] = current.copy(speciesId = species)
    }
    return swapped
  }
}
