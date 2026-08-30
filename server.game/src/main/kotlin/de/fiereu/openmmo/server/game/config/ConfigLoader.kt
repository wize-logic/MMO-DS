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

/**
 * The one way to run with the shipped secret, for a checkout on a desk.
 *
 * Warning about it was not enough: a warning is a line that scrolls past at start, and the failure
 * it warns about is silent. So the default is a refusal, and development says so out loud instead.
 * start-server.sh sets this when no real secret is in the environment.
 */
private const val ALLOW_DEV_SECRET_ENV = "OPENMMO_ALLOW_DEV_SECRET"

private fun devSecretAllowed(): Boolean =
    System.getenv(ALLOW_DEV_SECRET_ENV)?.lowercase() in setOf("1", "true", "yes")

private fun Config.stringOrNull(path: String): String? =
    if (hasPath(path)) getString(path) else null

object ConfigLoader {
  /**
   * [allowDevSecret] is the seam the environment variable feeds, and the one a test names directly:
   * a test that reads the shipped config is not a deployment facing a network.
   */
  fun load(allowDevSecret: Boolean = devSecretAllowed()): GameServerConfig {
    val config = ConfigFactory.load()
    val secret = config.getString("server.sessionSecret")
    require(secret.isNotEmpty()) { "server.sessionSecret must not be empty" }
    if (secret == DEV_SESSION_SECRET) {
      require(allowDevSecret) {
        "server.sessionSecret is the one this repository ships with, so anyone holding it can" +
            " join as any account, with any role. Set OPENMMO_SESSION_SECRET, or" +
            " $ALLOW_DEV_SECRET_ENV=1 to run anyway."
      }
      log.warn {
        "Running with the session secret this repository ships with, because" +
            " $ALLOW_DEV_SECRET_ENV is set. Anyone holding it can join as any account."
      }
    }
    val seedDev = config.getBoolean("db.seedDev")
    val tokenMaxAge = config.getDuration("server.sessionTokenMaxAge")
    require(!tokenMaxAge.isNegative && !tokenMaxAge.isZero) {
      "server.sessionTokenMaxAge must be positive"
    }
    return GameServerConfig(
        host = config.getString("server.host"),
        port = config.getInt("server.port"),
        statusHost = config.getString("server.statusHost"),
        statusPort = config.getInt("server.statusPort"),
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
                seedDev = seedDev,
            ),
    )
  }
}
