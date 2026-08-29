package de.fiereu.openmmo.server.game.services

import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.CharacterInfo
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.Skin
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.EntityStatus
import de.fiereu.openmmo.common.enums.SkinSlot
import de.fiereu.openmmo.maps.MapDef
import de.fiereu.openmmo.maps.MapManager
import de.fiereu.openmmo.net.game.codecs.SkinSet
import de.fiereu.openmmo.net.game.packets.LoadEntityPacket
import de.fiereu.openmmo.net.game.packets.MapData
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.mapCacheKey
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class MapLoadService
@Inject
constructor(
    private val mapManager: MapManager,
) {

  fun createLoadEntity(
      info: CharacterInfo,
      facing: Direction = Direction.DOWN,
      z: Int = 0,
      party: List<Pokemon> = emptyList(),
      skins: Map<SkinSlot, Skin> = emptyMap(),
      /** What the entity is riding, so a peer who is already surfing spawns on the water. */
      transportation: Byte = 0,
  ): LoadEntityPacket {
    return LoadEntityPacket(
        entityId = info.id,
        gender = info.rivalSex,
        skin = SkinSet(info.skinRegionSelectionIndex, skins),
        name = info.name,
        // DS header ids such as Twinleaf bedroom (415) live in the low byte as 155-160.
        // A signed Byte's toInt() is negative there, and U8 refuses to write it.
        regionId = info.positionRegionId.toInt() and 0xFF,
        bankId = info.positionBankId.toInt() and 0xFF,
        mapId = info.positionMapId.toInt() and 0xFF,
        x = info.positionX.toInt(),
        y = info.positionY.toInt(),
        z = z,
        facing = facing,
        transportation = transportation.toInt() and 0xFF,
        status = EntityStatus.NONE,
        hasFollower = party.isNotEmpty(),
        followerDexId = (party.firstOrNull()?.dexId ?: 0).toShort(),
    )
  }

  /**
   * Forget what the client has cached. Call this alongside a LoadMap that carries deleteCache,
   * since the client throws its own cache away when it sees that flag.
   */
  fun resetClientCache(ctx: SessionContext, map: MapDef) {
    val state = ctx.attributes[PLAYER_STATE] ?: return
    state.loadedMaps.clear()
    state.loadedMaps.add(mapCacheKey(map.regionId.toInt(), map.bankId.toInt(), map.mapId.toInt()))
  }

  fun preloadConnectedMaps(
      ctx: SessionContext,
      map: MapDef,
      depth: Int = 2,
      reloadPlayer: Boolean = false,
  ) {
    val loaded = ctx.attributes[PLAYER_STATE]?.loadedMaps ?: mutableSetOf()
    val regionId = map.regionId.toInt()
    loaded.add(mapCacheKey(regionId, map.bankId.toInt(), map.mapId.toInt()))
    fun preload(connections: List<MapData.GbaConnection>, remaining: Int) {
      if (remaining <= 0) return
      for (conn in connections) {
        val key = mapCacheKey(regionId, conn.targetBank, conn.targetMap)
        if (!loaded.add(key)) continue
        // Connections stay inside one region.
        val connected = mapManager.getMap(regionId, conn.targetBank, conn.targetMap)
        if (connected != null) {
          ctx.send(
              mapManager.createLoadMapPacket(
                  connected,
                  reloadPlayer = reloadPlayer,
                  deleteCache = false,
              ))
          preload(connected.connections, remaining - 1)
        }
      }
    }
    preload(map.connections, depth)
  }
}
