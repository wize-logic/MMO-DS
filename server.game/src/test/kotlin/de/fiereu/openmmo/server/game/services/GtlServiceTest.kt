package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.CharacterGender
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.common.enums.Region
import de.fiereu.openmmo.net.game.packets.CategoryFlagsPacket
import de.fiereu.openmmo.net.game.packets.ChatMessagePacket
import de.fiereu.openmmo.net.game.packets.CreateMarketListingPacket
import de.fiereu.openmmo.net.game.packets.LocalCharacterDeltaPacket
import de.fiereu.openmmo.net.game.packets.SceneObjectStatesPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlClaimPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlConfirmPurchasePacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlItemListing
import de.fiereu.openmmo.net.game.packets.gtl.GtlListKind
import de.fiereu.openmmo.net.game.packets.gtl.GtlListingCancelPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlOpenSessionPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlPokemonListing
import de.fiereu.openmmo.net.game.packets.gtl.GtlPriceChangePacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlResultPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlSearchPagePacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlSearchPageRequestPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlSpeciesFilter
import de.fiereu.openmmo.net.game.packets.gtl.GtlTradeLogRequestPacket
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.battle.BattleRegistry
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.EntityIdService
import de.fiereu.openmmo.server.game.storage.GTL_KIND_ITEM
import de.fiereu.openmmo.server.game.storage.GTL_SORT_PRICE_DESC
import de.fiereu.openmmo.server.game.storage.GTL_STATE_ACTIVE
import de.fiereu.openmmo.server.game.storage.GTL_STATE_CLOSED
import de.fiereu.openmmo.server.game.storage.GTL_STATE_SOLD
import de.fiereu.openmmo.server.game.storage.GtlListing
import de.fiereu.openmmo.server.game.testsupport.FakeCharacterRepository
import de.fiereu.openmmo.server.game.testsupport.FakeGtlRepository
import de.fiereu.openmmo.server.game.testsupport.FakeSession
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldBeEmpty
import io.kotest.matchers.ints.shouldBeGreaterThan
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe
import io.kotest.matchers.string.shouldContain
import java.time.LocalDateTime
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.runTest

private const val TACKLE: Short = 33

/** The fee the service charges per unit: price / 40, floored at 100, capped at 50000. */
private fun fee(price: Int): Int = (price / 40).coerceAtLeast(100).coerceAtMost(50_000)

private fun mon(ownerId: Long, dexId: Int, level: Byte = 50): Pokemon =
    Pokemon(
        id = EntityIdService().newMonsterId(),
        ownerId = ownerId,
        container = PokemonContainer.PARTY,
        containerSlot = 0,
        dexId = dexId,
        seed = 0,
        ot = "Ash",
        nickname = "",
        level = level,
        hp = 123,
        xp = 0,
        eVs = EVs(),
        iVs = IVs(),
        moves =
            listOf(
                PokemonMove(TACKLE, 35), PokemonMove(0, 0), PokemonMove(0, 0), PokemonMove(0, 0)),
        isShiny = false,
        hasHiddenAbility = false,
        isAlpha = false,
        isSecret = false,
        isFatefulEncounter = false,
        isRaidEncounter = false,
        caughtAt = LocalDateTime.now(),
    )

private class GtlFixture(scope: CoroutineScope) {
  val repo = FakeCharacterRepository()
  val store = CharacterStore(repo, EntityIdService(), scope)
  val sessions = SessionRegistry()
  val shelf = FakeGtlRepository()
  val gtl = GtlService(sessions, store, shelf, SpeciesRegistry(), BattleRegistry())

  suspend fun seated(
      name: String,
      userId: Int,
      dexId: Int,
      money: Int = 0
  ): Pair<FakeSession, Long> {
    val created = store.createCharacter(userId, name, CharacterGender.MALE, Region.HOENN)
    store.addPokemon(created.info.id, mon(created.info.id, dexId))
    // A new character starts with pocket money; set the wallet to exactly what the test names.
    store.addMoney(created.info.id, money - created.info.money)
    val session = FakeSession(created.info.id)
    sessions.bindCharacter(session, created.info.id)
    return session to created.info.id
  }

  suspend fun listMon(session: FakeSession, monId: Long, price: Int) =
      gtl.onCreateListing(PacketEvent(CreateMarketListingPacket(0, monId, price, 1), session))

