package de.fiereu.openmmo.common

/** The five Super Contest categories, in the game's own order. */
enum class ContestType {
  COOL,
  BEAUTY,
  CUTE,
  SMART,
  TOUGH,
}

/** The four contest ranks, in the order they have to be won. The ordinal is a ribbon-bit offset. */
enum class ContestRank {
  NORMAL,
  GREAT,
  ULTRA,
  MASTER,
}

/**
 * The bit a won ribbon sets, in the same layout the game keeps it in: four ranks per type, types in
 * order, twenty bits of the sixty-four.
 */
fun superContestRibbonBit(type: ContestType, rank: ContestRank): Long =
    1L shl (type.ordinal * ContestRank.entries.size + rank.ordinal)

/**
 * Every bit a ribbon can stand on: four ranks of each of five types, twenty of the sixty-four. The
 * game leaves the other forty-four alone, so a mask arriving with one of them set is carrying
 * something that is not a ribbon.
 */
val ALL_SUPER_CONTEST_RIBBONS: Long =
    ContestType.entries.fold(0L) { mask, type ->
      ContestRank.entries.fold(mask) { bits, rank -> bits or superContestRibbonBit(type, rank) }
    }

/** How many of [type]'s four ribbons this mask holds. This is the contest rank gate. */
fun superContestRibbonCount(mask: Long, type: ContestType): Int =
    ContestRank.entries.count { (mask and superContestRibbonBit(type, it)) != 0L }

/**
 * The ceiling on a condition and on sheen alike. The game's own number, and the reason Poffins stop
 * helping: sheen rises with every one fed and caps the total a monster can ever be raised by.
 */
const val MAX_CONTEST_STAT = 255

/** A monster's five contest conditions. */
data class ContestConditions(
    val cool: Int = 0,
    val beauty: Int = 0,
    val cute: Int = 0,
    val smart: Int = 0,
    val tough: Int = 0,
) {
  operator fun get(type: ContestType): Int =
      when (type) {
        ContestType.COOL -> cool
        ContestType.BEAUTY -> beauty
        ContestType.CUTE -> cute
        ContestType.SMART -> smart
        ContestType.TOUGH -> tough
      }

  /** The five in wire order, which is [ContestType] order. */
  fun toList(): List<Int> = ContestType.entries.map { this[it] }

  companion object {
    val NONE = ContestConditions()

    /** Reads back what [toList] wrote; a short list fills the rest with zero. */
    fun ofList(values: List<Int>): ContestConditions =
        ContestConditions(
            cool = values.getOrElse(0) { 0 },
            beauty = values.getOrElse(1) { 0 },
            cute = values.getOrElse(2) { 0 },
            smart = values.getOrElse(3) { 0 },
            tough = values.getOrElse(4) { 0 },
        )
  }
}
