package de.fiereu.openmmo.common.pvp

/** What a matchmaking signup may be answered with. */
enum class SignupOutcome(val id: Byte, val stringId: Int) {
  ACCEPTED(0, 0),
  ALREADY_REGISTERED(1, 5600),
  INVALID_LEVEL(2, 5601),
  PARTY_SIZE(3, 5602),
  UNKNOWN(4, 5603),
  CLAUSE_VIOLATED(5, 5604),
  MONSTER_BANNED(6, 5605),
  TOURNAMENT_INVALID(7, 5606),
  NOT_A_PARTICIPANT(8, 5607),
  SIGNUP_REQUIREMENTS(9, 5608),
  NOT_ENOUGH_BATTLE_POINTS(10, 5611),
  TIERING_OR_BAN(11, 5609),
  STORYLINE_INCOMPLETE(12, 5610),
  REQUIREMENTS_NOT_MET(13, 5608),
  BUSY(14, 5622),
  NEEDS_BADGES(15, 5624),
  STORYLINE_TEAM_IN_INCOMPLETE_REGION(16, 5635),
  RESTRICTED_FOR_UNFAIR_PLAY(17, 6078);

  /** True while this answer opens a signup rather than refusing one. */
  val accepted: Boolean
    get() = this == ACCEPTED

  companion object {
    fun fromId(id: Byte): SignupOutcome? = entries.firstOrNull { it.id == id }
  }
}
