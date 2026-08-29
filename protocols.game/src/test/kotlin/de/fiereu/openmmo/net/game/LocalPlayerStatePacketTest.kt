package de.fiereu.openmmo.net.game

import de.fiereu.openmmo.common.test.decodeBytes
import de.fiereu.openmmo.common.test.encodeToBytes
import de.fiereu.openmmo.common.test.fixture
import de.fiereu.openmmo.net.game.packets.LocalPlayerStatePacketCodec
import de.fiereu.openmmo.net.game.packets.PlayerVariableEntry
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

class LocalPlayerStatePacketTest :
    FunSpec({
      test("a captured state block decodes to its map, tile, money and party") {
        val packet =
            LocalPlayerStatePacketCodec.decodeBytes(
                fixture("game/s2c/f3/local_player_state_31914.bin"),
            )

        packet.region shouldBe 0.toByte()
        packet.mapId shouldBe 19.toShort()
        packet.x shouldBe 13.toShort()
        packet.y shouldBe 2.toShort()
        packet.money shouldBe 28900
        packet.partyDex shouldBe listOf(1.toShort())
        packet.badges shouldBe emptyList()
        packet.variables.size shouldBe 7
      }

      // A variable entry is three bytes either way round, so a swapped split
      // round-trips cleanly and only the byte order tells them apart. The client
      // keys these in a short-keyed map of bytes: the key is the wide half.
      test("a variable entry writes its key as a halfword and its value as a byte") {
        val entry = PlayerVariableEntry(key = 0x0155, value = 6)
        val packet =
            LocalPlayerStatePacketCodec.decodeBytes(
                    fixture("game/s2c/f3/local_player_state_31914.bin"),
                )
                .copy(variables = listOf(entry))

        val bytes = LocalPlayerStatePacketCodec.encodeToBytes(packet)

        bytes.takeLast(5) shouldBe listOf<Byte>(0x01, 0x00, 0x55, 0x01, 0x06)
      }
    })
