package de.fiereu.openmmo.server.game

import de.fiereu.openmmo.server.game.config.ConfigLoader
import de.fiereu.openmmo.server.game.di.DaggerGameServerComponent
import io.github.oshai.kotlinlogging.KotlinLogging
import kotlin.system.exitProcess
import kotlin.time.Duration.Companion.seconds
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout

private val log = KotlinLogging.logger {}

private val SHUTDOWN_FLUSH_TIMEOUT = 10.seconds

private const val RESET_DB = "reset-db"
private const val CONFIRM = "--yes"

fun main(args: Array<String>) {
  if (args.isNotEmpty()) {
    runCommand(args)
    return
  }
  val config = ConfigLoader.load()
  val component = DaggerGameServerComponent.factory().create(config)
  component.databaseBootstrap().migrate()
  val characterStore = component.characterStore()
  characterStore.startPeriodicFlush()
  Runtime.getRuntime()
      .addShutdownHook(
          Thread {
            runCatching {
                  runBlocking { withTimeout(SHUTDOWN_FLUSH_TIMEOUT) { characterStore.shutdown() } }
                }
                .onFailure { log.warn(it) { "Final character flush did not complete" } }
          })
  component.server().start()
}

/**
 * Empty this server's database and build the schema again, leaving it as a fresh install.
 *
 * Reset the login database in the same breath: a user id ties the two together, so a game database
 * kept across a login reset leaves the next account inheriting a stranger's characters. reset-db.sh
 * does both. Nothing may be connected, or the next flush undoes it.
 */
private fun runCommand(args: Array<String>) {
  val usage = "usage: server.game [$RESET_DB $CONFIRM]"
  if (args[0] != RESET_DB || args.size != 2) {
    System.err.println(usage)
    exitProcess(2)
  }
  val config = ConfigLoader.load()
  if (args[1] != CONFIRM) {
    System.err.println(
        "$RESET_DB deletes every character in '${config.db.name}'. Add $CONFIRM if you mean it.")
    exitProcess(2)
  }
  DaggerGameServerComponent.factory().create(config).databaseBootstrap().reset()
  println("'${config.db.name}' is empty.")
}
