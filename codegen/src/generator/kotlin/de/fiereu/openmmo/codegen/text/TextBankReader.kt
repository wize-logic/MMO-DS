package de.fiereu.openmmo.codegen.text

import java.io.File
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

/** One message inside a bank. [entry] is its index in the bank, which is half of its wire id. */
data class TextMessage(val label: String, val entry: Int, val text: String)

/** One bank of the DS text archive. [index] is its position in the archive. */
data class TextBank(val name: String, val index: Int, val messages: List<TextMessage>)

/** Reads pokeplatinum's text archive as the decomp itself orders it. */
object TextBankReader {
  private const val PREFIX = "TEXT_BANK_"
  private val json = Json { ignoreUnknownKeys = true }

  fun bankOrder(decompDir: File): List<String> =
      File(decompDir, "generated/text_banks.txt")
          .readLines()
          .map { it.trim() }
          .filter { it.isNotEmpty() }
          .map {
            require(it.startsWith(PREFIX)) { "text_banks.txt line '$it' is not a $PREFIX constant" }
            it.removePrefix(PREFIX).lowercase()
          }

  fun read(decompDir: File): List<TextBank> {
    val textDir = File(decompDir, "res/text")
    require(textDir.isDirectory) { "no text banks at $textDir" }
    return bankOrder(decompDir).mapIndexedNotNull { index, name ->
      val file = File(textDir, "$name.json")
      if (!file.isFile) return@mapIndexedNotNull null
      val messages =
          json.parseToJsonElement(file.readText()).jsonObject.getValue("messages").jsonArray
      TextBank(name, index, messages.mapIndexed { entry, m -> message(name, entry, m.jsonObject) })
    }
  }

  private fun message(
      bank: String,
      entry: Int,
      obj: kotlinx.serialization.json.JsonObject,
  ): TextMessage {
    val label = obj.getValue("id").jsonPrimitive.content
    require(label.isNotEmpty()) { "message $entry of bank $bank has no id" }
    // `garbage` is the decomp's marker for a slot holding leftover bytes rather than a string. It
    // still occupies an entry index, so it is carried, not dropped: dropping one would shift every
    // message after it onto the wrong id.
    val text =
        when (val en = obj["en_US"]) {
          null -> "(unused)"
          is JsonArray -> en.joinToString("") { it.jsonPrimitive.content }
          is JsonPrimitive -> en.content
          else -> error("message $entry of bank $bank has an en_US that is neither text nor lines")
        }
    return TextMessage(label, entry, text)
  }
}
