package de.fiereu.openmmo.server.game.offline

/** What the door decided about one field of one save. */
sealed interface ImportVerdict {
  /** The field, in words a player reads back in the import's own log line. */
  val what: String
}

data class Clamped(override val what: String, val from: String, val to: String) : ImportVerdict {
  override fun toString(): String = "$what: $from brought to $to"
}

data class Dropped(override val what: String, val why: String) : ImportVerdict {
  override fun toString(): String = "$what dropped: $why"
}

data class Allowed(override val what: String, val note: String) : ImportVerdict {
  override fun toString(): String = "$what allowed: $note"
}

data class Refused(override val what: String, val why: String) : ImportVerdict {
  override fun toString(): String = "$what refused: $why"
}

/** The whole of one import's reading. */
data class LegalityResult(val save: OfflineSave?, val verdicts: List<ImportVerdict>) {

  val refusal: Refused?
    get() = verdicts.filterIsInstance<Refused>().firstOrNull()

  val accepted: Boolean
    get() = save != null

  fun of(what: String): List<ImportVerdict> = verdicts.filter { it.what == what }
}
