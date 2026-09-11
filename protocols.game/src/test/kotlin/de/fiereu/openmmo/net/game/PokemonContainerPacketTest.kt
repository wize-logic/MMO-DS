package de.fiereu.openmmo.net.game

import de.fiereu.openmmo.common.ContestConditions
import de.fiereu.openmmo.common.ContestRank
import de.fiereu.openmmo.common.ContestType
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.superContestRibbonBit
import de.fiereu.openmmo.common.superContestRibbonCount
import de.fiereu.openmmo.common.test.decodeBytes
import de.fiereu.openmmo.common.test.encodeToBytes
import de.fiereu.openmmo.common.test.fixture
import de.fiereu.openmmo.net.game.codecs.PokemonCodec
import de.fiereu.openmmo.net.game.packets.PokemonContainerPacket
import de.fiereu.openmmo.net.game.packets.PokemonContainerPacketCodec
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import java.time.LocalDateTime

private fun testMon(id: Long, dex: Int, slot: Short) =
    Pokemon(
        id = id,
        ownerId = 0x9000L,
        container = PokemonContainer.PARTY,
        containerSlot = slot,
        dexId = dex,
        seed = 0,
        ot = "Test",
        nickname = "",
        level = 5,
        hp = 20,
        xp = 100,
        eVs = EVs(),
        iVs = IVs(),
        moves = List(4) { PokemonMove(0, 0) },
        isShiny = false,
        hasHiddenAbility = false,
        isAlpha = false,
        isSecret = false,
        isFatefulEncounter = false,
        isRaidEncounter = false,
        caughtAt = LocalDateTime.of(2026, 1, 1, 0, 0, 0),
    )

