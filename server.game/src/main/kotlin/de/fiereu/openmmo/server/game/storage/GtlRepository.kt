package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.common.ContestConditions
import de.fiereu.openmmo.common.DEFAULT_FRIENDSHIP
import de.fiereu.openmmo.common.Pokemon
import de.fiereu.openmmo.common.PokemonMove
import de.fiereu.openmmo.common.enums.EVs
import de.fiereu.openmmo.common.enums.IVs
import de.fiereu.openmmo.common.enums.PokemonContainer
import de.fiereu.openmmo.db.game.tables.records.GtlListingsRecord
import de.fiereu.openmmo.db.game.tables.records.GtlSalesRecord
import de.fiereu.openmmo.db.game.tables.references.GTL_LISTINGS
import de.fiereu.openmmo.db.game.tables.references.GTL_SALES
import java.time.LocalDateTime
import javax.inject.Inject
import javax.inject.Named
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext
import org.jooq.Condition
import org.jooq.DSLContext
import org.jooq.impl.DSL

/** GtlListKind's own ids: what a listing carries. */
const val GTL_KIND_POKEMON = 0
const val GTL_KIND_ITEM = 1

/** Where a listing stands. Sold keeps standing until the seller is online to be paid. */
const val GTL_STATE_ACTIVE = 0
const val GTL_STATE_SOLD = 1
const val GTL_STATE_CLOSED = 2

/** One shelf entry. */
data class GtlListing(
    val id: Long,
    val sellerId: Long,
    val sellerName: String,
    val kind: Int,
    val itemId: Int?,
    val quantity: Int,
    val price: Int,
    val state: Int,
    val buyerId: Long?,
    val buyerName: String?,
    val listedAt: LocalDateTime,
    val expiresAt: LocalDateTime,
    val soldAt: LocalDateTime?,
    val pokemon: Pokemon?,
    /** Units still on the shelf. A monster listing is one unit. */
    val remaining: Int = 0,
    /** Units sold whose money the seller has not pressed Claim on. */
    val unclaimedUnits: Int = 0,
    /** What listing cost to put up. Fees are not refunded. */
    val fee: Int = 0,
    val lastPriceChange: LocalDateTime? = null,
)

/** One finished fill, the trade log's row. A snapshot: it outlives the listing. */
data class GtlSaleRow(
    val id: Long,
    val listingId: Long,
    val sellerId: Long,
    val sellerName: String,
    val buyerId: Long,
    val buyerName: String,
    val kind: Int,
    val itemId: Int?,
    val monDexId: Int?,
    val monLevel: Int?,
    val quantity: Int,
    val unitPrice: Int,
    val soldAt: LocalDateTime,
)

/** What a monster search narrows by. A null field is a filter the request did not set. */
data class GtlMonsterQuery(
    val speciesIds: List<Int>? = null,
    val minLevel: Int? = null,
    val maxLevel: Int? = null,
    val shiny: Boolean? = null,
    val nature: Int? = null,
    val minPrice: Int? = null,
    val maxPrice: Int? = null,
)

/** One page of a shelf query, with the total the pager needs. */
data class GtlPage(val total: Int, val listings: List<GtlListing>)

/** A price-and-quantity rung of one item's ladder. */
data class GtlPriceLevel(val price: Int, val quantity: Long)

/** One finished sale, for the price history strip. */
data class GtlSale(val soldAt: LocalDateTime, val price: Int)

/** The sort orders a page request can ask for. */
const val GTL_SORT_NEWEST = 0
const val GTL_SORT_OLDEST = 1
const val GTL_SORT_PRICE_ASC = 2
const val GTL_SORT_PRICE_DESC = 3

interface GtlRepository {
  /** Insert with a generated id; the returned listing carries it. */
  suspend fun insert(listing: GtlListing): GtlListing

  /** How many listings the seller has on the shelf right now. */
  suspend fun standingCount(sellerId: Long): Int

