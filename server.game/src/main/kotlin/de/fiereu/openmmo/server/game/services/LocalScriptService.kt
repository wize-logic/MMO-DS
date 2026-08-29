package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.ContestConditions
import de.fiereu.openmmo.common.MAX_CONTEST_STAT
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.items.ItemRegistry
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.net.game.packets.BagDeltaPacket
import de.fiereu.openmmo.net.game.packets.BattleOutcomePacket
import de.fiereu.openmmo.net.game.packets.ClientScriptOwnershipPacket
import de.fiereu.openmmo.net.game.packets.LocalCharacterDeltaPacket
import de.fiereu.openmmo.net.game.packets.MoneyDeltaPacket
import de.fiereu.openmmo.net.game.packets.PokemonContainerPacket
import de.fiereu.openmmo.net.game.packets.PokemonReleasePacket
import de.fiereu.openmmo.net.game.packets.RegisteredItemPacket
import de.fiereu.openmmo.net.game.packets.ScriptGrantPacket
import de.fiereu.openmmo.net.game.packets.ScriptStatePacket
import de.fiereu.openmmo.net.game.packets.ScriptWarpArrivedPacket
import de.fiereu.openmmo.pokemon.LearnsetRegistry
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.battle.StatCalculator
import de.fiereu.openmmo.server.game.session.CLIENT_RUNS_SCRIPTS
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.MONEY_MAX
import de.fiereu.openmmo.server.game.storage.SaveBlockRepository
import de.fiereu.openmmo.server.game.world.UndergroundExit
import de.fiereu.openmmo.server.game.world.UndergroundMap
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/**
 * The widest save block this server will store, matching the client's own
 * `MMO_SAVE_BLOCK_BYTES`.
 */
private const val MAX_SAVE_BLOCK_BYTES = 8192

