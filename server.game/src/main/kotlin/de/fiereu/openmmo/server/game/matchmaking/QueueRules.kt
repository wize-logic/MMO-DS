package de.fiereu.openmmo.server.game.matchmaking

import de.fiereu.openmmo.common.pvp.Clause
import de.fiereu.openmmo.common.pvp.ClauseSite
import de.fiereu.openmmo.common.pvp.MatchmakingQueue
import de.fiereu.openmmo.common.pvp.TierGroup

/** One clause as a queue sets it. [value] is the number the four that take one are given. */
data class ClauseSetting(val clause: Clause, val value: Int = 0) {
  init {
    require(clause.takesNumber || value == 0) { "$clause takes no number, but was given $value" }
  }
}

/** What a queue checks a party against. */
data class QueueRules(
    val queue: MatchmakingQueue,
    val checkGroup: TierGroup,
    val clauses: List<ClauseSetting>,
    /** Whether a party holding a monster brought in from an offline save may enter. */
    val noOfflineOrigin: Boolean = queue.ranked,
) {
  /** The clauses this queue expects the team check to answer for. */
  val teamClauses: List<ClauseSetting>
    get() = clauses.filter { it.clause.enforcedBy == ClauseSite.TEAM }

  companion object {
    /** The clauses every competitive queue here runs, before its own tiering. */
    private val STANDARD =
        listOf(
            ClauseSetting(Clause.EVASION),
            ClauseSetting(Clause.SLEEP),
            ClauseSetting(Clause.OHKO),
            ClauseSetting(Clause.UNIQUE_SPECIES),
            ClauseSetting(Clause.EXACT_PARTY_SIZE, 6),
            ClauseSetting(Clause.MAXIMUM_LEVEL, 50),
            ClauseSetting(Clause.MINIMUM_LEVEL, 50),
        )

    private fun standing(queue: MatchmakingQueue, group: TierGroup) =
        QueueRules(queue, group, STANDARD)

    /** The queues this server will open, and what each one is. */
    val ALL: List<QueueRules> =
        listOf(
            standing(MatchmakingQueue.OVER_USED, TierGroup.OVER_USED),
            standing(MatchmakingQueue.UNDER_USED, TierGroup.UNDER_USED),
            standing(MatchmakingQueue.NEVER_USED, TierGroup.NEVER_USED),
            standing(MatchmakingQueue.DOUBLES_OFFICIAL, TierGroup.DOUBLES_OFFICIAL),
        )

    fun of(queue: MatchmakingQueue): QueueRules? = ALL.firstOrNull { it.queue == queue }
  }
}
