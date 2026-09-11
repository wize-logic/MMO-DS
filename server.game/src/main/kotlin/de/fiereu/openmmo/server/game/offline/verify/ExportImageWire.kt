package de.fiereu.openmmo.server.game.offline.verify

import java.nio.ByteBuffer
import java.nio.ByteOrder

/** The offline copy a session sends as it leaves, as the client puts it on the wire. */
object ExportImageWire {

  const val MAGIC = "OMEX"

  /** The only version written so far. Bump both ends together. */
  const val VERSION = 1

  /** A Platinum backup chip, in bytes. Measured on the port's own write, 2026-09-04. */
  const val IMAGE_BYTES = 512 * 1024

  /** `u8 4`, the magic, a u16 version and a u32 length: what precedes the image. */
  private const val HEAD = 11

  fun looksLikeExport(bytes: ByteArray): Boolean = SessionChainWire.named(bytes, MAGIC)

  /** Read a copy, or say why these bytes are not one. */
  fun decode(bytes: ByteArray): ExportReading {
    if (!looksLikeExport(bytes)) return ExportReading.Unreadable("that is not an offline copy")
    if (bytes.size < HEAD) return ExportReading.Unreadable("the offline copy ends at its head")
    val buf = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
    buf.position(5)
    val version = buf.short.toInt() and 0xFFFF
    if (version != VERSION) {
      return ExportReading.Unreadable(
          "offline copy version $version, which this server cannot read")
    }
    val length = buf.int
    if (length != IMAGE_BYTES) {
      return ExportReading.Unreadable(
          "an offline copy of $length bytes, and a save image is $IMAGE_BYTES")
    }
    if (buf.remaining() != length) {
      return ExportReading.Unreadable(
          "the offline copy says $length bytes and carries ${buf.remaining()}")
    }
    return ExportReading.Read(bytes.copyOfRange(HEAD, HEAD + length))
  }
}

/** Either the image or the sentence saying why the bytes are not one. */
sealed interface ExportReading {
  data class Read(val image: ByteArray) : ExportReading {
    override fun equals(other: Any?): Boolean =
        this === other || (other is Read && image.contentEquals(other.image))

    override fun hashCode(): Int = image.size
  }

  data class Unreadable(val why: String) : ExportReading
}
