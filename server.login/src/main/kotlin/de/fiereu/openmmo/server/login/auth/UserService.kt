package de.fiereu.openmmo.server.login.auth

import de.fiereu.openmmo.common.auth.AccountRole
import de.fiereu.openmmo.common.auth.AccountRoles
import de.fiereu.openmmo.common.enums.LoginState
import de.fiereu.openmmo.common.utils.toHex
import java.security.MessageDigest
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicInteger
import javax.inject.Inject
import javax.inject.Singleton

interface UserService {
  data class AuthResult(val state: LoginState, val userId: Int? = null)

  /** Identity a remember me token resolves to. */
  data class TokenUser(val id: Int, val username: String, val displayName: String)

  suspend fun authenticate(username: String, password: String): AuthResult

  suspend fun getUserId(username: String): Int?

  suspend fun findForToken(userId: Int): TokenUser?

  suspend fun hasAnyUser(): Boolean

  /** Takes the plain password and hashes it the way the client would before sending. */
  suspend fun addUser(username: String, password: String): Int

  /**
   * Replaces one account's credential, taking the plain password like [addUser]. False when there
   * is no account with that id.
   */
  suspend fun setPassword(userId: Int, password: String): Boolean

  /** What this account may do. Empty for one that does not exist, same as for a plain player. */
  suspend fun rolesOf(userId: Int): AccountRoles

  /** Writes the whole set. False when there is no such account. */
  suspend fun setRoles(userId: Int, roles: AccountRoles): Boolean

  companion object {
    /** What the first account on a server is given. */
    val FIRST_ACCOUNT_ROLES = AccountRoles.of(AccountRole.DEVELOPER)
  }
}

@Suppress("kotlin:S4790")
internal fun sha1Hex(value: String): String =
    MessageDigest.getInstance("SHA-1").digest(value.toByteArray()).toHex()

@Singleton
class InMemoryUserStore @Inject constructor() : UserService {

  private data class UserInfo(
      val id: Int,
      val passwordHash: String,
      val username: String,
      val roles: AccountRoles = AccountRoles.NONE,
  )

  private val users = ConcurrentHashMap<String, UserInfo>()
  private val nextId = AtomicInteger(1)

  override suspend fun hasAnyUser(): Boolean = users.isNotEmpty()

  override suspend fun addUser(username: String, password: String): Int {
    val id = nextId.getAndIncrement()
    val roles = if (users.isEmpty()) UserService.FIRST_ACCOUNT_ROLES else AccountRoles.NONE
    users[username.lowercase()] =
        UserInfo(id, PasswordHash.hash(sha1Hex(password)), username, roles = roles)
    return id
  }

  override suspend fun rolesOf(userId: Int): AccountRoles =
      users.values.firstOrNull { it.id == userId }?.roles ?: AccountRoles.NONE

  override suspend fun setRoles(userId: Int, roles: AccountRoles): Boolean {
    val entry = users.entries.firstOrNull { it.value.id == userId } ?: return false
    users[entry.key] = entry.value.copy(roles = roles)
    return true
  }

  override suspend fun setPassword(userId: Int, password: String): Boolean {
    val entry = users.entries.firstOrNull { it.value.id == userId } ?: return false
    users[entry.key] = entry.value.copy(passwordHash = PasswordHash.hash(sha1Hex(password)))
    return true
  }

  override suspend fun authenticate(username: String, password: String): UserService.AuthResult {
    val user =
        users[username.lowercase()] ?: return UserService.AuthResult(LoginState.INVALID_PASSWORD)
    if (!PasswordHash.verify(user.passwordHash, password)) {
      return UserService.AuthResult(LoginState.INVALID_PASSWORD)
    }
    return UserService.AuthResult(LoginState.AUTHED, user.id)
  }

  override suspend fun getUserId(username: String): Int? = users[username.lowercase()]?.id

  override suspend fun findForToken(userId: Int): UserService.TokenUser? =
      users.values
          .firstOrNull { it.id == userId }
          ?.let { UserService.TokenUser(it.id, it.username.lowercase(), it.username) }
}

/** Whether the stored credential matches the one that arrived, compared in constant time. */
internal fun samePassword(stored: String?, offered: String): Boolean =
    stored != null &&
        MessageDigest.isEqual(
            stored.toByteArray(Charsets.UTF_8), offered.toByteArray(Charsets.UTF_8))
