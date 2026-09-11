package de.fiereu.openmmo.server.login.handler

import de.fiereu.network.PacketEvent
import de.fiereu.network.Side
import de.fiereu.network.coroutines.CoroutineProtocolHandler
import de.fiereu.openmmo.common.auth.SessionTokenIssuer
import de.fiereu.openmmo.common.enums.LoginState
import de.fiereu.openmmo.net.login.LoginProtocol
import de.fiereu.openmmo.net.login.packets.GameServerData
import de.fiereu.openmmo.net.login.packets.GameServerListPacket
import de.fiereu.openmmo.net.login.packets.GameServerNodesPacket
import de.fiereu.openmmo.net.login.packets.JoinGameServerPacket
import de.fiereu.openmmo.net.login.packets.LoginRequestPacket
import de.fiereu.openmmo.net.login.packets.LoginResponsePacket
import de.fiereu.openmmo.net.login.packets.PasswordLogin
import de.fiereu.openmmo.net.login.packets.RequestGameServerListPacket
import de.fiereu.openmmo.net.login.packets.SentCredentialsPacket
import de.fiereu.openmmo.net.login.packets.TokenLogin
import de.fiereu.openmmo.server.login.auth.CreateAccount
import de.fiereu.openmmo.server.login.auth.LoginAttemptLimiter
import de.fiereu.openmmo.server.login.auth.RememberMeTokens
import de.fiereu.openmmo.server.login.auth.UserService
import de.fiereu.openmmo.server.login.catalog.GameServerCatalog
import de.fiereu.openmmo.server.login.session.AUTHED_USER_ID
import de.fiereu.openmmo.server.login.update.ClientRevisionFloor
import io.github.oshai.kotlinlogging.KotlinLogging
import javax.inject.Inject
import kotlinx.coroutines.CoroutineScope

private val log = KotlinLogging.logger {}

