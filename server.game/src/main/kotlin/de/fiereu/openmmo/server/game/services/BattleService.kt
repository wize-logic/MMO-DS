package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.BattleAction
import de.fiereu.openmmo.common.enums.ChatType
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.Language
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.items.ItemDef
import de.fiereu.openmmo.items.ItemRegistry
import de.fiereu.openmmo.items.generated.Items
import de.fiereu.openmmo.moves.MoveRegistry
import de.fiereu.openmmo.net.game.packets.ChatMessagePacket
import de.fiereu.openmmo.net.game.packets.LocalCharacterDeltaPacket
import de.fiereu.openmmo.net.game.packets.MapLoadedAckPacket
import de.fiereu.openmmo.net.game.packets.PokemonContainerPacket
import de.fiereu.openmmo.net.game.packets.SocialListEntryAddPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleActionSelectPacket
import de.fiereu.openmmo.net.game.packets.battle.BattleChatMessagePacket
import de.fiereu.openmmo.net.game.packets.battle.BattleListEventDetail
import de.fiereu.openmmo.net.game.packets.battle.BattleListEventPacket
import de.fiereu.openmmo.net.game.packets.battle.moves.MOVE_LEARN_NO_SLOT
import de.fiereu.openmmo.net.game.packets.battle.moves.MoveLearnPromptPacket
import de.fiereu.openmmo.net.game.packets.battle.moves.MoveLearnReplyPacket
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.battle.BattleInstance
import de.fiereu.openmmo.server.game.battle.BattleMonState
import de.fiereu.openmmo.server.game.battle.BattlePacketEmitter
import de.fiereu.openmmo.server.game.battle.BattleRegistry
import de.fiereu.openmmo.server.game.battle.BattleResult
import de.fiereu.openmmo.server.game.battle.BattleRewards
import de.fiereu.openmmo.server.game.battle.BattleRng
import de.fiereu.openmmo.server.game.battle.BattleRules
import de.fiereu.openmmo.server.game.battle.CatchRoll
import de.fiereu.openmmo.server.game.battle.MoveLearner
import de.fiereu.openmmo.server.game.battle.PvpFoe
import de.fiereu.openmmo.server.game.battle.StatCalculator
import de.fiereu.openmmo.server.game.battle.TurnEngine
import de.fiereu.openmmo.server.game.battle.WildMonFactory
import de.fiereu.openmmo.server.game.battle.acquiredMonsterDelta
import de.fiereu.openmmo.server.game.script.Pokedex
import de.fiereu.openmmo.server.game.session.CLIENT_RUNS_SCRIPTS
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.StoredCharacter
import de.fiereu.openmmo.server.game.world.interest.InterestManager
import de.fiereu.openmmo.trainer.TrainerDef
import de.fiereu.openmmo.trainer.TrainerRegistry
import io.github.oshai.kotlinlogging.KotlinLogging
import java.time.LocalDateTime
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

private const val CANT_USE_NOW = "You can't use that now."

private const val ALREADY_IN_BATTLE = "You are already in a battle."

/**
 * The moves one monster has been offered and not yet answered for, kept after the battle ends. Only
 * the head has been prompted: the client answers one move at a time, so the rest wait their turn.
 */
private data class PendingMoveLearn(
    val charId: Long,
    val entityId: Long,
    val offered: List<Short>,
)

/**
 * Orchestrates battles: builds the battle state from the party and the opposing side, routes client
 * actions through the [TurnEngine], and persists the outcome. Packets go out through the
 * [BattlePacketEmitter] over the battle's interest key.
 */
