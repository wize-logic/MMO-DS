package de.fiereu.openmmo.net.game

import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.test.decodeBytes
import de.fiereu.openmmo.common.test.encodeToBytes
import de.fiereu.openmmo.net.game.packets.PokemonMove
import de.fiereu.openmmo.net.game.packets.PokemonMovePacket
import de.fiereu.openmmo.net.game.packets.PokemonMovePacketCodec
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

class PokemonMovePacketTest :
    FunSpec({
      // The game client writes a byte pair count and then, per pair, the source container byte, the
      // source slot as a little-endian short, and the same two for the destination.
      test("lays out a pair the way the game client writes one") {
        val deposit =
            PokemonMovePacket(
                listOf(PokemonMove(PokemonContainer.PARTY, 2, PokemonContainer.PC, 517)))
        PokemonMovePacketCodec.encodeToBytes(deposit) shouldBe
            byteArrayOf(0x01, 0x01, 0x02, 0x00, 0x00, 0x05, 0x02)
      }

      test("carries a batch and round-trips it") {
        val batch =
            PokemonMovePacket(
                listOf(
                    PokemonMove(PokemonContainer.PC, 0, PokemonContainer.PARTY, 5),
                    PokemonMove(PokemonContainer.PARTY, 5, PokemonContainer.PC, 0),
                ))
        val bytes = PokemonMovePacketCodec.encodeToBytes(batch)
        bytes.size shouldBe 1 + 2 * 6
        PokemonMovePacketCodec.decodeBytes(bytes) shouldBe batch
      }
    })
