package de.fiereu.openmmo.server.game.battle

import de.fiereu.openmmo.common.enums.Ability
import de.fiereu.openmmo.common.enums.MoveEffect
import de.fiereu.openmmo.common.enums.MoveFlag
import de.fiereu.openmmo.common.enums.PokemonType
import de.fiereu.openmmo.moves.MoveDef
import de.fiereu.openmmo.typechart.TypeChart

/**
 * What an ability does in terms this engine has, the same way [EffectTable] is what a move's effect
 * does in them.
 */
internal object AbilityTable {

  // ------------------------------------------------------------------ mould breaking

  /** Whether [attacker] ignores the target's ability for the move it is using. */
  private val MOULD_BREAKERS = setOf(Ability.MOLD_BREAKER, Ability.TURBOBLAZE, Ability.TERAVOLT)

  /** The defender's ability as this attacker sees it. */
  fun defending(attacker: Ability, defender: Ability): Ability =
      if (attacker in MOULD_BREAKERS) Ability.NONE else defender

  // ------------------------------------------------------------------ immunity

  /** The type each ability makes its holder immune to. */
  private val TYPE_IMMUNITY: Map<Ability, PokemonType> =
      mapOf(
          Ability.LEVITATE to PokemonType.GROUND,
          Ability.VOLT_ABSORB to PokemonType.ELECTRIC,
          Ability.MOTOR_DRIVE to PokemonType.ELECTRIC,
          Ability.LIGHTNING_ROD to PokemonType.ELECTRIC,
          Ability.WATER_ABSORB to PokemonType.WATER,
          Ability.STORM_DRAIN to PokemonType.WATER,
          // Dry Skin heals in the rain and burns in the sun, neither of which exists here; the
          // Water immunity is the half of it that does not need weather.
          Ability.DRY_SKIN to PokemonType.WATER,
          Ability.FLASH_FIRE to PokemonType.FIRE,
          Ability.SAP_SIPPER to PokemonType.GRASS,
      )

  /**
   * Whether the target's ability turns this move away entirely. The absorbing abilities heal or
   * raise a stat as well as absorbing; nothing here heals, and Motor Drive's and Storm Drain's and
   * Sap Sipper's stage is applied by the caller.
   */
  fun blocksMove(defender: Ability, move: MoveDef, effectiveness: Int): Boolean {
    if (move.power <= 0) return false
    if (TYPE_IMMUNITY[defender] == move.type) return true
    return defender == Ability.WONDER_GUARD && effectiveness <= TypeChart.NEUTRAL
  }

  /** The stage an absorbed hit gives its target instead of damage. */
  fun absorbStage(defender: Ability, move: MoveDef): StageEffect? =
      when {
        move.power <= 0 || TYPE_IMMUNITY[defender] != move.type -> null
        defender == Ability.MOTOR_DRIVE -> StageEffect(BattleStat.SPEED, 1, false)
        defender == Ability.LIGHTNING_ROD || defender == Ability.STORM_DRAIN ->
            StageEffect(BattleStat.SP_ATTACK, 1, false)
        defender == Ability.SAP_SIPPER -> StageEffect(BattleStat.ATTACK, 1, false)
        else -> null
      }

  /** Damp stops a user blowing itself up, and the move fails outright. */
  fun blocksOwnMove(defender: Ability, move: MoveDef): Boolean =
      defender == Ability.DAMP && move.effect == MoveEffect.EXPLOSION

  // ------------------------------------------------------------------ order

  /**
   * The priority an ability adds. Prankster pushes a status move ahead of the ordinary ones; Stall
   * puts its holder last within its priority band, which is spelled as a large negative because
   * this engine orders on priority and then speed and has no third key.
   */
  fun priorityBonus(ability: Ability, move: MoveDef?): Int =
      when {
        move == null -> 0
        ability == Ability.PRANKSTER && move.power <= 0 -> 1
        ability == Ability.STALL -> STALL_PRIORITY
        else -> 0
      }

