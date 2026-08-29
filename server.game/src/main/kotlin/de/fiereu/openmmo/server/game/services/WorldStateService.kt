package de.fiereu.openmmo.server.game.services

import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.common.utils.hexToBytes
import de.fiereu.openmmo.net.game.packets.LocalPlayerStatePacket
import de.fiereu.openmmo.net.game.packets.PokemonContainerPacket
import de.fiereu.openmmo.net.game.packets.RegisteredItemPacket
import de.fiereu.openmmo.net.game.packets.WorldFlagTableResetPacket
import de.fiereu.openmmo.server.game.script.Badge
import de.fiereu.openmmo.server.game.script.Pokedex
import de.fiereu.openmmo.server.game.storage.StoredCharacter
import javax.inject.Inject
import javax.inject.Singleton

// The world-flag table the client loads on join. Groups one to three are zlib streams that each
// decompress to a 67-byte flag block, the fourth is empty. The client reads real flag state while
// building the follower and party, so empty groups leave a lookup null and crash it.
private val WORLD_FLAG_GROUPS =
    listOf(
            "789c637060a00434a8b0320000133900ea",
            "789c637060a00434303032000012c500c2",
            "789c637060a00434303032000012c500c2",
            "",
        )
        .map(String::hexToBytes)

/**
 * Sends the client its story, party and bag state. Story vars have no incremental packet, so
 * anything that rewrites them has to send this whole block again.
 */
@Singleton
class WorldStateService @Inject constructor() {

  /**
   * Set [fullVars] when this is a resync rather than a login, so vars that dropped back to 0 are
   * sent as 0 instead of being left off and read as their old value.
   */
  fun send(
      ctx: SessionContext,
      stored: StoredCharacter,
      fullVars: Boolean = false,
      saveBlocks: Map<Int, ByteArray> = emptyMap(),
  ) {
    // The table must land before any monster or follower is built. Without it the table stays null
    // and the client crashes constructing a party monster that reads a flag.
    ctx.send(WorldFlagTableResetPacket(WORLD_FLAG_GROUPS))
    ctx.send(localPlayerState(stored, fullVars))
    StoryClientState.flags(stored.info.positionRegionId, stored.storyFlags).forEach { ctx.send(it) }
    // The client's own script VM reads and writes the engine's VarsFlags block, so seat it here,
    // ahead of the map: the scene the engine builds on arrival is the one this progress earned.
    ctx.send(
        StoryClientState.scriptState(
            stored.info.positionRegionId, stored.storyFlags, stored.storyVars, saveBlocks))

    val containers =
        mapOf(
            PokemonContainer.PARTY to stored.pokemon,
            PokemonContainer.PC to stored.pcStorage,
            PokemonContainer.BATTLE_BOX_1 to emptyList(),
            PokemonContainer.BATTLE_BOX_2 to emptyList(),
            PokemonContainer.DAYCARE to emptyList(),
            PokemonContainer.UNKNOWN_13 to emptyList(),
            PokemonContainer.UNKNOWN_14 to emptyList(),
        )
    for ((container, pokemon) in containers) {
      ctx.send(
          PokemonContainerPacket(
              container = container,
              hasChange = true,
              delete = false,
              pokemon = pokemon,
          ))
    }

    // The real server sends the bag stacks interleaved with the containers, so the client has the
    // items before entering the world.
    ctx.send(storyItemStacksPacket(stored.items))

    // The registered key item rides behind the bag it points into. Absolute like the script
    // seat: sent even when it is none, so a client holding a stale register clears it.
    ctx.send(RegisteredItemPacket(stored.info.registeredItem.toInt()))
  }

  /** The badges this character has earned, as the wire's own list of badge ids. */
  private fun earnedBadges(stored: StoredCharacter): List<Short> {
    val region =
        Region.byWireValue(stored.info.positionRegionId)?.name?.lowercase() ?: return emptyList()
    return Badge.entries.filter { it.keyIn(region) in stored.storyFlags }.map(Badge::wireId)
  }

  // Missing it leaves player state uninitialised and the client crashes reading it, for example
  // when opening the battle bag.
  private fun localPlayerState(
      stored: StoredCharacter,
      fullVars: Boolean,
  ): LocalPlayerStatePacket {
    val info = stored.info
    val partyDex = stored.pokemon.map { it.dexId.toShort() }
    return LocalPlayerStatePacket(
        region = info.positionRegionId,
        mapId = info.positionMapId.toShort(),
        moveSpeed = 0.05f,
        x = info.positionX,
        y = info.positionY,
        z = 0,
        money = info.money,
        gender = info.rivalSex,
        skinTone = 0,
        hairColor = 0,
        playtime = 0.0,
        flags = 0,
        partyDex = partyDex,
        partyForms = partyDex.map { 0.toByte() },
        pokedexSeen = Pokedex.seenSpecies(stored),
        pokedexCaught = Pokedex.caughtSpecies(stored),
        badges = earnedBadges(stored),
        variables =
            if (fullVars) StoryClientState.allVariables(info.positionRegionId, stored.storyVars)
            else StoryClientState.variables(info.positionRegionId, stored.storyVars),
    )
  }
}
