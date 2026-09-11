package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.ContestConditions
import de.fiereu.openmmo.common.ContestRank
import de.fiereu.openmmo.common.ContestType
import de.fiereu.openmmo.common.MAX_CONTEST_STAT
import de.fiereu.openmmo.common.MAX_FRIENDSHIP
import de.fiereu.openmmo.common.MAX_PARTY_SIZE
import de.fiereu.openmmo.common.MON_STATUS_MASK
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.GrowthRate
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.items.ItemRegistry
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.maps.generated.sinnoh.SpawnLocations
import de.fiereu.openmmo.moves.MoveRegistry
import de.fiereu.openmmo.net.game.packets.BagDeltaPacket
import de.fiereu.openmmo.net.game.packets.BattleOutcomeMon
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
import de.fiereu.openmmo.pokemon.EvolutionRegistry
import de.fiereu.openmmo.pokemon.LearnsetRegistry
import de.fiereu.openmmo.pokemon.MoveSourceRegistry
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.battle.BattleRegistry
import de.fiereu.openmmo.server.game.battle.ExpCurves
import de.fiereu.openmmo.server.game.battle.StatCalculator
import de.fiereu.openmmo.server.game.offline.verify.ReplayVerdict
import de.fiereu.openmmo.server.game.session.CLIENT_RUNS_SCRIPTS
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.setMapAddress
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.ImportRepository
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

/** What one script-state report may carry, which is what the client's own reporter will send. */
private const val MAX_FLAGS_PER_REPORT = 192
private const val MAX_VARS_PER_REPORT = 64
private const val MAX_BLOCKS_PER_REPORT = 16

/** The most one battle outcome may raise a single contest condition by. */
private const val MAX_CONDITION_GAIN_PER_REPORT = 60

