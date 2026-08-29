package de.fiereu.openmmo.server.login.auth

import de.fiereu.openmmo.common.enums.LoginState
import de.fiereu.openmmo.common.test.DockerAvailable
import io.kotest.core.annotation.EnabledIf
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import kotlinx.coroutines.Dispatchers
import org.flywaydb.core.Flyway
import org.jooq.impl.DSL
import org.testcontainers.containers.PostgreSQLContainer

@EnabledIf(DockerAvailable::class)
class JooqUserStoreIT :
    FunSpec({
      val container = PostgreSQLContainer<Nothing>("postgres:18")
      lateinit var store: JooqUserStore

      beforeSpec {
        container.start()
        Flyway.configure()
            .dataSource(container.jdbcUrl, container.username, container.password)
            .locations("classpath:db/migration", "classpath:db/dev")
            .load()
            .migrate()
        val dsl = DSL.using(container.jdbcUrl, container.username, container.password)
        store = JooqUserStore(dsl, Dispatchers.IO)
      }

      afterSpec { container.stop() }

      test("the dev migration seeds no account") {
        store.getUserId("admin") shouldBe null
        store.getUserId("test") shouldBe null
      }

      test("authenticate succeeds with the hashed password") {
        val id = store.addUser("Bruno", "rocks")
        val result = store.authenticate("bruno", sha1Hex("rocks"))
        result.state shouldBe LoginState.AUTHED
        result.userId shouldBe id
      }

      test("authenticate is case-insensitive on username") {
        store.addUser("Casey", "pw")
        store.authenticate("CASEY", sha1Hex("pw")).state shouldBe LoginState.AUTHED
      }

      test("authenticate fails for a wrong password") {
        store.addUser("Dawn", "right")
        store.authenticate("dawn", sha1Hex("nope")).state shouldBe LoginState.INVALID_PASSWORD
      }

      test("authenticate fails for an unknown user") {
        store.authenticate("nobody", sha1Hex("pw")).state shouldBe LoginState.INVALID_PASSWORD
      }

      test("addUser returns the generated id and getUserId finds it") {
        val id = store.addUser("Alice", "pw")
        store.getUserId("alice") shouldBe id
      }

      test("CreateAccount inserts a user who can then authenticate") {
        val outcome = CreateAccount.create(store, "Zoe", "secret")
        val created = outcome as CreateAccount.Outcome.Created
        created.username shouldBe "zoe"
        store.authenticate("zoe", sha1Hex("secret")).state shouldBe LoginState.AUTHED
        store.authenticate("zoe", sha1Hex("secret")).userId shouldBe created.userId
      }

      test("CreateAccount refuses a name someone already registered") {
        CreateAccount.create(store, "Yara", "first")
        CreateAccount.create(store, "yara", "whatever") shouldBe
            CreateAccount.Outcome.Rejected("account 'yara' already exists")
      }
    })
