package de.fiereu.openmmo.net.game

import de.fiereu.bytecodec.ByteArrayReadBuffer
import de.fiereu.openmmo.common.test.fixture
import de.fiereu.openmmo.common.test.fixtureBuffer
import de.fiereu.openmmo.net.game.packets.gtl.GtlItemListing
import de.fiereu.openmmo.net.game.packets.gtl.GtlListKind
import de.fiereu.openmmo.net.game.packets.gtl.GtlPokemonListing
import de.fiereu.openmmo.net.game.packets.gtl.GtlSearchPagePacketCodec
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import java.io.ByteArrayOutputStream

/**
 * A captured page from client 32710, rewritten so its monster records are the length the client
 * this server speaks to reads.
 */
private fun withRecordsThisClientReads(bytes: ByteArray): ByteArray {
  val out = ByteArrayOutputStream()
  var at = 0
  fun take(n: Int) {
    out.write(bytes, at, n)
    at += n
  }
  take(8) // requestId, list kind, page, total matches
  val rows = bytes[at].toInt() and 0xFF
  take(1)
  repeat(rows) {
    val rowIsItem = bytes[at + 8].toInt() == GtlListKind.ITEM.id
    take(23) // listing id, row kind, price, listed at, expires at, quantity
    if (rowIsItem) {
      take(3)
    } else {
      val hasRecord = bytes[at].toInt() != 0
      take(1)
      if (hasRecord) {
        take(OfficialMonsterRecord.read(bytes, at) - at)
        at++ // the byte 32710 added to the record
        take(12) // the six board stats
      }
    }
  }
  take(bytes.size - at) // the item quote strip, when the page has one
  return out.toByteArray()
}

class GtlSearchPagePacketTest :
    FunSpec({
      test("a captured item page is consumed to the last byte") {
        val buf = fixtureBuffer("game/s2c/9b/item_page_32710.bin")
        val page = GtlSearchPagePacketCodec.read(buf)

        page.requestId shouldBe 7.toByte()
        page.listKind shouldBe GtlListKind.ITEM
        page.page shouldBe 0.toShort()
        page.totalMatches shouldBe 73
        page.listings.size shouldBe 10

        val first = page.listings.first() as GtlItemListing
        first.listingId shouldBe 0x1ACFDD4BEA482000L
        first.price shouldBe 1900
        first.quantity shouldBe 2.toShort()
        first.itemId shouldBe 1111.toShort()

        page.quotes!!.map { it.itemId } shouldBe listOf<Short>(1118, 1116, 1115, 1114, 1113, 1111)
        buf.remaining() shouldBe 0
      }

      test("a captured monster page is consumed to the last byte") {
        val bytes = withRecordsThisClientReads(fixture("game/s2c/9b/monster_page_32710.bin"))
        val buf = ByteArrayReadBuffer(bytes)
        val page = GtlSearchPagePacketCodec.read(buf)

        page.requestId shouldBe 1.toByte()
        page.listKind shouldBe GtlListKind.POKEMON
        page.totalMatches shouldBe 216090
        page.listings.size shouldBe 10

        val first = page.listings.first() as GtlPokemonListing
        first.listingId shouldBe 0x1ACFF6ACDDC82000L
        first.price shouldBe 11000
        first.pokemon!!.dexId shouldBe 246
        first.pokemon!!.ot shouldBe "DraganwMax"
        first.pokemon!!.level shouldBe 54
        // The board repeats the record's own hp as the first stat.
        first.stats shouldBe listOf<Short>(127, 88, 68, 64, 61, 68)
        first.pokemon!!.hp shouldBe 127

        page.quotes shouldBe null
        buf.remaining() shouldBe 0
      }

      test("a page that is not an item board carries no quote strip") {
        val buf = fixtureBuffer("game/s2c/9b/own_listings_page_31914.bin")
        val page = GtlSearchPagePacketCodec.read(buf)

        page.listKind shouldBe GtlListKind.OWN_LISTINGS
        page.totalMatches shouldBe 0
        page.listings shouldBe emptyList()
        page.quotes shouldBe null
        buf.remaining() shouldBe 0
      }
    })
