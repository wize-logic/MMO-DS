package de.fiereu.openmmo.server.web

import com.zaxxer.hikari.HikariConfig
import com.zaxxer.hikari.HikariDataSource
import de.fiereu.openmmo.server.login.auth.JooqUserStore
import io.github.oshai.kotlinlogging.KotlinLogging
import kotlinx.coroutines.Dispatchers
import org.jooq.SQLDialect
import org.jooq.impl.DSL

private val log = KotlinLogging.logger {}

fun main() {
  val config = WebConfig.fromEnvironment()
  val dataSource =
      HikariDataSource(
          HikariConfig().apply {
            jdbcUrl = config.db.jdbcUrl
            username = config.db.user
            password = config.db.password
            maximumPoolSize = config.db.poolSize
            poolName = "openmmo-web"
          })
  // The login server owns the schema. This process only ever inserts a user, never migrates.
  val users =
      JooqUserStore(
          DSL.using(dataSource, SQLDialect.POSTGRES),
          Dispatchers.IO.limitedParallelism(config.db.poolSize))
  val server =
      WebServer(
          config,
          RegistrationService(
              users,
              limiter = RateLimiter(config.registrationsPerHour),
              attempts = RateLimiter(config.registrationAttemptsPerHour),
              global = RateLimiter(config.registrationsPerHourTotal),
          ),
          StatusProbe(
              config.loginHost,
              config.loginPort,
              config.gameHost,
              config.gamePort,
              config.gameStatusHost,
              config.gameStatusPort,
          ))
  Runtime.getRuntime()
      .addShutdownHook(
          Thread {
            server.stop()
            dataSource.close()
          })
  log.info { "serving accounts from ${config.db.jdbcUrl}" }
  server.start()
}
