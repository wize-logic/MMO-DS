package de.fiereu.openmmo.net.game

import de.fiereu.openmmo.common.test.decodeBytes
import de.fiereu.openmmo.common.test.encodeToBytes
import de.fiereu.openmmo.net.game.packets.DialogOptionPacket
import de.fiereu.openmmo.net.game.packets.DialogOptionPacketCodec
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

class DialogOptionPacketTest :
    FunSpec({
      // Body of a capture: a Potion (item 5017) used on the starter from the overworld bag. The
      // opcode byte is stripped by the frame layer, so the body is the remaining fourteen bytes,
      // reading only the first four of them is what dropped the connection.
      val captured =
          byteArrayOf(
              0x99.toByte(),
              0x13, // itemId U16LE = 0x1399 = 5017 (POTION)
              0x00,
              0xc0.toByte(),
              0x0a,
              0x0f,
              0xef.toByte(),
              0x29,
              0x01,
              0x20, // target S64LE
              0x01,
              0x00,
              0xff.toByte(),
              0x00, // trailer, meaning not yet known
          )

      test("decodes the captured overworld item-use target") {
        val packet = DialogOptionPacketCodec.decodeBytes(captured)
        packet.itemId shouldBe 5017
        packet.targetEntityId shouldBe 0x200129ef0f0ac000L
        packet.trailer shouldBe DialogOptionPacket.TRAILER_SAMPLE
      }

      test("re-encodes to the captured bytes") {
        val packet = DialogOptionPacketCodec.decodeBytes(captured)
        DialogOptionPacketCodec.encodeToBytes(packet) shouldBe captured
      }
    })
