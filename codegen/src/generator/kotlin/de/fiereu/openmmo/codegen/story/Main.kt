@file:JvmName("Main")

package de.fiereu.openmmo.codegen.story

import de.fiereu.openmmo.codegen.maps.NDS_REGIONS
import java.io.File

fun main(args: Array<String>) {
  require(args.size >= 4) {
    "Usage: <output-dir> <templates-dir> <class-cache-dir> <region|decomp>... got ${args.toList()}"
  }
  val outputDir = File(args[0])
  val templatesDir = File(args[1])
  val classCacheDir = File(args[2])

  for (spec in args.drop(3)) {
    val (region, decomp) = spec.split("|")
    val decompDir = File(decomp)
    val nds = region in NDS_REGIONS
    val flags = if (nds) FlagVarParser.ndsFlags(decompDir) else FlagVarParser.flags(decompDir)
    val vars = if (nds) FlagVarParser.ndsVars(decompDir) else FlagVarParser.vars(decompDir)
    println("[story] $region: parsed ${flags.size} flags, ${vars.size} vars from $decompDir")
    StoryConstantsRenderer(region, templatesDir, outputDir, classCacheDir)
        .render(
            flags,
            vars,
            // The DS game opens on a script of its own; its intro sets no gender-specific flags,
            // which is a fact about Platinum rather than a gap here.
            if (nds) FlagVarParser.ndsInitialFlags(decompDir)
            else FlagVarParser.initialFlags(decompDir),
            if (nds) emptyList() else FlagVarParser.maleIntroFlags(decompDir),
            if (nds) emptyList() else FlagVarParser.femaleIntroFlags(decompDir),
        )
  }
}
