package de.fiereu.openmmo.server.login.handler

import de.fiereu.network.PacketEvent
import de.fiereu.network.SessionAttribute
import de.fiereu.network.SessionAttributes
import de.fiereu.network.SessionContext
import de.fiereu.network.SessionPhase
import de.fiereu.network.Side
import de.fiereu.openmmo.common.auth.SessionTokenIssuer
import de.fiereu.openmmo.common.enums.Language
import de.fiereu.openmmo.common.enums.LoginState
import de.fiereu.openmmo.net.login.packets.JoinGameServerPacket
import de.fiereu.openmmo.net.login.packets.LoginRequestPacket
import de.fiereu.openmmo.net.login.packets.LoginResponsePacket
import de.fiereu.openmmo.net.login.packets.PasswordLogin
import de.fiereu.openmmo.net.login.packets.RequestGameServerListPacket
import de.fiereu.openmmo.net.login.packets.SentCredentialsPacket
import de.fiereu.openmmo.net.login.packets.TokenLogin
import de.fiereu.openmmo.server.login.auth.InMemoryRememberMeTokens
import de.fiereu.openmmo.server.login.auth.InMemoryUserStore
import de.fiereu.openmmo.server.login.auth.LoginAttemptLimiter
import de.fiereu.openmmo.server.login.auth.RememberMeTokens
import de.fiereu.openmmo.server.login.auth.sha1Hex
import de.fiereu.openmmo.server.login.catalog.GameServerCatalog
import de.fiereu.openmmo.server.login.config.GameServerEndpointConfig
import de.fiereu.openmmo.server.login.config.LoginServerConfig
import de.fiereu.openmmo.server.login.di.DaggerLoginServerComponent
import de.fiereu.openmmo.server.login.session.AUTHED_USER_ID
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import io.netty.channel.ChannelFuture
import io.netty.channel.embedded.EmbeddedChannel
import java.net.SocketAddress
import java.time.Clock
import java.time.Duration
import java.time.Instant
import java.util.Base64
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.runBlocking

private class RecordingSession : SessionContext {
  val sent = mutableListOf<Any>()
  private val backing = EmbeddedChannel()

  override val side: Side = Side.SERVER
  override val channel = backing
  override val remoteAddress: SocketAddress
    get() = backing.remoteAddress() ?: java.net.InetSocketAddress(0)

  override val phase: SessionPhase = SessionPhase.ESTABLISHED
  override val handshakeCompletedAt: Instant? = Instant.now()

  override val attributes: SessionAttributes =
      object : SessionAttributes {
        private val map = HashMap<SessionAttribute<*>, Any>()

        @Suppress("UNCHECKED_CAST")
        override fun <T : Any> get(key: SessionAttribute<T>): T? = map[key] as T?

        override fun <T : Any> set(key: SessionAttribute<T>, value: T) {
          map[key] = value
        }

        @Suppress("UNCHECKED_CAST")
        override fun <T : Any> remove(key: SessionAttribute<T>): T? = map.remove(key) as T?

        @Suppress("UNCHECKED_CAST")
        override fun <T : Any> getOrPut(key: SessionAttribute<T>, default: () -> T): T =
            map.getOrPut(key) { default() } as T

        override fun contains(key: SessionAttribute<*>): Boolean = map.containsKey(key)
      }

  override fun send(packet: Any): ChannelFuture {
    sent += packet
    return backing.newSucceededFuture()
  }

  override fun close(reason: () -> String) {
    backing.close()
  }

  override fun onPhase(phase: SessionPhase, listener: () -> Unit) = listener()
}

private fun loginRequest(
    username: String,
    method: de.fiereu.openmmo.net.login.packets.LoginMethod
) =
    LoginRequestPacket(
        username = username,
        manualLogin = true,
        hwid = ByteArray(0),
        method = method,
        language = Language.EN,
        clientRevision = 0,
        installationRevision = 0,
        os = 0u,
        hardwareInfoCache = ByteArray(0),
    )

