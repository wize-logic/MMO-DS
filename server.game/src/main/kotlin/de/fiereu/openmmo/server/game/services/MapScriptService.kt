package de.fiereu.openmmo.server.game.services

import de.fiereu.network.SessionContext
import de.fiereu.openmmo.maps.MapDef
import de.fiereu.openmmo.server.game.script.Script
import de.fiereu.openmmo.server.game.script.ScriptRunner
import de.fiereu.openmmo.server.game.session.CLIENT_RUNS_SCRIPTS
import de.fiereu.openmmo.server.game.session.PlayerState
import javax.inject.Inject
import javax.inject.Singleton

/**
 * Runs a map's scripts when a player enters it. The decomp ON_TRANSITION script fires on every
 * entry however the player got there (login, a warp, or walking across a map connection), followed
 * by the first ON_FRAME entry whose story var matches.
 */
@Singleton
class MapScriptService
@Inject
constructor(
    private val entryScripts: MapEntryScripts,
    private val scriptRunner: ScriptRunner,
    private val blackoutService: BlackoutService,
    private val fieldMoveService: FieldMoveService,
) {
  private fun clientOwnsThem(session: SessionContext): Boolean =
      session.attributes[CLIENT_RUNS_SCRIPTS] == true

  fun onMapEnter(session: SessionContext, state: PlayerState, map: MapDef) {
    // Strength and this visit's boulder positions die with the map, the way the decomp
    // clears FLAG_STRENGTH_ACTIVE in field_map_change_flags.c. Runs before the early
    // return so a scripted warp still forgets a push.
    fieldMoveService.onMapChange(session, state)

    // The map's setrespawn is data rather than a ported script, and it is the one part of an
    // ON_TRANSITION that still has to happen when a cutscene owns the arrival.
    blackoutService.onMapEnter(state, map)

    if (clientOwnsThem(session)) return
    // A script is already running for this player, do not start a second one on top of it.
    if (state.scriptOwnsMapEntry || state.inDialog) return
    val charId = state.characterId
    val entry = entryScripts.onEntry(state, map)
    val hasArrivalTrigger = entryScripts.hasCoordinate(map, state.x.toInt(), state.y.toInt())
    if (entry.isEmpty() && !hasArrivalTrigger) return

    // Entry scripts may trigger their landing coordinate.
    val entrySequence = Script { ctx ->
      entry.forEach { it.run(ctx) }
      if (charId != null) {
        entryScripts.atCoordinate(charId, map, state.x.toInt(), state.y.toInt())?.run(ctx)
      }
    }
    scriptRunner.run(session, state, entrySequence, entityId = -1)
  }

  /** Run the matching conditional coordinate script after a completed player step. */
  fun onStep(
      session: SessionContext,
      state: PlayerState,
      map: MapDef,
      x: Int,
      y: Int,
  ): Boolean {
    if (clientOwnsThem(session)) return false
    if (state.inDialog) return false
    val charId = state.characterId ?: return false
    val script = entryScripts.atCoordinate(charId, map, x, y) ?: return false
    scriptRunner.run(session, state, script, entityId = -1)
    return true
  }
}
