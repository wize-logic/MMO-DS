package de.fiereu.openmmo.codegen

import java.io.File

/**
 * Reads a `#define <prefix><name> <number>` block, keyed by the full constant name. Emerald writes
 * its trainer classes in hex, FireRed in decimal.
 */
fun defineTable(file: File, prefix: String): Map<String, Int> {
  if (!file.exists()) return emptyMap()
  val regex = Regex("""^#define\s+($prefix\w+)\s+(0[xX][0-9a-fA-F]+|\d+)\s*(?://.*)?$""")
  return file
      .readLines()
      .mapNotNull { regex.find(it.trim()) }
      .associate { it.groupValues[1] to parseNumber(it.groupValues[2]) }
}

/**
 * Reads one of the DS decomp's constant lists under `generated`, where a constant's value is the
 * line it is on. Lines carrying their own `=` value are skipped, the same way the map generator
 * reads them.
 */
fun generatedEnumTable(file: File): Map<String, Int> {
  require(file.exists()) { "Missing generated constant table at ${file.path}" }
  return file
      .readLines()
      .map { it.trim() }
      .withIndex()
      .filter { (_, line) -> line.isNotEmpty() && !line.contains('=') }
      .associate { (index, line) -> line to index }
}

private fun parseNumber(text: String): Int =
    if (text.startsWith("0x", ignoreCase = true)) text.substring(2).toInt(16) else text.toInt()
