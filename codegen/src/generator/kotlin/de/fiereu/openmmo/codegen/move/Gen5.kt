package de.fiereu.openmmo.codegen.move

import de.fiereu.openmmo.codegen.gen5.Gen5Tables
import java.io.File

/** The 92 moves Black adds, as rows of the same table Platinum's 467 are rows of. */
fun gen5Moves(dir: File): List<ParsedMove> =
    Gen5Tables(dir).moves.map {
      ParsedMove(
          id = it.id,
          name = it.name,
          effect = "MoveEffect.${it.effect}",
          power = it.power,
          type = "PokemonType.${it.type}",
          accuracy = it.accuracy,
          pp = it.pp,
          secondaryEffectChance = it.secondaryEffectChance,
          target = "MoveTarget.${it.target}",
          priority = it.priority,
          flags = it.flags.map { flag -> "MoveFlag.$flag" },
      )
    }
