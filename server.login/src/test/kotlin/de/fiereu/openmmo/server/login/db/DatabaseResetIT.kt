package de.fiereu.openmmo.server.login.db

import com.zaxxer.hikari.HikariDataSource
import de.fiereu.openmmo.common.auth.AccountRole
import de.fiereu.openmmo.common.test.DockerAvailable
import de.fiereu.openmmo.server.login.auth.JooqUserStore
import de.fiereu.openmmo.server.login.config.LoginServerConfig
import io.kotest.core.annotation.EnabledIf
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import kotlinx.coroutines.Dispatchers
import org.jooq.impl.DSL
import org.testcontainers.containers.PostgreSQLContainer

/**
 * The clean slate behind `reset-db.sh`. It is the one operation here that cannot be undone, so what
 * matters is that it leaves a database a server can start against rather than an empty one.
 */
@EnabledIf(DockerAvailable::class)
class DatabaseResetIT :
    FunSpec({
      val container = PostgreSQLContainer<Nothing>("postgres:18")
      lateinit var bootstrap: DatabaseBootstrap
      lateinit var store: JooqUserStore

      beforeSpec {
        container.start()
        val dataSource =
            HikariDataSource().apply {
              jdbcUrl = container.jdbcUrl
              username = container.username
              password = container.password
              maximumPoolSize = 2
            }
        bootstrap =
            DatabaseBootstrap(
                dataSource,
                LoginServerConfig(
                    host = "127.0.0.1",
                    port = 0,
                    checksumSize = 16,
                    rootKeyResource = "login.private.pem",
                    sessionSecret = ByteArray(32) { 1 },
                ),
            )
        bootstrap.migrate()
        store =
            JooqUserStore(
                DSL.using(container.jdbcUrl, container.username, container.password),
                Dispatchers.IO,
            )
      }

      afterSpec { container.stop() }

      test("reset empties the accounts and leaves a schema that still works") {
        store.addUser("Cynthia", "garchomp")
        store.addUser("Barry", "toosLow")
        store.getUserId("cynthia") shouldBe 1

        bootstrap.reset()

        store.getUserId("cynthia") shouldBe null
        store.getUserId("barry") shouldBe null

        // The point of rebuilding rather than deleting rows: the table is there, the identity
        // counter has gone back, and the account made next is the first one again.
        val first = store.addUser("Cynthia", "garchomp")
        first shouldBe 1
        store.rolesOf(first).has(AccountRole.DEVELOPER) shouldBe true
      }
    })
