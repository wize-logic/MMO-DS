plugins {
  application
  id("buildsrc.convention.kotlin-jvm")
  id("buildsrc.convention.spotless")
  id("buildsrc.convention.sonarlint")
  id("buildsrc.common.keys")
  id("buildsrc.convention.jooq-db")
  alias(libs.plugins.ksp)
}

application { mainClass.set("de.fiereu.openmmo.server.game.MainKt") }

jooqDb { packageName = "de.fiereu.openmmo.db.game" }

dependencies {
  api(project(":network"))
  api(project(":protocols.game"))
  api(project(":common"))
  api(project(":codegen"))

  implementation(libs.dagger)
  ksp(libs.dagger.compiler)

  implementation(libs.typesafe.config)
  implementation(libs.kotlinx.coroutines)
  implementation(libs.kotlin.logging)
  implementation(libs.logback)
  implementation(libs.kotlinx.serialization.json)

  testImplementation(project(":common.test"))
  testImplementation(libs.bundles.kotest)
  testImplementation(libs.kotlinx.coroutines.test)
}

tasks.named<JavaExec>("run") {
  listOf(
          "OPENMMO_SESSION_SECRET",
          "OPENMMO_SESSION_TOKEN_MAX_AGE",
          "GAME_HOST",
          "GAME_DB_HOST",
          "GAME_DB_PORT",
          "GAME_DB_NAME",
          "GAME_DB_USER",
          "GAME_DB_PASSWORD")
      .forEach { key -> env.fetchOrNull(key)?.let { environment(key, it) } }
  // Off unless .env asks for it. All db/dev holds now is the cleanup for what older checkouts
  // seeded, so this creates nothing and grants nothing.
  environment("GAME_DB_SEED_DEV", env.fetchOrNull("GAME_DB_SEED_DEV") ?: "false")
}

listOf("classes", "processResources").forEach { taskName ->
  tasks.named(taskName) { dependsOn("copyPublicKeyGame", "copyPrivateKeyGame") }
}
