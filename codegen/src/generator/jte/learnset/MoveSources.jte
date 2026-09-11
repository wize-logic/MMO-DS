@import java.util.List
@import de.fiereu.openmmo.codegen.learnset.ParsedMoveSources
@import de.fiereu.openmmo.codegen.learnset.RenderUtil

@param List<List<ParsedMoveSources>> chunks
package de.fiereu.openmmo.pokemon.generated

import de.fiereu.openmmo.pokemon.MoveSourceRegistry
import de.fiereu.openmmo.pokemon.MoveSources

internal object GeneratedMoveSources {
  fun loadInto(reg: MoveSourceRegistry) {
@for(int i = 0; i < chunks.size(); i++)
    registerChunk${i}(reg)
@endfor
  }

@for(int c = 0; c < chunks.size(); c++)
  private fun registerChunk${c}(reg: MoveSourceRegistry) {
@for(ParsedMoveSources s : chunks.get(c))
    ${RenderUtil.INSTANCE.moveSources(s)}
@endfor
  }

@endfor
}