  /** Far enough below any real move's priority to lose every tie, and not so far it wraps. */
  private const val STALL_PRIORITY = -8

  // ------------------------------------------------------------------ accuracy

  /** The move's accuracy after both sides' abilities, or null when it cannot miss. */
  fun accuracy(attacker: Ability, defender: Ability, move: MoveDef, accuracy: Int): Int? {
    if (attacker == Ability.NO_GUARD || defender == Ability.NO_GUARD) return null
    var value = accuracy
    if (attacker == Ability.COMPOUND_EYES) value = value * 13 / 10
    if (attacker == Ability.VICTORY_STAR) value = value * 11 / 10
    if (attacker == Ability.HUSTLE && MoveCategory.isPhysical(move.type)) value = value * 4 / 5
    // Wonder Skin drags a status move down to a coin flip. A move with power is untouched.
    if (defender == Ability.WONDER_SKIN && move.power <= 0 && value > 50) value = 50
    return value
  }

  // ------------------------------------------------------------------ the damage formula

  /**
   * The multiplier on the stat the attacker is swinging with, as (numerator, denominator).
   * [halfHealthOrLess] is Defeatist's condition, which reads both attack stats; the other two read
   * only the physical one.
   */
  fun attackMultiplier(
      ability: Ability,
      physical: Boolean,
      halfHealthOrLess: Boolean,
  ): Pair<Int, Int> =
      when {
        ability == Ability.DEFEATIST && halfHealthOrLess -> 1 to 2
        !physical -> 1 to 1
        ability == Ability.HUGE_POWER || ability == Ability.PURE_POWER -> 2 to 1
        ability == Ability.HUSTLE -> 3 to 2
        else -> 1 to 1
      }

  /** The pinch abilities: a third of its health left, and its own type hits half again as hard. */
  private val PINCH: Map<Ability, PokemonType> =
      mapOf(
          Ability.OVERGROW to PokemonType.GRASS,
          Ability.BLAZE to PokemonType.FIRE,
          Ability.TORRENT to PokemonType.WATER,
          Ability.SWARM to PokemonType.BUG,
      )

  private val RECOIL_EFFECTS =
      setOf(
          MoveEffect.RECOIL,
          MoveEffect.RECOIL_HALF,
          MoveEffect.RECOIL_IF_MISS,
          MoveEffect.RECOIL_BURN_HIT,
          MoveEffect.RECOIL_PARALYZE_HIT,
      )

  /** The base power multiplier the attacker's ability applies, as (numerator, denominator). */
  fun powerMultiplier(
      ability: Ability,
      move: MoveDef,
      inPinch: Boolean,
      movedLast: Boolean,
      sameGender: Boolean?,
  ): Pair<Int, Int> =
      when {
        PINCH[ability] == move.type && inPinch -> 3 to 2
        ability == Ability.TECHNICIAN && move.power in 1..60 -> 3 to 2
        // Sheer Force trades the rider for the power, so the caller drops the rider too.
        ability == Ability.SHEER_FORCE && move.secondaryEffectChance > 0 -> 13 to 10
        ability == Ability.RECKLESS && move.effect in RECOIL_EFFECTS -> 12 to 10
        ability == Ability.ANALYTIC && movedLast -> 13 to 10
        ability == Ability.RIVALRY && sameGender == true -> 5 to 4
        ability == Ability.RIVALRY && sameGender == false -> 3 to 4
        else -> 1 to 1
      }

  /** Sheer Force spends the rider, so a move that gets the power does not get the effect. */
  fun suppressesRider(attacker: Ability, defender: Ability, move: MoveDef): Boolean =
      (attacker == Ability.SHEER_FORCE && move.secondaryEffectChance > 0) ||
          defender == Ability.SHIELD_DUST

  /** Serene Grace doubles the chance of a rider landing. */
  fun riderChance(ability: Ability, chance: Int): Int =
      if (ability == Ability.SERENE_GRACE) chance * 2 else chance

