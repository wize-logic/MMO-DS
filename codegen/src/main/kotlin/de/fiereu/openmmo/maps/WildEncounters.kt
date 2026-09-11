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
 * What makes a [WildEncounterOverride] apply. A Gen 4 grass table is one fixed list of twelve slots
 * whose *species* are swapped out before the roll; the level and the weight of the slot stay the
 * table's own either way.
 */
enum class EncounterVariant(val isDualSlot: Boolean = false) {
  /** Replaces the morning species while it is day or twilight. */
  DAY,
  /** Replaces the morning species while it is night or late night. */
  NIGHT,
  /** The day's swarm, on the one map the swarm is on. */
  SWARM,
  /** The Poke Radar's patch, when the patch shook rather than rustled. */
  RADAR,
  /** A GBA cartridge in the console's second slot, once the national dex is obtained. */
  DUAL_SLOT_RUBY(true),
  DUAL_SLOT_SAPPHIRE(true),
  DUAL_SLOT_EMERALD(true),
  DUAL_SLOT_FIRERED(true),
  DUAL_SLOT_LEAFGREEN(true),
}

/**
 * Everything a table needs to know that is not the table's own, gathered in the order
 * `WildEncounters_TryWildEncounter` asks its questions.
 */
data class EncounterConditions(
    val timeOfDay: TimeOfDay,
    /** True only on the one map the day's swarm is on (`Swarm_GetMapId`). */
    val swarming: Boolean = false,
    /**
     * True when the step came out of a Poke Radar patch that shook rather than rustled, the
     * engine's `shakeType == 1`, and the only case in which the radar species reach the table.
     */
    val radarPatch: Boolean = false,
    /** Which cartridge the second slot holds, or null for an empty slot. */
    val dualSlot: EncounterVariant? = null,
    /**
     * What the day puts over slots 6 and 7, in order, and empty for every map that has no such
     * pair, which is all of them but the six Great Marsh areas and the Trophy Garden.
     */
    val dailySpecies: List<Int> = emptyList(),
) {
  init {
    require(dualSlot == null || dualSlot.isDualSlot) {
      "$dualSlot is not a second-cartridge variant"
    }
  }
}

/**
 * A conditional swap into a table's slots: [speciesIds] replace the species of [slots], position
 * for position, while [variant] holds. The slot indices are the game's own, it writes into fixed
 * positions of the table it just built, not into positions it looks up.
 */
data class WildEncounterOverride(
    val variant: EncounterVariant,
    val slots: List<Int>,
    val speciesIds: List<Int>,
)

/**
 * Form selectors that belong to a map's whole encounter archive rather than to one of its tables.
 * The game reads them off the same file the tables come from and applies them to whatever the roll
 * produced.
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
 * The wild monsters met by one [method] on a map. [encounterRate] drives how often a step meets
 * anything, the per-slot weights decide which one.
 */
data class WildEncounterTable(
    val method: EncounterMethod,
    val encounterRate: Int,
    val slots: List<WildEncounterSlot>,
    val overrides: List<WildEncounterOverride> = emptyList(),
) {

  /** The table as it stands at [timeOfDay] with nothing else in play. */
  fun slotsAt(timeOfDay: TimeOfDay): List<WildEncounterSlot> =
      slotsAt(EncounterConditions(timeOfDay))

  /**
   * The table as it stands under [conditions]. Morning with nothing else in play is the table as
   * authored; every variant that holds swaps in its own species and keeps the slot's level and
   * weight.
   */
  fun slotsAt(conditions: EncounterConditions): List<WildEncounterSlot> {
    if (overrides.isEmpty() && conditions.dailySpecies.isEmpty()) return slots
    val swapped = slots.toMutableList()
    timeVariant(conditions.timeOfDay)?.let { swap(swapped, it) }
    if (conditions.swarming) swap(swapped, EncounterVariant.SWARM)
    conditions.dualSlot?.let { swap(swapped, it) }
    if (conditions.radarPatch) swap(swapped, EncounterVariant.RADAR)
    swapDaily(swapped, conditions.dailySpecies)
    return swapped
  }

  /**
   * Writes the day's pair over [DAILY_SLOTS]. A table shorter than that is left alone, which is
   * what keeps the marsh's own water and rod tables out of it: only the grass table has twelve
   * slots, and the cartridge only ever writes this pair into that one.
   */
  private fun swapDaily(into: MutableList<WildEncounterSlot>, species: List<Int>) {
    species.forEachIndexed { i, speciesId ->
      val slot = DAILY_SLOTS.getOrNull(i) ?: return@forEachIndexed
      val current = into.getOrNull(slot) ?: return@forEachIndexed
      into[slot] = current.copy(speciesId = speciesId)
    }
  }

  companion object {
    /** The two slots the Trophy Garden's pair and the Great Marsh's daily pair are written over. */
    val DAILY_SLOTS: List<Int> = listOf(6, 7)
  }

  private fun timeVariant(timeOfDay: TimeOfDay): EncounterVariant? =
      when (timeOfDay) {
        TimeOfDay.DAY,
        TimeOfDay.TWILIGHT -> EncounterVariant.DAY
        TimeOfDay.NIGHT,
        TimeOfDay.LATE_NIGHT -> EncounterVariant.NIGHT
        TimeOfDay.MORNING -> null
      }

  /**
   * Writes [variant]'s species into the positions it names, position for position. A table that
   * carries no such override is left alone, which is how a map with no swarm reads under one.
   */
  private fun swap(into: MutableList<WildEncounterSlot>, variant: EncounterVariant) {
    val override = overrides.firstOrNull { it.variant == variant } ?: return
    override.slots.forEachIndexed { i, slot ->
      val species = override.speciesIds.getOrNull(i) ?: return@forEachIndexed
      val current = into.getOrNull(slot) ?: return@forEachIndexed
      into[slot] = current.copy(speciesId = species)
    }
  }
}
