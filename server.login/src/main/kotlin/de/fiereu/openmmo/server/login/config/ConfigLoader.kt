package de.fiereu.openmmo.server.login.config

import com.github.maltalex.ineter.base.IPAddress
import com.github.maltalex.ineter.base.IPv4Address
import com.github.maltalex.ineter.base.IPv6Address
import com.typesafe.config.Config
import com.typesafe.config.ConfigFactory
import io.github.oshai.kotlinlogging.KotlinLogging

private fun Config.stringOrNull(path: String): String? =
    if (hasPath(path)) getString(path) else null

private fun adminAccount(config: Config): AdminAccountConfig? {
  val username = config.stringOrNull("server.admin.username").orEmpty().trim()
  val password = config.stringOrNull("server.admin.password").orEmpty()
  if (username.isEmpty() && password.isBlank()) return null
  require(username.isNotEmpty()) { "server.admin.password is set without server.admin.username" }
  require(password.isNotBlank()) { "server.admin.username is set without server.admin.password" }
  return AdminAccountConfig(username, password)
}

private val log = KotlinLogging.logger {}

/**
 * The secret this repository ships with. This server signs both token types with it, so anyone who
 * knows it can mint either for any account. It is a default so a checkout runs.
 */
private const val DEV_SESSION_SECRET = "dev-only-secret-do-not-use-in-production"

/** The one way to run with the shipped secret, for a checkout on a desk. */
private const val ALLOW_DEV_SECRET_ENV = "OPENMMO_ALLOW_DEV_SECRET"

private fun devSecretAllowed(): Boolean =
    System.getenv(ALLOW_DEV_SECRET_ENV)?.lowercase() in setOf("1", "true", "yes")

object ConfigLoader {
  /**
   * [allowDevSecret] is the seam the environment variable feeds, and the one a test names directly:
   * a test that wants to read the shipped config is not a deployment facing a network.
   */
  fun load(allowDevSecret: Boolean = devSecretAllowed()): LoginServerConfig {
    val config = ConfigFactory.load()
    val secret = config.getString("server.sessionSecret")
    require(secret.isNotEmpty()) { "server.sessionSecret must not be empty" }
    if (secret == DEV_SESSION_SECRET) {
      require(allowDevSecret) {
        "server.sessionSecret is the one this repository ships with. This server signs both the" +
            " join ticket and the remember me token with it, so anyone holding it can mint either" +
            " for any account. Set OPENMMO_SESSION_SECRET, or $ALLOW_DEV_SECRET_ENV=1 to run" +
            " anyway."
      }
      log.warn {
        "Running with the session secret this repository ships with, because" +
            " $ALLOW_DEV_SECRET_ENV is set. Anyone holding it can mint a ticket for any account."
      }
    }
    val rememberMeMaxAge = config.getDuration("server.rememberMeMaxAge")
    require(!rememberMeMaxAge.isNegative && !rememberMeMaxAge.isZero) {
      "server.rememberMeMaxAge must be positive"
    }
    return LoginServerConfig(
        host = config.getString("server.host"),
        port = config.getInt("server.port"),
        checksumSize = config.getInt("server.checksumSize"),
        rootKeyResource = config.getString("server.rootKeyResource"),
        rootKey = config.stringOrNull("server.rootKey"),
        rootKeyFile = config.stringOrNull("server.rootKeyFile"),
        clientFeed = config.stringOrNull("server.clientFeed")?.trim()?.ifEmpty { null },
        sessionSecret = secret.toByteArray(Charsets.UTF_8),
        rememberMeMaxAge = rememberMeMaxAge,
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
        gameServer =
            GameServerEndpointConfig(
                ipv4Address = IPv4Address.of(config.getString("gameServer.ipv4Address")),
                ipv6Address = IPv6Address.of(config.getString("gameServer.ipv6Address")),
                port = config.getInt("gameServer.port"),
                localAddress = IPAddress.of(config.getString("gameServer.localAddress")),
                localHostname = config.getString("gameServer.localHostname"),
            ),
        admin = adminAccount(config),
    )
  }
}