  /** Adaptability makes the same-type bonus a doubling instead of half again. */
  fun stabMultiplier(ability: Ability): Pair<Int, Int> =
      if (ability == Ability.ADAPTABILITY) 2 to 1 else 3 to 2

  /**
   * The multiplier both sides' abilities put on an already type-scaled hit, as (numerator,
   * denominator): the attacker's lens against the defender's hide, plus the two that read the
   * move's own type and the one that reads the target's health.
   */
  fun effectivenessMultiplier(
      attacker: Ability,
      defender: Ability,
      move: MoveDef,
      effectiveness: Int,
      targetAtFullHp: Boolean,
  ): Pair<Int, Int> {
    var num = 1
    var den = 1
    if (attacker == Ability.TINTED_LENS && effectiveness < TypeChart.NEUTRAL) {
      num *= 2
    }
    if ((defender == Ability.FILTER || defender == Ability.SOLID_ROCK) &&
        effectiveness > TypeChart.NEUTRAL) {
      num *= 3
      den *= 4
    }
    if (defender == Ability.MULTISCALE && targetAtFullHp) {
      den *= 2
    }
    if (defender == Ability.THICK_FAT &&
        (move.type == PokemonType.FIRE || move.type == PokemonType.ICE)) {
      den *= 2
    }
    if (defender == Ability.HEATPROOF && move.type == PokemonType.FIRE) {
      den *= 2
    }
    if (defender == Ability.DRY_SKIN && move.type == PokemonType.FIRE) {
      num *= 5
      den *= 4
    }
    return num to den
  }

  // ------------------------------------------------------------------ critical hits

  /** Whether the target's ability refuses critical hits outright. */
  fun preventsCrits(defender: Ability): Boolean =
      defender == Ability.BATTLE_ARMOR || defender == Ability.SHELL_ARMOR

  /** Super Luck halves the odds denominator: one in sixteen becomes one in eight. */
  fun critDenominator(attacker: Ability, denominator: Int): Int =
      if (attacker == Ability.SUPER_LUCK) maxOf(1, denominator / 2) else denominator

  /** Sniper takes a critical hit from double to triple. */
  fun critMultiplier(attacker: Ability): Int = if (attacker == Ability.SNIPER) 3 else 2

  // ------------------------------------------------------------------ stat stages

  /** Whether this ability refuses a drop the foe is applying to [stat]. */
  fun refusesDrop(ability: Ability, stat: BattleStat): Boolean =
      when (ability) {
        Ability.CLEAR_BODY,
        Ability.WHITE_SMOKE -> true
        Ability.HYPER_CUTTER -> stat == BattleStat.ATTACK
        Ability.KEEN_EYE -> stat == BattleStat.ACCURACY
        Ability.BIG_PECKS -> stat == BattleStat.DEFENSE
        else -> false
      }

  /**
   * Contrary turns a stage change round; Simple doubles it. Both read the change, not the source.
   */
  fun scaleStage(ability: Ability, delta: Int): Int =
      when (ability) {
        Ability.CONTRARY -> -delta
        Ability.SIMPLE -> delta * 2
        else -> delta
      }

  /** Unaware looks at the other side as if it had no stages at all. */
  fun ignoresStages(ability: Ability): Boolean = ability == Ability.UNAWARE

  /** Defiant answers a drop the foe applied with two stages of Attack. */
  fun answersDrop(ability: Ability): StageEffect? =
      if (ability == Ability.DEFIANT) StageEffect(BattleStat.ATTACK, 2, true) else null

  /** Moxie takes a stage of Attack off whatever it just knocked out. */
  fun onKnockout(ability: Ability): StageEffect? =
      if (ability == Ability.MOXIE) StageEffect(BattleStat.ATTACK, 1, true) else null

