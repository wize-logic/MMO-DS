package de.fiereu.openmmo.server.login.auth

/** Replacing the password on an account that already exists. */
object RotatePassword {

  sealed class Outcome {
    data class Changed(val userId: Int, val username: String, val tokensRevoked: Int) : Outcome()

    data class Rejected(val reason: String) : Outcome()
  }

  /** Give [username] a new password and sign it out everywhere. */
  suspend fun rotate(
      users: UserService,
      tokens: RememberMeTokens,
      username: String,
      password: String,
  ): Outcome {
    CreateAccount.validatePassword(password)?.let {
      return Outcome.Rejected(it)
    }
    val name = username.trim().lowercase()
    val id = users.getUserId(name) ?: return Outcome.Rejected("no account called '$name'")
    if (!users.setPassword(id, password)) {
      return Outcome.Rejected("no account called '$name'")
    }
    return Outcome.Changed(id, name, tokens.revokeAll(id))
  }
}
