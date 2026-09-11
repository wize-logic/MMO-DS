package de.fiereu.openmmo.server.game.offline

import de.fiereu.bytecodec.CodecException
import de.fiereu.openmmo.common.ContestConditions
import de.fiereu.openmmo.common.ContestRank
import de.fiereu.openmmo.common.ContestType
import de.fiereu.openmmo.common.HealLocation
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.PokemonStat
import de.fiereu.openmmo.common.superContestRibbonBit
import de.fiereu.openmmo.maps.generated.sinnoh.SpawnLocations
import io.kotest.core.spec.style.StringSpec
import io.kotest.matchers.collections.shouldContainAll
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe
import io.kotest.matchers.string.shouldContain

/** The save report, read as the client writes it. */
class OfflineSaveWireTest :
    StringSpec({
      "a report the client wrote reads back field for field" {
        val wire = OfflineSaveWire.decode(hex(VECTOR))

        wire.saveSha256 shouldBe "a6f3595e6375180db81a5f63664d73b661c1c8310450a1468de72bde4041bce2"
        wire.clientRevision shouldBe 41
        wire.trainerId shouldBe 24680
        wire.money shouldBe 123456
        wire.badges shouldBe 0x07
        wire.playTimeSeconds shouldBe 7265
        wire.position shouldBe WirePosition(bankId = 1, mapId = 155, x = 116, y = 886)
        // Not a position: row 2 of the cartridge's spawn table, which is Sandgem Town's Pokemon
        // Center. The save was left standing in Twinleaf Town, which is no row at all, so a reader
        // that took the black-out warp from the position would have nothing to take.
        wire.blackOutWarpId shouldBe 2

        wire.monsters.size shouldBe 2
        val lead = wire.monsters[0]
        lead.pid shouldBe 305419896
        lead.dexId shouldBe 387
        lead.level shouldBe 22
        lead.xp shouldBe 10648
        // The engine's params run HP, ATK, DEF, SPEED, SPATK, SPDEF and the wire runs the
        // server's order; the client's own table is what turns one into the other, so a swap
        // there lands here as Speed holding what Special Attack should.
        lead.ivs shouldBe
            mapOf(
                PokemonStat.HP to 1,
                PokemonStat.ATTACK to 2,
                PokemonStat.DEFENSE to 3,
                PokemonStat.SP_ATTACK to 4,
                PokemonStat.SP_DEFENSE to 5,
                PokemonStat.SPEED to 6)
        lead.evs[PokemonStat.SP_DEFENSE] shouldBe 8
        lead.moves shouldBe listOf(OfflineMove(33, 30, 1), OfflineMove(45, 40, 0))
        lead.nickname shouldBe "TURTWIG"
        lead.otName shouldBe "PROBE"
        lead.otId shouldBe 24680
        lead.abilityId shouldBe 65
        lead.natureByte shouldBe 21
        lead.isShiny shouldBe true
        lead.heldItemId shouldBe 5017
        lead.friendship shouldBe 70
        lead.isEgg shouldBe false
        lead.container shouldBe PokemonContainer.PARTY
        lead.containerSlot shouldBe 0
        // The contest half. The five conditions are in ContestType order, so a swap on either side
        // lands here as Beauty holding what Cool should.
        lead.conditions shouldBe
            ContestConditions(cool = 10, beauty = 20, cute = 30, smart = 40, tough = 50)
        lead.sheen shouldBe 60
        // Three ranks of Cool and the last rank of Tough: the first three bits of the twenty and
        // the last one, which is where a mask read a byte short would lose it.
        lead.superContestRibbons shouldBe
            (superContestRibbonBit(ContestType.COOL, ContestRank.NORMAL) or
                superContestRibbonBit(ContestType.COOL, ContestRank.GREAT) or
                superContestRibbonBit(ContestType.COOL, ContestRank.ULTRA) or
                superContestRibbonBit(ContestType.TOUGH, ContestRank.MASTER))
        // A location label and not a map: 16 is the game's own label for Route 201.
        lead.metLocationLabel shouldBe 16
        lead.ballItemId shouldBe 5004
        lead.pokerus shouldBe 0x24
        lead.markings shouldBe 0x05
        // Badly poisoned with the counter three turns in. The counter lives in bits 8 to 11, so a
        // reader that took the twelve-bit condition word for a byte would land here reading a
        // plain poison and lose everything the bad one had built up.
        lead.status shouldBe (MON_CONDITION_TOXIC or (3 shl MON_CONDITION_TOXIC_COUNTER_SHIFT))

        // An egg keeps its remaining cycles where a hatched monster keeps its friendship, so the
        // client sends one or the other and never both.
        val egg = wire.monsters[1]
        egg.isEgg shouldBe true
        egg.eggCyclesLeft shouldBe 20
        egg.friendship shouldBe 0
        egg.container shouldBe PokemonContainer.PC
        egg.containerSlot shouldBe 31
        // An egg has been to no contest and was caught nowhere, so its half of all this is zero,
        // which is the case a reader that is one field out of step gets wrong quietly.
        egg.conditions shouldBe ContestConditions.NONE
        egg.superContestRibbons shouldBe 0L
        egg.metLocationLabel shouldBe 0
        egg.ballItemId shouldBe 0
        // An egg is in no condition at all, and so is every monster in a box: the client engine
        // keeps a condition in the half of the record only a party member has.
        egg.status shouldBe 0

        wire.bag shouldBe listOf(OfflineItem(5017, 10), OfflineItem(5013, 3))
        wire.dexSeen shouldBe setOf(387, 390, 393)
        wire.dexCaught shouldBe setOf(387, 393)
        wire.flagIds shouldBe listOf(1, 271, 4096)
        wire.varIds shouldBe listOf(16531 to 3, 16400 to -2)
        wire.blocks.keys shouldBe setOf(23)
        wire.blocks.getValue(23).toList() shouldBe
            listOf(0xDE, 0xAD, 0xBE, 0xEF).map { it.toByte() }
      }

      "what it decodes to is what it encodes back" {
        val wire = OfflineSaveWire.decode(hex(VECTOR))
        OfflineSaveWire.encode(wire).toList() shouldBe hex(VECTOR).toList()
      }

      "a file that is not a report is refused by name, not by exception" {
        val e =
            runCatching {
                  OfflineSaveWire.decode(
                      byteArrayOf(
                          4,
                          'N'.code.toByte(),
                          'O'.code.toByte(),
                          'P'.code.toByte(),
                          'E'.code.toByte(),
                          1,
                          0))
                }
                .exceptionOrNull()
        (e is CodecException) shouldBe true
        e!!.message.shouldNotBeNull() shouldContain "not a save report"
      }

      "a report of a version this server does not read is refused, not guessed at" {
        val bytes = hex(VECTOR)
        bytes[5] = 99
        val e = runCatching { OfflineSaveWire.decode(bytes) }.exceptionOrNull()
        (e is CodecException) shouldBe true
        e!!.message.shouldNotBeNull() shouldContain "version 99"
      }

      // The badges and the Pokedex are story flags on this server and nothing else.
      "the checked badges and Pokedex land as the keys this server keeps them under" {
        val save = OfflineImportRequests.toSave(OfflineSaveWire.decode(hex(VECTOR)), regionId = 3)
        val landed = with(OfflineImportRequests) { save.withEarnedKeys("sinnoh") }

        landed.storyFlags shouldContainAll
            setOf(
                "sinnoh/BADGE_ID_COAL",
                "sinnoh/BADGE_ID_FOREST",
                "sinnoh/BADGE_ID_COBBLE",
                "sinnoh/DEX_SEEN_390",
                "sinnoh/DEX_CAUGHT_387",
                "sinnoh/DEX_CAUGHT_393")
        landed.storyFlags.count { it.contains("BADGE") } shouldBe 3
        // A badge whose bit is clear must not appear at all: the whole flag set is replaced, so a
        // stray key here is a badge nobody earned.
        landed.storyFlags.none { it == "sinnoh/BADGE_ID_FEN" } shouldBe true
        // The script VM's own flags cross under their own prefix and keep the engine's numbers.
        landed.storyFlags shouldContainAll setOf("sinnoh/vm/flag/271", "sinnoh/vm/flag/4096")
        landed.storyVars["sinnoh/vm/var/16531"] shouldBe 3
        landed.position.regionId shouldBe 3
        landed.position.bankId shouldBe 1
        landed.position.mapId shouldBe 155
        landed.blackOutWarpId shouldBe 2
      }

      // The row is a place only the spawn table can name, and the table is generated from the same
      // decomp the client's engine is built from. Sandgem Town's Center is bank 1 map 164 at 8,6,
      // which HealLocationTest pins against that decomp from the other side.
      "the save's black-out row names the heal location this server keeps" {
        val save = OfflineImportRequests.toSave(OfflineSaveWire.decode(hex(VECTOR)), regionId = 3)
        SpawnLocations.healLocation(save.blackOutWarpId) shouldBe
            HealLocation(regionId = 3, bankId = 1, mapId = 164.toByte(), x = 8, y = 6)
        // A save that has never been in a Center says 0, which is no row and no location.
        SpawnLocations.healLocation(0) shouldBe null
      }
    })

/**
 * The client engine's own condition bits, which the report carries verbatim: bad poison at bit 7,
 * and the count of turns it has been running at bits 8 to 11.
 */
private const val MON_CONDITION_TOXIC = 1 shl 7

private const val MON_CONDITION_TOXIC_COUNTER_SHIFT = 8

private fun hex(s: String): ByteArray =
    ByteArray(s.length / 2) { s.substring(it * 2, it * 2 + 2).toInt(16).toByte() }

private const val VECTOR =
    "044f4d4952040040006136663335393565363337353138306462383161356636333636346437" +
        "3362363631633163383331303435306131343638646537326264653430343162636532290000" +
        "006860000040e2010007000000611c0000019b00740076030200020078563412830100169829" +
        "000001020304050600020406080a0221001e012d002800070054555254574947050050524f42" +
        "4568600000410000150199134600000000000a141e28323c070008000000000010008c132405" +
        "8003123456788901000587000000000000000000000000000000013700280003004547470500" +
        "50524f42456860000000000000000000000114011f0000000000000000000000000000000000" +
        "000000000000020099130a00951303000300830186018901020083018901030001000f010010" +
        "0200934003001040feff01170400deadbeef"
