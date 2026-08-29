package de.fiereu.openmmo.net.game

import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.enums.SkinSlot
import de.fiereu.openmmo.common.test.assertBytesRoundtrip
import de.fiereu.openmmo.common.test.decodeBytes
import de.fiereu.openmmo.common.test.fixture
import de.fiereu.openmmo.net.game.packets.LoadEntityPacketCodec
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

class LoadEntityPacketTest :
    FunSpec({
      test("a captured spawn decodes to its skin, name and tile") {
        val bytes = fixture("game/s2c/05/load_entity_31914.bin")

        val packet = LoadEntityPacketCodec.decodeBytes(bytes)

        packet.gender shouldBe 0
        packet.name shouldBe "Player"
        packet.skin.regionSelectionIndex shouldBe 3
        packet.skin.keys shouldBe
            setOf(
                SkinSlot.HAIR,
                SkinSlot.EYES,
                SkinSlot.TOP,
                SkinSlot.FOOTWEAR,
                SkinSlot.LEGGINGS,
            )
        packet.bankId shouldBe 4
        packet.mapId shouldBe 1
        packet.x shouldBe 6
        packet.y shouldBe 6
        packet.facing shouldBe Direction.DOWN
        packet.hasFollower shouldBe false
        LoadEntityPacketCodec.assertBytesRoundtrip(bytes)
      }

      // The branches no session of ours produces: a female peer, a slot mask asking for a per-
      // slot override byte, a heading byte carrying the 0x08 flag beside its direction, and
      // the flags trailer in full.
      test("the branches a live session never reaches still land the later fields") {
        val bytes =
            byteArrayOf(
                7,
                0,
                0,
                0,
                0,
                0,
                0,
                0, // entityId
                1, // gender: female
                0, // skin region
                0x03,
                0x80.toByte(), // slot mask: two slots, per-slot overrides
                0xAA.toByte(),
                0xBB.toByte(),
                0x11, // slot 0: word + override
                0xCC.toByte(),
                0xDD.toByte(),
                0x22, // slot 1: word + override
                0x41,
                0x00,
                0x00,
                0x00, // name "A"
                2,
                4,
                6, // region, bank, map
                0x0A,
                0x00, // x = 10
                0x2C,
                0x01, // y = 300
                0, // z
                0x0B, // heading: RIGHT, 0x08 set
                0x05, // status bits
                0x02, // entity state
                0x1F, // flags: every conditional bit
                0x11, // 0x01
                0x22,
                0x33,
                0x44, // 0x02
                0x97.toByte(),
                0x00, // 0x04: follower 151
                0x55, // 0x08
                0x4F,
                0x00,
                0x4B,
                0x00,
                0x00,
                0x00, // 0x10: prefix "OK"
            )

        val packet = LoadEntityPacketCodec.decodeBytes(bytes)

        packet.gender shouldBe 1
        packet.name shouldBe "A"
        packet.skin.keys shouldBe setOf(SkinSlot.FOREHEAD, SkinSlot.HAT)
        packet.x shouldBe 10
        packet.y shouldBe 300
        packet.facing shouldBe Direction.RIGHT
        packet.hasFollower shouldBe true
        packet.followerDexId shouldBe 151
      }
    })
