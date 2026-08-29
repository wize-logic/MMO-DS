@import java.util.List
@import de.fiereu.openmmo.codegen.dialog.DialogEntry
@import de.fiereu.openmmo.codegen.dialog.RenderUtil

@param String pkg
@param String enumName
@param String bankName
@param int bankIndex
@param List<DialogEntry> entries
package ${pkg}

import de.fiereu.openmmo.common.dialog.DialogLine

/**
 * Text bank `${bankName}`, bank ${bankIndex} of the DS text archive, generated from the decomp.
 *
 * Each [textId] is the region, this bank's index and the message's index within it, which is the
 * pair the engine's own message loader takes. A message the decomp marks as garbage keeps its slot
 * here: dropping one would move every message after it onto the wrong id.
 */
enum class ${enumName}(override val textId: Int) : DialogLine {
@for(DialogEntry e : entries)
  ${RenderUtil.INSTANCE.entry(e)}
@endfor
}
