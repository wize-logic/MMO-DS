package de.fiereu.openmmo.net.game

import de.fiereu.openmmo.common.test.decodeBytes
import de.fiereu.openmmo.common.test.encodeToBytes
import de.fiereu.openmmo.common.test.fixture
import de.fiereu.openmmo.common.utils.hexToBytes
import de.fiereu.openmmo.net.game.packets.battle.moves.MOVE_LEARN_NO_SLOT
import de.fiereu.openmmo.net.game.packets.battle.moves.MoveLearnPromptPacket
import de.fiereu.openmmo.net.game.packets.battle.moves.MoveLearnPromptPacketCodec
import de.fiereu.openmmo.net.game.packets.battle.moves.MoveLearnReplyPacket
import de.fiereu.openmmo.net.game.packets.battle.moves.MoveLearnReplyPacketCodec
import io.kotest.assertions.throwables.shouldThrowAny
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

private const val ENTITY_ID = 0x1ACEADEF2AC8C000L
private const val ENTITY_LE = "00c0c82aefadce1a"
private const val FURY_SWIPES: Short = 154

/**
 * The layout is the one client the official client reads and writes: a monster id, a signed move
 * slot and one move id. Its own reader is three statements, `the official client` takes the id, a
 * `get()` and a `getShort()`; `the official client` writes the same three back.
 */
class MoveLearnPacketsTest :
    FunSpec({
      test("a full moveset is offered a move with no slot to put it in") {
        val bytes = "${ENTITY_LE}ff9a00".hexToBytes()
        val decoded = MoveLearnPromptPacketCodec.decodeBytes(bytes)
        decoded shouldBe MoveLearnPromptPacket(ENTITY_ID, MOVE_LEARN_NO_SLOT, FURY_SWIPES)
        MoveLearnPromptPacketCodec.encodeToBytes(decoded) shouldBe bytes
      }

      test("a move that fitted names the slot it went into") {
        val bytes = "${ENTITY_LE}029a00".hexToBytes()
        val decoded = MoveLearnPromptPacketCodec.decodeBytes(bytes)
        decoded shouldBe MoveLearnPromptPacket(ENTITY_ID, 2, FURY_SWIPES)
        MoveLearnPromptPacketCodec.encodeToBytes(decoded) shouldBe bytes
      }

      test("the answer names the slot the new move takes over") {
        val bytes = "${ENTITY_LE}019a00".hexToBytes()
        val decoded = MoveLearnReplyPacketCodec.decodeBytes(bytes)
        decoded shouldBe MoveLearnReplyPacket(ENTITY_ID, 1, FURY_SWIPES)
        MoveLearnReplyPacketCodec.encodeToBytes(decoded) shouldBe bytes
      }

      test("declining answers with no slot and the move it turned down") {
        val bytes = "${ENTITY_LE}ff9a00".hexToBytes()
        val decoded = MoveLearnReplyPacketCodec.decodeBytes(bytes)
        decoded shouldBe MoveLearnReplyPacket(ENTITY_ID, MOVE_LEARN_NO_SLOT, FURY_SWIPES)
        MoveLearnReplyPacketCodec.encodeToBytes(decoded) shouldBe bytes
      }

      // 32710 sends `02` then two move ids; the official client reads that `02` as a slot and stops
      // after the
      // first id. A one-move 32710 prompt is the same eleven bytes either way and so proves
      // nothing, which is why the two-move one is the case worth keeping.
      test("a 32710 prompt for two moves is not this packet") {
        shouldThrowAny {
          MoveLearnPromptPacketCodec.decodeBytes(fixture("game/s2c/17/prompt_two_moves_32710.bin"))
        }
      }

      test("a 32710 reply carries a whole moveset, not a slot") {
        shouldThrowAny {
          MoveLearnReplyPacketCodec.decodeBytes(fixture("game/c2s/0a/reply_swapped_32710.bin"))
        }
        shouldThrowAny {
          MoveLearnReplyPacketCodec.decodeBytes(fixture("game/c2s/0a/reply_declined_32710.bin"))
        }
      }
    })
