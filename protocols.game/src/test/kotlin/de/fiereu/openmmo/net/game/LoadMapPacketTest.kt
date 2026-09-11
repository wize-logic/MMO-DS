package de.fiereu.openmmo.net.game

import de.fiereu.openmmo.common.enums.Lighting
import de.fiereu.openmmo.common.enums.MapType
import de.fiereu.openmmo.common.enums.Weather
import de.fiereu.openmmo.common.test.assertBytesRoundtrip
import de.fiereu.openmmo.common.test.decodeBytes
import de.fiereu.openmmo.common.test.encodeToBytes
import de.fiereu.openmmo.common.test.fixture
import de.fiereu.openmmo.common.utils.GzipTooLargeException
import de.fiereu.openmmo.common.utils.gzipCompress
import de.fiereu.openmmo.net.game.packets.LoadMapPacket
import de.fiereu.openmmo.net.game.packets.LoadMapPacketCodec
import de.fiereu.openmmo.net.game.packets.MapData
import io.kotest.assertions.throwables.shouldThrow
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

class LoadMapPacketTest :
    FunSpec({
      test("a captured GBA map decodes to its grid, scene and connections") {
        val bytes = fixture("game/s2c/10/load_map_31914.bin")

        val packet = LoadMapPacketCodec.decodeBytes(bytes)
        val gba = packet.mapData as MapData.GbaMapData

        packet.regionId shouldBe 0
        packet.bankId shouldBe 3
        packet.mapId shouldBe 19
        gba.width shouldBe 24
        gba.height shouldBe 40
        gba.connections.map { it.targetBank to it.targetMap } shouldBe listOf(3 to 1, 3 to 0)
        LoadMapPacketCodec.assertBytesRoundtrip(bytes)
      }

      // The byte after the NDS branch's leading halfword sizes a list of halfword pairs.
      test("an NDS map's pair list is a count, and the scene follows the list") {
        val bytes =
            byteArrayOf(
                0x03, // deleteCache | reloadPlayer
                0x04, // region 4, an NDS region
                0x01, // bank
                0x02, // map
                0x00, // reserved
                0x2c,
                0x01, // the unnamed halfword, 300
                0x02, // two pairs follow
                0x11,
                0x00,
                0x22,
                0x00,
                0x33,
                0x00,
                0x44,
                0x00,
                Lighting.entries.indexOf(Lighting.REGULAR).toByte(),
                Weather.entries.indexOf(Weather.REGULAR_WEATHER).toByte(),
                MapType.entries.indexOf(MapType.INSIDE).toByte(),
            )

        val packet = LoadMapPacketCodec.decodeBytes(bytes)
        val nds = packet.mapData as MapData.NdsMapData

        nds.unknown shouldBe 300
        nds.unknownPairs shouldBe
            listOf(
                MapData.NdsUnknownPair(0x11, 0x22),
                MapData.NdsUnknownPair(0x33, 0x44),
            )
        nds.lighting shouldBe Lighting.REGULAR
        nds.weather shouldBe Weather.REGULAR_WEATHER
        nds.mapType shouldBe MapType.INSIDE
        LoadMapPacketCodec.assertBytesRoundtrip(bytes)
      }

      // The GBA branch carries a gzip blob whose compressed side a frame bounds and whose
      // unpacked side nothing did.
      test("a gzip bomb inside a map is refused rather than unpacked") {
        val bomb = ByteArray(20 * 1024 * 1024).gzipCompress()
        val head =
            byteArrayOf(
                0x03, // deleteCache | reloadPlayer
                0x00, // region 0, a GBA region
                0x01, // bank
                0x02, // map
                0x00, // reserved
            )
        val body =
            le32(1) + // width
                le32(1) + // height
                le32(0) + // paletteIdx1
                le32(0) + // paletteIdx2
                byteArrayOf(
                    0x01, // borderWidth
                    0x01, // borderHeight
                    0x00,
                    0x00, // the unnamed halfword
                    0x00, // the unnamed byte
                    Lighting.entries.indexOf(Lighting.REGULAR).toByte(),
                    Weather.entries.indexOf(Weather.REGULAR_WEATHER).toByte(),
                    MapType.entries.indexOf(MapType.INSIDE).toByte(),
                    0x00, // encounter type
                    0x00,
                    0x00, // the one border tile
                    0x01, // a compressed blob follows
                ) +
                le32(bomb.size) +
                bomb +
                byteArrayOf(
                    0x00, // no connections
                    0x00, // no trailer
                )

        // Small enough to arrive in one frame, and twenty megabytes on the way out.
        (bomb.size < 0xFFFF) shouldBe true
        shouldThrow<GzipTooLargeException> { LoadMapPacketCodec.decodeBytes(head + body) }
      }

      test("an NDS map with no pairs is eleven bytes long") {
        val packet =
            LoadMapPacket(
                reloadPlayer = true,
                deleteCache = false,
                regionId = 4,
                bankId = 1,
                mapId = 2,
                mapData =
                    MapData.NdsMapData(Lighting.REGULAR, Weather.REGULAR_WEATHER, MapType.INSIDE),
            )

        LoadMapPacketCodec.encodeToBytes(packet).size shouldBe 11
      }
    })

/** A signed 32-bit little endian field, which is how this packet writes its sizes. */
private fun le32(value: Int): ByteArray =
    byteArrayOf(
        (value and 0xFF).toByte(),
        ((value shr 8) and 0xFF).toByte(),
        ((value shr 16) and 0xFF).toByte(),
        ((value shr 24) and 0xFF).toByte(),
    )
