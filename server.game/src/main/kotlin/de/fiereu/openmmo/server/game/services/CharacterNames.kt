package de.fiereu.openmmo.server.game.services

/** What a character may be called. */
object CharacterNames {

  const val MAX_LENGTH = 32

  /** Letters, digits, and the three separators a person's name really uses. */
  private val ALLOWED = Regex("^[A-Za-z][A-Za-z0-9 '.\\-]*$")

  /** Words that would make a player sound like the server or its staff. */
  private val RESERVED =
      setOf(
          "admin",
          "administrator",
          "gm",
          "gamemaster",
          "mod",
          "moderator",
          "staff",
          "support",
          "system",
          "server",
          "console",
          "official",
          "openmmo",
          "developer",
          "dev",
      )

  /** Why a name was refused, for the log line. Null when it was not. */
  fun refuse(name: String): String? {
    if (name.isEmpty()) return "empty"
    if (name.length > MAX_LENGTH) return "longer than $MAX_LENGTH"
    if (!ALLOWED.matches(name)) return "holds something outside letters, digits and ' . -"
    if (name.contains("  ")) return "holds a run of spaces"
    if (name.endsWith(" ") || name.endsWith("-") || name.endsWith("'")) return "ends on a separator"
    if (normalize(name) in RESERVED) return "is a word this server speaks with"
    return null
  }

  /** The name with its separators and case taken out, which is the form two names collide in. */
  private fun normalize(name: String): String = name.filter { it.isLetterOrDigit() }.lowercase()
}
