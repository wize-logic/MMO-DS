package de.fiereu.openmmo.server.game.offline.verify

import io.kotest.core.spec.style.StringSpec
import io.kotest.matchers.nulls.shouldBeNull
import io.kotest.matchers.shouldBe
import io.kotest.matchers.string.shouldContain
import io.kotest.matchers.types.shouldBeInstanceOf
import java.nio.charset.StandardCharsets

/** The chain of session records, read as the client writes it. */
class SessionChainWireTest :
    StringSpec({
      "a chain the client wrote reads back session for session" {
        val read = SessionChainWire.decode(hex(VECTOR)).shouldBeInstanceOf<ChainReading.Read>()
        val chain = read.chain

        chain.anchorSha256 shouldBe "1".repeat(64)
        chain.links.size shouldBe 2
        chain.recordings.size shouldBe 2

        val first = chain.links[0]
        first.version shouldBe 1
        first.revision shouldBe 12
        first.rtc.toString() shouldBe "2026-09-04T09:00"
        // A New Game booted from no file at all, which is the one start nothing could have forged.
        first.bootSha256.shouldBeNull()
        first.recordingName shouldBe "sessions/20260904-090000.inp"
        first.recordingSha256 shouldBe "2".repeat(64)
        first.quitSha256 shouldBe "3".repeat(64)
        first.endFrame shouldBe 1200
        chain.recordings[0].toString(StandardCharsets.UTF_8) shouldBe "0 keys none\n120 keys A\n"

        // The second boots from what the first quit on: the linkage the admission check walks.
        val second = chain.links[1]
        second.bootSha256 shouldBe "3".repeat(64)
        // A session that never closed leaves no quit hash and no end frame, and says so rather than
        // claiming a zero.
        second.quitSha256.shouldBeNull()
        second.endFrame.shouldBeNull()
        chain.recordings[1].size shouldBe 0
      }

      "a blob naming another format is not read as a chain" {
        // The save report shares this channel and opens the same way with a different name.
        val report =
            hex(VECTOR).copyOf().also {
              "OMIR".forEachIndexed { i, c -> it[i + 1] = c.code.toByte() }
            }
        SessionChainWire.looksLikeChain(report) shouldBe false
        SessionChainWire.named(report, "OMIR") shouldBe true
        val read = SessionChainWire.decode(report).shouldBeInstanceOf<ChainReading.Unreadable>()
        read.why shouldContain "not a chain"
      }

      "a version this server cannot read is a reason and not a refusal" {
        val bumped = hex(VECTOR).copyOf().also { it[5] = 9 }
        val read = SessionChainWire.decode(bumped).shouldBeInstanceOf<ChainReading.Unreadable>()
        read.why shouldContain "version 9"
      }

      "a length that runs off the end is refused rather than allocated on" {
        // The first record's U32LE length, at the head plus the count and the anchor.
        val at = 7 + 2 + 2 + 64
        val huge = hex(VECTOR).copyOf()
        huge[at] = 0xFF.toByte()
        huge[at + 1] = 0xFF.toByte()
        huge[at + 2] = 0xFF.toByte()
        huge[at + 3] = 0x7F
        val read = SessionChainWire.decode(huge).shouldBeInstanceOf<ChainReading.Unreadable>()
        read.why shouldContain "end inside session 1"
      }

      "a chain that is only a head has nothing in it to check" {
        val empty =
            hex(VECTOR).copyOf(13).also {
              it[7] = 0
              it[8] = 0
            }
        val read = SessionChainWire.decode(empty).shouldBeInstanceOf<ChainReading.Unreadable>()
        read.why shouldContain "no sessions"
      }

      "an anchor that is not a hash this server could have written is refused" {
        val bytes = hex(VECTOR).copyOf()
        // Blank the first byte of the 64 the anchor occupies; `1` becomes `!`, which is not hex.
        bytes[11] = '!'.code.toByte()
        val read = SessionChainWire.decode(bytes).shouldBeInstanceOf<ChainReading.Unreadable>()
        read.why shouldContain "not an export"
      }
    })

private fun hex(text: String): ByteArray =
    ByteArray(text.length / 2) { text.substring(it * 2, it * 2 + 2).toInt(16).toByte() }

private const val VECTOR =
    "044f4d43480100020040003131313131313131313131313131313131313131313131313131313131" +
        "31313131313131313131313131313131313131313131313131313131313131313131311401000076" +
        "657273696f6e20310a7265766973696f6e2031320a72746320323032362d30392d30342030393a30" +
        "303a30300a626f6f742d736861323536206e6f6e650a7265636f7264696e672073657373696f6e73" +
        "2f32303236303930342d3039303030302e696e700a7265636f7264696e672d736861323536203232" +
        "32323232323232323232323232323232323232323232323232323232323232323232323232323232" +
        "323232323232323232323232323232323232323232320a717569742d736861323536203333333333" +
        "33333333333333333333333333333333333333333333333333333333333333333333333333333333" +
        "333333333333333333333333333333333333330a656e642d6672616d6520313230300a1700000030" +
        "206b657973206e6f6e650a313230206b65797320410ac900000076657273696f6e20310a72657669" +
        "73696f6e2031320a72746320323032362d30392d30342031313a30303a30300a626f6f742d736861" +
        "32353620333333333333333333333333333333333333333333333333333333333333333333333333" +
        "333333333333333333333333333333333333333333333333333333330a7265636f7264696e672073" +
        "657373696f6e732f32303236303930342d3131303030302e696e700a7265636f7264696e672d7368" +
        "61323536206e6f6e650a717569742d736861323536206e6f6e650a00000000"
