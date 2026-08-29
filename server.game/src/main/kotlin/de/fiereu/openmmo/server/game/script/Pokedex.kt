package de.fiereu.openmmo.server.game.script

import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.net.game.packets.PokedexSpeciesUnlockPacket
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.StoredCharacter

/**
 * Seen and caught species, stored as story flags so they persist with the rest of a character's
 * progress. The wire's pokedex lists are a view of those flags, the same shape as [Badge].
 */
internal object Pokedex {
  fun seenKey(regionName: String, speciesId: Int): String = "$regionName/DEX_SEEN_$speciesId"

  fun caughtKey(regionName: String, speciesId: Int): String = "$regionName/DEX_CAUGHT_$speciesId"

  fun nationalDexKey(regionName: String): String = "$regionName/NATIONAL_DEX_OBTAINED"

  fun leagueVictoriesKey(regionName: String): String = "$regionName/LEAGUE_VICTORIES"

  fun regionName(stored: StoredCharacter): String =
      Region.byWireValue(stored.info.positionRegionId)?.name?.lowercase().orEmpty()

  fun seenSpecies(stored: StoredCharacter): List<Short> =
      speciesList(stored.storyFlags, regionName(stored), "DEX_SEEN_")

  fun caughtSpecies(stored: StoredCharacter): List<Short> =
      speciesList(stored.storyFlags, regionName(stored), "DEX_CAUGHT_")

  fun localSeenCount(stored: StoredCharacter): Int =
      seenSpecies(stored).count { SinnohDex.isLocal(it.toInt()) }

  fun isLocalCompleted(stored: StoredCharacter): Boolean =
      localSeenCount(stored) >= SinnohDex.LOCAL_GOAL

  fun isNationalDexEnabled(stored: StoredCharacter): Boolean =
      nationalDexKey(regionName(stored)) in stored.storyFlags

  fun leagueVictories(stored: StoredCharacter): Int =
      stored.storyVars[leagueVictoriesKey(regionName(stored))] ?: 0

  fun markSeen(
      store: CharacterStore,
      session: SessionContext?,
      characterId: Long,
      speciesId: Int,
  ) {
    if (speciesId <= 0) return
    val stored = store.getCharacter(characterId) ?: return
    val key = seenKey(regionName(stored), speciesId)
    if (key in stored.storyFlags) return
    store.setStoryFlag(characterId, key)
    session?.send(PokedexSpeciesUnlockPacket(speciesId.toShort()))
  }

  fun markCaught(
      store: CharacterStore,
      session: SessionContext?,
      characterId: Long,
      speciesId: Int,
  ) {
    markSeen(store, session, characterId, speciesId)
    val stored = store.getCharacter(characterId) ?: return
    store.setStoryFlag(characterId, caughtKey(regionName(stored), speciesId))
  }

  private fun speciesList(flags: Set<String>, regionName: String, kind: String): List<Short> {
    val prefix = "$regionName/$kind"
    return flags
        .mapNotNull { key ->
          if (!key.startsWith(prefix)) return@mapNotNull null
          key.removePrefix(prefix).toShortOrNull()
        }
        .sorted()
  }
}
