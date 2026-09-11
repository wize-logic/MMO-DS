package de.fiereu.openmmo.net.game

import de.fiereu.openmmo.common.ContestConditions
import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.test.decodeBytes
import de.fiereu.openmmo.common.test.encodeToBytes
import de.fiereu.openmmo.common.utils.hexToBytes
import de.fiereu.openmmo.net.game.packets.BattleOutcomeMon
import de.fiereu.openmmo.net.game.packets.BattleOutcomeMove
import de.fiereu.openmmo.net.game.packets.BattleOutcomePacket
import de.fiereu.openmmo.net.game.packets.BattleOutcomePacketCodec
import de.fiereu.openmmo.net.game.packets.DialogStatePacket
import de.fiereu.openmmo.net.game.packets.DialogStatePacketCodec
import de.fiereu.openmmo.net.game.packets.EntityLeavePacket
import de.fiereu.openmmo.net.game.packets.EntityLeavePacketCodec
import de.fiereu.openmmo.net.game.packets.FaceDirectionPacket
import de.fiereu.openmmo.net.game.packets.FaceDirectionPacketCodec
import de.fiereu.openmmo.net.game.packets.MovementPacket
import de.fiereu.openmmo.net.game.packets.MovementPacketCodec
import de.fiereu.openmmo.net.game.packets.RenderScreenPacket
import de.fiereu.openmmo.net.game.packets.RenderScreenPacketCodec
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

class GameCodecRoundtripTest :
    FunSpec({
      /**
       * The bytes are the fused client's own, written by `mmo_game_write_battle_outcome` and pinned
       * beside this in `mmo/tests/game_test.c`.
       */
      test("BattleOutcomePacket reads the client's row, species and all") {
        // id, level 16, xp 3000, hp 22, one move (52, 24 pp), the five conditions, sheen 6,
        // no ribbons, species 5, friendship 220, Leftovers (5234), not an egg, badly poisoned.
        val row =
            ("01" +
                    "0807060504030201" +
                    "10" +
                    "b80b0000" +
                    "1600" +
                    "340018" +
                    "000000" +
                    "000000" +
                    "000000" +
                    "0102030405" +
                    "06" +
                    "0000000000000000" +
                    "0500" +
                    "dc00" +
                    "7214" +
                    "00" +
                    "8800")
                .hexToBytes()
        val mon =
            BattleOutcomeMon(
                id = 0x0102030405060708L,
                level = 16,
                xp = 3000,
                hp = 22,
                moves =
                    listOf(
                        BattleOutcomeMove(52, 24),
                        BattleOutcomeMove(0, 0),
                        BattleOutcomeMove(0, 0),
                        BattleOutcomeMove(0, 0)),
                conditions =
                    ContestConditions(cool = 1, beauty = 2, cute = 3, smart = 4, tough = 5),
                sheen = 6,
                species = 5,
                friendship = 220,
                heldItemId = 5234,
                status = 0x88,
            )
        BattleOutcomePacketCodec.decodeBytes(row) shouldBe BattleOutcomePacket(listOf(mon))
        BattleOutcomePacketCodec.encodeToBytes(BattleOutcomePacket(listOf(mon))) shouldBe row
      }

      test("RenderScreenPacket roundtrip") {
        RenderScreenPacketCodec.encodeToBytes(RenderScreenPacket(true)) shouldBe byteArrayOf(1)
        RenderScreenPacketCodec.decodeBytes(byteArrayOf(0)) shouldBe RenderScreenPacket(false)
      }

      test("EntityLeavePacket roundtrip") {
        val pkt = EntityLeavePacket(entityId = 0x0102030405060708L)
        EntityLeavePacketCodec.decodeBytes(EntityLeavePacketCodec.encodeToBytes(pkt)) shouldBe pkt
      }

      test("FaceDirectionPacket roundtrip") {
        val pkt = FaceDirectionPacket(direction = Direction.UP)
        FaceDirectionPacketCodec.decodeBytes(FaceDirectionPacketCodec.encodeToBytes(pkt)) shouldBe
            pkt
      }

      test("MovementPacket roundtrip") {
        val pkt = MovementPacket(x = 100, y = 250, direction = Direction.DOWN)
        MovementPacketCodec.decodeBytes(MovementPacketCodec.encodeToBytes(pkt)) shouldBe pkt
      }

      test("MovementPacket decodes the running flag") {
        val pkt = MovementPacket(x = 100, y = 250, direction = Direction.UP, running = true)
        MovementPacketCodec.encodeToBytes(pkt) shouldBe byteArrayOf(100, 0, -6, 0, -127)
        MovementPacketCodec.decodeBytes(byteArrayOf(100, 0, -6, 0, -127)) shouldBe pkt
      }

      test("DialogStatePacket roundtrip") {
        DialogStatePacketCodec.encodeToBytes(DialogStatePacket(true)) shouldBe byteArrayOf(1)
        DialogStatePacketCodec.decodeBytes(byteArrayOf(0)) shouldBe DialogStatePacket(false)
      }
    })
