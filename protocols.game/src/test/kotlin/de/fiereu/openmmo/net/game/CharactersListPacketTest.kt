package de.fiereu.openmmo.net.game

import de.fiereu.bytecodec.ByteArrayReadBuffer
import de.fiereu.bytecodec.U8
import de.fiereu.openmmo.common.CharacterInfo
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.compress
import de.fiereu.openmmo.common.test.decodeBytes
import de.fiereu.openmmo.common.test.encodeToBytes
import de.fiereu.openmmo.common.test.fixture
import de.fiereu.openmmo.common.test.fixtureBuffer
import de.fiereu.openmmo.net.game.codecs.CharacterInfoCodecShort
import de.fiereu.openmmo.net.game.codecs.DefaultSkinSetCodec
import de.fiereu.openmmo.net.game.codecs.PokemonCodec
import de.fiereu.openmmo.net.game.codecs.SkinSet
import de.fiereu.openmmo.net.game.codecs.SkinSetCodecNoLeading
import de.fiereu.openmmo.net.game.packets.CharacterEntry
import de.fiereu.openmmo.net.game.packets.CharactersListPacket
import de.fiereu.openmmo.net.game.packets.CharactersListPacketCodec
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.maps.shouldBeEmpty
import io.kotest.matchers.shouldBe
import java.time.LocalDateTime

// Client 32710 appends one byte to the monster record that the client this server speaks to does
// not read. The captures below are that later client's, so walking them takes the extra byte.
private const val TRAILING_BYTES_32710 = 1

private fun mon(id: Long, slot: Short, moves: List<PokemonMove>) =
    Pokemon(
        id = id,
        ownerId = 0x9000L,
        container = PokemonContainer.PARTY,
        containerSlot = slot,
        dexId = 495,
        seed = 7,
        ot = "Test",
        nickname = "",
        level = 5,
        hp = 20,
        xp = 100,
        eVs = EVs(),
        iVs = IVs(),
        moves = moves,
        isShiny = false,
        hasHiddenAbility = false,
        isAlpha = false,
        isSecret = false,
        isFatefulEncounter = false,
        isRaidEncounter = false,
        caughtAt = LocalDateTime.of(2026, 1, 1, 0, 0, 0),
    )

private fun characterInfo() =
    CharacterInfo(
        id = 1,
        name = "Test",
        userId = 1,
        rivalSex = 0,
        lastLogin = LocalDateTime.of(2026, 1, 1, 0, 0, 0),
        createdAt = LocalDateTime.of(2026, 1, 1, 0, 0, 0),
        money = 0,
        permissions = 0,
        remainingSafariSteps = 0,
        remainingSafariBalls = 0,
        pcExtraSlots = 0,
        battleBoxExtraSlots = 0,
        templateAmount = 0,
        positionRegionId = 0,
        positionBankId = 0,
        positionMapId = 1,
        positionX = 1,
        positionY = 1,
        repelLeft = 0,
        repelItemId = 0,
        lureLeft = 0,
        lureItemId = 0,
    )

