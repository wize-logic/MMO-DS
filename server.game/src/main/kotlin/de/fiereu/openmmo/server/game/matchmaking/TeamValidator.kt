package de.fiereu.openmmo.server.game.matchmaking

import de.fiereu.openmmo.common.MAX_PARTY_SIZE
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.pvp.Clause
import de.fiereu.openmmo.common.pvp.ClauseSite
import de.fiereu.openmmo.common.pvp.SignupOutcome
import de.fiereu.openmmo.pokemon.EvolutionRegistry
import javax.inject.Inject
import javax.inject.Singleton

/** The answer to a team check: the party may enter, or here is the sentence saying why not. */
sealed interface TeamVerdict {
  data object Accepted : TeamVerdict

  /**
   * [clause] is set only for the one outcome whose packet carries it, and [value] only for the two
   * whose sentence has a blank in it.
   */
  data class Refused(
      val outcome: SignupOutcome,
      val clause: Clause? = null,
      val value: Int = 0,
  ) : TeamVerdict
}

/** Whether a party may enter a queue. */
@Singleton
class TeamValidator
@Inject
constructor(
    private val tiers: TierRegistry,
    private val evolutions: EvolutionRegistry,
) {

  /** The team clauses [rules] declares that no monster record can answer. */
  fun unsupported(rules: QueueRules): List<Clause> =
      rules.teamClauses.map { it.clause }.filter { it in UNANSWERABLE }

  fun validate(party: List<Pokemon>, rules: QueueRules, ownerName: String): TeamVerdict {
    val fighters = party.filterNot { it.isEgg }
    if (fighters.isEmpty() || fighters.size > MAX_PARTY_SIZE) {
      return TeamVerdict.Refused(SignupOutcome.PARTY_SIZE)
    }

    val cannotAnswer = unsupported(rules)
    if (cannotAnswer.isNotEmpty()) {
      // The queue asks for a rule nothing here can check. Refusing every party is the honest
      // answer; passing one would be a rule that exists on paper and nowhere else.
      return TeamVerdict.Refused(SignupOutcome.UNKNOWN)
    }

    settingFor(rules, Clause.EXACT_PARTY_SIZE)?.let {
      if (fighters.size != it.value) {
        return TeamVerdict.Refused(SignupOutcome.CLAUSE_VIOLATED, it.clause, it.value)
      }
    }
    settingFor(rules, Clause.MINIMUM_LEVEL)?.let {
      if (fighters.any { m -> m.level.toInt() < it.value }) {
        return TeamVerdict.Refused(SignupOutcome.INVALID_LEVEL, value = it.value)
      }
    }
    settingFor(rules, Clause.MAXIMUM_LEVEL)?.let {
      if (fighters.any { m -> m.level.toInt() > it.value }) {
        return TeamVerdict.Refused(SignupOutcome.INVALID_LEVEL, value = it.value)
      }
    }

    if (fighters.any { !tiers.permits(rules.checkGroup, it.dexId, it.form) }) {
      return TeamVerdict.Refused(SignupOutcome.TIERING_OR_BAN)
    }

    settingFor(rules, Clause.UNIQUE_SPECIES)?.let {
      if (fighters.distinctBy { m -> m.dexId }.size != fighters.size) {
        return TeamVerdict.Refused(SignupOutcome.CLAUSE_VIOLATED, it.clause)
      }
    }
    settingFor(rules, Clause.UNIQUE_EVOLUTION_TREE)?.let {
      if (fighters.distinctBy { m -> lineRoot(m.dexId) }.size != fighters.size) {
        return TeamVerdict.Refused(SignupOutcome.CLAUSE_VIOLATED, it.clause)
      }
    }
    settingFor(rules, Clause.MINIMUM_OWN_CAUGHT)?.let {
      if (fighters.count { m -> m.ot == ownerName } < it.value) {
        return TeamVerdict.Refused(SignupOutcome.CLAUSE_VIOLATED, it.clause, it.value)
      }
    }

    return TeamVerdict.Accepted
  }

  /** The species at the bottom of this one's evolution line. */
  private fun lineRoot(dexId: Int): Int {
    var at = dexId
    repeat(MAX_EVOLUTION_DEPTH) {
      val prior = evolutions.preEvolutionsOf(at).firstOrNull() ?: return at
      at = prior
    }
    return at
  }

  private fun settingFor(rules: QueueRules, clause: Clause): ClauseSetting? =
      rules.teamClauses.firstOrNull { it.clause == clause }

  private companion object {
    /** No monster record carries a held item or a rental mark, so these have nothing to read. */
    val UNANSWERABLE =
        setOf(Clause.UNIQUE_ITEM, Clause.NO_RENTALS, Clause.ONE_RENTAL_ACROSS_PARTIES)

    /** Longer than any line in the data; a bound rather than a trust. */
    const val MAX_EVOLUTION_DEPTH = 8

    init {
      // Every unanswerable clause must be a team clause; one that is not would mean this set has
      // drifted from what the clause table says each clause is for.
      require(UNANSWERABLE.all { it.enforcedBy == ClauseSite.TEAM })
    }
  }
}