class LoginAppHandlerTest :
    FunSpec({
      val secret = ByteArray(32) { it.toByte() }

      val maxAge: Duration = Duration.ofDays(30)

      fun newHandler(
          users: InMemoryUserStore,
          clock: Clock = Clock.systemUTC(),
          rememberMe: RememberMeTokens = InMemoryRememberMeTokens(maxAge),
      ): LoginAppHandler =
          LoginAppHandler(
              users = users,
              catalog = GameServerCatalog(GameServerEndpointConfig()),
              tokenIssuer = SessionTokenIssuer(secret, clock),
              rememberMe = rememberMe,
              attempts = LoginAttemptLimiter(),
              scope = CoroutineScope(Job()),
          )

      test("dagger component constructs the handler with all three packet types registered") {
        val config =
            LoginServerConfig(
                host = "127.0.0.1",
                port = 0,
                checksumSize = 16,
                rootKeyResource = "game.private.pem",
                sessionSecret = "test-secret-for-unit-tests".toByteArray(),
            )
        val component = DaggerLoginServerComponent.factory().create(config)
        val handler = component.handlerProvider().get()
        handler.isRegistered(LoginRequestPacket::class) shouldBe true
        handler.isRegistered(RequestGameServerListPacket::class) shouldBe true
        handler.isRegistered(JoinGameServerPacket::class) shouldBe true
      }

      test("password login without stay-logged-in sends only the AUTHED response") {
        val users = InMemoryUserStore()
        users.addUser("test", "test")
        val handler = newHandler(users)
        val session = RecordingSession()
        runBlocking {
          handler.onLoginRequest(
              PacketEvent(loginRequest("test", PasswordLogin(sha1Hex("test"), false)), session))
        }
        session.sent.map { it::class } shouldBe listOf(LoginResponsePacket::class)
        (session.sent.single() as LoginResponsePacket).state shouldBe LoginState.AUTHED
      }

      test("password login with stay-logged-in sends a token that verifies to the user id") {
        val users = InMemoryUserStore()
        val userId = users.addUser("stay", "secret")
        val handler = newHandler(users)
        val session = RecordingSession()
        runBlocking {
          handler.onLoginRequest(
              PacketEvent(loginRequest("stay", PasswordLogin(sha1Hex("secret"), true)), session))
        }
        session.sent.map { it::class } shouldBe
            listOf(SentCredentialsPacket::class, LoginResponsePacket::class)
        val credentials = session.sent[0] as SentCredentialsPacket
        credentials.key.isNotBlank() shouldBe true
        val tokenBytes = Base64.getDecoder().decode(credentials.key)
        tokenBytes.size shouldBe 32
        (session.sent[1] as LoginResponsePacket).state shouldBe LoginState.AUTHED
        session.attributes[AUTHED_USER_ID] shouldBe userId
      }

      test("token login with a matching token authenticates the user and refreshes the token") {
        val users = InMemoryUserStore()
        val userId = users.addUser("tokenuser", "secret")
        val tokens = InMemoryRememberMeTokens(maxAge)
        val handler = newHandler(users, rememberMe = tokens)
        val session = RecordingSession()
        runBlocking {
          val token = tokens.issue(userId)
          handler.onLoginRequest(PacketEvent(loginRequest("tokenuser", TokenLogin(token)), session))
        }
        session.sent.map { it::class } shouldBe
            listOf(SentCredentialsPacket::class, LoginResponsePacket::class)
        val refreshed = session.sent[0] as SentCredentialsPacket
        Base64.getDecoder().decode(refreshed.key).size shouldBe 32
        (session.sent[1] as LoginResponsePacket).state shouldBe LoginState.AUTHED
        session.attributes[AUTHED_USER_ID] shouldBe userId
      }

      /**
       * A token is spent when it is used, so a copy somebody else took stops working the moment the
       * owner signs in. The old signed tokens could not do this: every copy stayed valid until it
       * expired, and nothing could withdraw one.
       */
      test("a token cannot be used twice") {
        val users = InMemoryUserStore()
        val userId = users.addUser("tokenuser", "secret")
        val tokens = InMemoryRememberMeTokens(maxAge)
        val handler = newHandler(users, rememberMe = tokens)
        runBlocking {
          val token = tokens.issue(userId)
          handler.onLoginRequest(
              PacketEvent(loginRequest("tokenuser", TokenLogin(token)), RecordingSession()))

          val replay = RecordingSession()
          handler.onLoginRequest(PacketEvent(loginRequest("tokenuser", TokenLogin(token)), replay))
          (replay.sent.single() as LoginResponsePacket).state shouldBe
              LoginState.INVALID_SAVED_CREDENTIALS
          replay.attributes.contains(AUTHED_USER_ID) shouldBe false
        }
      }

      /** Revoking is deleting, which is the whole reason the tokens became rows. */
      test("a revoked token stops working") {
        val users = InMemoryUserStore()
        val userId = users.addUser("tokenuser", "secret")
        val tokens = InMemoryRememberMeTokens(maxAge)
        val handler = newHandler(users, rememberMe = tokens)
        val session = RecordingSession()
        runBlocking {
          val token = tokens.issue(userId)
          tokens.revokeAll(userId) shouldBe 1
          handler.onLoginRequest(PacketEvent(loginRequest("tokenuser", TokenLogin(token)), session))
        }
        (session.sent.single() as LoginResponsePacket).state shouldBe
            LoginState.INVALID_SAVED_CREDENTIALS
        session.attributes.contains(AUTHED_USER_ID) shouldBe false
      }

      test("token login with an expired token is rejected") {
        val users = InMemoryUserStore()
        val userId = users.addUser("tokenuser", "secret")
        var now = Instant.ofEpochSecond(1_700_000_000)
        val tokens = InMemoryRememberMeTokens(maxAge) { now }
        val handler = newHandler(users, rememberMe = tokens)
        val session = RecordingSession()
        runBlocking {
          val token = tokens.issue(userId)
          now = now.plus(Duration.ofDays(31))
          handler.onLoginRequest(PacketEvent(loginRequest("tokenuser", TokenLogin(token)), session))
        }
        (session.sent.single() as LoginResponsePacket).state shouldBe
            LoginState.INVALID_SAVED_CREDENTIALS
        session.attributes.contains(AUTHED_USER_ID) shouldBe false
      }

      test("token login with invalid bytes is rejected") {
        val users = InMemoryUserStore()
        users.addUser("tokenuser", "secret")
        val handler = newHandler(users)
        val session = RecordingSession()
        runBlocking {
          handler.onLoginRequest(
              PacketEvent(loginRequest("tokenuser", TokenLogin(ByteArray(24) { 1 })), session))
        }
        (session.sent.single() as LoginResponsePacket).state shouldBe
            LoginState.INVALID_SAVED_CREDENTIALS
        session.attributes.contains(AUTHED_USER_ID) shouldBe false
      }

      test("token login for a username that does not exist is rejected") {
        val users = InMemoryUserStore()
        val tokens = InMemoryRememberMeTokens(maxAge)
        val handler = newHandler(users, rememberMe = tokens)
        val session = RecordingSession()
        runBlocking {
          val token = tokens.issue(999)
          handler.onLoginRequest(PacketEvent(loginRequest("ghost", TokenLogin(token)), session))
        }
        (session.sent.single() as LoginResponsePacket).state shouldBe
            LoginState.INVALID_SAVED_CREDENTIALS
        session.attributes.contains(AUTHED_USER_ID) shouldBe false
      }

      test("token login for a user id that does not match the username is rejected") {
        val users = InMemoryUserStore()
        val otherId = users.addUser("other", "secret")
        users.addUser("victim", "secret")
        val tokens = InMemoryRememberMeTokens(maxAge)
        val handler = newHandler(users, rememberMe = tokens)
        val session = RecordingSession()
        runBlocking {
          val token = tokens.issue(otherId)
          handler.onLoginRequest(PacketEvent(loginRequest("victim", TokenLogin(token)), session))
        }
        (session.sent.single() as LoginResponsePacket).state shouldBe
            LoginState.INVALID_SAVED_CREDENTIALS
        session.attributes.contains(AUTHED_USER_ID) shouldBe false
      }
    })
