package de.fiereu.openmmo.server.game.services

import de.fiereu.bytecodec.GrowableWriteBuffer
import de.fiereu.openmmo.net.game.packets.PlayerVariableEntry
import de.fiereu.openmmo.net.game.packets.ScriptStatePacketCodec
import de.fiereu.openmmo.net.game.packets.StoryFlagUpdatePacket
import de.fiereu.openmmo.story.generated.hoenn.HoennFlags
import de.fiereu.openmmo.story.generated.hoenn.HoennVars
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.ints.shouldBeLessThan
import io.kotest.matchers.shouldBe

class StoryClientStateTest :
    FunSpec({
      test("maps persisted GBA flags to the captured region and numeric id") {
        StoryClientState.flags(
            regionId = 1,
            flags = setOf(HoennFlags.FLAG_VISITED_LITTLEROOT_TOWN),
        ) shouldBe listOf(StoryFlagUpdatePacket(1, 0x086f, enabled = true))
      }

      test("maps GBA variable ids to their client keys, key wide and value narrow") {
        StoryClientState.variables(
            regionId = 1,
            vars = mapOf(HoennVars.VAR_LITTLEROOT_INTRO_STATE to 7),
        ) shouldBe listOf(PlayerVariableEntry(0x92.toShort(), 7))
      }

      test("a value too wide for the wire's byte is sent truncated, not dropped") {
        StoryClientState.variables(
            regionId = 1,
            vars = mapOf(HoennVars.VAR_LITTLEROOT_INTRO_STATE to 300),
        ) shouldBe listOf(PlayerVariableEntry(0x92.toShort(), 300.toByte()))
      }

      test("does not leak another region's story state") {
        StoryClientState.flags(
            regionId = 0,
            flags = setOf(HoennFlags.FLAG_VISITED_LITTLEROOT_TOWN),
        ) shouldBe emptyList()
        StoryClientState.variables(
            regionId = 0,
            vars = mapOf(HoennVars.VAR_LITTLEROOT_INTRO_STATE to 7),
        ) shouldBe emptyList()
      }

      test("the whole of a real seat goes out untouched") {
        // The fifteen blocks this game writes come to 16,980 bytes together, the widest of them
        // being the Battle Frontier's 5,680, so nothing an honest character holds is near the cut
        // below.
        val blocks = mapOf(7 to ByteArray(812), 23 to ByteArray(5680), 10 to ByteArray(1940))
        val seat =
            StoryClientState.scriptState(
                regionId = 3,
                flags = (0 until 2000).map { "sinnoh/vm/flag/$it" },
                vars = (0 until 300).associate { "sinnoh/vm/var/$it" to 1 },
                saveBlocks = blocks,
            )

        seat.blocks.map { it.id } shouldBe listOf(7, 10, 23)
        seat.flags.size shouldBe 2000
        seat.vars.size shouldBe 300
      }

      test("a seat past what one packet carries is cut, blocks kept ahead of the rows") {
        // Twenty blocks at the widest the store will hold is 160 KB, and every packet is framed
        // behind a 16-bit length. What a character holds is the store's, so nothing but this cut
        // stops one like that being a join that dies at the seat every time.
        val seat =
            StoryClientState.scriptState(
                regionId = 3,
                flags = (0 until 400).map { "sinnoh/vm/flag/$it" },
                vars = emptyMap(),
                saveBlocks = (1..20).associateWith { ByteArray(8192) },
            )

        // Seven whole blocks fit under the cut, and the rows that follow still had room.
        seat.blocks.map { it.id } shouldBe (1..7).toList()
        seat.flags.size shouldBe 400

        val buf = GrowableWriteBuffer(4096)
        ScriptStatePacketCodec.write(buf, seat)
        buf.toByteArray().size shouldBeLessThan 0xFFFF - 2
      }
    })
