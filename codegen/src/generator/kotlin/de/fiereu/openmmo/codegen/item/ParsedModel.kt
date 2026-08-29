package de.fiereu.openmmo.codegen.item

/** [ids] is a list because the table lists some items in more than one slot. */
data class ParsedItem(
    val identifier: String,
    val name: String,
    val price: Int,
    val holdEffect: Int,
    val ids: List<Int>,
    val fieldUse: String = "none",
    val useClass: String = "0",
    val amount: Int = 0,
)
