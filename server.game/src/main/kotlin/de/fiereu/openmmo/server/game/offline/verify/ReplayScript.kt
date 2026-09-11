package de.fiereu.openmmo.server.game.offline.verify

/** What a recording is, once it has been read the way the game's own parser reads it. */
sealed interface ScriptReading {
  /** [lastFrame] is -1 for a script with no events at all, which is a legal if useless one. */
  data class Ok(val events: Int, val lastFrame: Long) : ScriptReading

  data class Refused(val why: String) : ScriptReading
}

/** The port's own input-script rules, read here before anything is handed to the port. */
object ReplayScript {

  /** `pc_input.c` `sButtons`, which is the SDK's own names minus their prefixes. */
  private val BUTTONS =
      setOf(
          "A",
          "B",
          "SELECT",
          "START",
          "RIGHT",
          "LEFT",
          "UP",
          "DOWN",
          "R",
          "L",
          "X",
          "Y",
          "DEBUG",
      )

  /** `char line[256]` in the port. A line at or over it is not one line to the game. */
  const val LINE_MAX = 256

  private val DIGITS = Regex("[0-9]{1,20}")

  fun read(recording: ByteArray, byteCap: Int): ScriptReading {
    if (recording.size > byteCap) {
      return ScriptReading.Refused(
          "the recording is ${recording.size} bytes, over the $byteCap this server will replay")
    }
    val text = recording.toString(Charsets.ISO_8859_1)
    var events = 0
    var last = -1L
    var lineno = 0

    for (raw in text.lineSequence()) {
      lineno++
      // A trailing newline gives one empty last line; the port's fgets never sees it either.
      val line = raw.removeSuffix("\r")
      if (line.length >= LINE_MAX) {
        return ScriptReading.Refused("line $lineno is ${line.length} bytes, past the game's own")
      }
      val words = line.trim().split(WHITESPACE).filter { it.isNotEmpty() }
      if (words.isEmpty() || words[0].startsWith("#")) continue
      if (words.size < 2) return ScriptReading.Refused("line $lineno has a frame and nothing to do")

      if (!DIGITS.matches(words[0])) {
        return ScriptReading.Refused("line $lineno does not start with a frame")
      }
      val frame =
          words[0].toLongOrNull()?.takeIf { it >= 0 }
              ?: return ScriptReading.Refused(
                  "line $lineno names a frame past what a run can reach")

      when (words[1]) {
        "keys" ->
            for (name in words.drop(2)) {
              if (name != "none" && name !in BUTTONS) {
                return ScriptReading.Refused(
                    "line $lineno presses \"$name\", which is not a button")
              }
            }
        "touch" -> {
          // Two numbers; the port ignores whatever follows them, so this does too.
          if (words.size < 4 || !DIGITS.matches(words[2]) || !DIGITS.matches(words[3])) {
            return ScriptReading.Refused("line $lineno touches nowhere in particular")
          }
        }
        "release" -> Unit
        else ->
            return ScriptReading.Refused(
                "line $lineno asks for \"${words[1]}\", which is not a verb")
      }

      if (frame < last) return ScriptReading.Refused("line $lineno goes back in time")
      last = frame
      events++
    }
    return ScriptReading.Ok(events, last)
  }

  private val WHITESPACE = Regex("\\s+")
}
