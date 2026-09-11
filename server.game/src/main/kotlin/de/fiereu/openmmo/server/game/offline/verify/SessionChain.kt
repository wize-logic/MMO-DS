package de.fiereu.openmmo.server.game.offline.verify

import java.time.LocalDateTime
import java.time.format.DateTimeFormatter
import java.time.format.DateTimeParseException

/** One offline session, as the client wrote it down beside the save. */
data class SessionLink(
    val version: Int,
    /** Null for `unknown`: an install that cannot name its build says so rather than claiming 0. */
    val revision: Int?,
    val rtc: LocalDateTime,
    val bootSha256: String?,
    val recordingName: String,
    val recordingSha256: String?,
    val quitSha256: String?,
    val endFrame: Long?,
)

/**
 * The sessions between a point this server trusts and the save being asked about, oldest first,
 * each one's recording beside it.
 */
data class SessionChain(
    val anchorSha256: String?,
    val links: List<SessionLink>,
    val recordings: List<ByteArray>,
) {
  init {
    require(links.size == recordings.size) { "every link carries its own recording" }
  }

  val bytes: Long
    get() = recordings.sumOf { it.size.toLong() }

  override fun equals(other: Any?): Boolean =
      this === other ||
          (other is SessionChain &&
              anchorSha256 == other.anchorSha256 &&
              links == other.links &&
              recordings.size == other.recordings.size &&
              recordings.indices.all { recordings[it].contentEquals(other.recordings[it]) })

  override fun hashCode(): Int =
      31 * (31 * (anchorSha256?.hashCode() ?: 0) + links.hashCode()) + recordings.size
}

/** Either a link or the sentence saying why the file is not one. */
sealed interface LinkReading {
  data class Read(val link: SessionLink) : LinkReading

  data class Unreadable(val why: String) : LinkReading
}

/** The session record's own format, which is a handful of `key value` lines and nothing else. */
object SessionLinkFormat {

  /** The only version written so far. Bump both ends together. */
  const val VERSION = 1

  private val RTC: DateTimeFormatter = DateTimeFormatter.ofPattern("uuuu-MM-dd HH:mm:ss")

  private val HEX = Regex("[0-9a-f]{64}")

  fun parse(text: String): LinkReading {
    val fields = mutableMapOf<String, String>()
    for (raw in text.lineSequence()) {
      val line = raw.trim()
      if (line.isEmpty()) continue
      val key = line.substringBefore(' ')
      val value = line.substringAfter(' ', "").trim()
      if (key.isEmpty() || value.isEmpty())
          return LinkReading.Unreadable("a line with no value: \"$line\"")
      // First wins. A second `quit-sha256` is a record that was appended to twice, which is a
      // client writing over its own history, and the earlier one is the one the earlier lines
      // belong with.
      fields.putIfAbsent(key, value)
    }

    val version =
        fields["version"]?.toIntOrNull() ?: return LinkReading.Unreadable("no version line")
    if (version != VERSION)
        return LinkReading.Unreadable(
            "session record version $version, which this server cannot read")

    val revision =
        when (val r = fields["revision"]) {
          null -> return LinkReading.Unreadable("no revision line")
          "unknown" -> null
          else ->
              r.toIntOrNull()?.takeIf { it >= 0 }
                  ?: return LinkReading.Unreadable("revision \"$r\"")
        }

    val rtcText = fields["rtc"] ?: return LinkReading.Unreadable("no rtc line")
    val rtc =
        try {
          LocalDateTime.parse(rtcText, RTC)
        } catch (_: DateTimeParseException) {
          return LinkReading.Unreadable(
              "rtc \"$rtcText\" is not a date the game would have been given")
        }

    val boot = hashOrNone(fields["boot-sha256"]) ?: return LinkReading.Unreadable("boot-sha256")
    val recordingName = fields["recording"] ?: return LinkReading.Unreadable("no recording line")
    val recordingHash =
        hashOrNone(fields["recording-sha256"]) ?: return LinkReading.Unreadable("recording-sha256")
    val quit = hashOrNone(fields["quit-sha256"]) ?: return LinkReading.Unreadable("quit-sha256")

    val endFrame =
        when (val e = fields["end-frame"]) {
          null -> null
          else ->
              e.toLongOrNull()?.takeIf { it > 0 }
                  ?: return LinkReading.Unreadable(
                      "end-frame \"$e\", which is not a frame the game stopped on")
        }

    return LinkReading.Read(
        SessionLink(
            version = version,
            revision = revision,
            rtc = rtc,
            bootSha256 = boot.value,
            recordingName = recordingName,
            recordingSha256 = recordingHash.value,
            quitSha256 = quit.value,
            endFrame = endFrame,
        ))
  }

  /** `none` or a lowercase sha256, and nothing else. */
  private fun hashOrNone(text: String?): Hash? {
    if (text == null) return null
    if (text == "none") return Hash(null)
    if (!HEX.matches(text)) return null
    return Hash(text)
  }

  private data class Hash(val value: String?)
}
