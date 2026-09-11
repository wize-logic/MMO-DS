package de.fiereu.openmmo.server.game.offline.verify

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.charset.StandardCharsets

/** A chain of offline sessions as the client that recorded them puts it on the wire. */
object SessionChainWire {

  const val MAGIC = "OMCH"

  /** The only version written so far. Bump both ends together. */
  const val VERSION = 1

  /** Sessions in one chain. The client refuses to send more and this refuses to read more. */
  const val MAX_LINKS = 4096

  /** The head both blobs on this channel share: a length-prefixed four-byte name. */
  private const val HEAD = 7

  fun looksLikeChain(bytes: ByteArray): Boolean = named(bytes, MAGIC)

  fun named(bytes: ByteArray, magic: String): Boolean {
    if (bytes.size < HEAD) return false
    if (bytes[0].toInt() != 4) return false
    return String(bytes, 1, 4, StandardCharsets.US_ASCII) == magic
  }

  /** Read a chain, or say why these bytes are not one. */
  fun decode(bytes: ByteArray): ChainReading {
    if (!named(bytes, MAGIC))
        return ChainReading.Unreadable("that is not a chain of session records")
    val buf = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
    buf.position(5)
    val version = buf.short.toInt() and 0xFFFF
    if (version != VERSION)
        return ChainReading.Unreadable(
            "session records version $version, which this server cannot read")

    if (buf.remaining() < 4) return ChainReading.Unreadable("the session records end at their head")
    val count = buf.short.toInt() and 0xFFFF
    if (count == 0) return ChainReading.Unreadable("a chain with no sessions in it")
    if (count > MAX_LINKS) return ChainReading.Unreadable("$count sessions in one chain")

    val anchorLength = buf.short.toInt() and 0xFFFF
    if (buf.remaining() < anchorLength)
        return ChainReading.Unreadable("the anchor runs off the end")
    val anchorText = String(bytes, buf.position(), anchorLength, StandardCharsets.US_ASCII)
    buf.position(buf.position() + anchorLength)
    // `none` is a New Game: the absence of a file, which is not something a client could forge. Any
    // other word is a hash this server is asked to have written itself, and the admission decides.
    val anchor =
        when {
          anchorText == "none" -> null
          HEX.matches(anchorText) -> anchorText
          else ->
              return ChainReading.Unreadable("\"$anchorText\" is not an export this server wrote")
        }

    val links = ArrayList<SessionLink>(count)
    val recordings = ArrayList<ByteArray>(count)
    for (i in 0 until count) {
      val record = take(bytes, buf) ?: return ranOut(i)
      val recording = take(bytes, buf) ?: return ranOut(i)
      when (val read = SessionLinkFormat.parse(record.toString(StandardCharsets.UTF_8))) {
        is LinkReading.Unreadable -> return ChainReading.Unreadable("session ${i + 1}: ${read.why}")
        is LinkReading.Read -> links.add(read.link)
      }
      recordings.add(recording)
    }
    return ChainReading.Read(SessionChain(anchor, links, recordings))
  }

  /** One U32LE-prefixed run of bytes, or null when the blob ends inside it. */
  private fun take(bytes: ByteArray, buf: ByteBuffer): ByteArray? {
    if (buf.remaining() < 4) return null
    // A length is unsigned on the wire and the reader's is signed, so a client claiming two
    // gigabytes reads back as a negative number. Refuse it as short rather than allocating on it.
    val length = buf.int
    if (length < 0 || buf.remaining() < length) return null
    val from = buf.position()
    buf.position(from + length)
    return bytes.copyOfRange(from, from + length)
  }

  private fun ranOut(index: Int): ChainReading =
      ChainReading.Unreadable("the session records end inside session ${index + 1}")

  private val HEX = Regex("[0-9a-f]{64}")
}

/** Either a chain or the sentence saying why the bytes are not one. */
sealed interface ChainReading {
  data class Read(val chain: SessionChain) : ChainReading

  data class Unreadable(val why: String) : ChainReading
}