/**
 * The twenty bits a Super Contest ribbon mask can legally hold: four ranks for each of five types,
 * exactly as [superContestRibbonBit] lays them out.
 */
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
    private val moveSources: MoveSourceRegistry,
    private val undergroundTalkService: UndergroundTalkService,
    private val items: ItemRegistry,
    private val species: SpeciesRegistry,
    private val evolutions: EvolutionRegistry,
    private val budget: GrantBudget,
    private val moveRegistry: MoveRegistry,
    private val reportedIndividual: ReportedIndividual,
    private val ribbonCredits: ContestRibbonCredits,
    private val violations: ViolationLog,
    private val warpNeighbours: WarpNeighbours,
    private val saveBlockRepository: SaveBlockRepository,
    private val fieldMoveService: FieldMoveService,
    private val battles: BattleRegistry,
    private val staticEncounters: StaticEncounterService,
    private val importRecords: ImportRepository,
) {

  /** How often a reported battle may have paid out. */
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
    // The guard every other handler in this file opens with, and the one place it was missing. A
    // report only means anything from a session that said it runs the scenes; from one that did
    // not there is no scene behind it, and the rows would be written anyway.
    if (event.session.attributes[CLIENT_RUNS_SCRIPTS] != true) return
    val state = event.session.attributes[PLAYER_STATE] ?: return
    val characterId = state.characterId ?: return
    // These rows are keyed under the character's region, never the region the session's avatar
    // happens to be standing in.
    val stored = characterStore.getCharacter(characterId)
    if (stored == null) {
      // A disconnect evicts the character while its last packets are still draining.
      log.warn { "char=$characterId reported a script state but is no longer loaded; dropped" }
      return
    }
    val regionId = stored.info.positionRegionId
    val msg = event.packet

    // A report wider than the reporter can build is not one, so it is refused whole rather than
    // trimmed: a client sending twenty thousand flags is not a scene whose first two hundred are
    // worth keeping. The ceilings are the client's own (see the constants above).
    if (msg.flags.size > MAX_FLAGS_PER_REPORT ||
        msg.vars.size > MAX_VARS_PER_REPORT ||
        msg.blocks.size > MAX_BLOCKS_PER_REPORT) {
      log.warn {
        "char=$characterId reported ${msg.flags.size} flag(s), ${msg.vars.size} var(s) and" +
            " ${msg.blocks.size} block(s), past what a client sends" +
            " ($MAX_FLAGS_PER_REPORT/$MAX_VARS_PER_REPORT/$MAX_BLOCKS_PER_REPORT), refused"
      }
      return
    }
    // Each row is a write to this character's story rows, and one of them is now load bearing:
    // a Fly destination is gated on the row the engine sets when the player first stands in a
    // town (FieldMoveService.canFlyTo).
    val rowsClaimed = msg.flags.size + msg.vars.size + msg.blocks.size
    if (!budget.allow(characterId, GrantBudget.Kind.STORY_WRITES, rowsClaimed)) {
      violations.record(
          characterId,
          ViolationLog.Kind.PAST_ALLOWANCE,
          "reported $rowsClaimed story rows, past the window's allowance")
      return
    }

    for (flag in msg.flags) {
      val id = flag.id.toInt() and 0xFFFF
      val badge = SyntheticRows.badgeOf(id)
      if (badge != null) {
        // A badge is the character's, held under its own key (Badge) where the trainer card, the
        // HM gates and the offline import read it, not as a vm number. Monotone: the engine never
        // takes one away, so an off row is a fresh save's zero rather than a fact.
        if (flag.on) {
          storyService.setFlag(characterId, badge.key)
          log.info { "char=$characterId earned the ${badge.name} badge" }
        }
        continue
      }
      val key = VmStoryKeys.flag(regionId, id)
      if (flag.on) storyService.setFlag(characterId, key)
      else storyService.clearFlag(characterId, key)
    }
    for (v in msg.vars) {
      val id = v.id.toInt() and 0xFFFF
      if (id == SyntheticRows.RESPAWN) {
        // The engine's black-out warp id, kept as the heal location the server-run white out and
        // the one-hop check read. A number off the spawn table is nothing to store.
        SpawnLocations.healLocation(v.value.toInt() and 0xFFFF)?.let {
          characterStore.setHealLocation(characterId, it)
        }
        continue
      }
      storyService.setVar(
          characterId, VmStoryKeys.variable(regionId, id), v.value.toInt() and 0xFFFF)
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
    // A block id is the game's own save table id and the client keeps fifteen of them, but
    // nothing here checked that, so a report could name any id in the field and each one
    // became a row.
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
    // was the one handler in the file without the check every other one opens with, so any client
    // at all could name a destination and be moved to it.
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
    // Inside the map is not the same as somewhere a player can be. A scene never leaves anyone
    // standing in collision, and a tile that blocks movement is the one destination that cannot be
    // walked back out of: the position is stored, so the next login puts them there again.
    if (target.tileAt(msg.x.toInt(), msg.y.toInt())?.blocksMovement() == true &&
        target.warps.none { it.x == msg.x.toInt() && it.y == msg.y.toInt() }) {
      // A door mat is the one blocked tile a warp legitimately leaves a player on, and the one
      // they can always walk back out of, which is what the refusal below is really guarding.
      // Cherrygrove's Center door was the first ported mat to arrive here (2026-08-31).
      log.warn {
        "Character $characterId reports a local warp onto (${msg.x}, ${msg.y}) of" +
            " $regionId:$bank:$map, which nothing can stand on"
      }
      return
    }
    // The three checks above all ask whether the destination is a real place.
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
    // Fly is the fifth case and the only one the graph above cannot describe, because it
    // crosses the world with no warp tile behind it.
    val flyId = fieldMoveService.flyLanding(msg.mapHeaderId, msg.x.toInt(), msg.y.toInt())
    val flyAllows =
        flyId != null &&
            fieldMoveService.canFlyTo(
                stored,
                mapManager.getMap(regionId, cameFrom.positionBankId, cameFrom.positionMapId),
                flyId)
    if (destination !in reachable && !dynamicAllows && !flyAllows) {
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
      "Character $characterId ${if (flyAllows) "flew" else "walked"} itself to" +
          " $regionId:$bank:$map (${msg.x}, ${msg.y})"
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
    // Strength and this visit's boulder positions die with the map, the way the decomp clears
    // FLAG_STRENGTH_ACTIVE in field_map_change_flags.c.
    if (stored.info.positionBankId != bank || stored.info.positionMapId != map) {
      fieldMoveService.onMapChange(session, state)
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
    state.setMapAddress(state.regionId, bank.toInt(), map.toInt())
    state.x = msg.x
    state.y = msg.y
    state.elevation = target.tileAt(msg.x.toInt(), msg.y.toInt())?.elevation ?: 0
    state.facingDirection = facing
    // Surf is a scene like any other: the engine runs the script, mounts its own avatar and
    // rides it onto the water, and this report is the only thing the server hears about it.
    fieldMoveService.syncMountToTile(session, state)
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
    // One row per monster. A party holds six, so a report naming the same one two hundred times is
    // not two hundred battles; every row past the first for a monster is dropped before anything
    // reads it, and the whole report cannot describe more monsters than a party has.
    val rows = event.packet.mons.distinctBy { it.id }.take(MAX_PARTY_SIZE)
    // A fight takes time, and nothing here knew that.
    val claimsGrowth =
        rows.any { entry ->
          val mon = stored.pokemon.firstOrNull { it.id == entry.id } ?: return@any false
          // An evolution counts as growth even when nothing else moved. A stone is used from a
          // menu rather than won in a fight, so a report carrying one need not carry a level, and
          // without this line that report cost nothing and could be sent at line rate.
          entry.level > mon.level ||
              entry.xp > mon.xp ||
              (entry.species != 0 && entry.species != mon.dexId)
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
      // The per-outcome cap above bounds one report; nothing bounded how many were worth
      // sending, and ten levels a packet at line rate is a level 100 party in about a second.
      val levelGain = entry.level - mon.level
      val growthAllowed =
          levelGain <= 0 || budget.allow(characterId, GrantBudget.Kind.LEVELS, levelGain)
      val level = if (growthAllowed) entry.level else mon.level.toInt()
      // How much the monster likes its trainer. Every rule that moves this is the engine's,
      // and there is no event on the wire to recompute it from, so the reported number is
      // taken as it stands.
      val claimedFriendship =
          if (entry.friendship < 0) mon.friendship else entry.friendship.coerceIn(0, MAX_FRIENDSHIP)
      val friendshipGain = claimedFriendship - mon.friendship
      val friendship =
          if (friendshipGain <= 0 ||
              budget.allow(characterId, GrantBudget.Kind.FRIENDSHIP, friendshipGain))
              claimedFriendship
          else mon.friendship
      // What the monster is carrying.
      val heldItemId = claimedHeldItem(characterId, mon, entry)
      // What the engine holds this monster as now.
      val dexId = evolvedDexId(characterId, mon, entry, level, friendship)
      // Everything after this reads the species: the experience band and the hit point ceiling are
      // computed from it, and the moveset check asks what it learns. So it is settled here, and a
      // refused claim leaves every one of them reading the stored species.
      val definition = species.get(dexId)
      // Level and experience are one number told two ways, and only the level was ever
      // bounded. A row could sit at the level it started on and put two billion experience
      // behind it, which the next server-run battle reads straight back out as level 100.
      val ceiling = definition?.let { xpCeilingFor(it.growthRate, level) }
      val xp = if (growthAllowed) entry.xp else mon.xp
      val cappedXp = if (ceiling != null) xp.coerceAtMost(ceiling) else xp
      if (ceiling != null && xp > ceiling) {
        log.warn {
          "char=$characterId reports monster ${entry.id} at level $level holding $xp xp," +
              " past the $ceiling that level can hold, clamped"
        }
      }
      // A reported moveset is the one thing in this report a fight adds to rather than moves,
      // and it was taken whole.
      val chain = (withPreEvolutions(dexId) + withPreEvolutions(mon.dexId)).distinct()
      val levelUp =
          chain.flatMap { id -> learnsets.get(id).filter { it.level <= level }.map { it.moveId } }
      val taught = chain.mapNotNull { moveSources.get(it) }
      val hasTable = levelUp.isNotEmpty() || taught.isNotEmpty()
      val learnable = (mon.moves.map { it.id.toInt() } + levelUp).toSet()
      val reportedMoves = entry.moves.filter { it.id != 0 }
      val unlearnable =
          reportedMoves.filter { m ->
            moveRegistry.get(m.id) == null ||
                (hasTable && m.id !in learnable && taught.none { it.teaches(m.id) })
          }
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
                    " ${unlearnable.joinToString { "${it.id}" }}, which nothing up to level" +
                    " $level, no machine, no tutor and no egg could have taught it; the stored" +
                    " moveset stands")
            emptyList()
          }
      // The contest half of the same report. All three only ever go up: a Poffin adds to a
      // condition and a contest adds a ribbon, and nothing in the game takes either away.
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
      // A contest awards exactly one ribbon, so exactly one new bit may appear. More than that
      // is not a contest that happened; the ribbon is the rank gate, so a client that could
      // set the mask outright could enter Master rank on a monster that has never competed.
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
            // A rate said how fast the set could be claimed. It never asked whether a contest
            // had happened at all, so a session that had never opened the Hall could still
            // fill the mask, given twenty minutes and nobody to compete against.
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
      // Whether it is still an egg. The engine owns the whole of a hatch: the record's
      // friendship byte is the egg's remaining cycles, the field's step chain spends one every
      // 255 steps, and the game's own hatch scene clears the bit.
      val isEgg = mon.isEgg && entry.isEgg
      // What the scene left it suffering from. Taken as reported and only masked: nothing here
      // is bought by being ill, and clearing a condition is what the player's own Center does
      // for free, so there is nothing for a budget to hold back.
      val status = entry.status and MON_STATUS_MASK
      val grown =
          mon.copy(
              dexId = dexId,
              level = level.toByte(),
              xp = cappedXp,
              moves = moves.ifEmpty { mon.moves },
              conditions = conditions,
              sheen = sheen,
              superContestRibbons = superContestRibbons,
              friendship = friendship,
              heldItemId = heldItemId,
              isEgg = isEgg,
              status = status,
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
              "->${grown.moves.joinToString(",") { "${it.pp}" }}" +
              (if (dexId != mon.dexId) " dex ${mon.dexId}->$dexId" else "") +
              (if (heldItemId != mon.heldItemId) " item ${mon.heldItemId}->$heldItemId" else "") +
              (if (status != mon.status) " status ${mon.status}->$status" else "") +
              if (mon.isEgg && !isEgg) " hatched" else ""
      if (dexId != mon.dexId ||
          level.toByte() != mon.level ||
          cappedXp != mon.xp ||
          hp != mon.hp ||
          grown.moves != mon.moves ||
          grown.conditions != mon.conditions ||
          grown.sheen != mon.sheen ||
          grown.superContestRibbons != mon.superContestRibbons ||
          grown.friendship != mon.friendship ||
          grown.heldItemId != mon.heldItemId ||
          grown.isEgg != mon.isEgg ||
          grown.status != mon.status)
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
   * The species [mon] is after the scene the report describes: the one it claims when the evolution
   * table allows it, and the stored one otherwise.
   */
  /** [dexId] and everything it evolved from, which learned what a stage cannot re-learn. */
  private fun withPreEvolutions(dexId: Int): List<Int> {
    val chain = mutableListOf(dexId)
    var frontier = listOf(dexId)
    repeat(EvolutionRegistry.MAX_EVOLUTION_STEPS) {
      frontier = frontier.flatMap { evolutions.preEvolutionsOf(it) }.filter { it !in chain }
      chain += frontier
    }
    return chain
  }

  private fun evolvedDexId(
      characterId: Long,
      mon: Pokemon,
      entry: BattleOutcomeMon,
      level: Int,
      friendship: Int,
  ): Int {
    val claimed = entry.species
    if (claimed == 0 || claimed == mon.dexId) return mon.dexId
    // The reported conditions are not raised yet, and the raise only ever goes up, so the beauty
    // a Feebas is judged on is the better of what it is stored with and what it reports.
    val beauty = maxOf(mon.conditions.beauty, entry.conditions.beauty)
    // The item is the stored one, not the reported one. The game clears a held item as it
    // evolves on one.
    val heldItem = items.get(mon.heldItemId)
    if (species.get(claimed) == null ||
        !evolutions.isReachable(
            mon.dexId,
            claimed,
            level,
            beauty,
            friendship,
            heldItemId = mon.heldItemId,
            heldItemPreventsEvolution = heldItem?.preventsEvolution ?: false)) {
      violations.record(
          characterId,
          ViolationLog.Kind.IMPOSSIBLE_MONSTER,
          "reports monster ${entry.id} as species $claimed, which a ${mon.dexId} at level" +
              " $level does not become; the stored species stands")
      return mon.dexId
    }
    return claimed
  }

  /**
   * The item [mon] is carrying after the report: what it claims when the claim is an item a monster
   * can hold, and the stored one otherwise.
   */
  private fun claimedHeldItem(characterId: Long, mon: Pokemon, entry: BattleOutcomeMon): Int {
    val claimed = entry.heldItemId
    if (claimed < 0 || claimed == mon.heldItemId) return mon.heldItemId
    if (claimed == 0) return 0
    // A key item is story progress and one-per-save equipment; the game's own give flow never
    // offers one to a monster, so a report of one held is a client sending numbers rather than
    // recording a scene. An id no registry knows is the same thing said differently.
    if (items.get(claimed) == null || claimed in KEY_ITEM_IDS) {
      violations.record(
          characterId,
          ViolationLog.Kind.FORBIDDEN_ITEM,
          "reports monster ${entry.id} holding $claimed, which is not an item a monster can carry;" +
              " the stored item stands")
      return mon.heldItemId
    }
    return claimed
  }

  /**
   * One contest condition as the report leaves it: never below what is stored, never above the
   * game's own ceiling, and never more than a Poffin above where it started.
   */
  private fun raise(stored: Int, reported: Int): Int =
      maxOf(
          stored,
          reported
              .coerceIn(0, MAX_CONTEST_STAT)
              .coerceAtMost(stored + MAX_CONDITION_GAIN_PER_REPORT))

  /**
   * The most experience a monster at [level] can be holding on [rate]'s curve: everything short of
   * what the next level asks for, or the curve's own total at the cap.
   */
  private fun xpCeilingFor(rate: GrowthRate, level: Int): Int {
    // The curve refuses a level outside its own range, and a throw in here is a disconnect for
    // whoever sent the packet. Every level reaching this is already inside it; the clamp is so
    // that stays true of a stored one nobody has checked lately.
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
    // A key item is story progress and one-per-save equipment rather than goods, which is why
    // the broker and the register both wall them off. This wire cannot wall them off the same
    // way, because a scene really does hand one over and this report is how it arrives.
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

  /** The box screen let a monster go. */
  suspend fun onPokemonRelease(event: PacketEvent<PokemonReleasePacket>) {
    val session = event.session
    if (session.attributes[CLIENT_RUNS_SCRIPTS] != true) return
    val state = session.attributes[PLAYER_STATE] ?: return
    val characterId = state.characterId ?: return
    // A release is the third gesture that takes a monster out of a container, and the other
    // two are already refused here.
    val busy =
        when {
          battles.byChar(characterId) != null -> "in battle"
          state.atTradeTable -> "at a trade table"
          else -> null
        }
    if (busy != null) {
      log.warn {
        "Character $characterId asked to release monster ${event.packet.monsterId} while $busy"
      }
      reseatContainers(session, characterId)
      return
    }
    val released = characterStore.releasePokemon(characterId, event.packet.monsterId)
    if (released) {
      log.info { "Character $characterId released monster ${event.packet.monsterId}" }
    } else {
      log.warn {
        "Character $characterId asked to release monster ${event.packet.monsterId}," +
            " which is refused"
      }
    }
    reseatContainers(session, characterId)
  }

  /** Both stored containers back onto the screen, whatever the gesture that asked for them did. */
  private fun reseatContainers(session: SessionContext, characterId: Long) {
    characterStore.getCharacter(characterId)?.let { fresh ->
      session.sendContainer(PokemonContainer.PARTY, fresh.pokemon)
      session.sendContainer(PokemonContainer.PC, fresh.pcStorage)
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
      // The report is the only place this monster exists, so a refusal is a monster the player
      // caught and will not find. Said out loud rather than dropped in silence.
      violations.record(
          characterId,
          ViolationLog.Kind.PAST_ALLOWANCE,
          "is past its monster allowance; the grant of dex ${msg.dexId} lv ${msg.level} was" +
              " refused")
      return
    }
    // What the client sends about the individual is a **name** for the capture, not the
    // capture.
    val rolled =
        if (msg.seed != 0) reportedIndividual.forToken(characterId, msg.dexId, msg.seed) else null
    // A report of an individual the character already owns is the same catch told twice, not a
    // second monster: the engine rolls a fresh personality per capture, so a matching seed on
    // a matching species is a duplicate report (a client whose box sync re-swept an unanswered
    // grant did exactly that, one copy per following battle).
    if (rolled != null) {
      val stored = characterStore.getCharacter(characterId)
      val twin =
          (stored?.pokemon.orEmpty() + stored?.pcStorage.orEmpty() + stored?.daycare.orEmpty())
              .firstOrNull { it.seed == rolled.seed && it.dexId == msg.dexId }
      if (twin != null) {
        log.warn {
          "char=$characterId re-reported the grant of dex ${msg.dexId} seed ${msg.seed}" +
              " (already monster ${twin.id}); refused"
        }
        session.sendContainer(
            twin.container,
            when (twin.container) {
              PokemonContainer.PC -> stored?.pcStorage
              PokemonContainer.DAYCARE -> stored?.daycare
              else -> stored?.pokemon
            }.orEmpty())
        return
      }
    }
    // Shininess used to be the client's word, and there was no way to check it: the engine
    // rolls it from the personality it is reporting and a trainer id that never reaches this
    // record.
    val shiny = rolled?.isShiny == true
    if (shiny && !budget.allow(characterId, GrantBudget.Kind.SHINY, 1)) {
      violations.record(
          characterId,
          ViolationLog.Kind.PAST_ALLOWANCE,
          "has been handed more shinies than an hour of luck gives (dex ${msg.dexId})")
    }
    val moves = learnsets.initialMoveset(msg.dexId, msg.level).ifEmpty { listOf(TACKLE_ID) }
    val container = if (msg.container == 0) PokemonContainer.PC else PokemonContainer.PARTY
    /*
     * A static site's Pokemon, taken by a character whose story came out of a save file that
     * nobody has replayed, carries the same mark that file's own monsters do.
     */
    val fromStaticSite = staticEncounters.claimStaticCatch(characterId, msg.dexId)
    val unearned = fromStaticSite && standsOnUnverifiedImport(characterId)
    if (unearned) {
      log.info {
        "char=$characterId took a static site's dex ${msg.dexId} while standing on an import" +
            " nobody has replayed; it carries the offline mark"
      }
    }
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
                // A name of zero is a report with no capture behind it, and the ordinary wild
                // roll stands. Anything else is the individual derived above.
                rolled?.let { StoryPlayerService.Individual(it.seed, it.ivBits, shiny) },
                offlineOrigin = unearned,
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
      // Said at the moment it happens, so the trade window is never where a player finds out.
      if (unearned) session.send(notice(staticSiteMarkedNotice(granted)))
    }
  }

  /**
   * Is this character's story one it brought in from a file, and has nobody replayed the play
   * behind it?
   */
  private suspend fun standsOnUnverifiedImport(characterId: Long): Boolean {
    val newest = importRecords.newestStanding(characterId) ?: return false
    return newest.replayVerdict != ReplayVerdict.VERIFIED.name
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
