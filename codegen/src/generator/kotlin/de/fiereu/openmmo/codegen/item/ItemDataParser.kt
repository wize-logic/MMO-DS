package de.fiereu.openmmo.codegen.item

import java.io.File
import java.text.Normalizer
import java.util.Locale
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

/**
 * A wire id is regionId * 1000 + index, and the client answers in the region 5 table. Any
 * table keyed by a decomp's own item index has to be lifted into this block before the two can
 * be compared.
 */
const val ITEM_REGION_BLOCK = 5000

private val PLACEHOLDER_NAMES = setOf("None", "?????", "???", "-", "?", "")

/** A name's position in `item_names.json` is the index `items.json` keys by. */
class ItemDataParser(private val dataDir: File) {

  private val json = Json { ignoreUnknownKeys = true }

  fun parseAll(): List<ParsedItem> {
    val itemsFile = File(dataDir, "items.json")
    val namesFile = File(dataDir, "item_names.json")
    require(itemsFile.exists() && namesFile.exists()) {
      "Item data not found at $dataDir (missing ${itemsFile.name} or ${namesFile.name})"
    }

    val names =
        json.parseToJsonElement(namesFile.readText()).jsonArray.map { it.jsonPrimitive.content }
    val entries =
        json.parseToJsonElement(itemsFile.readText()).jsonArray.associateBy { entry ->
          entry.jsonObject.getValue("index").jsonPrimitive.int
        }

    return names
        .withIndex()
        .filterNot { (index, name) -> index == 0 || name.trim() in PLACEHOLDER_NAMES }
        .mapNotNull { (index, name) ->
          val identifier = identifierOf(name)
          if (identifier.isEmpty()) null else Triple(identifier, name, index)
        }
        .groupBy { it.first }
        .map { (identifier, group) ->
          // Two different names normalising the same would silently become one item.
          val grouped = group.map { it.second }.distinct()
          check(grouped.size == 1) { "Items $grouped all derive the identifier $identifier" }
          val lowest = group.minBy { it.third }
          val entry = entries[lowest.third]?.jsonObject
          ParsedItem(
              identifier = identifier,
              name = lowest.second,
              price = entry?.get("price")?.jsonPrimitive?.int ?: 0,
              // The number the DS decomp gives the same effect a name; see ItemDef.
              holdEffect = entry?.get("hold_effect")?.jsonPrimitive?.int ?: 0,
              ids = group.map { ITEM_REGION_BLOCK + it.third }.sorted(),
              fieldUse = entry?.get("field_use")?.jsonPrimitive?.content ?: "none",
              // The table writes a string for balls and a number for everything else.
              useClass = entry?.get("use_class")?.jsonPrimitive?.content ?: "0",
              amount = entry?.get("amount")?.jsonPrimitive?.int ?: 0,
          )
        }
        .sortedBy { it.ids.first() }
  }

  /** Turns a display name into a Kotlin constant name: "Poké Ball" becomes POKE_BALL. */
  private fun identifierOf(name: String): String =
      Normalizer.normalize(name, Normalizer.Form.NFD)
          .replace(Regex("\\p{Mn}+"), "")
          .uppercase(Locale.ROOT)
          .replace(Regex("[^A-Z0-9]+"), "_")
          .trim('_')
          .let { if (it.firstOrNull()?.isDigit() == true) "_$it" else it }
}
