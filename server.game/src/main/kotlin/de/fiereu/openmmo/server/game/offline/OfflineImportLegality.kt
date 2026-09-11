package de.fiereu.openmmo.server.game.offline

import de.fiereu.openmmo.common.ALL_SUPER_CONTEST_RIBBONS
import de.fiereu.openmmo.common.MAX_FRIENDSHIP
import de.fiereu.openmmo.common.MAX_MOVE_SLOTS
import de.fiereu.openmmo.common.MAX_PARTY_SIZE
import de.fiereu.openmmo.common.enums.Ability
import de.fiereu.openmmo.common.enums.MAX_IV
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.PokemonNature
import de.fiereu.openmmo.common.enums.PokemonStat
import de.fiereu.openmmo.common.enums.TileBehavior
import de.fiereu.openmmo.items.ItemRegistry
import de.fiereu.openmmo.maps.MapDef
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.moves.MoveRegistry
import de.fiereu.openmmo.offline.CartridgeLimits
import de.fiereu.openmmo.pokemon.EvolutionRegistry
import de.fiereu.openmmo.pokemon.LearnsetRegistry
import de.fiereu.openmmo.pokemon.MoveSourceRegistry
import de.fiereu.openmmo.pokemon.SpeciesDef
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.battle.ExpCurves
import de.fiereu.openmmo.server.game.services.ITEM_ID_BASE
import de.fiereu.openmmo.server.game.services.KEY_ITEM_IDS
import de.fiereu.openmmo.server.game.services.MAX_SAVE_BLOCK_BYTES
import de.fiereu.openmmo.server.game.services.MAX_SAVE_BLOCK_IDS
import de.fiereu.openmmo.server.game.storage.PC_STORAGE_SIZE
import javax.inject.Inject
import javax.inject.Singleton

/**
 * The door an offline save comes back through: every field measured against what the cartridge can
 * produce.
 */
