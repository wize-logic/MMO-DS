package de.fiereu.openmmo.common.utils

import io.kotest.assertions.throwables.shouldThrow
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

class GzipTest :
    FunSpec({
      test("a blob makes the round trip") {
        val text = "a terms of service page, or a map".repeat(64).toByteArray()
        text.gzipCompress().gzipDecompress().toList() shouldBe text.toList()
      }

      test("a blob that fits the ceiling exactly is still read") {
        val exact = ByteArray(4096) { (it % 251).toByte() }
        exact.gzipCompress().gzipDecompress(max = 4096).size shouldBe 4096
      }

      test("one byte past the ceiling is refused") {
        val over = ByteArray(4097)
        shouldThrow<GzipTooLargeException> { over.gzipCompress().gzipDecompress(max = 4096) }
      }

      // The shape the ceiling exists for: the compressed side is bounded by a frame, the unpacked
      // side by nothing. Twenty megabytes of zeroes is about twenty kilobytes on the wire.
      test("a bomb inside one frame is refused by the default ceiling") {
        val bomb = ByteArray(20 * 1024 * 1024).gzipCompress()
        (bomb.size < 0xFFFF) shouldBe true
        shouldThrow<GzipTooLargeException> { bomb.gzipDecompress() }
      }
    })