  suspend fun monsterPage(query: GtlMonsterQuery, sort: Int, offset: Int, limit: Int): GtlPage

  suspend fun itemPage(minPrice: Int?, maxPrice: Int?, sort: Int, offset: Int, limit: Int): GtlPage

  /** The seller's own shelf: active listings, newest first. Sold ones pay out on login instead. */
  suspend fun ownPage(sellerId: Long, offset: Int, limit: Int): GtlPage

  /**
   * One of the seller's own listings by its id, on the same terms [ownPage] shows them: a listing
   * closed with nothing owed on it is off their shelf, and somebody else's is never theirs.
   */
  suspend fun findOwn(sellerId: Long, id: Long): GtlListing?

  /** The cheapest active listings of one item, for a market buy that fills across sellers. */
  suspend fun cheapestItemListings(itemId: Int, limit: Int): List<GtlListing>

  /** The cheapest asking price per item id, for the quote strip under an item page. */
  suspend fun itemQuotes(limit: Int): List<Pair<Int, Int>>

  /** One item's ladder: each asked price and how many are on it, cheapest first. */
  suspend fun priceLevels(itemId: Int, limit: Int): List<GtlPriceLevel>

  /** One item's finished sales, newest first. */
  suspend fun priceHistory(itemId: Int, limit: Int): List<GtlSale>

  /**
   * Take [units] of an active, unexpired listing for [buyerId], the single-winner step of a
   * purchase: only the update that decrements `remaining` sees the row, so two buyers cannot be
   * handed the same units.
   */
  suspend fun purchaseUnits(
      id: Long,
      units: Int,
      buyerId: Long,
      now: LocalDateTime,
  ): GtlListing?

  /** Put a fill back, for a buyer whose payment or delivery then refused. */
  suspend fun revertUnits(id: Long, units: Int): Boolean

  /** One finished fill into the log. */
  suspend fun recordSale(sale: GtlSaleRow)

  /**
   * Take [units] of claimed money off the row for its seller. Only what was read is taken, so a
   * fill landing between the read and the claim stays claimable.
   */
  suspend fun claimUnits(id: Long, sellerId: Long, units: Int): Boolean

  /** Lower a standing listing's price. [cutoff] is the oldest allowed previous change. */
  suspend fun changePrice(
      id: Long,
      sellerId: Long,
      newPrice: Int,
      now: LocalDateTime,
      cutoff: LocalDateTime
  ): Boolean

  /** The trade log: this character's fills, both chairs, newest first. */
  suspend fun sales(charId: Long, limit: Int): List<GtlSaleRow>

  /** Close the seller's own listing out of [fromState], answering with the row as it was. */
  suspend fun close(id: Long, sellerId: Long, fromState: Int): GtlListing?

  /** Put a just-closed listing back on the shelf, for a take-back whose return had no room. */
  suspend fun reopen(id: Long, sellerId: Long): Boolean
}

