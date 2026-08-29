package de.fiereu.openmmo.codegen.text

import de.fiereu.openmmo.codegen.dialog.RenderUtil

/**
 * What a DS text bank and its messages are called in generated Kotlin. Shared, because a
 * script that shows a line has to name the same enum entry the text generator emitted for it,
 * and two copies of this rule would drift into a port that does not compile.
 */
object TextNames {

  fun className(bank: String): String =
      RenderUtil.className(
          bank.split('_').joinToString("") { it.replaceFirstChar(Char::uppercase) })

  /**
   * A message's label already carries its bank, `TwinleafTown_Text_BigThud` in
   * `twinleaf_town`, so the enum entry is what is left after that prefix.
   */
  fun entryName(enumName: String, label: String): String {
    val bare = enumName.trim('`')
    val stripped =
        when {
          label.startsWith("${bare}_Text_") -> label.removePrefix("${bare}_Text_")
          label.startsWith("${bare}_") -> label.removePrefix("${bare}_")
          else -> label
        }
    return RenderUtil.identifier(stripped.ifEmpty { label })
  }
}
