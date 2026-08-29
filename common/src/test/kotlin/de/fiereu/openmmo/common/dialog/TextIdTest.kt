package de.fiereu.openmmo.common.dialog

import de.fiereu.openmmo.common.enums.Region
import io.kotest.assertions.throwables.shouldThrow
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

/**
 * The oracle is the engine's own build output, not a round trip through this file:
 * pokeplatinum's generated enum gives `TEXT_BANK_TWINLEAF_TOWN` 554, and that bank's generated
 * header gives `TwinleafTown_Text_BigThud` message 0.
 */
class TextIdTest :
    FunSpec({
      test("names a DS string by its bank and message index") {
        TextId.ds(Region.SINNOH, 554, 0) shouldBe 0x322A0000
        TextId.ds(Region.SINNOH, 554, 14) shouldBe 0x322A000E
      }

      test("splits back into the pair the engine's message loader takes") {
        val id = TextId.ds(Region.SINNOH, 554, 14)
        TextId.regionOf(id) shouldBe Region.SINNOH.wireValue.toInt()
        TextId.bankOf(id) shouldBe 554
        TextId.entryOf(id) shouldBe 14
      }

      test("keeps the region nibble clear of the pair") {
        TextId.ds(Region.SINNOH, TextId.BANK_MAX, TextId.ENTRY_MAX) shouldBe 0x3FFFFFFF
      }

      test("refuses a bank or a message that would not fit") {
        shouldThrow<IllegalArgumentException> { TextId.ds(Region.SINNOH, TextId.BANK_MAX + 1, 0) }
        shouldThrow<IllegalArgumentException> { TextId.ds(Region.SINNOH, 0, TextId.ENTRY_MAX + 1) }
        shouldThrow<IllegalArgumentException> { TextId.ds(Region.SINNOH, -1, 0) }
      }

      test("a GBA string spends the same bits on a ROM offset") {
        TextId.gba(Region.HOENN, 0x1E86BC) shouldBe 0x101E86BC
        TextId.regionOf(TextId.gba(Region.KANTO, 0x1A2B3C)) shouldBe Region.KANTO.wireValue.toInt()
      }
    })
