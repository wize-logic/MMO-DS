package de.fiereu.openmmo.server.login.update

import io.github.oshai.kotlinlogging.KotlinLogging
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.attribute.BasicFileAttributes

private val log = KotlinLogging.logger {}

/**
 * The oldest client this server will let in, read out of the operator's own published
 * `main_feed.txt`.
 */
class ClientRevisionFloor(private val feed: Path?) {

  private val lock = Any()
  @Volatile private var floor: Int = 0
  private var readAt: FileStamp? = null

  private data class FileStamp(val modified: Long, val size: Long)

  init {
    if (feed != null) {
      val stamp = stamp(feed) ?: throw IllegalStateException("no update feed at $feed")
      floor = read(feed, stamp)
      log.info { "Refusing clients below revision $floor, from $feed" }
    }
  }

  /**
   * Re-read when the file has changed underneath us, so a publish that raises the floor takes
   * effect without a restart. The publish writes the feed, and an operator should not have to
   * remember a second step to make what they wrote mean anything.
   */
  fun current(): Int {
    val feed = this.feed ?: return 0
    val now = stamp(feed)
    if (now == null) {
      // It proved readable at startup, so this is a publish mid-copy or a mount that went away.
      // Neither is a reason to admit a client this server was told to refuse.
      log.warn { "The update feed at $feed is not readable; holding the floor at $floor" }
      return floor
    }
    synchronized(lock) {
      if (now != readAt) {
        floor =
            runCatching { read(feed, now) }
                .getOrElse {
                  log.warn(it) { "The update feed at $feed did not parse; holding at $floor" }
                  return floor
                }
                .also { if (it != floor) log.info { "Client revision floor is now $it" } }
      }
      return floor
    }
  }

  /** Whether [revision], as a client claimed it, is one this server still speaks to. */
  fun admits(revision: Int): Boolean {
    val floor = current()
    // A client claiming nothing cannot be measured against a floor, and refusing it would
    // refuse every build driven straight out of a checkout.
    return floor <= 0 || revision <= 0 || revision >= floor
  }

  private fun stamp(feed: Path): FileStamp? =
      runCatching {
            val attrs = Files.readAttributes(feed, BasicFileAttributes::class.java)
            FileStamp(attrs.lastModifiedTime().toMillis(), attrs.size())
          }
          .getOrNull()

  private fun read(feed: Path, now: FileStamp): Int {
    val text = Files.readString(feed)
    val found =
        MIN_REVISION.find(text)
            ?: throw IllegalStateException(
                "$feed holds no <min_revision>; it is not a main_feed.txt")
    val value =
        found.groupValues[1].toIntOrNull()
            ?: throw IllegalStateException("$feed holds a <min_revision> that is not a number")
    readAt = now
    return value
  }

  private companion object {
    val MIN_REVISION = Regex("""<min_revision>\s*(-?\d{1,9})\s*</min_revision>""")
  }
}
