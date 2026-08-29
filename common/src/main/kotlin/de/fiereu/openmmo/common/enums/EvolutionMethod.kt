package de.fiereu.openmmo.common.enums

/**
 * What an evolution entry's parameter holds. The parameter is one 16 bit number in the data,
 * so its meaning is only knowable from the method beside it, and a level and an item id are
 * otherwise indistinguishable.
 */
enum class EvolutionParam {
  /** No parameter; the entry is two fields wide. */
  NONE,
  /** The level at or above which the evolution happens. */
  LEVEL,
  /** The beauty condition stat at or above which the evolution happens. */
  BEAUTY,
  /** An item id, either held or used depending on the method. */
  ITEM,
  /** A move id the monster must know. */
  MOVE,
  /** A species id that must be elsewhere in the party. */
  SPECIES,
}

/** What made the game look for an evolution at all. */
enum class EvolutionTrigger {
  LEVEL_UP,
  TRADE,
  ITEM_USED,
}

/**
 * How a species evolves. The names and their order are the game's own `EVO_*` table; the [id]
 * is the number the data stores and the generator refuses to run if the table it reads has
 * drifted from this enum.
 */
enum class EvolutionMethod(val id: Int, val param: EvolutionParam, val trigger: EvolutionTrigger) {
  NONE(0, EvolutionParam.NONE, EvolutionTrigger.LEVEL_UP),
  LEVEL_HAPPINESS(1, EvolutionParam.NONE, EvolutionTrigger.LEVEL_UP),
  LEVEL_HAPPINESS_DAY(2, EvolutionParam.NONE, EvolutionTrigger.LEVEL_UP),
  LEVEL_HAPPINESS_NIGHT(3, EvolutionParam.NONE, EvolutionTrigger.LEVEL_UP),
  LEVEL(4, EvolutionParam.LEVEL, EvolutionTrigger.LEVEL_UP),
  TRADE(5, EvolutionParam.NONE, EvolutionTrigger.TRADE),
  TRADE_WITH_HELD_ITEM(6, EvolutionParam.ITEM, EvolutionTrigger.TRADE),
  USE_ITEM(7, EvolutionParam.ITEM, EvolutionTrigger.ITEM_USED),
  LEVEL_ATK_GT_DEF(8, EvolutionParam.LEVEL, EvolutionTrigger.LEVEL_UP),
  LEVEL_ATK_EQ_DEF(9, EvolutionParam.LEVEL, EvolutionTrigger.LEVEL_UP),
  LEVEL_ATK_LT_DEF(10, EvolutionParam.LEVEL, EvolutionTrigger.LEVEL_UP),
  LEVEL_PID_LOW(11, EvolutionParam.LEVEL, EvolutionTrigger.LEVEL_UP),
  LEVEL_PID_HIGH(12, EvolutionParam.LEVEL, EvolutionTrigger.LEVEL_UP),
  LEVEL_NINJASK(13, EvolutionParam.LEVEL, EvolutionTrigger.LEVEL_UP),
  LEVEL_SHEDINJA(14, EvolutionParam.LEVEL, EvolutionTrigger.LEVEL_UP),
  LEVEL_BEAUTY(15, EvolutionParam.BEAUTY, EvolutionTrigger.LEVEL_UP),
  USE_ITEM_MALE(16, EvolutionParam.ITEM, EvolutionTrigger.ITEM_USED),
  USE_ITEM_FEMALE(17, EvolutionParam.ITEM, EvolutionTrigger.ITEM_USED),
  LEVEL_WITH_HELD_ITEM_DAY(18, EvolutionParam.ITEM, EvolutionTrigger.LEVEL_UP),
  LEVEL_WITH_HELD_ITEM_NIGHT(19, EvolutionParam.ITEM, EvolutionTrigger.LEVEL_UP),
  LEVEL_KNOW_MOVE(20, EvolutionParam.MOVE, EvolutionTrigger.LEVEL_UP),
  LEVEL_SPECIES_IN_PARTY(21, EvolutionParam.SPECIES, EvolutionTrigger.LEVEL_UP),
  LEVEL_MALE(22, EvolutionParam.LEVEL, EvolutionTrigger.LEVEL_UP),
  LEVEL_FEMALE(23, EvolutionParam.LEVEL, EvolutionTrigger.LEVEL_UP),
  LEVEL_MAGNETIC_FIELD(24, EvolutionParam.NONE, EvolutionTrigger.LEVEL_UP),
  LEVEL_MOSS_ROCK(25, EvolutionParam.NONE, EvolutionTrigger.LEVEL_UP),
  LEVEL_ICE_ROCK(26, EvolutionParam.NONE, EvolutionTrigger.LEVEL_UP);

  /** The three methods that need the monster to be standing somewhere in particular. */
  val isLocationBound: Boolean
    get() = this == LEVEL_MAGNETIC_FIELD || this == LEVEL_MOSS_ROCK || this == LEVEL_ICE_ROCK

  companion object {
    fun byId(id: Int): EvolutionMethod? = entries.find { it.id == id }
  }
}
