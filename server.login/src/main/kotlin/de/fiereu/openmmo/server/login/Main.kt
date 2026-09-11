package de.fiereu.openmmo.server.login

import de.fiereu.openmmo.common.auth.AccountRole
import de.fiereu.openmmo.common.auth.AccountRoles
import de.fiereu.openmmo.server.login.auth.CreateAccount
import de.fiereu.openmmo.server.login.config.ConfigLoader
import de.fiereu.openmmo.server.login.di.DaggerLoginServerComponent
import de.fiereu.openmmo.server.login.di.LoginServerComponent
import kotlin.system.exitProcess
import kotlinx.coroutines.runBlocking

fun main(args: Array<String>) {
  if (args.isNotEmpty()) {
    runCommand(args)
    return
  }
  val config = ConfigLoader.load()
  val component = DaggerLoginServerComponent.factory().create(config)
  component.databaseBootstrap().migrate()
  // Before the port opens, for the same reason: an update feed this server was told to hold clients
  // to and cannot read is a refusal that would otherwise never happen, silently.
  component.clientRevisionFloor()
  runBlocking {
    // After the migrations, so the dev seed's cleanup still matches the rows it names by their old
    // hash, and before the port opens, so nothing serves a table that is still its own credential.
    component.userUpgrade().upgradeLegacyHashes()
    component.adminAccountBootstrap().ensureAdmin()
  }
  component.server().start()
}

private const val CREATE_USER = "create-user"
private const val REVOKE_TOKENS = "revoke-tokens"
private const val SHOW_ROLES = "roles"
private const val GRANT_ROLE = "grant-role"
private const val REVOKE_ROLE = "revoke-role"
private const val RESET_DB = "reset-db"
private const val CONFIRM = "--yes"

/** How many words each verb takes, itself included. A verb that is not here is not a verb. */
private val ARG_COUNT =
    mapOf(
        CREATE_USER to 3,
        REVOKE_TOKENS to 2,
        SHOW_ROLES to 2,
        GRANT_ROLE to 3,
        REVOKE_ROLE to 3,
        RESET_DB to 2,
    )

private fun runCommand(args: Array<String>) {
  val roles = AccountRole.names()
  val usage =
      "usage: server.login [$CREATE_USER <name> <password> | $REVOKE_TOKENS <name> |" +
          " $SHOW_ROLES <name> | $GRANT_ROLE <name> <$roles> | $REVOKE_ROLE <name> <$roles> |" +
          " $RESET_DB $CONFIRM]"
  if (ARG_COUNT[args[0]] != args.size) {
    System.err.println(usage)
    exitProcess(2)
  }
  val config = ConfigLoader.load()
  val component = DaggerLoginServerComponent.factory().create(config)
  if (args[0] == RESET_DB) {
    resetDatabase(component, args[1], config.db.name)
    return
  }
  component.databaseBootstrap().migrate()
  when (args[0]) {
    REVOKE_TOKENS -> revokeTokens(component, args[1])
    SHOW_ROLES -> showRoles(component, args[1])
    GRANT_ROLE -> changeRole(component, args[1], args[2], granting = true)
    REVOKE_ROLE -> changeRole(component, args[1], args[2], granting = false)
    else -> createUser(component, args[1], args[2])
  }
}

private fun createUser(component: LoginServerComponent, username: String, password: String) {
  when (val outcome = runBlocking { CreateAccount.create(component.users(), username, password) }) {
    is CreateAccount.Outcome.Created ->
        println("created account '${outcome.username}' as user ${outcome.userId}")
    is CreateAccount.Outcome.Rejected -> {
      System.err.println("$CREATE_USER: ${outcome.reason}")
      exitProcess(1)
    }
  }
}

/**
 * Sign one account out everywhere. Until the tokens became rows there was no way to do this at all:
 * a signed one was good until it expired, and the column meant to withdraw them was never written.
 */
private fun revokeTokens(component: LoginServerComponent, username: String) {
  val gone = runBlocking {
    val id = component.users().getUserId(username)
    if (id == null) null else component.rememberMeTokens().revokeAll(id)
  }
  if (gone == null) {
    System.err.println("$REVOKE_TOKENS: no account called '$username'")
    exitProcess(1)
  }
  println("revoked $gone remembered login(s) for '$username'")
}

/**
 * Empty this server's database and build the schema again, which is the state a fresh install is
 * in: no accounts, so the next one created is a developer again.
 */
private fun resetDatabase(component: LoginServerComponent, confirm: String, dbName: String) {
  if (confirm != CONFIRM) {
    System.err.println("$RESET_DB deletes every account in '$dbName'. Add $CONFIRM if you mean it.")
    exitProcess(2)
  }
  component.databaseBootstrap().reset()
  println("'$dbName' is empty. The next account created will be a developer.")
}

/**
 * Hand a role out, or take it back. The first account on a server is a developer so that somebody
 * can run the commands at all; every account after it is a plain player until this is used.
 */
private fun changeRole(
    component: LoginServerComponent,
    username: String,
    roleName: String,
    granting: Boolean,
) {
  val role = AccountRole.parse(roleName)
  if (role == null) {
    System.err.println("not a role: '$roleName'. One of ${AccountRole.names()}")
    exitProcess(2)
  }
  val users = component.users()
  val roles = runBlocking {
    val id = users.getUserId(username) ?: return@runBlocking null
    val updated = if (granting) users.rolesOf(id) + role else users.rolesOf(id) - role
    users.setRoles(id, updated)
    updated
  }
  if (roles == null) {
    System.err.println("${if (granting) GRANT_ROLE else REVOKE_ROLE}: no account '$username'")
    exitProcess(1)
  }
  println("'$username' is now: $roles")
}

/** What one account may do. */
private fun showRoles(component: LoginServerComponent, username: String) {
  val users = component.users()
  val roles: AccountRoles? = runBlocking { users.getUserId(username)?.let { users.rolesOf(it) } }
  if (roles == null) {
    System.err.println("$SHOW_ROLES: no account called '$username'")
    exitProcess(1)
  }
  println("'$username': $roles")
}
