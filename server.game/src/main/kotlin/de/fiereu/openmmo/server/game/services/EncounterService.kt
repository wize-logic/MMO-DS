package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.enums.EncounterMethod
import de.fiereu.openmmo.common.enums.MapType
import de.fiereu.openmmo.common.enums.TileBehavior
import de.fiereu.openmmo.items.ItemRegistry
import de.fiereu.openmmo.items.generated.Items
import de.fiereu.openmmo.maps.MapDef
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.maps.WildEncounterSlot
import de.fiereu.openmmo.maps.WildEncounterTable
import de.fiereu.openmmo.maps.WildExpansion
import de.fiereu.openmmo.net.game.packets.UndergroundTalkPacket
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.PlayerState
import de.fiereu.openmmo.server.game.storage.CharacterStore
import io.github.oshai.kotlinlogging.KotlinLogging
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Singleton
import kotlin.random.Random

private val log = KotlinLogging.logger {}

// The GBA rolls a land encounter out of 2880 with the table rate scaled by 16.
private const val ENCOUNTER_ROLL_MAX = 2880
private const val ENCOUNTER_RATE_SCALE = 16

// A cast's roll is kept until the line is reeled in or lost; a client that never says which is
// holding a fish nobody is fighting, so the hold lapses on its own.
private const val FISHING_HOLD_MS = 30_000L

