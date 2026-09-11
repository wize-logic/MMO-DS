package de.fiereu.openmmo.codegen.evolution

import de.fiereu.openmmo.codegen.gen5.Gen5Tables
import java.io.File

/** How the species Black adds evolve, and what their eggs are. */
fun gen5Evolutions(dir: File): Pair<List<ParsedSpeciesEvolutions>, List<ParsedBreeding>> {
  val tables = Gen5Tables(dir)
  val evolutions =
      tables.species
          .mapNotNull { species ->
            val rows =
                species.evolutions.map {
                  ParsedEvolution(
                      method = "EvolutionMethod.${it.method}",
                      param = it.param,
                      targetSpeciesId = it.targetSpeciesId,
                  )
                }
            if (rows.isEmpty()) null else ParsedSpeciesEvolutions(species.id, rows)
          }
          .sortedBy { it.dexId }
  val breeding =
      tables.species
          .map {
            ParsedBreeding(
                dexId = it.id,
                offspringSpeciesId = it.offspringSpeciesId,
                eggGroup1 = "EggGroup.${it.eggGroup1}",
                eggGroup2 = "EggGroup.${it.eggGroup2}",
                hatchCycles = it.eggCycles,
            )
          }
          .sortedBy { it.dexId }
  return evolutions to breeding
}
