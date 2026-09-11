package de.fiereu.openmmo.server.game.testsupport

import de.fiereu.openmmo.server.game.storage.GTL_KIND_ITEM
import de.fiereu.openmmo.server.game.storage.GTL_KIND_POKEMON
import de.fiereu.openmmo.server.game.storage.GTL_SORT_OLDEST
import de.fiereu.openmmo.server.game.storage.GTL_SORT_PRICE_ASC
import de.fiereu.openmmo.server.game.storage.GTL_SORT_PRICE_DESC
import de.fiereu.openmmo.server.game.storage.GTL_STATE_ACTIVE
import de.fiereu.openmmo.server.game.storage.GTL_STATE_CLOSED
import de.fiereu.openmmo.server.game.storage.GTL_STATE_SOLD
import de.fiereu.openmmo.server.game.storage.GtlListing
import de.fiereu.openmmo.server.game.storage.GtlMonsterQuery
import de.fiereu.openmmo.server.game.storage.GtlPage
import de.fiereu.openmmo.server.game.storage.GtlPriceLevel
import de.fiereu.openmmo.server.game.storage.GtlRepository
import de.fiereu.openmmo.server.game.storage.GtlSale
import de.fiereu.openmmo.server.game.storage.GtlSaleRow
import java.time.LocalDateTime
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicLong

/** The shelf as a map, with the same refusals the SQL one makes. */
class FakeGtlRepository : GtlRepository {
  val rows = ConcurrentHashMap<Long, GtlListing>()
  val saleRows = ConcurrentHashMap<Long, GtlSaleRow>()
  private val ids = AtomicLong(1)
  private val saleIds = AtomicLong(1)
  var failNextInsert = false

  override suspend fun insert(listing: GtlListing): GtlListing {
    if (failNextInsert) {
      failNextInsert = false
      throw IllegalStateException("simulated insert failure")
    }
    val id = ids.getAndIncrement()
    val row = listing.copy(id = id)
    rows[id] = row
    return row
  }

  override suspend fun standingCount(sellerId: Long): Int =
      rows.values.count { it.sellerId == sellerId && it.state == GTL_STATE_ACTIVE }

  private fun sorted(all: List<GtlListing>, sort: Int): List<GtlListing> =
      when (sort) {
        GTL_SORT_OLDEST -> all.sortedWith(compareBy({ it.listedAt }, { it.id }))
        GTL_SORT_PRICE_ASC -> all.sortedWith(compareBy({ it.price }, { it.id }))
        GTL_SORT_PRICE_DESC ->
            all.sortedWith(compareByDescending<GtlListing> { it.price }.thenBy { it.id })
        else ->
            all.sortedWith(
                compareByDescending<GtlListing> { it.listedAt }.thenByDescending { it.id })
      }

  override suspend fun monsterPage(
      query: GtlMonsterQuery,
      sort: Int,
      offset: Int,
      limit: Int,
  ): GtlPage {
    val all =
        rows.values
            .filter {
              it.state == GTL_STATE_ACTIVE && it.remaining > 0 && it.kind == GTL_KIND_POKEMON
            }
            .filter { query.speciesIds?.contains(it.pokemon?.dexId) ?: true }
            .filter { query.minLevel?.let { min -> (it.pokemon?.level ?: 0) >= min } ?: true }
            .filter { query.maxLevel?.let { max -> (it.pokemon?.level ?: 0) <= max } ?: true }
            .filter { query.shiny?.let { shiny -> it.pokemon?.isShiny == shiny } ?: true }
            .filter { query.nature?.let { n -> it.pokemon?.nature?.ordinal == n } ?: true }
            .filter { query.minPrice?.let { min -> it.price >= min } ?: true }
            .filter { query.maxPrice?.let { max -> it.price <= max } ?: true }
    val sorted = sorted(all, sort)
    return GtlPage(sorted.size, sorted.drop(offset).take(limit))
  }

  override suspend fun itemPage(
      minPrice: Int?,
      maxPrice: Int?,
      sort: Int,
      offset: Int,
      limit: Int,
  ): GtlPage {
    val all =
        rows.values
            .filter { it.state == GTL_STATE_ACTIVE && it.remaining > 0 && it.kind == GTL_KIND_ITEM }
            .filter { minPrice?.let { min -> it.price >= min } ?: true }
            .filter { maxPrice?.let { max -> it.price <= max } ?: true }
    val sorted = sorted(all, sort)
    return GtlPage(sorted.size, sorted.drop(offset).take(limit))
  }

  override suspend fun ownPage(sellerId: Long, offset: Int, limit: Int): GtlPage {
    val all =
        rows.values
            .filter {
              it.sellerId == sellerId && (it.state != GTL_STATE_CLOSED || it.unclaimedUnits > 0)
            }
            .sortedWith(compareByDescending<GtlListing> { it.listedAt }.thenByDescending { it.id })
    return GtlPage(all.size, all.drop(offset).take(limit))
  }

  override suspend fun findOwn(sellerId: Long, id: Long): GtlListing? =
      rows[id]?.takeIf {
        it.sellerId == sellerId && (it.state != GTL_STATE_CLOSED || it.unclaimedUnits > 0)
      }