class JooqGtlRepository
@Inject
constructor(
    private val dsl: DSLContext,
    @param:Named("db") private val dispatcher: CoroutineDispatcher,
) : GtlRepository {

  override suspend fun insert(listing: GtlListing): GtlListing =
      withContext(dispatcher) {
        val record = listing.toRecord()
        record.reset(GTL_LISTINGS.ID)
        val id =
            dsl.insertInto(GTL_LISTINGS)
                .set(record)
                .returning(GTL_LISTINGS.ID)
                .fetchSingle()[GTL_LISTINGS.ID]!!
        listing.copy(id = id)
      }

  override suspend fun standingCount(sellerId: Long): Int =
      withContext(dispatcher) {
        dsl.fetchCount(
            GTL_LISTINGS,
            GTL_LISTINGS.SELLER_ID.eq(sellerId)
                .and(GTL_LISTINGS.STATE.eq(GTL_STATE_ACTIVE.toShort())))
      }

  override suspend fun monsterPage(
      query: GtlMonsterQuery,
      sort: Int,
      offset: Int,
      limit: Int
  ): GtlPage {
    var cond: Condition =
        GTL_LISTINGS.STATE.eq(GTL_STATE_ACTIVE.toShort())
            .and(GTL_LISTINGS.REMAINING.gt(0))
            .and(GTL_LISTINGS.KIND.eq(GTL_KIND_POKEMON.toShort()))
    query.speciesIds?.let { cond = cond.and(GTL_LISTINGS.MON_DEX_ID.`in`(it)) }
    query.minLevel?.let { cond = cond.and(GTL_LISTINGS.MON_LEVEL.ge(it.toShort())) }
    query.maxLevel?.let { cond = cond.and(GTL_LISTINGS.MON_LEVEL.le(it.toShort())) }
    query.shiny?.let { cond = cond.and(GTL_LISTINGS.MON_IS_SHINY.eq(it)) }
    query.nature?.let {
      // Nature is derived from the seed, not stored: unsigned seed modulo the 25 natures.
      cond =
          cond.and(
              DSL.bitAnd(GTL_LISTINGS.MON_SEED.cast(Long::class.java), 0xFFFFFFFFL)
                  .mod(25L)
                  .eq(it.toLong()))
    }
    query.minPrice?.let { cond = cond.and(GTL_LISTINGS.PRICE.ge(it)) }
    query.maxPrice?.let { cond = cond.and(GTL_LISTINGS.PRICE.le(it)) }
    return page(cond, sort, offset, limit)
  }

  override suspend fun itemPage(
      minPrice: Int?,
      maxPrice: Int?,
      sort: Int,
      offset: Int,
      limit: Int
  ): GtlPage {
    var cond: Condition =
        GTL_LISTINGS.STATE.eq(GTL_STATE_ACTIVE.toShort())
            .and(GTL_LISTINGS.REMAINING.gt(0))
            .and(GTL_LISTINGS.KIND.eq(GTL_KIND_ITEM.toShort()))
    minPrice?.let { cond = cond.and(GTL_LISTINGS.PRICE.ge(it)) }
    maxPrice?.let { cond = cond.and(GTL_LISTINGS.PRICE.le(it)) }
    return page(cond, sort, offset, limit)
  }

  override suspend fun ownPage(sellerId: Long, offset: Int, limit: Int): GtlPage =
      withContext(dispatcher) {
        val cond =
            GTL_LISTINGS.SELLER_ID.eq(sellerId)
                .and(
                    GTL_LISTINGS.STATE.ne(GTL_STATE_CLOSED.toShort())
                        .or(GTL_LISTINGS.UNCLAIMED_UNITS.gt(0)))
        val total = dsl.fetchCount(GTL_LISTINGS, cond)
        val rows =
            dsl.selectFrom(GTL_LISTINGS)
                .where(cond)
                .orderBy(GTL_LISTINGS.LISTED_AT.desc(), GTL_LISTINGS.ID.desc())
                .offset(offset)
                .limit(limit)
                .fetch()
                .map { it.toListing() }
                .toList()
        GtlPage(total, rows)
      }

  override suspend fun findOwn(sellerId: Long, id: Long): GtlListing? =
      withContext(dispatcher) {
        dsl.selectFrom(GTL_LISTINGS)
            .where(GTL_LISTINGS.ID.eq(id))
            .and(GTL_LISTINGS.SELLER_ID.eq(sellerId))
            .and(
                GTL_LISTINGS.STATE.ne(GTL_STATE_CLOSED.toShort())
                    .or(GTL_LISTINGS.UNCLAIMED_UNITS.gt(0)))
            .fetchOne()
            ?.toListing()
      }

  override suspend fun cheapestItemListings(itemId: Int, limit: Int): List<GtlListing> =
      withContext(dispatcher) {
        dsl.selectFrom(GTL_LISTINGS)
            .where(GTL_LISTINGS.STATE.eq(GTL_STATE_ACTIVE.toShort()))
            .and(GTL_LISTINGS.KIND.eq(GTL_KIND_ITEM.toShort()))
            .and(GTL_LISTINGS.ITEM_ID.eq(itemId))
            .orderBy(GTL_LISTINGS.PRICE.asc(), GTL_LISTINGS.ID.asc())
            .limit(limit)
            .fetch()
            .map { it.toListing() }
      }

  private suspend fun page(cond: Condition, sort: Int, offset: Int, limit: Int): GtlPage =
      withContext(dispatcher) {
        val order =
            when (sort) {
              GTL_SORT_OLDEST -> listOf(GTL_LISTINGS.LISTED_AT.asc(), GTL_LISTINGS.ID.asc())
              GTL_SORT_PRICE_ASC -> listOf(GTL_LISTINGS.PRICE.asc(), GTL_LISTINGS.ID.asc())
              GTL_SORT_PRICE_DESC -> listOf(GTL_LISTINGS.PRICE.desc(), GTL_LISTINGS.ID.asc())
              else -> listOf(GTL_LISTINGS.LISTED_AT.desc(), GTL_LISTINGS.ID.desc())
            }
        val total = dsl.fetchCount(GTL_LISTINGS, cond)
        val rows =
            dsl.selectFrom(GTL_LISTINGS)
                .where(cond)
                .orderBy(order)
                .offset(offset)
                .limit(limit)
                .fetch()
                .map { it.toListing() }
                .toList()
        GtlPage(total, rows)
      }

  override suspend fun itemQuotes(limit: Int): List<Pair<Int, Int>> =
      withContext(dispatcher) {
        dsl.select(GTL_LISTINGS.ITEM_ID, DSL.min(GTL_LISTINGS.PRICE))
            .from(GTL_LISTINGS)
            .where(
                GTL_LISTINGS.STATE.eq(GTL_STATE_ACTIVE.toShort())
                    .and(GTL_LISTINGS.KIND.eq(GTL_KIND_ITEM.toShort())))
            .groupBy(GTL_LISTINGS.ITEM_ID)
            .orderBy(GTL_LISTINGS.ITEM_ID.asc())
            .limit(limit)
            .fetch()
            .map { (it.value1() ?: 0) to (it.value2() ?: 0) }
      }

  override suspend fun priceLevels(itemId: Int, limit: Int): List<GtlPriceLevel> =
      withContext(dispatcher) {
        dsl.select(GTL_LISTINGS.PRICE, DSL.sum(GTL_LISTINGS.QUANTITY))
            .from(GTL_LISTINGS)
            .where(
                GTL_LISTINGS.STATE.eq(GTL_STATE_ACTIVE.toShort())
                    .and(GTL_LISTINGS.KIND.eq(GTL_KIND_ITEM.toShort()))
                    .and(GTL_LISTINGS.ITEM_ID.eq(itemId)))
            .groupBy(GTL_LISTINGS.PRICE)
            .orderBy(GTL_LISTINGS.PRICE.asc())
            .limit(limit)
            .fetch()
            .map { GtlPriceLevel(it.value1() ?: 0, it.value2()?.toLong() ?: 0L) }
      }

  override suspend fun priceHistory(itemId: Int, limit: Int): List<GtlSale> =
      withContext(dispatcher) {
        dsl.select(GTL_LISTINGS.SOLD_AT, GTL_LISTINGS.PRICE)
            .from(GTL_LISTINGS)
            .where(
                GTL_LISTINGS.KIND.eq(GTL_KIND_ITEM.toShort())
                    .and(GTL_LISTINGS.ITEM_ID.eq(itemId))
                    .and(GTL_LISTINGS.SOLD_AT.isNotNull))
            .orderBy(GTL_LISTINGS.SOLD_AT.desc())
            .limit(limit)
            .fetch()
            .mapNotNull { row -> row.value1()?.let { GtlSale(it, row.value2() ?: 0) } }
      }

  override suspend fun purchaseUnits(
      id: Long,
      units: Int,
      buyerId: Long,
      now: LocalDateTime,
  ): GtlListing? =
      withContext(dispatcher) {
        val row =
            dsl.update(GTL_LISTINGS)
                .set(GTL_LISTINGS.REMAINING, GTL_LISTINGS.REMAINING.minus(units))
                .set(GTL_LISTINGS.UNCLAIMED_UNITS, GTL_LISTINGS.UNCLAIMED_UNITS.plus(units))
                .set(
                    GTL_LISTINGS.STATE,
                    DSL.`when`(GTL_LISTINGS.REMAINING.eq(units), GTL_STATE_SOLD.toShort())
                        .otherwise(GTL_LISTINGS.STATE))
                .set(GTL_LISTINGS.SOLD_AT, now)
                .where(GTL_LISTINGS.ID.eq(id))
                .and(GTL_LISTINGS.STATE.eq(GTL_STATE_ACTIVE.toShort()))
                .and(GTL_LISTINGS.REMAINING.ge(units))
                .and(GTL_LISTINGS.EXPIRES_AT.gt(now))
                .and(GTL_LISTINGS.SELLER_ID.ne(buyerId))
                .returning()
                .fetchOne()
                ?.toListing()
        // The caller wants the row as it stood before the fill it just took.
        row?.copy(remaining = row.remaining + units, unclaimedUnits = row.unclaimedUnits - units)
      }

  override suspend fun revertUnits(id: Long, units: Int): Boolean =
      withContext(dispatcher) {
        dsl.update(GTL_LISTINGS)
            .set(GTL_LISTINGS.REMAINING, GTL_LISTINGS.REMAINING.plus(units))
            .set(GTL_LISTINGS.UNCLAIMED_UNITS, GTL_LISTINGS.UNCLAIMED_UNITS.minus(units))
            .set(GTL_LISTINGS.STATE, GTL_STATE_ACTIVE.toShort())
            .where(GTL_LISTINGS.ID.eq(id))
            .and(GTL_LISTINGS.UNCLAIMED_UNITS.ge(units))
            .execute() == 1
      }

  override suspend fun recordSale(sale: GtlSaleRow): Unit =
      withContext(dispatcher) {
        val record = sale.toRecord()
        record.reset(GTL_SALES.ID)
        dsl.insertInto(GTL_SALES).set(record).execute()
        Unit
      }

  override suspend fun claimUnits(id: Long, sellerId: Long, units: Int): Boolean =
      withContext(dispatcher) {
        dsl.update(GTL_LISTINGS)
            .set(GTL_LISTINGS.UNCLAIMED_UNITS, GTL_LISTINGS.UNCLAIMED_UNITS.minus(units))
            .set(
                GTL_LISTINGS.STATE,
                DSL.`when`(
                        GTL_LISTINGS.STATE.eq(GTL_STATE_SOLD.toShort())
                            .and(GTL_LISTINGS.UNCLAIMED_UNITS.eq(units)),
                        GTL_STATE_CLOSED.toShort())
                    .otherwise(GTL_LISTINGS.STATE))
            .where(GTL_LISTINGS.ID.eq(id))
            .and(GTL_LISTINGS.SELLER_ID.eq(sellerId))
            .and(GTL_LISTINGS.UNCLAIMED_UNITS.ge(units))
            .execute() == 1
      }

  override suspend fun changePrice(
      id: Long,
      sellerId: Long,
      newPrice: Int,
      now: LocalDateTime,
      cutoff: LocalDateTime,
  ): Boolean =
      withContext(dispatcher) {
        dsl.update(GTL_LISTINGS)
            .set(GTL_LISTINGS.PRICE, newPrice)
            .set(GTL_LISTINGS.LAST_PRICE_CHANGE, now)
            .where(GTL_LISTINGS.ID.eq(id))
            .and(GTL_LISTINGS.SELLER_ID.eq(sellerId))
            .and(GTL_LISTINGS.STATE.eq(GTL_STATE_ACTIVE.toShort()))
            .and(GTL_LISTINGS.PRICE.gt(newPrice))
            .and(
                GTL_LISTINGS.LAST_PRICE_CHANGE.isNull.or(GTL_LISTINGS.LAST_PRICE_CHANGE.lt(cutoff)))
            .execute() == 1
      }

  override suspend fun sales(charId: Long, limit: Int): List<GtlSaleRow> =
      withContext(dispatcher) {
        dsl.selectFrom(GTL_SALES)
            .where(GTL_SALES.SELLER_ID.eq(charId).or(GTL_SALES.BUYER_ID.eq(charId)))
            .orderBy(GTL_SALES.SOLD_AT.desc(), GTL_SALES.ID.desc())
            .limit(limit)
            .fetch()
            .map { it.toSale() }
      }

  override suspend fun close(id: Long, sellerId: Long, fromState: Int): GtlListing? =
      withContext(dispatcher) {
        dsl.update(GTL_LISTINGS)
            .set(GTL_LISTINGS.STATE, GTL_STATE_CLOSED.toShort())
            .where(GTL_LISTINGS.ID.eq(id))
            .and(GTL_LISTINGS.SELLER_ID.eq(sellerId))
            .and(GTL_LISTINGS.STATE.eq(fromState.toShort()))
            .and(GTL_LISTINGS.UNCLAIMED_UNITS.eq(0))
            .returning()
            .fetchOne()
            ?.toListing()
            // The returned row already carries the closed state; hand back what was closed from.
            ?.copy(state = fromState)
      }

  override suspend fun reopen(id: Long, sellerId: Long): Boolean =
      withContext(dispatcher) {
        dsl.update(GTL_LISTINGS)
            .set(GTL_LISTINGS.STATE, GTL_STATE_ACTIVE.toShort())
            .where(GTL_LISTINGS.ID.eq(id))
            .and(GTL_LISTINGS.SELLER_ID.eq(sellerId))
            .and(GTL_LISTINGS.STATE.eq(GTL_STATE_CLOSED.toShort()))
            .execute() == 1
      }

  private fun GtlListing.toRecord(): GtlListingsRecord =
      GtlListingsRecord(
          id = id,
          sellerId = sellerId,
          sellerName = sellerName,
          kind = kind.toShort(),
          itemId = itemId,
          quantity = quantity,
          price = price,
          state = state.toShort(),
          buyerId = buyerId,
          buyerName = buyerName,
          listedAt = listedAt,
          expiresAt = expiresAt,
          soldAt = soldAt,
          monId = pokemon?.id,
          monDexId = pokemon?.dexId,
          monSeed = pokemon?.seed,
          monOt = pokemon?.ot,
          monNickname = pokemon?.nickname,
          monLevel = pokemon?.level?.toShort(),
          monHp = pokemon?.hp,
          monXp = pokemon?.xp,
          monEvHp = pokemon?.eVs?.hp?.toShort(),
          monEvAtk = pokemon?.eVs?.atk?.toShort(),
          monEvDef = pokemon?.eVs?.def?.toShort(),
          monEvSpAtk = pokemon?.eVs?.spAtk?.toShort(),
          monEvSpDef = pokemon?.eVs?.spDef?.toShort(),
          monEvSpd = pokemon?.eVs?.spd?.toShort(),
          monIvHp = pokemon?.iVs?.hp?.toShort(),
          monIvAtk = pokemon?.iVs?.atk?.toShort(),
          monIvDef = pokemon?.iVs?.def?.toShort(),
          monIvSpAtk = pokemon?.iVs?.spAtk?.toShort(),
          monIvSpDef = pokemon?.iVs?.spDef?.toShort(),
          monIvSpd = pokemon?.iVs?.spd?.toShort(),
          monMove1Id = pokemon?.moves?.getOrNull(0)?.id,
          monMove1Pp = pokemon?.moves?.getOrNull(0)?.pp?.toShort(),
          monMove2Id = pokemon?.moves?.getOrNull(1)?.id,
          monMove2Pp = pokemon?.moves?.getOrNull(1)?.pp?.toShort(),
          monMove3Id = pokemon?.moves?.getOrNull(2)?.id,
          monMove3Pp = pokemon?.moves?.getOrNull(2)?.pp?.toShort(),
          monMove4Id = pokemon?.moves?.getOrNull(3)?.id,
          monMove4Pp = pokemon?.moves?.getOrNull(3)?.pp?.toShort(),
          monHeldItemId = pokemon?.heldItemId,
          monOfflineOrigin = pokemon?.offlineOrigin,
          monForm = pokemon?.form?.toShort(),
          monCondCool = pokemon?.conditions?.cool?.toShort(),
          monCondBeauty = pokemon?.conditions?.beauty?.toShort(),
          monCondCute = pokemon?.conditions?.cute?.toShort(),
          monCondSmart = pokemon?.conditions?.smart?.toShort(),
          monCondTough = pokemon?.conditions?.tough?.toShort(),
          monSheen = pokemon?.sheen?.toShort(),
          monSuperContestRibbons = pokemon?.superContestRibbons,
          monCaughtRegionId = pokemon?.caughtRegionId?.toShort(),
          monCaughtBankId = pokemon?.caughtBankId?.toShort(),
          monCaughtMapId = pokemon?.caughtMapId?.toShort(),
          monCaughtLocationLabel = pokemon?.caughtLocationLabel?.toShort(),
          monFriendship = pokemon?.friendship?.toShort(),
          monStatus = pokemon?.status?.toShort(),
          monIsShiny = pokemon?.isShiny,
          monHasHiddenAbility = pokemon?.hasHiddenAbility,
          monIsAlpha = pokemon?.isAlpha,
          monIsSecret = pokemon?.isSecret,
          monIsFatefulEncounter = pokemon?.isFatefulEncounter,
          monIsRaidEncounter = pokemon?.isRaidEncounter,
          monIsEgg = pokemon?.isEgg,
          monCaughtAt = pokemon?.caughtAt,
          remaining = remaining,
          unclaimedUnits = unclaimedUnits,
          fee = fee,
          lastPriceChange = lastPriceChange,
      )

  private fun GtlSaleRow.toRecord(): GtlSalesRecord =
      GtlSalesRecord(
          id = id,
          listingId = listingId,
          sellerId = sellerId,
          sellerName = sellerName,
          buyerId = buyerId,
          buyerName = buyerName,
          kind = kind.toShort(),
          itemId = itemId,
          monDexId = monDexId,
          monLevel = monLevel?.toShort(),
          quantity = quantity,
          unitPrice = unitPrice,
          soldAt = soldAt,
      )

  private fun GtlSalesRecord.toSale(): GtlSaleRow =
      GtlSaleRow(
          id = id!!,
          listingId = listingId,
          sellerId = sellerId,
          sellerName = sellerName,
          buyerId = buyerId,
          buyerName = buyerName,
          kind = kind.toInt(),
          itemId = itemId,
          monDexId = monDexId,
          monLevel = monLevel?.toInt(),
          quantity = quantity,
          unitPrice = unitPrice,
          soldAt = soldAt,
      )

  private fun GtlListingsRecord.toListing(): GtlListing =
      GtlListing(
          id = id!!,
          sellerId = sellerId,
          sellerName = sellerName,
          kind = kind.toInt(),
          itemId = itemId,
          quantity = quantity ?: 1,
          price = price,
          state = (state ?: 0).toInt(),
          buyerId = buyerId,
          buyerName = buyerName,
          listedAt = listedAt,
          expiresAt = expiresAt,
          soldAt = soldAt,
          remaining = remaining ?: 0,
          unclaimedUnits = unclaimedUnits ?: 0,
          fee = fee ?: 0,
          lastPriceChange = lastPriceChange,
          pokemon =
              monId?.let { monsterId ->
                Pokemon(
                    id = monsterId,
                    // The escrow has no owner; whoever it is handed to seats it properly.
                    ownerId = sellerId,
                    container = PokemonContainer.GTS,
                    containerSlot = 0,
                    dexId = monDexId ?: 0,
                    seed = monSeed ?: 0,
                    ot = monOt ?: "",
                    nickname = monNickname ?: "",
                    level = (monLevel ?: 1).toByte(),
                    hp = monHp ?: 1,
                    xp = monXp ?: 0,
                    eVs =
                        EVs().also {
                          it.hp = (monEvHp ?: 0).toInt()
                          it.atk = (monEvAtk ?: 0).toInt()
                          it.def = (monEvDef ?: 0).toInt()
                          it.spAtk = (monEvSpAtk ?: 0).toInt()
                          it.spDef = (monEvSpDef ?: 0).toInt()
                          it.spd = (monEvSpd ?: 0).toInt()
                        },
                    iVs =
                        IVs().also {
                          it.hp = (monIvHp ?: 0).toInt()
                          it.atk = (monIvAtk ?: 0).toInt()
                          it.def = (monIvDef ?: 0).toInt()
                          it.spAtk = (monIvSpAtk ?: 0).toInt()
                          it.spDef = (monIvSpDef ?: 0).toInt()
                          it.spd = (monIvSpd ?: 0).toInt()
                        },
                    moves =
                        listOf(
                            PokemonMove(monMove1Id ?: 0, (monMove1Pp ?: 0).toByte()),
                            PokemonMove(monMove2Id ?: 0, (monMove2Pp ?: 0).toByte()),
                            PokemonMove(monMove3Id ?: 0, (monMove3Pp ?: 0).toByte()),
                            PokemonMove(monMove4Id ?: 0, (monMove4Pp ?: 0).toByte()),
                        ),
                    heldItemId = monHeldItemId ?: 0,
                    offlineOrigin = monOfflineOrigin ?: false,
                    form = (monForm ?: 0).toInt(),
                    conditions =
                        ContestConditions(
                            cool = (monCondCool ?: 0).toInt(),
                            beauty = (monCondBeauty ?: 0).toInt(),
                            cute = (monCondCute ?: 0).toInt(),
                            smart = (monCondSmart ?: 0).toInt(),
                            tough = (monCondTough ?: 0).toInt(),
                        ),
                    sheen = (monSheen ?: 0).toInt(),
                    superContestRibbons = monSuperContestRibbons ?: 0L,
                    caughtRegionId = (monCaughtRegionId ?: -1).toInt(),
                    caughtBankId = (monCaughtBankId ?: -1).toInt(),
                    caughtMapId = (monCaughtMapId ?: -1).toInt(),
                    caughtLocationLabel = (monCaughtLocationLabel ?: 0).toInt(),
                    friendship = (monFriendship ?: DEFAULT_FRIENDSHIP.toShort()).toInt(),
                    status = (monStatus ?: 0).toInt(),
                    isShiny = monIsShiny ?: false,
                    hasHiddenAbility = monHasHiddenAbility ?: false,
                    isAlpha = monIsAlpha ?: false,
                    isSecret = monIsSecret ?: false,
                    isFatefulEncounter = monIsFatefulEncounter ?: false,
                    isRaidEncounter = monIsRaidEncounter ?: false,
                    isEgg = monIsEgg ?: false,
                    caughtAt = monCaughtAt ?: listedAt,
                )
              },
      )
}
