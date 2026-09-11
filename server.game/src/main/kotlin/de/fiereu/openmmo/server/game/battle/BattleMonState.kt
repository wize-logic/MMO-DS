package de.fiereu.openmmo.server.game.battle

import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.Ability
import de.fiereu.openmmo.net.game.packets.battle.BattleMonBlock
import de.fiereu.openmmo.net.game.packets.battle.BattleOpponentBlock
import de.fiereu.openmmo.pokemon.SpeciesDef
import java.util.EnumMap

/**
 * One monster's live state inside a battle. [source] is the snapshot the battle started from.
 * [currentHp] and the move pp are the live values written back when the battle ends.
 */
class BattleMonState(
    val entityId: Long,
    val species: SpeciesDef,
    val partyIndex: Int?,
    // Both move on when a reward lands, so a second reward in the same battle builds on the first.
    var source: Pokemon,
    var stats: ComputedStats,
    val gender: Byte = 0,
) {
  var currentHp: Int = source.hp.toInt().coerceIn(0, stats.hp)
  val moves: MutableList<PokemonMove> =
      source.moves.map { PokemonMove(it.id, it.pp) }.toMutableList()

  /**
   * The ability this monster actually has, which is what [AbilityTable] is asked about and what the
   * wire is told.
   */
  val ability: Ability
    get() =
        when {
          source.hasHiddenAbility && species.abilityHidden != Ability.NONE -> species.abilityHidden
          species.ability2 != Ability.NONE && (source.seed and 1) == 1 -> species.ability2
          else -> species.ability1
        }

  private val stages = EnumMap<BattleStat, Int>(BattleStat::class.java)

  val level: Int
    get() = source.level.toInt()

  val fainted: Boolean
    get() = currentHp <= 0

  fun stage(stat: BattleStat): Int = stages[stat] ?: 0

  /**
   * Forget every stat change, which is what leaving the field does. Nothing cleared these, so a
   * monster could raise itself to +6, switch out, and come back still at +6.
   */
  fun clearStages() {
    stages.clear()
  }

  /** Clamp to the stage limits and return the delta that was actually applied. */
  fun changeStage(stat: BattleStat, delta: Int): Int {
    val old = stage(stat)
    val new = (old + delta).coerceIn(StatStages.MIN, StatStages.MAX)
    stages[stat] = new
    return new - old
  }

  fun unstaged(stat: BattleStat): Int =
      when (stat) {
        BattleStat.ATTACK -> stats.atk
        BattleStat.DEFENSE -> stats.def
        BattleStat.SP_ATTACK -> stats.spAtk
        BattleStat.SP_DEFENSE -> stats.spDef
        BattleStat.SPEED -> stats.spd
        BattleStat.ACCURACY,
        BattleStat.EVASION -> error("$this has no base stat to stage")
      }

  fun effective(stat: BattleStat): Int = StatStages.scaleStat(unstaged(stat), stage(stat))

  /** Replace one base stat for the rest of the fight: Guard Split and Power Split. */
  fun setUnstaged(stat: BattleStat, value: Int) {
    stats =
        when (stat) {
          BattleStat.ATTACK -> stats.copy(atk = value)
          BattleStat.DEFENSE -> stats.copy(def = value)
          BattleStat.SP_ATTACK -> stats.copy(spAtk = value)
          BattleStat.SP_DEFENSE -> stats.copy(spDef = value)
          BattleStat.SPEED -> stats.copy(spd = value)
          BattleStat.ACCURACY,
          BattleStat.EVASION -> error("$stat has no base stat to split")
        }
  }

  fun toOpponentBlock(slot: Int): BattleOpponentBlock =
      BattleOpponentBlock(
          slot = slot,
          revealed = true,
          entityId = entityId,
          species = species.id.toShort(),
          level = source.level,
          gender = gender,
          maxHp = stats.hp.toShort(),
          currentHp = currentHp.toShort(),
      )

  fun toBlock(slot: Int, movesPresent: Boolean): BattleMonBlock =
      BattleMonBlock(
          slot = slot,
          entityId = entityId,
          species = species.id.toShort(),
          level = source.level,
          gender = gender,
          abilityId = ability.ordinal.toShort(),
          maxHp = stats.hp.toShort(),
          currentHp = currentHp.toShort(),
          movesPresent = movesPresent,
          moveIds = List(BattleMonBlock.MOVE_SLOTS) { moves.getOrNull(it)?.id ?: 0 },
      )
}