class LoginAppHandler
@Inject
constructor(
    private val users: UserService,
    private val catalog: GameServerCatalog,
    private val tokenIssuer: SessionTokenIssuer,
    private val rememberMe: RememberMeTokens,
    private val attempts: LoginAttemptLimiter,
    private val updates: ClientRevisionFloor,
    scope: CoroutineScope,
) : CoroutineProtocolHandler<LoginProtocol>(LoginProtocol, Side.SERVER, scope) {

  init {
    onSuspend<LoginRequestPacket> { event -> onLoginRequest(event) }
    onSuspend<RequestGameServerListPacket> { event -> onServerListRequest(event) }
    onSuspend<JoinGameServerPacket> { event -> onJoinGameServer(event) }
  }

  internal suspend fun onLoginRequest(event: PacketEvent<LoginRequestPacket>) {
    // Ahead of the credential, and ahead of the rate limiter, because this is a property of
    // the client rather than of the attempt: a build that cannot play should not spend a
    // password check to find that out, and should not use up an account's attempts doing it
    // either.
    if (!updates.admits(event.packet.installationRevision)) {
      log.info {
        "Refusing ${event.packet.username}: revision ${event.packet.installationRevision}" +
            " is below ${updates.current()}"
      }
      event.session.send(LoginResponsePacket(LoginState.CLIENT_OUT_OF_DATE))
      return
    }
    // A name no account here could have is refused on its shape, before it is carried into a rate
    // limiter key or a query. The wire's string has no ceiling of its own, so without this a login
    // could name thirty thousand characters and every layer below would carry them.
    val badName = CreateAccount.validateUsername(event.packet.username)
    if (badName != null) {
      log.info { "Refusing a login: $badName" }
      event.session.send(LoginResponsePacket(LoginState.INVALID_PASSWORD))
      return
    }
    when (val method = event.packet.method) {
      is PasswordLogin -> onPasswordLogin(event, method)
      is TokenLogin -> onTokenLogin(event, method)
    }
  }

  private suspend fun onPasswordLogin(
      event: PacketEvent<LoginRequestPacket>,
      method: PasswordLogin,
  ) {
    val username = event.packet.username
    val address = addressOf(event)
    // Before the password is checked, not after: checking one costs real work now, so an attempt
    // nobody is allowed to make must not buy any of it.
    if (!attempts.allow(username, address)) {
      log.warn { "Too many failed logins for $username from $address" }
      event.session.send(LoginResponsePacket(LoginState.RATE_LIMITED))
      return
    }
    val result = users.authenticate(username, method.password)
    log.info { "Login attempt for $username: ${result.state}" }
    if (result.state != LoginState.AUTHED || result.userId == null) {
      attempts.recordFailure(username, address)
      event.session.send(LoginResponsePacket(result.state))
      return
    }
    attempts.recordSuccess(username, address)
    event.session.attributes[AUTHED_USER_ID] = result.userId
    if (method.stayLoggedIn) {
      sendRememberMeToken(event, result.userId, username)
    }
    event.session.send(LoginResponsePacket(result.state))
  }

  private suspend fun onTokenLogin(event: PacketEvent<LoginRequestPacket>, method: TokenLogin) {
    val username = event.packet.username
    val address = addressOf(event)
    // The same counter a password goes through. A token is thirty-two random bytes, so this is
    // not about guessing one; it is that the lookup is a database round trip, and an unnamed
    // peer could buy one per packet at whatever rate the socket ran at.
    if (!attempts.allow(username, address)) {
      log.warn { "Too many failed logins for $username from $address" }
      event.session.send(LoginResponsePacket(LoginState.RATE_LIMITED))
      return
    }
    // Spending it is the lookup, so the token that just arrived is now used up whatever happens
    // next. A copy somebody else took stops working the moment the owner signs in, and the other
    // way round, which is the point of a credential that lives in a row rather than in a signature.
    val userId = rememberMe.consume(method.token)
    val user = userId?.let { users.findForToken(it) }
    if (user == null || !user.username.equals(username, ignoreCase = true)) {
      log.warn { "Rejected token login for $username" }
      attempts.recordFailure(username, address)
      event.session.send(LoginResponsePacket(LoginState.INVALID_SAVED_CREDENTIALS))
      return
    }
    log.info { "Token login for ${user.displayName}: AUTHED" }
    attempts.recordSuccess(username, address)
    event.session.attributes[AUTHED_USER_ID] = user.id
    // Sliding expiry, so a player who keeps logging in never has to type a password again.
    sendRememberMeToken(event, user.id, user.displayName)
    event.session.send(LoginResponsePacket(LoginState.AUTHED))
  }

  /**
   * The peer's address without its port, so every socket from one machine shares a counter. A port
   * changes per connection, and a counter that a reconnect resets is not one.
   */
  private fun addressOf(event: PacketEvent<LoginRequestPacket>): String =
      when (val remote = event.session.remoteAddress) {
        is java.net.InetSocketAddress -> remote.address?.hostAddress ?: remote.hostString
        else -> remote.toString()
      }

  private suspend fun sendRememberMeToken(
      event: PacketEvent<LoginRequestPacket>,
      userId: Int,
      displayName: String,
  ) {
    event.session.send(SentCredentialsPacket(displayName, rememberMe.issue(userId)))
  }

  private fun onServerListRequest(event: PacketEvent<RequestGameServerListPacket>) {
    event.session.send(GameServerListPacket(catalog.list()))
  }

  private suspend fun onJoinGameServer(event: PacketEvent<JoinGameServerPacket>) {
    val entry = catalog.find(event.packet.gameServerId)
    if (entry == null) {
      log.warn { "Unknown game server id ${event.packet.gameServerId}" }
      event.session.send(GameServerNodesPacket(LoginState.NO_GS_AVAILABLE))
      return
    }
    val userId = event.session.attributes[AUTHED_USER_ID]
    if (userId == null) {
      log.warn { "Join game server without a completed login" }
      event.session.send(GameServerNodesPacket(LoginState.INVALID_SAVED_CREDENTIALS))
      return
    }
    // Read here rather than kept from the login, so a role granted or withdrawn while the player
    // sits on the character screen lands on this join instead of the one after it. The game server
    // holds no user table, so this ticket is the only way it can learn any of this.
    val roles = users.rolesOf(userId)
    val token = tokenIssuer.issue(userId = userId.toLong(), roles = roles)
    val data =
        GameServerData(
            gameServerId = entry.server.id,
            userId = userId,
            sessionToken = token.bytes,
            localAddress = entry.localAddress,
            localHostname = entry.localHostname,
            port = entry.node.port,
        )
    event.session.send(
        GameServerNodesPacket(
            loginState = LoginState.AUTHED,
            gameServerData = data,
            nodes = listOf(entry.node),
        ),
    )
  }
}
