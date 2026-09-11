package de.fiereu.openmmo.server.game.services.command

import de.fiereu.openmmo.server.game.storage.PcVisit
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe
import java.time.LocalDateTime

private fun used(bank: Int, map: Int, minutesAgo: Long, x: Int = 7, y: Int = 3) =
    PcVisit(
        characterId = 1,
        region = 3,
        bank = bank,
        map = map,
        x = x,
        y = y,
        elevation = 0,
        lastUsed = LocalDateTime.of(2026, 8, 31, 12, 0).minusMinutes(minutesAgo),
    )

class PcBookTest :
    FunSpec({
      test("a character with no PC behind them still has the two doors") {
        val book = PcBook.of(emptyList())
        book.map { it.header } shouldBe listOf(663, 1095)
        book.all { it.x == 4 && it.y == 9 } shouldBe true
      }

      test("used PCs follow the doors, newest first, and a door once used keeps its tile") {
        val book =
            PcBook.of(
                listOf(
                    used(0, 45, minutesAgo = 1),
                    used(2, 151, minutesAgo = 5, x = 5, y = 9),
                    used(1, 162, minutesAgo = 9),
                ))
        book.map { it.header } shouldBe listOf(663, 1095, 45, 418)
        book[0].x shouldBe 5
      }

      test("a map byte recorded off a signed Byte still names its real header") {
        val poisoned = used(bank = 1, map = -92, minutesAgo = 1) // 164 as a signed byte
        poisoned.header shouldBe ((1 shl 8) or 164)
        poisoned.samePlace(used(bank = 1, map = 164, minutesAgo = 2)) shouldBe true
      }

      test("the list is cut at what the client draws") {
        val many = (0 until 20).map { used(bank = 1, map = 100 + it, minutesAgo = it.toLong()) }
        val book = PcBook.of(many)
        book.size shouldBe PcBook.SHOWN
        book.drop(2).map { it.map } shouldBe (100 until 106).toList()
      }
    })