/** Rolls a wild encounter when a player's step lands somewhere wild, and starts the battle. */
@Singleton
class EncounterService
@Inject
constructor(
    private val characterStore: CharacterStore,
    private val battleService: BattleService,
    private val items: ItemRegistry,
    private val mapManager: MapManager,
    private val variants: EncounterVariantService,
    private val safari: SafariService,
) {

  private val random: Random = Random.Default

  /** A cast's roll, waiting on the reel-in. */
  private data class FishingHold(val speciesId: Int, val level: Int, val until: Long)

  private val holds = ConcurrentHashMap<Long, FishingHold>()

  /** Called after a completed step. Starts a wild battle if the tile and roll call for one. */
  fun onStep(session: SessionContext, charId: Long, map: MapDef, x: Int, y: Int) {
    if (battleService.inBattle(charId)) return
    val tile = map.tileAt(x, y) ?: return
    val method = methodAt(session, map, tile.behavior) ?: return
    val table = map.encounterTable(method) ?: return
    if (!hasUsablePartyMon(charId)) return
    if (!rollsEncounter(table)) return
    val conditions = variants.conditionsFor(characterStore.getCharacter(charId), map)
    val slot = pickSlot(WildExpansion.rolledSlots(map, table, conditions)) ?: return
    val level = random.nextInt(slot.minLevel, slot.maxLevel + 1)
    // In the Great Marsh the encounter is a Safari game's, which is caught out of an allowance of
    // balls rather than fought (SafariService).
    val isSafari = safari.isGreatMarsh(map)
    log.info {
      "Wild encounter for char=$charId at ($x, $y) by $method: species ${slot.speciesId}" +
          " level $level${if (isSafari) " (Safari)" else ""}"
    }
    battleService.startWildBattle(session, slot.speciesId, level, safari = isSafari)
  }

  /** Which table a step at [behavior] rolls, or null for a step that meets nothing. */
  private fun methodAt(
      session: SessionContext,
      map: MapDef,
      behavior: TileBehavior,
  ): EncounterMethod? =
      when {
        behavior == TileBehavior.TALL_GRASS || behavior == TileBehavior.LONG_GRASS ->
            EncounterMethod.LAND
        behavior == TileBehavior.SURFABLE_WATER ->
            if (session.attributes[PLAYER_STATE]?.isSurfing == true) EncounterMethod.WATER else null
        map.mapType == MapType.UNDERGROUND && !map.hasGrass -> EncounterMethod.LAND
        else -> null
      }

  /**
   * The three fishing kinds of the Underground-talk opcode, which is the one bidirectional opcode
   * this server owns with a kind byte to spare.
   */
  fun onFishing(event: PacketEvent<UndergroundTalkPacket>) {
    val session = event.session
    val state = session.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    when (event.packet.kind) {
      UndergroundTalkPacket.KIND_FISHING_CAST ->
          onFishingCast(session, state, charId, event.packet.payload.firstOrNull()?.toInt() ?: 0)
      UndergroundTalkPacket.KIND_FISHING_HOOKED -> onFishingHooked(session, charId)
      UndergroundTalkPacket.KIND_FISHING_LOST -> holds.remove(charId)
      else -> Unit
    }
  }

  /** A session went away or changed maps; a fish on its line is nobody's. */
  fun onLeave(charId: Long) {
    holds.remove(charId)
  }

  /** The rod was cast. */
  private fun onFishingCast(session: SessionContext, state: PlayerState, charId: Long, rod: Int) {
    holds.remove(charId)
    val method =
        when (rod) {
          ROD_OLD -> EncounterMethod.OLD_ROD
          ROD_GOOD -> EncounterMethod.GOOD_ROD
          ROD_SUPER -> EncounterMethod.SUPER_ROD
          else -> {
            log.warn { "char=$charId casts rod $rod, which is not one" }
            return
          }
        }
    if (battleService.inBattle(charId)) return answerCast(session, false)
    val rodItem =
        when (method) {
          EncounterMethod.OLD_ROD -> Items.OLD_ROD
          EncounterMethod.GOOD_ROD -> Items.GOOD_ROD
          else -> Items.SUPER_ROD
        }
    val held =
        items.idsOf(rodItem).any { (characterStore.getCharacter(charId)?.items?.get(it) ?: 0) > 0 }
    if (!held) {
      log.warn { "char=$charId casts a ${rodItem.name} the bag does not hold" }
      return answerCast(session, false)
    }
    val map = mapManager.getMap(state.regionId, state.bankId, state.mapId)
    if (map == null) return answerCast(session, false)
    val facing = state.facingDirection
    val tile = map.tileAt(state.x + facing.dx, state.y + facing.dy)
    if (tile == null || !tile.behavior.isWater) {
      log.info {
        "char=$charId casts at (${state.x + facing.dx}, ${state.y + facing.dy}), which is not water"
      }
      return answerCast(session, false)
    }
    val table = map.encounterTable(method)
    if (table == null) {
      log.info { "char=$charId casts a ${rodItem.name} on ${map.name}, which has no $method table" }
      return answerCast(session, false)
    }
    if (!hasUsablePartyMon(charId)) return answerCast(session, false)
    if (random.nextInt(FISHING_ROLL_MAX) >= table.encounterRate) {
      log.info {
        "char=$charId casts a ${rodItem.name} on ${map.name}: nothing bit (rate ${table.encounterRate})"
      }
      return answerCast(session, false)
    }
    val conditions = variants.conditionsFor(characterStore.getCharacter(charId), map)
    val slot =
        pickSlot(WildExpansion.rolledSlots(map, table, conditions))
            ?: return answerCast(session, false)
    val level = random.nextInt(slot.minLevel, slot.maxLevel + 1)
    holds[charId] = FishingHold(slot.speciesId, level, System.currentTimeMillis() + FISHING_HOLD_MS)
    log.info { "char=$charId hooks species ${slot.speciesId} level $level by $method" }
    answerCast(session, true)
  }

  /** The reel-in landed: the held roll becomes the battle, the same way a step's does. */
  private fun onFishingHooked(session: SessionContext, charId: Long) {
    val hold = holds.remove(charId) ?: return
    if (hold.until < System.currentTimeMillis()) {
      log.info { "char=$charId reels in a fish that was held too long" }
      return
    }
    if (battleService.inBattle(charId)) return
    log.info {
      "Wild encounter for char=$charId by fishing: species ${hold.speciesId} level ${hold.level}"
    }
    battleService.startWildBattle(session, hold.speciesId, hold.level)
  }

  private fun answerCast(session: SessionContext, bite: Boolean) {
    session.send(
        UndergroundTalkPacket(
            UndergroundTalkPacket.KIND_FISHING_VERDICT,
            0,
            byteArrayOf(if (bite) 1 else 0),
        ))
  }

  private fun hasUsablePartyMon(charId: Long): Boolean =
      characterStore.getCharacter(charId)?.pokemon?.any { it.hp > 0 } ?: false

  private fun rollsEncounter(table: WildEncounterTable): Boolean {
    val chance = (table.encounterRate * ENCOUNTER_RATE_SCALE).coerceAtMost(ENCOUNTER_ROLL_MAX)
    return random.nextInt(ENCOUNTER_ROLL_MAX) < chance
  }

  /** Picks a slot weighted by its encounter rate. */
  private fun pickSlot(slots: List<WildEncounterSlot>): WildEncounterSlot? {
    val total = slots.sumOf { it.weight }
    if (total <= 0) return null
    var roll = random.nextInt(total)
    for (slot in slots) {
      roll -= slot.weight
      if (roll < 0) return slot
    }
    return slots.lastOrNull()
  }

  companion object {
    /** The rod byte of a cast: the engine's own `enum EncounterFishingRodType`, plus one. */
    const val ROD_OLD = 1
    const val ROD_GOOD = 2
    const val ROD_SUPER = 3

    // The engine rolls a bite as `LCRNG_RandMod(100) < rate`
    // (`WildEncounters_TryFishingEncounter`).
    private const val FISHING_ROLL_MAX = 100
  }
}
