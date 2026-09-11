package de.fiereu.openmmo.server.game.services

import de.fiereu.network.SessionContext
import de.fiereu.openmmo.maps.NpcDef
import de.fiereu.openmmo.server.game.battle.BattleInstance
import de.fiereu.openmmo.server.game.battle.BattleResult
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.PlayerState
import de.fiereu.openmmo.server.game.storage.CharacterStore
import io.github.oshai.kotlinlogging.KotlinLogging
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Singleton
import kotlinx.coroutines.ExperimentalCoroutinesApi

private val log = KotlinLogging.logger {}

/** The wild fights a script stages, in both cartridges. */
@Singleton
class StaticEncounterService
@Inject
constructor(
    private val npcService: NpcService,
    private val battleService: BattleService,
    private val characterStore: CharacterStore,
) {
  /** A site fight the server dealt: the instance, the person, and where they stand. */
  internal class Running(
      val battle: BattleInstance,
      val session: SessionContext,
      val npc: NpcDef?,
      val regionId: Int,
      val bankId: Int,
      val mapId: Int,
      val dexId: Int,
  )

  /** The site fights running, by character; a test reads one to end it. */
  internal val running = ConcurrentHashMap<Long, Running>()

  /**
   * A site's Pokemon this character is fighting for, held until the grant that reports catching it.
   */
  internal class Claim(val dexId: Int, val at: Long = System.currentTimeMillis())

  /**
   * The claims standing, by character. Internal for the same reason [running] is: a test sets one.
   */
  internal val claims = ConcurrentHashMap<Long, Claim>()

  /**
   * Was this character's catch of [dexId] the one from a static site? True once per fight, and then
   * the claim is spent.
   */
  fun claimStaticCatch(charId: Long, dexId: Int, now: Long = System.currentTimeMillis()): Boolean {
    val claim = claims[charId] ?: return false
    if (now - claim.at > CLAIM_WINDOW_MS) {
      claims.remove(charId)
      return false
    }
    if (claim.dexId != dexId) return false
    claims.remove(charId)
    return true
  }

  /**
   * The press on a static site. Answers whether the npc was one; a site already won by this
   * character, or a character already fighting, is answered with nothing.
   */
  fun challenge(
      session: SessionContext,
      state: PlayerState,
      npc: NpcDef,
      regionId: Int,
      bankId: Int,
      mapId: Int,
  ): Boolean = deal(session, state, npc.script, npc, regionId, bankId, mapId)

  /** The press on a site that is not a person: a sign, or the map itself. */
  fun challengeScenery(
      session: SessionContext,
      state: PlayerState,
      script: String,
      regionId: Int,
      bankId: Int,
      mapId: Int,
  ): Boolean = deal(session, state, script, null, regionId, bankId, mapId)

  @OptIn(ExperimentalCoroutinesApi::class)
  private fun deal(
      session: SessionContext,
      state: PlayerState,
      script: String,
      npc: NpcDef?,
      regionId: Int,
      bankId: Int,
      mapId: Int,
  ): Boolean {
    val site = siteOf(script) ?: return false
    val charId = state.characterId ?: return true
    if (battleService.inBattle(charId)) return true
    val hideFlag = npc?.hideFlag.orEmpty()
    if (hideFlag.isNotEmpty() &&
        characterStore.getCharacter(charId)?.storyFlags?.contains(hideFlag) == true) {
      log.info { "char=$charId presses a static site already won ($hideFlag)" }
      return true
    }
    val (dexId, level) = site
    val battle = battleService.startStaticBattle(session, dexId, level)
    if (battle == null) {
      log.warn { "char=$charId presses a static site and no fight would start: $script" }
      return true
    }
    log.info {
      "char=$charId fights the static site on $regionId:$bankId:$mapId: species $dexId level $level"
    }
    val run = Running(battle, session, npc, regionId, bankId, mapId, dexId)
    running[charId] = run
    claims[charId] = Claim(dexId)
    // A wild fight is fought on the client's engine and the instance here only ever hears run
    // at the end of one, so a win arrives as [onWon] before that run; a catch is this server's
    // own doing and ends the instance CAUGHT. Both hide the site; anything else leaves it.
    battle.completion.invokeOnCompletion { cause ->
      running.remove(charId)
      if (cause != null) return@invokeOnCompletion
      val result = battle.completion.getCompleted()
      // The claim outlives a catch and dies with anything else. A fight that was won, fled or lost
      // put no monster in a ball, so nothing after it is a static site's catch.
      if (result == BattleResult.CAUGHT) {
        claims[charId] = Claim(run.dexId)
      } else {
        claims.remove(charId)
      }
      if (result == BattleResult.VICTORY || result == BattleResult.CAUGHT) {
        markDone(charId, run, result.name)
      } else {
        log.info { "char=$charId ends the static site's fight $result" }
      }
    }
    return true
  }

  /**
   * The client says the site's fight ended won, or with the Pokemon caught, on its engine. Only a
   * fight this server dealt and has not seen end counts; the packet names no site, the record does.
   */
  fun onWon(session: SessionContext) {
    val state = session.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    val run = running[charId]
    if (run == null) {
      log.info { "char=$charId reports a static site won with no fight of theirs dealt" }
      return
    }
    markDone(charId, run, "won on the client")
  }

  private fun markDone(charId: Long, run: Running, how: String) {
    val npc = run.npc
    if (npc == null || npc.hideFlag.isEmpty()) {
      log.info { "char=$charId ends the static site's fight $how" }
      return
    }
    if (characterStore.getCharacter(charId)?.storyFlags?.contains(npc.hideFlag) == true) return
    characterStore.setStoryFlag(charId, npc.hideFlag)
    npcService.despawnNpc(run.session, run.regionId, run.bankId, run.mapId, npc.entityIdx)
    log.info { "char=$charId ends the static site's fight $how; ${npc.hideFlag} is set" }
  }

  companion object {
    /**
     * How long a claim stands. Generous, because it covers a whole wild fight and the report that
     * follows it, and bounded because a claim nobody spent must not still be standing when the same
     * species is caught somewhere else hours later.
     */
    const val CLAIM_WINDOW_MS = 30 * 60 * 1000L

    /** The codegen's mark on a static site's script: `static:<species>:<level>`. */
    const val MARK = "static:"

    /** The species and level a mark names, or null for a script that is not one. */
    fun siteOf(script: String): Pair<Int, Int>? {
      if (!script.startsWith(MARK)) return null
      val parts = script.removePrefix(MARK).split(':')
      if (parts.size != 2) return null
      val dexId = parts[0].toIntOrNull() ?: return null
      val level = parts[1].toIntOrNull() ?: return null
      return dexId to level
    }
  }
}
