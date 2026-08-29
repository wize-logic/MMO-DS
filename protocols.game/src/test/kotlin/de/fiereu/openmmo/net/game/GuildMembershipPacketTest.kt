package de.fiereu.openmmo.net.game

import de.fiereu.openmmo.common.test.decodeBytes
import de.fiereu.openmmo.common.test.encodeToBytes
import de.fiereu.openmmo.net.game.packets.guild.GuildMembershipPacket
import de.fiereu.openmmo.net.game.packets.guild.GuildMembershipPacketCodec
import de.fiereu.openmmo.net.game.packets.guild.GuildProfileData
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

class GuildMembershipPacketTest :
    FunSpec({
      test("not in a guild encodes to a single flag byte") {
        val pkt = GuildMembershipPacket(inGuild = false, profile = null)
        GuildMembershipPacketCodec.encodeToBytes(pkt) shouldBe byteArrayOf(0)
        GuildMembershipPacketCodec.decodeBytes(byteArrayOf(0)) shouldBe pkt
      }

      test(
          "in a guild walks gs: id, name, tag, founded, message, unknown, five shorts, expiry, U8 names") {
            val pkt =
                GuildMembershipPacket(
                    inGuild = true,
                    profile =
                        GuildProfileData(
                            guildId = 1L,
                            name = "A",
                            tag = "B",
                            foundedAt = 0,
                            message = "",
                            unknown = 0,
                            perms = listOf(5, 5, 5, 0, 0),
                            expiry = 0,
                            rankNames = listOf("", "", "", "", "", ""),
                        ),
                )
            val bytes = GuildMembershipPacketCodec.encodeToBytes(pkt)
            bytes[0] shouldBe 1
            GuildMembershipPacketCodec.decodeBytes(bytes) shouldBe pkt
          }
    })
