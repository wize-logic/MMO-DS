package de.fiereu.openmmo.server.login.update

import io.kotest.assertions.throwables.shouldThrowAny
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.attribute.FileTime
import kotlin.io.path.createTempDirectory

/** The repository root, found by walking up from wherever the test runner was started. */
private fun repoRoot(): Path {
  var here: Path? = Path.of("").toAbsolutePath()
  while (here != null) {
    if (Files.exists(here.resolve("settings.gradle.kts"))) return here
    here = here.parent
  }
  error("no settings.gradle.kts above ${Path.of("").toAbsolutePath()}")
}

/** A main_feed.txt the way feedgen.sh writes one. */
internal fun feedgenDocument(dir: Path, minRevision: Int): Path {
  val feed = dir.resolve("main_feed.txt")
  Files.writeString(
      feed,
      """<?xml version="1.0" encoding="UTF-8" standalone="no"?>
        |<main_feed>
        |  <port>2106</port>
        |  <revision>1500</revision>
        |  <min_revision>$minRevision</min_revision>
        |</main_feed>
        |"""
          .trimMargin())
  return feed
}

class ClientRevisionFloorTest :
    FunSpec({
      test("the floor is read out of a real published feed, not out of the tag beside it") {
        // The operator's own live document, signature and all, as it was served. It carries
        // min_ipa_revision beside min_revision, which is exactly the pair a reader that went
        // looking for the first "min...revision" it could see would get wrong.
        val fixture = repoRoot().resolve("mmo/tests/fixtures/feeds/main_feed.txt")
        Files.exists(fixture) shouldBe true
        val floor = ClientRevisionFloor(fixture)
        floor.current() shouldBe 32763
        floor.admits(32763) shouldBe true
        floor.admits(32762) shouldBe false
        // 32707 is what min_ipa_revision says, and reading that one would have let in a client
        // the operator meant to turn away.
        floor.admits(32707) shouldBe false
      }

      test("no feed named is no gate") {
        val floor = ClientRevisionFloor(null)
        floor.current() shouldBe 0
        floor.admits(1) shouldBe true
        floor.admits(0) shouldBe true
      }

      test("a client that claims no revision is admitted rather than measured") {
        val floor = ClientRevisionFloor(feedgenDocument(createTempDirectory("openmmo-feed"), 1500))
        floor.admits(0) shouldBe true
        floor.admits(-1) shouldBe true
        floor.admits(1499) shouldBe false
      }

      test("a feed that is named and cannot be read stops the server rather than the login") {
        val dir = createTempDirectory("openmmo-feed")
        shouldThrowAny { ClientRevisionFloor(dir.resolve("main_feed.txt")) }
        val wrong = dir.resolve("something_else.txt")
        Files.writeString(wrong, "<main_feed><revision>1500</revision></main_feed>")
        shouldThrowAny { ClientRevisionFloor(wrong) }
      }

      test("raising the floor takes effect without a restart") {
        val dir = createTempDirectory("openmmo-feed")
        val feed = feedgenDocument(dir, 1500)
        val floor = ClientRevisionFloor(feed)
        floor.admits(1500) shouldBe true

        feedgenDocument(dir, 1600)
        // A publish that lands inside the filesystem's timestamp granularity is still a publish.
        Files.setLastModifiedTime(
            feed, FileTime.fromMillis(Files.getLastModifiedTime(feed).toMillis() + 2000))
        floor.current() shouldBe 1600
        floor.admits(1500) shouldBe false

        // A feed that goes away mid-publish holds the floor it last read rather than opening up.
        Files.delete(feed)
        floor.current() shouldBe 1600
        floor.admits(1500) shouldBe false
      }
    })
