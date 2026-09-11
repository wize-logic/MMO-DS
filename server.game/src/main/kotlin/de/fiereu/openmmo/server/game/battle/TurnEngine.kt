package de.fiereu.openmmo.server.game.battle

import de.fiereu.openmmo.common.MAX_FRIENDSHIP
import de.fiereu.openmmo.common.enums.Ability
import de.fiereu.openmmo.common.enums.MonsterGender
import de.fiereu.openmmo.common.enums.MoveEffect
import de.fiereu.openmmo.moves.MoveDef
import de.fiereu.openmmo.moves.MoveRegistry
import de.fiereu.openmmo.typechart.TypeChart
import javax.inject.Inject
import javax.inject.Singleton

private const val CRIT_DENOMINATOR = 16
private const val TACKLE_ID = 33

/** The gender byte a battle carries for a monster that has none: Rivalry's "neither". */
private val GENDERLESS = MonsterGender.GENDERLESS.gameValue.toByte()

sealed interface BattleEvent {
  data class MoveUsed(val attackerId: Long, val moveId: Short, val moveSlot: Int, val ppLeft: Int) :
      BattleEvent

  /** A move that reached no target of its own, so its message lands on the opponent. */
  sealed interface MoveWithoutTarget : BattleEvent {
    val attackerId: Long
    val moveId: Short
  }

  data class MoveMissed(override val attackerId: Long, override val moveId: Short) :
      MoveWithoutTarget

  data class MoveFailed(override val attackerId: Long, override val moveId: Short) :
      MoveWithoutTarget

  data class DamageDealt(
      val targetId: Long,
      val newHp: Int,
      val crit: Boolean,
      val effectiveness: Int,
  ) : BattleEvent

  data class StageChanged(
      val targetId: Long,
      val stat: BattleStat,
      val stage: Int,
      val effectiveValue: Int,
      val delta: Int,
      val failed: Boolean,
  ) : BattleEvent

  data class Fainted(val targetId: Long) : BattleEvent
}

private data class TurnAction(
    val attacker: BattleMonState,
    val defender: BattleMonState,
    val move: MoveDef?,
    /**
     * Whether this action runs after the other side has already acted. Only Analytic reads it, and
     * it is settled before the turn runs because this engine orders the whole turn up front.
     */
    val movedLast: Boolean = false,
)

/**
 * Resolves one wild battle turn with the Gen 3 core rules: priority and speed order, accuracy
 * stages, the damage formula with crit, stab, and type chart, and stat stage moves.
 */
