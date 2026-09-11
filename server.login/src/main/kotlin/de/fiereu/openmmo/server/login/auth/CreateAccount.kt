package de.fiereu.openmmo.server.login.auth

/**
 * Out-of-band account creation. The login protocol has no registration packet, so a new person gets
 * an account from whoever runs the login server, not from the client.
 */
object CreateAccount {
  const val USERNAME_MAX = 32

  sealed class Outcome {
    data class Created(val userId: Int, val username: String) : Outcome()

    data class Rejected(val reason: String) : Outcome()
  }

  /** Why this is not a name any account here has, or null when it could be one. */
  fun validateUsername(username: String): String? {
    val name = username.trim()
    if (name.isEmpty()) return "username must not be blank"
    if (name.length > USERNAME_MAX) {
      return "username is longer than $USERNAME_MAX characters"
    }
    if (name.any { it.isISOControl() || it.isWhitespace() }) {
      return "username cannot contain spaces or control characters"
    }
    return null
  }

  /**
   * Why this password cannot be used, or null when it can be. Separate from [validate] because
   * changing a password asks this and nothing about the name.
   */
  fun validatePassword(password: String): String? {
    if (password.isBlank()) return "password must not be blank"
    return null
  }

  fun validate(username: String, password: String): String? {
    validateUsername(username)?.let {
      return it
    }
    return validatePassword(password)
  }

  suspend fun create(users: UserService, username: String, password: String): Outcome {
    val name = username.trim()
    validate(name, password)?.let {
      return Outcome.Rejected(it)
    }
    val key = name.lowercase()
    if (users.getUserId(key) != null) {
      return Outcome.Rejected("account '$key' already exists")
    }
    return Outcome.Created(users.addUser(name, password), key)
  }
}
