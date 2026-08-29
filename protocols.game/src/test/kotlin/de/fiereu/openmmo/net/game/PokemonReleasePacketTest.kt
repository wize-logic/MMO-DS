package de.fiereu.openmmo.net.game

import de.fiereu.openmmo.common.test.decodeBytes
import de.fiereu.openmmo.common.test.encodeToBytes
import de.fiereu.openmmo.net.game.packets.PokemonReleasePacket
import de.fiereu.openmmo.net.game.packets.PokemonReleasePacketCodec
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

class PokemonReleasePacketTest :
    FunSpec({
      // The game client writes the monster id alone, as a little-endian
      // signed 64-bit value (mmo_game_write_pokemon_release).
      test("lays out the id the way the game client writes it") {
        PokemonReleasePacketCodec.encodeToBytes(PokemonReleasePacket(0x0102030405060708L)) shouldBe
            byteArrayOf(0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01)
      }

      test("round-trips an id with the top bit of a byte set") {
        val packet = PokemonReleasePacket(2319737414753227968L)
        val bytes = PokemonReleasePacketCodec.encodeToBytes(packet)
        bytes.size shouldBe 8
        PokemonReleasePacketCodec.decodeBytes(bytes) shouldBe packet
      }
    })
