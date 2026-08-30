package de.fiereu.openmmo.server.game.services

import de.fiereu.network.PacketEvent
import de.fiereu.network.SessionContext
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.net.game.packets.CategoryFlagsPacket
import de.fiereu.openmmo.net.game.packets.CreateMarketListingPacket
import de.fiereu.openmmo.net.game.packets.LocalCharacterDeltaPacket
import de.fiereu.openmmo.net.game.packets.MarketBoardPagePacket
import de.fiereu.openmmo.net.game.packets.MarketListingsRequestPacket
import de.fiereu.openmmo.net.game.packets.MarketSearchFilterPacket
import de.fiereu.openmmo.net.game.packets.PokemonContainerPacket
import de.fiereu.openmmo.net.game.packets.SceneObjectFrame
import de.fiereu.openmmo.net.game.packets.SceneObjectState
import de.fiereu.openmmo.net.game.packets.SceneObjectStatesPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlClaimPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlConfirmPurchasePacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlCreateListingPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlFilterKind
import de.fiereu.openmmo.net.game.packets.gtl.GtlItemListing
import de.fiereu.openmmo.net.game.packets.gtl.GtlItemMarketPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlItemQuote
import de.fiereu.openmmo.net.game.packets.gtl.GtlListKind
import de.fiereu.openmmo.net.game.packets.gtl.GtlListingActionPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlListingCancelPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlListingSearchPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlListingsPagePacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlListingsPageRequestPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlMarketEntry
import de.fiereu.openmmo.net.game.packets.gtl.GtlMarketListingsPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlMarketListingsRequestPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlOpenSessionPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlOwnInfo
import de.fiereu.openmmo.net.game.packets.gtl.GtlPokemonListing
import de.fiereu.openmmo.net.game.packets.gtl.GtlPriceChangePacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlPriceHistoryPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlPriceHistoryPoint
import de.fiereu.openmmo.net.game.packets.gtl.GtlPurchaseListingPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlPurchasePacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlRawFilter
import de.fiereu.openmmo.net.game.packets.gtl.GtlResultPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlSearchFilter
import de.fiereu.openmmo.net.game.packets.gtl.GtlSearchFilterPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlSearchListing
import de.fiereu.openmmo.net.game.packets.gtl.GtlSearchPagePacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlSearchPageRequestPacket
import de.fiereu.openmmo.net.game.packets.gtl.GtlSpeciesFilter
import de.fiereu.openmmo.net.game.packets.gtl.GtlTradeLogRequestPacket
import de.fiereu.openmmo.net.game.packets.gtl.TradeListing
import de.fiereu.openmmo.net.game.packets.gtl.TradeListingEntry
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.battle.BattleRegistry
import de.fiereu.openmmo.server.game.battle.StatCalculator
import de.fiereu.openmmo.server.game.session.PLAYER_STATE
import de.fiereu.openmmo.server.game.session.SessionRegistry
import de.fiereu.openmmo.server.game.storage.CharacterStore
import de.fiereu.openmmo.server.game.storage.GTL_KIND_ITEM
import de.fiereu.openmmo.server.game.storage.GTL_KIND_POKEMON
import de.fiereu.openmmo.server.game.storage.GTL_STATE_ACTIVE
import de.fiereu.openmmo.server.game.storage.GtlListing
import de.fiereu.openmmo.server.game.storage.GtlMonsterQuery
import de.fiereu.openmmo.server.game.storage.GtlRepository
import de.fiereu.openmmo.server.game.storage.GtlSaleRow
import de.fiereu.openmmo.server.game.storage.MONEY_MAX
import io.github.oshai.kotlinlogging.KotlinLogging
import java.time.LocalDateTime
import java.time.ZoneOffset
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

/** The official client pages ten rows at a time; the captures never carry more. */
private const val PAGE_SIZE = 10

/** How many listings one seller may have standing at once. */
private const val LISTING_CAP = 16

/** How long a listing stands before it can no longer be bought. */
private const val LISTING_DAYS = 7L

/** The official 0xDC flag payload is 822 bytes; ours carries no categories to switch off. */
private const val CATEGORY_FLAG_BYTES = 822

