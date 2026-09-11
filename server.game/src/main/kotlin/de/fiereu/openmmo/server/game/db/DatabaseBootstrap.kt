package de.fiereu.openmmo.server.game.db

import de.fiereu.openmmo.server.game.config.GameServerConfig
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import javax.inject.Singleton
import javax.sql.DataSource
import org.flywaydb.core.Flyway

private val log = KotlinLogging.logger {}

@Singleton
class DatabaseBootstrap
@Inject
constructor(
    private val dataSource: DataSource,
    private val config: GameServerConfig,
) {

  private fun flyway(cleanAllowed: Boolean = false): Flyway {
    val locations = buildList {
      add("classpath:db/migration")
      if (config.db.seedDev) add("classpath:db/dev")
    }
    return Flyway.configure()
        .dataSource(dataSource)
        .locations(*locations.toTypedArray())
        .cleanDisabled(!cleanAllowed)
        .load()
  }

  fun migrate() {
    val result = flyway().migrate()
    log.info { "Applied ${result.migrationsExecuted} database migrations" }
  }

  /**
   * Drops every object in the schema and builds it again from the migrations. Nothing survives, and
   * there is no undo.
   */
  fun reset() {
    val flyway = flyway(cleanAllowed = true)
    flyway.clean()
    log.warn { "Dropped every object in the database" }
    val result = flyway.migrate()
    log.info { "Rebuilt the schema from ${result.migrationsExecuted} migrations" }
  }
}
