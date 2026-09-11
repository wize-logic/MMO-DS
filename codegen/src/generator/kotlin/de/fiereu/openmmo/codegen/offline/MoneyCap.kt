package de.fiereu.openmmo.codegen.offline

import de.fiereu.openmmo.codegen.trainer.ParsedTrainer

/** The most money one playthrough can be holding, computed from the trainers rather than typed. */
object MoneyCap {

  /** What the game multiplies a level by before the class rate. */
  private const val LEVEL_RATE = 4

  /** The Amulet Coin's multiplier, which a player optimising for money always has. */
  private const val AMULET_COIN = 2

  /** A double battle pays for two trainers. */
  private const val DOUBLE_BATTLE = 2

  fun compute(trainers: List<ParsedTrainer>): Int =
      trainers.sumOf { trainer ->
        val lastLevel = trainer.party.last().level
        val doubled = if (trainer.doubleBattle) DOUBLE_BATTLE else 1
        lastLevel * LEVEL_RATE * AMULET_COIN * trainer.prizeRate * doubled
      }
}