class PokemonContainerPacketTest :
    FunSpec({
      test("round-trips a captured party container and exposes the monster fields") {
        val bytes = bytes()
        val decoded = PokemonContainerPacketCodec.decodeBytes(bytes)
        val mon = decoded.pokemon.single()
        mon.dexId shouldBe 495
        mon.ot shouldBe "Test"
        mon.level shouldBe 5.toByte()
        mon.hp shouldBe 20.toShort()
        mon.moves.map { it.id } shouldBe listOf<Short>(33, 43, 0, 0)
        mon.containerSlot shouldBe 0.toShort()
        // Starter monsters roll fixed 15s across every stat.
        mon.iVs.total shouldBe 90
        mon.iVs.hp shouldBe 15
        mon.caughtAt shouldBe LocalDateTime.of(2026, 7, 12, 12, 20, 22)
        mon.isShiny shouldBe false
        mon.isEgg shouldBe false
        PokemonContainerPacketCodec.encodeToBytes(decoded) shouldBe bytes
      }

      test("EVs, rarity flags, and egg survive a monster codec round-trip") {
        val mon = PokemonContainerPacketCodec.decodeBytes(bytes()).pokemon.single()
        // Distinct per-stat values catch a wrong wire order (speed is the fourth EV byte).
        val evs =
            EVs().apply {
              hp = 4
              atk = 8
              def = 12
              spAtk = 16
              spDef = 20
              spd = 24
            }
        val rare =
            mon.copy(
                eVs = evs,
                isShiny = true,
                hasHiddenAbility = false,
                isAlpha = true,
                isSecret = true,
                isFatefulEncounter = false,
                isRaidEncounter = true,
                isEgg = true,
                form = 1,
            )
        val roundTripped = PokemonCodec.decodeBytes(PokemonCodec.encodeToBytes(rare))
        roundTripped.eVs.hp shouldBe 4
        roundTripped.eVs.atk shouldBe 8
        roundTripped.eVs.def shouldBe 12
        roundTripped.eVs.spAtk shouldBe 16
        roundTripped.eVs.spDef shouldBe 20
        roundTripped.eVs.spd shouldBe 24
        roundTripped.isShiny shouldBe true
        roundTripped.hasHiddenAbility shouldBe false
        roundTripped.isAlpha shouldBe true
        roundTripped.isSecret shouldBe true
        roundTripped.isFatefulEncounter shouldBe false
        roundTripped.isRaidEncounter shouldBe true
        roundTripped.isEgg shouldBe true
        roundTripped.form shouldBe 1
      }

      test("contest conditions and sheen ride the bytes the record already had") {
        val mon = PokemonContainerPacketCodec.decodeBytes(bytes()).pokemon.single()
        // A monster that has never entered a contest reads as zeroes, and the captured bytes it
        // came from are unchanged by writing it back, the whole point of using fields the record
        // already carries rather than adding any.
        mon.conditions shouldBe ContestConditions.NONE
        mon.sheen shouldBe 0
        mon.superContestRibbons shouldBe 0L

        // Distinct per-type values catch a wrong order the way the EV test does.
        val entered =
            mon.copy(
                conditions =
                    ContestConditions(cool = 10, beauty = 20, cute = 30, smart = 40, tough = 50),
                sheen = 200,
            )
        val roundTripped = PokemonCodec.decodeBytes(PokemonCodec.encodeToBytes(entered))
        roundTripped.conditions.cool shouldBe 10
        roundTripped.conditions.beauty shouldBe 20
        roundTripped.conditions.cute shouldBe 30
        roundTripped.conditions.smart shouldBe 40
        roundTripped.conditions.tough shouldBe 50
        roundTripped.sheen shouldBe 200
        // The bytes are the record's own, so a contest-entered monster is exactly as long as one
        // that has never competed.
        PokemonCodec.encodeToBytes(entered).size shouldBe PokemonCodec.encodeToBytes(mon).size
      }

      test("super contest ribbons ride the record's trailing list") {
        val mon = PokemonContainerPacketCodec.decodeBytes(bytes()).pokemon.single()
        val mask =
            superContestRibbonBit(ContestType.COOL, ContestRank.GREAT) or
                superContestRibbonBit(ContestType.TOUGH, ContestRank.MASTER)
        val decorated = mon.copy(superContestRibbons = mask)
        val bytes = PokemonCodec.encodeToBytes(decorated)
        val roundTripped = PokemonCodec.decodeBytes(bytes)
        roundTripped.superContestRibbons shouldBe mask
        // The rank gate reads the mask this way: one Cool ribbon lets this monster enter Great.
        superContestRibbonCount(mask, ContestType.COOL) shouldBe 1
        superContestRibbonCount(mask, ContestType.CUTE) shouldBe 0

        // The mask has no field of its own, so a monster carrying one is ten bytes longer, and a
        // monster carrying none writes no entry at all, which is what keeps every record this
        // server has ever sent byte-for-byte what it was.
        bytes.size shouldBe PokemonCodec.encodeToBytes(mon).size + 10
      }

      test("where a monster was caught rides the same trailing list") {
        val mon = PokemonContainerPacketCodec.decodeBytes(bytes()).pokemon.single()
        // A record that carries no place decodes as -1, not as region 0 bank 0 map 0, which is a
        // real map, and the difference is a Pokemon claiming to have been caught in Twinleaf Town.
        mon.caughtRegionId shouldBe -1
        mon.caughtBankId shouldBe -1
        mon.caughtMapId shouldBe -1

        // Distinct values in the three catch a wrong order the way the EV test does. Map 200 is
        // past a signed byte, which is where the server's own Byte-wide position ids wrap.
        val placed = mon.copy(caughtRegionId = 0, caughtBankId = 3, caughtMapId = 200)
        val roundTripped = PokemonCodec.decodeBytes(PokemonCodec.encodeToBytes(placed))
        roundTripped.caughtRegionId shouldBe 0
        roundTripped.caughtBankId shouldBe 3
        roundTripped.caughtMapId shouldBe 200
        // Eight bytes for the place, two for its tag and length.
        PokemonCodec.encodeToBytes(placed).size shouldBe PokemonCodec.encodeToBytes(mon).size + 8
      }

      test("the held item rides the same trailing list") {
        val mon = PokemonContainerPacketCodec.decodeBytes(bytes()).pokemon.single()
        // Nothing held is 0, and a record with no entry says exactly that, which is what every
        // record this server sent before the party menu's give reached the record was saying.
        mon.heldItemId shouldBe 0

        // Leftovers, the wire id the bag and the shop use for it.
        val holding = mon.copy(heldItemId = 5234)
        val bytes = PokemonCodec.encodeToBytes(holding)
        PokemonCodec.decodeBytes(bytes).heldItemId shouldBe 5234
        // Two bytes for the item, two for its tag and length.
        bytes.size shouldBe PokemonCodec.encodeToBytes(mon).size + 4
      }

      test("the location label a save gives rides the trailing list on its own") {
        val mon = PokemonContainerPacketCodec.decodeBytes(bytes()).pokemon.single()
        // 0 is the label the game draws as the Mystery Zone, which is exactly what a record with
        // no entry has always shown, so an absent entry and a zero mean the same thing here.
        mon.caughtLocationLabel shouldBe 0

        // 34 is a label and not a map id: the two live in separate entries because a label names
        // up to 46 map headers and neither reads back out of the other.
        val labelled = mon.copy(caughtLocationLabel = 34)
        val bytes = PokemonCodec.encodeToBytes(labelled)
        PokemonCodec.decodeBytes(bytes).caughtLocationLabel shouldBe 34
        PokemonCodec.decodeBytes(bytes).caughtMapId shouldBe -1
        // Two bytes for the label, two for its tag and length.
        bytes.size shouldBe PokemonCodec.encodeToBytes(mon).size + 4
      }

      test("a place, a ribbon mask and a held item all fit the trailing list") {
        // Three entries is the case the walk exists for: the reader has to step over the ones it
        // is not looking for by their own length rather than stopping at them.
        val mon = PokemonContainerPacketCodec.decodeBytes(bytes()).pokemon.single()
        val mask = superContestRibbonBit(ContestType.COOL, ContestRank.GREAT)
        val both =
            mon.copy(
                superContestRibbons = mask,
                caughtRegionId = 0,
                caughtBankId = 3,
                caughtMapId = 200,
                caughtLocationLabel = 34,
                heldItemId = 5234)
        val roundTripped = PokemonCodec.decodeBytes(PokemonCodec.encodeToBytes(both))
        roundTripped.superContestRibbons shouldBe mask
        roundTripped.caughtMapId shouldBe 200
        roundTripped.caughtBankId shouldBe 3
        roundTripped.caughtLocationLabel shouldBe 34
        roundTripped.heldItemId shouldBe 5234
      }

      test("a two-monster party container round-trips") {
        val packet =
            PokemonContainerPacket(
                container = PokemonContainer.PARTY,
                hasChange = true,
                delete = false,
                pokemon =
                    listOf(
                        testMon(0x000000000001C000L, dex = 495, slot = 0),
                        testMon(0x000000000002C000L, dex = 504, slot = 1),
                    ),
            )
        val bytes = PokemonContainerPacketCodec.encodeToBytes(packet)
        val decoded = PokemonContainerPacketCodec.decodeBytes(bytes)
        decoded.pokemon.size shouldBe 2
        decoded.pokemon[0].dexId shouldBe 495
        decoded.pokemon[1].dexId shouldBe 504
        decoded.pokemon[1].containerSlot shouldBe 1.toShort()
        PokemonContainerPacketCodec.encodeToBytes(decoded) shouldBe bytes
      }

      // The third flag bit gates a halfword the game client reads before the record list. Nothing
      // here sets it, but a packet that does has to be crossed at its own width: two bytes read as
      // the record count put every record after it at the wrong offset.
      test("the flagged halfword sits between the flags and the record list") {
        val packet =
            PokemonContainerPacket(
                container = PokemonContainer.PARTY,
                hasChange = true,
                delete = false,
                pokemon = listOf(testMon(0x000000000001C000L, dex = 495, slot = 0)),
                unknown = 0x1234,
            )
        val bytes = PokemonContainerPacketCodec.encodeToBytes(packet)
        bytes[1] shouldBe 0x05.toByte() // hasChange | the halfword's own bit
        bytes[2] shouldBe 0x34.toByte()
        bytes[3] shouldBe 0x12.toByte()
        bytes[4] shouldBe 0x01.toByte() // the record count, two bytes further on

        val decoded = PokemonContainerPacketCodec.decodeBytes(bytes)
        decoded.unknown shouldBe 0x1234
        decoded.pokemon.single().dexId shouldBe 495
        PokemonContainerPacketCodec.encodeToBytes(decoded) shouldBe bytes
      }

      // The record carries its own container byte, and it is not always the party: a monster in
      // the PC said "party" there until the codec wrote what it was given.
      test("a record names the container it is in") {
        val stored =
            testMon(0x000000000003C000L, dex = 25, slot = 3).copy(container = PokemonContainer.PC)
        val packet =
            PokemonContainerPacket(
                container = PokemonContainer.PC,
                hasChange = true,
                delete = false,
                pokemon = listOf(stored),
            )
        val bytes = PokemonContainerPacketCodec.encodeToBytes(packet)
        val decoded = PokemonContainerPacketCodec.decodeBytes(bytes)
        decoded.container shouldBe PokemonContainer.PC
        decoded.pokemon.single().container shouldBe PokemonContainer.PC
      }

      // A monster caught from the wild keeps the moves it had, and a low-level one has fewer
      // than four.
      test("encodes a monster holding fewer than four moves, padding the empty slots") {
        val twoMoves =
            testMon(0x000000000002B000L, dex = 16, slot = 0)
                .copy(moves = listOf(PokemonMove(33, 35), PokemonMove(45, 40)))
        val packet =
            PokemonContainerPacket(
                container = PokemonContainer.PARTY,
                hasChange = true,
                delete = false,
                pokemon = listOf(twoMoves),
            )

        val decoded =
            PokemonContainerPacketCodec.decodeBytes(
                PokemonContainerPacketCodec.encodeToBytes(packet))

        val mon = decoded.pokemon.single()
        mon.moves.map { it.id } shouldBe listOf<Short>(33, 45, 0, 0)
        mon.moves.map { it.pp } shouldBe listOf<Byte>(35, 40, 0, 0)
      }
    })

private fun bytes(): ByteArray = fixture("game/s2c/13/party_scrubbed.bin")
