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

  // Testcontainers' docker-java asks for API 1.32 and modern daemons refuse anything under 1.40,
  // so the Docker-gated specs skipped silently on a machine that has Docker. 1.41 is Docker 20.10
  // and above; an environment that pins its own version keeps it.
  systemProperty("api.version", System.getenv("DOCKER_API_VERSION") ?: "1.41")

  testLogging {
    events(
      TestLogEvent.FAILED,
      TestLogEvent.PASSED,
      TestLogEvent.SKIPPED
    )
  }
}
