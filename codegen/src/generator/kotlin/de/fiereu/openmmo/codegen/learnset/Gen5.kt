package de.fiereu.openmmo.codegen.learnset

import de.fiereu.openmmo.codegen.gen5.Gen5Tables
import java.io.File

/**
 * The level up learnsets of the species Black adds. A species with an empty list gets no row, the
 * same way the decomp reader drops one, so [de.fiereu.openmmo.pokemon.LearnsetRegistry] answers for
 * it the way it answers for anything it has never heard of.
 */
fun gen5Learnsets(dir: File): List<ParsedLearnset> =
    Gen5Tables(dir)
        .species
        .mapNotNull { species ->
          val moves =
              species.learnset.map { ParsedLevelUpMove(level = it.level, moveId = it.moveId) }
          if (moves.isEmpty()) null else ParsedLearnset(species.id, moves.sortedBy { it.level })
        }
        .sortedBy { it.dexId }
