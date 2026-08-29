package de.fiereu.openmmo.common.enums

import java.util.*

enum class PokemonStat {
  HP,
  ATTACK,
  DEFENSE,
  SP_ATTACK,
  SP_DEFENSE,
  SPEED
}

open class PokemonStats(private val individualCap: Int, private val totalCap: Int) :
    EnumMap<PokemonStat, Byte>(PokemonStat.entries.associateWith { 0.toByte() }) {

  val total
    get() = hp + atk + def + spAtk + spDef + spd

  private fun checkedSet(stat: PokemonStat, value: Int) {
    if (value < 0) error("Value can't be negative")
    if (value > individualCap) error("Value exceeds individual capacity")
    if (total - (get(stat)!!.toInt() and 0xFF) + value > totalCap)
        error("Total value exceeds capacity")
    set(stat, value.toByte())
  }

  var hp
    get() = get(PokemonStat.HP)!!.toInt() and 0xFF
    set(value) = checkedSet(PokemonStat.HP, value)

  var atk
    get() = get(PokemonStat.ATTACK)!!.toInt() and 0xFF
    set(value) = checkedSet(PokemonStat.ATTACK, value)

  var def
    get() = get(PokemonStat.DEFENSE)!!.toInt() and 0xFF
    set(value) = checkedSet(PokemonStat.DEFENSE, value)

  var spAtk
    get() = get(PokemonStat.SP_ATTACK)!!.toInt() and 0xFF
    set(value) = checkedSet(PokemonStat.SP_ATTACK, value)

  var spDef
    get() = get(PokemonStat.SP_DEFENSE)!!.toInt() and 0xFF
    set(value) = checkedSet(PokemonStat.SP_DEFENSE, value)

  var spd
    get() = get(PokemonStat.SPEED)!!.toInt() and 0xFF
    set(value) = checkedSet(PokemonStat.SPEED, value)
}

class EVs : PokemonStats(252, 510)

const val MAX_IV = 31

class IVs : PokemonStats(MAX_IV, 186)

// Six 5-bit stats in one word.
private const val IV_BITS = 5

fun IVs.compress(): Int =
    (((hp and 31) shl (IV_BITS * 0)) or
        ((atk and 31) shl (IV_BITS * 1)) or
        ((def and 31) shl (IV_BITS * 2)) or
        ((spd and 31) shl (IV_BITS * 3)) or
        ((spAtk and 31) shl (IV_BITS * 4)) or
        ((spDef and 31) shl (IV_BITS * 5)))

fun decompressIVs(value: Int): IVs =
    IVs().apply {
      hp = (value shr (IV_BITS * 0)) and 31
      atk = (value shr (IV_BITS * 1)) and 31
      def = (value shr (IV_BITS * 2)) and 31
      spd = (value shr (IV_BITS * 3)) and 31
      spAtk = (value shr (IV_BITS * 4)) and 31
      spDef = (value shr (IV_BITS * 5)) and 31
    }
