@import java.util.List
@import de.fiereu.openmmo.codegen.evolution.ParsedBreeding
@import de.fiereu.openmmo.codegen.evolution.ParsedIncenseBaby
@import de.fiereu.openmmo.codegen.evolution.RenderUtil

@param List<List<ParsedBreeding>> chunks
@param List<ParsedIncenseBaby> incenseBabies
package de.fiereu.openmmo.pokemon.generated

import de.fiereu.openmmo.common.enums.EggGroup
import de.fiereu.openmmo.pokemon.BreedingDef
import de.fiereu.openmmo.pokemon.BreedingRegistry
import de.fiereu.openmmo.pokemon.IncenseBaby

internal object GeneratedBreeding {

  /** `sIncenseBabyTable`, in the order the game walks it. */
  val INCENSE_BABIES: List<IncenseBaby> =
      listOf(
@for(ParsedIncenseBaby i : incenseBabies)
          ${RenderUtil.INSTANCE.incenseBaby(i)},
@endfor
      )

  fun loadInto(reg: BreedingRegistry) {
@for(int i = 0; i < chunks.size(); i++)
    registerChunk${i}(reg)
@endfor
  }

@for(int c = 0; c < chunks.size(); c++)
  private fun registerChunk${c}(reg: BreedingRegistry) {
@for(ParsedBreeding b : chunks.get(c))
    ${RenderUtil.INSTANCE.breeding(b)}
@endfor
  }

@endfor
}
