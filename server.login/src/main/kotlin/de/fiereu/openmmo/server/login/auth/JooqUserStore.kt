package de.fiereu.openmmo.server.login.auth

import de.fiereu.openmmo.common.enums.LoginState
import de.fiereu.openmmo.db.login.tables.references.USERS
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Named
import javax.inject.Singleton
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext
import org.jooq.DSLContext

private val log = KotlinLogging.logger {}

@Singleton
class JooqUserStore
@Inject
constructor(
    private val dsl: DSLContext,
    @param:Named("db") private val dispatcher: CoroutineDispatcher,
) : UserService {

  override suspend fun authenticate(username: String, password: String): UserService.AuthResult =
      withContext(dispatcher) {
        val user = dsl.selectFrom(USERS).where(USERS.USERNAME.eq(username.lowercase())).fetchOne()
        if (user == null || !PasswordHash.verify(user.passwordHash, password)) {
          UserService.AuthResult(LoginState.INVALID_PASSWORD)
        } else {
          // The account still holds the old shape, and somebody just proved they own it, so this
          // is the moment to rewrite it. The startup sweep gets the ones nobody logs into.
          if (PasswordHash.isLegacy(user.passwordHash)) {
            dsl.update(USERS)
                .set(USERS.PASSWORD_HASH, PasswordHash.hash(password))
                .where(USERS.ID.eq(user.id))
                .execute()
            log.info { "Rewrote the stored credential for user ${user.id}" }
          }
          UserService.AuthResult(LoginState.AUTHED, user.id, user.tokenEpoch ?: 0)
        }
      }

  /**
   * Rewrite every credential still stored the old way, and answer how many there were. A row nobody
   * signs into is the one a dump is read from, so leaving them until their owner turns up would
   * leave most of the table as it was.
   */
  suspend fun upgradeLegacyHashes(): Int =
      withContext(dispatcher) {
        val rows = dsl.select(USERS.ID, USERS.PASSWORD_HASH).from(USERS).fetch()
        var done = 0
        for (row in rows) {
          val id = row[USERS.ID] ?: continue
          val stored = row[USERS.PASSWORD_HASH] ?: continue
          if (!PasswordHash.isLegacy(stored)) continue
          dsl.update(USERS)
              .set(USERS.PASSWORD_HASH, PasswordHash.hash(stored))
              .where(USERS.ID.eq(id))
              .and(USERS.PASSWORD_HASH.eq(stored))
              .execute()
          done++
        }
        if (done > 0) log.info { "Rewrote $done stored credential(s) that were still unsalted" }
        done
      }

  override suspend fun findForToken(userId: Int): UserService.TokenUser? =
      withContext(dispatcher) {
        dsl.selectFrom(USERS).where(USERS.ID.eq(userId)).fetchOne()?.let {
          UserService.TokenUser(it.id!!, it.username, it.displayName, it.tokenEpoch ?: 0)
        }
      }

  override suspend fun getUserId(username: String): Int? =
      withContext(dispatcher) {
        dsl.select(USERS.ID)
            .from(USERS)
            .where(USERS.USERNAME.eq(username.lowercase()))
            .fetchOne(USERS.ID)
      }

  override suspend fun hasAnyUser(): Boolean =
      withContext(dispatcher) { dsl.fetchExists(dsl.selectFrom(USERS)) }

  override suspend fun addUser(username: String, password: String): Int =
      withContext(dispatcher) {
        dsl.insertInto(USERS)
            .set(USERS.USERNAME, username.lowercase())
            .set(USERS.DISPLAY_NAME, username)
            .set(USERS.PASSWORD_HASH, PasswordHash.hash(sha1Hex(password)))
            .returning(USERS.ID)
            .fetchSingle()
            .id!!
      }
}
