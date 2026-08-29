package de.fiereu.openmmo.server.game.services

import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.maps.MapDef
import de.fiereu.openmmo.net.game.packets.LocalCharacterDeltaPacket
import de.fiereu.openmmo.server.game.script.Script
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
      // A story script is waiting on this battle's result and owns what happens next. Two scripts
      // on one connection would interleave their writes and their rollbacks, so leave it alone.
      log.warn { "char=$charId lost a scripted battle, there is no white out for that path yet" }
      return
    }
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
    scriptRunner
        .get()
        .run(
            session,
            state,
            Script { ctx ->
              if (lost > 0 && characterStore.addMoney(charId, -lost)) {
                ctx.send(
                    LocalCharacterDeltaPacket(
                        money = characterStore.getCharacter(charId)?.info?.money ?: 0))
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
            },
            entityId = -1,
        )
  }
}
