package de.fiereu.openmmo.common.utils

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.util.zip.GZIPInputStream
import java.util.zip.GZIPOutputStream

/** The most a gzip blob off the wire may unpack to. */
const val GZIP_MAX_INFLATED: Int = 16 * 1024 * 1024

fun ByteArray.gzipCompress(): ByteArray {
  val baos = ByteArrayOutputStream()
  val gzip = GZIPOutputStream(baos)
  gzip.write(this)
  gzip.close()
  return baos.toByteArray()
}

/** Unpacks a gzip blob, refusing one that unpacks past [max]. */
fun ByteArray.gzipDecompress(max: Int = GZIP_MAX_INFLATED): ByteArray {
  val out = ByteArrayOutputStream(minOf(size * 4, max).coerceAtLeast(32))
  val chunk = ByteArray(0x4000)
  GZIPInputStream(ByteArrayInputStream(this)).use { input ->
    while (true) {
      val read = input.read(chunk)
      if (read < 0) break
      if (out.size() + read > max) {
        throw GzipTooLargeException(max)
      }
      out.write(chunk, 0, read)
    }
  }
  return out.toByteArray()
}

/** A gzip blob that unpacks past the ceiling. Its sender is not a client. */
class GzipTooLargeException(max: Int) :
    IllegalArgumentException("a gzip field unpacked past $max bytes")
