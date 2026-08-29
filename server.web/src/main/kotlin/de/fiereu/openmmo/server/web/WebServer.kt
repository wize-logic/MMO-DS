package de.fiereu.openmmo.server.web

import com.sun.net.httpserver.HttpExchange
import com.sun.net.httpserver.HttpServer
import io.github.oshai.kotlinlogging.KotlinLogging
import java.net.InetSocketAddress
import java.net.URLDecoder
import java.nio.charset.StandardCharsets
import java.util.concurrent.Executors
import java.util.concurrent.Semaphore
import kotlinx.coroutines.runBlocking

private val log = KotlinLogging.logger {}

/**
 * The website's dynamic half: a registration form target and a status endpoint, on loopback
 * behind apache. It renders whole pages rather than answering with json, so registering works
 * in a browser with no script running at all.
 */
class WebServer(
    private val config: WebConfig,
    private val registrations: RegistrationService,
    private val status: StatusProbe,
) {
  private val static = config.staticRoot?.let { StaticFiles(it.toAbsolutePath().normalize()) }

  /** How many registrations may be in the database at once. */
  private val registering = Semaphore(config.concurrentRegistrations)

  private val server: HttpServer

  val port: Int
    get() = server.address.port

  init {
    limitRequestTime()
    server = HttpServer.create(InetSocketAddress(config.host, config.port), BACKLOG)
    server.createContext("/", ::dispatch)
    server.executor =
        Executors.newFixedThreadPool(WORKERS) { runnable ->
          Thread(runnable, "openmmo-web").apply { isDaemon = true }
        }
  }

  fun start() {
    server.start()
    log.info { "website listening on http://${server.address.hostString}:$port" }
  }

  fun stop() = server.stop(0)

  private fun dispatch(exchange: HttpExchange) {
    exchange.use {
      try {
        when (it.requestURI.path) {
          "/api/register" -> register(it)
          "/api/status" -> json(it, status.json())
          "/api/health" -> json(it, """{"ok":true}""")
          else -> staticOrNotFound(it)
        }
      } catch (e: Exception) {
        log.error(e) { "unhandled failure serving ${it.requestURI.path}" }
        html(it, 500, Pages.rejected("something went wrong on our side"))
      }
    }
  }

  private fun register(exchange: HttpExchange) {
    if (exchange.requestMethod != "POST") {
      exchange.responseHeaders.add("Allow", "POST")
      html(exchange, 405, Pages.rejected("registration only accepts a form post"))
      return
    }
    if (!registering.tryAcquire()) {
      log.warn { "registration refused: ${config.concurrentRegistrations} already in flight" }
      html(exchange, 429, Pages.rejected("too many people are registering, try again in a moment"))
      return
    }
    try {
      registerHeld(exchange)
    } finally {
      registering.release()
    }
  }

  private fun registerHeld(exchange: HttpExchange) {
    val form = form(exchange)
    val username = form["username"].orEmpty()
    val outcome = runBlocking {
      registrations.register(
          username = username,
          password = form["password"].orEmpty(),
          confirmation = form["confirm"].orEmpty(),
          remoteAddress = clientAddress(exchange),
      )
    }
    when (outcome) {
      is RegistrationService.Outcome.Created -> {
        log.info { "created account '${outcome.username}'" }
        html(exchange, 201, Pages.created(outcome.username))
      }
      is RegistrationService.Outcome.Rejected -> html(exchange, 400, Pages.rejected(outcome.reason))
      RegistrationService.Outcome.Taken ->
          html(exchange, 409, Pages.rejected("that name is already taken"))
      RegistrationService.Outcome.RateLimited ->
          html(
              exchange,
              429,
              Pages.rejected("too many accounts from this address, try again in an hour"))
      RegistrationService.Outcome.Failed ->
          html(exchange, 500, Pages.rejected("the account could not be saved, try again later"))
    }
  }

  /**
   * Caps how long one request may take to arrive and to be read back. The default is no cap at
   * all, so a caller that opens a connection and then dribbles a byte a minute keeps a worker
   * thread for as long as it likes; eight of those and nothing else is served.
   */
  private fun limitRequestTime() {
    setIfUnset("sun.net.httpserver.maxReqTime", "15")
    setIfUnset("sun.net.httpserver.maxRspTime", "30")
    setIfUnset("sun.net.httpserver.maxIdleConnections", "64")
  }

  private fun setIfUnset(key: String, value: String) {
    if (System.getProperty(key) == null) System.setProperty(key, value)
  }

  private fun staticOrNotFound(exchange: HttpExchange) {
    val served =
        if (exchange.requestMethod == "GET") static?.read(exchange.requestURI.path) else null
    if (served == null) {
      html(exchange, 404, Pages.notFound())
      return
    }
    exchange.responseHeaders.add(CONTENT_TYPE, served.contentType)
    send(exchange, 200, served.bytes)
  }

  /** Reads a urlencoded body, capped, because anonymous callers decide how much they send. */
  private fun form(exchange: HttpExchange): Map<String, String> {
    val body = exchange.requestBody.readNBytes(MAX_BODY).toString(StandardCharsets.UTF_8)
    return body
        .split('&')
        .mapNotNull { pair ->
          val name = pair.substringBefore('=', "")
          if (name.isEmpty()) null else decode(name) to decode(pair.substringAfter('=', ""))
        }
        .toMap()
  }

  private fun decode(value: String): String =
      try {
        URLDecoder.decode(value.replace('+', ' '), StandardCharsets.UTF_8)
      } catch (_: IllegalArgumentException) {
        ""
      }

  /**
   * The address the rate limiter counts against. Only apache talks to this socket, so its forwarded
   * header is trustworthy; a header on a connection from anywhere else is not.
   */
  private fun clientAddress(exchange: HttpExchange): String {
    val peer = exchange.remoteAddress.address
    if (peer?.isLoopbackAddress != true) return peer?.hostAddress ?: "unknown"
    val forwarded =
        exchange.requestHeaders.getFirst("CF-Connecting-IP")
            ?: exchange.requestHeaders.getFirst("X-Forwarded-For")?.substringBefore(',')
    return forwarded?.trim()?.takeIf(String::isNotEmpty) ?: peer.hostAddress
  }

  private fun html(exchange: HttpExchange, status: Int, body: String) {
    exchange.responseHeaders.add(CONTENT_TYPE, "text/html; charset=utf-8")
    exchange.responseHeaders.add("Cache-Control", "no-store")
    send(exchange, status, body.toByteArray(StandardCharsets.UTF_8))
  }

  private fun json(exchange: HttpExchange, body: String) {
    exchange.responseHeaders.add(CONTENT_TYPE, "application/json")
    exchange.responseHeaders.add("Cache-Control", "no-store")
    send(exchange, 200, body.toByteArray(StandardCharsets.UTF_8))
  }

  private fun send(exchange: HttpExchange, status: Int, body: ByteArray) {
    exchange.responseHeaders.add("X-Content-Type-Options", "nosniff")
    exchange.responseHeaders.add("Referrer-Policy", "same-origin")
    if (exchange.requestMethod == "HEAD") {
      exchange.sendResponseHeaders(status, -1)
      return
    }
    exchange.sendResponseHeaders(status, body.size.toLong())
    exchange.responseBody.write(body)
  }

  private companion object {
    const val CONTENT_TYPE = "Content-Type"
    const val BACKLOG = 64
    const val WORKERS = 8
    const val MAX_BODY = 4096
  }
}
