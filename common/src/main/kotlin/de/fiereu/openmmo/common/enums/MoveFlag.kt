package de.fiereu.openmmo.common.enums

/**
 * The per move flags both decompilations carry. The first six are the same six in both, one for
 * one, across every move they share; the last two are presentation flags only the DS tree has, and
 * their bits are ours.
 */
enum class MoveFlag(val bit: Int) {
  MAKES_CONTACT(1 shl 0),
  PROTECT_AFFECTED(1 shl 1),
  MAGIC_COAT_AFFECTED(1 shl 2),
  SNATCH_AFFECTED(1 shl 3),
  MIRROR_MOVE_AFFECTED(1 shl 4),
  KINGS_ROCK_AFFECTED(1 shl 5),
  HIDES_HP_GAUGES(1 shl 6),
  HIDES_SHADOWS(1 shl 7),
}
