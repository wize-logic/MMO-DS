package de.fiereu.openmmo.server.login

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
  runBlocking {
    // After the migrations, so the dev seed's cleanup still matches the rows it names by their old
    // hash, and before the port opens, so nothing serves a table that is still its own credential.
    component.userUpgrade().upgradeLegacyHashes()
    component.adminAccountBootstrap().ensureAdmin()
  }
  component.server().start()
}

private fun runCommand(args: Array<String>) {
  val usage = "usage: server.login [create-user <name> <password> | revoke-tokens <name>]"
  if (args[0] !in setOf("create-user", "revoke-tokens")) {
    System.err.println(usage)
    exitProcess(2)
  }
  if ((args[0] == "create-user" && args.size != 3) ||
      (args[0] == "revoke-tokens" && args.size != 2)) {
    System.err.println(usage)
    exitProcess(2)
  }
  val config = ConfigLoader.load()
  val component = DaggerLoginServerComponent.factory().create(config)
  component.databaseBootstrap().migrate()
  if (args[0] == "revoke-tokens") {
    revokeTokens(component, args[1])
    return
  }
  when (val outcome = runBlocking { CreateAccount.create(component.users(), args[1], args[2]) }) {
    is CreateAccount.Outcome.Created ->
        println("created account '${outcome.username}' as user ${outcome.userId}")
    is CreateAccount.Outcome.Rejected -> {
      System.err.println("create-user: ${outcome.reason}")
      exitProcess(1)
    }
  }
}

/** Sign one account out everywhere, which until the tokens became rows could not be done at all. */
private fun revokeTokens(component: LoginServerComponent, username: String) {
  val gone = runBlocking {
    val id = component.users().getUserId(username)
    if (id == null) null else component.rememberMeTokens().revokeAll(id)
  }
  if (gone == null) {
    System.err.println("revoke-tokens: no account called '$username'")
    exitProcess(1)
  }
  println("revoked $gone remembered login(s) for '$username'")
}
