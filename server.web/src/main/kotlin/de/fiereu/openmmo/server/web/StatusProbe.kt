package de.fiereu.openmmo.server.web

import java.io.IOException
import java.net.HttpURLConnection
import java.net.InetSocketAddress
import java.net.Socket
import java.net.URI
import java.nio.charset.StandardCharsets
import java.util.concurrent.atomic.AtomicReference

/**
 * Whether the login and game servers are accepting connections, for the panel on the front page. A
 * TCP handshake is the whole test: neither protocol says anything before the client does.
 *
 * When the game answers, its online-count endpoint is asked how many are playing, so the panel can
 * name a number. A build without that endpoint loses the number, never the panel.
 */
class StatusProbe(
    private val loginHost: String,
    private val loginPort: Int,
    private val gameHost: String,
    private val gamePort: Int,
    gameStatusHost: String = WebConfig.LOOPBACK,
    gameStatusPort: Int = 7779,
    private val cacheMillis: Long = 10_000,
    connectTimeoutMillis: Int = 400,
    private val clock: () -> Long = System::currentTimeMillis,
    private val reachable: (String, Int) -> Boolean = { host, port ->
      tcpReachable(host, port, connectTimeoutMillis)
    },
    private val players: () -> Int? = {
      httpPlayers(gameStatusHost, gameStatusPort, connectTimeoutMillis)
    },
) {
  data class Status(val login: Boolean, val game: Boolean, val players: Int? = null)

  private val cached = AtomicReference<Pair<Long, Status>>()

  /** The last answer for [cacheMillis], so a busy front page cannot turn into a port scan. */
  fun status(): Status {
    val now = clock()
    cached.get()?.let { (at, status) -> if (now - at < cacheMillis) return status }
    val game = reachable(gameHost, gamePort)
    val fresh =
        Status(
            login = reachable(loginHost, loginPort),
            game = game,
            players = if (game) players() else null,
        )
    cached.set(now to fresh)
    return fresh
  }

  fun json(): String {
    val status = status()
    val players = status.players?.let { ""","players":$it""" } ?: ""
    return """{"login":${status.login},"game":${status.game}$players}"""
  }
}

private fun tcpReachable(host: String, port: Int, timeoutMillis: Int): Boolean =
    try {
      Socket().use {
        it.connect(InetSocketAddress(host, port), timeoutMillis)
        true
      }
    } catch (_: IOException) {
      false
    }

private val PLAYERS = Regex(""""players"\s*:\s*(\d{1,9})""")

/** Reads the count out of the game server's status json. Any failure means no number. */
private fun httpPlayers(host: String, port: Int, timeoutMillis: Int): Int? =
    try {
      val connection =
          URI("http://$host:$port/status").toURL().openConnection() as HttpURLConnection
      connection.connectTimeout = timeoutMillis
      connection.readTimeout = timeoutMillis
      try {
        connection.inputStream.use { stream ->
          val body = stream.readNBytes(256).toString(StandardCharsets.UTF_8)
          PLAYERS.find(body)?.groupValues?.get(1)?.toInt()
        }
      } finally {
        connection.disconnect()
      }
    } catch (_: IOException) {
      null
    }
