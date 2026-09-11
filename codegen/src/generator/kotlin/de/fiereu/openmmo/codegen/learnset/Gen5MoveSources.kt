package de.fiereu.openmmo.codegen.learnset

import de.fiereu.openmmo.codegen.gen5.Gen5Tables
import java.io.File

/** The machines the species Black adds are compatible with. */
fun gen5MoveSources(dir: File): List<ParsedMoveSources> =
    Gen5Tables(dir)
        .species
        .mapNotNull {
          if (it.machineMoveIds.isEmpty()) null
          else
              ParsedMoveSources(
                  dexId = it.id,
                  machineMoveIds = it.machineMoveIds.distinct().sorted(),
                  eggMoveIds = emptyList(),
                  tutorMoveIds = emptyList(),
              )
        }
        .sortedBy { it.dexId }
