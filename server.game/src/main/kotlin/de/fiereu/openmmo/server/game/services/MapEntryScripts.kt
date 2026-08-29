package de.fiereu.openmmo.server.game.services

import de.fiereu.openmmo.maps.MapDef
import de.fiereu.openmmo.server.game.script.Script
import de.fiereu.openmmo.server.game.script.ScriptRegistry
import de.fiereu.openmmo.server.game.session.PlayerState
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/**
 * Works out which scripts a map runs when a player arrives on it: the ON_TRANSITION script,
 * then the first ON_FRAME entry whose story var matches, then the coordinate trigger the
 * player landed on.
 */
@Singleton
class MapEntryScripts
@Inject
constructor(
    private val scriptRegistry: ScriptRegistry,
    private val storyService: StoryService,
) {
  fun onEntry(state: PlayerState, map: MapDef): List<Script> {
    val charId = state.characterId
    return buildList {
      resolve(map, map.onTransitionScript)?.let { add(it) }
      if (charId != null) {
        map.onFrameScripts
            .firstOrNull { storyService.getVar(charId, it.varKey) == it.value }
            ?.let { resolve(map, it.script) }
            ?.let { add(it) }
      }
    }
  }

  /** The conditional coordinate trigger covering one tile, the decomp coord_events table. */
  fun atCoordinate(charId: Long, map: MapDef, x: Int, y: Int): Script? {
    val trigger =
        map.coordScripts.firstOrNull {
          it.covers(x, y) && storyService.getVar(charId, it.varKey) == it.value
        } ?: return null
    return resolve(map, trigger.script)
  }

  /** True when the tile has a trigger at all, whatever its var currently says. */
  fun hasCoordinate(map: MapDef, x: Int, y: Int): Boolean = map.coordScripts.any { it.covers(x, y) }

  /**
   * A Sinnoh map's entry scripts are numeric ids that only mean anything on their own map, so
   * the lookup has to be keyed by the map the way [InteractionService] already keys its npc
   * and sign scripts.
   */
  private fun resolve(map: MapDef, label: String): Script? {
    val script =
        scriptRegistry.forMap(map.regionId.toInt(), map.bankId.toInt(), map.mapId.toInt(), label)
    if (script == null && label.isNotEmpty()) {
      // Named, and at the level a playthrough reads its front from: a map whose arrival script is
      // missing is a story that stops here, and a debug line is a story that stops silently.
      log.info {
        "NO SCRIPT: map ${map.regionId}:${map.bankId}:${map.mapId} entry $label is not ported"
      }
    }
    return script
  }
}
