package de.fiereu.openmmo.server.game.services

import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.DynamicWarp
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.maps.MapDef
import de.fiereu.openmmo.server.game.session.PlayerState
import de.fiereu.openmmo.server.game.session.SCRIPT_SCOPE
import de.fiereu.openmmo.server.game.storage.CharacterStore
import io.github.oshai.kotlinlogging.KotlinLogging
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Singleton
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

private val log = KotlinLogging.logger {}

/**
 * The Great Marsh's Safari game: an allowance of steps and of balls, and nothing else to catch
 * with.
 */
@Singleton
class SafariService
@Inject
constructor(
    private val characterStore: CharacterStore,
    private val scriptWarpService: ScriptWarpService,
) {

  /**
   * Characters this service has seen standing in the marsh, so that walking in starts a game and
   * running out of steps does not start the next one on the following step.
   */
  private val inside = ConcurrentHashMap.newKeySet<Long>()

  /** The six areas of the Great Marsh, which are Sinnoh's headers 504 through 509. */
  fun isGreatMarsh(map: MapDef): Boolean =
      (map.regionId.toInt() and 0xFF) == SINNOH &&
          (map.bankId.toInt() and 0xFF) == MARSH_BANK &&
          (map.mapId.toInt() and 0xFF) in MARSH_MAPS

  /** True while [charId] has an allowance left to spend, which is what makes a marsh step wild. */
  fun playing(charId: Long): Boolean =
      (characterStore.getCharacter(charId)?.info?.remainingSafariSteps ?: 0).toInt() > 0

  /**
   * Accounts one completed step, in the order `Field_UpdateSafari` accounts it: the balls first,
   * then the steps. Returns true when this step must meet nothing, because it ended the game, or
   * because the game is already over and the player has not left yet.
   */
  fun onStep(session: SessionContext, state: PlayerState, map: MapDef): Boolean {
    val charId = state.characterId ?: return false
    if (!isGreatMarsh(map)) {
      // Asked of the record rather than of [inside], because a session that ended in the marsh and
      // came back somewhere else is not in [inside] and still has an allowance written down. Gating
      // the clear on the set left one there, which a live drive found the moment it reconnected.
      inside.remove(charId)
      clear(charId)
      return false
    }
    val info = characterStore.getCharacter(charId)?.info ?: return false
    val entering = inside.add(charId)
    if (entering && info.remainingSafariSteps.toInt() <= 0) {
      characterStore.updateCharacter(
          info.copy(
              remainingSafariSteps = STEP_ALLOWANCE.toShort(),
              remainingSafariBalls = BALL_ALLOWANCE.toByte(),
          ))
      log.info { "char=$charId starts a Safari game on ${map.name}" }
      return false
    }
    if (info.remainingSafariSteps.toInt() <= 0) return true
    if (info.remainingSafariBalls.toInt() <= 0) {
      end(session, state, charId, "You have no Safari Balls left!")
      return true
    }
    val left = info.remainingSafariSteps.toInt() - 1
    characterStore.updateCharacter(info.copy(remainingSafariSteps = left.toShort()))
    if (left > 0) return false
    end(session, state, charId, "Your Safari Game is over!")
    return true
  }

  /**
   * Spends one Safari Ball, or answers false when there is none to spend. The bag is not touched:
   * in the marsh the allowance is the bag, which is why a marsh battle can hand out a monster
   * without a ball ever leaving the player's own pockets.
   */
  fun spendBall(charId: Long): Boolean {
    val info = characterStore.getCharacter(charId)?.info ?: return false
    val left = info.remainingSafariBalls.toInt() - 1
    if (left < 0) return false
    characterStore.updateCharacter(info.copy(remainingSafariBalls = left.toByte()))
    return true
  }

  /** A session went away; the next one starts the count where the record left it. */
  fun onLeave(charId: Long) {
    inside.remove(charId)
  }

  /** Nobody is in the marsh, so nobody has an allowance. */
  private fun clear(charId: Long) {
    val info = characterStore.getCharacter(charId)?.info ?: return
    if (info.remainingSafariSteps.toInt() == 0 && info.remainingSafariBalls.toInt() == 0) return
    characterStore.updateCharacter(info.copy(remainingSafariSteps = 0, remainingSafariBalls = 0))
  }

  /**
   * The four endings of `scripts_safari_game.s` are one ending: a line, a fade, and a warp to the
   * entrance building's doorway facing south. The counters go to zero first, so that a step
   * arriving while the destination is still loading meets nothing.
   */
  private fun end(session: SessionContext, state: PlayerState, charId: Long, line: String) {
    val info = characterStore.getCharacter(charId)?.info ?: return
    characterStore.updateCharacter(info.copy(remainingSafariSteps = 0, remainingSafariBalls = 0))
    log.info { "char=$charId leaves the Safari game: $line" }
    session.send(notice(line))
    val scope =
        session.attributes.getOrPut(SCRIPT_SCOPE) {
          CoroutineScope(SupervisorJob() + Dispatchers.Default)
        }
    scope.launch { scriptWarpService.warp(session, state, EXIT) }
  }

  companion object {
    /** `ScrCmd_StartEndSafariGame`, the SAFARI_GAME_ACTIVE arm. */
    const val BALL_ALLOWANCE = 30

    /** `Field_UpdateSafari` ends the game the step the count reaches this. */
    const val STEP_ALLOWANCE = 500

    private const val SINNOH = 3
    private const val MARSH_BANK = 1
    private val MARSH_MAPS = 248..253

    /**
     * `Warp MAP_HEADER_PASTORIA_CITY_OBSERVATORY_GATE_1F, 5, 2, DIR_SOUTH`, the entrance building,
     * which is Sinnoh bank 0 map 125.
     */
    private val EXIT =
        DynamicWarp(
            regionId = 3.toByte(),
            bankId = 0.toByte(),
            mapId = 125.toByte(),
            x = 5.toShort(),
            y = 2.toShort(),
            facing = Direction.DOWN,
        )
  }
}
