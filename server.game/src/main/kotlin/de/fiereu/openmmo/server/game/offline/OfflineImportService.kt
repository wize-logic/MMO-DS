package de.fiereu.openmmo.server.game.offline

import de.fiereu.openmmo.common.CharacterInfo
import de.fiereu.openmmo.common.HealLocation
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.PokemonStat
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.maps.generated.sinnoh.SpawnLocations
import de.fiereu.openmmo.offline.CartridgeLimits
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.battle.StatCalculator
import de.fiereu.openmmo.server.game.script.Badge
import de.fiereu.openmmo.server.game.services.DuelService
import de.fiereu.openmmo.server.game.services.GrantBudget
import de.fiereu.openmmo.server.game.services.TradeService
import de.fiereu.openmmo.server.game.services.ViolationLog
import de.fiereu.openmmo.server.game.services.VmStoryKeys
import de.fiereu.openmmo.server.game.services.notice
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.session.setMapAddress
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.storage.ImportRecord
import de.fiereu.openmmo.server.game.storage.ImportRepository
import de.fiereu.openmmo.server.game.storage.LinkStore
import de.fiereu.openmmo.server.game.storage.OfflineItemRepository
import de.fiereu.openmmo.server.game.storage.SaveBlockRepository
import de.fiereu.openmmo.server.game.storage.StoredCharacter
import io.github.oshai.kotlinlogging.KotlinLogging
import java.time.Duration
import java.time.LocalDateTime
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/** A save offered to the server, with the two things only the client that read it can say. */
data class ImportRequest(
    val characterId: Long,
    val save: OfflineSave,
    /** The client's hash of the file. Recognises the same save twice; decides nothing. */
    val saveSha256: String,
    val clientRevision: Int,
)

sealed interface ImportOutcome {
  /**
   * Something is mid-flight that the replacement would tear in half. Minutes, never a rule, and the
   * sentence says so, the player is meant to press it again.
   */
  data class TryAgain(val why: String) : ImportOutcome

  /** A save no honest playthrough could have written, named by the row that said so. */
  data class Refused(val why: String) : ImportOutcome

  data class Landed(val record: ImportRecord, val verdicts: List<ImportVerdict>) : ImportOutcome
}

sealed interface UndoOutcome {
  data class Restored(val record: ImportRecord) : UndoOutcome

  data class Refused(val why: String) : UndoOutcome
}

