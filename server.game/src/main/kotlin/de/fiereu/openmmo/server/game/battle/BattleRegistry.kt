package de.fiereu.openmmo.server.game.battle

import de.fiereu.network.SessionContext
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicLong
import javax.inject.Inject
import javax.inject.Singleton

/** The running battles, one per character. */
@Singleton
class BattleRegistry @Inject constructor() {

  private val byChar = ConcurrentHashMap<Long, BattleInstance>()
  private val ids = AtomicLong(1)

  fun create(
      charId: Long,
      session: SessionContext,
      party: List<BattleMonState>,
      opponent: List<BattleMonState>,
      rng: BattleRng,
      rules: BattleRules = BattleRules(),
      pvp: PvpFoe? = null,
  ): BattleInstance? {
    val battle =
        BattleInstance(
            ids.getAndIncrement(),
            charId,
            session,
            party,
            opponent,
            rng,
            rules.catchable,
            rules.escapable,
            rules.trainer,
            pvp,
            rules.safari,
        )
    // Take both seats or neither.
    if (byChar.putIfAbsent(charId, battle) != null) return null
    if (battle.foeCharId != 0L && byChar.putIfAbsent(battle.foeCharId, battle) != null) {
      byChar.remove(charId, battle)
      return null
    }
    return battle
  }

  fun byChar(charId: Long): BattleInstance? = byChar[charId]

  fun remove(charId: Long): BattleInstance? {
    val battle = byChar.remove(charId) ?: return null
    if (battle.charId != charId) byChar.remove(battle.charId)
    if (battle.foeCharId != 0L && battle.foeCharId != charId) byChar.remove(battle.foeCharId)
    return battle
  }
}
