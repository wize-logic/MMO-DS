package de.fiereu.openmmo.server.login.auth

import de.fiereu.openmmo.server.login.config.LoginServerConfig
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton

private val log = KotlinLogging.logger {}

@Singleton
class AdminAccountBootstrap
@Inject
constructor(
    private val users: UserService,
    private val config: LoginServerConfig,
) {

  /**
   * Creates the configured account on a database that holds no user at all. It never touches an
   * existing one, so a changed password in the config does not reset the account it created.
   */
  suspend fun ensureAdmin() {
    val admin = config.admin
    if (admin == null) {
      // The web server takes registrations, so on an empty database the next person through any
      // door is the first account and gets the developer role. That is the point of it, but an
      // operator who has not made their account yet should hear about it before a stranger does.
      if (!users.hasAnyUser()) {
        log.warn {
          "No account exists yet, so the next one created anywhere becomes a" +
              " ${UserService.FIRST_ACCOUNT_ROLES}. Make yours before opening the web server," +
              " with server.login create-user."
        }
      }
      return
    }
    if (users.hasAnyUser()) return
    val id = users.addUser(admin.username, admin.password)
    val roles = users.rolesOf(id)
    log.info { "Created '${admin.username}' as the first account, user $id ($roles)" }
  }
}
