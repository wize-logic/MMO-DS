package de.fiereu.openmmo.server.game.services

/**
 * What a character may be called.
 *
 * A name was anything at all up to thirty-two characters, which is two problems. It is drawn by
 * every client in a font this project builds a glyph table for, so a name made of control
 * characters or combining marks is one other people's screens have to render, and the sender is not
 * the one who finds out what it does to them.
 *
 * It is also an identity. Unicode has several dozen characters that draw like a Latin `a`, so
 * without an alphabet there is no such thing as a taken name, and the uniqueness check the store
 * does is only worth having once two names that look alike are the same name.
 *
 * The reserved list is short on purpose: the words that would let somebody speak as this server,
 * matched with separators taken out. It does not try to catch every name a person might find
 * offensive; that needs a human.
 */
object CharacterNames {

  const val MAX_LENGTH = 32

  /** Letters, digits and the three separators a name really uses. Ascii only. */
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

  /** The name with separators and case taken out, which is the form two names collide in. */
  private fun normalize(name: String): String = name.filter { it.isLetterOrDigit() }.lowercase()
}
