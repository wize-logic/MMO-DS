package de.fiereu.openmmo.server.login.auth

import java.time.Duration
import java.time.Instant
import java.util.concurrent.ConcurrentHashMap
import javax.inject.Inject
import javax.inject.Singleton

/** [RememberMeTokens] with no database behind it, for a test that does not want one. */
@Singleton
class InMemoryRememberMeTokens
@Inject
constructor(
    private val maxAge: Duration = Duration.ofDays(30),
    private val now: () -> Instant = Instant::now,
) : RememberMeTokens {

  private data class Row(val userId: Int, val expiresAt: Instant)

  private val rows = ConcurrentHashMap<String, Row>()

  override suspend fun issue(userId: Int): ByteArray {
    val token = RememberMeTokens.newToken()
    rows[RememberMeTokens.fingerprint(token)] = Row(userId, now().plus(maxAge))
    return token
  }

  override suspend fun consume(token: ByteArray): Int? {
    val row = rows.remove(RememberMeTokens.fingerprint(token)) ?: return null
    return if (row.expiresAt.isAfter(now())) row.userId else null
  }

  override suspend fun revokeAll(userId: Int): Int {
    val gone = rows.entries.filter { it.value.userId == userId }
    gone.forEach { rows.remove(it.key, it.value) }
    return gone.size
  }
}
