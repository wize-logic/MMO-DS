package de.fiereu.openmmo.server.web

import de.fiereu.openmmo.server.login.auth.CreateAccount
import de.fiereu.openmmo.server.login.auth.UserService
import io.github.oshai.kotlinlogging.KotlinLogging
import org.jooq.exception.DataAccessException

private val log = KotlinLogging.logger {}

/**
 * Account creation for the website. The login protocol has no registration packet, so this writes
 * the same row `server.login create-user` would, through the same [CreateAccount] rules.
 */
class RegistrationService(
    private val users: UserService,
    /** Accounts one address may create in the window. */
    private val limiter: RateLimiter,
    /**
     * Requests one address may make in the window, whether or not they were accounts. Without
     * it a caller who never sends anything valid is never counted at all, and posting rubbish
     * forever is free.
     */
    private val attempts: RateLimiter = RateLimiter(ATTEMPTS_PER_ADDRESS),
    /**
     * Accounts everybody together may create in the window. The per-address limit is a limit on one
     * machine; this is the one that still holds when the requests come from a thousand of them.
     */
    private val global: RateLimiter = RateLimiter(CREATIONS_TOTAL),
) {
  sealed interface Outcome {
    data class Created(val username: String) : Outcome

    /** Something the visitor can fix and retry. */
    data class Rejected(val reason: String) : Outcome

    data object Taken : Outcome

    data object RateLimited : Outcome

    data object Failed : Outcome
  }

  suspend fun register(
      username: String,
      password: String,
      confirmation: String,
      remoteAddress: String,
  ): Outcome {
    // Counted before the form is even looked at, so a caller cannot buy attempts by sending
    // something that fails validation.
    if (!attempts.allow(remoteAddress)) return Outcome.RateLimited
    policy(username, password, confirmation)?.let {
      return Outcome.Rejected(it)
    }
    if (!global.allow(GLOBAL_KEY)) return Outcome.RateLimited
    if (!limiter.allow(remoteAddress)) return Outcome.RateLimited
    return try {
      when (val outcome = CreateAccount.create(users, username, password)) {
        is CreateAccount.Outcome.Created -> Outcome.Created(outcome.username)
        // The only rejection left is the name already existing; the rest was checked above.
        is CreateAccount.Outcome.Rejected -> Outcome.Taken
      }
    } catch (e: DataAccessException) {
      // Two requests for the same free name race past the existence check; the unique index on
      // users.username is what actually settles it.
      log.warn(e) { "registration for '${username.lowercase()}' failed in the database" }
      if (e.message?.contains("uq_users_username") == true) Outcome.Taken else Outcome.Failed
    }
  }

  /** The website's own rules, on top of what the login server enforces. Null when acceptable. */
  private fun policy(username: String, password: String, confirmation: String): String? {
    CreateAccount.validate(username, password)?.let {
      return it
    }
    val name = username.trim()
    // Stricter than the login server, which takes any name without spaces in it. A public form
    // that anyone can submit is not the place to allow markup or lookalike characters.
    if (name.length < NAME_MIN) return "account name must be at least $NAME_MIN characters"
    if (!NAME.matches(name)) {
      return "account name may only use letters, digits, underscore and hyphen"
    }
    if (password != confirmation) return "the two passwords do not match"
    if (password.length < PASSWORD_MIN) return "password must be at least $PASSWORD_MIN characters"
    if (password.length > PASSWORD_MAX) return "password is longer than $PASSWORD_MAX characters"
    if (password.contains(name, ignoreCase = true)) {
      return "password must not contain the account name"
    }
    return null
  }

  companion object {
    const val NAME_MIN = 3
    const val PASSWORD_MIN = 8
    const val PASSWORD_MAX = 128
    const val ATTEMPTS_PER_ADDRESS = 20
    const val CREATIONS_TOTAL = 120
    private const val GLOBAL_KEY = "all"
    private val NAME = Regex("[A-Za-z0-9_-]+")
  }
}
