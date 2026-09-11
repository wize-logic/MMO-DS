package de.fiereu.openmmo.codegen.pokemon

import de.fiereu.openmmo.codegen.gen5.Gen5Tables
import java.io.File

/** The 156 species Black adds, as rows of the same table Platinum's 493 are rows of. */
fun gen5Species(dir: File, hidden: Map<Int, String>): List<ParsedSpecies> =
    Gen5Tables(dir).species.map {
      ParsedSpecies(
          id = it.id,
          name = it.name,
          baseHp = it.baseHp,
          baseAttack = it.baseAttack,
          baseDefense = it.baseDefense,
          baseSpeed = it.baseSpeed,
          baseSpAttack = it.baseSpAttack,
          baseSpDefense = it.baseSpDefense,
          type1 = "PokemonType.${it.type1}",
          type2 = "PokemonType.${it.type2}",
          catchRate = it.catchRate,
          expYield = it.expYield,
          evYieldHp = it.evYieldHp,
          evYieldAttack = it.evYieldAttack,
          evYieldDefense = it.evYieldDefense,
          evYieldSpeed = it.evYieldSpeed,
          evYieldSpAttack = it.evYieldSpAttack,
          evYieldSpDefense = it.evYieldSpDefense,
          itemCommon = it.itemCommon,
          itemRare = it.itemRare,
          genderRatio = it.genderRatio,
          eggCycles = it.eggCycles,
          friendship = it.friendship,
          growthRate = "GrowthRate.${it.growthRate}",
          eggGroup1 = "EggGroup.${it.eggGroup1}",
          eggGroup2 = "EggGroup.${it.eggGroup2}",
          ability1 = "Ability.${it.ability1}",
          ability2 = "Ability.${it.ability2}",
          abilityHidden = "Ability.${hidden.getValue(it.id)}",
          safariZoneFleeRate = it.escapeRate,
          bodyColor = "BodyColor.${it.bodyColor}",
          flipSprite = it.flipSprite,
      )
    }
