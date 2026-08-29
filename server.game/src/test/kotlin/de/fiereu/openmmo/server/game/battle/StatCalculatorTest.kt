package de.fiereu.openmmo.server.game.battle

import de.fiereu.openmmo.common.enums.PokemonNature
import de.fiereu.openmmo.common.enums.PokemonStat
import de.fiereu.openmmo.moves.MoveRegistry
import de.fiereu.openmmo.pokemon.LearnsetRegistry
import de.fiereu.openmmo.pokemon.SpeciesRegistry
import de.fiereu.openmmo.server.game.storage.EntityIdService
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.shouldBe

class StatCalculatorTest :
    FunSpec({
      test("a level 5 Bulbasaur with empty IVs and EVs matches the known stat line") {
        StatCalculator.maxHp(45, 0, 0, 5) shouldBe 19
        StatCalculator.stat(49, 0, 0, 5, PokemonNature.HARDY, PokemonStat.ATTACK) shouldBe 9
        StatCalculator.stat(49, 0, 0, 5, PokemonNature.HARDY, PokemonStat.DEFENSE) shouldBe 9
        StatCalculator.stat(65, 0, 0, 5, PokemonNature.HARDY, PokemonStat.SP_ATTACK) shouldBe 11
        StatCalculator.stat(65, 0, 0, 5, PokemonNature.HARDY, PokemonStat.SP_DEFENSE) shouldBe 11
        StatCalculator.stat(45, 0, 0, 5, PokemonNature.HARDY, PokemonStat.SPEED) shouldBe 9
      }

      test("max HP at level 100 with perfect IVs and full EVs") {
        StatCalculator.maxHp(45, 31, 252, 100) shouldBe 294
      }

      test("natures raise and lower by ten percent") {
        val neutral = StatCalculator.stat(49, 0, 0, 50, PokemonNature.HARDY, PokemonStat.ATTACK)
        neutral shouldBe 54
        StatCalculator.stat(49, 0, 0, 50, PokemonNature.ADAMANT, PokemonStat.ATTACK) shouldBe 59
        StatCalculator.stat(49, 0, 0, 50, PokemonNature.MODEST, PokemonStat.ATTACK) shouldBe 48
        StatCalculator.stat(49, 0, 0, 50, PokemonNature.MODEST, PokemonStat.DEFENSE) shouldBe 54
      }

      test("Shedinja's maximum is one, whatever the formula would say") {
        val species = SpeciesRegistry()
        val factory = WildMonFactory(species, MoveRegistry(), LearnsetRegistry(), EntityIdService())
        val shedinja = species.get(292)!!

        // The bare formula over base HP 1, which is what this used to publish.
        StatCalculator.maxHp(shedinja.baseHp, 31, 252, 100) shouldBe 206

        repeat(8) { i ->
          val mon = factory.create(292, 100, BattleRng(seed = i.toLong()))!!
          StatCalculator.computeAll(shedinja, mon).hp shouldBe 1
        }
      }
    })