/** The record of a cutscene the client ran. */
@Singleton
class LocalScriptService
@Inject
constructor(
    private val storyService: StoryService,
    private val characterStore: CharacterStore,
    private val mapManager: MapManager,
    private val presenceService: PresenceService,
    private val storyPlayerService: StoryPlayerService,
    private val learnsets: LearnsetRegistry,
    private val undergroundTalkService: UndergroundTalkService,
    private val items: ItemRegistry,
    private val species: SpeciesRegistry,
    private val budget: GrantBudget,
    private val saveBlockRepository: SaveBlockRepository,
) {

  /**
   * The client saying whether it runs the game's field scripts itself. It arrives once, in
   * front of the request that would start this map's entry script, so exactly one of the two
   * ever runs a scene.
   */
  fun onScriptOwnership(event: PacketEvent<ClientScriptOwnershipPacket>) {
    val runs = event.packet.clientRunsFieldScripts
    // Asked and answered once, in front of the request that starts the map's entry script.
    val already = event.session.attributes[CLIENT_RUNS_SCRIPTS]
    if (already != null) {
      if (already != runs) {
        log.warn {
          "Session already said its field scripts are ${owner(already)}; refusing the change" +
              " to ${owner(runs)}"
        }
      }
      return
    }
    event.session.attributes[CLIENT_RUNS_SCRIPTS] = runs
    log.debug { "Session says its field scripts are ${owner(runs)}" }
  }

  private fun owner(clientRuns: Boolean) = if (clientRuns) "its own" else "the server's"

  /** A report of what the client's VM wrote. Recorded, never answered. */
  suspend fun onScriptState(event: PacketEvent<ScriptStatePacket>) {
    val state = event.session.attributes[PLAYER_STATE] ?: return
    val characterId = state.characterId ?: return
    val regionId = state.regionId.toByte()
    val msg = event.packet

    for (flag in msg.flags) {
      val key = VmStoryKeys.flag(regionId, flag.id.toInt() and 0xFFFF)
      if (flag.on) storyService.setFlag(characterId, key)
      else storyService.clearFlag(characterId, key)
    }
    for (v in msg.vars) {
      storyService.setVar(
          characterId,
          VmStoryKeys.variable(regionId, v.id.toInt() and 0xFFFF),
          v.value.toInt() and 0xFFFF)
    }
    // Whole save blocks, recorded as the bytes they arrived as. Nothing here reads inside one
    // and nothing should: the block is the game's struct, and the id is the game's own save
    // table id.
    val blocks =
        msg.blocks
            .filter { it.data.isNotEmpty() && it.data.size <= MAX_SAVE_BLOCK_BYTES }
            .associate { it.id to it.data }
    if (blocks.size != msg.blocks.size) {
      log.warn {
        "char=$characterId sent ${msg.blocks.size - blocks.size} save block(s) this server will" +
            " not store (empty, or past $MAX_SAVE_BLOCK_BYTES bytes)"
      }
    }
    if (blocks.isNotEmpty()) saveBlockRepository.save(characterId, blocks)

    if (msg.flags.isNotEmpty() || msg.vars.isNotEmpty() || blocks.isNotEmpty()) {
      log.debug {
        "Character $characterId ran a local script: ${msg.flags.size} flag(s)," +
            " ${msg.vars.size} var(s), ${blocks.size} save block(s)"
      }
    }
  }

  /**
   * The client has already changed map. Move the record and the map group to where it says it
   * is.
   */
  fun onScriptWarpArrived(event: PacketEvent<ScriptWarpArrivedPacket>) {
    val session = event.session
    val state = session.attributes[PLAYER_STATE] ?: return
    val characterId = state.characterId ?: return
    val stored = characterStore.getCharacter(characterId) ?: return
    val msg = event.packet

    val bank = ((msg.mapHeaderId shr 8) and 0xFF).toByte()
    val map = (msg.mapHeaderId and 0xFF).toByte()
    val regionId = stored.info.positionRegionId
    val target = mapManager.getMap(regionId, bank, map)
    if (target == null) {
      log.warn {
        "Character $characterId reports a local warp to header ${msg.mapHeaderId}" +
            " ($regionId:$bank:$map), which this server has no map for"
      }
      return
    }
    if (msg.x < 0 || msg.x >= target.width || msg.y < 0 || msg.y >= target.height) {
      log.warn {
        "Character $characterId reports a local warp to (${msg.x}, ${msg.y}) on" +
            " $regionId:$bank:$map, which is ${target.width}x${target.height}"
      }
      return
    }

    val facing = Direction.entries.getOrNull(msg.direction) ?: stored.info.positionFacing
    val samePlace =
        stored.info.positionBankId == bank &&
            stored.info.positionMapId == map &&
            stored.info.positionX == msg.x &&
            stored.info.positionY == msg.y
    if (samePlace) return

    log.debug {
      "Character $characterId walked itself to $regionId:$bank:$map (${msg.x}, ${msg.y})"
    }
    // Descending is the one local warp whose origin the server has to keep: the way back up lives
    // in a save block the client rebuilds from nothing on its next boot, so a session that ends
    // underground would rejoin with no surface tile at all. UndergroundMap carries the reasoning.
    state.undergroundExit =
        if (UndergroundMap.isUnderground(regionId, bank, map)) {
          state.undergroundExit
              ?: UndergroundExit(
                  regionId = stored.info.positionRegionId,
                  bankId = stored.info.positionBankId,
                  mapId = stored.info.positionMapId,
                  x = stored.info.positionX,
                  y = stored.info.positionY,
                  facing = stored.info.positionFacing,
              )
        } else {
          // A conversation cannot outlive the cavern, and the other party's menu is waiting on a
          // reply that is not coming.
          undergroundTalkService.onLeftUnderground(characterId)
          null
        }
    presenceService.leave(session)
    characterStore.updateCharacter(
        stored.info.copy(
            positionBankId = bank,
            positionMapId = map,
            positionX = msg.x,
            positionY = msg.y,
            positionFacing = facing,
        ))
    characterStore.flushCharacterAsync(characterId)
    state.bankId = bank.toInt()
    state.mapId = map.toInt()
    state.x = msg.x
    state.y = msg.y
    state.elevation = target.tileAt(msg.x.toInt(), msg.y.toInt())?.elevation ?: 0
    state.facingDirection = facing
    // The client already asked for the destination's players by loading it; entering the new map
    // group is what makes this player visible to them.
    presenceService.enter(session)
  }

  /** What an engine-run battle left each party member as, recorded onto the stored monsters. */
  fun onBattleOutcome(event: PacketEvent<BattleOutcomePacket>) {
    val session = event.session
    if (session.attributes[CLIENT_RUNS_SCRIPTS] != true) return
    val state = session.attributes[PLAYER_STATE] ?: return
    val characterId = state.characterId ?: return
    val stored = characterStore.getCharacter(characterId) ?: return
    var written = 0
    // What each row said against what was held.
    val said = mutableListOf<String>()
    var moved = 0
    for (entry in event.packet.mons) {
      if (entry.level !in 1..100 || entry.xp < 0) continue
      val mon =
          stored.pokemon.firstOrNull { it.id == entry.id && it.container == PokemonContainer.PARTY }
              ?: continue
      // A fight is the one thing that moves these numbers, and it moves them one way.
      if (entry.level < mon.level || entry.xp < mon.xp) {
        log.warn {
          "char=$characterId reports monster ${entry.id} at level ${entry.level}/xp ${entry.xp}," +
              " below the stored ${mon.level}/${mon.xp}, refused"
        }
        continue
      }
      if (entry.level - mon.level > budget.levelsPerOutcome) {
        log.warn {
          "char=$characterId reports monster ${entry.id} jumping ${mon.level} -> ${entry.level}" +
              " in one battle, refused"
        }
        continue
      }
      val moves =
          entry.moves.filter { it.id != 0 }.map { PokemonMove(it.id.toShort(), it.pp.toByte()) }
      // The contest half of the same report. All three only ever go up: a Poffin adds to a
      // condition and a contest adds a ribbon, and nothing in the game takes either away.
      val conditions =
          ContestConditions(
              cool =
                  maxOf(mon.conditions.cool, entry.conditions.cool.coerceIn(0, MAX_CONTEST_STAT)),
              beauty =
                  maxOf(
                      mon.conditions.beauty, entry.conditions.beauty.coerceIn(0, MAX_CONTEST_STAT)),
              cute =
                  maxOf(mon.conditions.cute, entry.conditions.cute.coerceIn(0, MAX_CONTEST_STAT)),
              smart =
                  maxOf(mon.conditions.smart, entry.conditions.smart.coerceIn(0, MAX_CONTEST_STAT)),
              tough =
                  maxOf(mon.conditions.tough, entry.conditions.tough.coerceIn(0, MAX_CONTEST_STAT)),
          )
      val sheen = maxOf(mon.sheen, entry.sheen.coerceIn(0, MAX_CONTEST_STAT))
      // A contest awards exactly one ribbon, so exactly one new bit may appear. More than that is
      // not a contest that happened; the ribbon is the rank gate, so a client that could set the
      // mask outright could enter Master rank on a monster that has never competed.
      val newRibbons = entry.superContestRibbons and mon.superContestRibbons.inv()
      val superContestRibbons =
          if (java.lang.Long.bitCount(newRibbons) > 1) {
            log.warn {
              "char=$characterId reports monster ${entry.id} winning" +
                  " ${java.lang.Long.bitCount(newRibbons)} ribbons at once, none taken"
            }
            mon.superContestRibbons
          } else {
            mon.superContestRibbons or newRibbons
          }
      val grown =
          mon.copy(
              level = entry.level.toByte(),
              xp = entry.xp,
              moves = moves.ifEmpty { mon.moves },
              conditions = conditions,
              sheen = sheen,
              superContestRibbons = superContestRibbons,
          )
      // Hit points are the engine's arithmetic, but the ceiling is the server's stat calculation:
      // an hp past the maximum for the level being reported is a monster that cannot be knocked
      // out, which is not something a battle can produce.
      val maxHp = species.get(grown.dexId)?.let { StatCalculator.computeAll(it, grown).hp }
      val hp = entry.hp.coerceIn(0, maxHp ?: entry.hp.coerceAtLeast(0)).toShort()
      characterStore.updatePokemon(characterId, grown.copy(hp = hp))
      said +=
          "${entry.id} lv ${mon.level}->${entry.level} xp ${mon.xp}->${entry.xp}" +
              " hp ${mon.hp}->$hp pp ${mon.moves.joinToString(",") { "${it.pp}" }}" +
              "->${grown.moves.joinToString(",") { "${it.pp}" }}"
      if (entry.level.toByte() != mon.level ||
          entry.xp != mon.xp ||
          hp != mon.hp ||
          grown.moves != mon.moves ||
          grown.conditions != mon.conditions ||
          grown.sheen != mon.sheen ||
          grown.superContestRibbons != mon.superContestRibbons)
          moved++
      written++
    }
    if (written > 0) {
      // updatePokemon mutates the memory copy only; without the flush every outcome died with
      // the next server restart, and a rejoin regressed the party to its last durable state.
      characterStore.flushCharacterAsync(characterId)
      // The client rebuilds its fighting party from the container it was last sent, so the
      // recorded outcome has to travel back or the next battle is seated from stale records
      // and this one's gains regress.
      characterStore.getCharacter(characterId)?.let { fresh ->
        session.send(
            PokemonContainerPacket(
                container = PokemonContainer.PARTY,
                hasChange = true,
                delete = false,
                pokemon = fresh.pokemon.filter { it.container == PokemonContainer.PARTY },
            ))
      }
      log.info {
        "Character $characterId recorded a battle outcome for $written monster(s)" +
            (if (moved == 0) ", nothing moved" else "") +
            ": ${said.joinToString("; ")}"
      }
    }
  }

  /**
   * The engine consumed or gained an item, a potion drunk, a ball thrown, an item tossed, a
   * script's gift, through a bag this session keeps as a display of ours.
   */
  suspend fun onBagDelta(event: PacketEvent<BagDeltaPacket>) {
    val session = event.session
    if (session.attributes[CLIENT_RUNS_SCRIPTS] != true) return
    val state = session.attributes[PLAYER_STATE] ?: return
    val characterId = state.characterId ?: return
    val msg = event.packet
    if (msg.delta == 0 || msg.delta !in -99..99) return
    // An id no registry knows is a row the bag would carry and no screen could name. It is also
    // what a client sending numbers rather than items looks like.
    if (items.get(msg.itemId) == null) {
      log.warn { "Character $characterId reports item ${msg.itemId}, which is not an item" }
      return
    }
    if (!budget.allow(characterId, GrantBudget.Kind.ITEMS, msg.delta)) return
    if (!characterStore.addItem(characterId, msg.itemId, msg.delta)) {
      log.warn {
        "Character $characterId reports item ${msg.itemId} x${msg.delta} the bag cannot take"
      }
      return
    }
    log.info {
      "Character $characterId ${if (msg.delta < 0) "consumed" else "gained"} item ${msg.itemId} x${msg.delta}"
    }
    characterStore.getCharacter(characterId)?.let { fresh ->
      session.send(storyItemStacksPacket(fresh.items))
    }
  }

  /** The bag screen registered a key item to Y, or cleared it. */
  fun onRegisteredItem(event: PacketEvent<RegisteredItemPacket>) {
    val session = event.session
    if (session.attributes[CLIENT_RUNS_SCRIPTS] != true) return
    val characterId = session.attributes[PLAYER_STATE]?.characterId ?: return
    val itemId = event.packet.itemId
    if (itemId != 0) {
      if (itemId !in KEY_ITEM_IDS) {
        log.warn { "Character $characterId registers item $itemId, which is not a key item" }
        return
      }
      if ((characterStore.getCharacter(characterId)?.items?.get(itemId) ?: 0) < 1) {
        log.warn { "Character $characterId registers item $itemId, which they do not hold" }
        return
      }
    }
    characterStore.setRegisteredItem(characterId, itemId.toShort())
  }

  /**
   * The box screen let a monster go. Removed from whichever container holds it; refused when
   * it would empty the party, or names a monster this character does not own, in either case
   * the containers are resent so the screen snaps back to the record.
   */
  suspend fun onPokemonRelease(event: PacketEvent<PokemonReleasePacket>) {
    val session = event.session
    if (session.attributes[CLIENT_RUNS_SCRIPTS] != true) return
    val state = session.attributes[PLAYER_STATE] ?: return
    val characterId = state.characterId ?: return
    val released = characterStore.releasePokemon(characterId, event.packet.monsterId)
    if (released) {
      log.info { "Character $characterId released monster ${event.packet.monsterId}" }
    } else {
      log.warn {
        "Character $characterId asked to release monster ${event.packet.monsterId}," +
            " which is refused"
      }
    }
    characterStore.getCharacter(characterId)?.let { fresh ->
      session.send(
          PokemonContainerPacket(
              container = PokemonContainer.PARTY,
              hasChange = true,
              delete = false,
              pokemon = fresh.pokemon))
      session.send(
          PokemonContainerPacket(
              container = PokemonContainer.PC,
              hasChange = true,
              delete = false,
              pokemon = fresh.pcStorage))
    }
  }

  /**
   * The engine spent or earned cash, a mart purchase or sale through the engine's own shop, a
   * script's reward, against a wallet this session keeps as a display of ours.
   */
  suspend fun onMoneyDelta(event: PacketEvent<MoneyDeltaPacket>) {
    val session = event.session
    if (session.attributes[CLIENT_RUNS_SCRIPTS] != true) return
    val state = session.attributes[PLAYER_STATE] ?: return
    val characterId = state.characterId ?: return
    val msg = event.packet
    if (msg.delta == 0 || msg.delta !in -MONEY_MAX..MONEY_MAX) return
    if (!budget.allow(characterId, GrantBudget.Kind.MONEY, msg.delta)) return
    // Through the store's own delta, never a total computed from a balance read here first: a
    // stale total lands on top of whatever was spent in between and pays the spend back, which is
    // a free guild, a free mart run, and a wallet that grows by standing still.
    if (!characterStore.addMoney(characterId, msg.delta)) {
      log.warn { "Character $characterId reports spending ${-msg.delta} it does not have" }
      characterStore.getCharacter(characterId)?.let {
        session.send(LocalCharacterDeltaPacket(money = it.info.money))
      }
      return
    }
    val after = characterStore.getCharacter(characterId)?.info?.money ?: return
    log.info {
      "Character $characterId ${if (msg.delta < 0) "spent" else "earned"} ${
        if (msg.delta < 0) -msg.delta else msg.delta
      }, now $after"
    }
    session.send(LocalCharacterDeltaPacket(money = after))
  }

  /**
   * A local script's `GivePokemon` succeeded, and the engine's party already carries the
   * monster.
   */
  suspend fun onScriptGrant(event: PacketEvent<ScriptGrantPacket>) {
    val session = event.session
    if (session.attributes[CLIENT_RUNS_SCRIPTS] != true) return
    val state = session.attributes[PLAYER_STATE] ?: return
    val characterId = state.characterId ?: return
    val msg = event.packet
    if (msg.level !in 1..100) return
    if (!budget.allow(characterId, GrantBudget.Kind.MONSTERS, 1)) {
      // The report is the only place this monster exists, so a refusal is a monster the player
      // caught and will not find. Said out loud rather than dropped in silence.
      log.warn {
        "char=$characterId is past its monster allowance; the grant of dex ${msg.dexId}" +
            " lv ${msg.level} was refused"
      }
      return
    }
    // A report of an individual the character already owns is the same catch told twice, not a
    // second monster: the engine rolls a fresh personality per capture, so a matching seed on
    // a matching species is a duplicate report (a client whose box sync re-swept an unanswered
    // grant did exactly that, one copy per following battle).
    if (msg.seed != 0) {
      val stored = characterStore.getCharacter(characterId)
      val twin =
          (stored?.pokemon.orEmpty() + stored?.pcStorage.orEmpty()).firstOrNull {
            it.seed == msg.seed && it.dexId == msg.dexId
          }
      if (twin != null) {
        log.warn {
          "char=$characterId re-reported the grant of dex ${msg.dexId} seed ${msg.seed}" +
              " (already monster ${twin.id}); refused"
        }
        session.send(
            PokemonContainerPacket(
                container = twin.container,
                hasChange = true,
                delete = false,
                pokemon =
                    (if (twin.container == PokemonContainer.PC) stored?.pcStorage
                        else stored?.pokemon)
                        .orEmpty(),
            ))
        return
      }
    }
    val moves = learnsets.initialMoveset(msg.dexId, msg.level).ifEmpty { listOf(TACKLE_ID) }
    val container = if (msg.container == 0) PokemonContainer.PC else PokemonContainer.PARTY
    val granted =
        storyPlayerService.givePokemon(
            session,
            state,
            msg.dexId,
            msg.level,
            moves,
            StoryPlayerService.Grant(
                msg.nickname.trim().take(10),
                msg.hp,
                container,
                msg.slot,
                // A seed of zero is a report with no individual behind it; the server rolls one.
                if (msg.seed != 0) StoryPlayerService.Individual(msg.seed, msg.ivBits, msg.isShiny)
                else null,
            ))
    if (granted == null) {
      log.warn {
        "Character ${state.characterId} claims a scripted grant of dex ${msg.dexId}" +
            " lv ${msg.level} that could not be granted"
      }
    } else {
      log.info {
        "Character ${state.characterId} was granted dex ${msg.dexId} lv ${msg.level} by a script"
      }
    }
  }
}

// The fallback a species with no learnset entry can still attack with.
private const val TACKLE_ID = 33

/** The key a local-VM flag or var is stored under: `region/vm/flag/N` and `region/vm/var/N`. */
internal object VmStoryKeys {
  fun prefix(regionId: Byte): String =
      (Region.byWireValue(regionId)?.name?.lowercase() ?: "") + "/vm/"

  fun flag(regionId: Byte, id: Int): String = "${prefix(regionId)}flag/$id"

  fun variable(regionId: Byte, id: Int): String = "${prefix(regionId)}var/$id"

  /** The numeric flag id in [key], or null if [key] is not one of ours for this region. */
  fun flagId(regionId: Byte, key: String): Int? = idUnder(key, "${prefix(regionId)}flag/")

  /** The numeric var id in [key], or null if [key] is not one of ours for this region. */
  fun varId(regionId: Byte, key: String): Int? = idUnder(key, "${prefix(regionId)}var/")

  private fun idUnder(key: String, prefix: String): Int? =
      if (key.startsWith(prefix)) key.substring(prefix.length).toIntOrNull() else null
}
