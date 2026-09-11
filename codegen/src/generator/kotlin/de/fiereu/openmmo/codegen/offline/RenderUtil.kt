package de.fiereu.openmmo.codegen.offline

object RenderUtil {

  /** The species ids as indented rows of twelve, so the generated file stays readable. */
  fun speciesRows(ids: List<Int>): String =
      ids.chunked(12).joinToString("\n") { row -> "          " + row.joinToString(", ") + "," }
}
