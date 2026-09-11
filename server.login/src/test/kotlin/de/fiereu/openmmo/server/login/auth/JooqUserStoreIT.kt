package de.fiereu.openmmo.server.login.auth

import de.fiereu.openmmo.common.auth.AccountRole
import de.fiereu.openmmo.common.auth.AccountRoles
import de.fiereu.openmmo.common.enums.LoginState
import de.fiereu.openmmo.common.test.DockerAvailable
import de.fiereu.openmmo.db.login.tables.references.USERS
import io.kotest.core.annotation.EnabledIf
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.ints.shouldBeGreaterThan
import io.kotest.matchers.shouldBe
import io.kotest.matchers.shouldNotBe
import kotlinx.coroutines.Dispatchers
import org.flywaydb.core.Flyway
import org.jooq.impl.DSL
import org.testcontainers.containers.PostgreSQLContainer

@EnabledIf(DockerAvailable::class)
class JooqUserStoreIT :
    FunSpec({
      val container = PostgreSQLContainer<Nothing>("postgres:18")
      lateinit var store: JooqUserStore
      lateinit var db: org.jooq.DSLContext
      val config =
          de.fiereu.openmmo.server.login.config.LoginServerConfig(
              host = "127.0.0.1",
              port = 0,
              checksumSize = 16,
              rootKeyResource = "login.private.pem",
              sessionSecret = ByteArray(32) { 1 },
          )

      /** What is actually in the column, which is the thing these tests are about. */
      fun storedHashOf(username: String): String =
          db.select(USERS.PASSWORD_HASH)
              .from(USERS)
              .where(USERS.USERNAME.eq(username))
              .fetchSingle(USERS.PASSWORD_HASH)!!

      /** Put a row back into the shape this server used to write, to test the way out of it. */
      fun setStoredHash(username: String, value: String) {
        db.update(USERS)
            .set(USERS.PASSWORD_HASH, value)
            .where(USERS.USERNAME.eq(username))
            .execute()
      }

      beforeSpec {
        container.start()
        Flyway.configure()
            .dataSource(container.jdbcUrl, container.username, container.password)
            .locations("classpath:db/migration", "classpath:db/dev")
            .load()
            .migrate()
        db = DSL.using(container.jdbcUrl, container.username, container.password)
        store = JooqUserStore(db, Dispatchers.IO)
      }

      afterSpec { container.stop() }

      test("the dev migration seeds no account") {
        store.getUserId("admin") shouldBe null
        store.getUserId("test") shouldBe null
      }

      /**
       * Runs before anything else adds a row, which is the whole point: somebody has to be able to
       * work a server that has nobody on it, and the answer used to be two accounts whose passwords
       * were printed in this repository.
       */
      test("the first account on the server is a developer and the next one is not") {
        val first = store.addUser("Aaron", "first")
        val second = store.addUser("Amber", "second")

        store.rolesOf(first).has(AccountRole.DEVELOPER) shouldBe true
        store.rolesOf(second) shouldBe AccountRoles.NONE
      }

      test("a role is handed out and taken back on a live row") {
        val id = store.addUser("Roxanne", "pw")

        store.setRoles(id, AccountRoles.of(AccountRole.MODERATOR)) shouldBe true
        store.rolesOf(id).has(AccountRole.MODERATOR) shouldBe true
        store.rolesOf(id).has(AccountRole.ADMIN) shouldBe false

        store.setRoles(id, AccountRoles.NONE) shouldBe true
        store.rolesOf(id) shouldBe AccountRoles.NONE
      }

      test("writing roles for an account that is not there says so") {
        store.setRoles(999_999, AccountRoles.of(AccountRole.ADMIN)) shouldBe false
        store.rolesOf(999_999) shouldBe AccountRoles.NONE
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

      /**
       * The point of the whole change. The client sends a SHA-1 and this used to be what the row
       * held, so the column was the credential and reading the table was logging in as everybody.
       */
      test("the stored row is not the value the client sends") {
        store.addUser("Erika", "grass")
        val stored = storedHashOf("erika")

        stored shouldNotBe sha1Hex("grass")
        stored.startsWith("pbkdf2-sha256$") shouldBe true
        store.authenticate("erika", sha1Hex("grass")).state shouldBe LoginState.AUTHED
      }

      test("two accounts with the same password do not share a row") {
        store.addUser("Falkner", "same")
        store.addUser("Bugsy", "same")

        storedHashOf("falkner") shouldNotBe storedHashOf("bugsy")
      }

      /** An account written before this still works, and is rewritten the moment it is used. */
      test("a row left in the old shape authenticates once and is upgraded") {
        val id = store.addUser("Giovanni", "rockets")
        setStoredHash("giovanni", sha1Hex("rockets"))

        val result = store.authenticate("giovanni", sha1Hex("rockets"))

        result.state shouldBe LoginState.AUTHED
        result.userId shouldBe id
        storedHashOf("giovanni") shouldNotBe sha1Hex("rockets")
      }

      /** The rows nobody signs into are the ones a dump is read from. */
      test("the startup sweep rewrites accounts nobody has logged into") {
        store.addUser("Sabrina", "psychic")
        setStoredHash("sabrina", sha1Hex("psychic"))

        store.upgradeLegacyHashes() shouldBeGreaterThan 0

        storedHashOf("sabrina") shouldNotBe sha1Hex("psychic")
        store.authenticate("sabrina", sha1Hex("psychic")).state shouldBe LoginState.AUTHED
      }

      /**
       * The remediation path. Until this existed a password somebody else knew could only be dealt
       * with by deleting the account, so an exposed credential outlived every other fix.
       */
      test("a rotated password authenticates and the old one stops") {
        val id = store.addUser("Whitney", "miltank")

        store.setPassword(id, "clefairy") shouldBe true

        store.authenticate("whitney", sha1Hex("clefairy")).state shouldBe LoginState.AUTHED
        store.authenticate("whitney", sha1Hex("miltank")).state shouldBe LoginState.INVALID_PASSWORD
      }

      test("a rotated password is stored the way a new account's is") {
        val id = store.addUser("Morty", "ghosts")

        store.setPassword(id, "gengar")

        val stored = storedHashOf("morty")
        stored shouldNotBe sha1Hex("gengar")
        stored.startsWith("pbkdf2-sha256$") shouldBe true
      }

      /** The case that matters most: the row that is still the wire value is rotated out of it. */
      test("a row left in the old shape can be rotated straight to a salted one") {
        val id = store.addUser("Pryce", "ice")
        setStoredHash("pryce", sha1Hex("ice"))

        store.setPassword(id, "seel") shouldBe true

        storedHashOf("pryce").startsWith("pbkdf2-sha256$") shouldBe true
        store.authenticate("pryce", sha1Hex("ice")).state shouldBe LoginState.INVALID_PASSWORD
        store.authenticate("pryce", sha1Hex("seel")).state shouldBe LoginState.AUTHED
      }

      test("setPassword reports an account it cannot find") {
        store.setPassword(404, "pw") shouldBe false
      }

      /**
       * A remembered login is proof of the password that has just been replaced, so a rotation that
       * left one alive would leave the account reachable by whoever made the rotation necessary.
       */
      test("rotating a password revokes the remembered logins with it") {
        val tokens = JooqRememberMeTokens(db, config, Dispatchers.IO)
        val id = store.addUser("Clair", "dragons")
        val kept = tokens.issue(id)

        val outcome = RotatePassword.rotate(store, tokens, "CLAIR", "kingdra")

        outcome shouldBe RotatePassword.Outcome.Changed(id, "clair", 1)
        tokens.consume(kept) shouldBe null
        store.authenticate("clair", sha1Hex("kingdra")).state shouldBe LoginState.AUTHED
        store.authenticate("clair", sha1Hex("dragons")).state shouldBe LoginState.INVALID_PASSWORD
      }

      test("rotating refuses a blank password and an account that is not there") {
        val tokens = JooqRememberMeTokens(db, config, Dispatchers.IO)
        store.addUser("Lance", "dragonite")

        RotatePassword.rotate(store, tokens, "lance", "  ") shouldBe
            RotatePassword.Outcome.Rejected("password must not be blank")
        RotatePassword.rotate(store, tokens, "nobody", "pw") shouldBe
            RotatePassword.Outcome.Rejected("no account called 'nobody'")
        store.authenticate("lance", sha1Hex("dragonite")).state shouldBe LoginState.AUTHED
      }

      test("addUser returns the generated id and getUserId finds it") {
        val id = store.addUser("Alice", "pw")
        store.getUserId("alice") shouldBe id
      }

      /**
       * The remembered login is a row now, not a signature, which is what makes it revocable and
       * what stops any other server minting one.
       */
      test("a remembered login is spent when it is used and can be revoked") {
        val tokens = JooqRememberMeTokens(db, config, Dispatchers.IO)
        val id = store.addUser("Janine", "kunoichi")

        val token = tokens.issue(id)
        tokens.consume(token) shouldBe id
        // Spent, so a copy somebody else kept is worth nothing.
        tokens.consume(token) shouldBe null

        val kept = tokens.issue(id)
        tokens.revokeAll(id) shouldBe 1
        tokens.consume(kept) shouldBe null
      }

      test("a remembered login the server never issued is not accepted") {
        val tokens = JooqRememberMeTokens(db, config, Dispatchers.IO)

        tokens.consume(ByteArray(RememberMeTokens.TOKEN_BYTES) { 7 }) shouldBe null
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
