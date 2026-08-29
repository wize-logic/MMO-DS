package de.fiereu.openmmo.net.game

import de.fiereu.openmmo.common.test.decodeBytes
import de.fiereu.openmmo.common.test.encodeToBytes
import de.fiereu.openmmo.net.game.packets.FriendAppearance
import de.fiereu.openmmo.net.game.packets.FriendListEntry
import de.fiereu.openmmo.net.game.packets.FriendListPacket
import de.fiereu.openmmo.net.game.packets.FriendListPacketCodec
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

class FriendListPacketTest :
    FunSpec({
      test("roundtrips entries with mixed online state and variable-length names") {
        val pkt =
            FriendListPacket(
                mode = 0,
                entries =
                    listOf(
                        FriendListEntry(
                            player = 0x0102030405069000L,
                            unknown = 1_600_000_000,
                            online = false,
                            appearance =
                                FriendAppearance(
                                    name = "Alpha",
                                    unk0 = 0,
                                    lastSeen = 1_700_000_000,
                                    kind = 0,
                                    packedSlots = 0,
                                    sprite = listOf(1, 2, 3, 4),
                                ),
                        ),
                        FriendListEntry(
                            player = 0xA7L,
                            unknown = 1_650_000_000,
                            online = true,
                            appearance =
                                FriendAppearance(
                                    name = "BravoBravoBravo",
                                    unk0 = 0,
                                    lastSeen = 1_710_000_000,
                                    kind = 1,
                                    packedSlots = 0,
                                    sprite = listOf(0, 0, 0, 0),
                                ),
                        ),
                    ),
            )
        val decoded = FriendListPacketCodec.decodeBytes(FriendListPacketCodec.encodeToBytes(pkt))
        decoded shouldBe pkt
        decoded.entries.map { it.appearance.name } shouldBe listOf("Alpha", "BravoBravoBravo")
        decoded.entries.map { it.online } shouldBe listOf(false, true)
      }
    })
