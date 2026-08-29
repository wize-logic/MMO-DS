package de.fiereu.openmmo.server.login

import de.fiereu.openmmo.server.login.auth.CreateAccount
import de.fiereu.openmmo.server.login.config.ConfigLoader
import de.fiereu.openmmo.server.login.di.DaggerLoginServerComponent
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
  runBlocking { component.adminAccountBootstrap().ensureAdmin() }
  component.server().start()
}

private fun runCommand(args: Array<String>) {
  if (args[0] != "create-user" || args.size != 3) {
    System.err.println("usage: server.login [create-user <name> <password>]")
    exitProcess(2)
  }
  val config = ConfigLoader.load()
  val component = DaggerLoginServerComponent.factory().create(config)
  component.databaseBootstrap().migrate()
  when (val outcome = runBlocking { CreateAccount.create(component.users(), args[1], args[2]) }) {
    is CreateAccount.Outcome.Created ->
        println("created account '${outcome.username}' as user ${outcome.userId}")
    is CreateAccount.Outcome.Rejected -> {
      System.err.println("create-user: ${outcome.reason}")
      exitProcess(1)
    }
  }
}