@Singleton
class BattleService
@Inject
constructor(
    private val characterStore: CharacterStore,
    private val battles: BattleRegistry,
    private val engine: TurnEngine,
    private val wildMons: WildMonFactory,
    private val emitter: BattlePacketEmitter,
    private val rewards: BattleRewards,
    private val moveLearner: MoveLearner,
    private val interestManager: InterestManager,
    private val speciesRegistry: SpeciesRegistry,
    private val moveRegistry: MoveRegistry,
    private val trainers: TrainerRegistry,
    private val items: ItemRegistry,
    private val blackout: BlackoutService,
    private val budget: GrantBudget,
    private val violations: ViolationLog,
) {

  private val pokeBallItemId: Short by lazy { items.idOf(Items.POKE_BALL).toShort() }

  private val pendingLearns = ConcurrentHashMap<Long, PendingMoveLearn>()

  fun onBattlePacket(event: PacketEvent<*>) {
    log.info { "Battle packet ${event.packet::class.simpleName} received: ${event.packet}" }
  }

  /**
   * A line typed on the battle channel. It is scoped to the sender's battle, both players of a
   * player battle, the sender alone otherwise, and delivered as an ordinary [ChatMessagePacket] of
   * type BATTLE, which is the type the client's Battle tab shows.
   */
  fun onBattleChat(event: PacketEvent<BattleChatMessagePacket>) {
    val session = event.session
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    val text = event.packet.message.trim()
    if (text.isEmpty()) return
    val battle = battles.byChar(charId)
    if (battle == null) {
      session.send(notice("You are not in a battle."))
      return
    }
    val me = characterStore.getCharacter(charId) ?: return
    val line =
        ChatMessagePacket(
            type = ChatType.BATTLE,
            language = Language.EN,
            message = text,
            sender = me.info.name,
            senderId = charId,
        )
    battle.session.send(line)
    battle.foeSession?.takeIf { it.channel.isActive }?.send(line)
  }

  suspend fun onBattleAction(event: PacketEvent<BattleActionSelectPacket>) {
    val charId = event.session.attributes[PLAYER_STATE]?.characterId ?: return
    val battle = battles.byChar(charId) ?: return
    if (battle.pendingResult != null) return
    val action = event.packet
    val isFoe = battle.isPvp && charId == battle.foeCharId
    log.info { "Battle action char=$charId: $action" }
    val active = if (isFoe) battle.opponentMon() else battle.activeMon()
    // While the active mon is fainted the player owes a replacement and may only switch.
    if (active.fainted && action.action != BattleAction.SWITCH) return
    // The move id is the client's, and the engine looked it up in the whole move table rather than
    // in the monster's four, so any monster could use any move in the game. A move it does not
    // know also costs no pp, because there is no slot to take it from.
    if (action.action == BattleAction.MOVE && !knowsMove(active, action.moveOrItemId)) {
      log.warn {
        "char=$charId picked move ${action.moveOrItemId} for ${active.entityId}," +
            " which knows ${active.moves.joinToString(",") { "${it.id}/${it.pp}pp" }}"
      }
      emitter.sendNotice(battle, CANT_USE_NOW)
      emitter.sendPrompt(battle)
      return
    }
    if (battle.isPvp) {
      onPvpAction(battle, isFoe, action)
      return
    }
    when (action.action) {
      BattleAction.MOVE -> resolveTurn(battle, action.moveOrItemId)
      BattleAction.ITEM -> useItem(battle, action)
      BattleAction.SWITCH -> switchMon(battle, action.moveOrItemId)
      BattleAction.RUN -> flee(battle)
    }
  }

  /** Drops the move the player picked and puts the offered one in its place. */
  fun onMoveLearnReply(event: PacketEvent<MoveLearnReplyPacket>) {
    val charId = event.session.attributes[PLAYER_STATE]?.characterId ?: return
    val reply = event.packet
    val pending = pendingLearns[reply.entityId] ?: return
    if (pending.charId != charId) return
    // Only the head of the queue has been prompted, so it is the only move the answer may name.
    val offered = pending.offered.first()
    if (reply.moveId != offered) {
      log.warn { "char=$charId answered ${reply.moveId} to an offer of $offered" }
      return
    }
    val rest = pending.offered.drop(1)
    if (rest.isEmpty()) pendingLearns.remove(reply.entityId)
    else pendingLearns[reply.entityId] = pending.copy(offered = rest)

    if (reply.slot >= 0) {
      learnOffered(charId, event.session, reply.entityId, reply.slot.toInt(), offered)
    } else {
      log.info { "char=$charId let ${reply.entityId} keep its moveset instead of $offered" }
    }
    // A level up can offer more than one move; the next offer only goes out once this one is done.
    rest.firstOrNull()?.let {
      event.session.send(MoveLearnPromptPacket(reply.entityId, MOVE_LEARN_NO_SLOT, it))
    }
  }

  private fun learnOffered(
      charId: Long,
      session: SessionContext,
      entityId: Long,
      slot: Int,
      moveId: Short,
  ) {
    val stored =
        characterStore.getCharacter(charId)?.pokemon?.firstOrNull { it.id == entityId } ?: return
    val moves = stored.moves.toMutableList()
    if (!moveLearner.apply(moves, slot, moveId, moveId)) {
      log.warn { "char=$charId picked slot $slot of ${moves.size} for $moveId on $entityId" }
      return
    }
    characterStore.updatePokemon(charId, stored.copy(moves = moves))
    characterStore.flushCharacterAsync(charId)
    // A battle still running holds its own copy, and the next reward writes that copy back over
    // the store. Move the live one across so the pick survives the rest of the battle.
    battles
        .byChar(charId)
        ?.party
        ?.firstOrNull { it.entityId == entityId }
        ?.let { live ->
          live.moves.clear()
          live.moves.addAll(moves.map { PokemonMove(it.id, it.pp) })
          live.source = live.source.copy(moves = moves)
        }
    session.send(emitter.moveSlotsDelta(entityId, moves.map { it.id to it.pp }, 0))
    // The party is how the client holds a monster's moves outside a battle, and a reply can arrive
    // after the battle has already sent its own copy, so send the container the change landed in.
    session.send(
        PokemonContainerPacket(
            container = PokemonContainer.PARTY,
            hasChange = true,
            delete = false,
            pokemon = characterStore.getCharacter(charId)?.pokemon.orEmpty(),
        ),
    )
  }

  /**
   * Takes the monster, for the developer command that exists to skip the game. No ball, so no roll
   * and no allowance: /catch is gated on the account's own developer bit, and a testing shortcut
   * that had to get lucky would not be one.
   */
  suspend fun catchActiveWild(charId: Long): Boolean {
    val battle = battles.byChar(charId) ?: return false
    catchWild(battle, ball = null)
    return true
  }

  /** Ends a running battle when the player disconnects, keeping the last hp and pp state. */
  fun onDisconnect(session: SessionContext) {
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    pendingLearns.values.removeIf { it.charId == charId }
    val battle = battles.byChar(charId) ?: return
    if (battle.isPvp) {
      forfeit(battle, foeForfeited = charId == battle.foeCharId)
      finishBattle(battle, BattleResult.DISCONNECTED)
      return
    }
    persistParty(battle)
    finishBattle(battle, BattleResult.DISCONNECTED)
  }

  /** Resumes scripts after returning to the overworld. */
  fun onClientReady(event: PacketEvent<MapLoadedAckPacket>) {
    if (event.packet.data.isNotEmpty()) return
    val charId = event.session.attributes[PLAYER_STATE]?.characterId ?: return
    val battle = battles.byChar(charId) ?: return
    val result = battle.pendingResult ?: return
    finishBattle(battle, result)
  }

  /** True while the character has a battle running, so callers can skip starting another. */
  fun inBattle(charId: Long): Boolean = battles.byChar(charId) != null

  fun startWildBattle(session: SessionContext, dexId: Int, level: Int) {
    createWildBattle(session, dexId, level, catchable = true, escapable = true)
  }

  /** Starts a 1v1 between [session] and [foe]. Each side fights with its stored party. */
  fun startPlayerBattle(session: SessionContext, foe: SessionContext) {
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    val foeId = foe.attributes[PLAYER_STATE]?.characterId ?: return
    if (battles.byChar(charId) != null) {
      session.send(notice(ALREADY_IN_BATTLE))
      return
    }
    if (battles.byChar(foeId) != null) {
      session.send(notice("They are already in a battle."))
      return
    }
    val stored = characterStore.getCharacter(charId) ?: return
    val foeStored = characterStore.getCharacter(foeId) ?: return
    val party = partyFrom(stored, session) ?: return
    val foeParty = partyFrom(foeStored, session) ?: return
    if (party.all { it.fainted }) {
      session.send(notice("All of your monsters have fainted."))
      return
    }
    if (foeParty.all { it.fainted }) {
      session.send(notice("They have no monster that can fight."))
      return
    }
    val firstAlive = party.indexOfFirst { !it.fainted }
    val foeFirst = foeParty.indexOfFirst { !it.fainted }
    val battle =
        battles.create(
            charId,
            session,
            party,
            foeParty,
            BattleRng(),
            BattleRules(catchable = false, escapable = true),
            PvpFoe(foe, foeId, foeStored.info.name),
        )
            ?: run {
              session.send(notice(ALREADY_IN_BATTLE))
              return
            }
    battle.activeSlot = firstAlive
    battle.seenActive.clear()
    battle.seenActive.add(firstAlive)
    battle.opponentSlot = foeFirst
    battle.opponentSeen.clear()
    battle.opponentSeen.add(foeFirst)
    interestManager.join(session, battle.key)
    interestManager.join(foe, battle.key)
    log.info {
      "Starting player battle char=$charId (${stored.info.name}) vs char=$foeId (${foeStored.info.name})"
    }
    emitter.sendStart(battle, stored.info.name)
    session.send(notice("Battle with ${foeStored.info.name}!"))
    foe.send(notice("Battle with ${stored.info.name}!"))
  }

  /** Runs a story battle and waits for its scene. */
  suspend fun startScriptedBattle(
      session: SessionContext,
      dexId: Int,
      level: Int,
      moveIds: List<Int> = emptyList(),
  ): BattleResult {
    val battle =
        createWildBattle(
            session,
            dexId,
            level,
            catchable = false,
            escapable = false,
            moveIds = moveIds,
        ) ?: return BattleResult.FAILED
    return battle.completion.await()
  }

  /** Runs a battle against the decomp trainer with this id and waits for its scene. */
  suspend fun startTrainerBattle(
      session: SessionContext,
      region: Region,
      trainerId: Int,
  ): BattleResult {
    val trainer = trainers.get(region, trainerId)
    if (trainer == null) {
      log.warn { "No $region trainer with id $trainerId" }
      return BattleResult.FAILED
    }
    return startTrainerBattle(session, trainer)
  }

  /** Runs a battle against a trainer's whole team and waits for its scene. */
  suspend fun startTrainerBattle(session: SessionContext, trainer: TrainerDef): BattleResult {
    val battle =
        createBattle(
            session,
            trainer.party.map { OpponentSpec(it.dexId, it.level, it.moveIds, it.iv) },
            catchable = false,
            escapable = false,
            trainer = trainer,
        ) ?: return BattleResult.FAILED
    return battle.completion.await()
  }

  /** Empty [moveIds] keeps the level up moveset, a null [iv] rolls one like a wild encounter. */
  private data class OpponentSpec(
      val dexId: Int,
      val level: Int,
      val moveIds: List<Int>,
      val iv: Int? = null,
  )

  private fun createWildBattle(
      session: SessionContext,
      dexId: Int,
      level: Int,
      catchable: Boolean,
      escapable: Boolean,
      moveIds: List<Int> = emptyList(),
  ): BattleInstance? =
      createBattle(session, listOf(OpponentSpec(dexId, level, moveIds)), catchable, escapable)

  private fun partyFrom(
      stored: StoredCharacter,
      session: SessionContext,
  ): List<BattleMonState>? {
    if (stored.pokemon.isEmpty()) {
      session.send(notice("You need a monster in your party to battle."))
      return null
    }
    val party = mutableListOf<BattleMonState>()
    for ((index, mon) in stored.pokemon.withIndex()) {
      val def = speciesRegistry.get(mon.dexId)
      if (def == null) {
        session.send(notice("Your party has a species the battle data does not cover yet."))
        return null
      }
      party += BattleMonState(mon.id, def, index, mon, StatCalculator.computeAll(def, mon))
    }
    return party
  }

  private fun createBattle(
      session: SessionContext,
      opponents: List<OpponentSpec>,
      catchable: Boolean,
      escapable: Boolean,
      trainer: TrainerDef? = null,
  ): BattleInstance? {
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return null
    if (battles.byChar(charId) != null) {
      session.send(notice(ALREADY_IN_BATTLE))
      return null
    }
    val stored = characterStore.getCharacter(charId) ?: return null
    val party = partyFrom(stored, session) ?: return null
    if (party.all { it.fainted }) {
      session.send(notice("All of your monsters have fainted."))
      return null
    }
    val rng = BattleRng()
    val enemies = mutableListOf<BattleMonState>()
    for (spec in opponents) {
      var rolled = wildMons.create(spec.dexId, spec.level, rng)
      if (rolled == null) {
        session.send(notice("Unknown species ${spec.dexId}."))
        return null
      }
      if (spec.moveIds.isNotEmpty()) {
        rolled =
            rolled.copy(
                moves =
                    spec.moveIds.take(4).map { id ->
                      PokemonMove(id.toShort(), (moveRegistry.get(id)?.pp ?: 0).toByte())
                    } + List((4 - spec.moveIds.size).coerceAtLeast(0)) { PokemonMove(0, 0) })
      }
      val def = speciesRegistry.get(spec.dexId)!!
      // A trainer's monsters are built to a fixed difficulty, so they must not keep the rolled
      // IVs. Max hp moves with them, and the monster comes out full.
      if (spec.iv != null) {
        val ivs =
            IVs().apply {
              hp = spec.iv
              atk = spec.iv
              this.def = spec.iv
              spAtk = spec.iv
              spDef = spec.iv
              spd = spec.iv
            }
        val fixed = rolled.copy(iVs = ivs)
        rolled = fixed.copy(hp = StatCalculator.computeAll(def, fixed).hp.toShort())
      }
      enemies +=
          BattleMonState(rolled.id, def, null, rolled, StatCalculator.computeAll(def, rolled))
    }
    log.info {
      "Starting battle for char=$charId (${stored.info.name}) against " +
          enemies.joinToString { "${it.species.name} level ${it.level}" }
    }
    val battle =
        battles.create(
            charId, session, party, enemies, rng, BattleRules(catchable, escapable, trainer))
            ?: run {
              session.send(notice(ALREADY_IN_BATTLE))
              return null
            }
    val firstAlive = party.indexOfFirst { !it.fainted }
    battle.activeSlot = firstAlive
    battle.seenActive.clear()
    battle.seenActive.add(firstAlive)
    interestManager.join(session, battle.key)
    emitter.sendStart(battle, stored.info.name)
    for (spec in opponents) {
      // Persist only: the battle-start sequence is a measured packet order, and a dex
      // unlock has no capture placing it in that burst.
      Pokedex.markSeen(characterStore, session = null, charId, spec.dexId)
    }
    return battle
  }

  /**
   * Whether this monster may use the move the client picked: one of its own four, with pp left. Pp
   * is checked here rather than in the engine, whose own fallback for a monster with nothing left
   * is to swing anyway, which is the game's behaviour and not something a client asked for.
   */
  private fun knowsMove(mon: BattleMonState, moveId: Short): Boolean =
      mon.moves.any { it.id == moveId && it.id.toInt() != 0 && it.pp > 0 }

  private suspend fun useItem(battle: BattleInstance, action: BattleActionSelectPacket) {
    val item = items.get(action.moveOrItemId.toInt())
    if (item == null) {
      log.info { "char=${battle.charId} used unknown item ${action.moveOrItemId}" }
      emitter.sendNotice(battle, CANT_USE_NOW)
      emitter.sendPrompt(battle)
      return
    }
    if (item.isBall) {
      // A ball has to be in the bag and leaves it when thrown. Neither was asked, so one packet
      // naming a ball id caught anything from an empty bag, as often as you liked.
      val ballId = action.moveOrItemId.toInt()
      val held = characterStore.getCharacter(battle.charId)?.items?.get(ballId) ?: 0
      if (held < 1) {
        log.warn { "char=${battle.charId} threw a ${item.name} it does not have" }
        emitter.sendNotice(battle, CANT_USE_NOW)
        emitter.sendPrompt(battle)
        return
      }
      if (!characterStore.addItem(battle.charId, ballId, -1)) {
        emitter.sendPrompt(battle)
        return
      }
      battle.session.send(
          itemStackUpdatePacket(
              ballId, characterStore.getCharacter(battle.charId)?.items?.get(ballId) ?: 0))
      catchWild(battle, item)
      return
    }
    if (item.healsHp) {
      useHealItem(battle, action, item)
      return
    }
    log.info {
      "char=${battle.charId} used ${item.name}, which this build does not apply in battle"
    }
    emitter.sendNotice(battle, CANT_USE_NOW)
    emitter.sendPrompt(battle)
  }

  private suspend fun useHealItem(
      battle: BattleInstance,
      action: BattleActionSelectPacket,
      item: ItemDef,
  ) {
    val stored = characterStore.getCharacter(battle.charId) ?: return
    if ((stored.items[action.moveOrItemId.toInt()] ?: 0) < 1) {
      emitter.sendNotice(battle, CANT_USE_NOW)
      emitter.sendPrompt(battle)
      return
    }
    val target =
        if (action.targetEntityId != 0L) {
          battle.party.firstOrNull { it.entityId == action.targetEntityId }
        } else {
          battle.activeMon()
        }
    if (target == null || target.fainted) {
      emitter.sendNotice(battle, "It won't have any effect.")
      emitter.sendPrompt(battle)
      return
    }
    val maxHp = target.stats.hp
    if (target.currentHp >= maxHp) {
      emitter.sendNotice(battle, "It won't have any effect.")
      emitter.sendPrompt(battle)
      return
    }

    val healed = (target.currentHp + item.healAmount(maxHp)).coerceAtMost(maxHp)
    if (!characterStore.addItem(battle.charId, action.moveOrItemId.toInt(), -1)) {
      emitter.sendPrompt(battle)
      return
    }
    target.currentHp = healed
    target.source = target.source.copy(hp = healed.toShort())
    characterStore.updatePokemon(battle.charId, target.source)
    log.info { "char=${battle.charId} used ${item.name} on ${target.entityId}: hp -> $healed" }
    val after = characterStore.getCharacter(battle.charId)
    if (after != null) {
      val left = after.items[action.moveOrItemId.toInt()] ?: 0
      battle.session.send(itemStackUpdatePacket(action.moveOrItemId.toInt(), left))
    }
    emitter.sendCurrentHp(battle, target.entityId, healed.toShort())
    // Using an item spends the turn the way a voluntary switch does.
    emitter.sendEvents(battle, engine.resolveSwitchTurn(battle))
    afterTurn(battle)
  }

  private suspend fun onPvpAction(
      battle: BattleInstance,
      isFoe: Boolean,
      action: BattleActionSelectPacket,
  ) {
    val selfFainted = if (isFoe) battle.opponentMon().fainted else battle.activeMon().fainted
    if (selfFainted && action.action == BattleAction.SWITCH) {
      if (isFoe) switchFoe(battle, action.moveOrItemId, forced = true)
      else switchMon(battle, action.moveOrItemId)
      return
    }
    if (action.action == BattleAction.RUN) {
      forfeit(battle, isFoe)
      return
    }
    if (action.action == BattleAction.ITEM) {
      val dest = if (isFoe) battle.foeSession else battle.session
      dest?.let { emitter.sendNoticeTo(it, "Items are not used in player battles.") }
      return
    }
    if (isFoe) {
      if (battle.pendingFoeAction != null) return
      battle.pendingFoeAction = action
    } else {
      if (battle.pendingPlayerAction != null) return
      battle.pendingPlayerAction = action
    }
    val playerAction = battle.pendingPlayerAction ?: return
    val foeAction = battle.pendingFoeAction ?: return
    battle.pendingPlayerAction = null
    battle.pendingFoeAction = null
    resolvePvpTurn(battle, playerAction, foeAction)
  }

  private suspend fun resolvePvpTurn(
      battle: BattleInstance,
      playerAction: BattleActionSelectPacket,
      foeAction: BattleActionSelectPacket,
  ) {
    val playerMoves = playerAction.action == BattleAction.MOVE
    val foeMoves = foeAction.action == BattleAction.MOVE
    if (playerAction.action == BattleAction.SWITCH) {
      switchMon(battle, playerAction.moveOrItemId, spendTurn = false)
    }
    if (foeAction.action == BattleAction.SWITCH) {
      switchFoe(battle, foeAction.moveOrItemId, forced = false)
    }
    val events =
        when {
          playerMoves && foeMoves ->
              engine.resolvePvpTurn(battle, playerAction.moveOrItemId, foeAction.moveOrItemId)
          playerMoves && !foeMoves -> engine.resolvePlayerAttack(battle, playerAction.moveOrItemId)
          !playerMoves && foeMoves -> engine.resolveFoeAttack(battle, foeAction.moveOrItemId)
          else -> emptyList()
        }
    if (events.isNotEmpty()) emitter.sendEvents(battle, events)
    afterTurn(battle)
  }

  private suspend fun resolveTurn(battle: BattleInstance, moveId: Short) {
    val events = engine.resolveTurn(battle, moveId)
    emitter.sendEvents(battle, events)
    afterTurn(battle)
  }

  private suspend fun afterTurn(battle: BattleInstance) {
    when {
      battle.opponent.all { it.fainted } -> endVictory(battle)
      battle.party.all { it.fainted } -> endDefeat(battle)
      else -> {
        if (battle.isPvp) {
          afterPvpTurn(battle)
          return
        }
        if (battle.opponentMon().fainted) {
          awardXp(battle, battle.opponentMon())
          sendOutNextOpponent(battle)
        }
        // The active mon fainted with a live backup. Open the switch screen instead of the action
        // prompt. The replacement arrives as a normal SWITCH action.
        if (battle.activeMon().fainted) {
          emitter.sendSwitchPrompt(battle)
        } else {
          battle.turn += 1
          emitter.sendPrompt(battle)
        }
      }
    }
  }

  private fun afterPvpTurn(battle: BattleInstance) {
    val foeFainted = battle.opponentMon().fainted
    val playerFainted = battle.activeMon().fainted
    if (foeFainted) emitter.sendSwitchPrompt(battle, toFoe = true)
    if (playerFainted) emitter.sendSwitchPrompt(battle, toFoe = false)
    if (!foeFainted && !playerFainted) {
      battle.turn += 1
      emitter.sendPrompt(battle)
    }
  }

  private suspend fun switchMon(
      battle: BattleInstance,
      partyIndex: Short,
      spendTurn: Boolean = true,
  ) {
    val target = partyIndex.toInt()
    val mon = battle.party.getOrNull(target)
    val forced = battle.activeMon().fainted
    if (mon == null || mon.fainted || target == battle.activeSlot) {
      // Reopen the switch screen on an invalid forced choice, otherwise re-prompt for an action.
      // The turn does not move: naming the slot already out used to advance it and skip the
      // opponent's attack, so repeating that was an endless free stall.
      if (forced) {
        emitter.sendSwitchPrompt(battle)
      } else {
        emitter.sendPrompt(battle)
      }
      return
    }
    // A forced switch confirms the choice before the switch-in. The captures pair the confirm with
    // a full block for a new mon and with a return block for a mon that was already active.
    if (forced) emitter.sendSwitchConfirm(battle)
    performSwitch(battle, target)
    if (forced) {
      // Replacing a fainted mon does not spend a turn, the new mon acts next.
      battle.turn += 1
      emitter.sendPrompt(battle)
    } else if (spendTurn) {
      // A voluntary switch spends the turn, so the wild attacks the incoming mon.
      emitter.sendEvents(battle, engine.resolveSwitchTurn(battle))
      afterTurn(battle)
    }
  }

  private fun switchFoe(
      battle: BattleInstance,
      partyIndex: Short,
      forced: Boolean,
  ) {
    val target = partyIndex.toInt()
    val mon = battle.opponent.getOrNull(target)
    if (mon == null || mon.fainted || target == battle.opponentSlot) {
      if (forced) emitter.sendSwitchPrompt(battle, toFoe = true)
      return
    }
    if (forced) emitter.sendSwitchConfirm(battle)
    val oldSlot = battle.opponentSlot
    val fullBlock = target !in battle.opponentSeen
    battle.opponent.getOrNull(oldSlot)?.clearStages()
    battle.opponent.getOrNull(target)?.clearStages()
    battle.opponentSlot = target
    battle.opponentSeen.add(target)
    log.info {
      "Switch foe char=${battle.foeCharId} slot $oldSlot -> $target (fullBlock=$fullBlock)"
    }
    emitter.sendOpponentSwitchIn(battle, oldSlot, fullBlock)
    if (forced) {
      battle.turn += 1
      emitter.sendPrompt(battle)
    }
  }

  private fun sendOutNextOpponent(battle: BattleInstance) {
    val next = battle.opponent.indexOfFirst { !it.fainted }
    if (next < 0) return
    battle.opponent.getOrNull(next)?.clearStages()
    val fullBlock = next !in battle.opponentSeen
    val oldSlot = battle.opponentSlot
    battle.opponentSlot = next
    battle.opponentSeen.add(next)
    log.info { "Opponent sends out slot $next for char=${battle.charId}" }
    emitter.sendOpponentSwitchIn(battle, oldSlot, fullBlock)
  }

  private fun performSwitch(battle: BattleInstance, target: Int) {
    val oldSlot = battle.activeSlot
    val fullBlock = target !in battle.seenActive
    // Stat changes belong to the time on the field, not to the monster.
    battle.party.getOrNull(oldSlot)?.clearStages()
    battle.party.getOrNull(target)?.clearStages()
    battle.activeSlot = target
    battle.seenActive.add(target)
    log.info { "Switch char=${battle.charId} slot $oldSlot -> $target (fullBlock=$fullBlock)" }
    emitter.sendSwitchIn(battle, oldSlot, fullBlock)
  }

  private fun flee(battle: BattleInstance) {
    if (!battle.escapable) {
      emitter.sendNotice(battle, "You can't run from this battle.")
      emitter.sendPrompt(battle)
      return
    }
    emitter.sendFled(battle)
    persistParty(battle)
    battle.pendingResult = BattleResult.FLED
  }

  private fun forfeit(battle: BattleInstance, foeForfeited: Boolean) {
    val runnerName =
        if (foeForfeited) battle.foeName
        else characterStore.getCharacter(battle.charId)?.info?.name ?: "Someone"
    val winnerSession = if (foeForfeited) battle.session else battle.foeSession
    val loserSession = if (foeForfeited) battle.foeSession else battle.session
    persistParty(battle)
    persistFoe(battle)
    val party = characterStore.getCharacter(battle.charId)?.pokemon ?: emptyList()
    val foeParty = characterStore.getCharacter(battle.foeCharId)?.pokemon ?: emptyList()
    emitter.sendBattleEnd(battle, party, foeParty)
    winnerSession?.send(notice("$runnerName forfeited. You win!"))
    loserSession?.send(notice("You forfeited."))
    battle.pendingResult = BattleResult.FLED
  }

  private suspend fun catchWild(battle: BattleInstance, ball: ItemDef?) {
    if (!battle.catchable) {
      emitter.sendNotice(battle, "You can't catch this monster.")
      emitter.sendPrompt(battle)
      return
    }
    // The ball has to hold. It always did, which was the wrong game and an unattended bot's whole
    // program: answer every battle with one packet and take the monster.
    val target = battle.opponentMon()
    if (ball != null &&
        !CatchRoll.holds(
            ball, target.species.catchRate, target.currentHp, target.stats.hp, battle.rng)) {
      emitter.sendNotice(battle, "Oh, no! The Pokemon broke free!")
      emitter.sendPrompt(battle)
      return
    }
    // Counted the way a client-reported grant is. Nothing here is a claim, but the allowance is
    // about how many monsters one character takes out of the world in a stretch.
    if (ball != null &&
        (!budget.allow(battle.charId, GrantBudget.Kind.MONSTERS, 1) ||
            !budget.allow(battle.charId, GrantBudget.Kind.MONSTERS_HOURLY, 1))) {
      violations.record(
          battle.charId,
          ViolationLog.Kind.PAST_ALLOWANCE,
          "is catching faster than an allowance of monsters permits")
      emitter.sendNotice(battle, "Oh, no! The Pokemon broke free!")
      emitter.sendPrompt(battle)
      return
    }
    val stored = characterStore.getCharacter(battle.charId) ?: return
    val offered =
        battle
            .opponentMon()
            .source
            .copy(
                ownerId = battle.charId,
                container = PokemonContainer.PARTY,
                ot = stored.info.name,
                hp = battle.opponentMon().currentHp.toShort(),
                moves = battle.opponentMon().moves.map { PokemonMove(it.id, it.pp) },
                caughtAt = LocalDateTime.now(),
                // Where the trainer is standing is where it was caught. Recorded once, here,
                // because the party is rebuilt on the client from these records at every join and
                // a place that is not on the record is one the engine invents.
                caughtRegionId = (stored.info.positionRegionId.toInt() and 0xFF),
                caughtBankId = (stored.info.positionBankId.toInt() and 0xFF),
                caughtMapId = (stored.info.positionMapId.toInt() and 0xFF),
            )
    // A full party sends the catch to the box, the way the cartridge does. The store seats it
    // either way, so the record and the packets below name the same slot.
    val caught =
        characterStore.addPokemon(battle.charId, offered)
            ?: characterStore.addPokemon(
                battle.charId, offered.copy(container = PokemonContainer.PC))
    if (caught == null) {
      // Nowhere to put it. The cartridge never lets the encounter start at all when the party and
      // every box are full, so the nearest honest answer here is a refusal that leaves the battle
      // standing, the old path ended the battle and the monster was simply gone.
      log.warn { "char=${battle.charId} caught a monster with no room in the party or the box" }
      emitter.sendNotice(battle, "There's no room for it: your party and your boxes are full.")
      emitter.sendPrompt(battle)
      return
    }
    log.info {
      "Caught wild ${battle.opponentMon().species.name} for char=${battle.charId}" +
          if (caught.container == PokemonContainer.PC) ", sent to the box (party full)" else ""
    }
    // The caught monster is sent as a full 148-byte record on opcode 0x14 before the ball-throw
    // event, so the client can resolve the monster when the throw lands.
    battle.session.send(SocialListEntryAddPacket(caught))
    battle.session.send(acquiredMonsterDelta(caught, battle.opponentMon().species))
    // The throw event, naming the ball that was actually thrown. It used to name the Poke Ball
    // whatever went in, which drew the wrong ball for every other kind.
    battle.session.send(
        BattleListEventPacket(
            kind = 0,
            value = ball?.let { items.idOrNull(it)?.toShort() } ?: pokeBallItemId,
            subKind = 4,
            detail = BattleListEventDetail(listType = 1, value = 1),
        ),
    )
    // A box catch is the one outcome no other packet describes: the battle's end sends the party
    // back and nothing sends the PC, so without this the monster sat in the database and nowhere
    // on screen until the next join. The cartridge says so in the battle text too.
    if (caught.container == PokemonContainer.PC) {
      emitter.sendNotice(battle, "${battle.opponentMon().species.name} was transferred to a Box.")
      battle.session.send(
          PokemonContainerPacket(
              container = PokemonContainer.PC,
              hasChange = true,
              delete = false,
              pokemon = characterStore.getCharacter(battle.charId)?.pcStorage.orEmpty(),
          ),
      )
    }
    Pokedex.markCaught(characterStore, battle.session, battle.charId, caught.dexId)
    endBattle(battle, BattleResult.CAUGHT)
  }

  private suspend fun endVictory(battle: BattleInstance) {
    if (battle.isPvp) {
      endPvp(battle, challengerWon = true)
      return
    }
    awardXp(battle, battle.opponentMon())
    val prize = battle.trainer?.let { rewards.trainerPrize(it, battle.opponent.last().level) } ?: 0
    val paid = prize > 0 && characterStore.addMoney(battle.charId, prize)
    if (prize > 0 && !paid) {
      log.error { "Could not pay char=${battle.charId} the $prize prize" }
    }
    if (paid) {
      val after = characterStore.getCharacter(battle.charId)?.info?.money
      if (after != null) {
        battle.session.send(LocalCharacterDeltaPacket(money = after))
      }
    }
    endBattle(battle, BattleResult.VICTORY, battle.activeMon().entityId, if (paid) prize else 0)
  }

  /**
   * Pays the active monster for knocking [defeated] out. A trainer's team is paid for one at a
   * time, as each faints, which is when the captures show the delta going out.
   */
  private fun awardXp(battle: BattleInstance, defeated: BattleMonState) {
    val winner = battle.activeMon()
    val reward = rewards.apply(winner, defeated.species, defeated.level, battle.trainer != null)
    log.info {
      "char=${battle.charId} won: +${reward.xpGained} xp, level ${winner.level} -> ${reward.newLevel}"
    }
    winner.currentHp = reward.newCurrentHp
    val outcome =
        moveLearner.learn(winner.moves, winner.source.dexId, winner.level, reward.newLevel)
    emitter.sendVictoryDelta(battle, winner.entityId, reward)
    // A move that fitted is announced with the slot it went into; the client writes the "learned"
    // line from that and asks nothing. Only a full moveset gets a slotless offer to answer.
    for (move in outcome.learned) {
      battle.session.send(
          MoveLearnPromptPacket(winner.entityId, move.slot.toByte(), move.moveId.toShort()),
      )
    }
    if (outcome.offered.isNotEmpty()) {
      val offered = outcome.offered.map { it.moveId.toShort() }
      val queued = pendingLearns[winner.entityId]
      pendingLearns[winner.entityId] =
          PendingMoveLearn(battle.charId, winner.entityId, queued?.offered.orEmpty() + offered)
      // An unanswered offer is still on the client's screen; the queue holds the rest until it is.
      if (queued == null) {
        battle.session.send(
            MoveLearnPromptPacket(winner.entityId, MOVE_LEARN_NO_SLOT, offered.first()),
        )
      }
    }
    val grown =
        winner.source.copy(
            level = reward.newLevel.toByte(),
            xp = reward.newXp,
            hp = reward.newCurrentHp.toShort(),
            eVs = reward.newEvs,
            moves = winner.moves.map { PokemonMove(it.id, it.pp) },
        )
    winner.source = grown
    winner.stats = reward.newStats
    characterStore.updatePokemon(battle.charId, grown)
  }

  private fun endDefeat(battle: BattleInstance) {
    if (battle.isPvp) {
      endPvp(battle, challengerWon = false)
      return
    }
    endBattle(battle, BattleResult.DEFEAT)
  }

  private fun endPvp(battle: BattleInstance, challengerWon: Boolean) {
    persistParty(battle)
    persistFoe(battle)
    val party = characterStore.getCharacter(battle.charId)?.pokemon ?: emptyList()
    val foeParty = characterStore.getCharacter(battle.foeCharId)?.pokemon ?: emptyList()
    emitter.sendBattleEnd(battle, party, foeParty)
    val challengerName = characterStore.getCharacter(battle.charId)?.info?.name ?: "Someone"
    if (challengerWon) {
      battle.session.send(notice("You won against ${battle.foeName}!"))
      battle.foeSession?.send(notice("You lost to $challengerName."))
      battle.pendingResult = BattleResult.VICTORY
    } else {
      battle.session.send(notice("You lost to ${battle.foeName}."))
      battle.foeSession?.send(notice("You won against $challengerName!"))
      battle.pendingResult = BattleResult.DEFEAT
    }
  }

  // Known issue: the caught monster does not show up in the party until the client reopens it.
  private fun endBattle(
      battle: BattleInstance,
      result: BattleResult,
      skip: Long? = null,
      prizeMoney: Int = 0,
  ) {
    persistParty(battle, skip)
    val party = characterStore.getCharacter(battle.charId)?.pokemon ?: emptyList()
    emitter.sendBattleEnd(battle, party, prizeMoney)
    battle.pendingResult = result
  }

  /** Write the battle's live hp and pp back into the party and flush the character. */
  private fun persistParty(battle: BattleInstance, skip: Long? = null) {
    // A session that runs its own battles never plays this instance: the client's engine
    // computes the fight and reports it over BattleOutcome, then runs from this one.
    if (battle.session.attributes[CLIENT_RUNS_SCRIPTS] == true) return
    writeSide(battle.charId, battle.party, skip)
  }

  private fun persistFoe(battle: BattleInstance) {
    if (!battle.isPvp) return
    writeSide(battle.foeCharId, battle.opponent, skip = null)
  }

  private fun writeSide(charId: Long, side: List<BattleMonState>, skip: Long?) {
    for (state in side) {
      if (state.entityId == skip) continue
      val updated =
          state.source.copy(
              hp = state.currentHp.toShort(),
              moves = state.moves.map { PokemonMove(it.id, it.pp) },
          )
      characterStore.updatePokemon(charId, updated)
    }
    characterStore.flushCharacterAsync(charId)
  }

  private fun finishBattle(battle: BattleInstance, result: BattleResult) {
    interestManager.leave(battle.session, battle.key)
    battle.foeSession?.let { interestManager.leave(it, battle.key) }
    battles.remove(battle.charId)
    battle.completion.complete(result)
    // A wiped party can neither fight nor walk into a Pokémon Center, so the way back has to start
    // here rather than wait for the player to do something. A player battle leaves them standing.
    if (result == BattleResult.DEFEAT && !battle.isPvp) {
      battle.session.attributes[PLAYER_STATE]?.let { blackout.blackOut(battle.session, it) }
    }
  }
}