  /**
   * What being hit does to the target's own stats: the three that answer a type, the one that
   * answers a physical hit, and Anger Point, which answers a critical one with everything it has.
   */
  fun onHit(defender: Ability, move: MoveDef, crit: Boolean): List<StageEffect> =
      when {
        defender == Ability.ANGER_POINT && crit ->
            listOf(StageEffect(BattleStat.ATTACK, StatStages.MAX * 2, false))
        defender == Ability.JUSTIFIED && move.type == PokemonType.DARK ->
            listOf(StageEffect(BattleStat.ATTACK, 1, false))
        defender == Ability.RATTLED &&
            move.type in setOf(PokemonType.BUG, PokemonType.GHOST, PokemonType.DARK) ->
            listOf(StageEffect(BattleStat.SPEED, 1, false))
        defender == Ability.WEAK_ARMOR && MoveCategory.isPhysical(move.type) ->
            listOf(
                StageEffect(BattleStat.DEFENSE, -1, false), StageEffect(BattleStat.SPEED, 1, false))
        else -> emptyList()
      }

  /** The fraction of the attacker's health a spiked hide takes back, or 0. */
  fun contactRecoilDenominator(defender: Ability, move: MoveDef): Int =
      if ((defender == Ability.ROUGH_SKIN || defender == Ability.IRON_BARBS) &&
          move.hasFlag(MoveFlag.MAKES_CONTACT))
          8
      else 0

  /** Sturdy leaves its holder on one hit point, but only from full health. */
  fun survivesAtOne(defender: Ability, fromFullHp: Boolean): Boolean =
      defender == Ability.STURDY && fromFullHp

  /**
   * Everything above is what this engine can express. These are the abilities it cannot, with the
   * system each is waiting on, so that a reader can tell "does nothing" from "not written yet".
   */
  val INERT: Set<Ability> =
      Ability.entries.toSet() -
          setOf(
              Ability.NONE,
              Ability.MOLD_BREAKER,
              Ability.TURBOBLAZE,
              Ability.TERAVOLT,
              Ability.WONDER_GUARD,
              Ability.DAMP,
              Ability.PRANKSTER,
              Ability.STALL,
              Ability.NO_GUARD,
              Ability.COMPOUND_EYES,
              Ability.VICTORY_STAR,
              Ability.HUSTLE,
              Ability.WONDER_SKIN,
              Ability.HUGE_POWER,
              Ability.PURE_POWER,
              Ability.DEFEATIST,
              Ability.TECHNICIAN,
              Ability.SHEER_FORCE,
              Ability.RECKLESS,
              Ability.ANALYTIC,
              Ability.RIVALRY,
              Ability.SHIELD_DUST,
              Ability.SERENE_GRACE,
              Ability.ADAPTABILITY,
              Ability.TINTED_LENS,
              Ability.FILTER,
              Ability.SOLID_ROCK,
              Ability.MULTISCALE,
              Ability.THICK_FAT,
              Ability.HEATPROOF,
              Ability.DRY_SKIN,
              Ability.BATTLE_ARMOR,
              Ability.SHELL_ARMOR,
              Ability.SUPER_LUCK,
              Ability.SNIPER,
              Ability.CLEAR_BODY,
              Ability.WHITE_SMOKE,
              Ability.HYPER_CUTTER,
              Ability.KEEN_EYE,
              Ability.BIG_PECKS,
              Ability.CONTRARY,
              Ability.SIMPLE,
              Ability.UNAWARE,
              Ability.DEFIANT,
              Ability.MOXIE,
              Ability.ANGER_POINT,
              Ability.JUSTIFIED,
              Ability.RATTLED,
              Ability.WEAK_ARMOR,
              Ability.ROUGH_SKIN,
              Ability.IRON_BARBS,
              Ability.STURDY,
              Ability.LEVITATE,
              Ability.VOLT_ABSORB,
              Ability.MOTOR_DRIVE,
              Ability.LIGHTNING_ROD,
              Ability.WATER_ABSORB,
              Ability.STORM_DRAIN,
              Ability.FLASH_FIRE,
              Ability.SAP_SIPPER,
              Ability.OVERGROW,
              Ability.BLAZE,
              Ability.TORRENT,
              Ability.SWARM,
          )
}