class CharactersListPacketTest :
    FunSpec({
      test("a captured client 32710 character list is consumed to the last byte") {
        val buf = fixtureBuffer("game/s2c/02/character_list_32710.bin")
        val names = mutableListOf<String>()
        val parties = mutableListOf<Int>()
        val skinSlotCounts = mutableListOf<Int>()
        repeat(U8.read(buf)) {
          names += CharacterInfoCodecShort.read(buf).name
          skinSlotCounts += DefaultSkinSetCodec.read(buf).size
          SkinSetCodecNoLeading.read(buf).shouldBeEmpty()
          val hasGuild = buf.readByte().toInt() != 0
          hasGuild shouldBe false
          val party = U8.read(buf)
          parties += party
          repeat(party) {
            PokemonCodec.read(buf)
            repeat(TRAILING_BYTES_32710) { buf.readByte() }
          }
        }

        names shouldBe listOf("MacherRin", "astridefour", "MacherDer")
        parties shouldBe listOf(0, 1, 0)
        skinSlotCounts shouldBe listOf(5, 5, 5)
        buf.remaining() shouldBe 0
      }

      test("a captured client 32710 monster record is consumed to the last byte") {
        // A SocialListEntryAdd payload, which is a bare monster record.
        val buf = fixtureBuffer("game/s2c/14/monster_record_32710.bin")
        val mon = PokemonCodec.read(buf)

        mon.dexId shouldBe 4
        mon.ot shouldBe "MachRR"
        mon.level shouldBe 5.toByte()
        mon.hp shouldBe 19.toShort()
        mon.moves.map { it.id } shouldBe listOf<Short>(10, 45, 33, 52)
        buf.remaining() shouldBe TRAILING_BYTES_32710
      }

      test("the game client's own reader agrees with this codec on a captured record") {
        val bytes = fixture("game/s2c/14/monster_record_32710.bin")
        val ours = PokemonCodec.decodeBytes(bytes.copyOf(bytes.size - TRAILING_BYTES_32710))
        val theirs = OfficialMonsterRecord.fields(bytes, 0)

        theirs.end shouldBe bytes.size - TRAILING_BYTES_32710
        theirs.dexId shouldBe ours.dexId
        theirs.ot shouldBe ours.ot
        theirs.nickname shouldBe ours.nickname
        theirs.level shouldBe ours.level.toInt()
        theirs.hp shouldBe ours.hp.toInt()
        theirs.xp shouldBe ours.xp
        theirs.moveIds shouldBe ours.moves.map { it.id.toInt() }
        theirs.movePps shouldBe ours.moves.map { it.pp.toInt() }
        theirs.ivBits shouldBe ours.iVs.compress()
      }

      // A record one byte too long is invisible in a party of one, it sits at the end of the
      // packet, where nothing reads it.
      test("a party of two encodes records the game client reads back to back") {
        val party =
            listOf(
                mon(0x1C000L, slot = 0, moves = listOf(PokemonMove(33, 35), PokemonMove(45, 40))),
                mon(0x2C000L, slot = 1, moves = List(4) { PokemonMove(1, 10) }),
            )
        val first = PokemonCodec.encodeToBytes(party[0])
        val bytes = first + PokemonCodec.encodeToBytes(party[1])

        val afterFirst = OfficialMonsterRecord.read(bytes, 0)
        afterFirst shouldBe first.size
        OfficialMonsterRecord.read(bytes, afterFirst) shouldBe bytes.size

        val second = OfficialMonsterRecord.fields(bytes, afterFirst)
        second.id shouldBe 0x2C000L
        second.moveIds shouldBe listOf(1, 1, 1, 1)
      }

      test("a character list carrying a party of two is walked to its last byte") {
        val packet =
            CharactersListPacket(
                listOf(
                    CharacterEntry(
                        characterInfo = characterInfo(),
                        skinSet = SkinSet(),
                        pokemon =
                            listOf(
                                mon(0x1C000L, slot = 0, moves = List(2) { PokemonMove(33, 35) }),
                                mon(0x2C000L, slot = 1, moves = List(4) { PokemonMove(1, 10) }),
                            ),
                    )))
        val bytes = CharactersListPacketCodec.encodeToBytes(packet)

        // The entry layout around the party is the one a captured list is read with above.
        val buf = ByteArrayReadBuffer(bytes)
        U8.read(buf) shouldBe 1
        CharacterInfoCodecShort.read(buf).name shouldBe "Test"
        DefaultSkinSetCodec.read(buf).shouldBeEmpty()
        SkinSetCodecNoLeading.read(buf).shouldBeEmpty()
        buf.readByte().toInt() shouldBe 0
        val party = U8.read(buf)
        party shouldBe 2

        var at = bytes.size - buf.remaining()
        repeat(party) { at = OfficialMonsterRecord.read(bytes, at) }
        at shouldBe bytes.size
      }
    })
