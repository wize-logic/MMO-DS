package de.fiereu.openmmo.server.game.services

import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.MovementType
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.maps.NpcDef
import de.fiereu.openmmo.net.game.packets.EntityLeavePacket
import de.fiereu.openmmo.net.game.packets.NpcSpawnPacket
import de.fiereu.openmmo.net.game.packets.NpcUpdatePacket
import de.fiereu.openmmo.net.game.packets.StoryFlagUpdatePacket
import de.fiereu.openmmo.server.game.session.CLIENT_RUNS_SCRIPTS
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.PartnerFollow
import de.fiereu.openmmo.server.game.session.PlayerState
import de.fiereu.openmmo.server.game.storage.CharacterStore
import io.github.oshai.kotlinlogging.KotlinLogging
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicLong
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

@Singleton
class NpcService
@Inject
constructor(
    private val mapManager: MapManager,
    private val characterStore: CharacterStore,
) {

  private val npcEntityIdCounter = AtomicLong(0x1A69000000000000L)
  // Allocated from whichever session first walks onto the map, so both the map and the allocation
  // have to be atomic: two players arriving at once must be told the same id for the same npc, and
  // getOrPut over a plain map is a read and a write with a gap between them.
  private val npcEntityIds = ConcurrentHashMap<String, Long>()

  fun getNpcEntityId(regionId: Int, bankId: Int, mapId: Int, entityIdx: Int): Long? {
    return npcEntityIds[key(regionId, bankId, mapId, entityIdx)]
  }

  /** Spawn a map's own people for this client. */
  fun spawnNpcsForMap(ctx: SessionContext, bankId: Int, mapId: Int, regionId: Int) {
    if (ctx.attributes[CLIENT_RUNS_SCRIPTS] == true) return
    val map = mapManager.getMap(regionId, bankId, mapId) ?: return
    val stored = ctx.attributes[PLAYER_STATE]?.characterId?.let(characterStore::getCharacter)
    val storyFlags = stored?.storyFlags.orEmpty()
    val storyVars = stored?.storyVars.orEmpty()

    for (npc in map.npcs) {
      // Decoration slots are not normal NPCs.
      if (npc.hideFlag.substringAfter('/').startsWith(DECORATION_FLAG_PREFIX)) continue

      // Only set hide flags suppress NPCs.
      if (shouldHideNpc(bankId, mapId, npc, storyFlags)) continue
      val resolved =
          resolveDynamicGraphics(
              ctx, applyStoryPlacement(bankId, mapId, npc, storyFlags, storyVars))
      ctx.send(
          buildSpawnPacket(
              resolved,
              entityIdFor(regionId, bankId, mapId, npc.entityIdx),
              regionId,
              bankId,
              mapId,
          ))
    }
    spawnPartnerIfFollowing(ctx, regionId, bankId, mapId)
  }

  /**
   * The decomp's SetHasPartner plus SetMovementType FOLLOW_PLAYER. The flag is 0x2A with
   * [FLAG_HAS_PARTNER]; the body is a 0x12 whose movement id is 48.
   */
  fun setHasPartner(ctx: SessionContext, regionId: Int, bankId: Int, mapId: Int, localId: Int) {
    val state = ctx.attributes[PLAYER_STATE] ?: return
    val npc = findNpc(regionId, bankId, mapId, localId) ?: return
    val resolved = resolveDynamicGraphics(ctx, npc)
    val entityId = entityIdFor(regionId, bankId, mapId, localId)
    state.partner = PartnerFollow(entityId, resolved)
    ctx.send(StoryFlagUpdatePacket(SINNOH_REGION, FLAG_HAS_PARTNER, true))
    ctx.send(
        buildSpawnPacket(
            resolved.copy(rawMovementId = MOVEMENT_FOLLOW_PLAYER),
            entityId,
            regionId,
            bankId,
            mapId,
        ))
  }

  /** The decomp's ClearHasPartner. Restores the snapshot's own movement type. */
  fun clearHasPartner(ctx: SessionContext, regionId: Int, bankId: Int, mapId: Int) {
    val state = ctx.attributes[PLAYER_STATE] ?: return
    val partner = state.partner ?: return
    state.partner = null
    ctx.send(StoryFlagUpdatePacket(SINNOH_REGION, FLAG_HAS_PARTNER, false))
    ctx.send(
        buildSpawnPacket(
            partner.npc,
            partner.entityId,
            regionId,
            bankId,
            mapId,
        ))
  }

  /** The npc this player can see standing on a tile, or null when there is none. */
  fun visibleNpcAt(
      ctx: SessionContext,
      regionId: Int,
      bankId: Int,
      mapId: Int,
      x: Int,
      y: Int,
  ): NpcDef? {
    val map = mapManager.getMap(regionId, bankId, mapId) ?: return null
    val state = ctx.attributes[PLAYER_STATE]
    val stored = state?.characterId?.let(characterStore::getCharacter)
    val storyFlags = stored?.storyFlags.orEmpty()
    val storyVars = stored?.storyVars.orEmpty()
    return map.npcs
        .asSequence()
        .filterNot { it.hideFlag.substringAfter('/').startsWith(DECORATION_FLAG_PREFIX) }
        .filterNot { shouldHideNpc(bankId, mapId, it, storyFlags) }
        .map { applyStoryPlacement(bankId, mapId, it, storyFlags, storyVars) }
        .map { applyBoulderPlacement(it, state) }
        .firstOrNull { it.x == x && it.y == y }
  }

  /** Allocate (or return) the stable entity id for a map npc by its decomp local id. */
  fun entityIdFor(regionId: Int, bankId: Int, mapId: Int, entityIdx: Int): Long =
      npcEntityIds.computeIfAbsent(key(regionId, bankId, mapId, entityIdx)) {
        npcEntityIdCounter.incrementAndGet()
      }

  /** Spawn a single npc (including a normally hidden one) for one player, for cutscenes. */
  fun spawnNpc(ctx: SessionContext, regionId: Int, bankId: Int, mapId: Int, localId: Int) {
    val npc = findNpc(regionId, bankId, mapId, localId) ?: return
    ctx.send(
        buildSpawnPacket(
            resolveDynamicGraphics(ctx, npc),
            entityIdFor(regionId, bankId, mapId, localId),
            regionId,
            bankId,
            mapId,
        ))
  }

  /** Spawns an NPC at a cutscene position. */
  fun spawnNpcAt(
      ctx: SessionContext,
      regionId: Int,
      bankId: Int,
      mapId: Int,
      localId: Int,
      x: Int,
      y: Int,
  ) {
    val npc = findNpc(regionId, bankId, mapId, localId) ?: return
    ctx.send(
        buildSpawnPacket(
            resolveDynamicGraphics(ctx, npc.copy(x = x, y = y)),
            entityIdFor(regionId, bankId, mapId, localId),
            regionId,
            bankId,
            mapId,
        ))
  }

  /** Repositions an existing NPC. */
  fun repositionNpc(
      ctx: SessionContext,
      regionId: Int,
      bankId: Int,
      mapId: Int,
      localId: Int,
      x: Int,
      y: Int,
  ) {
    val npc = findNpc(regionId, bankId, mapId, localId) ?: return
    ctx.send(
        NpcUpdatePacket(
            entityId = entityIdFor(regionId, bankId, mapId, localId),
            regionId = regionId and 0xFF,
            bankId = bankId and 0xFF,
            mapId = mapId and 0xFF,
            x = x,
            y = y,
            // Captures use 0xF6 as the move-speed byte for this update packet.
            movementMode = 0xF6,
            heading = npc.facing.ordinal,
        ))
  }

  /** Removes a cutscene NPC. */
  fun despawnNpc(ctx: SessionContext, regionId: Int, bankId: Int, mapId: Int, localId: Int) {
    val entityId = getNpcEntityId(regionId, bankId, mapId, localId) ?: return
    val state = ctx.attributes[PLAYER_STATE]
    if (state?.partner?.entityId == entityId) {
      state.partner = null
      ctx.send(StoryFlagUpdatePacket(SINNOH_REGION, FLAG_HAS_PARTNER, false))
    }
    ctx.send(EntityLeavePacket(entityId))
  }

  private fun spawnPartnerIfFollowing(
      ctx: SessionContext,
      regionId: Int,
      bankId: Int,
      mapId: Int,
  ) {
    val state = ctx.attributes[PLAYER_STATE] ?: return
    val partner = state.partner ?: return
    ctx.send(
        buildSpawnPacket(
            partner.npc.copy(
                x = state.x.toInt(),
                y = state.y.toInt(),
                rawMovementId = MOVEMENT_FOLLOW_PLAYER,
            ),
            partner.entityId,
            regionId,
            bankId,
            mapId,
        ))
  }

  private fun findNpc(regionId: Int, bankId: Int, mapId: Int, localId: Int): NpcDef? {
    val npc =
        mapManager.getMap(regionId, bankId, mapId)?.npcs?.firstOrNull { it.entityIdx == localId }
    if (npc == null) log.warn { "npc $localId not found on $regionId:$bankId:$mapId" }
    return npc
  }

  private fun key(regionId: Int, bankId: Int, mapId: Int, entityIdx: Int) =
      "$regionId:$bankId:$mapId:$entityIdx"

  private fun buildSpawnPacket(
      npc: NpcDef,
      entityId: Long,
      regionId: Int,
      bankId: Int,
      mapId: Int,
  ): NpcSpawnPacket {
    val region = requireNotNull(Region.byId(regionId)) { "Unknown region id $regionId" }
    // A DS map's movement ids are its own game's, and the def carries them verbatim rather than
    // borrowing the GBA numbering the normalized type maps to.
    val movementId =
        if (npc.rawMovementId >= 0) npc.rawMovementId else npc.movementType.forRegion(region).id
    val unk3 = ((movementId and 0xFF) shl 8) or 0x02
    val unk4 =
        if (movementId in 1..6 || (movementId in 25..52)) {
          ((npc.movementRangeX and 0xFF) shl 8) or (npc.movementRangeY and 0xFF)
        } else {
          0
        }
    return NpcSpawnPacket(
        entityId = entityId,
        spriteRegionId = regionId and 0xFF,
        graphicsId = npc.graphicsId,
        unk3 = unk3,
        unk4 = unk4,
        regionId = regionId and 0xFF,
        bankId = bankId and 0xFF,
        mapId = mapId and 0xFF,
        x = npc.x,
        y = npc.y,
        facing = npc.facing.ordinal,
        unk5 = 2,
        unk6 = 8,
    )
  }

  private fun resolveDynamicGraphics(ctx: SessionContext, npc: NpcDef): NpcDef {
    if (npc.graphicsId != DYNAMIC_GFX_VAR_0 ||
        !(npc.script.contains("Rival", ignoreCase = true) ||
            npc.hideFlag.contains("RIVAL", ignoreCase = true))) {
      return npc
    }
    val playerGender =
        ctx.attributes[PLAYER_STATE]?.characterId?.let(characterStore::getCharacter)?.info?.rivalSex
    // The rival is the opposite gender from the player.
    val graphicsId = if (playerGender == FEMALE) RIVAL_BRENDAN_NORMAL else RIVAL_MAY_NORMAL
    return npc.copy(graphicsId = graphicsId)
  }

  /** Where story state says an npc stands, which is not always where its map data puts it. */
  private fun applyStoryPlacement(
      bankId: Int,
      mapId: Int,
      npc: NpcDef,
      storyFlags: Set<String>,
      storyVars: Map<String, Int>,
  ): NpcDef {
    // Twinleaf Town, coming home from the first battle: mom waits by the stairs, not in the
    // kitchen.
    if (bankId == TWINLEAF_1F_BANK &&
        (mapId and 0xFF) == TWINLEAF_1F_MAP &&
        npc.entityIdx == TWINLEAF_1F_MOM &&
        storyVars[TWINLEAF_HOUSE_STATE_VAR].orZero() == TWINLEAF_HOUSE_STATE_AFTER_BATTLE) {
      return npc.copy(x = 2, y = 4, facing = Direction.UP, movementType = MovementType.FACE_UP)
    }
    if (bankId != LITTLEROOT_BANK || mapId != LITTLEROOT_MAP || npc.entityIdx != LITTLEROOT_TWIN) {
      return npc
    }
    if (RESCUED_BIRCH_FLAG in storyFlags) return npc
    return if (storyVars[LITTLEROOT_STATE_VAR].orZero() == 0) {
      npc.copy(x = 7, y = 2, facing = Direction.DOWN, movementType = MovementType.FACE_DOWN)
    } else {
      npc.copy(x = 10, y = 1, facing = Direction.UP, movementType = MovementType.FACE_UP)
    }
  }

  /**
   * A Strength boulder sits where this visit pushed it, not where the map still writes it. The
   * A-press has to agree with [de.fiereu.openmmo.server.game.services.FieldMoveService.boulderAt]
   * or a pushed rock can only be talked to on the tile it left.
   */
  private fun applyBoulderPlacement(npc: NpcDef, state: PlayerState?): NpcDef {
    if (state == null || npc.script != STRENGTH_BOULDER_SCRIPT) return npc
    val pos = state.boulderTiles[npc.entityIdx] ?: return npc
    return npc.copy(x = pos.first, y = pos.second)
  }

  private fun Int?.orZero(): Int = this ?: 0

  private fun shouldHideNpc(
      bankId: Int,
      mapId: Int,
      npc: NpcDef,
      storyFlags: Set<String>,
  ): Boolean {
    if (npc.hideFlag in storyFlags) return true
    // Completed rescues must not restore Zigzagoon.
    return bankId == ROUTE_101_BANK &&
        mapId == ROUTE_101_MAP &&
        npc.entityIdx == ROUTE_101_ZIGZAGOON &&
        (RESCUED_BIRCH_FLAG in storyFlags || ROUTE_101_RESCUE_HIDDEN_FLAG in storyFlags)
  }

  private companion object {
    const val DECORATION_FLAG_PREFIX = "FLAG_DECORATION_"
    const val STRENGTH_BOULDER_SCRIPT = "10002"
    const val DYNAMIC_GFX_VAR_0 = 240
    const val RIVAL_BRENDAN_NORMAL = 100
    const val RIVAL_MAY_NORMAL = 105
    const val FEMALE: Byte = 1
    const val LITTLEROOT_BANK = 50
    const val LITTLEROOT_MAP = 9
    const val LITTLEROOT_TWIN = 0
    const val RESCUED_BIRCH_FLAG = "hoenn/FLAG_RESCUED_BIRCH"
    const val LITTLEROOT_STATE_VAR = "hoenn/VAR_LITTLEROOT_TOWN_STATE"
    const val ROUTE_101_BANK = 50
    const val ROUTE_101_MAP = 16
    const val ROUTE_101_ZIGZAGOON = 3
    const val ROUTE_101_RESCUE_HIDDEN_FLAG = "hoenn/FLAG_HIDE_ROUTE_101_BIRCH_ZIGZAGOON_BATTLE"
    const val TWINLEAF_1F_BANK = 1
    const val TWINLEAF_1F_MAP = 158
    const val TWINLEAF_1F_MOM = 0
    const val TWINLEAF_HOUSE_STATE_VAR = "sinnoh/VAR_PLAYER_HOUSE_STATE"
    const val TWINLEAF_HOUSE_STATE_AFTER_BATTLE = 3
    const val SINNOH_REGION: Byte = 3
    const val FLAG_HAS_PARTNER = 2408
    const val MOVEMENT_FOLLOW_PLAYER = 48
  }
}
