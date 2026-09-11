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
  // Before the port opens: a guild is asked for by character id from paths that cannot wait on a
  // database, so every one of them is in memory by the time the first player can join.
  val guildStore = component.guildStore()
  runBlocking { guildStore.load() }
  log.info { "Read ${guildStore.count()} guilds" }
  // Asked for by name before the port opens: a singleton is built on first use, and first use of
  // the replay worker would otherwise be the first player bringing a save online. An operator who
  // configured VERIFY_* badly hears about it here, in the log, at start.
  log.info {
    val revisions = component.replayRunner().revisions()
    if (revisions.isEmpty()) "Offline play will not be replayed: no worker is configured"
    else "Offline play is replayed for client revisions ${revisions.sorted()}"
  }
  component.replayWorker().start()
  Runtime.getRuntime()
      .addShutdownHook(
          Thread {
            runCatching {
                  runBlocking { withTimeout(SHUTDOWN_FLUSH_TIMEOUT) { characterStore.shutdown() } }
                }
                .onFailure { log.warn(it) { "Final character flush did not complete" } }
          })
  StatusEndpoint(config, component.sessionRegistry()).start()
  component.server().start()
}

/**
 * Empty this server's database and build the schema again, leaving it as a fresh install: no
 * characters, no guilds, nothing on the shelf.
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
