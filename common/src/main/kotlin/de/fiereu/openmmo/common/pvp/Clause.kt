package de.fiereu.openmmo.common.pvp

/** One rule a format puts on a battle, as the competitive wire numbers them. */
enum class Clause(
    val id: Byte,
    val enforcedBy: ClauseSite,
    val takesNumber: Boolean = false,
) {
  EVASION(0, ClauseSite.BATTLE),
  SLEEP(1, ClauseSite.BATTLE),
  OHKO(2, ClauseSite.BATTLE),
  UNIQUE_SPECIES(3, ClauseSite.TEAM),
  UNIQUE_ITEM(4, ClauseSite.TEAM),
  UNIQUE_EVOLUTION_TREE(5, ClauseSite.TEAM),
  NO_RENTALS(6, ClauseSite.TEAM),
  MINIMUM_OWN_CAUGHT(7, ClauseSite.TEAM, takesNumber = true),
  ONE_RENTAL_ACROSS_PARTIES(8, ClauseSite.TEAM),
  EXACT_PARTY_SIZE(9, ClauseSite.TEAM, takesNumber = true),
  MINIMUM_LEVEL(10, ClauseSite.TEAM, takesNumber = true),
  MAXIMUM_LEVEL(11, ClauseSite.TEAM, takesNumber = true),
  OPEN_TEAM_SHEET(12, ClauseSite.TEAM_PREVIEW);

  /** The competitive string table's id for this clause's name. */
  val nameStringId: Int
    get() = 5700 + id

  /** The competitive string table's id for the sentence that explains it. */
  val descriptionStringId: Int
    get() = 5720 + id

  companion object {
    fun fromId(id: Byte): Clause? = entries.firstOrNull { it.id == id }
  }
}

/** Which half of the system a [Clause] is the responsibility of. */
enum class ClauseSite {
  /** The server, before a signup is accepted. */
  TEAM,

  /** The engine's battle script, while the fight runs. */
  BATTLE,

  /** The team-preview screen, by showing more than it otherwise would. */
  TEAM_PREVIEW,
}
