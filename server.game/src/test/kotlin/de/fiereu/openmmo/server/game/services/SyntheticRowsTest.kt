package de.fiereu.openmmo.server.game.services

import de.fiereu.openmmo.server.game.script.Badge
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe
import java.io.File

/**
 * The synthetic ids are declared once, in the client's header, and mirrored here because the server
 * reads two of them. This pins the mirror to the header so the two cannot drift apart unnoticed.
 */
class SyntheticRowsTest :
    FunSpec({
      val header =
          generateSequence(File("").absoluteFile) { it.parentFile }
              .map { File(it, "mmo/include/client.h") }
              .firstOrNull { it.exists() }
              .shouldNotBeNull()
      val text = header.readText()

      fun define(name: String): Int =
          Regex("""#define\s+$name\s+(\d+)""").find(text).shouldNotBeNull().groupValues[1].toInt()

      test("the badge base and count are the header's") {
        define("MMO_SCRIPT_FLAG_BADGE_BASE") shouldBe SyntheticRows.BADGE_BASE
        define("MMO_SCRIPT_BADGE_COUNT") shouldBe Badge.PER_REGION
      }

      test("the respawn var and the running shoes flag are the header's") {
        define("MMO_SCRIPT_VAR_RESPAWN") shouldBe SyntheticRows.RESPAWN
        define("MMO_SCRIPT_FLAG_RUNNING_SHOES") shouldBe SyntheticRows.RUNNING_SHOES
      }

      test("every character is seated with the running shoes, on") {
        // Engine state outside VarsFlags, and the play path rolls a fresh save at every join,
        // so a character without this row cannot run, which is what happened to everybody
        // until it was sent.
        val row = WorldStateService.RUNNING_SHOES_ROW.single()
        row.id.toInt() shouldBe SyntheticRows.RUNNING_SHOES
        row.on shouldBe true
      }

      test("a badge flag names its badge and nothing else does") {
        SyntheticRows.badgeOf(SyntheticRows.BADGE_BASE) shouldBe Badge.COAL
        SyntheticRows.badgeOf(SyntheticRows.BADGE_BASE + 7) shouldBe Badge.BEACON
        SyntheticRows.badgeOf(SyntheticRows.BADGE_BASE + 8) shouldBe null
        SyntheticRows.badgeOf(SyntheticRows.RUNNING_SHOES) shouldBe null
      }
    })
