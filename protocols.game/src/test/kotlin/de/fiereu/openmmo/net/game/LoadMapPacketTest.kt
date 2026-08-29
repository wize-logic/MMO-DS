package de.fiereu.openmmo.net.game

import de.fiereu.openmmo.common.enums.Lighting
import de.fiereu.openmmo.common.enums.MapType
import de.fiereu.openmmo.common.enums.Weather
import de.fiereu.openmmo.common.test.assertBytesRoundtrip
import de.fiereu.openmmo.common.test.decodeBytes
import de.fiereu.openmmo.common.test.encodeToBytes
import de.fiereu.openmmo.common.test.fixture
import de.fiereu.openmmo.net.game.packets.LoadMapPacket
import de.fiereu.openmmo.net.game.packets.LoadMapPacketCodec
import de.fiereu.openmmo.net.game.packets.MapData
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