@Singleton
class OfflineImportLegality
@Inject
constructor(
    private val species: SpeciesRegistry,
    private val learnsets: LearnsetRegistry,
    private val moveSources: MoveSourceRegistry,
    private val evolutions: EvolutionRegistry,
    private val moves: MoveRegistry,
    private val items: ItemRegistry,
    private val maps: MapManager,
    private val cartridge: CartridgeLimits,
) {

  fun check(save: OfflineSave, context: ImportContext): LegalityResult {
    val verdicts = mutableListOf<ImportVerdict>()

    val structural = refuseStructural(save)
    if (structural != null) return LegalityResult(null, listOf(structural))

    val foreign = refuseForeign(save, context)
    if (foreign != null) return LegalityResult(null, listOf(foreign))

    val kept = mutableListOf<OfflineMonster>()
    val seenPids = mutableSetOf<Int>()
    for (monster in save.monsters) {
      val checked = checkMonster(monster, context, seenPids, verdicts)
      if (checked != null) {
        kept += checked
        seenPids += checked.pid
      }
    }

    return LegalityResult(
        save.copy(
            trainerId = trainerId(save, context, verdicts),
            monsters = kept,
            money = money(save.money, verdicts),
            bag = bag(save.bag, verdicts),
            badges = badges(save.badges, verdicts),
            dexSeen =
                save.dexSeen.filter { known(it) }.toSet() + save.dexCaught.filter { known(it) },
            dexCaught = dexCaught(save, verdicts),
            position = position(save.position, context, verdicts),
            blocks = blocks(save.blocks, context, verdicts),
        ),
        verdicts + playTime(save.playTimeSeconds) + story(save),
    )
  }

  /** Whose game this is, which is the one question the rest of this file does not ask. */
  private fun refuseForeign(save: OfflineSave, context: ImportContext): Refused? {
    if (context.serverTrainerId != 0) {
      if (save.trainerId == context.serverTrainerId) return null
      return Refused(
          WHOSE_GAME,
          "the save was played as trainer ${save.trainerId} and this character's own games are" +
              " trainer ${context.serverTrainerId}'s; a different trainer id is a different game," +
              " so bring it in on a new character rather than over this one")
    }
    if (context.exportedPids.isNotEmpty() &&
        save.monsters.none { it.pid in context.exportedPids }) {
      return Refused(
          WHOSE_GAME,
          "not one of the ${context.exportedPids.size} Pokemon this character holds is in the" +
              " save, so this is not the game it was playing; bring it in on a new character" +
              " rather than over this one")
    }
    return null
  }

  // ---- the structural rows, the only ones that may turn a save away -------------------------

  private fun refuseStructural(save: OfflineSave): Refused? {
    val party = save.monsters.filter { it.container == PokemonContainer.PARTY }
    if (party.size > MAX_PARTY_SIZE) {
      return Refused("party", "${party.size} in a party of at most $MAX_PARTY_SIZE")
    }
    val strayParty = party.firstOrNull { it.containerSlot !in 0 until MAX_PARTY_SIZE }
    if (strayParty != null) {
      return Refused("party", "a monster in slot ${strayParty.containerSlot} of $MAX_PARTY_SIZE")
    }
    val boxed = save.monsters.filter { it.container == PokemonContainer.PC }
    val past = boxed.firstOrNull { it.containerSlot !in 0 until PC_STORAGE_SIZE }
    if (past != null) {
      return Refused("boxes", "a monster in slot ${past.containerSlot} of $PC_STORAGE_SIZE")
    }
    val outside = save.monsters.firstOrNull { it.container !in HOMES }
    if (outside != null) {
      return Refused("storage", "a monster kept in ${outside.container}, which is not a home")
    }
    val slots = save.monsters.map { it.container to it.containerSlot }
    if (slots.size != slots.toSet().size) {
      return Refused("storage", "two monsters in one slot")
    }
    return null
  }

  // ---- one monster ---------------------------------------------------------------------------

  private fun checkMonster(
      monster: OfflineMonster,
      context: ImportContext,
      seenPids: Set<Int>,
      verdicts: MutableList<ImportVerdict>,
  ): OfflineMonster? {
    val name = "#${monster.dexId} ${monster.nickname}"

    val def = species.get(monster.dexId)
    if (def == null) {
      verdicts += Dropped(name, "species ${monster.dexId} is not one this server has")
      return null
    }
    if (!cartridge.canProduce(monster.dexId) && monster.pid !in context.exportedPids) {
      verdicts += Dropped(name, "${def.name} is not a species this game can produce")
      return null
    }
    if (monster.pid in seenPids) {
      verdicts += Dropped(name, "a second copy of the same monster")
      return null
    }
    if (monster.pid in context.pidsElsewhere) {
      verdicts += Dropped(name, "this monster lives on another character here")
      return null
    }
    if (monster.pid in context.daycarePids) {
      verdicts += Dropped(name, "this monster is boarding at the day care here, which keeps it")
      return null
    }
    if (monster.form != 0) {
      // No form table exists on this server yet, so there is nothing to measure a form against and
      // an invented rule would drop an honest Giratina. Carried as it stands and written down.
      verdicts += Allowed("$name form", "no form table to check form ${monster.form} against")
    }

    val level = clamp(monster.level, 1, ExpCurves.MAX_LEVEL, "$name level", verdicts)
    val xp = ExpCurves.totalXpFor(def.growthRate, level)
    if (xp != monster.xp) {
      verdicts += Clamped("$name experience", "${monster.xp}", "$xp")
    }

    val ivs = stats(monster.ivs, MAX_IV, IV_TOTAL_MAX, "$name IVs", verdicts)
    val evs = stats(monster.evs, EV_MAX, EV_TOTAL_MAX, "$name EVs", verdicts)

    val nature = PokemonNature.entries[(monster.pid.toLong() and 0xFFFFFFFFL).mod(NATURES)].ordinal
    if (nature != monster.natureByte) {
      verdicts += Clamped("$name nature", "${monster.natureByte}", "$nature")
    }

    val shiny = isShiny(monster.otId, monster.pid)
    if (shiny != monster.isShiny) {
      verdicts += Clamped("$name shiny", "${monster.isShiny}", "$shiny")
    }

    val ability = abilityOf(def, monster.pid)
    if (monster.hasHiddenAbility) {
      verdicts += Clamped("$name ability", "a hidden ability", ability.name)
    } else if (ability.ordinal != monster.abilityId) {
      verdicts += Clamped("$name ability", "${monster.abilityId}", ability.name)
    }

    val heldItem =
        if (monster.heldItemId != 0 && items.get(monster.heldItemId) == null) {
          verdicts +=
              Dropped("$name held item", "item ${monster.heldItemId} is not one this server has")
          0
        } else {
          monster.heldItemId
        }

    val friendship = clamp(monster.friendship, 0, MAX_FRIENDSHIP, "$name friendship", verdicts)
    // The conditions and sheen are single bytes on the wire and the game's own ceiling is 255, so
    // there is nothing to bring into range. The ribbon mask is sixty-four bits of which twenty are
    // ribbons, and a bit outside them is not a ribbon this game has ever awarded.
    val ribbons = monster.superContestRibbons and ALL_SUPER_CONTEST_RIBBONS
    if (ribbons != monster.superContestRibbons) {
      verdicts +=
          Clamped(
              "$name contest ribbons",
              "0x%x".format(monster.superContestRibbons),
              "0x%x".format(ribbons))
    }
    val eggCycles =
        if (monster.isEgg) clamp(monster.eggCyclesLeft, 0, def.eggCycles, "$name egg", verdicts)
        else monster.eggCyclesLeft

    return monster.copy(
        level = level,
        xp = xp,
        ivs = ivs,
        evs = evs,
        natureByte = nature,
        isShiny = shiny,
        abilityId = ability.ordinal,
        hasHiddenAbility = false,
        heldItemId = heldItem,
        friendship = friendship,
        superContestRibbons = ribbons,
        eggCyclesLeft = eggCycles,
        moves = movesOf(monster, def, level, name, verdicts),
        nickname = clampName(monster.nickname, MON_NAME_LEN, def.name, "$name nickname", verdicts),
        otName = clampName(monster.otName, TRAINER_NAME_LEN, DEFAULT_OT, "$name OT name", verdicts),
    )
  }

  /**
   * The moves the monster may keep. A move is legal where a table says the species can know it: its
   * level up list at or below its level, a machine it is compatible with, an egg move or a tutor's.
   */
  private fun movesOf(
      monster: OfflineMonster,
      def: SpeciesDef,
      level: Int,
      name: String,
      verdicts: MutableList<ImportVerdict>,
  ): List<OfflineMove> {
    val chain = withPreEvolutions(monster.dexId)
    val levelUp =
        chain.flatMap { id -> learnsets.get(id).filter { it.level <= level }.map { it.moveId } }
    val taught = chain.mapNotNull { moveSources.get(it) }
    val hasTable = levelUp.isNotEmpty() || taught.isNotEmpty()

    val kept = mutableListOf<OfflineMove>()
    for (slot in monster.moves.take(MAX_MOVE_SLOTS)) {
      val move = moves.get(slot.moveId)
      if (move == null) {
        verdicts += Dropped("$name move ${slot.moveId}", "not a move this server has")
        continue
      }
      val legal = slot.moveId in levelUp || taught.any { it.teaches(slot.moveId) }
      if (!legal) {
        if (hasTable) {
          verdicts += Dropped("$name ${move.name}", "${def.name} cannot learn it")
          continue
        }
        verdicts +=
            Allowed("$name ${move.name}", "no move table for ${def.name} to check it against")
      }
      val ppUps = clamp(slot.ppUps, 0, MAX_PP_UPS, "$name ${move.name} PP Ups", verdicts)
      val maxPp = move.pp + move.pp * PP_UP_PERCENT * ppUps / 100
      kept +=
          slot.copy(pp = clamp(slot.pp, 0, maxPp, "$name ${move.name} PP", verdicts), ppUps = ppUps)
    }

    if (kept.isEmpty()) {
      // The cartridge has no monster with an empty moveset, so one is given its first move back
      // rather than landing as something the battle engine cannot use.
      val first = learnsets.get(monster.dexId).firstOrNull()?.moveId
      if (first != null) {
        val pp = moves.get(first)?.pp ?: 0
        verdicts += Clamped("$name moves", "none left", moves.get(first)?.name ?: "$first")
        return listOf(OfflineMove(first, pp, 0))
      }
    }
    return kept
  }

  /** The species and everything that evolves into it, walked all the way back. */
  private fun withPreEvolutions(dexId: Int): List<Int> {
    val chain = mutableListOf(dexId)
    var frontier = listOf(dexId)
    repeat(EvolutionRegistry.MAX_EVOLUTION_STEPS) {
      frontier = frontier.flatMap { evolutions.preEvolutionsOf(it) }.filter { it !in chain }
      chain += frontier
    }
    return chain
  }

  // ---- the rest of the character ---------------------------------------------------------------

  /**
   * A brand new online character takes the save's trainer id; one that already exists keeps its
   * own, and the save's monsters keep theirs as data the way a traded monster does.
   */
  private fun trainerId(
      save: OfflineSave,
      context: ImportContext,
      verdicts: MutableList<ImportVerdict>,
  ): Int =
      if (context.firstImport) {
        verdicts += Allowed("trainer id", "the save's own id becomes this character's")
        save.trainerId
      } else {
        verdicts += Allowed("trainer id", "this character keeps the id it already had")
        context.serverTrainerId
      }

  /**
   * Money does not cross. The character keeps the wallet it earned here and the file keeps the one
   * it earned there, and the two are never the same wallet.
   */
  private fun money(money: Int, verdicts: MutableList<ImportVerdict>): Int {
    if (money > 0) {
      verdicts +=
          Dropped(
              "money",
              "$money stays in the offline game; a wallet is earned in the world it is spent in")
    }
    return 0
  }

  /**
   * The bag, with anything the item table does not have dropped by name. A key item or an HM is
   * one-per-save equipment, so a stack of them is brought back to one, and the whole of
   * [DOES_NOT_CROSS] is left behind whatever the file says about it.
   */
  private fun bag(bag: List<OfflineItem>, verdicts: MutableList<ImportVerdict>): List<OfflineItem> {
    val kept = mutableListOf<OfflineItem>()
    for (line in bag) {
      val item = items.get(line.itemId)
      if (item == null) {
        verdicts += Dropped("item ${line.itemId}", "not an item this server has")
        continue
      }
      if (line.itemId - ITEM_ID_BASE in cartridge.eventItems) {
        verdicts += Dropped(item.name, "nothing in this game hands one out")
        continue
      }
      val fenced = DOES_NOT_CROSS.firstOrNull { line.itemId in it.first }
      if (fenced != null) {
        verdicts += Dropped(item.name, fenced.second)
        continue
      }
      val cap = if (line.itemId in ONE_EACH_IDS) 1 else BAG_MAX_QUANTITY
      kept += line.copy(quantity = clamp(line.quantity, 0, cap, item.name, verdicts))
    }
    return kept.filter { it.quantity > 0 }
  }

  /** The badge bitfield, kept to the eight bits a region has. */
  private fun badges(badges: Int, verdicts: MutableList<ImportVerdict>): Int {
    val mask = (1 shl BADGE_COUNT) - 1
    val kept = badges and mask
    if (badges != kept) {
      verdicts +=
          Clamped("badges", "0x%x".format(badges), "0x%x, the $BADGE_COUNT there are".format(kept))
    }
    return kept
  }

  /** Caught implies seen, and a bit for a species that does not exist is not a species. */
  private fun dexCaught(save: OfflineSave, verdicts: MutableList<ImportVerdict>): Set<Int> {
    val caught = save.dexCaught.filter { known(it) }.toSet()
    val unknown = save.dexCaught.size - caught.size
    if (unknown > 0)
        verdicts += Clamped("Pokedex", "${save.dexCaught.size} caught", "${caught.size}")
    val unseen = caught - save.dexSeen
    if (unseen.isNotEmpty()) {
      verdicts += Clamped("Pokedex", "${unseen.size} caught but unseen", "seen")
    }
    return caught
  }

  /**
   * Where the save left the player, if this server can draw it and they can stand there; the last
   * place they healed otherwise, which is where a blackout would have put them anyway.
   */
  private fun position(
      position: OfflinePosition,
      context: ImportContext,
      verdicts: MutableList<ImportVerdict>,
  ): OfflinePosition {
    val map = maps.getMap(position.regionId, position.bankId, position.mapId)
    if (map != null && !map.ported && canStandAt(map, position.x, position.y)) return position
    val why =
        when {
          map == null -> "a map this server cannot draw"
          map.ported -> "a map out of another cartridge, which no offline save was written on"
          else -> "a tile nobody can stand on"
        }
    verdicts += Clamped("position", why, "the last place this character healed")
    return context.lastHealLocation
  }

  private fun canStandAt(map: MapDef, x: Int, y: Int): Boolean {
    if (x !in 0 until map.width || y !in 0 until map.height) return false
    val tile = map.tileAt(x, y) ?: return true
    if (tile.blocksMovement()) return false
    return tile.behavior != TileBehavior.WATERFALL && tile.behavior != TileBehavior.SURFABLE_WATER
  }

  /** The play time is a signal about a save, never a gate: the offline clock is the player's. */
  private fun playTime(seconds: Int): ImportVerdict =
      Allowed("play time", "$seconds seconds, recorded and not gated on")

  /** The script VM's keys, which cross as they stand because nothing here is gated on one. */
  private fun story(save: OfflineSave): ImportVerdict =
      Allowed(
          "story",
          "${save.storyFlags.size} flags and ${save.storyVars.size} variables cross as the" +
              " script left them")

  /**
   * The engine blocks that cross: everything but [blocksKeptHere], whose server copies stand, and
   * nothing wider or more numerous than the script report's own ceilings.
   */
  private fun blocks(
      blocks: Map<Int, ByteArray>,
      context: ImportContext,
      verdicts: MutableList<ImportVerdict>,
  ): Map<Int, ByteArray> {
    val stays = blocksKeptHere(context.firstImport)
    val crossing = blocks.filterKeys { it !in stays }
    for (id in blocks.keys - crossing.keys) {
      verdicts +=
          Dropped(
              "save block $id",
              BLOCKS_THAT_DO_NOT_CROSS[id] ?: "this character keeps the trainer id it already had")
    }
    val sized =
        crossing.filterValues { it.isNotEmpty() && it.size <= MAX_SAVE_BLOCK_BYTES }.toSortedMap()
    for ((id, data) in crossing) {
      if (id in sized) continue
      verdicts +=
          Dropped(
              "save block $id",
              if (data.isEmpty()) "the file carries no bytes for it"
              else
                  "${data.size} bytes, and no block this game writes is past" +
                      " $MAX_SAVE_BLOCK_BYTES")
    }
    val room = MAX_SAVE_BLOCK_IDS - stays.size
    if (sized.size <= room) return sized
    val kept = sized.entries.take(room).associate { it.key to it.value }
    verdicts += Clamped("save blocks", "${sized.size} the file carries", "$room")
    return kept
  }

  // ---- the small measurements ------------------------------------------------------------------

  private fun known(dexId: Int) = species.get(dexId) != null

  /**
   * The cartridge's own shininess: the two halves of the trainer id and the two halves of the
   * personality value, exclusive-ored together, under eight (`Pokemon_InlineIsPersonalityShiny`,
   * `pokemon.c`).
   */
  private fun isShiny(otId: Int, pid: Int): Boolean {
    val fold = (otId ushr 16) xor (otId and 0xFFFF) xor (pid ushr 16) xor (pid and 0xFFFF)
    return fold < SHINY_THRESHOLD
  }

  /**
   * Which of the species' two abilities the personality value picks. The cartridge takes the second
   * on an odd personality and the first otherwise, and a species with only one ability always has
   * that one (`BoxPokemon_SetDefaultData`, `pokemon.c`).
   */
  private fun abilityOf(def: SpeciesDef, pid: Int): Ability =
      if (def.ability2 != Ability.NONE && (pid and 1) == 1) def.ability2 else def.ability1

  /**
   * Six stats brought into the cartridge's own range: each into 0..[each], and then the six
   * together down to [total] by taking from whichever is largest, a point at a time, so a save that
   * spread its points evenly keeps the shape it had.
   */
  private fun stats(
      values: Map<PokemonStat, Int>,
      each: Int,
      total: Int,
      what: String,
      verdicts: MutableList<ImportVerdict>,
  ): Map<PokemonStat, Int> {
    val raw = PokemonStat.entries.associateWith { values[it] ?: 0 }
    var clamped = raw.mapValues { it.value.coerceIn(0, each) }
    if (clamped != raw) verdicts += Clamped(what, render(raw), render(clamped))

    if (clamped.values.sum() > total) {
      val before = clamped.values.sum()
      while (clamped.values.sum() > total) {
        val biggest = clamped.maxBy { it.value }
        if (biggest.value == 0) break
        clamped = clamped + (biggest.key to biggest.value - 1)
      }
      verdicts += Clamped("$what total", "$before", "${clamped.values.sum()}")
    }
    return clamped
  }

  private fun render(stats: Map<PokemonStat, Int>): String =
      PokemonStat.entries.joinToString("/") { "${stats[it] ?: 0}" }

  private fun clamp(
      value: Int,
      low: Int,
      high: Int,
      what: String,
      verdicts: MutableList<ImportVerdict>,
  ): Int {
    val clamped = value.coerceIn(low, high)
    if (clamped != value) verdicts += Clamped(what, "$value", "$clamped")
    return clamped
  }

  /**
   * A name the game can draw, no longer than the cartridge's own field. Everything outside the
   * drawable set is taken out rather than the name being refused, and a name with nothing left of
   * it falls back to what the game would have called it.
   */
  private fun clampName(
      name: String,
      cap: Int,
      fallback: String,
      what: String,
      verdicts: MutableList<ImportVerdict>,
  ): String {
    val drawable = name.filter { it in DRAWABLE }.trim().take(cap)
    val clamped = drawable.ifEmpty { fallback.take(cap) }
    if (clamped != name) verdicts += Clamped(what, name, clamped)
    return clamped
  }

  companion object {
    /**
     * What [refuseForeign] names itself, so the caller can tell an identity refusal from a
     * structural one and write it down under the right heading.
     */
    const val WHOSE_GAME = "whose game this is"

    /** The containers a save can put a monster in. Everything else is a server-side holding pen. */
    private val HOMES = setOf(PokemonContainer.PARTY, PokemonContainer.PC)

    /** The cartridge's name fields (`MON_NAME_LEN`, `TRAINER_NAME_LEN`, `string.h`). */
    const val MON_NAME_LEN = 10
    const val TRAINER_NAME_LEN = 7

    private const val DEFAULT_OT = "Trainer"

    private val DRAWABLE = (('A'..'Z') + ('a'..'z') + ('0'..'9') + " '.-!?♂♀".toList()).toSet()

    private val NATURES = PokemonNature.entries.size

    /** `Pokemon_InlineIsPersonalityShiny` compares the fold against 8. */
    private const val SHINY_THRESHOLD = 8

    /** Six IVs of at most 31 (`IVs`, five bits each). */
    private const val IV_TOTAL_MAX = MAX_IV * 6

    /** The cartridge lets one stat hold 255 EVs and the six hold 510 together. */
    private const val EV_MAX = 252
    private const val EV_TOTAL_MAX = 510

    /** `MoveTable_CalcMaxPP`: three PP Ups, each worth a fifth of the move's own PP. */
    private const val MAX_PP_UPS = 3
    private const val PP_UP_PERCENT = 20

    /** `BAG_MAX_QUANTITY_ITEM`, `bag.c`. */
    private const val BAG_MAX_QUANTITY = 999

    private const val BADGE_COUNT = 8

    /**
     * The HMs and the key items pocket, which the wire numbers as one unbroken run: HM01 at 5420
     * through Secret Key at 5467, read off the engine's own item table (an item's wire id is 5000
     * plus its line in `generated/items.txt`, the same block [KEY_ITEM_IDS] is cut from).
     */
    private val ONE_EACH_IDS = 5420..KEY_ITEM_IDS.last

    /** What a save cannot bring with it, whatever the file says, and why each band is here. */
    private val DOES_NOT_CROSS: List<Pair<IntRange, String>> =
        listOf(
            5001..5016 to "balls are caught with, and a catch is the server's roll to make",
            5017..5054 to
                "medicine and vitamins are consumables, and consumables are re-earned here",
            5055..5069 to "battle items are consumables, and consumables are re-earned here",
            5076..5079 to "repels are consumables, and consumables are re-earned here",
            5137..5148 to "mail is written and carried here, not brought in",
            5149..5212 to "berries are grown, and a berry from a file is a berry nobody grew",
            5328..5419 to "a TM teaches a move, and a move is taught on this side of the door",
            // A fossil is a Pokemon with a waiting room. Revived here it would be minted
            // online, unmarked and sellable, out of an item from a file, the same road the
            // static sites took, and closed the same way.
            5099..5105 to "a fossil is revived into a Pokemon, and that happens on this side",
            5572..5573 to "a fossil is revived into a Pokemon, and that happens on this side",
        )

    /**
     * The engine save blocks that do not cross, and why. Ids are the engine's own save table
     * (`save_table.h`) except where the mod had to invent one for a field the table has no entry
     * for, which is where options' 0xC0 comes from.
     */
    private val BLOCKS_THAT_DO_NOT_CROSS =
        mapOf(
            8 to
                "the day care is kept here; a boarder in the file stays with the file, so" +
                    " withdraw it before bringing the save online",
            12 to "the Underground's goods, spheres and traps are earned below ground here",
            16 to "Poffins are a consumable, and consumables are re-made online",
            23 to "Battle Points and the frontier's records are earned at the frontier here",
            0xC0 to "the settings screen is this machine's, not the character's",
        )

    /** The trainer id and secret id, under the id the mod gave them (`openmmo_save_blocks.c`). */
    const val TRAINER_ID_BLOCK = 0xC1

    /**
     * The blocks whose server copies stand through an import: the ones that never cross, and on any
     * import but the first the trainer id, the verdict says the character keeps the id it already
     * had, and that was only a record until this block stopped crossing with the file.
     */
    fun blocksKeptHere(firstImport: Boolean): Set<Int> =
        if (firstImport) BLOCKS_THAT_DO_NOT_CROSS.keys
        else BLOCKS_THAT_DO_NOT_CROSS.keys + TRAINER_ID_BLOCK
  }
}
