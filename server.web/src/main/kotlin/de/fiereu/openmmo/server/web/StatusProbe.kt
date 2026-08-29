package de.fiereu.openmmo.server.web

import java.io.IOException
import java.net.InetSocketAddress
import java.net.Socket
import java.util.concurrent.atomic.AtomicReference

/**
 * Whether the login and game servers are accepting connections, for the panel on the front
 * page. A TCP handshake is the whole test: neither protocol says anything before the client
 * does.
 */
class StatusProbe(
    private val loginHost: String,
    private val loginPort: Int,
    private val gameHost: String,
    private val gamePort: Int,
    private val cacheMillis: Long = 10_000,
    connectTimeoutMillis: Int = 400,
    private val clock: () -> Long = System::currentTimeMillis,
    private val reachable: (String, Int) -> Boolean = { host, port ->
      tcpReachable(host, port, connectTimeoutMillis)
    },
) {
  data class Status(val login: Boolean, val game: Boolean)

  private val cached = AtomicReference<Pair<Long, Status>>()

  /** The last answer for [cacheMillis], so a busy front page cannot turn into a port scan. */
  fun status(): Status {
    val now = clock()
    cached.get()?.let { (at, status) -> if (now - at < cacheMillis) return status }
    val fresh =
        Status(login = reachable(loginHost, loginPort), game = reachable(gameHost, gamePort))
    cached.set(now to fresh)
    return fresh
  }

  fun json(): String {
    val status = status()
    return """{"login":${status.login},"game":${status.game}}"""
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
