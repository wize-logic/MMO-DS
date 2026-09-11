package de.fiereu.openmmo.server.game.offline

import de.fiereu.openmmo.server.game.script.Badge
import de.fiereu.openmmo.server.game.script.Pokedex
import de.fiereu.openmmo.server.game.services.VmStoryKeys

/** The two translations between what a client can say about a save and what this server stores. */
internal object OfflineImportRequests {

  /** The report as a save to check, keyed under [regionId]'s own prefix. */
  fun toSave(wire: OfflineSaveWire, regionId: Byte): OfflineSave =
      OfflineSave(
          trainerId = wire.trainerId,
          monsters = wire.monsters,
          money = wire.money,
          bag = wire.bag,
          badges = wire.badges,
          dexSeen = wire.dexSeen,
          dexCaught = wire.dexCaught,
          position =
              OfflinePosition(
                  regionId = regionId.toInt(),
                  bankId = wire.position.bankId,
                  mapId = wire.position.mapId,
                  x = wire.position.x,
                  y = wire.position.y,
              ),
          blackOutWarpId = wire.blackOutWarpId,
          playTimeSeconds = wire.playTimeSeconds,
          storyFlags = wire.flagIds.map { VmStoryKeys.flag(regionId, it) }.toSet(),
          storyVars =
              wire.varIds
                  .filter { it.second != 0 }
                  .associate { VmStoryKeys.variable(regionId, it.first) to it.second },
          blocks = wire.blocks,
      )

  /** The checked badges and Pokedex as story flags, which is where this server keeps them. */
  fun OfflineSave.withEarnedKeys(regionName: String): OfflineSave {
    val earned = mutableSetOf<String>()
    Badge.entries.take(Badge.PER_REGION).forEachIndexed { bit, badge ->
      if (badges and (1 shl bit) != 0) earned += badge.keyIn(regionName)
    }
    dexSeen.forEach { earned += Pokedex.seenKey(regionName, it) }
    dexCaught.forEach { earned += Pokedex.caughtKey(regionName, it) }
    return copy(storyFlags = storyFlags + earned)
  }
}
