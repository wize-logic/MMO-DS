package de.fiereu.openmmo.server.game.offline.verify

import io.kotest.core.spec.style.StringSpec
import io.kotest.matchers.shouldBe
import io.kotest.matchers.string.shouldContain
import io.kotest.matchers.types.shouldBeInstanceOf

/** The offline copy, read as the client writes it. */
class ExportImageWireTest :
    StringSpec({
      "the head the client writes is read as an offline copy and as nothing else" {
        val head = hex(HEAD_VECTOR)
        ExportImageWire.looksLikeExport(head) shouldBe true
        SessionChainWire.looksLikeChain(head) shouldBe false
        SessionChainWire.named(head, "OMIR") shouldBe false
        // Three bytes is not a backup chip, and the reader says how big one is.
        val read = ExportImageWire.decode(head).shouldBeInstanceOf<ExportReading.Unreadable>()
        read.why shouldContain ExportImageWire.IMAGE_BYTES.toString()
      }

      "a whole chip reads back byte for byte" {
        val image = ByteArray(ExportImageWire.IMAGE_BYTES) { (it * 7).toByte() }
        val read = ExportImageWire.decode(blob(image)).shouldBeInstanceOf<ExportReading.Read>()
        read.image.contentEquals(image) shouldBe true
      }

      "a version this server does not write is said so" {
        val blob = blob(ByteArray(ExportImageWire.IMAGE_BYTES)).also { it[5] = 2 }
        val read = ExportImageWire.decode(blob).shouldBeInstanceOf<ExportReading.Unreadable>()
        read.why shouldContain "version 2"
      }

      "a length that is not the bytes carried is refused" {
        val blob = blob(ByteArray(ExportImageWire.IMAGE_BYTES)).copyOf(1000)
        val read = ExportImageWire.decode(blob).shouldBeInstanceOf<ExportReading.Unreadable>()
        read.why shouldContain "carries"
      }

      "a save report is not an offline copy" {
        val report =
            hex(HEAD_VECTOR).also { "OMIR".forEachIndexed { i, c -> it[i + 1] = c.code.toByte() } }
        ExportImageWire.looksLikeExport(report) shouldBe false
        ExportImageWire.decode(report).shouldBeInstanceOf<ExportReading.Unreadable>()
      }
    })

/** `u8 4`, OMEX, version 1, a three-byte length, and the three bytes. */
private const val HEAD_VECTOR = "04 4f4d4558 0100 03000000 aabbcc"

private fun hex(text: String): ByteArray =
    text.replace(" ", "").chunked(2).map { it.toInt(16).toByte() }.toByteArray()

/** The blob the client's encoder writes for [image], built here the same way it builds it. */
private fun blob(image: ByteArray): ByteArray {
  val out = ByteArray(11 + image.size)
  out[0] = 4
  "OMEX".forEachIndexed { i, c -> out[i + 1] = c.code.toByte() }
  out[5] = 1
  out[6] = 0
  val n = image.size
  out[7] = (n and 0xFF).toByte()
  out[8] = ((n shr 8) and 0xFF).toByte()
  out[9] = ((n shr 16) and 0xFF).toByte()
  out[10] = ((n shr 24) and 0xFF).toByte()
  image.copyInto(out, 11)
  return out
}
