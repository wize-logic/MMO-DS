package de.fiereu.openmmo.common.enums

import io.kotest.assertions.throwables.shouldThrowAny
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

class PokemonStatsTest :
    FunSpec({
      test("zero is a valid stat value") {
        val evs = EVs()
        evs.atk = 10
        evs.atk = 0
        evs.atk shouldBe 0
      }

      test("the individual cap still rejects") {
        shouldThrowAny { EVs().apply { hp = 253 } }
        shouldThrowAny { IVs().apply { hp = 32 } }
      }

      test("the total cap counts the replaced value, not the old one") {
        val evs = EVs()
        evs.hp = 252
        evs.atk = 252
        shouldThrowAny { evs.def = 7 }
        evs.def = 6
        evs.total shouldBe 510
        evs.hp = 100
        evs.def = 158
        evs.total shouldBe 510
      }

      test("a full set of perfect IVs fits the total cap") {
        val ivs = IVs()
        ivs.hp = 31
        ivs.atk = 31
        ivs.def = 31
        ivs.spAtk = 31
        ivs.spDef = 31
        ivs.spd = 31
        ivs.total shouldBe 186
      }

      test("an all-zero IV word decompresses and compresses back") {
        val ivs = decompressIVs(0)
        ivs.total shouldBe 0
        ivs.compress() shouldBe 0
      }

      // Each stat's bit position is the game client's own stat index, which is what it shifts
      // by when it reads an IV back out of the word.
      test("each IV sits at the bit position the game client reads it from") {
        fun wordFor(set: IVs.() -> Unit) = IVs().apply(set).compress()

        wordFor { hp = 1 } shouldBe (1 shl 0)
        wordFor { atk = 1 } shouldBe (1 shl 5)
        wordFor { def = 1 } shouldBe (1 shl 10)
        wordFor { spd = 1 } shouldBe (1 shl 15)
        wordFor { spAtk = 1 } shouldBe (1 shl 20)
        wordFor { spDef = 1 } shouldBe (1 shl 25)
      }

      test("an IV word round-trips through every stat") {
        val ivs =
            IVs().apply {
              hp = 22
              atk = 28
              def = 9
              spd = 11
              spAtk = 14
              spDef = 9
            }
        val back = decompressIVs(ivs.compress())
        back.hp shouldBe 22
        back.atk shouldBe 28
        back.def shouldBe 9
        back.spd shouldBe 11
        back.spAtk shouldBe 14
        back.spDef shouldBe 9
      }
    })