  override suspend fun cheapestItemListings(itemId: Int, limit: Int): List<GtlListing> =
      rows.values
          .filter {
            it.state == GTL_STATE_ACTIVE && it.kind == GTL_KIND_ITEM && it.itemId == itemId
          }
          .sortedWith(compareBy({ it.price }, { it.id }))
          .take(limit)

  override suspend fun itemQuotes(limit: Int): List<Pair<Int, Int>> =
      rows.values
          .filter { it.state == GTL_STATE_ACTIVE && it.kind == GTL_KIND_ITEM }
          .groupBy { it.itemId ?: 0 }
          .map { (item, listings) -> item to listings.minOf { it.price } }
          .sortedBy { it.first }
          .take(limit)

  override suspend fun priceLevels(itemId: Int, limit: Int): List<GtlPriceLevel> =
      rows.values
          .filter {
            it.state == GTL_STATE_ACTIVE && it.kind == GTL_KIND_ITEM && it.itemId == itemId
          }
          .groupBy { it.price }
          .map { (price, listings) ->
            GtlPriceLevel(price, listings.sumOf { it.quantity.toLong() })
          }
          .sortedBy { it.price }
          .take(limit)

  override suspend fun priceHistory(itemId: Int, limit: Int): List<GtlSale> =
      rows.values
          .filter { it.kind == GTL_KIND_ITEM && it.itemId == itemId && it.soldAt != null }
          .sortedByDescending { it.soldAt }
          .take(limit)
          .map { GtlSale(it.soldAt!!, it.price) }

  override suspend fun purchaseUnits(
      id: Long,
      units: Int,
      buyerId: Long,
      now: LocalDateTime,
  ): GtlListing? {
    var taken: GtlListing? = null
    rows.computeIfPresent(id) { _, row ->
      if (row.state != GTL_STATE_ACTIVE ||
          row.remaining < units ||
          row.sellerId == buyerId ||
          !row.expiresAt.isAfter(now)) {
        row
      } else {
        taken = row
        row.copy(
            remaining = row.remaining - units,
            unclaimedUnits = row.unclaimedUnits + units,
            state = if (row.remaining == units) GTL_STATE_SOLD else row.state,
            soldAt = now,
        )
      }
    }
    return taken
  }

  override suspend fun revertUnits(id: Long, units: Int): Boolean {
    var done = false
    rows.computeIfPresent(id) { _, row ->
      if (row.unclaimedUnits < units) row
      else {
        done = true
        row.copy(
            remaining = row.remaining + units,
            unclaimedUnits = row.unclaimedUnits - units,
            state = GTL_STATE_ACTIVE,
        )
      }
    }
    return done
  }

  override suspend fun recordSale(sale: GtlSaleRow) {
    val id = saleIds.getAndIncrement()
    saleRows[id] = sale.copy(id = id)
  }

  override suspend fun claimUnits(id: Long, sellerId: Long, units: Int): Boolean {
    var done = false
    rows.computeIfPresent(id) { _, row ->
      if (row.sellerId != sellerId || row.unclaimedUnits < units) row
      else {
        done = true
        row.copy(
            unclaimedUnits = row.unclaimedUnits - units,
            state =
                if (row.state == GTL_STATE_SOLD && row.unclaimedUnits == units) GTL_STATE_CLOSED
                else row.state,
        )
      }
    }
    return done
  }

  override suspend fun changePrice(
      id: Long,
      sellerId: Long,
      newPrice: Int,
      now: LocalDateTime,
      cutoff: LocalDateTime,
  ): Boolean {
    var done = false
    rows.computeIfPresent(id) { _, row ->
      val lastChange = row.lastPriceChange
      val cooled = lastChange == null || lastChange.isBefore(cutoff)
      if (row.sellerId != sellerId ||
          row.state != GTL_STATE_ACTIVE ||
          row.price <= newPrice ||
          !cooled) {
        row
      } else {
        done = true
        row.copy(price = newPrice, lastPriceChange = now)
      }
    }
    return done
  }

  override suspend fun sales(charId: Long, limit: Int): List<GtlSaleRow> =
      saleRows.values
          .filter { it.sellerId == charId || it.buyerId == charId }
          .sortedWith(compareByDescending<GtlSaleRow> { it.soldAt }.thenByDescending { it.id })
          .take(limit)

  override suspend fun close(id: Long, sellerId: Long, fromState: Int): GtlListing? {
    var closed: GtlListing? = null
    rows.computeIfPresent(id) { _, row ->
      if (row.sellerId != sellerId || row.state != fromState || row.unclaimedUnits != 0) row
      else {
        closed = row
        row.copy(state = GTL_STATE_CLOSED)
      }
    }
    return closed
  }

  override suspend fun reopen(id: Long, sellerId: Long): Boolean {
    var done = false
    rows.computeIfPresent(id) { _, row ->
      if (row.sellerId != sellerId || row.state != GTL_STATE_CLOSED) row
      else {
        done = true
        row.copy(state = GTL_STATE_ACTIVE)
      }
    }
    return done
  }
}
