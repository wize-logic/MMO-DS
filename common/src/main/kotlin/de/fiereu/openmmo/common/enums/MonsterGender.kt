package de.fiereu.openmmo.common.enums

/**
 * A monster's gender, in the game's own order. Distinct from [CharacterGender], which is the
 * player's and has no genderless case.
 */
enum class MonsterGender(val gameValue: Int) {
  MALE(0),
  FEMALE(1),
  GENDERLESS(2);

  companion object {
    fun byGameValue(value: Int): MonsterGender? = entries.find { it.gameValue == value }
  }
}
