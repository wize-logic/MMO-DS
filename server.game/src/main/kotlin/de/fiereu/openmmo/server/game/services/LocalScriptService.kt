package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.ContestConditions
import de.fiereu.openmmo.common.ContestRank
import de.fiereu.openmmo.common.ContestType
import de.fiereu.openmmo.common.MAX_CONTEST_STAT
import de.fiereu.openmmo.common.MAX_PARTY_SIZE
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.GrowthRate
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.items.ItemRegistry
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.moves.MoveRegistry
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
import de.fiereu.openmmo.server.game.battle.ExpCurves
import de.fiereu.openmmo.server.game.battle.StatCalculator
import de.fiereu.openmmo.server.game.session.CLIENT_RUNS_SCRIPTS
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.MONEY_MAX
import de.fiereu.openmmo.server.game.storage.SaveBlockRepository
import de.fiereu.openmmo.server.game.world.UndergroundExit
import de.fiereu.openmmo.server.game.world.UndergroundMap
import de.fiereu.openmmo.server.game.world.WarpNeighbours
import de.fiereu.openmmo.server.game.world.localMapKey
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/**
 * The widest save block this server will store, matching the client's own `MMO_SAVE_BLOCK_BYTES`.
 */
private const val MAX_SAVE_BLOCK_BYTES = 8192

/** Distinct save block ids one character may hold. The client keeps seven. */
private const val MAX_SAVE_BLOCK_IDS = 16

/**
 * What one script-state report may carry, which is what the client's own reporter sends. It diffs
 * the engine's flag and var block and stops at these, leaving the rest for its next report, so
 * anything wider did not come from it. The codec would take about 21,000 flags in one frame.
 */
private const val MAX_FLAGS_PER_REPORT = 192
private const val MAX_VARS_PER_REPORT = 64
private const val MAX_BLOCKS_PER_REPORT = 8

/** The most one report may raise a contest condition by. A Poffin is worth a few dozen. */
private const val MAX_CONDITION_GAIN_PER_REPORT = 60

