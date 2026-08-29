package de.fiereu.openmmo.server.web

import de.fiereu.openmmo.server.login.auth.InMemoryUserStore
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import io.kotest.matchers.string.shouldContain
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.nio.file.Files
import java.nio.file.Path

private fun form(vararg fields: Pair<String, String>) =
    fields.joinToString("&") { (name, value) ->
      "$name=${java.net.URLEncoder.encode(value, Charsets.UTF_8)}"
    }

class WebServerTest :
    FunSpec({
      lateinit var server: WebServer
      lateinit var store: InMemoryUserStore
      lateinit var root: Path
      val client = HttpClient.newHttpClient()

      fun url(path: String) = URI.create("http://127.0.0.1:${server.port}$path")

      fun post(path: String, body: String): HttpResponse<String> =
          client.send(
              HttpRequest.newBuilder(url(path))
                  .header("Content-Type", "application/x-www-form-urlencoded")
                  .POST(HttpRequest.BodyPublishers.ofString(body))
                  .build(),
              HttpResponse.BodyHandlers.ofString())

      fun get(path: String): HttpResponse<String> =
          client.send(
              HttpRequest.newBuilder(url(path)).GET().build(), HttpResponse.BodyHandlers.ofString())

      beforeTest {
        root = Files.createTempDirectory("openmmo-web")
        Files.writeString(root.resolve("index.html"), "<h1>front page</h1>")
        store = InMemoryUserStore()
        // Two accounts already here, so the one a test registers is neither the first id nor the
        // first account, which is the one the server makes a developer.
        store.addUser("admin", "an-admin-password")
        store.addUser("test", "a-test-password")
        server =
            WebServer(
                WebConfig(port = 0, staticRoot = root),
                RegistrationService(store, RateLimiter(5)),
                StatusProbe(
                    "login.test",
                    1,
                    "game.test",
                    2,
                    clock = { 0 },
                    reachable = { _, port -> port == 1 }))
        server.start()
      }

      afterTest { server.stop() }

      test("the form creates an account and says so in the page it returns") {
        val response =
            post(
                "/api/register",
                form("username" to "Ash", "password" to "pikachu-1", "confirm" to "pikachu-1"))

        response.statusCode() shouldBe 201
        response.body() shouldContain "You're in, ash."
        store.getUserId("ash") shouldBe 3
      }

      test("a taken name comes back as a conflict, not a success page") {
        val response =
            post(
                "/api/register",
                form("username" to "admin", "password" to "password-1", "confirm" to "password-1"))

        response.statusCode() shouldBe 409
        response.body() shouldContain "already taken"
      }

      test("a name carrying markup never reaches the database or the page") {
        val response =
            post(
                "/api/register",
                form(
                    "username" to "<script>x</script>",
                    "password" to "password-1",
                    "confirm" to "password-1"))

        response.statusCode() shouldBe 400
        response.body() shouldContain "letters, digits"
        store.getUserId("<script>x</script>") shouldBe null
      }

      test("the form only answers a post") { get("/api/register").statusCode() shouldBe 405 }

      test("the status endpoint reports each server separately") {
        val response = get("/api/status")

        response.statusCode() shouldBe 200
        response.body() shouldBe """{"login":true,"game":false}"""
      }

      test("a developer run serves the site, and cannot be walked out of") {
        get("/").body() shouldContain "front page"
        get("/../../etc/passwd").statusCode() shouldBe 404
        get("/nothing-here.html").statusCode() shouldBe 404
      }
    })
