package de.fiereu.openmmo.moves

import de.fiereu.openmmo.common.enums.MoveEffect
import de.fiereu.openmmo.common.enums.MoveFlag
import de.fiereu.openmmo.common.enums.MoveTarget
import de.fiereu.openmmo.common.enums.PokemonType
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldContainExactly
import io.kotest.matchers.nulls.shouldBeNull
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe

private const val LAST_GBA_MOVE = 354 // Psycho Boost
private const val LAST_DS_MOVE = 467 // Shadow Force, and the last one the client can draw
private const val LAST_MOVE = 559 // Fusion Bolt

class MoveRegistryTest :
    FunSpec({
      val moves = MoveRegistry()

      test("covers every move the game has, not just the ones the GBA table stops at") {
        moves.size() shouldBe LAST_MOVE
        moves.all().map { it.id }.sorted() shouldContainExactly (1..LAST_MOVE).toList()
      }

      // The gap this table was read from the DS decomp to close: Ambipom's own move had no
      // definition at all, because the GBA table it used to come from ends 104 moves earlier.
      test("has Double Hit, which is past where the GBA table ends") {
        val doubleHit = moves.get(458)

        doubleHit.shouldNotBeNull()
        doubleHit.name shouldBe "Double Hit"
        doubleHit.type shouldBe PokemonType.NORMAL
        doubleHit.power shouldBe 35
        doubleHit.accuracy shouldBe 90
        doubleHit.pp shouldBe 10
        doubleHit.effect shouldBe MoveEffect.DOUBLE_HIT
        doubleHit.target shouldBe MoveTarget.SELECTED
        doubleHit.hasFlag(MoveFlag.MAKES_CONTACT) shouldBe true
      }

      // Gen 4 rebalanced moves the GBA table also has, and these are the Gen 4 numbers because
      // Sinnoh is the world the server hosts. Fly at 70 power would mean the older table came back.
      test("gives a shared move the stats the world it hosts gives it") {
        moves.get(19)?.power shouldBe 90 // Fly, 70 in Gen 3
        moves.get(91)?.power shouldBe 80 // Dig, 60
        moves.get(22)?.pp shouldBe 15 // Vine Whip, 10
        moves.get(148)?.accuracy shouldBe 100 // Flash, 70
      }

      // Effects, targets and flags Gen 4 introduced: the three places a move from the new block
      // needs a name the GBA tree never had.
      test("carries the effects, targets and flags only the DS tree has") {
        moves.get(367)?.target shouldBe MoveTarget.USER_OR_ALLY // Acupressure
        moves.get(382)?.target shouldBe MoveTarget.SELECTED_ME_FIRST // Me First
        moves.get(467)?.effect shouldBe MoveEffect.SHADOW_FORCE
        moves.get(19)?.hasFlag(MoveFlag.HIDES_SHADOWS) shouldBe true // Fly, off screen for a turn
      }

      // A move that never misses is accuracy 0 in both trees, and the battle engine reads it that
      // way. A generator that turned it into 0% would silently make Aerial Ace whiff.
      test("writes a move that cannot miss as accuracy 0") { moves.get(332)?.accuracy shouldBe 0 }

      // The three generations end where each of them ends, which is the check that the Gen 5
      // block was appended rather than laid over the table it joined.
      test("ends where each generation it was assembled from ends") {
        moves.get(LAST_GBA_MOVE)?.name shouldBe "Psycho Boost"
        moves.get(LAST_DS_MOVE)?.name shouldBe "Shadow Force"
        moves.get(LAST_MOVE)?.name shouldBe "Fusion Bolt"
        moves.get(LAST_MOVE + 1).shouldBeNull()
      }

      // The block Black added, which no decompilation this repo reads has a row for. Hone Claws is
      // the first of them and V-create is the strongest, so between them they catch a Gen 5 table
      // that was read at the wrong offset or joined at the wrong id.
      test("carries the moves that came out of a Black cartridge") {
        val honeClaws = moves.get(468)

        honeClaws.shouldNotBeNull()
        honeClaws.name shouldBe "Hone Claws"
        honeClaws.type shouldBe PokemonType.DARK
        honeClaws.pp shouldBe 15
        honeClaws.target shouldBe MoveTarget.USER
        honeClaws.hasFlag(MoveFlag.SNATCH_AFFECTED) shouldBe true

        moves.get(557)?.power shouldBe 180 // V-create
        moves.get(469)?.priority shouldBe 3 // Wide Guard
      }
    })
