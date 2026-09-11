package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.db.game.tables.references.CHARACTER_SAVE_BLOCK
import javax.inject.Inject
import javax.inject.Named
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext
import org.jooq.DSLContext
import org.jooq.impl.DSL

/** The game's own save blocks, held as bytes. */
interface SaveBlockRepository {
  /** Every block held for this character, by id. Empty for a character that has never sent one. */
  suspend fun load(characterId: Long): Map<Int, ByteArray>

  /** Record what the client reported. Blocks not named are left alone; this is never a delete. */
  suspend fun save(characterId: Long, blocks: Map<Int, ByteArray>)

  /**
   * Make [blocks] the whole of what is held for this character: every id named is written and every
   * id not named is taken away.
   */
  suspend fun replace(characterId: Long, blocks: Map<Int, ByteArray>)
}

class JooqSaveBlockRepository
@Inject
constructor(
    private val dsl: DSLContext,
    @param:Named("db") private val dispatcher: CoroutineDispatcher,
) : SaveBlockRepository {

  override suspend fun load(characterId: Long): Map<Int, ByteArray> =
      withContext(dispatcher) {
        dsl.select(CHARACTER_SAVE_BLOCK.BLOCK_ID, CHARACTER_SAVE_BLOCK.DATA)
            .from(CHARACTER_SAVE_BLOCK)
            .where(CHARACTER_SAVE_BLOCK.CHARACTER_ID.eq(characterId))
            .fetch()
            .associate { it.value1()!!.toInt() to it.value2()!! }
      }

  override suspend fun save(characterId: Long, blocks: Map<Int, ByteArray>) {
    if (blocks.isEmpty()) return
    withContext(dispatcher) { upsert(dsl, characterId, blocks) }
  }

  override suspend fun replace(characterId: Long, blocks: Map<Int, ByteArray>) {
    withContext(dispatcher) {
      // One transaction, so a reader never sees the character with no blocks at all between the
      // delete and the writes.
      dsl.transaction { tx ->
        val db = DSL.using(tx)
        var gone = CHARACTER_SAVE_BLOCK.CHARACTER_ID.eq(characterId)
        if (blocks.isNotEmpty()) {
          gone = gone.and(CHARACTER_SAVE_BLOCK.BLOCK_ID.notIn(blocks.keys.map { it.toShort() }))
        }
        db.deleteFrom(CHARACTER_SAVE_BLOCK).where(gone).execute()
        upsert(db, characterId, blocks)
      }
    }
  }

  // One statement per block rather than a batch merge: jOOQ's batchMerge ORs every unique key, so
  // one source row can match two target rows and duplicate the primary key. The counts here are
  // single digits, a character holds a dozen or so of these, so there is nothing to win.
  private fun upsert(db: DSLContext, characterId: Long, blocks: Map<Int, ByteArray>) {
    for ((id, data) in blocks) {
      db.insertInto(CHARACTER_SAVE_BLOCK)
          .set(CHARACTER_SAVE_BLOCK.CHARACTER_ID, characterId)
          .set(CHARACTER_SAVE_BLOCK.BLOCK_ID, id.toShort())
          .set(CHARACTER_SAVE_BLOCK.DATA, data)
          .onConflict(CHARACTER_SAVE_BLOCK.CHARACTER_ID, CHARACTER_SAVE_BLOCK.BLOCK_ID)
          .doUpdate()
          .set(CHARACTER_SAVE_BLOCK.DATA, data)
          .execute()
    }
  }
}

/** The same store with no database behind it, for tests and for a server run without one. */
class InMemorySaveBlockRepository : SaveBlockRepository {
  private val byCharacter = mutableMapOf<Long, MutableMap<Int, ByteArray>>()

  override suspend fun load(characterId: Long): Map<Int, ByteArray> =
      byCharacter[characterId]?.toMap() ?: emptyMap()

  override suspend fun save(characterId: Long, blocks: Map<Int, ByteArray>) {
    byCharacter.getOrPut(characterId) { mutableMapOf() }.putAll(blocks)
  }

  override suspend fun replace(characterId: Long, blocks: Map<Int, ByteArray>) {
    byCharacter[characterId] = blocks.toMutableMap()
  }
}
