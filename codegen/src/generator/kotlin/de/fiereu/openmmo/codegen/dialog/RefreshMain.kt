@file:JvmName("RefreshMain")

package de.fiereu.openmmo.codegen.dialog

import de.fiereu.openmmo.common.dialog.TextId
import de.fiereu.openmmo.common.enums.Region
import java.io.File

// A GBA dialog id is the ROM file offset with the region on top, which is how the client
// knows the ROM to resolve it in. Captured Kanto ids carry 0 there and Hoenn ids carry 1. TextId is
// where that layout lives, shared with the DS ids that spend the same 28 bits differently.
private fun regionOf(region: String): Region =
    Region.entries.find { it.name.equals(region, ignoreCase = true) }
        ?: error("unknown region '$region', its dialog ids cannot be built")

fun main(args: Array<String>) {
  require(args.size >= 3) {
    "Usage: <roms-dir> <data-dir> <region|gameCode|decomp>... got ${args.toList()}"
  }
  val romsDir = File(args[0])
  val dataDir = File(args[1])

  for (spec in args.drop(2)) {
    val (region, gameCode, decomp) = spec.split("|")
    refreshRegion(region, gameCode, File(decomp), romsDir, dataDir)
  }
}

private fun refreshRegion(
    region: String,
    gameCode: String,
    decompDir: File,
    romsDir: File,
    dataDir: File,
) {
  val rom =
      RomIndex.find(romsDir, gameCode)
          ?: error("no $gameCode ROM in $romsDir, so $region dialog ids cannot be resolved")

  val texts = TextParser(decompDir).parseAll()
  val charmap = Charmap.load(File(decompDir, "charmap.txt"))
  val known = regionOf(region)

  var unencodable = 0
  var notFound = 0
  val lines =
      texts.mapNotNull { t ->
        val bytes = charmap.encode(t.content)
        if (bytes == null) {
          unencodable++
          return@mapNotNull null
        }
        val offset = rom.offsetOf(bytes)
        if (offset < 0) {
          notFound++
          return@mapNotNull null
        }
        DialogLine(t.label, TextId.gba(known, offset), t.content)
      }

  val file = DialogTable.file(dataDir, region)
  DialogTable.write(file, region, gameCode, lines)
  println(
      "[dialog] $region: wrote ${lines.size} lines to $file (skipped $unencodable unencodable, $notFound not in ROM)")
}
