package de.fiereu.openmmo.server.game.storage

import de.fiereu.openmmo.db.game.tables.references.SOCIAL_CONTACTS
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CopyOnWriteArrayList
import javax.inject.Inject
import javax.inject.Named
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext
import org.jooq.DSLContext

/** Which of an account's two lists a contact sits on. */
const val SOCIAL_KIND_FRIEND = 0

const val SOCIAL_KIND_BLOCKED = 1

/** An account's friends and the players it has blocked. */
interface SocialRepository {
  /** One list, in the order the names were added. */
  suspend fun contacts(userId: Int, kind: Int): List<String>

  /** Adds a name, or does nothing if it is already on the list. */
  suspend fun add(userId: Int, kind: Int, name: String)

  suspend fun remove(userId: Int, kind: Int, name: String)
}

class JooqSocialRepository
@Inject
constructor(
    private val dsl: DSLContext,
    @param:Named("db") private val dispatcher: CoroutineDispatcher,
) : SocialRepository {

  override suspend fun contacts(userId: Int, kind: Int): List<String> =
      withContext(dispatcher) {
        dsl.select(SOCIAL_CONTACTS.NAME)
            .from(SOCIAL_CONTACTS)
            .where(SOCIAL_CONTACTS.USER_ID.eq(userId).and(SOCIAL_CONTACTS.KIND.eq(kind.toShort())))
            .orderBy(SOCIAL_CONTACTS.ID)
            .fetch(SOCIAL_CONTACTS.NAME)
            .filterNotNull()
      }

  override suspend fun add(userId: Int, kind: Int, name: String) {
    withContext(dispatcher) {
      dsl.insertInto(SOCIAL_CONTACTS)
          .set(SOCIAL_CONTACTS.USER_ID, userId)
          .set(SOCIAL_CONTACTS.KIND, kind.toShort())
          .set(SOCIAL_CONTACTS.NAME, name)
          .onDuplicateKeyIgnore()
          .execute()
    }
  }

  override suspend fun remove(userId: Int, kind: Int, name: String) {
    withContext(dispatcher) {
      dsl.deleteFrom(SOCIAL_CONTACTS)
          .where(
              SOCIAL_CONTACTS.USER_ID.eq(userId)
                  .and(SOCIAL_CONTACTS.KIND.eq(kind.toShort()))
                  .and(SOCIAL_CONTACTS.NAME.eq(name)))
          .execute()
    }
  }
}

/** The two lists a test builds without a database. Same ordering rule: as added. */
class InMemorySocialRepository : SocialRepository {
  private val rows = ConcurrentHashMap<Pair<Int, Int>, CopyOnWriteArrayList<String>>()

  override suspend fun contacts(userId: Int, kind: Int): List<String> =
      rows[userId to kind]?.toList() ?: emptyList()

  override suspend fun add(userId: Int, kind: Int, name: String) {
    rows.getOrPut(userId to kind) { CopyOnWriteArrayList() }.addIfAbsent(name)
  }

  override suspend fun remove(userId: Int, kind: Int, name: String) {
    rows[userId to kind]?.remove(name)
  }
}