/** The twenty bits a ribbon mask can hold: four ranks for each of five types. */
private val SUPER_CONTEST_RIBBON_MASK: Long =
    (1L shl (ContestType.entries.size * ContestRank.entries.size)) - 1

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
    private val moveRegistry: MoveRegistry,
    private val reportedIndividual: ReportedIndividual,
    private val ribbonCredits: ContestRibbonCredits,
    private val violations: ViolationLog,
    private val warpNeighbours: WarpNeighbours,
    private val saveBlockRepository: SaveBlockRepository,
) {

  /**
   * How often a reported battle may have paid out. A quarter of a token a second is one battle
   * every four seconds, faster than any fight here reaches its last turn, and the burst covers a
   * session catching up. Per report rather than per monster, so a whole party growing out of one
   * fight costs one.
   */
  private val outcomePace = PaceLimit(burst = 10.0, perSecond = 0.25)

  /**
   * The client saying whether it runs the game's field scripts itself. It arrives once, in front of
   * the request that would start this map's entry script, so exactly one of the two ever runs a
   * scene.
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
    // The guard every other handler here opens with, and the one place it was missing. From a
    // session that does not run the scenes there is no scene behind the report.
    if (event.session.attributes[CLIENT_RUNS_SCRIPTS] != true) return
    val state = event.session.attributes[PLAYER_STATE] ?: return
    val characterId = state.characterId ?: return
    val regionId = state.regionId.toByte()
    val msg = event.packet

    // A report wider than the reporter can build is refused whole rather than trimmed.
    if (msg.flags.size > MAX_FLAGS_PER_REPORT ||
        msg.vars.size > MAX_VARS_PER_REPORT ||
        msg.blocks.size > MAX_BLOCKS_PER_REPORT) {
      log.warn {
        "char=$characterId reported ${msg.flags.size} flag(s), ${msg.vars.size} var(s) and" +
            " ${msg.blocks.size} block(s), past what a client sends, refused"
      }
      return
    }
    // Every row is a write to this character's story rows, so the count is charged before the
    // first one lands and a refused report writes nothing.
    // Blocks are counted with the rows. A block is the widest write in the report, and counting
    // only flags and vars meant a report carrying nothing else spent nothing.
    val rowsClaimed = msg.flags.size + msg.vars.size + msg.blocks.size
    if (!budget.allow(characterId, GrantBudget.Kind.STORY_WRITES, rowsClaimed)) {
      violations.record(
          characterId,
          ViolationLog.Kind.PAST_ALLOWANCE,
          "reported $rowsClaimed story rows, past the window's allowance")
      return
    }

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
    // A block id is the game's own save table id and the client keeps seven, but nothing checked
    // that, so a report naming any id in the field grew a row per id. Eight a report and the
    // story allowance bound the rate; this bounds the total. The ids themselves are not mirrored
    // here on purpose: that would be a second copy of the engine's enum to keep true.
    val known = saveBlockRepository.load(characterId)
    val fresh = blocks.keys.count { it !in known }
    if (fresh > 0 && known.size + fresh > MAX_SAVE_BLOCK_IDS) {
      violations.record(
          characterId,
          ViolationLog.Kind.PAST_ALLOWANCE,
          "already holds ${known.size} save blocks and reports $fresh more ids," +
              " past the $MAX_SAVE_BLOCK_IDS a client keeps")
      return
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
   * The client has already changed map. Move the record and the map group to where it says it is.
   */
  fun onScriptWarpArrived(event: PacketEvent<ScriptWarpArrivedPacket>) {
    val session = event.session
    // Only a session that said it runs the scenes may report having walked itself somewhere. This
    // was the one handler in the file without the check every other one opens with.
    if (session.attributes[CLIENT_RUNS_SCRIPTS] != true) return
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
    // Inside the map is not the same as somewhere a player can be, and the position is stored, so
    // a tile that blocks movement is one nobody can walk back out of.
    if (target.tileAt(msg.x.toInt(), msg.y.toInt())?.blocksMovement() == true) {
      log.warn {
        "Character $characterId reports a local warp onto (${msg.x}, ${msg.y}) of" +
            " $regionId:$bank:$map, which nothing can stand on"
      }
      return
    }
    // The checks above ask whether the destination is a real place. None asked whether it is one
    // this player could have got to, and the region is the whole game, so the report was a
    // teleport anywhere in Sinnoh. [WarpNeighbours] reads the warp graph the map data already
    // describes: one hop from the map they were on, that map, a Pokemon Centre for a blackout, or
    // their own dynamic warp.
    val cameFrom = stored.info
    val destination = localMapKey(bank.toInt(), map.toInt())
    val reachable =
        warpNeighbours.oneHopFrom(
            regionId.toInt(), cameFrom.positionBankId.toInt(), cameFrom.positionMapId.toInt())
    val dynamic = cameFrom.dynamicWarp
    val dynamicAllows =
        dynamic != null &&
            dynamic.regionId == regionId &&
            localMapKey(dynamic.bankId.toInt(), dynamic.mapId.toInt()) == destination
    if (destination !in reachable && !dynamicAllows) {
      violations.record(
          characterId,
          ViolationLog.Kind.IMPOSSIBLE_POSITION,
          "reports walking itself to $regionId:$bank:$map, which is not one warp from" +
              " ${cameFrom.positionBankId}:${cameFrom.positionMapId}")
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
    // One row per monster. A party holds six, so a report naming the same one many times is not
    // many battles.
    val rows = event.packet.mons.distinctBy { it.id }.take(MAX_PARTY_SIZE)
    // A fight takes time and nothing here knew that. A ceiling bounded one report and a window
    // bounded a minute of them, but nothing put a floor under how often a battle can end. One
    // token per report that claims growth; a report that changes nothing is free, which is what a
    // closed menu sends.
    val claimsGrowth =
        rows.any { entry ->
          val mon = stored.pokemon.firstOrNull { it.id == entry.id } ?: return@any false
          entry.level > mon.level || entry.xp > mon.xp
        }
    if (claimsGrowth && !outcomePace.allow(characterId)) {
      violations.record(
          characterId,
          ViolationLog.Kind.IMPOSSIBLE_PACE,
          "reports a battle ending sooner than one can be fought")
      return
    }
    for (entry in rows) {
      if (entry.level !in 1..ExpCurves.MAX_LEVEL || entry.xp < 0) continue
      val mon =
          stored.pokemon.firstOrNull { it.id == entry.id && it.container == PokemonContainer.PARTY }
              ?: continue
      val definition = species.get(mon.dexId)
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
      // The cap above bounds one report, and reports are free. Ten levels a packet at line rate
      // is a level 100 party in a second, so the levels spend from a window too. A refusal leaves
      // the monster where the record has it and lets the rest of the row through.
      val levelGain = entry.level - mon.level
      val growthAllowed =
          levelGain <= 0 || budget.allow(characterId, GrantBudget.Kind.LEVELS, levelGain)
      val level = if (growthAllowed) entry.level else mon.level.toInt()
      // Level and experience are the same fact told twice, and only the level was bounded. A row
      // could sit still and put two billion experience behind it, which the next server-run battle
      // reads back as level 100. Past the band the level names it is clamped, not refused.
      val ceiling = definition?.let { xpCeilingFor(it.growthRate, level) }
      val xp = if (growthAllowed) entry.xp else mon.xp
      val cappedXp = if (ceiling != null) xp.coerceAtMost(ceiling) else xp
      if (ceiling != null && xp > ceiling) {
        log.warn {
          "char=$characterId reports monster ${entry.id} at level $level holding $xp xp," +
              " past the $ceiling that level can hold, clamped"
        }
      }
      // A reported moveset was taken whole, and the in-battle gate decides what a monster may
      // use by reading the stored one, so a report could write an unlearnable move in and make
      // every later check agree with it. What a fight can add is what the species learns by level
      // up at or below the level reported; whatever is already on the record stays allowed.
      val learnable =
          (mon.moves.map { it.id.toInt() } + learnsets.movesAt(mon.dexId, level)).toSet()
      val reportedMoves = entry.moves.filter { it.id != 0 }
      val unlearnable =
          reportedMoves.filter { it.id !in learnable || moveRegistry.get(it.id) == null }
      val moves =
          if (unlearnable.isEmpty()) {
            reportedMoves.map {
              val maxPp = moveRegistry.get(it.id)?.pp ?: it.pp
              PokemonMove(it.id.toShort(), it.pp.coerceIn(0, maxPp).toByte())
            }
          } else {
            violations.record(
                characterId,
                ViolationLog.Kind.ILLEGAL_MOVESET,
                "reports monster ${entry.id} (dex ${mon.dexId}) knowing" +
                    " ${unlearnable.joinToString { "${it.id}" }}, which nothing at level $level" +
                    " could have taught it; the stored moveset stands")
            emptyList()
          }
      // The contest half of the same report. All three only ever go up: a Poffin adds to a
      // condition and a contest adds a ribbon, and nothing in the game takes either away. Up was
      // unbounded, and one packet took every condition to the ceiling. A contest is scored against
      // other players, so a report now carries a Poffin's worth and a window's worth in total.
      val raised =
          ContestConditions(
              cool = raise(mon.conditions.cool, entry.conditions.cool),
              beauty = raise(mon.conditions.beauty, entry.conditions.beauty),
              cute = raise(mon.conditions.cute, entry.conditions.cute),
              smart = raise(mon.conditions.smart, entry.conditions.smart),
              tough = raise(mon.conditions.tough, entry.conditions.tough),
          )
      val raisedSheen = raise(mon.sheen, entry.sheen)
      val points =
          ContestType.entries.sumOf { raised[it] - mon.conditions[it] } + (raisedSheen - mon.sheen)
      val takeContest =
          points <= 0 || budget.allow(characterId, GrantBudget.Kind.CONTEST_POINTS, points)
      val conditions = if (takeContest) raised else mon.conditions
      val sheen = if (takeContest) raisedSheen else mon.sheen
      // A contest awards exactly one ribbon, so exactly one new bit may appear. More than that is
      // not a contest that happened; the ribbon is the rank gate, so a client that could set the
      // mask outright could enter Master rank on a monster that has never competed. Bits that name
      // no contest are dropped first, and a win spends from the window, since one at a time was
      // still a full set in twenty packets.
      val claimed = entry.superContestRibbons and SUPER_CONTEST_RIBBON_MASK
      val newRibbons = claimed and mon.superContestRibbons.inv()
      val superContestRibbons =
          when {
            java.lang.Long.bitCount(newRibbons) > 1 -> {
              log.warn {
                "char=$characterId reports monster ${entry.id} winning" +
                    " ${java.lang.Long.bitCount(newRibbons)} ribbons at once, none taken"
              }
              mon.superContestRibbons
            }
            newRibbons == 0L -> mon.superContestRibbons
            !budget.allow(characterId, GrantBudget.Kind.RIBBONS, 1) -> mon.superContestRibbons
            // A rate said how fast the set could be claimed and never whether a contest happened.
            // [ContestService] settles one by agreement between every seat, which is the only
            // evidence there is, and leaves a credit here. A ribbon spends one.
            !ribbonCredits.spend(characterId) -> {
              violations.record(
                  characterId,
                  ViolationLog.Kind.IMPOSSIBLE_MONSTER,
                  "reports a Super Contest ribbon on monster ${entry.id} with no contest settled" +
                      " to pay for it")
              mon.superContestRibbons
            }
            else -> mon.superContestRibbons or newRibbons
          }
      val grown =
          mon.copy(
              level = level.toByte(),
              xp = cappedXp,
              moves = moves.ifEmpty { mon.moves },
              conditions = conditions,
              sheen = sheen,
              superContestRibbons = superContestRibbons,
          )
      // Hit points are the engine's arithmetic, but the ceiling is the server's stat calculation:
      // an hp past the maximum for the level being reported is a monster that cannot be knocked
      // out, which is not something a battle can produce.
      val maxHp = definition?.let { StatCalculator.computeAll(it, grown).hp }
      val hp = entry.hp.coerceIn(0, maxHp ?: entry.hp.coerceAtLeast(0)).toShort()
      characterStore.updatePokemon(characterId, grown.copy(hp = hp))
      said +=
          "${entry.id} lv ${mon.level}->$level xp ${mon.xp}->$cappedXp" +
              " hp ${mon.hp}->$hp pp ${mon.moves.joinToString(",") { "${it.pp}" }}" +
              "->${grown.moves.joinToString(",") { "${it.pp}" }}"
      if (level.toByte() != mon.level ||
          cappedXp != mon.xp ||
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

  /** One condition as the report leaves it: never below what is stored, never above the ceiling. */
  private fun raise(stored: Int, reported: Int): Int =
      maxOf(
          stored,
          reported
              .coerceIn(0, MAX_CONTEST_STAT)
              .coerceAtMost(stored + MAX_CONDITION_GAIN_PER_REPORT))

  /** The most experience a monster at [level] can hold on this curve. */
  private fun xpCeilingFor(rate: GrowthRate, level: Int): Int {
    // The curve refuses a level outside its range, and a throw here disconnects the sender.
    val safe = level.coerceIn(1, ExpCurves.MAX_LEVEL)
    return if (safe >= ExpCurves.MAX_LEVEL) ExpCurves.totalXpFor(rate, ExpCurves.MAX_LEVEL)
    else ExpCurves.totalXpFor(rate, safe + 1) - 1
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
    // A key item is story progress and one-per-save equipment, which is why the broker and the
    // register wall them off. This wire cannot, because a scene really does hand one over and this
    // report is how it arrives. What is true of all of them is that nobody is given one twice.
    if (msg.itemId in KEY_ITEM_IDS && msg.delta > 0) {
      val held = characterStore.getCharacter(characterId)?.items?.get(msg.itemId) ?: 0
      if (msg.delta > 1 || held >= 1) {
        violations.record(
            characterId,
            ViolationLog.Kind.FORBIDDEN_ITEM,
            "reports gaining key item ${msg.itemId} x${msg.delta} while holding $held")
        return
      }
    }
    if (!budget.allow(characterId, GrantBudget.Kind.ITEMS, msg.delta)) {
      violations.record(
          characterId,
          ViolationLog.Kind.PAST_ALLOWANCE,
          "reports gaining item ${msg.itemId} x${msg.delta} past the window's allowance")
      return
    }
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
   * The box screen let a monster go. Removed from whichever container holds it; refused when it
   * would empty the party, or names a monster this character does not own, in either case the
   * containers are resent so the screen snaps back to the record.
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
   * A local script's `GivePokemon` succeeded, and the engine's party already carries the monster.
   */
  suspend fun onScriptGrant(event: PacketEvent<ScriptGrantPacket>) {
    val session = event.session
    if (session.attributes[CLIENT_RUNS_SCRIPTS] != true) return
    val state = session.attributes[PLAYER_STATE] ?: return
    val characterId = state.characterId ?: return
    val msg = event.packet
    if (msg.level !in 1..100) return
    if (!budget.allow(characterId, GrantBudget.Kind.MONSTERS, 1) ||
        !budget.allow(characterId, GrantBudget.Kind.MONSTERS_HOURLY, 1)) {
      violations.record(
          characterId,
          ViolationLog.Kind.PAST_ALLOWANCE,
          "is past its monster allowance; the grant of dex ${msg.dexId} lv ${msg.level} was" +
              " refused")
      return
    }
    // What the client sends about the individual is a name for the capture, not the capture.
    // [ReportedIndividual] derives the monster behind it from that name, this character, the
    // species and the server's secret, so the same name always means the same monster and the
    // client cannot pick what is in it.
    val rolled =
        if (msg.seed != 0) reportedIndividual.forToken(characterId, msg.dexId, msg.seed) else null
    // A report of an individual the character already owns is the same catch told twice, not a
    // second monster: the engine rolls a fresh personality per capture, so a matching seed on
    // a matching species is a duplicate report (a client whose box sync re-swept an unanswered
    // grant did exactly that, one copy per following battle).
    if (rolled != null) {
      val stored = characterStore.getCharacter(characterId)
      val twin =
          (stored?.pokemon.orEmpty() + stored?.pcStorage.orEmpty()).firstOrNull {
            it.seed == rolled.seed && it.dexId == msg.dexId
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
    // Shininess used to be the client's word with no way to check it. It is the server's own draw
    // now, at the odds the games use, so a shiny that gets here is real and is never downgraded.
    // The count stays because the derivation leaves one way to hunt: take a monster, look at it,
    // let it go, report a different name. That costs a slot from the monster allowance each time.
    val shiny = rolled?.isShiny == true
    if (shiny && !budget.allow(characterId, GrantBudget.Kind.SHINY, 1)) {
      violations.record(
          characterId,
          ViolationLog.Kind.PAST_ALLOWANCE,
          "has been handed more shinies than an hour of luck gives (dex ${msg.dexId})")
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
                // A name of zero is a report with no capture behind it; the wild roll stands.
                rolled?.let { StoryPlayerService.Individual(it.seed, it.ivBits, shiny) },
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
