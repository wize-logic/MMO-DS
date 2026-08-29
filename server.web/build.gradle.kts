plugins {
  application
  id("buildsrc.convention.kotlin-jvm")
  id("buildsrc.convention.spotless")
  id("buildsrc.convention.sonarlint")
}

application { mainClass.set("de.fiereu.openmmo.server.web.MainKt") }

dependencies {
  // The account rules, the password scheme and the user store all live in the login server.
  // The website must create an account exactly the way `create-user` does.
  implementation(project(":server.login"))

  implementation(libs.jooq)
  implementation(libs.hikaricp)
  implementation(libs.kotlinx.coroutines)
  implementation(libs.kotlin.logging)
  implementation(libs.logback)
  runtimeOnly(libs.postgresql)

  testImplementation(libs.bundles.kotest)
  testImplementation(libs.kotlinx.coroutines.test)
}

tasks.named<JavaExec>("run") {
  listOf(
          "OPENMMO_WEB_HOST",
          "OPENMMO_WEB_PORT",
          "OPENMMO_WEB_ROOT",
          "OPENMMO_WEB_REGISTRATIONS_PER_HOUR",
          "LOGIN_DB_HOST",
          "LOGIN_DB_PORT",
          "LOGIN_DB_NAME",
          "LOGIN_DB_USER",
          "LOGIN_DB_PASSWORD",
          "LOGIN_PORT",
          "GAME_SERVER_PORT")
      .forEach { key -> env.fetchOrNull(key)?.let { environment(key, it) } }
  // A developer run serves the site itself, so the whole thing works without apache.
  environment("OPENMMO_WEB_ROOT", env.fetchOrNull("OPENMMO_WEB_ROOT") ?: "$rootDir/web/public")
}
