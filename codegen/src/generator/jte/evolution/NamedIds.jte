@import java.util.Map
@import java.lang.Integer

@param Map<String, Integer> ids
package de.fiereu.openmmo.pokemon.generated

/**
 * The national dex ids of the species the hand written breeding and evolution rules name, resolved
 * from the decomp's own species table rather than written out by hand.
 */
internal object NamedIds {
@for(Map.Entry<String, Integer> e : ids.entrySet())
  const val ${e.getKey()} = ${e.getValue()}
@endfor
}
