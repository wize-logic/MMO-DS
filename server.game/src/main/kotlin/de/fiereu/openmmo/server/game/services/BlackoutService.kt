package de.fiereu.openmmo.server.game.services

import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.maps.MapDef
import de.fiereu.openmmo.net.game.packets.LocalCharacterDeltaPacket
import de.fiereu.openmmo.server.game.script.Script
import de.fiereu.openmmo.server.game.script.ScriptContext
import de.fiereu.openmmo.server.game.script.ScriptRunner
import de.fiereu.openmmo.server.game.session.PlayerState
import de.fiereu.openmmo.server.game.storage.CharacterStore
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Provider
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/** Losing every party member. */
@Singleton
class BlackoutService
@Inject
constructor(
    private val characterStore: CharacterStore,
    private val storyPlayer: StoryPlayerService,
    // Deferred: the script runner reaches back into the battle service that ends the battle here.
    private val scriptRunner: Provider<ScriptRunner>,
) {

  /**
   * The decomp `setrespawn`, which sits in the ON_TRANSITION script of every map that sets one.
   * Applied from the map's own data rather than from a ported script, so entering a Pokémon Center
   * moves the respawn whether or not that map's scripts are ported.
   */
  fun onMapEnter(state: PlayerState, map: MapDef) {
    val charId = state.characterId ?: return
    val location = map.healLocation ?: return
    characterStore.setHealLocation(charId, location)
  }

  /** Runs the white out for a character whose party has just been wiped out. */
  fun blackOut(session: SessionContext, state: PlayerState) {
    val charId = state.characterId ?: return
    if (state.inDialog) {
      // The cartridge runs the white out inside the script task that staged the battle
      // (`ScrCmd_BlackOutFromBattle` calls it on `ctx->task`), not beside it.
      state.pendingBlackOut = true
      return
    }
    if (!session.channel.isActive) {
      // Nothing can be played to a connection that has gone, and a script launched on it would sit
      // out the map load it waits for while the character is written and evicted underneath it.
      blackOutOffline(charId)
      return
    }
    scriptRunner.get().run(session, state, Script { ctx -> whiteOut(ctx, charId) }, entityId = -1)
  }

  /**
   * The white out a scripted battle left owed, run by [ScriptRunner] on the coroutine that owned
   * the connection, once its script has ended. It asks again whether the party is still wiped,
   * because the script between the defeat and here may have healed it.
   */
  suspend fun runDeferred(ctx: ScriptContext, state: PlayerState) {
    if (!state.pendingBlackOut) return
    state.pendingBlackOut = false
    val charId = state.characterId ?: return
    val stored = characterStore.getCharacter(charId) ?: return
    if (stored.pokemon.any { !it.isEgg && it.hp > 0 }) {
      log.info { "char=$charId came out of its scene able to battle, no white out" }
      return
    }
    whiteOut(ctx, charId)
  }

  /** The white out itself, on a script scope that is already claimed. */
  private suspend fun whiteOut(ctx: ScriptContext, charId: Long) {
    val stored = characterStore.getCharacter(charId) ?: return
    val destination = stored.info.lastHealLocation
    if (destination == null) {
      // Every character gets one at creation and every Pokémon Center sets one, so this is a
      // character the server has no way to put anywhere: say so instead of leaving it fainted.
      log.error { "char=$charId blacked out with no respawn set, it stays where it fell" }
      return
    }
    val lost = stored.info.money - stored.info.money / 2
    log.info { "char=$charId blacked out, ${destination.bankId}:${destination.mapId}" }
    if (lost > 0 && characterStore.addMoney(charId, -lost)) {
      ctx.send(
          LocalCharacterDeltaPacket(money = characterStore.getCharacter(charId)?.info?.money ?: 0))
    }
    ctx.healParty()
    ctx.warp(
        destination.regionId.toInt(),
        destination.bankId.toInt(),
        destination.mapId.toInt(),
        destination.x.toInt(),
        destination.y.toInt(),
        // The decomp resets the avatar to face south rather than reading a facing.
        Direction.DOWN,
    )
  }

  /**
   * The white out for a player whose connection has already gone: the same halved money, healed
   * party and respawn, written straight into the character rather than played to nobody.
   */
  private fun blackOutOffline(charId: Long) {
    val stored = characterStore.getCharacter(charId) ?: return
    val destination = stored.info.lastHealLocation
    if (destination == null) {
      log.error { "char=$charId blacked out with no respawn set, it stays where it fell" }
      return
    }
    log.info {
      "char=$charId blacked out as its connection went, ${destination.bankId}:${destination.mapId}"
    }
    storyPlayer.healPartyStored(charId)
    characterStore.updateCharacter(
        stored.info.copy(
            money = stored.info.money / 2,
            positionRegionId = destination.regionId,
            positionBankId = destination.bankId,
            positionMapId = destination.mapId,
            positionX = destination.x,
            positionY = destination.y,
            positionFacing = Direction.DOWN,
        ))
  }
}
