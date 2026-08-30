package de.fiereu.openmmo.server.game

import com.sun.net.httpserver.HttpServer
import de.fiereu.openmmo.server.game.config.GameServerConfig
import de.fiereu.openmmo.server.game.session.SessionRegistry
import io.github.oshai.kotlinlogging.KotlinLogging
import java.net.InetSocketAddress
import java.nio.charset.StandardCharsets

private val log = KotlinLogging.logger {}

/**
 * How many characters are in the world right now, for the website. The game protocol never says who
 * is online without a login, so the front page needs this side door. Loopback by default: the only
 * caller is the web server on the same machine.
 */
class StatusEndpoint(
    private val config: GameServerConfig,
    private val sessions: SessionRegistry,
) {
  private var server: HttpServer? = null

  fun start() {
    val bound = HttpServer.create(InetSocketAddress(config.statusHost, config.statusPort), 0)
    bound.createContext("/status") { exchange ->
      exchange.use {
        val body =
            """{"players":${sessions.onlineCharacterIds().size}}"""
                .toByteArray(StandardCharsets.UTF_8)
        it.responseHeaders.add("Content-Type", "application/json")
        it.responseHeaders.add("Cache-Control", "no-store")
        it.sendResponseHeaders(200, body.size.toLong())
        it.responseBody.write(body)
      }
    }
    bound.start()
    server = bound
    log.info {
      "Status endpoint listening on http://${config.statusHost}:${config.statusPort}/status"
    }
  }

  fun stop() {
    server?.stop(0)
  }
}
