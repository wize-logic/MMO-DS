package de.fiereu.openmmo.codegen.evolution

data class ParsedEvolution(val method: String, val param: Int, val targetSpeciesId: Int)

data class ParsedBreeding(
    val dexId: Int,
    val offspringSpeciesId: Int,
    val eggGroup1: String,
    val eggGroup2: String,
    val hatchCycles: Int,
)

data class ParsedSpeciesEvolutions(val dexId: Int, val evolutions: List<ParsedEvolution>)

data class ParsedIncenseBaby(
    val babySpeciesId: Int,
    val incenseItemId: Int,
    val grownSpeciesId: Int
)

/** Everything one pass over the per species data produces. */
data class ParsedSpeciesData(
    val evolutions: List<ParsedSpeciesEvolutions>,
    val breeding: List<ParsedBreeding>,
    val incenseBabies: List<ParsedIncenseBaby>,
    val namedSpeciesIds: Map<String, Int>,
)
