package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.db.game.tables.references.CHARACTER_OFFLINE_ITEMS
import javax.inject.Inject
import javax.inject.Named
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext
import org.jooq.DSLContext
import org.jooq.impl.DSL

/** How much of what a character holds came out of a save file. */
interface OfflineItemRepository {

  /** How many of each item id this character has that came from a file. */
  suspend fun load(characterId: Long): Map<Int, Int>

  /** Make [items] the whole of what is marked for this character; ids not named are cleared. */
  suspend fun replace(characterId: Long, items: Map<Int, Int>)
}

class JooqOfflineItemRepository
@Inject
constructor(
    private val dsl: DSLContext,
    @param:Named("db") private val dispatcher: CoroutineDispatcher,
) : OfflineItemRepository {

  override suspend fun load(characterId: Long): Map<Int, Int> =
      withContext(dispatcher) {
        dsl.select(CHARACTER_OFFLINE_ITEMS.ITEM_ID, CHARACTER_OFFLINE_ITEMS.QUANTITY)
            .from(CHARACTER_OFFLINE_ITEMS)
            .where(CHARACTER_OFFLINE_ITEMS.CHARACTER_ID.eq(characterId))
            .fetch()
            .associate { it.value1()!! to it.value2()!! }
      }

  override suspend fun replace(characterId: Long, items: Map<Int, Int>) {
    val kept = items.filterValues { it > 0 }
    withContext(dispatcher) {
      // One transaction, so a mart never reads a character with half its marks between the delete
      // and the writes, which for that moment would be a character whose file items are sellable.
      dsl.transaction { tx ->
        val db = DSL.using(tx)
        var gone = CHARACTER_OFFLINE_ITEMS.CHARACTER_ID.eq(characterId)
        if (kept.isNotEmpty()) {
          gone = gone.and(CHARACTER_OFFLINE_ITEMS.ITEM_ID.notIn(kept.keys))
        }
        db.deleteFrom(CHARACTER_OFFLINE_ITEMS).where(gone).execute()
        // One statement each rather than a batch merge, for the reason JooqSaveBlockRepository
        // gives: jOOQ's batchMerge ORs every unique key. A bag is a few dozen rows.
        for ((id, quantity) in kept) {
          db.insertInto(CHARACTER_OFFLINE_ITEMS)
              .set(CHARACTER_OFFLINE_ITEMS.CHARACTER_ID, characterId)
              .set(CHARACTER_OFFLINE_ITEMS.ITEM_ID, id)
              .set(CHARACTER_OFFLINE_ITEMS.QUANTITY, quantity)
              .onConflict(CHARACTER_OFFLINE_ITEMS.CHARACTER_ID, CHARACTER_OFFLINE_ITEMS.ITEM_ID)
              .doUpdate()
              .set(CHARACTER_OFFLINE_ITEMS.QUANTITY, quantity)
              .execute()
        }
      }
    }
  }
}

/** The same store with no database behind it, for tests and for a server run without one. */
class InMemoryOfflineItemRepository : OfflineItemRepository {
  private val byCharacter = mutableMapOf<Long, Map<Int, Int>>()

  override suspend fun load(characterId: Long): Map<Int, Int> =
      byCharacter[characterId] ?: emptyMap()

  override suspend fun replace(characterId: Long, items: Map<Int, Int>) {
    byCharacter[characterId] = items.filterValues { it > 0 }
  }
}

/** How many of [itemId] this character may sell: what they hold, less what came out of a file. */
fun sellableQuantity(held: Int, marked: Int): Int =
    (held - marked.coerceAtMost(held)).coerceAtLeast(0)
