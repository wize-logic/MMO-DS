package de.fiereu.openmmo.server.game.battle

import de.fiereu.openmmo.common.DEFAULT_FRIENDSHIP
import de.fiereu.openmmo.common.MAX_FRIENDSHIP
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.Ability
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.moves.MoveRegistry
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import de.fiereu.openmmo.typechart.TypeChart
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.booleans.shouldBeTrue
import io.kotest.matchers.collections.shouldNotBeEmpty
import io.kotest.matchers.ints.shouldBeGreaterThan
import io.kotest.matchers.shouldBe
import io.kotest.matchers.shouldNotBe
import java.time.LocalDateTime

private const val PLAYER_ID = 0x1C000L
private const val WILD_ID = 0x3C000L

private const val TACKLE: Short = 33
private const val GROWL: Short = 45
private const val SWIFT: Short = 129
private const val QUICK_ATTACK: Short = 98
private const val SPLASH: Short = 150
private const val PECK: Short = 64
private const val WATER_GUN: Short = 55
private const val EARTHQUAKE: Short = 89
// Black's moves, on the shared numbering (codegen/gen5/moves.json).
private const val HONE_CLAWS: Short = 468
private const val GUARD_SPLIT: Short = 470
private const val PSYSHOCK: Short = 473
private const val STORM_THROW: Short = 480
private const val SYNCHRONOISE: Short = 485
private const val ELECTRO_BALL: Short = 486
private const val FLAME_CHARGE: Short = 488
private const val ACID_SPRAY: Short = 491
private const val CLEAR_SMOG: Short = 499
private const val STORED_POWER: Short = 500
private const val SHELL_SMASH: Short = 504
private const val FINAL_GAMBIT: Short = 515
private const val EXTRASENSORY: Short = 326
private const val RETURN: Short = 216
private const val FRUSTRATION: Short = 218

private val speciesRegistry = SpeciesRegistry()

private fun pokemon(
    dexId: Int,
    level: Int,
    moves: List<Short>,
    id: Long,
    friendship: Int = DEFAULT_FRIENDSHIP,
    seed: Int = 0,
    hiddenAbility: Boolean = false,
): Pokemon {
  val moveRegistry = MoveRegistry()
  val padded = List(4) { i -> moves.getOrNull(i) ?: 0 }
  return Pokemon(
      id = id,
      ownerId = 0,
      container = PokemonContainer.PARTY,
      containerSlot = 0,
      dexId = dexId,
      seed = seed,
      ot = "",
      nickname = "",
      level = level.toByte(),
      hp = Short.MAX_VALUE,
      xp = 0,
      eVs = EVs(),
      iVs = IVs(),
      moves = padded.map { PokemonMove(it, (moveRegistry.get(it.toInt())?.pp ?: 0).toByte()) },
      isShiny = false,
      hasHiddenAbility = hiddenAbility,
      isAlpha = false,
      isSecret = false,
      isFatefulEncounter = false,
      isRaidEncounter = false,
      caughtAt = LocalDateTime.now(),
      friendship = friendship,
  )
}

private fun state(
    dexId: Int,
    level: Int,
    moves: List<Short>,
    id: Long,
    friendship: Int = DEFAULT_FRIENDSHIP,
    // The personality value's low bit picks the second ability slot, which is how a monster gets
    // one that is not its species' first; the Dream World bit picks the third.
    seed: Int = 0,
    hiddenAbility: Boolean = false,
): BattleMonState {
  val def = speciesRegistry.get(dexId)!!
  val mon = pokemon(dexId, level, moves, id, friendship, seed, hiddenAbility)
  return BattleMonState(
      id, def, if (id == PLAYER_ID) 0 else null, mon, StatCalculator.computeAll(def, mon))
}

private fun battle(
    player: BattleMonState,
    wild: BattleMonState,
    seed: Long,
): BattleInstance =
    BattleInstance(1L, 100L, FakeSession(100L), listOf(player), listOf(wild), BattleRng(seed))

private val engine = TurnEngine(MoveRegistry(), TypeChart())

