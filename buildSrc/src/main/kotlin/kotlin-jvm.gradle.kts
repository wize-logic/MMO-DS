package buildsrc.convention

import org.gradle.api.tasks.testing.logging.TestLogEvent
import org.gradle.kotlin.dsl.dependencies
import org.gradle.kotlin.dsl.withType

plugins {
  kotlin("jvm")
}

kotlin {
  jvmToolchain(25)
}

dependencies {
  testImplementation(kotlin("test"))
}

tasks.withType<Test>().configureEach {
  useJUnitPlatform()

  // Testcontainers' docker-java asks for API 1.32 and a daemon refuses anything below its own
  // minimum, so the Docker-gated specs skip silently on a machine that has Docker. This pin is the
  // floor we hand it, and it has gone stale once already: 1.41 was chosen against a daemon whose
  // minimum was 1.40, and a later one raised that to 1.44. Raise it when that happens again; an
  // environment that pins its own version keeps it. The symptom is a green build with no XML under
  // build/test-results for the IT specs.
  systemProperty("api.version", System.getenv("DOCKER_API_VERSION") ?: "1.44")

  testLogging {
    events(
      TestLogEvent.FAILED,
      TestLogEvent.PASSED,
      TestLogEvent.SKIPPED
    )
  }
}