/* The economics, mirrored by the window's own display: a 2.5%
 * listing fee with a floor and a ceiling, price floors per kind, and a
 * cooldown on lowering a price. Fees are not refunded. */
private const val FEE_DIVISOR = 40
private const val FEE_FLOOR = 100
private const val FEE_CAP = 50_000
private const val MIN_PRICE_ITEM = 3
private const val MIN_PRICE_MON = 100
private const val PRICE_CHANGE_COOLDOWN_MIN = 10L

/* The shelf refuses key items: the official client's GTS never sees one because the bag
 * never offers them, and this shelf's window is our own, so the bag is not the
 * wall here and the shelf has to be. Which ids they are is [KEY_ITEM_IDS],
 * shared with the registered-item slot (Items.kt carries the receipt). */

/** The trade-log page the 0x5E answer carries. */
private const val LOG_ROWS = 30

/**
 * The global trade link, official-shaped: an anonymous shelf of monsters and item stacks anyone can
 * search; item listings sell by the unit, proceeds park on the row until the seller presses Claim,
 * listing costs a fee that is not refunded, and every verb is answered with a result code the
 * window maps to the official client's own toasts ([GtlResultPacket]).
 */
@Singleton
class GtlService
@Inject
constructor(
    private val sessions: SessionRegistry,
    private val store: CharacterStore,
    private val shelf: GtlRepository,
    private val species: SpeciesRegistry,
    private val battles: BattleRegistry,
) {

  /**
   * c2s 0xA5: the session open. The seller is told what sold while they were away, told, not paid:
   * the money waits for Claim, as the official client's does.
   */
  suspend fun onOpenSession(event: PacketEvent<GtlOpenSessionPacket>) {
    val session = event.session
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    session.send(
        CategoryFlagsPacket(
            entryKind = 1,
            timestampMillis = System.currentTimeMillis(),
            flagBits = ByteArray(CATEGORY_FLAG_BYTES),
        ))
    val standing = shelf.ownPage(charId, 0, LISTING_CAP * 2).listings
    val sold = standing.filter { it.unclaimedUnits > 0 }
    if (sold.size == 1) {
      val row = sold.single()
      session.send(
          GtlResultPacket(
              GtlResultPacket.CODE_SOLD_ONE,
              (row.pokemon?.dexId ?: row.itemId ?: 0).toLong(),
              row.unclaimedUnits * row.price))
    } else if (sold.size > 1) {
      session.send(
          GtlResultPacket(
              GtlResultPacket.CODE_SOLD_MANY,
              sold.size.toLong(),
              sold.sumOf { it.unclaimedUnits * it.price }))
    }
  }

  /** c2s 0xE4 -> s2c 0xFE: the price board, one row per listing, entry `c` the listing id. */
  suspend fun onListingsPage(event: PacketEvent<GtlListingsPageRequestPacket>) {
    val session = event.session
    if (session.attributes[PLAYER_STATE]?.characterId == null) return
    val request = event.packet
    val page = request.page.toInt().coerceAtLeast(0)
    val result =
        when (request.listKind) {
          GtlListKind.ITEM ->
              shelf.itemPage(null, null, request.categoryId.toInt(), page * PAGE_SIZE, PAGE_SIZE)
          else ->
              shelf.monsterPage(
                  GtlMonsterQuery(), request.categoryId.toInt(), page * PAGE_SIZE, PAGE_SIZE)
        }
    session.send(
        GtlListingsPagePacket(
            requestId = request.requestId,
            listKind = request.listKind,
            page = request.page,
            totalCount = result.total,
            listings = result.listings.map { it.toBoardRow() },
        ))
  }

  /** c2s 0x9B -> s2c 0x9B: the search pages. The request's third byte is the sort order. */
  suspend fun onSearchPage(event: PacketEvent<GtlSearchPageRequestPacket>) {
    val session = event.session
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    val request = event.packet
    val page = request.page.toInt().coerceAtLeast(0)
    val offset = page * PAGE_SIZE
    val sort = request.categoryId.toInt()
    val (result, quotes) =
        when (request.listKind) {
          GtlListKind.POKEMON ->
              shelf.monsterPage(request.filters.toQuery(), sort, offset, PAGE_SIZE) to null
          GtlListKind.ITEM -> {
            val query = request.filters.toQuery()
            shelf.itemPage(query.minPrice, query.maxPrice, sort, offset, PAGE_SIZE) to
                shelf.itemQuotes(PAGE_SIZE).map { (item, price) ->
                  GtlItemQuote(item.toShort(), price)
                }
          }
          GtlListKind.OWN_LISTINGS -> shelf.ownPage(charId, offset, PAGE_SIZE) to null
        }
    session.send(
        GtlSearchPagePacket(
            requestId = request.requestId,
            listKind = request.listKind,
            page = request.page,
            totalMatches = result.total,
            listings = result.listings.map { it.toSearchRow() },
            quotes = quotes,
        ))
  }

  /**
   * c2s 0x9A: put something up, the official client's own create packet: kind, the monster's id or
   * the item's, the unit price, the quantity. The goods leave first, the fee is paid second, the
   * row is written third, and every later failure walks the earlier steps back.
   */
  suspend fun onCreateListing(event: PacketEvent<CreateMarketListingPacket>) {
    val session = event.session
    val state = session.attributes[PLAYER_STATE] ?: return
    val charId = state.characterId ?: return
    val self = store.getCharacter(charId) ?: return
    // Listing takes a monster out of the party, the same gesture a box move is, and is refused for
    // the same reason: it lands between the two writes of a settlement.
    if (state.atTradeTable) {
      session.send(notice("You cannot list a monster while trading."))
      return
    }
    // And for the same reason again, a fight. A battle holds its own copy of the party and
    // writes it back when it ends, so a monster listed out from under one is given away twice or
    // taken back by the writeback.
    if (battles.byChar(charId) != null) {
      session.send(notice("You cannot list a monster while battling."))
      return
    }
    val kind = event.packet.entryKind.toInt()
    val price = event.packet.price
    val quantity = if (kind == GTL_KIND_POKEMON) 1 else event.packet.quantity.toInt()
    val floor = if (kind == GTL_KIND_POKEMON) MIN_PRICE_MON else MIN_PRICE_ITEM

    if (quantity < 1 || price > MONEY_MAX) return
    if (price < floor) {
      session.send(GtlResultPacket(GtlResultPacket.CODE_MIN_PRICE, 0, floor))
      return
    }
    if (shelf.standingCount(charId) >= LISTING_CAP) {
      session.send(GtlResultPacket(GtlResultPacket.CODE_CAP, LISTING_CAP.toLong(), 0))
      return
    }
    val fee = feeFor(price) * quantity

    if (kind == GTL_KIND_POKEMON) {
      val monId = event.packet.entityId
      val mon =
          (self.pokemon + self.pcStorage).firstOrNull { it.id == monId }
              ?: run {
                session.send(notice("That monster is not yours to list."))
                return
              }
      if (!store.releasePokemon(charId, monId)) {
        session.send(notice("Your last party monster cannot be listed."))
        return
      }
      if (!store.addMoney(charId, -fee)) {
        if (store.addPokemon(charId, mon, mon.containerSlot.toInt()) == null) {
          log.error { "gtl char=$charId monster=$monId: unaffordable fee lost the return seat" }
        }
        session.send(GtlResultPacket(GtlResultPacket.CODE_NO_FUNDS, 0, fee))
        return
      }
      val inserted =
          runCatching { shelf.insert(newListing(charId, self.info.name, price, fee, mon = mon)) }
              .getOrNull()
      if (inserted == null) {
        store.addMoney(charId, fee)
        if (store.addPokemon(charId, mon, mon.containerSlot.toInt()) == null) {
          log.error { "gtl char=$charId monster=$monId: failed listing lost its return seat" }
        }
        session.send(notice("The listing could not be written."))
        return
      }
      resendContainers(session, charId)
      sendMoney(session, charId)
      session.send(GtlResultPacket(GtlResultPacket.CODE_LISTED, inserted.id, fee))
      log.info {
        "gtl list monster=$monId char=$charId price=$price fee=$fee listing=${inserted.id}"
      }
      return
    }

    val itemId = event.packet.entityId.toInt()
    if (itemId in KEY_ITEM_IDS) {
      session.send(notice("Key items cannot be listed."))
      return
    }
    if (!store.addItem(charId, itemId, -quantity)) {
      session.send(notice("You do not have that many."))
      return
    }
    if (!store.addMoney(charId, -fee)) {
      store.addItem(charId, itemId, quantity)
      session.send(GtlResultPacket(GtlResultPacket.CODE_NO_FUNDS, 0, fee))
      return
    }
    val inserted =
        runCatching {
              shelf.insert(
                  newListing(
                      charId, self.info.name, price, fee, itemId = itemId, quantity = quantity))
            }
            .getOrNull()
    if (inserted == null) {
      store.addMoney(charId, fee)
      if (!store.addItem(charId, itemId, quantity)) {
        log.error { "gtl char=$charId item=$itemId x$quantity: failed listing lost the stack" }
      }
      session.send(notice("The listing could not be written."))
      return
    }
    session.send(itemStackUpdatePacket(itemId, store.getCharacter(charId)?.items?.get(itemId) ?: 0))
    sendMoney(session, charId)
    session.send(GtlResultPacket(GtlResultPacket.CODE_LISTED, inserted.id, fee))
    log.info {
      "gtl list item=$itemId x$quantity char=$charId price=$price fee=$fee listing=${inserted.id}"
    }
  }

  /** c2s 0x9C: buy [GtlConfirmPurchasePacket.quantity] units (a monster is one unit). */
  suspend fun onConfirmPurchase(event: PacketEvent<GtlConfirmPurchasePacket>) {
    buy(event.session, event.packet.listingEntityId, event.packet.quantity.toInt(), null)
  }

  /** c2s 0x71: the older single-packet purchase; its price is a stale-shelf check. */
  suspend fun onPurchaseListing(event: PacketEvent<GtlPurchaseListingPacket>) {
    buy(event.session, event.packet.listingId.toLong(), 1, event.packet.price)
  }

  /** c2s 0xA3: a market buy, fill [GtlPurchasePacket.quantity] of an item, cheapest first. */
  suspend fun onPurchase(event: PacketEvent<GtlPurchasePacket>) {
    val session = event.session
    if (session.attributes[PLAYER_STATE]?.characterId == null) return
    val itemId = event.packet.itemTypeId.toInt()
    var remaining = event.packet.quantity.toInt()
    var budget = event.packet.totalPrice
    if (remaining <= 0 || budget <= 0) return
    var bought = 0
    for (listing in shelf.cheapestItemListings(itemId, PAGE_SIZE)) {
      if (listing.price > budget) break
      val take = minOf(remaining, listing.remaining, budget / listing.price)
      if (take < 1) continue
      if (!buy(session, listing.id, take, null, quiet = true)) continue
      remaining -= take
      budget -= take * listing.price
      bought += take
      if (remaining <= 0) break
    }
    session.send(
        if (bought > 0) GtlResultPacket(GtlResultPacket.CODE_BOUGHT, itemId.toLong(), bought)
        else GtlResultPacket(GtlResultPacket.CODE_GONE, 0, 0))
  }

  /**
   * c2s 0xA1: cancel an active listing, by its id in decimal. Fees are not refunded, and a listing
   * with unclaimed money on it refuses, claim first, as the official client does.
   */
  suspend fun onListingCancel(event: PacketEvent<GtlListingCancelPacket>) {
    val session = event.session
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    val id = event.packet.listingId.toLongOrNull() ?: return
    val standing = ownRow(charId, id)
    if (standing != null && standing.unclaimedUnits > 0) {
      session.send(GtlResultPacket(GtlResultPacket.CODE_UNSETTLED, id, 0))
      return
    }
    val listing = shelf.close(id, charId, GTL_STATE_ACTIVE)
    if (listing == null) {
      session.send(GtlResultPacket(GtlResultPacket.CODE_GONE, id, 0))
      return
    }
    if (!deliverUnits(session, charId, listing, listing.remaining)) {
      shelf.reopen(id, charId)
      session.send(GtlResultPacket(GtlResultPacket.CODE_NO_ROOM, id, 0))
      return
    }
    session.send(GtlResultPacket(GtlResultPacket.CODE_CANCELED, id, 0))
    log.info { "gtl cancel listing=$id char=$charId" }
  }

  /** c2s 0x54: claim the money the named listings have settled. */
  suspend fun onClaim(event: PacketEvent<GtlClaimPacket>) {
    val session = event.session
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    var total = 0L
    for (id in event.packet.listingIds.distinct()) {
      val row = ownRow(charId, id) ?: continue
      val units = row.unclaimedUnits
      if (units < 1) continue
      if (!shelf.claimUnits(id, charId, units)) continue
      if (!store.addMoney(charId, units * row.price)) {
        log.error { "gtl listing=$id char=$charId: claim of ${units * row.price} did not persist" }
        continue
      }
      total += units.toLong() * row.price
    }
    if (total > 0) {
      sendMoney(session, charId)
      session.send(GtlResultPacket(GtlResultPacket.CODE_CLAIMED, total, 0))
    } else {
      session.send(GtlResultPacket(GtlResultPacket.CODE_GONE, 0, 0))
    }
  }

  /** c2s 0x55: lower a standing listing's price, on a cooldown, never below the floor. */
  suspend fun onPriceChange(event: PacketEvent<GtlPriceChangePacket>) {
    val session = event.session
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    val id = event.packet.listingId
    val row = ownRow(charId, id)
    if (row == null || row.state != GTL_STATE_ACTIVE) {
      session.send(GtlResultPacket(GtlResultPacket.CODE_GONE, id, 0))
      return
    }
    if (row.unclaimedUnits > 0) {
      session.send(GtlResultPacket(GtlResultPacket.CODE_UNSETTLED, id, 0))
      return
    }
    val floor = if (row.kind == GTL_KIND_POKEMON) MIN_PRICE_MON else MIN_PRICE_ITEM
    if (event.packet.newPrice < floor || event.packet.newPrice >= row.price) {
      session.send(GtlResultPacket(GtlResultPacket.CODE_PRICE_FLOOR, id, floor))
      return
    }
    val now = LocalDateTime.now()
    val cutoff = now.minusMinutes(PRICE_CHANGE_COOLDOWN_MIN)
    if (!shelf.changePrice(id, charId, event.packet.newPrice, now, cutoff)) {
      session.send(
          GtlResultPacket(
              GtlResultPacket.CODE_PRICE_COOLDOWN, id, PRICE_CHANGE_COOLDOWN_MIN.toInt()))
      return
    }
    session.send(GtlResultPacket(GtlResultPacket.CODE_PRICE_CHANGED, id, event.packet.newPrice))
  }

  /** c2s 0x9F -> s2c 0x5E: the trade log. */
  suspend fun onTradeLog(event: PacketEvent<GtlTradeLogRequestPacket>) {
    val session = event.session
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return
    val rows =
        shelf.sales(charId, LOG_ROWS).map { sale ->
          val bought = sale.buyerId == charId
          val type = (if (bought) 2 else 0) + (if (sale.kind == GTL_KIND_ITEM) 1 else 0)
          SceneObjectState(
              objectId = sale.id,
              type = type.toByte(),
              frames =
                  listOf(
                      SceneObjectFrame(
                          valueA = (sale.itemId ?: sale.monDexId ?: 0).toShort(),
                          valueB =
                              (if (sale.kind == GTL_KIND_ITEM) sale.quantity
                                  else (sale.monLevel ?: 0))
                                  .toShort(),
                          valueC = sale.unitPrice * sale.quantity,
                          flag = bought,
                      ),
                      SceneObjectFrame(
                          valueA = 0,
                          valueB = 0,
                          valueC = sale.soldAt.toEpochSecond(ZoneOffset.UTC).toInt(),
                          flag = false,
                      ),
                  ))
        }
    session.send(SceneObjectStatesPacket(rows))
  }

  /** c2s 0x70: the featured board. Answered empty; the search pages are the shelf's face. */
  fun onMarketListings(event: PacketEvent<GtlMarketListingsRequestPacket>) {
    event.session.send(
        GtlMarketListingsPacket(
            fullReset = true,
            applyToBoards = true,
            referenceData = ByteArray(0),
            listings = emptyList(),
        ))
  }

  /** c2s 0x7C: one item's market depth, plus its sale history right behind it. */
  suspend fun onListingSearch(event: PacketEvent<GtlListingSearchPacket>) {
    val session = event.session
    if (session.attributes[PLAYER_STATE]?.characterId == null) return
    val itemId = event.packet.searchId
    val levels = shelf.priceLevels(itemId, PAGE_SIZE)
    session.send(
        GtlItemMarketPacket(
            itemType = 1,
            variant = itemId.toShort(),
            count = levels.sumOf { it.quantity }.toInt(),
            quantities = listOf(levels.sumOf { it.quantity }.toInt(), levels.size, 0),
            entries = levels.map { GtlMarketEntry(it.price, it.quantity) },
        ))
    session.send(
        GtlPriceHistoryPacket(
            itemType = 1,
            variant = itemId.toShort(),
            points =
                shelf.priceHistory(itemId, 30).map {
                  GtlPriceHistoryPoint(it.soldAt.toEpochSecond(ZoneOffset.UTC).toInt(), it.price)
                },
        ))
  }

  /** The rest of the group: accepted so the client is never left waiting, acted on never. */
  fun onSearchFilter(event: PacketEvent<GtlSearchFilterPacket>) {
    log.debug { "gtl filter state ${event.packet.filterState}" }
  }

  fun onListingAction(event: PacketEvent<GtlListingActionPacket>) {
    log.debug { "gtl listing action ${event.packet.entryKindId}/${event.packet.itemId}" }
  }

  fun onMarketBoard(event: PacketEvent<MarketListingsRequestPacket>) {
    event.session.send(emptyMarketBoard())
  }

  fun onMarketSearch(event: PacketEvent<MarketSearchFilterPacket>) {
    event.session.send(emptyMarketBoard())
  }

  /**
   * The retired 0xE1 create tail. The official client's 0xE1 is a template search; not this shelf's
   * verb.
   */
  fun onLegacyCreate(event: PacketEvent<GtlCreateListingPacket>) {
    log.debug { "gtl legacy 0xE1 create ignored (${event.packet.categoryIndex})" }
  }

  private suspend fun ownRow(charId: Long, id: Long): GtlListing? =
      shelf.ownPage(charId, 0, LISTING_CAP * 2).listings.firstOrNull { it.id == id }

  private suspend fun buy(
      session: SessionContext,
      listingId: Long,
      quantity: Int,
      expectedPrice: Int?,
      quiet: Boolean = false,
  ): Boolean {
    val charId = session.attributes[PLAYER_STATE]?.characterId ?: return false
    val self = store.getCharacter(charId) ?: return false
    val units = quantity.coerceAtLeast(1)
    val listing = shelf.purchaseUnits(listingId, units, charId, LocalDateTime.now())
    if (listing == null) {
      if (!quiet) session.send(GtlResultPacket(GtlResultPacket.CODE_GONE, listingId, 0))
      return false
    }
    // A key item listed before the shelf refused them. Nobody may buy it; the
    // seller can still cancel and take it back, which is the way off the shelf.
    if (listing.kind == GTL_KIND_ITEM && listing.itemId in KEY_ITEM_IDS) {
      shelf.revertUnits(listing.id, units)
      if (!quiet) session.send(notice("That is a key item; it cannot be bought."))
      return false
    }
    if (expectedPrice != null && expectedPrice != listing.price) {
      shelf.revertUnits(listing.id, units)
      if (!quiet) session.send(GtlResultPacket(GtlResultPacket.CODE_GONE, listingId, 0))
      return false
    }
    // The money moves before the goods do, because the undo in the other order cannot be relied
    // on: a monster goes back through a release that refuses to empty a party, so a buyer with an
    // empty party and too little money kept it for nothing and the listing went back on the shelf.
    val cost = listing.price * units
    if (!store.addMoney(charId, -cost)) {
      shelf.revertUnits(listing.id, units)
      if (!quiet) session.send(GtlResultPacket(GtlResultPacket.CODE_NO_FUNDS, listingId, 0))
      return false
    }
    if (!deliverUnits(session, charId, listing, units)) {
      if (!store.addMoney(charId, cost)) {
        log.error { "gtl listing=${listing.id} char=$charId: undelivered $cost was not refunded" }
      }
      shelf.revertUnits(listing.id, units)
      if (!quiet) session.send(GtlResultPacket(GtlResultPacket.CODE_NO_ROOM, listingId, 0))
      return false
    }
    shelf.recordSale(
        GtlSaleRow(
            id = 0,
            listingId = listing.id,
            sellerId = listing.sellerId,
            sellerName = listing.sellerName,
            buyerId = charId,
            buyerName = self.info.name,
            kind = listing.kind,
            itemId = listing.itemId,
            monDexId = listing.pokemon?.dexId,
            monLevel = listing.pokemon?.level?.toInt(),
            quantity = units,
            unitPrice = listing.price,
            soldAt = LocalDateTime.now(),
        ))
    sendMoney(session, charId)
    if (!quiet)
        session.send(GtlResultPacket(GtlResultPacket.CODE_BOUGHT, listingId, listing.price * units))
    log.info { "gtl buy listing=${listing.id} char=$charId units=$units price=${listing.price}" }
    // A seller at the keyboard hears the sale live; the money still waits for Claim.
    sessions
        .getByCharacterId(listing.sellerId)
        ?.send(
            GtlResultPacket(
                GtlResultPacket.CODE_SOLD_ONE,
                (listing.pokemon?.dexId ?: listing.itemId ?: 0).toLong(),
                listing.price * units))
    return true
  }

  /** Hand [units] of a listing's goods to a character. False when there is no room. */
  private suspend fun deliverUnits(
      session: SessionContext,
      charId: Long,
      listing: GtlListing,
      units: Int,
  ): Boolean {
    val mon = listing.pokemon
    if (mon != null) {
      if (units < 1) return true
      val seated =
          store.addPokemon(charId, mon.copy(container = PokemonContainer.PARTY))
              ?: store.addPokemon(charId, mon.copy(container = PokemonContainer.PC))
      if (seated == null) return false
      resendContainers(session, charId)
      return true
    }
    val itemId = listing.itemId ?: return false
    if (units < 1) return true
    if (!store.addItem(charId, itemId, units)) return false
    session.send(itemStackUpdatePacket(itemId, store.getCharacter(charId)?.items?.get(itemId) ?: 0))
    return true
  }

  private fun feeFor(price: Int): Int =
      (price / FEE_DIVISOR).coerceAtLeast(FEE_FLOOR).coerceAtMost(FEE_CAP)

  private fun sendMoney(session: SessionContext, charId: Long) {
    val money = store.getCharacter(charId)?.info?.money ?: return
    session.send(LocalCharacterDeltaPacket(money = money))
  }

  private fun resendContainers(session: SessionContext, charId: Long) {
    val stored = store.getCharacter(charId) ?: return
    session.send(
        PokemonContainerPacket(
            container = PokemonContainer.PARTY,
            hasChange = true,
            delete = false,
            pokemon = stored.pokemon,
        ))
    val pc = stored.pcStorage.chunked(255).ifEmpty { listOf(emptyList()) }
    pc.forEachIndexed { i, chunk ->
      session.send(
          PokemonContainerPacket(
              container = PokemonContainer.PC,
              hasChange = i == 0,
              delete = false,
              pokemon = chunk,
          ))
    }
  }

  private fun newListing(
      sellerId: Long,
      sellerName: String,
      price: Int,
      fee: Int,
      mon: Pokemon? = null,
      itemId: Int? = null,
      quantity: Int = 1,
  ): GtlListing {
    val now = LocalDateTime.now()
    return GtlListing(
        id = 0,
        sellerId = sellerId,
        sellerName = sellerName,
        kind = if (mon != null) GTL_KIND_POKEMON else GTL_KIND_ITEM,
        itemId = itemId,
        quantity = quantity,
        price = price,
        state = GTL_STATE_ACTIVE,
        buyerId = null,
        buyerName = null,
        listedAt = now,
        expiresAt = now.plusDays(LISTING_DAYS),
        soldAt = null,
        pokemon = mon?.copy(container = PokemonContainer.GTS, containerSlot = 0),
        remaining = quantity,
        unclaimedUnits = 0,
        fee = fee,
    )
  }

  private fun GtlListing.toSearchRow(): GtlSearchListing {
    val listed = listedAt.toEpochSecond(ZoneOffset.UTC).toInt()
    val expires = expiresAt.toEpochSecond(ZoneOffset.UTC).toInt()
    // Every row carries its own-page tail; the codec only writes it on OWN_LISTINGS pages.
    val ownInfo = GtlOwnInfo(state.toByte(), remaining.toShort(), unclaimedUnits.toShort())
    val mon = pokemon
    if (mon != null) {
      return GtlPokemonListing(
          listingId = id,
          price = price,
          listedAt = listed,
          expiresAt = expires,
          quantity = 1.toShort(),
          pokemon = mon,
          stats = statsOf(mon),
          own = ownInfo,
      )
    }
    return GtlItemListing(
        listingId = id,
        price = price,
        listedAt = listed,
        expiresAt = expires,
        quantity = remaining.toShort(),
        itemId = (itemId ?: 0).toShort(),
        itemState = 0,
        own = ownInfo,
    )
  }

  private fun GtlListing.toBoardRow(): TradeListing =
      TradeListing(
          typeId = (pokemon?.dexId ?: itemId ?: 0).toShort(),
          price = price,
          fieldB = remaining,
          fieldC = pokemon?.level?.toInt() ?: 0,
          fieldD = if (pokemon?.isShiny == true) 1 else 0,
          entries = listOf(TradeListingEntry(price, remaining, id)),
      )

  /** The board's stat strip: the record's own hp, then computed battle stats, in EV wire order. */
  private fun statsOf(mon: Pokemon): List<Short> {
    val def = species.get(mon.dexId) ?: return List(6) { 0 }
    val stats = StatCalculator.computeAll(def, mon)
    return listOf(
        mon.hp,
        stats.atk.toShort(),
        stats.def.toShort(),
        stats.spd.toShort(),
        stats.spAtk.toShort(),
        stats.spDef.toShort(),
    )
  }

  private fun List<GtlSearchFilter>.toQuery(): GtlMonsterQuery {
    var query = GtlMonsterQuery()
    for (filter in this) {
      when (filter) {
        is GtlSpeciesFilter -> query = query.copy(speciesIds = filter.speciesIds.map { it.toInt() })
        is GtlRawFilter ->
            query =
                when (filter.kind) {
                  GtlFilterKind.MIN_LEVEL -> query.copy(minLevel = filter.payload.u8(0))
                  GtlFilterKind.MAX_LEVEL -> query.copy(maxLevel = filter.payload.u8(0))
                  GtlFilterKind.SHINY -> query.copy(shiny = filter.payload.u8(0) != 0)
                  GtlFilterKind.NATURE -> query.copy(nature = filter.payload.u8(0))
                  GtlFilterKind.MIN_PRICE -> query.copy(minPrice = filter.payload.le32(0))
                  GtlFilterKind.MAX_PRICE -> query.copy(maxPrice = filter.payload.le32(0))
                  else -> {
                    log.debug { "gtl filter ${filter.kind} ignored" }
                    query
                  }
                }
        else -> {}
      }
    }
    return query
  }

  private fun emptyMarketBoard(): MarketBoardPagePacket =
      MarketBoardPagePacket(
          hasData = 0,
          page = null,
          category = null,
          total = null,
          expiryOffsetMillis = null,
          count = null,
          categoryRanked = false,
          entries = emptyList(),
      )

  private fun ByteArray.u8(at: Int): Int = this[at].toInt() and 0xFF

  private fun ByteArray.le32(at: Int): Int =
      (this[at].toInt() and 0xFF) or
          ((this[at + 1].toInt() and 0xFF) shl 8) or
          ((this[at + 2].toInt() and 0xFF) shl 16) or
          ((this[at + 3].toInt() and 0xFF) shl 24)
}
