package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.enums.Ability
import de.fiereu.openmmo.common.enums.EncounterMethod
import de.fiereu.openmmo.items.ItemRegistry
import de.fiereu.openmmo.maps.MapDef
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.maps.NpcDef
import de.fiereu.openmmo.maps.WildEncounterSlot
import de.fiereu.openmmo.maps.generated.ported.HeadbuttLuts
import de.fiereu.openmmo.net.game.packets.UndergroundTalkPacket
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.PlayerState
import de.fiereu.openmmo.server.game.storage.CharacterStore
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton
import kotlin.random.Random

private val log = KotlinLogging.logger {}

/** A roll of `0 until bound`. A field, so a test can load the dice. */
fun interface Dice {
  fun roll(bound: Int): Int
}

/**
 * What a field move meets on a ported map: the rubble of a smashed rock, and the tree a Headbutt
 * shakes.
 */
@Singleton
class FieldEncounterService
@Inject
constructor(
    private val npcService: NpcService,
    private val mapManager: MapManager,
    private val characterStore: CharacterStore,
    private val battleService: BattleService,
    private val species: SpeciesRegistry,
    private val items: ItemRegistry,
) {

  var dice: Dice = Dice { Random.nextInt(it) }

  suspend fun onPacket(event: PacketEvent<UndergroundTalkPacket>) {
    when (event.packet.kind) {
      UndergroundTalkPacket.KIND_ROCK_SMASH -> onRockSmash(event.session)
      UndergroundTalkPacket.KIND_HEADBUTT -> onHeadbutt(event.session)
      else -> Unit
    }
  }

  /**
   * A rock was smashed. The rock has to be one the map places in front of the player; then the
   * map's rock-smash table gets its roll, and only if that meets nothing does the rubble.
   */
  private fun onRockSmash(session: SessionContext) {
    val state = session.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    val map = mapManager.getMap(state.regionId, state.bankId, state.mapId) ?: return
    val verdict = UndergroundTalkPacket.KIND_ROCK_SMASH_VERDICT
    if (battleService.inBattle(charId)) return answer(session, verdict, NOTHING)
    val rock = faced(session, state, map)
    if (rock == null || !rock.isRock) {
      log.info { "char=$charId smashes a rock the map does not place in front of them" }
      return answer(session, verdict, NOTHING)
    }
    val table = map.encounterTable(EncounterMethod.ROCK_SMASH)
    if (table != null && hasUsablePartyMon(charId) && dice.roll(ROLL_MAX) < table.encounterRate) {
      val slot = pickSlot(table.slots)
      if (slot != null) {
        val level = levelOf(slot)
        log.info {
          "char=$charId smashes a rock on ${map.name} and meets species ${slot.speciesId} level $level"
        }
        battleService.startWildBattle(session, slot.speciesId, level)
        return answer(session, verdict, BATTLE)
      }
    }
    val rubble = map.rubble ?: return answer(session, verdict, NOTHING)
    // The source's own nudges: three abilities raise the odds by five, and two more move the draw
    // one place up the table. A follower using the move raised them too; nobody follows here.
    val ability = leadAbility(charId)
    var odds = rubble.odds
    if (ability in LUCKY_FIND) odds += ABILITY_ODDS
    if (dice.roll(ROLL_MAX) >= odds.coerceAtMost(ROLL_MAX)) {
      log.info {
        "char=$charId smashes a rock on ${map.name}; the rubble held nothing (odds $odds)"
      }
      return answer(session, verdict, NOTHING)
    }
    var index = drawIndex()
    if (ability in LUCKY_DRAW && index < rubble.items.size - 1) index++
    val item = rubble.items[index]
    val id = items.idOf(item)
    log.info { "char=$charId smashes a rock on ${map.name}; ${item.name} was in the rubble" }
    answer(session, verdict, ITEM, id and 0xFF, (id shr 8) and 0xFF)
  }

  /**
   * A tree was headbutted. Which of the map's trees it is says which table it rolls, by the
   * source's rule: a secret tree rolls the secret table for everyone, and a regular one is common,
   * rare or empty for this trainer according to `HeadbuttLuts`.
   */
  private fun onHeadbutt(session: SessionContext) {
    val state = session.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    val map = mapManager.getMap(state.regionId, state.bankId, state.mapId) ?: return
    val verdict = UndergroundTalkPacket.KIND_HEADBUTT_VERDICT
    if (battleService.inBattle(charId)) return answer(session, verdict, NOTHING)
    val facing = state.facingDirection
    val x = state.x + facing.dx
    val y = state.y + facing.dy
    val tree = map.headbuttTrees.firstOrNull { it.x == x && it.y == y }
    if (tree == null) {
      log.info { "char=$charId headbutts ($x, $y) on ${map.name}, a tree nothing lives in" }
      return answer(session, verdict, NOTHING)
    }
    val method =
        when (treeKind(map, tree.tree, tree.secret, trainerDigit(charId))) {
          TREE_COMMON -> EncounterMethod.HEADBUTT_COMMON
          TREE_RARE -> EncounterMethod.HEADBUTT_RARE
          TREE_SECRET -> EncounterMethod.HEADBUTT_SECRET
          else -> {
            log.info { "char=$charId headbutts tree ${tree.tree} on ${map.name}, empty for them" }
            return answer(session, verdict, NOTHING)
          }
        }
    val table = map.encounterTable(method)
    val slot = if (table != null && hasUsablePartyMon(charId)) pickSlot(table.slots) else null
    if (slot == null) return answer(session, verdict, NOTHING)
    val level = levelOf(slot)
    log.info {
      "char=$charId headbutts tree ${tree.tree} ($method) on ${map.name} and meets " +
          "species ${slot.speciesId} level $level"
    }
    battleService.startWildBattle(session, slot.speciesId, level)
    answer(session, verdict, BATTLE)
  }

  /**
   * The source's `Headbutt_GetTreeType_Regular`: the map's count of regular trees picks one of five
   * tables, the trainer id's last digit a row, and the tree's index a column, wrapping at five in
   * the table for five trees and more.
   */
  private fun treeKind(map: MapDef, tree: Int, secret: Boolean, digit: Int): Int {
    if (secret) return TREE_SECRET
    val count = map.headbuttTrees.filter { !it.secret }.distinctBy { it.tree }.size
    val table = HeadbuttLuts.BY_TREE_COUNT[count.coerceAtMost(LUT_MAX_TREES)] ?: return TREE_NONE
    val row = table.getOrNull(digit) ?: return TREE_NONE
    val column = if (count >= LUT_MAX_TREES) tree % LUT_MAX_TREES else tree
    return row.getOrNull(column) ?: TREE_NONE
  }

  /**
   * The source keys its tree tables by the trainer id's last digit, so that two players at the same
   * tree find different things. This server has no trainer id of that kind; the character's own id
   * is as stable and as spread, and its last digit stands in.
   */
  private fun trainerDigit(charId: Long): Int = (charId % 10).toInt()

  private fun faced(session: SessionContext, state: PlayerState, map: MapDef): NpcDef? {
    val facing = state.facingDirection
    return npcService.visibleNpcAt(
        session,
        map.regionId.toInt(),
        map.bankId.toInt(),
        map.mapId.toInt(),
        state.x + facing.dx,
        state.y + facing.dy,
    )
  }

  /**
   * A breakable rock: a ported map marks its rocks by name, and a Sinnoh map places them under this
   * game's own rock sprite (`OBJ_EVENT_GFX_ROCK_SMASH`, the id the field-move menu itself looks
   * for).
   */
  private val NpcDef.isRock: Boolean
    get() = script == ROCK_MARK || graphicsId == ROCK_GFX

  private fun leadAbility(charId: Long): Ability? {
    val lead = characterStore.getCharacter(charId)?.pokemon?.firstOrNull() ?: return null
    if (lead.isEgg) return null
    // The battle draws every monster with its first ability (BattleMonState), and so does this.
    return species.get(lead.dexId)?.ability1
  }

  private fun hasUsablePartyMon(charId: Long): Boolean =
      characterStore.getCharacter(charId)?.pokemon?.any { it.hp > 0 } ?: false

  private fun levelOf(slot: WildEncounterSlot): Int =
      if (slot.maxLevel > slot.minLevel)
          slot.minLevel + dice.roll(slot.maxLevel - slot.minLevel + 1)
      else slot.minLevel

  private fun pickSlot(slots: List<WildEncounterSlot>): WildEncounterSlot? {
    val total = slots.sumOf { it.weight }
    if (total <= 0) return null
    var roll = dice.roll(total)
    for (slot in slots) {
      roll -= slot.weight
      if (roll < 0) return slot
    }
    return slots.lastOrNull()
  }

  /** The source's `DrawRockSmashIdx`: a roll out of a hundred against the fixed thresholds. */
  private fun drawIndex(): Int {
    val roll = dice.roll(ROLL_MAX)
    RUBBLE_DRAW.forEachIndexed { index, threshold -> if (roll < threshold) return index }
    return RUBBLE_DRAW.size
  }

  private fun answer(session: SessionContext, kind: Int, vararg bytes: Int) {
    session.send(UndergroundTalkPacket(kind, 0, ByteArray(bytes.size) { bytes[it].toByte() }))
  }

  companion object {
    const val NOTHING = UndergroundTalkPacket.ROCK_NOTHING
    const val BATTLE = UndergroundTalkPacket.ROCK_BATTLE
    const val ITEM = UndergroundTalkPacket.ROCK_ITEM

    /** The generator's mark on a ported map's rocks, and this game's own rock sprite. */
    const val ROCK_MARK = "rock"
    const val ROCK_GFX = 85

    private const val ROLL_MAX = 100
    private const val ABILITY_ODDS = 5
    private val LUCKY_FIND = setOf(Ability.SUCTION_CUPS, Ability.MAGNET_PULL, Ability.KEEN_EYE)
    private val LUCKY_DRAW = setOf(Ability.SERENE_GRACE, Ability.SUPER_LUCK)
    // `DrawRockSmashIdx`: 25, 20, 10, 10, 10, 10, 10 and 5 in a hundred, first item to last.
    private val RUBBLE_DRAW = listOf(25, 45, 55, 65, 75, 85, 95)

    /** The source's `enum TreeType`, as the generated tables carry it. */
    private const val TREE_NONE = -1
    private const val TREE_COMMON = 0
    private const val TREE_RARE = 1
    private const val TREE_SECRET = 2
    private const val LUT_MAX_TREES = 5
  }
}
