package de.fiereu.openmmo.server.web

import de.fiereu.openmmo.server.login.config.DbConfig
import java.nio.file.Files
import java.nio.file.Path

/**
 * Everything the website needs, taken from the same environment variables the servers already read.
 * There is no config file: the site is one process behind apache and the login database is the only
 * state it touches.
 */
data class WebConfig(
    val host: String = LOOPBACK,
    val port: Int = 8088,
    val db: DbConfig = DbConfig(),
    /** Serves the static site itself when set. Unset in production, where apache serves it. */
    val staticRoot: Path? = null,
    /** Where the status panel probes the two servers. */
    val loginHost: String = LOOPBACK,
    val loginPort: Int = 2106,
    val gameHost: String = LOOPBACK,
    val gamePort: Int = 7777,
    /**
     * Where the game server's online-count endpoint answers. Loopback on both ends by default: the
     * endpoint binds it and this is the one caller, on the same machine.
     */
    val gameStatusHost: String = LOOPBACK,
    val gameStatusPort: Int = 7779,
    val registrationsPerHour: Int = 5,
    /** Registration requests one address may make an hour, valid or not. */
    val registrationAttemptsPerHour: Int = RegistrationService.ATTEMPTS_PER_ADDRESS,
    /** Accounts everybody together may create an hour. */
    val registrationsPerHourTotal: Int = RegistrationService.CREATIONS_TOTAL,
    /** Registrations allowed in the database at once; the rest are turned away straight away. */
    val concurrentRegistrations: Int = 4,
) {
  companion object {
    /** What a server that binds every interface answers on, and so what to probe by default. */
    const val LOOPBACK = "127.0.0.1"

    fun fromEnvironment(env: (String) -> String? = System::getenv): WebConfig {
      fun int(key: String, fallback: Int) =
          env(key)?.trim()?.takeIf(String::isNotEmpty)?.toInt() ?: fallback
      fun string(key: String, fallback: String) =
          env(key)?.trim()?.takeIf(String::isNotEmpty) ?: fallback

      val root = env("OPENMMO_WEB_ROOT")?.trim()?.takeIf(String::isNotEmpty)?.let(Path::of)
      require(root == null || Files.isDirectory(root)) {
        "OPENMMO_WEB_ROOT is not a directory: $root"
      }

      return WebConfig(
          host = string("OPENMMO_WEB_HOST", LOOPBACK),
          port = int("OPENMMO_WEB_PORT", 8088),
          db =
              DbConfig(
                  host = string("LOGIN_DB_HOST", "localhost"),
                  port = int("LOGIN_DB_PORT", 20011),
                  name = string("LOGIN_DB_NAME", "openmmo_login_db"),
                  user = string("LOGIN_DB_USER", "openmmo_login_user"),
                  password = string("LOGIN_DB_PASSWORD", "changeMe!"),
                  poolSize = int("LOGIN_DB_POOL_SIZE", 4),
              ),
          staticRoot = root,
          loginHost = string("LOGIN_HOST", LOOPBACK),
          loginPort = int("LOGIN_PORT", 2106),
          gameHost = string("GAME_HOST", LOOPBACK),
          gamePort = int("GAME_SERVER_PORT", 7777),
          gameStatusHost = string("GAME_STATUS_HOST", LOOPBACK),
          gameStatusPort = int("GAME_STATUS_PORT", 7779),
          registrationsPerHour = int("OPENMMO_WEB_REGISTRATIONS_PER_HOUR", 5),
          registrationAttemptsPerHour =
              int(
                  "OPENMMO_WEB_REGISTRATION_ATTEMPTS_PER_HOUR",
                  RegistrationService.ATTEMPTS_PER_ADDRESS),
          registrationsPerHourTotal =
              int("OPENMMO_WEB_REGISTRATIONS_PER_HOUR_TOTAL", RegistrationService.CREATIONS_TOTAL),
          concurrentRegistrations = int("OPENMMO_WEB_CONCURRENT_REGISTRATIONS", 4),
      )
    }
  }
}
