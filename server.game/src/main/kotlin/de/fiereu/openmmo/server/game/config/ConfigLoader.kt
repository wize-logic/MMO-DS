package de.fiereu.openmmo.server.game.config

import com.typesafe.config.Config
import com.typesafe.config.ConfigFactory
import io.github.oshai.kotlinlogging.KotlinLogging

private val log = KotlinLogging.logger {}

/**
 * The secret both servers ship with. The login server signs a join ticket with it and the game
 * server verifies with it, so anyone who knows it can mint a ticket for any account. It is a
 * default so a checkout runs, and it sits in a file anyone can read.
 */
private const val DEV_SESSION_SECRET = "dev-only-secret-do-not-use-in-production"

private fun Config.stringOrNull(path: String): String? =
    if (hasPath(path)) getString(path) else null

object ConfigLoader {
  fun load(): GameServerConfig {
    val config = ConfigFactory.load()
    val secret = config.getString("server.sessionSecret")
    require(secret.isNotEmpty()) { "server.sessionSecret must not be empty" }
    if (secret == DEV_SESSION_SECRET) {
      log.warn {
        "server.sessionSecret is the one this repository ships with, so anyone holding it can" +
            " join as any account. Set OPENMMO_SESSION_SECRET before this server faces a network."
      }
    }
    val tokenMaxAge = config.getDuration("server.sessionTokenMaxAge")
    require(!tokenMaxAge.isNegative && !tokenMaxAge.isZero) {
      "server.sessionTokenMaxAge must be positive"
    }
    return GameServerConfig(
        host = config.getString("server.host"),
        port = config.getInt("server.port"),
        checksumSize = config.getInt("server.checksumSize"),
        rootKeyResource = config.getString("server.rootKeyResource"),
        rootKey = config.stringOrNull("server.rootKey"),
        rootKeyFile = config.stringOrNull("server.rootKeyFile"),
        sessionSecret = secret.toByteArray(Charsets.UTF_8),
        sessionTokenMaxAge = tokenMaxAge,
        db =
            DbConfig(
                host = config.getString("db.host"),
                port = config.getInt("db.port"),
                name = config.getString("db.name"),
                user = config.getString("db.user"),
                password = config.getString("db.password"),
                poolSize = config.getInt("db.poolSize"),
                seedDev = config.getBoolean("db.seedDev"),
            ),
    )
  }
}