/** An offline save coming back online: bounded, written down, and undoable. */
@Singleton
class OfflineImportService(
    private val legality: OfflineImportLegality,
    private val characters: CharacterStore,
    private val saveBlocks: SaveBlockRepository,
    private val offlineItems: OfflineItemRepository,
    private val imports: ImportRepository,
    private val budget: GrantBudget,
    private val violations: ViolationLog,
    private val entityIds: EntityIdService,
    private val species: SpeciesRegistry,
    private val cartridge: CartridgeLimits,
    private val trades: TradeService,
    private val duels: DuelService,
    private val links: LinkStore,
    private val sessions: SessionRegistry,
    private val clock: () -> LocalDateTime = LocalDateTime::now,
) {

  @Inject
  constructor(
      legality: OfflineImportLegality,
      characters: CharacterStore,
      saveBlocks: SaveBlockRepository,
      offlineItems: OfflineItemRepository,
      imports: ImportRepository,
      budget: GrantBudget,
      violations: ViolationLog,
      entityIds: EntityIdService,
      species: SpeciesRegistry,
      cartridge: CartridgeLimits,
      trades: TradeService,
      duels: DuelService,
      links: LinkStore,
      sessions: SessionRegistry,
  ) : this(
      legality,
      characters,
      saveBlocks,
      offlineItems,
      imports,
      budget,
      violations,
      entityIds,
      species,
      cartridge,
      trades,
      duels,
      links,
      sessions,
      LocalDateTime::now)

  /** One import per account at a time, which is the whole of the "no two at once" rule. */
  private val landing = ConcurrentHashMap.newKeySet<Int>()

  suspend fun import(request: ImportRequest): ImportOutcome {
    val stored =
        characters.getOrLoadCharacter(request.characterId)
            ?: return ImportOutcome.Refused("that character is not on this server")
    val userId = stored.info.userId
    // The save's keys must already be the character's own.
    if (request.save.position.regionId != stored.info.positionRegionId.toInt()) {
      log.warn {
        "char=${request.characterId} offered a save keyed to region" +
            " ${request.save.position.regionId}, not its own ${stored.info.positionRegionId}"
      }
      return ImportOutcome.Refused("that save is not keyed to this character's region")
    }

    val busy = whatIsInTheWay(stored)
    if (busy != null) return ImportOutcome.TryAgain(busy)
    if (!landing.add(userId)) {
      return ImportOutcome.TryAgain("another save of this account is still landing")
    }
    try {
      return land(request, stored)
    } finally {
      landing.remove(userId)
    }
  }

  /** The things an import would tear in half, and the one race it would lose. */
  private fun whatIsInTheWay(stored: StoredCharacter): String? {
    val id = stored.info.id
    if (trades.inTrade(id)) return "finish the trade first, then try again"
    if (duels.inLinkBattle(id)) return "finish the battle first, then try again"
    if (links.get(id) != null) return "leave the link party first, then try again"
    val bound = sessions.getByCharacterId(id)
    val account = sessions.sessionForUser(stored.info.userId)
    if (bound != null && account != null && bound !== account) {
      return "this account is playing somewhere else; try again in a moment"
    }
    return null
  }

  private suspend fun land(request: ImportRequest, stored: StoredCharacter): ImportOutcome {
    val characterId = request.characterId
    // Not one of GrantBudget's per-minute windows: an import is a whole character at once and
    // would blow through every one of them by design.
    if (!budget.allow(characterId, GrantBudget.Kind.IMPORT, 1)) {
      violations.record(
          characterId, ViolationLog.Kind.PAST_ALLOWANCE, "more save imports in an hour than a hand")
      return ImportOutcome.Refused("that is a great many saves in an hour; try again later")
    }

    val history = imports.listFor(characterId, HISTORY_READ)
    val standing = history.filter { !it.undone }
    val context =
        ImportContext(
            // What left with the character is what this server still holds: "continue offline"
            // writes a save, it does not take the record away. So a monster the cartridge cannot
            // produce is this character's own if the record here already knows its personality.
            exportedPids = stored.allMonsters().map { it.seed }.toSet(),
            pidsElsewhere =
                characters.seedsHeldElsewhere(
                    request.save.monsters.map { it.pid }.toSet(), characterId),
            firstImport = standing.isEmpty(),
            serverTrainerId = standing.minByOrNull { it.importedAt }?.trainerId ?: 0,
            lastHealLocation = healLocationOf(stored),
            daycarePids = stored.daycare.map { it.seed }.toSet(),
        )

    val result = legality.check(request.save, context)
    val refusal = result.refusal
    if (refusal != null || result.save == null) {
      val why = refusal?.let { "${it.what}: ${it.why}" } ?: "the save could not be read"
      log.warn { "char=$characterId offered a save this server will not take, $why" }
      // A save that is somebody else's game is a different no from a save no playthrough could
      // have written, and it is the one a moderator wants to see a pattern of: the first is about
      // whose file this is, the second about what is in it.
      val kind =
          if (refusal?.what == OfflineImportLegality.WHOSE_GAME) ViolationLog.Kind.NOT_YOURS
          else ViolationLog.Kind.IMPOSSIBLE_MONSTER
      violations.record(characterId, kind, "import refused: $why")
      return ImportOutcome.Refused(why)
    }

    val verdicts = result.verdicts.toMutableList()
    // The same file, already somewhere else here.
    val alsoOn = imports.charactersWithSave(request.saveSha256, characterId, SHARED_SAVE_LOOK)
    if (alsoOn.isNotEmpty()) {
      verdicts +=
          Allowed(
              "this save file",
              "it has already been brought online on ${alsoOn.size} other character(s) here")
      violations.record(
          characterId,
          ViolationLog.Kind.NOT_YOURS,
          "brought in a save file that is already on character(s) ${alsoOn.joinToString()}")
      log.warn {
        "char=$characterId imported save ${request.saveSha256.take(12)}, already on" +
            " ${alsoOn.joinToString()}"
      }
    }
    // The badges and the Pokedex the door has just checked, turned into the story keys this server
    // actually keeps them under. It runs here rather than inside the door because a dex key for a
    // species the door dropped would put back what it had taken out.
    val region =
        Region.byWireValue(result.save.position.regionId.toByte())?.name?.lowercase().orEmpty()
    val checked = with(OfflineImportRequests) { result.save.withEarnedKeys(region) }
    // The character keeps the wallet it earned here. The save's is dropped at the door with a
    // verdict saying so ([OfflineImportLegality.money]), money is the one thing an import brings
    // that no mark can follow, so the two wallets are separate rather than one of them bounded.
    val moneyAfter = stored.info.money

    val now = clock()
    val blocksBefore = saveBlocks.load(characterId)
    val markedBefore = offlineItems.load(characterId)
    val snapshot = ImportSnapshot.encode(CharacterCopy(stored, blocksBefore.toMap(), markedBefore))
    val monsters = checked.monsters.map { monsterFor(characterId, it, now) }
    val party = monsters.filter { it.container == PokemonContainer.PARTY }
    val box = monsters.filter { it.container != PokemonContainer.PARTY }

    val record =
        ImportRecord(
            id = entityIds.newImportId(),
            characterId = characterId,
            importedAt = now,
            playTimeSeconds = checked.playTimeSeconds,
            saveSha256 = request.saveSha256,
            clientRevision = request.clientRevision,
            trainerId = checked.trainerId,
            partyCount = party.size,
            boxCount = box.size,
            speciesCount = monsters.map { it.dexId }.distinct().size,
            levelTotal = monsters.sumOf { it.level.toInt() },
            levelMax = monsters.maxOfOrNull { it.level.toInt() } ?: 0,
            moneyBefore = stored.info.money,
            moneyAfter = moneyAfter,
            badgesBefore = badgeCount(stored.storyFlags, regionOf(stored)),
            badgesAfter = badgeCount(checked.storyFlags, region),
            verdicts = verdicts.map { it.toString() },
            snapshotVersion = ImportSnapshot.VERSION,
        )

    // The record and its snapshot go down before the character moves.
    imports.record(record, snapshot)

    val regionId = result.save.position.regionId.toByte()
    val landed =
        characters.replaceDurably(characterId) { current ->
          current.replacedBy(checked, moneyAfter, party, box, regionId)
        }
    if (!landed) {
      imports.markRolledBack(record.id, clock(), "the write that would have replaced the character")
      log.error { "char=$characterId import ${record.id} could not be written; nothing changed" }
      return ImportOutcome.TryAgain("that did not save; try again in a moment")
    }
    // The character is the save now, blocks included: the file's are written whole and every old
    // one goes, except the few the door keeps here, whose server copies stand around them.
    val stays = OfflineImportLegality.blocksKeptHere(context.firstImport)
    saveBlocks.replace(characterId, blocksBefore.filterKeys { it in stays } + checked.blocks)
    // And how much of the bag came out of the file. Every item has a price and a mart pays half of
    // it, so a bag that crossed unmarked would be the wallet this door just refused, one sale
    // later.
    offlineItems.replace(characterId, checked.bag.associate { it.itemId to it.quantity })
    reseat(characterId, checked.position)

    log.info {
      "char=$characterId imported a save: ${party.size} in the party, ${box.size} in the boxes," +
          " ${record.moneyGained} in, ${verdicts.size} verdict(s), import ${record.id}"
    }
    return ImportOutcome.Landed(record, verdicts)
  }

  /**
   * The live session's idea of where the character stands, brought to where the record now says.
   */
  private fun reseat(characterId: Long, at: OfflinePosition) {
    val state = sessions.getByCharacterId(characterId)?.attributes?.get(PLAYER_STATE) ?: return
    state.setMapAddress(at.regionId, at.bankId, at.mapId)
    state.x = at.x.toShort()
    state.y = at.y.toShort()
  }

  private fun reseat(characterId: Long, info: CharacterInfo) =
      reseat(
          characterId,
          OfflinePosition(
              info.positionRegionId.toInt(),
              info.positionBankId.toInt(),
              info.positionMapId.toInt(),
              info.positionX.toInt(),
              info.positionY.toInt()))

  /** Ends the session a character is playing on, if there is one, with a sentence first. */
  fun endSessionOf(characterId: Long, message: String, why: String) {
    val session = sessions.getByCharacterId(characterId) ?: return
    session.send(notice(message)).addListener { session.close { why } }
  }

  /** Brings the money down to what the week has left, and says so. */
  /** The player's own undo, from the Menu. */
  suspend fun undoLatest(characterId: Long): UndoOutcome {
    val record =
        imports.listFor(characterId, HISTORY_READ).firstOrNull { !it.undone }
            ?: return UndoOutcome.Refused("there is nothing here to undo")
    if (record.sealedAt != null) {
      return UndoOutcome.Refused(
          "something has left this character since (${record.sealedReason}), so undoing the" +
              " import would make a second copy of it")
    }
    if (record.importedAt.isBefore(clock().minus(UNDO_WINDOW))) {
      return UndoOutcome.Refused(
          "that import is more than ${UNDO_WINDOW.toDays()} days old; a moderator can still undo it")
    }
    return restore(record, "the player")
  }

  /** A moderator's undo of a named import. No window and no seal: a person is looking at it. */
  suspend fun rollBack(importId: Long, by: String): UndoOutcome {
    val record = imports.find(importId) ?: return UndoOutcome.Refused("no import has that id")
    if (record.undone) {
      return UndoOutcome.Refused("that one was already rolled back by ${record.rolledBackBy}")
    }
    return restore(record, by)
  }

  private suspend fun restore(record: ImportRecord, by: String): UndoOutcome {
    val (blob, version) =
        imports.loadSnapshot(record.id)
            ?: return UndoOutcome.Refused("that import kept no copy of what it replaced")
    val copy =
        try {
          ImportSnapshot.decode(blob, version)
        } catch (e: ImportSnapshot.UnreadableSnapshot) {
          // Loudly and with nothing touched. Putting back a blob this server cannot read would be
          // handing a player somebody else's idea of their own party.
          log.error(e) { "import ${record.id} kept a copy this server cannot read" }
          return UndoOutcome.Refused("this server cannot read what that import kept: ${e.message}")
        }
    if (!imports.markRolledBack(record.id, clock(), by)) {
      return UndoOutcome.Refused("somebody else rolled that one back first")
    }
    // A moderator's undo usually reaches a player who is not online, and the store's writes work
    // on the cache, so the character is loaded for this if it has to be.
    val restored =
        characters.withLoaded(record.characterId) {
          characters.replaceDurably(record.characterId) { copy.character }
        } ?: false
    if (!restored) {
      return UndoOutcome.Refused("that did not save; the import still stands")
    }
    // Exactly what the import replaced, and nothing the file brought: the blocks are put back
    // whole, so an id the copy never had is taken away rather than left over from the import.
    saveBlocks.replace(record.characterId, copy.blocks)
    // The marks go back with the bag they are about. Left as they were, a rollback would hand the
    // character its old items under the newer import's marks and make things it earned here
    // unsellable; cleared, it would make a file's items sellable.
    offlineItems.replace(record.characterId, copy.offlineItems)
    reseat(record.characterId, copy.character.info)
    log.info { "import ${record.id} on char=${record.characterId} rolled back by $by" }
    return UndoOutcome.Restored(record)
  }

  // ---- turning a checked save into the character ------------------------------------------------

  /** The day care boarders and the shelf are not replaced along with the rest. */
  private fun StoredCharacter.replacedBy(
      checked: OfflineSave,
      money: Int,
      party: List<Pokemon>,
      box: List<Pokemon>,
      regionId: Byte,
  ): StoredCharacter {
    val theSaves = SaveKeys(regionId)
    return copy(
        info =
            info.copy(
                money = money,
                positionRegionId = checked.position.regionId.toByte(),
                positionBankId = checked.position.bankId.toByte(),
                positionMapId = checked.position.mapId.toByte(),
                positionX = checked.position.x.toShort(),
                positionY = checked.position.y.toShort(),
                lastHealLocation = healPointOf(checked, regionId) ?: info.lastHealLocation,
            ),
        pokemon = party.toMutableList(),
        pcStorage = box.toMutableList(),
        items = checked.bag.associate { it.itemId to it.quantity }.toMutableMap(),
        storyFlags =
            (storyFlags.filterNot { theSaves.owns(it) } + checked.storyFlags).toMutableSet(),
        // A var of 0 is the default, so it is stored as absent everywhere else too.
        storyVars =
            (storyVars.filterKeys { !theSaves.owns(it) } +
                    checked.storyVars.filterValues { it != 0 })
                .toMutableMap(),
    )
  }

  /** One checked monster as a record of this server's. */
  private fun monsterFor(characterId: Long, monster: OfflineMonster, at: LocalDateTime): Pokemon {
    val evs =
        EVs().also { stats -> PokemonStat.entries.forEach { stats[it] = byteOf(monster.evs[it]) } }
    val ivs =
        IVs().also { stats -> PokemonStat.entries.forEach { stats[it] = byteOf(monster.ivs[it]) } }
    val definition = species.get(monster.dexId)
    val hp =
        definition?.let { StatCalculator.maxHp(it.baseHp, ivs.hp, evs.hp, monster.level) }
            ?: monster.level
    return Pokemon(
        id = entityIds.newMonsterId(),
        ownerId = characterId,
        container = monster.container,
        containerSlot = monster.containerSlot.toShort(),
        dexId = monster.dexId,
        seed = monster.pid,
        ot = monster.otName,
        nickname = monster.nickname,
        level = monster.level.toByte(),
        hp = hp.toShort(),
        xp = monster.xp,
        eVs = evs,
        iVs = ivs,
        moves = monster.moves.map { PokemonMove(it.moveId.toShort(), it.pp.toByte()) },
        isShiny = monster.isShiny,
        hasHiddenAbility = monster.hasHiddenAbility,
        isAlpha = false,
        isSecret = false,
        isFatefulEncounter = false,
        isRaidEncounter = false,
        caughtAt = at,
        isEgg = monster.isEgg,
        form = monster.form,
        friendship = monster.friendship,
        heldItemId = monster.heldItemId,
        status = monster.status,
        // What the save's own contests and Poffins left on it. The ball it is in, the Pokerus it
        // is carrying and its box markings come across too and stop here: the record has nowhere
        // to keep them.
        conditions = monster.conditions,
        sheen = monster.sheen,
        superContestRibbons = monster.superContestRibbons,
        // Where the file says it was caught. A label and not a map: the file keeps no map, and the
        // 125 labels the game's 593 headers share cannot be told apart, so the triple the record
        // usually carries stays empty and the client draws this instead.
        caughtLocationLabel = monster.metLocationLabel,
        // The fence. Every monster an import brings wears it, and only a replayed session takes it
        // off; what it costs is the market and the ranked ladder, and nothing else.
        offlineOrigin = true,
    )
  }

  /** Where the save says a white out would land, as the location this server keeps. */
  private fun healPointOf(save: OfflineSave, regionId: Byte): HealLocation? {
    if (regionId != Region.SINNOH.wireValue) return null
    return SpawnLocations.healLocation(save.blackOutWarpId)
  }

  private fun byteOf(value: Int?): Byte = (value ?: 0).coerceIn(0, 255).toByte()

  /**
   * Which story keys a save speaks for: the VM's flags and variables ([VmStoryKeys]) and the badge
   * and Pokedex keys the door derives from it (`Badge.keyIn`, `Pokedex.seenKey`,
   * `Pokedex.caughtKey`).
   */
  private class SaveKeys(regionId: Byte) {
    private val region = Region.byWireValue(regionId)?.name?.lowercase().orEmpty()
    private val prefixes =
        listOf(
            VmStoryKeys.prefix(regionId),
            "$region/BADGE_ID_",
            "$region/DEX_SEEN_",
            "$region/DEX_CAUGHT_",
        )

    fun owns(key: String): Boolean = prefixes.any { key.startsWith(it) }
  }

  private fun StoredCharacter.allMonsters(): List<Pokemon> = pokemon + pcStorage + daycare

  private fun regionOf(stored: StoredCharacter): String =
      Region.byWireValue(stored.info.positionRegionId)?.name?.lowercase().orEmpty()

  private fun badgeCount(flags: Set<String>, region: String): Int =
      Badge.entries.count { it.keyIn(region) in flags }

  private fun healLocationOf(stored: StoredCharacter): OfflinePosition {
    val heal = stored.info.lastHealLocation
    return if (heal == null) {
      OfflinePosition(
          stored.info.positionRegionId.toInt(),
          stored.info.positionBankId.toInt(),
          stored.info.positionMapId.toInt(),
          stored.info.positionX.toInt(),
          stored.info.positionY.toInt())
    } else {
      OfflinePosition(
          heal.regionId.toInt(),
          heal.bankId.toInt(),
          heal.mapId.toInt(),
          heal.x.toInt(),
          heal.y.toInt())
    }
  }

  private companion object {
    /** How long the player's own undo stays open. A moderator's has no window. */
    val UNDO_WINDOW: Duration = Duration.ofDays(7)

    /** Enough history to answer "is this the first" and "what is the newest" in one read. */
    const val HISTORY_READ = 64

    /**
     * How many other characters holding the same file are worth naming. The question is "has this
     * been here before", not "how many times", and a handful is enough for a person to act on.
     */
    const val SHARED_SAVE_LOOK = 8
  }
}
