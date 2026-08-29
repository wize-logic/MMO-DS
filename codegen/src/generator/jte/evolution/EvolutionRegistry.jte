@import java.util.List
@import de.fiereu.openmmo.codegen.evolution.ParsedSpeciesEvolutions
@import de.fiereu.openmmo.codegen.evolution.RenderUtil

@param List<List<ParsedSpeciesEvolutions>> chunks
package de.fiereu.openmmo.pokemon.generated

import de.fiereu.openmmo.common.enums.EvolutionMethod
import de.fiereu.openmmo.pokemon.EvolutionDef
import de.fiereu.openmmo.pokemon.EvolutionRegistry

internal object GeneratedEvolutions {
  fun loadInto(reg: EvolutionRegistry) {
@for(int i = 0; i < chunks.size(); i++)
    registerChunk${i}(reg)
@endfor
  }

@for(int c = 0; c < chunks.size(); c++)
  private fun registerChunk${c}(reg: EvolutionRegistry) {
@for(ParsedSpeciesEvolutions s : chunks.get(c))
    ${RenderUtil.INSTANCE.evolutions(s)}
@endfor
  }

@endfor
}
