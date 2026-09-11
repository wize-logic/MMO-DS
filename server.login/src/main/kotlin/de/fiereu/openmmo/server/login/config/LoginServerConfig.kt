package de.fiereu.openmmo.server.login.config

import java.time.Duration

data class LoginServerConfig(
    val host: String,
    val port: Int,
    val checksumSize: Int,
    val rootKeyResource: String,
    val sessionSecret: ByteArray,
    val rememberMeMaxAge: Duration = Duration.ofDays(30),
    val db: DbConfig = DbConfig(),
    val gameServer: GameServerEndpointConfig = GameServerEndpointConfig(),
    val admin: AdminAccountConfig? = null,
    val rootKey: String? = null,
    val rootKeyFile: String? = null,
    /**
     * Path to the operator's published `main_feed.txt`, whose `<min_revision>` is the oldest client
     * this server admits. Unset means no update gate at all, which is what a checkout wants.
     */
    val clientFeed: String? = null,
) {
  override fun equals(other: Any?): Boolean =
      other is LoginServerConfig &&
          host == other.host &&
          port == other.port &&
          checksumSize == other.checksumSize &&
          rootKeyResource == other.rootKeyResource &&
          rootKey == other.rootKey &&
          rootKeyFile == other.rootKeyFile &&
          clientFeed == other.clientFeed &&
          sessionSecret.contentEquals(other.sessionSecret) &&
          rememberMeMaxAge == other.rememberMeMaxAge &&
          db == other.db &&
          gameServer == other.gameServer &&
          admin == other.admin

  override fun hashCode(): Int {
    var h = host.hashCode()
    h = h * 31 + port
    h = h * 31 + checksumSize
    h = h * 31 + rootKeyResource.hashCode()
    h = h * 31 + rootKey.hashCode()
    h = h * 31 + rootKeyFile.hashCode()
    h = h * 31 + clientFeed.hashCode()
    h = h * 31 + sessionSecret.contentHashCode()
    h = h * 31 + rememberMeMaxAge.hashCode()
    h = h * 31 + db.hashCode()
    h = h * 31 + gameServer.hashCode()
    h = h * 31 + admin.hashCode()
    return h
  }
}
