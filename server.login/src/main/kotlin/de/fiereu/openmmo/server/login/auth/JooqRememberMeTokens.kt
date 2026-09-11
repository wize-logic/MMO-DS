package de.fiereu.openmmo.server.login.auth

import de.fiereu.openmmo.db.login.tables.references.REMEMBER_ME_TOKENS
import de.fiereu.openmmo.server.login.config.LoginServerConfig
import java.time.LocalDateTime
import javax.inject.Inject
import javax.inject.Named
import javax.inject.Singleton
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext
import org.jooq.DSLContext

/** [RememberMeTokens] against the login database. */
@Singleton
class JooqRememberMeTokens
@Inject
constructor(
    private val dsl: DSLContext,
    private val config: LoginServerConfig,
    @param:Named("db") private val dispatcher: CoroutineDispatcher,
) : RememberMeTokens {

  override suspend fun issue(userId: Int): ByteArray =
      withContext(dispatcher) {
        val token = RememberMeTokens.newToken()
        dsl.insertInto(REMEMBER_ME_TOKENS)
            .set(REMEMBER_ME_TOKENS.USER_ID, userId)
            .set(REMEMBER_ME_TOKENS.TOKEN_HASH, RememberMeTokens.fingerprint(token))
            .set(
                REMEMBER_ME_TOKENS.EXPIRES_AT,
                LocalDateTime.now().plus(config.rememberMeMaxAge),
            )
            .execute()
        token
      }

  override suspend fun consume(token: ByteArray): Int? =
      withContext(dispatcher) {
        // Deleting is the lookup, so two clients racing the same token cannot both be answered:
        // whichever delete removes the row is the one that gets the user back.
        dsl.deleteFrom(REMEMBER_ME_TOKENS)
            .where(REMEMBER_ME_TOKENS.TOKEN_HASH.eq(RememberMeTokens.fingerprint(token)))
            .and(REMEMBER_ME_TOKENS.EXPIRES_AT.gt(LocalDateTime.now()))
            .returning(REMEMBER_ME_TOKENS.USER_ID)
            .fetchOne()
            ?.userId
      }

  override suspend fun revokeAll(userId: Int): Int =
      withContext(dispatcher) {
        dsl.deleteFrom(REMEMBER_ME_TOKENS).where(REMEMBER_ME_TOKENS.USER_ID.eq(userId)).execute()
      }
}