  suspend fun listItem(session: FakeSession, itemId: Int, quantity: Int, price: Int) =
      gtl.onCreateListing(
          PacketEvent(
              CreateMarketListingPacket(1, itemId.toLong(), price, quantity.toShort()), session))

  suspend fun buy(session: FakeSession, listingId: Long, quantity: Int = 1) =
      gtl.onConfirmPurchase(
          PacketEvent(GtlConfirmPurchasePacket(listingId, quantity.toShort()), session))

  suspend fun claim(session: FakeSession, vararg ids: Long) =
      gtl.onClaim(PacketEvent(GtlClaimPacket(ids.toList()), session))

  fun partyDex(charId: Long): List<Int> = store.getCharacter(charId)!!.pokemon.map { it.dexId }

  fun money(charId: Long): Int = store.getCharacter(charId)!!.info.money
}

private fun FakeSession.replies() = sent.filterIsInstance<ChatMessagePacket>().map { it.message }

private fun FakeSession.results() = sent.filterIsInstance<GtlResultPacket>()

@OptIn(ExperimentalCoroutinesApi::class)
class GtlServiceTest :
    FunSpec({
      test("listing a monster moves it onto the shelf and charges the fee") {
        runTest {
          val fx = GtlFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1, money = 500)
          fx.store.addPokemon(redId, mon(redId, 4))
          val listed = fx.store.getCharacter(redId)!!.pokemon.first { it.dexId == 4 }
          fx.listMon(red, listed.id, 5000)
          fx.partyDex(redId) shouldBe listOf(1)
          fx.money(redId) shouldBe 500 - fee(5000)
          val row = fx.shelf.rows.values.single()
          row.state shouldBe GTL_STATE_ACTIVE
          row.price shouldBe 5000
          row.remaining shouldBe 1
          row.fee shouldBe fee(5000)
          row.pokemon.shouldNotBeNull().dexId shouldBe 4
          red.results().last().code shouldBe GtlResultPacket.CODE_LISTED
        }
      }

      test("the last party monster cannot be listed") {
        runTest {
          val fx = GtlFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1, money = 500)
          val only = fx.store.getCharacter(redId)!!.pokemon.single()
          fx.listMon(red, only.id, 5000)
          fx.partyDex(redId) shouldBe listOf(1)
          fx.shelf.rows.values.shouldBeEmpty()
          red.replies().last() shouldContain "last party monster"
        }
      }

      test("a price under the floor refuses and keeps the goods") {
        runTest {
          val fx = GtlFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1, money = 500)
          fx.store.addPokemon(redId, mon(redId, 4))
          val listed = fx.store.getCharacter(redId)!!.pokemon.first { it.dexId == 4 }
          fx.listMon(red, listed.id, 50)
          fx.partyDex(redId) shouldBe listOf(1, 4)
          fx.shelf.rows.values.shouldBeEmpty()
          val refusal = red.results().last()
          refusal.code shouldBe GtlResultPacket.CODE_MIN_PRICE
          refusal.b shouldBe 100
        }
      }

      test("an unaffordable fee refuses and keeps the goods") {
        runTest {
          val fx = GtlFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1, money = 10)
          fx.store.addPokemon(redId, mon(redId, 4))
          val listed = fx.store.getCharacter(redId)!!.pokemon.first { it.dexId == 4 }
          fx.listMon(red, listed.id, 5000)
          fx.partyDex(redId) shouldBe listOf(1, 4)
          fx.money(redId) shouldBe 10
          fx.shelf.rows.values.shouldBeEmpty()
          red.results().last().code shouldBe GtlResultPacket.CODE_NO_FUNDS
        }
      }

      test("a search page narrows by species and carries the record") {
        runTest {
          val fx = GtlFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1, money = 1000)
          fx.store.addPokemon(redId, mon(redId, 4))
          fx.store.addPokemon(redId, mon(redId, 7))
          val four = fx.store.getCharacter(redId)!!.pokemon.first { it.dexId == 4 }
          val seven = fx.store.getCharacter(redId)!!.pokemon.first { it.dexId == 7 }
          fx.listMon(red, four.id, 5000)
          fx.listMon(red, seven.id, 300)
          fx.gtl.onSearchPage(
              PacketEvent(
                  GtlSearchPageRequestPacket(
                      requestId = 3,
                      listKind = GtlListKind.POKEMON,
                      categoryId = 0,
                      page = 0,
                      filters = listOf(GtlSpeciesFilter(listOf(4.toShort()))),
                  ),
                  red))
          val pageReply = red.sent.filterIsInstance<GtlSearchPagePacket>().single()
          pageReply.requestId shouldBe 3.toByte()
          pageReply.totalMatches shouldBe 1
          val rowOnPage = pageReply.listings.single() as GtlPokemonListing
          rowOnPage.pokemon.shouldNotBeNull().dexId shouldBe 4
          rowOnPage.price shouldBe 5000
          rowOnPage.stats[0] shouldBe 123.toShort()
        }
      }

      test("the sort byte orders an item page by price") {
        runTest {
          val fx = GtlFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1, money = 1000)
          fx.store.addItem(redId, 17, 5)
          fx.store.addItem(redId, 18, 5)
          fx.listItem(red, 17, 1, 300)
          fx.listItem(red, 18, 1, 900)
          fx.gtl.onSearchPage(
              PacketEvent(
                  GtlSearchPageRequestPacket(
                      requestId = 4,
                      listKind = GtlListKind.ITEM,
                      categoryId = GTL_SORT_PRICE_DESC.toByte(),
                      page = 0,
                      filters = emptyList(),
                  ),
                  red))
          val pageReply = red.sent.filterIsInstance<GtlSearchPagePacket>().single()
          pageReply.listings.map { it.price } shouldBe listOf(900, 300)
        }
      }

      test("a key item cannot be listed, and the bag keeps it") {
        runTest {
          val fx = GtlFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1, money = 500)
          // 5445 is the Old Rod, the exact item the report was filed with.
          fx.store.addItem(redId, 5445, 1)
          fx.listItem(red, 5445, 1, 100)
          fx.shelf.rows.values.shouldBeEmpty()
          fx.store.getCharacter(redId)!!.items[5445] shouldBe 1
          fx.money(redId) shouldBe 500
          red.replies().last() shouldContain "Key items"
        }
      }

      test("a key item listed before the wall went up cannot be bought") {
        runTest {
          val fx = GtlFixture(backgroundScope)
          val (_, redId) = fx.seated("Red", 1, 1, money = 0)
          val (blue, blueId) = fx.seated("Blue", 2, 25, money = 8000)
          val row =
              fx.shelf.insert(
                  GtlListing(
                      id = 0,
                      sellerId = redId,
                      sellerName = "Red",
                      kind = GTL_KIND_ITEM,
                      itemId = 5431, // the Poke Radar from the same report
                      quantity = 1,
                      price = 1,
                      state = GTL_STATE_ACTIVE,
                      buyerId = null,
                      buyerName = null,
                      listedAt = LocalDateTime.now(),
                      expiresAt = LocalDateTime.now().plusDays(7),
                      soldAt = null,
                      pokemon = null,
                      remaining = 1,
                  ))
          fx.buy(blue, row.id)
          blue.replies().last() shouldContain "key item"
          fx.store.getCharacter(blueId)!!.items[5431] shouldBe null
          fx.money(blueId) shouldBe 8000
          // The refusal put the unit back, so the seller can still cancel it off.
          fx.shelf.rows.getValue(row.id).remaining shouldBe 1
          fx.shelf.rows.getValue(row.id).state shouldBe GTL_STATE_ACTIVE
        }
      }

      test("a purchase parks the money until the seller claims it") {
        runTest {
          val fx = GtlFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1, money = 500)
          val (blue, blueId) = fx.seated("Blue", 2, 25, money = 8000)
          fx.store.addPokemon(redId, mon(redId, 4))
          val listed = fx.store.getCharacter(redId)!!.pokemon.first { it.dexId == 4 }
          fx.listMon(red, listed.id, 5000)
          val sellerAfterFee = fx.money(redId)
          val listingId = fx.shelf.rows.values.single().id
          fx.buy(blue, listingId)
          fx.money(blueId) shouldBe 3000
          fx.partyDex(blueId) shouldBe listOf(25, 4)
          // The buyer's own, not a row still owned by the seller: an unstamped
          // owner flushed under the seller, vanished from the buyer on the next
          // load, and collided with the seller's slots as "no room".
          fx.store.getCharacter(blueId)!!.pokemon.first { it.dexId == 4 }.ownerId shouldBe blueId
          blue.results().last().code shouldBe GtlResultPacket.CODE_BOUGHT
          blue.sent.filterIsInstance<LocalCharacterDeltaPacket>().last().money shouldBe 3000
          // The sale is announced live, but the money waits on the row for Claim.
          red.results().last().code shouldBe GtlResultPacket.CODE_SOLD_ONE
          fx.money(redId) shouldBe sellerAfterFee
          val sold = fx.shelf.rows.values.single()
          sold.state shouldBe GTL_STATE_SOLD
          sold.unclaimedUnits shouldBe 1
          fx.claim(red, listingId)
          fx.money(redId) shouldBe sellerAfterFee + 5000
          fx.shelf.rows.values.single().state shouldBe GTL_STATE_CLOSED
          red.results().last().code shouldBe GtlResultPacket.CODE_CLAIMED
        }
      }

      test("a full party sends the purchase to the buyer's own box") {
        runTest {
          val fx = GtlFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1, money = 500)
          val (blue, blueId) = fx.seated("Blue", 2, 25, money = 8000)
          repeat(5) { fx.store.addPokemon(blueId, mon(blueId, 100 + it)) }
          fx.store.addPokemon(redId, mon(redId, 4))
          val listed = fx.store.getCharacter(redId)!!.pokemon.first { it.dexId == 4 }
          fx.listMon(red, listed.id, 5000)
          fx.buy(blue, fx.shelf.rows.values.single().id)
          fx.store.getCharacter(blueId)!!.pokemon.size shouldBe 6
          val boxed = fx.store.getCharacter(blueId)!!.pcStorage.single()
          boxed.dexId shouldBe 4
          boxed.ownerId shouldBe blueId
          blue.results().last().code shouldBe GtlResultPacket.CODE_BOUGHT
        }
      }

      test("a buyer who cannot pay puts the listing back") {
        runTest {
          val fx = GtlFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1, money = 500)
          val (blue, blueId) = fx.seated("Blue", 2, 25, money = 100)
          fx.store.addPokemon(redId, mon(redId, 4))
          val listed = fx.store.getCharacter(redId)!!.pokemon.first { it.dexId == 4 }
          fx.listMon(red, listed.id, 5000)
          val listingId = fx.shelf.rows.values.single().id
          fx.buy(blue, listingId)
          fx.money(blueId) shouldBe 100
          fx.partyDex(blueId) shouldBe listOf(25)
          val row = fx.shelf.rows.values.single()
          row.state shouldBe GTL_STATE_ACTIVE
          row.remaining shouldBe 1
          row.unclaimedUnits shouldBe 0
          blue.results().last().code shouldBe GtlResultPacket.CODE_NO_FUNDS
        }
      }

      test("an item stack fills by the unit") {
        runTest {
          val fx = GtlFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1, money = 1000)
          val (blue, blueId) = fx.seated("Blue", 2, 25, money = 5000)
          fx.store.addItem(redId, 17, 5)
          fx.listItem(red, 17, 5, 900)
          (fx.store.getCharacter(redId)!!.items[17] ?: 0) shouldBe 0
          val sellerAfterFee = fx.money(redId)
          val listingId = fx.shelf.rows.values.single().id
          fx.buy(blue, listingId, quantity = 3)
          fx.store.getCharacter(blueId)!!.items[17] shouldBe 3
          fx.money(blueId) shouldBe 5000 - 3 * 900
          val partial = fx.shelf.rows.values.single()
          partial.state shouldBe GTL_STATE_ACTIVE
          partial.remaining shouldBe 2
          partial.unclaimedUnits shouldBe 3
          fx.buy(blue, listingId, quantity = 2)
          fx.shelf.rows.values.single().state shouldBe GTL_STATE_SOLD
          fx.claim(red, listingId)
          fx.money(redId) shouldBe sellerAfterFee + 5 * 900
          fx.shelf.rows.values.single().state shouldBe GTL_STATE_CLOSED
        }
      }

      test("an offline seller is told on the next session open, and still must claim") {
        runTest {
          val fx = GtlFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1, money = 500)
          val (blue, _) = fx.seated("Blue", 2, 25, money = 8000)
          fx.store.addPokemon(redId, mon(redId, 4))
          val listed = fx.store.getCharacter(redId)!!.pokemon.first { it.dexId == 4 }
          fx.listMon(red, listed.id, 5000)
          val sellerAfterFee = fx.money(redId)
          val listingId = fx.shelf.rows.values.single().id
          fx.sessions.unbindCharacter(redId, red)
          fx.buy(blue, listingId)
          fx.money(redId) shouldBe sellerAfterFee
          fx.shelf.rows.values.single().state shouldBe GTL_STATE_SOLD
          fx.sessions.bindCharacter(red, redId)
          fx.gtl.onOpenSession(PacketEvent(GtlOpenSessionPacket(0, 0), red))
          red.sent.filterIsInstance<CategoryFlagsPacket>().single().flagBits.size shouldBe 822
          val notice = red.results().last()
          notice.code shouldBe GtlResultPacket.CODE_SOLD_ONE
          notice.b shouldBe 5000
          fx.money(redId) shouldBe sellerAfterFee
          fx.claim(red, listingId)
          fx.money(redId) shouldBe sellerAfterFee + 5000
        }
      }

      test("the seller cannot buy its own listing") {
        runTest {
          val fx = GtlFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1, money = 8000)
          fx.store.addPokemon(redId, mon(redId, 4))
          val listed = fx.store.getCharacter(redId)!!.pokemon.first { it.dexId == 4 }
          fx.listMon(red, listed.id, 5000)
          val moneyAfterFee = fx.money(redId)
          val listingId = fx.shelf.rows.values.single().id
          fx.buy(red, listingId)
          fx.money(redId) shouldBe moneyAfterFee
          fx.shelf.rows.values.single().state shouldBe GTL_STATE_ACTIVE
          red.results().last().code shouldBe GtlResultPacket.CODE_GONE
        }
      }

      test("a take-back brings the monster home, but not the fee") {
        runTest {
          val fx = GtlFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1, money = 500)
          fx.store.addPokemon(redId, mon(redId, 4))
          val listed = fx.store.getCharacter(redId)!!.pokemon.first { it.dexId == 4 }
          fx.listMon(red, listed.id, 5000)
          val listingId = fx.shelf.rows.values.single().id
          fx.gtl.onListingCancel(PacketEvent(GtlListingCancelPacket(listingId.toString()), red))
          fx.partyDex(redId) shouldBe listOf(1, 4)
          fx.money(redId) shouldBe 500 - fee(5000)
          fx.shelf.rows.values.single().state shouldBe GTL_STATE_CLOSED
          red.results().last().code shouldBe GtlResultPacket.CODE_CANCELED
        }
      }

      test("a listing with unclaimed money refuses to cancel until it is claimed") {
        runTest {
          val fx = GtlFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1, money = 1000)
          val (blue, _) = fx.seated("Blue", 2, 25, money = 5000)
          fx.store.addItem(redId, 17, 5)
          fx.listItem(red, 17, 5, 900)
          val listingId = fx.shelf.rows.values.single().id
          fx.buy(blue, listingId, quantity = 2)
          fx.gtl.onListingCancel(PacketEvent(GtlListingCancelPacket(listingId.toString()), red))
          red.results().last().code shouldBe GtlResultPacket.CODE_UNSETTLED
          fx.shelf.rows.values.single().state shouldBe GTL_STATE_ACTIVE
          fx.claim(red, listingId)
          fx.gtl.onListingCancel(PacketEvent(GtlListingCancelPacket(listingId.toString()), red))
          red.results().last().code shouldBe GtlResultPacket.CODE_CANCELED
          fx.store.getCharacter(redId)!!.items[17] shouldBe 3
          fx.shelf.rows.values.single().state shouldBe GTL_STATE_CLOSED
        }
      }

      test("a lowered price sticks, a raise refuses, and the cooldown holds") {
        runTest {
          val fx = GtlFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1, money = 1000)
          fx.store.addPokemon(redId, mon(redId, 4))
          val listed = fx.store.getCharacter(redId)!!.pokemon.first { it.dexId == 4 }
          fx.listMon(red, listed.id, 5000)
          val listingId = fx.shelf.rows.values.single().id
          fx.gtl.onPriceChange(PacketEvent(GtlPriceChangePacket(listingId, 6000), red))
          red.results().last().code shouldBe GtlResultPacket.CODE_PRICE_FLOOR
          fx.gtl.onPriceChange(PacketEvent(GtlPriceChangePacket(listingId, 4000), red))
          red.results().last().code shouldBe GtlResultPacket.CODE_PRICE_CHANGED
          fx.shelf.rows.values.single().price shouldBe 4000
          fx.gtl.onPriceChange(PacketEvent(GtlPriceChangePacket(listingId, 3000), red))
          red.results().last().code shouldBe GtlResultPacket.CODE_PRICE_COOLDOWN
          fx.shelf.rows.values.single().price shouldBe 4000
        }
      }

      test("the listing cap refuses the seventeenth") {
        runTest {
          val fx = GtlFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1, money = 5000)
          fx.store.addItem(redId, 17, 100)
          repeat(16) { fx.listItem(red, 17, 1, 3) }
          fx.shelf.rows.size shouldBe 16
          fx.listItem(red, 17, 1, 3)
          fx.shelf.rows.size shouldBe 16
          red.results().last().code shouldBe GtlResultPacket.CODE_CAP
        }
      }

      test("an own-listings page carries state, remaining and unclaimed units") {
        runTest {
          val fx = GtlFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1, money = 1000)
          val (blue, _) = fx.seated("Blue", 2, 25, money = 5000)
          fx.store.addItem(redId, 17, 5)
          fx.listItem(red, 17, 5, 900)
          fx.buy(blue, fx.shelf.rows.values.single().id, quantity = 3)
          fx.gtl.onSearchPage(
              PacketEvent(
                  GtlSearchPageRequestPacket(
                      requestId = 5,
                      listKind = GtlListKind.OWN_LISTINGS,
                      categoryId = 0,
                      page = 0,
                      filters = emptyList(),
                  ),
                  red))
          val pageReply = red.sent.filterIsInstance<GtlSearchPagePacket>().single()
          val row = pageReply.listings.single() as GtlItemListing
          val own = row.own.shouldNotBeNull()
          own.state shouldBe GTL_STATE_ACTIVE.toByte()
          own.remaining shouldBe 2.toShort()
          own.unclaimedUnits shouldBe 3.toShort()
        }
      }

      test("the trade log answers both chairs of a fill") {
        runTest {
          val fx = GtlFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1, money = 1000)
          val (blue, _) = fx.seated("Blue", 2, 25, money = 5000)
          fx.store.addItem(redId, 17, 5)
          fx.listItem(red, 17, 5, 900)
          fx.buy(blue, fx.shelf.rows.values.single().id, quantity = 3)
          fx.gtl.onTradeLog(PacketEvent(GtlTradeLogRequestPacket(), red))
          fx.gtl.onTradeLog(PacketEvent(GtlTradeLogRequestPacket(), blue))
          val sellerLog = red.sent.filterIsInstance<SceneObjectStatesPacket>().single()
          val soldRow = sellerLog.objects.single()
          soldRow.type shouldBe 1.toByte()
          soldRow.frames[0].valueA shouldBe 17.toShort()
          soldRow.frames[0].valueB shouldBe 3.toShort()
          soldRow.frames[0].valueC shouldBe 2700
          soldRow.frames[0].flag shouldBe false
          soldRow.frames[1].valueC shouldBeGreaterThan 0
          val buyerLog = blue.sent.filterIsInstance<SceneObjectStatesPacket>().single()
          buyerLog.objects.single().type shouldBe 3.toByte()
          buyerLog.objects.single().frames[0].flag shouldBe true
        }
      }

      test("a failed shelf write puts the monster and the fee back") {
        runTest {
          val fx = GtlFixture(backgroundScope)
          val (red, redId) = fx.seated("Red", 1, 1, money = 500)
          fx.store.addPokemon(redId, mon(redId, 4))
          val listed = fx.store.getCharacter(redId)!!.pokemon.first { it.dexId == 4 }
          fx.shelf.failNextInsert = true
          fx.listMon(red, listed.id, 5000)
          fx.partyDex(redId) shouldBe listOf(1, 4)
          fx.money(redId) shouldBe 500
          fx.shelf.rows.values.shouldBeEmpty()
          red.replies().last() shouldContain "could not be written"
        }
      }
    })
