@import java.util.List
@import de.fiereu.openmmo.codegen.text.BankEntry

@param String pkg
@param String enumName
@param String region
@param List<BankEntry> banks
package ${pkg}

/**
 * Every text id ${region} can send, by the bank that holds it.
 *
 * [bankIndex] is the bank's place in the DS text archive and [messageCount] is how many messages it
 * holds, so the ids this region can name are exactly `0 until messageCount` in each bank listed
 * here. The banks the ROM build assembles from other resources, item, move, species and trainer
 * names, are absent: nothing in the decomp source says how long they are, so no id is minted into
 * one and the client refuses one that arrives.
 */
enum class ${enumName}(val bankIndex: Int, val messageCount: Int) {
@for(BankEntry b : banks)
  ${b.getName()}(${b.getIndex()}, ${b.getMessageCount()}),
@endfor
  ;

  companion object {
    fun byIndex(bankIndex: Int): ${enumName}? = entries.find { it.bankIndex == bankIndex }
  }
}
