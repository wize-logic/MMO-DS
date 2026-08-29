package de.fiereu.openmmo.net.game

import de.fiereu.openmmo.common.enums.Direction
import de.fiereu.openmmo.common.test.decodeBytes
import de.fiereu.openmmo.common.test.encodeToBytes
import de.fiereu.openmmo.net.game.codecs.SkinSet
import de.fiereu.openmmo.net.game.packets.EntityTransportationPacket
import de.fiereu.openmmo.net.game.packets.EntityTransportationPacketCodec
import de.fiereu.openmmo.net.game.packets.LoadEntityPacket
import de.fiereu.openmmo.net.game.packets.LoadEntityPacketCodec
import de.fiereu.openmmo.net.game.packets.TRANSPORTATION_BIKE
import de.fiereu.openmmo.net.game.packets.TRANSPORTATION_DIVING
import de.fiereu.openmmo.net.game.packets.TRANSPORTATION_NONE
import de.fiereu.openmmo.net.game.packets.TRANSPORTATION_SURFING
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

/**
 * The layout is the official reader's, `the official client`: the entity id read by the shared id
 * reader, then one byte.
 */
class EntityTransportationPacketTest :
    FunSpec({
      test("is an entity id and one byte, in that order") {
        val packet =
            EntityTransportationPacket(entityId = 0x0102030405060708L, TRANSPORTATION_SURFING)
        EntityTransportationPacketCodec.encodeToBytes(packet) shouldBe
            byteArrayOf(0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x01)
      }

      test("roundtrips every bit the client reads") {
        for (bits in
            listOf(
                TRANSPORTATION_NONE,
                TRANSPORTATION_SURFING,
                TRANSPORTATION_BIKE,
                TRANSPORTATION_DIVING,
            )) {
          val packet = EntityTransportationPacket(entityId = 7, transportation = bits)
          EntityTransportationPacketCodec.decodeBytes(
              EntityTransportationPacketCodec.encodeToBytes(packet)) shouldBe packet
        }
      }

      /** A spawn and a change are the same field, so a surfing peer looks the same either way. */
      test("the spawn carries the byte this packet changes") {
        val spawn =
            LoadEntityPacket(
                entityId = 7,
                gender = 0,
                skin = SkinSet(0, emptyMap()),
                name = "Surfer",
                regionId = 3,
                bankId = 1,
                mapId = 155,
                x = 112,
                y = 891,
                z = 0,
                facing = Direction.DOWN,
                transportation = TRANSPORTATION_SURFING.toInt(),
                hasFollower = false,
                followerDexId = 0,
            )
        LoadEntityPacketCodec.decodeBytes(LoadEntityPacketCodec.encodeToBytes(spawn))
            .transportation shouldBe TRANSPORTATION_SURFING.toInt()
      }
    })