@Singleton
class TurnEngine
@Inject
constructor(
    private val moves: MoveRegistry,
    private val typeChart: TypeChart,
) {

  fun resolveTurn(battle: BattleInstance, playerMoveId: Short): List<BattleEvent> {
    val events = mutableListOf<BattleEvent>()
    val player = battle.activeMon()
    val enemy = battle.opponentMon()

    val playerAction = TurnAction(player, enemy, moves.get(playerMoveId.toInt()))
    val enemyAction = TurnAction(enemy, player, pickEnemyMove(battle, enemy))

    for (action in order(battle, playerAction, enemyAction)) {
      if (action.attacker.fainted) continue
      execute(battle, action, events)
      if (player.fainted || enemy.fainted) break
    }
    return events
  }

  /** Both sides named a move. Same speed and priority rules as a wild turn. */
  fun resolvePvpTurn(
      battle: BattleInstance,
      playerMoveId: Short,
      foeMoveId: Short,
  ): List<BattleEvent> {
    val events = mutableListOf<BattleEvent>()
    val player = battle.activeMon()
    val enemy = battle.opponentMon()
    val playerAction = TurnAction(player, enemy, moves.get(playerMoveId.toInt()))
    val enemyAction = TurnAction(enemy, player, moves.get(foeMoveId.toInt()))
    for (action in order(battle, playerAction, enemyAction)) {
      if (action.attacker.fainted) continue
      execute(battle, action, events)
      if (player.fainted || enemy.fainted) break
    }
    return events
  }

  /** A voluntary switch spends the player's turn, so the enemy attacks the incoming monster. */
  fun resolveSwitchTurn(battle: BattleInstance): List<BattleEvent> {
    val events = mutableListOf<BattleEvent>()
    val enemy = battle.opponentMon()
    if (enemy.fainted) return events
    execute(
        battle,
        TurnAction(enemy, battle.activeMon(), pickEnemyMove(battle, enemy), movedLast = true),
        events)
    return events
  }

  /** The foe switched, so the challenger still gets their attack on the incoming monster. */
  fun resolvePlayerAttack(battle: BattleInstance, playerMoveId: Short): List<BattleEvent> {
    val events = mutableListOf<BattleEvent>()
    val player = battle.activeMon()
    if (player.fainted) return events
    execute(
        battle,
        TurnAction(player, battle.opponentMon(), moves.get(playerMoveId.toInt()), movedLast = true),
        events,
    )
    return events
  }

  /** The foe attacks after the challenger spent the turn switching or using an item. */
  fun resolveFoeAttack(battle: BattleInstance, foeMoveId: Short): List<BattleEvent> {
    val events = mutableListOf<BattleEvent>()
    val enemy = battle.opponentMon()
    if (enemy.fainted) return events
    execute(
        battle,
        TurnAction(enemy, battle.activeMon(), moves.get(foeMoveId.toInt()), movedLast = true),
        events)
    return events
  }

  /** The enemy ai picks a random usable move, falling back to Tackle with no pp left. */
  private fun pickEnemyMove(battle: BattleInstance, enemy: BattleMonState): MoveDef? {
    val usable = enemy.moves.filter { it.id.toInt() != 0 && it.pp > 0 }
    if (usable.isEmpty()) return moves.get(TACKLE_ID)
    return moves.get(usable[battle.rng.pick(usable.size)].id.toInt())
  }

  private fun order(
      battle: BattleInstance,
      a: TurnAction,
      b: TurnAction,
  ): List<TurnAction> {
    val pa = (a.move?.priority ?: 0) + AbilityTable.priorityBonus(a.attacker.ability, a.move)
    val pb = (b.move?.priority ?: 0) + AbilityTable.priorityBonus(b.attacker.ability, b.move)
    val sorted =
        when {
          pa != pb -> if (pa > pb) listOf(a, b) else listOf(b, a)
          else -> {
            val sa = a.attacker.effective(BattleStat.SPEED)
            val sb = b.attacker.effective(BattleStat.SPEED)
            when {
              sa != sb -> if (sa > sb) listOf(a, b) else listOf(b, a)
              battle.rng.coinFlip() -> listOf(a, b)
              else -> listOf(b, a)
            }
          }
        }
    return listOf(sorted[0], sorted[1].copy(movedLast = true))
  }

  private fun execute(
      battle: BattleInstance,
      action: TurnAction,
      events: MutableList<BattleEvent>
  ) {
    val attacker = action.attacker
    val move = action.move
    if (move == null) {
      events += BattleEvent.MoveFailed(attacker.entityId, 0)
      return
    }
    val moveId = move.id.toShort()
    val slot = attacker.moves.indexOfFirst { it.id == moveId }
    if (slot >= 0 && attacker.moves[slot].pp > 0) {
      attacker.moves[slot].pp = (attacker.moves[slot].pp - 1).toByte()
    }
    events +=
        BattleEvent.MoveUsed(
            attacker.entityId,
            moveId,
            slot.coerceAtLeast(0),
            attacker.moves.getOrNull(slot)?.pp?.toInt() ?: 0)

    if (!accuracyCheck(battle, action, move)) {
      events += BattleEvent.MoveMissed(attacker.entityId, moveId)
      return
    }

    val stages = EffectTable.primaryStages(move.effect)
    when {
      move.effect == MoveEffect.SYNCHRONOISE && !sharesType(attacker, action.defender) ->
          events += BattleEvent.MoveFailed(attacker.entityId, moveId)
      // Damp refuses the explosion rather than surviving it, so the move fails before it costs
      // the user its own health.
      AbilityTable.blocksOwnMove(defending(action), move) ->
          events += BattleEvent.MoveFailed(attacker.entityId, moveId)
      move.power > 0 -> damage(battle, action, move, events)
      stages.isNotEmpty() -> stages.forEach { applyStage(action, it, events) }
      move.effect == MoveEffect.GUARD_SPLIT ->
          split(action, BattleStat.DEFENSE, BattleStat.SP_DEFENSE)
      move.effect == MoveEffect.POWER_SPLIT ->
          split(action, BattleStat.ATTACK, BattleStat.SP_ATTACK)
      else -> events += BattleEvent.MoveFailed(attacker.entityId, moveId)
    }
  }

  /**
   * The defender's ability as the attacker of this action sees it: nothing, when the attacker
   * breaks moulds. Every read of a target's ability goes through here so that Mold Breaker,
   * Turboblaze and Teravolt cannot be forgotten at one of them.
   */
  private fun defending(action: TurnAction): Ability =
      AbilityTable.defending(action.attacker.ability, action.defender.ability)

  /** Synchronoise reaches only a target that shares a type with its user. */
  private fun sharesType(user: BattleMonState, target: BattleMonState): Boolean =
      target.species.hasType(user.species.type1) || target.species.hasType(user.species.type2)

  /**
   * Guard Split and Power Split: each of the two stats becomes the average of both sides', for the
   * rest of the fight, stages untouched.
   */
  private fun split(action: TurnAction, first: BattleStat, second: BattleStat) {
    for (stat in listOf(first, second)) {
      val average = (action.attacker.unstaged(stat) + action.defender.unstaged(stat)) / 2
      action.attacker.setUnstaged(stat, average)
      action.defender.setUnstaged(stat, average)
    }
  }

  private fun accuracyCheck(battle: BattleInstance, action: TurnAction, move: MoveDef): Boolean {
    val defender = defending(action)
    // No Guard on either side settles it before anything else is read, which is also what makes a
    // move with no accuracy at all (0, "never misses") and a No Guard move the same answer here.
    val base =
        AbilityTable.accuracy(action.attacker.ability, defender, move, move.accuracy) ?: return true
    if (base == 0) return true
    // Chip Away lands as if the target had no evasion stages, as it hits as if it had no others.
    // Unaware looks past them too, from the other side.
    val evasion =
        if (move.effect == MoveEffect.CHIP_AWAY ||
            AbilityTable.ignoresStages(action.attacker.ability))
            0
        else action.defender.stage(BattleStat.EVASION)
    val accuracyStage =
        if (AbilityTable.ignoresStages(defender)) 0 else action.attacker.stage(BattleStat.ACCURACY)
    val threshold = StatStages.scaleAccuracy(base, accuracyStage - evasion)
    return battle.rng.accuracyRoll() <= threshold
  }

  private fun damage(
      battle: BattleInstance,
      action: TurnAction,
      move: MoveDef,
      events: MutableList<BattleEvent>,
  ) {
    val attacker = action.attacker
    val defender = action.defender
    val ability = attacker.ability
    val foeAbility = defending(action)
    val eff = typeChart.effectiveness(move.type, defender.species.type1, defender.species.type2)
    if (eff == 0 || AbilityTable.blocksMove(foeAbility, move, eff)) {
      events += BattleEvent.MoveFailed(attacker.entityId, move.id.toShort())
      // Motor Drive, Lightning Rod, Storm Drain and Sap Sipper take a stage out of what they
      // absorbed. The other absorbers heal, which this engine cannot do, so they only refuse.
      AbilityTable.absorbStage(foeAbility, move)?.let { applyStage(action, it, events) }
      return
    }
    val physical = MoveCategory.isPhysical(move.type)
    val atkStat = if (physical) BattleStat.ATTACK else BattleStat.SP_ATTACK
    // Psyshock, Psystrike and Secret Sword are special moves that meet the target's Defense.
    val defStat =
        if (physical || move.effect == MoveEffect.PSYSHOCK) BattleStat.DEFENSE
        else BattleStat.SP_DEFENSE
    val crit =
        !AbilityTable.preventsCrits(foeAbility) &&
            (move.effect == MoveEffect.STORM_THROW ||
                battle.rng.critRoll(AbilityTable.critDenominator(ability, CRIT_DENOMINATOR)))
    // Foul Play swings with the target's own Attack. A crit ignores the attacker's negative stages
    // and the defender's positive stages; Chip Away ignores the defender's stages outright, and
    // Unaware ignores the other side's from either direction.
    val swinger = if (move.effect == MoveEffect.FOUL_PLAY) defender else attacker
    val swingStat = if (move.effect == MoveEffect.FOUL_PLAY) BattleStat.ATTACK else atkStat
    val rawAtk =
        if (AbilityTable.ignoresStages(foeAbility) || (crit && swinger.stage(swingStat) < 0))
            swinger.unstaged(swingStat)
        else swinger.effective(swingStat)
    val def =
        if (move.effect == MoveEffect.CHIP_AWAY ||
            AbilityTable.ignoresStages(ability) ||
            (crit && defender.stage(defStat) > 0))
            defender.unstaged(defStat)
        else defender.effective(defStat)
    // Huge Power and Hustle read the Attack stat, not the move's class, so the question here is
    // which stat is being swung, and Foul Play swings Attack with a move the gen 3 split calls
    // special. Defeatist reads both, so it does not care either way.
    val (atkNum, atkDen) =
        AbilityTable.attackMultiplier(
            swinger.ability,
            swingStat == BattleStat.ATTACK,
            swinger.currentHp * 2 <= swinger.stats.hp)
    val atk = (rawAtk * atkNum / atkDen).coerceAtLeast(1)

    var dmg: Int
    if (move.effect == MoveEffect.FINAL_GAMBIT) {
      dmg = attacker.currentHp
    } else {
      val sameGender =
          when {
            attacker.gender == GENDERLESS || defender.gender == GENDERLESS -> null
            else -> attacker.gender == defender.gender
          }
      val (powNum, powDen) =
          AbilityTable.powerMultiplier(
              ability,
              move,
              attacker.currentHp * 3 <= attacker.stats.hp,
              action.movedLast,
              sameGender)
      val basePower = (power(move, attacker, defender) * powNum / powDen).coerceAtLeast(1)
      dmg = (2 * attacker.level / 5 + 2) * basePower * atk / def / 50 + 2
      if (crit) dmg *= AbilityTable.critMultiplier(ability)
      if (attacker.species.hasType(move.type)) {
        val (stabNum, stabDen) = AbilityTable.stabMultiplier(ability)
        dmg = dmg * stabNum / stabDen
      }
      dmg = dmg * eff / TypeChart.NEUTRAL
      val (effNum, effDen) =
          AbilityTable.effectivenessMultiplier(
              ability, foeAbility, move, eff, defender.currentHp == defender.stats.hp)
      dmg = dmg * effNum / effDen
      dmg = dmg * battle.rng.damageRoll() / 100
      if (dmg < 1) dmg = 1
    }
    // Sturdy holds the blow at one hit point, and only a monster at full health has it to give.
    if (dmg >= defender.currentHp &&
        AbilityTable.survivesAtOne(foeAbility, defender.currentHp == defender.stats.hp)) {
      dmg = defender.currentHp - 1
    }

    defender.currentHp = (defender.currentHp - dmg).coerceAtLeast(0)
    events += BattleEvent.DamageDealt(defender.entityId, defender.currentHp, crit, eff)
    if (defender.fainted) {
      events += BattleEvent.Fainted(defender.entityId)
      // Moxie feeds on the knockout. Nothing on the wire says which ability did it, so it goes out
      // as the stage change it is.
      AbilityTable.onKnockout(ability)?.let { applyStage(action, it, events) }
    }
    if (move.effect == MoveEffect.FINAL_GAMBIT) {
      // The user goes down with the blow, whatever the blow did.
      attacker.currentHp = 0
      events += BattleEvent.DamageDealt(attacker.entityId, 0, false, TypeChart.NEUTRAL)
      events += BattleEvent.Fainted(attacker.entityId)
      return
    }
    if (defender.fainted) return
    if (move.effect == MoveEffect.CLEAR_SMOG) {
      // Every stage the target had goes back to zero, and each one is reported as the change it is.
      for (stat in BattleStat.entries) {
        val stage = defender.stage(stat)
        if (stage != 0) applyStage(action, StageEffect(stat, -stage, false), events)
      }
      return
    }
    // What being hit does to the target's own stats, and what a spiked hide takes back for it.
    AbilityTable.onHit(foeAbility, move, crit).forEach { applyStage(action, it, events) }
    val spikes = AbilityTable.contactRecoilDenominator(foeAbility, move)
    if (spikes > 0) {
      val bite = (attacker.stats.hp / spikes).coerceAtLeast(1)
      attacker.currentHp = (attacker.currentHp - bite).coerceAtLeast(0)
      events +=
          BattleEvent.DamageDealt(attacker.entityId, attacker.currentHp, false, TypeChart.NEUTRAL)
      if (attacker.fainted) {
        events += BattleEvent.Fainted(attacker.entityId)
        return
      }
    }
    val riders = EffectTable.secondaryStages(move.effect)
    val chance = AbilityTable.riderChance(ability, move.secondaryEffectChance)
    if (riders.isNotEmpty() &&
        move.secondaryEffectChance > 0 &&
        !AbilityTable.suppressesRider(ability, foeAbility, move) &&
        battle.rng.accuracyRoll() <= chance) {
      riders.forEach { applyStage(action, it, events) }
    }
  }

  /** A move's power where Black made it depend on the field. */
  private fun power(move: MoveDef, attacker: BattleMonState, defender: BattleMonState): Int =
      when (move.effect) {
        MoveEffect.STORED_POWER ->
            20 + 20 * BattleStat.entries.sumOf { maxOf(0, attacker.stage(it)) }
        MoveEffect.ELECTRO_BALL -> {
          val ratio =
              attacker.effective(BattleStat.SPEED) / maxOf(1, defender.effective(BattleStat.SPEED))
          when {
            ratio >= 4 -> 150
            ratio >= 3 -> 120
            ratio >= 2 -> 80
            ratio >= 1 -> 60
            else -> 40
          }
        }
        // No monster holds an item on this server (the record has no field for one), so the
        // doubled power is the only power Acrobatics has.
        MoveEffect.ACROBATICS -> move.power * 2
        // Both of these are the friendship on the record and nothing else, which is why they sat at
        // the table's power of 1 until it had a column. A monster that has never been walked is
        // near the floor and one at the evolution threshold is near the 102 ceiling.
        MoveEffect.RETURN -> friendshipPower(attacker.source.friendship)
        MoveEffect.FRUSTRATION -> friendshipPower(MAX_FRIENDSHIP - attacker.source.friendship)
        else -> move.power
      }

  /**
   * Return and Frustration, whose power is `10 * friendship / 25` on the value the move reads. The
   * floor is the table's own power for them, because a move with no power at all does no damage.
   */
  private fun friendshipPower(value: Int): Int =
      (10 * value.coerceIn(0, MAX_FRIENDSHIP) / 25).coerceAtLeast(1)

  /** One stage change, through whatever the target's ability makes of it. */
  private fun applyStage(
      action: TurnAction,
      effect: StageEffect,
      events: MutableList<BattleEvent>,
  ) {
    val target = if (effect.onSelf) action.attacker else action.defender
    val ability = target.ability
    val fromTheFoe = !effect.onSelf && effect.delta < 0
    if (fromTheFoe &&
        AbilityTable.defending(action.attacker.ability, ability).let {
          AbilityTable.refusesDrop(it, effect.stat)
        }) {
      events +=
          BattleEvent.StageChanged(
              target.entityId,
              effect.stat,
              target.stage(effect.stat),
              stageValue(target, effect.stat),
              effect.delta,
              true)
      return
    }
    val delta = AbilityTable.scaleStage(ability, effect.delta)
    val applied = target.changeStage(effect.stat, delta)
    events +=
        BattleEvent.StageChanged(
            target.entityId,
            effect.stat,
            target.stage(effect.stat),
            stageValue(target, effect.stat),
            delta,
            applied == 0)
    // Defiant reads what the foe tried to do, not what Contrary made of it, so it answers the same
    // drop a Contrary monster would have turned into a rise, and no monster has both.
    if (fromTheFoe && delta < 0) {
      AbilityTable.answersDrop(ability)?.let { answer ->
        val given = target.changeStage(answer.stat, answer.delta)
        events +=
            BattleEvent.StageChanged(
                target.entityId,
                answer.stat,
                target.stage(answer.stat),
                stageValue(target, answer.stat),
                answer.delta,
                given == 0)
      }
    }
  }

  /** Accuracy and evasion have no stat behind them to report. */
  private fun stageValue(target: BattleMonState, stat: BattleStat): Int =
      when (stat) {
        BattleStat.ACCURACY,
        BattleStat.EVASION -> 0
        else -> target.effective(stat)
      }
}