class TurnEngineTest :
    FunSpec({
      test("the same seed replays the same turn") {
        fun run(): List<BattleEvent> {
          val player = state(1, 5, listOf(TACKLE, GROWL), PLAYER_ID)
          val wild = state(19, 3, listOf(TACKLE), WILD_ID)
          return engine.resolveTurn(battle(player, wild, seed = 7), TACKLE)
        }
        run() shouldBe run()
      }

      test("swift always hits and pp decrements to the event value") {
        val player = state(1, 20, listOf(SWIFT), PLAYER_ID)
        val wild = state(19, 3, listOf(TACKLE), WILD_ID)
        val events = engine.resolveTurn(battle(player, wild, seed = 1), SWIFT)

        val used =
            events.filterIsInstance<BattleEvent.MoveUsed>().first { it.attackerId == PLAYER_ID }
        used.moveSlot shouldBe 0
        used.ppLeft shouldBe 19
        player.moves[0].pp shouldBe 19.toByte()
        events.filterIsInstance<BattleEvent.DamageDealt>().first { it.targetId == WILD_ID }
      }

      test("damage stays inside the gen 3 bounds for the setup") {
        // Bulbasaur level 20 Swift versus Snorlax level 20. Swift is Normal, so the gen 3 type
        // split makes it physical, and the target is bulky enough that no roll can faint it and
        // clamp the damage.
        repeat(30) { i ->
          val player = state(1, 20, listOf(SWIFT), PLAYER_ID)
          val wild = state(143, 20, listOf(TACKLE), WILD_ID)
          val before = wild.currentHp
          val events = engine.resolveTurn(battle(player, wild, seed = i.toLong()), SWIFT)
          val hit =
              events.filterIsInstance<BattleEvent.DamageDealt>().first { it.targetId == WILD_ID }
          val dealt = before - hit.newHp
          val base = (2 * 20 / 5 + 2) * 60 * player.stats.atk / wild.stats.def / 50 + 2
          val bound = if (hit.crit) base * 2 else base
          dealt shouldBeGreaterThan bound * 85 / 100 - 1
          (dealt <= bound).shouldBeTrue()
        }
      }

      test("a critical hit deals more than any normal roll") {
        var critMin = Int.MAX_VALUE
        var normalMax = 0
        for (seed in 0L until 200L) {
          val player = state(1, 50, listOf(SWIFT), PLAYER_ID)
          val wild = state(19, 30, listOf(TACKLE), WILD_ID)
          val before = wild.currentHp
          val events = engine.resolveTurn(battle(player, wild, seed), SWIFT)
          val hit =
              events.filterIsInstance<BattleEvent.DamageDealt>().firstOrNull {
                it.targetId == WILD_ID
              } ?: continue
          val dealt = before - hit.newHp
          if (hit.crit) critMin = minOf(critMin, dealt) else normalMax = maxOf(normalMax, dealt)
        }
        critMin shouldNotBe Int.MAX_VALUE
        normalMax shouldBeGreaterThan 0
        critMin shouldBeGreaterThan normalMax
      }

      test("STAB outdamages an equal power move without it") {
        // Rattata uses Tackle (Normal, 35, STAB) or Peck (Flying, 35, no STAB) into Snorlax.
        fun maxDealt(moveId: Short): Int {
          var best = 0
          for (seed in 0L until 100L) {
            val player = state(19, 30, listOf(moveId), PLAYER_ID)
            val wild = state(143, 30, listOf(SPLASH), WILD_ID)
            val before = wild.currentHp
            val events = engine.resolveTurn(battle(player, wild, seed), moveId)
            val hit =
                events.filterIsInstance<BattleEvent.DamageDealt>().firstOrNull {
                  it.targetId == WILD_ID
                } ?: continue
            best = maxOf(best, before - hit.newHp)
          }
          return best
        }
        maxDealt(TACKLE) shouldBeGreaterThan maxDealt(PECK)
      }

      test("an immune target takes no damage and the move fails") {
        // Tackle into Gastly (Ghost/Poison) is a Normal immunity.
        val player = state(1, 20, listOf(TACKLE), PLAYER_ID)
        val wild = state(92, 5, listOf(SPLASH), WILD_ID)
        val before = wild.currentHp
        val events = engine.resolveTurn(battle(player, wild, seed = 3), TACKLE)

        events.filterIsInstance<BattleEvent.MoveFailed>().first { it.attackerId == PLAYER_ID }
        events
            .filterIsInstance<BattleEvent.DamageDealt>()
            .none { it.targetId == WILD_ID }
            .shouldBeTrue()
        wild.currentHp shouldBe before
      }

      test("growl drops the enemy attack stage") {
        val player = state(1, 5, listOf(GROWL), PLAYER_ID)
        val wild = state(19, 3, listOf(TACKLE), WILD_ID)
        val events = engine.resolveTurn(battle(player, wild, seed = 5), GROWL)

        val change =
            events.filterIsInstance<BattleEvent.StageChanged>().first { it.targetId == WILD_ID }
        change.stat shouldBe BattleStat.ATTACK
        change.stage shouldBe -1
        change.failed shouldBe false
        wild.stage(BattleStat.ATTACK) shouldBe -1
      }

      test("a stage already at the limit reports a failed change") {
        val player = state(1, 5, listOf(GROWL), PLAYER_ID)
        val wild = state(19, 3, listOf(TACKLE), WILD_ID)
        wild.changeStage(BattleStat.ATTACK, -6)
        val events = engine.resolveTurn(battle(player, wild, seed = 5), GROWL)

        val change =
            events.filterIsInstance<BattleEvent.StageChanged>().first { it.targetId == WILD_ID }
        change.failed shouldBe true
        wild.stage(BattleStat.ATTACK) shouldBe -6
      }

      test("priority beats speed") {
        // Level 5 Bulbasaur is far slower than a level 30 Rattata, Quick Attack still goes first.
        val player = state(1, 5, listOf(QUICK_ATTACK), PLAYER_ID)
        val wild = state(19, 30, listOf(TACKLE), WILD_ID)
        val events = engine.resolveTurn(battle(player, wild, seed = 11), QUICK_ATTACK)

        events.filterIsInstance<BattleEvent.MoveUsed>().first().attackerId shouldBe PLAYER_ID
      }

      test("the faster monster acts first without priority") {
        val player = state(1, 5, listOf(TACKLE), PLAYER_ID)
        val wild = state(19, 30, listOf(TACKLE), WILD_ID)
        val events = engine.resolveTurn(battle(player, wild, seed = 11), TACKLE)

        events.filterIsInstance<BattleEvent.MoveUsed>().first().attackerId shouldBe WILD_ID
      }

      test("a speed tie falls to the rng and both orders appear") {
        val leaders = mutableSetOf<Long>()
        for (seed in 0L until 40L) {
          val player = state(19, 10, listOf(TACKLE), PLAYER_ID)
          val wild = state(19, 10, listOf(TACKLE), WILD_ID)
          val events = engine.resolveTurn(battle(player, wild, seed), TACKLE)
          leaders += events.filterIsInstance<BattleEvent.MoveUsed>().first().attackerId
        }
        leaders shouldBe setOf(PLAYER_ID, WILD_ID)
      }

      test("a faint ends the turn before the loser acts") {
        val player = state(1, 50, listOf(SWIFT), PLAYER_ID)
        val wild = state(19, 2, listOf(TACKLE), WILD_ID)
        wild.currentHp = 1
        val events = engine.resolveTurn(battle(player, wild, seed = 2), SWIFT)

        events.filterIsInstance<BattleEvent.Fainted>().first().targetId shouldBe WILD_ID
        events
            .filterIsInstance<BattleEvent.MoveUsed>()
            .none { it.attackerId == WILD_ID }
            .shouldBeTrue()
      }

      test("an effect outside the core fails gracefully") {
        val player = state(1, 5, listOf(SPLASH), PLAYER_ID)
        val wild = state(19, 3, listOf(TACKLE), WILD_ID)
        val before = wild.currentHp
        val events = engine.resolveTurn(battle(player, wild, seed = 9), SPLASH)

        events.filterIsInstance<BattleEvent.MoveFailed>().shouldNotBeEmpty()
        wild.currentHp shouldBe before
        wild.stage(BattleStat.ATTACK) shouldBe 0
      }

      test("hone claws raises attack and accuracy in one use") {
        val player = state(1, 5, listOf(HONE_CLAWS), PLAYER_ID)
        val wild = state(19, 3, listOf(SPLASH), WILD_ID)
        val events = engine.resolveTurn(battle(player, wild, seed = 5), HONE_CLAWS)

        val mine =
            events.filterIsInstance<BattleEvent.StageChanged>().filter { it.targetId == PLAYER_ID }
        mine.map { it.stat to it.stage } shouldBe
            listOf(BattleStat.ATTACK to 1, BattleStat.ACCURACY to 1)
        player.stage(BattleStat.ATTACK) shouldBe 1
        player.stage(BattleStat.ACCURACY) shouldBe 1
      }

      test("shell smash trades two defences for three attacks") {
        val player = state(1, 5, listOf(SHELL_SMASH), PLAYER_ID)
        val wild = state(19, 3, listOf(SPLASH), WILD_ID)
        engine.resolveTurn(battle(player, wild, seed = 5), SHELL_SMASH)

        listOf(
                BattleStat.DEFENSE,
                BattleStat.SP_DEFENSE,
                BattleStat.ATTACK,
                BattleStat.SP_ATTACK,
                BattleStat.SPEED)
            .map { player.stage(it) } shouldBe listOf(-1, -1, 2, 2, 2)
      }

      test("flame charge's rider raises the user's own speed after the hit") {
        val player = state(1, 20, listOf(FLAME_CHARGE), PLAYER_ID)
        val wild = state(143, 20, listOf(SPLASH), WILD_ID)
        val events = engine.resolveTurn(battle(player, wild, seed = 2), FLAME_CHARGE)

        val hit = events.indexOfFirst { it is BattleEvent.DamageDealt && it.targetId == WILD_ID }
        hit shouldBeGreaterThan 0
        val rider = events[hit + 1] as BattleEvent.StageChanged
        rider.targetId shouldBe PLAYER_ID
        rider.stat shouldBe BattleStat.SPEED
        rider.delta shouldBe 1
      }

      test("acid spray drops the target's special defence by two") {
        val player = state(1, 20, listOf(ACID_SPRAY), PLAYER_ID)
        val wild = state(143, 20, listOf(SPLASH), WILD_ID)
        engine.resolveTurn(battle(player, wild, seed = 2), ACID_SPRAY)
        wild.stage(BattleStat.SP_DEFENSE) shouldBe -2
      }

      test("clear smog puts every stage of the target back to zero") {
        val player = state(1, 20, listOf(CLEAR_SMOG), PLAYER_ID)
        val wild = state(143, 20, listOf(SPLASH), WILD_ID)
        wild.changeStage(BattleStat.ATTACK, 3)
        wild.changeStage(BattleStat.SPEED, -2)
        val events = engine.resolveTurn(battle(player, wild, seed = 2), CLEAR_SMOG)

        wild.stage(BattleStat.ATTACK) shouldBe 0
        wild.stage(BattleStat.SPEED) shouldBe 0
        events
            .filterIsInstance<BattleEvent.StageChanged>()
            .filter { it.targetId == WILD_ID }
            .map { it.stat to it.delta } shouldBe
            listOf(BattleStat.ATTACK to -3, BattleStat.SPEED to 2)
      }

      test("stored power grows with the user's positive stages") {
        fun maxDealt(boosted: Boolean): Int {
          var best = 0
          for (seed in 0L until 60L) {
            val player = state(1, 30, listOf(STORED_POWER), PLAYER_ID)
            if (boosted) {
              player.changeStage(BattleStat.SP_ATTACK, 2)
              player.changeStage(BattleStat.SPEED, 6)
              player.changeStage(BattleStat.DEFENSE, -2)
            }
            val wild = state(143, 30, listOf(SPLASH), WILD_ID)
            val before = wild.currentHp
            val events = engine.resolveTurn(battle(player, wild, seed), STORED_POWER)
            val hit =
                events.filterIsInstance<BattleEvent.DamageDealt>().firstOrNull {
                  it.targetId == WILD_ID
                } ?: continue
            best = maxOf(best, before - hit.newHp)
          }
          return best
        }
        // Power 180 against 20, on top of the +2 special attack: far more than the stage alone
        // buys.
        maxDealt(true) shouldBeGreaterThan maxDealt(false) * 4
      }

      test("electro ball hits harder the faster the user is than the target") {
        fun maxDealt(userLevel: Int, targetLevel: Int): Int {
          var best = 0
          for (seed in 0L until 60L) {
            val player = state(19, userLevel, listOf(ELECTRO_BALL), PLAYER_ID)
            val wild = state(143, targetLevel, listOf(SPLASH), WILD_ID)
            val before = wild.currentHp
            val events = engine.resolveTurn(battle(player, wild, seed), ELECTRO_BALL)
            val hit =
                events.filterIsInstance<BattleEvent.DamageDealt>().firstOrNull {
                  it.targetId == WILD_ID
                } ?: continue
            best = maxOf(best, before - hit.newHp)
          }
          return best
        }
        // A level 50 Rattata outspeeds a level 50 Snorlax more than twice over; a level 10 one does
        // not outspeed it at all.
        maxDealt(50, 50) shouldBeGreaterThan maxDealt(10, 50)
      }

      test("return and frustration read the friendship on the record") {
        fun maxDealt(move: Short, friendship: Int): Int {
          var best = 0
          for (seed in 0L until 60L) {
            val player = state(1, 30, listOf(move), PLAYER_ID, friendship)
            val wild = state(143, 30, listOf(SPLASH), WILD_ID)
            val before = wild.currentHp
            val events = engine.resolveTurn(battle(player, wild, seed), move)
            val hit =
                events.filterIsInstance<BattleEvent.DamageDealt>().firstOrNull {
                  it.targetId == WILD_ID
                } ?: continue
            best = maxOf(best, before - hit.newHp)
          }
          return best
        }
        // Both are `10 * value / 25` on the friendship each of them reads, so a monster at the
        // ceiling swings 102 power with one and the table's own 1 with the other. The table's
        // power is what both used to do whatever the record said.
        maxDealt(RETURN, MAX_FRIENDSHIP) shouldBeGreaterThan maxDealt(RETURN, 0) * 10
        maxDealt(FRUSTRATION, 0) shouldBeGreaterThan maxDealt(FRUSTRATION, MAX_FRIENDSHIP) * 10
        maxDealt(RETURN, MAX_FRIENDSHIP) shouldBe maxDealt(FRUSTRATION, 0)
      }

      test("final gambit deals the user's hp and the user faints") {
        val player = state(1, 30, listOf(FINAL_GAMBIT), PLAYER_ID)
        val wild = state(143, 50, listOf(SPLASH), WILD_ID)
        val hp = player.currentHp
        val before = wild.currentHp
        val events = engine.resolveTurn(battle(player, wild, seed = 4), FINAL_GAMBIT)

        val hit =
            events.filterIsInstance<BattleEvent.DamageDealt>().first { it.targetId == WILD_ID }
        before - hit.newHp shouldBe hp
        player.currentHp shouldBe 0
        events.filterIsInstance<BattleEvent.Fainted>().map { it.targetId } shouldBe
            listOf(PLAYER_ID)
      }

      test("synchronoise reaches only a target that shares a type") {
        // Bulbasaur is Grass/Poison: Rattata shares nothing, Oddish shares both.
        val player = state(1, 30, listOf(SYNCHRONOISE), PLAYER_ID)
        val rattata = state(19, 10, listOf(SPLASH), WILD_ID)
        engine
            .resolveTurn(battle(player, rattata, seed = 4), SYNCHRONOISE)
            .filterIsInstance<BattleEvent.MoveFailed>()
            .first { it.attackerId == PLAYER_ID }

        val again = state(1, 30, listOf(SYNCHRONOISE), PLAYER_ID)
        val oddish = state(43, 10, listOf(SPLASH), WILD_ID)
        engine
            .resolveTurn(battle(again, oddish, seed = 4), SYNCHRONOISE)
            .filterIsInstance<BattleEvent.DamageDealt>()
            .first { it.targetId == WILD_ID }
      }

      test("guard split averages both defences for both sides") {
        val player = state(1, 30, listOf(GUARD_SPLIT), PLAYER_ID)
        val wild = state(143, 30, listOf(SPLASH), WILD_ID)
        val def = (player.stats.def + wild.stats.def) / 2
        val spDef = (player.stats.spDef + wild.stats.spDef) / 2
        engine.resolveTurn(battle(player, wild, seed = 4), GUARD_SPLIT)

        player.stats.def shouldBe def
        wild.stats.def shouldBe def
        player.stats.spDef shouldBe spDef
        wild.stats.spDef shouldBe spDef
        player.stats.atk shouldNotBe wild.stats.atk
      }

      test("psyshock meets the target's defence, not its special defence") {
        // Steelix has a Defense of 200 and a Special Defense of 65: an 80-power Psychic move into
        // its Special Defense (Extrasensory) outdamages the same power into its Defense (Psyshock).
        fun maxDealt(moveId: Short): Int {
          var best = 0
          for (seed in 0L until 60L) {
            val player = state(1, 40, listOf(moveId), PLAYER_ID)
            val wild = state(208, 40, listOf(SPLASH), WILD_ID)
            val before = wild.currentHp
            val events = engine.resolveTurn(battle(player, wild, seed), moveId)
            val hit =
                events.filterIsInstance<BattleEvent.DamageDealt>().firstOrNull {
                  it.targetId == WILD_ID
                } ?: continue
            best = maxOf(best, before - hit.newHp)
          }
          return best
        }
        maxDealt(EXTRASENSORY) shouldBeGreaterThan maxDealt(PSYSHOCK)
      }

      // ------------------------------------------------------------------ abilities

      test("a monster fights with its own ability and not its species' first") {
        // Azumarill is Thick Fat then Huge Power, and until abilities were wired every battle drew
        // the first slot for everybody. The low bit of the personality value is what picks.
        state(184, 30, listOf(TACKLE), PLAYER_ID, seed = 0).ability shouldBe Ability.THICK_FAT
        state(184, 30, listOf(TACKLE), PLAYER_ID, seed = 1).ability shouldBe Ability.HUGE_POWER
        // The Dream World slot wins over both, and a species without one keeps what it had.
        state(184, 30, listOf(TACKLE), PLAYER_ID, seed = 1, hiddenAbility = true).ability shouldBe
            Ability.SAP_SIPPER
        state(92, 30, listOf(TACKLE), PLAYER_ID, seed = 1, hiddenAbility = true).ability shouldBe
            Ability.LEVITATE
      }

      test("levitate refuses a ground move, and mold breaker takes it anyway") {
        val gastly = state(92, 40, listOf(SPLASH), WILD_ID)
        val digger = state(50, 40, listOf(EARTHQUAKE), PLAYER_ID)
        val floated = engine.resolveTurn(battle(digger, gastly, seed = 3), EARTHQUAKE)
        floated
            .filterIsInstance<BattleEvent.DamageDealt>()
            .none { it.targetId == WILD_ID }
            .shouldBeTrue()
        floated.filterIsInstance<BattleEvent.MoveFailed>().shouldNotBeEmpty()

        // Pinsir's second slot is Mold Breaker, so the same move lands on the same target.
        val breaker = state(127, 40, listOf(EARTHQUAKE), PLAYER_ID, seed = 1)
        breaker.ability shouldBe Ability.MOLD_BREAKER
        val target = state(92, 40, listOf(SPLASH), WILD_ID)
        engine
            .resolveTurn(battle(breaker, target, seed = 3), EARTHQUAKE)
            .filterIsInstance<BattleEvent.DamageDealt>()
            .first { it.targetId == WILD_ID }
      }

      test("wonder guard lets only a super effective move through") {
        val shedinja = state(292, 40, listOf(SPLASH), WILD_ID)
        shedinja.ability shouldBe Ability.WONDER_GUARD
        // Water is neutral on Bug/Ghost, so Wonder Guard turns it away though the chart does not.
        engine
            .resolveTurn(
                battle(state(7, 40, listOf(WATER_GUN), PLAYER_ID), shedinja, seed = 2), WATER_GUN)
            .filterIsInstance<BattleEvent.DamageDealt>()
            .none { it.targetId == WILD_ID }
            .shouldBeTrue()
        // Flying is, so it lands.
        engine
            .resolveTurn(
                battle(
                    state(16, 40, listOf(PECK), PLAYER_ID),
                    state(292, 40, listOf(SPLASH), WILD_ID),
                    seed = 2),
                PECK)
            .filterIsInstance<BattleEvent.DamageDealt>()
            .first { it.targetId == WILD_ID }
      }

      test("huge power doubles the physical hit its species would otherwise land") {
        fun best(seed: Int): Int {
          var most = 0
          for (roll in 0L until 60L) {
            val player = state(184, 40, listOf(TACKLE), PLAYER_ID, seed = seed)
            val wild = state(143, 40, listOf(SPLASH), WILD_ID)
            val before = wild.currentHp
            val hit =
                engine
                    .resolveTurn(battle(player, wild, roll), TACKLE)
                    .filterIsInstance<BattleEvent.DamageDealt>()
                    .firstOrNull { it.targetId == WILD_ID } ?: continue
            if (!hit.crit) most = maxOf(most, before - hit.newHp)
          }
          return most
        }
        val plain = best(0)
        val huge = best(1)
        plain shouldBeGreaterThan 0
        // Doubling the attack stat does not quite double the damage, the formula adds two after
        // the division, so the claim is that it is far more than half again, not exactly twice.
        (huge > plain * 3 / 2).shouldBeTrue()
      }

      test("sturdy holds a knockout at one hit point, but only from full health") {
        val sudowoodo = state(185, 5, listOf(SPLASH), WILD_ID)
        sudowoodo.ability shouldBe Ability.STURDY
        val events =
            engine.resolveTurn(
                battle(state(6, 80, listOf(EARTHQUAKE), PLAYER_ID), sudowoodo, seed = 5),
                EARTHQUAKE)
        events
            .filterIsInstance<BattleEvent.DamageDealt>()
            .first { it.targetId == WILD_ID }
            .newHp shouldBe 1
        events.filterIsInstance<BattleEvent.Fainted>().isEmpty().shouldBeTrue()

        // The same blow on a monster already below full goes all the way through.
        val hurt = state(185, 5, listOf(SPLASH), WILD_ID)
        hurt.currentHp = hurt.stats.hp - 1
        engine
            .resolveTurn(
                battle(state(6, 80, listOf(EARTHQUAKE), PLAYER_ID), hurt, seed = 5), EARTHQUAKE)
            .filterIsInstance<BattleEvent.Fainted>()
            .shouldNotBeEmpty()
      }

      test("contrary turns a drop into a rise") {
        val snivy = state(495, 30, listOf(SPLASH), WILD_ID, hiddenAbility = true)
        snivy.ability shouldBe Ability.CONTRARY
        engine.resolveTurn(battle(state(1, 30, listOf(GROWL), PLAYER_ID), snivy, seed = 4), GROWL)
        snivy.stage(BattleStat.ATTACK) shouldBe 1
      }

      test("storm throw is a critical hit every time") {
        for (seed in 0L until 20L) {
          val player = state(1, 30, listOf(STORM_THROW), PLAYER_ID)
          val wild = state(143, 30, listOf(SPLASH), WILD_ID)
          val hit =
              engine
                  .resolveTurn(battle(player, wild, seed), STORM_THROW)
                  .filterIsInstance<BattleEvent.DamageDealt>()
                  .first { it.targetId == WILD_ID }
          hit.crit shouldBe true
        }
      }
    })
