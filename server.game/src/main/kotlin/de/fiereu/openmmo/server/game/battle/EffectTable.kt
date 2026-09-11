package de.fiereu.openmmo.server.game.battle

import de.fiereu.openmmo.common.enums.MoveEffect

/** One stage change a move applies: which stat, by how much, and to whom. */
internal data class StageEffect(val stat: BattleStat, val delta: Int, val onSelf: Boolean)

/**
 * What a move's effect does in terms this engine has. The engine's core is Gen 3's: damage, stat
 * stages, accuracy.
 */
internal object EffectTable {
  private val UP =
      mapOf(
          MoveEffect.ATTACK_UP to BattleStat.ATTACK,
          MoveEffect.DEFENSE_UP to BattleStat.DEFENSE,
          MoveEffect.SPEED_UP to BattleStat.SPEED,
          MoveEffect.SPECIAL_ATTACK_UP to BattleStat.SP_ATTACK,
          MoveEffect.SPECIAL_DEFENSE_UP to BattleStat.SP_DEFENSE,
          MoveEffect.ACCURACY_UP to BattleStat.ACCURACY,
          MoveEffect.EVASION_UP to BattleStat.EVASION,
      )
  private val UP_2 =
      mapOf(
          MoveEffect.ATTACK_UP_2 to BattleStat.ATTACK,
          MoveEffect.DEFENSE_UP_2 to BattleStat.DEFENSE,
          MoveEffect.SPEED_UP_2 to BattleStat.SPEED,
          MoveEffect.SPECIAL_ATTACK_UP_2 to BattleStat.SP_ATTACK,
          MoveEffect.SPECIAL_DEFENSE_UP_2 to BattleStat.SP_DEFENSE,
          MoveEffect.ACCURACY_UP_2 to BattleStat.ACCURACY,
          MoveEffect.EVASION_UP_2 to BattleStat.EVASION,
      )
  private val DOWN =
      mapOf(
          MoveEffect.ATTACK_DOWN to BattleStat.ATTACK,
          MoveEffect.DEFENSE_DOWN to BattleStat.DEFENSE,
          MoveEffect.SPEED_DOWN to BattleStat.SPEED,
          MoveEffect.SPECIAL_ATTACK_DOWN to BattleStat.SP_ATTACK,
          MoveEffect.SPECIAL_DEFENSE_DOWN to BattleStat.SP_DEFENSE,
          MoveEffect.ACCURACY_DOWN to BattleStat.ACCURACY,
          MoveEffect.EVASION_DOWN to BattleStat.EVASION,
      )
  private val DOWN_2 =
      mapOf(
          MoveEffect.ATTACK_DOWN_2 to BattleStat.ATTACK,
          MoveEffect.DEFENSE_DOWN_2 to BattleStat.DEFENSE,
          MoveEffect.SPEED_DOWN_2 to BattleStat.SPEED,
          MoveEffect.SPECIAL_ATTACK_DOWN_2 to BattleStat.SP_ATTACK,
          MoveEffect.SPECIAL_DEFENSE_DOWN_2 to BattleStat.SP_DEFENSE,
          MoveEffect.ACCURACY_DOWN_2 to BattleStat.ACCURACY,
          MoveEffect.EVASION_DOWN_2 to BattleStat.EVASION,
      )
  private val DOWN_HIT =
      mapOf(
          MoveEffect.ATTACK_DOWN_HIT to BattleStat.ATTACK,
          MoveEffect.DEFENSE_DOWN_HIT to BattleStat.DEFENSE,
          MoveEffect.SPEED_DOWN_HIT to BattleStat.SPEED,
          MoveEffect.SPECIAL_ATTACK_DOWN_HIT to BattleStat.SP_ATTACK,
          MoveEffect.SPECIAL_DEFENSE_DOWN_HIT to BattleStat.SP_DEFENSE,
          MoveEffect.ACCURACY_DOWN_HIT to BattleStat.ACCURACY,
          MoveEffect.EVASION_DOWN_HIT to BattleStat.EVASION,
      )
  // Gen 4 has only these two single-stat "up on hit" effects; the others in that family raise or
  // drop several stats at once and keep their own names.
  private val UP_HIT =
      mapOf(
          MoveEffect.ATTACK_UP_HIT to BattleStat.ATTACK,
          MoveEffect.DEFENSE_UP_HIT to BattleStat.DEFENSE,
      )

  private fun self(vararg pairs: Pair<BattleStat, Int>) =
      pairs.map { StageEffect(it.first, it.second, true) }

  private fun foe(vararg pairs: Pair<BattleStat, Int>) =
      pairs.map { StageEffect(it.first, it.second, false) }

  /** Black's stat-stage moves: the stages one use applies, in the order the game lists them. */
  private val GEN5_PRIMARY: Map<MoveEffect, List<StageEffect>> =
      mapOf(
          MoveEffect.HONE_CLAWS to self(BattleStat.ATTACK to 1, BattleStat.ACCURACY to 1),
          // Autotomize also halves the user's weight, which nothing here weighs.
          MoveEffect.AUTOTOMIZE to self(BattleStat.SPEED to 2),
          MoveEffect.QUIVER_DANCE to
              self(BattleStat.SP_ATTACK to 1, BattleStat.SP_DEFENSE to 1, BattleStat.SPEED to 1),
          MoveEffect.SHELL_SMASH to
              self(
                  BattleStat.DEFENSE to -1,
                  BattleStat.SP_DEFENSE to -1,
                  BattleStat.ATTACK to 2,
                  BattleStat.SP_ATTACK to 2,
                  BattleStat.SPEED to 2),
          MoveEffect.SHIFT_GEAR to self(BattleStat.SPEED to 2, BattleStat.ATTACK to 1),
          MoveEffect.COIL to
              self(BattleStat.ATTACK to 1, BattleStat.DEFENSE to 1, BattleStat.ACCURACY to 1),
          MoveEffect.WORK_UP to self(BattleStat.ATTACK to 1, BattleStat.SP_ATTACK to 1),
          MoveEffect.COTTON_GUARD to self(BattleStat.DEFENSE to 3),
      )

  /** Black's riders on a hit, rolled against the move's own chance (100 for all four). */
  private val GEN5_SECONDARY: Map<MoveEffect, List<StageEffect>> =
      mapOf(
          MoveEffect.FLAME_CHARGE to self(BattleStat.SPEED to 1),
          MoveEffect.ACID_SPRAY to foe(BattleStat.SP_DEFENSE to -2),
          MoveEffect.GLACIATE to foe(BattleStat.SPEED to -1),
          MoveEffect.V_CREATE to
              self(BattleStat.DEFENSE to -1, BattleStat.SP_DEFENSE to -1, BattleStat.SPEED to -1),
      )

  /** A status move's stages, or empty when the effect is not one this engine can apply. */
  fun primaryStages(effect: MoveEffect): List<StageEffect> {
    UP[effect]?.let {
      return self(it to 1)
    }
    UP_2[effect]?.let {
      return self(it to 2)
    }
    DOWN[effect]?.let {
      return foe(it to -1)
    }
    DOWN_2[effect]?.let {
      return foe(it to -2)
    }
    return GEN5_PRIMARY[effect] ?: emptyList()
  }

  /** The stages a hit may add, or empty. */
  fun secondaryStages(effect: MoveEffect): List<StageEffect> {
    DOWN_HIT[effect]?.let {
      return foe(it to -1)
    }
    UP_HIT[effect]?.let {
      return self(it to 1)
    }
    return GEN5_SECONDARY[effect] ?: emptyList()
  }
}
