package de.fiereu.openmmo.codegen.evolution

object RenderUtil {

  fun evolutions(s: ParsedSpeciesEvolutions): String {
    val defs =
        s.evolutions.joinToString(", ") {
          "EvolutionDef(${it.method}, ${it.param}, ${it.targetSpeciesId})"
        }
    return "reg.register(${s.dexId}, listOf($defs))"
  }

  fun breeding(b: ParsedBreeding): String =
      "reg.register(${b.dexId}, BreedingDef(${b.offspringSpeciesId}, ${b.eggGroup1}," +
          " ${b.eggGroup2}, ${b.hatchCycles}))"

  fun incenseBaby(i: ParsedIncenseBaby): String =
      "IncenseBaby(${i.babySpeciesId}, ${i.incenseItemId}, ${i.grownSpeciesId})"
}
