package de.fiereu.openmmo.codegen.maps

import java.io.File

/**
 * The DS decomp's map header table, read the two ways anything here needs it: the enum whose
 * line order is the header id, and the fields each header row carries.
 */
object PlatinumHeaders {

  /** The map header enum, whose line order is the header id. The tail rows are not headers. */
  fun ids(rootDir: File): Map<String, Int> =
      File(rootDir, "generated/map_headers.txt")
          .readLines()
          .map { it.trim() }
          .filter { it.isNotEmpty() }
          .withIndex()
          .filterNot { (_, line) -> line.contains('=') || line == "MAP_HEADER_COUNT" }
          .associate { (index, line) -> line to index }

  /** Each header row's fields, by header name. */
  fun fields(rootDir: File): Map<String, Map<String, String>> {
    val file = File(rootDir, "include/data/map_headers.h")
    val rows = Regex("""\[(MAP_HEADER_\w+)]\s*=\s*\{(.*?)\n {4}}""", RegexOption.DOT_MATCHES_ALL)
    val fields = Regex("""\.(\w+)\s*=\s*([\w\-]+)""")
    val out =
        rows.findAll(file.readText()).associate { row ->
          row.groupValues[1] to
              fields.findAll(row.groupValues[2]).associate {
                it.groupValues[1] to it.groupValues[2]
              }
        }
    require(out.isNotEmpty()) { "${file.path} parsed to no rows; its shape changed" }
    return out
  }
}
