plugins {
  id("buildsrc.convention.kotlin-jvm")
  id("buildsrc.convention.spotless")
  id("buildsrc.convention.sonarlint")
  id("buildsrc.convention.jte-codegen")
}

dependencies {
  api(project(":common"))
  api(project(":protocols.game"))
  api(libs.dagger)
  "generatorImplementation"(project(":common"))
  "generatorImplementation"(libs.jte)
  "generatorImplementation"(libs.kotlinx.serialization.json)
  testImplementation(sourceSets["generator"].output)
  testImplementation(libs.kotlinx.serialization.json)
  testImplementation(libs.bundles.kotest)
}

// GBA regions sharing the pret map format.
val regionSources =
    mapOf(
        "hoenn" to "pokeemerald",
        "kanto" to "pokefirered",
    )

// DS regions, whose maps are matrices of land data chunks rather than pret's per map layouts, and
// whose trainers, scripts and story flags are not in the same shape either. Only the map generator
// reads these; the rest stay on the GBA sources until each grows its own reader.
val ndsRegionSources =
    mapOf(
        "sinnoh" to "pokeplatinum",
    )

val mapRegionSources = regionSources + ndsRegionSources

// Maps this game did not ship, which reached the client out of another cartridge and are
// registered on the wire by hand in maps/PortedMaps.kt.
val portedMapSource = "pokeheartgold"
val portedMaps = listOf("goldenrod", "goldenrod_pokecenter_1f")

// Which checkout of a decompilation this build reads, per tree.
val decompRoot =
    providers
        .environmentVariable("DECOMP_DIR")
        .orElse(rootProject.layout.projectDirectory.dir("decomp").asFile.path)
        .get()

fun decompDir(name: String): Directory {
  val maintained = File(decompRoot, name)
  return if (File(maintained, ".git").exists()) {
    objects.directoryProperty().apply { set(maintained) }.get()
  } else if (name == "pokeplatinum") {
    // The engine is a submodule of this repository, so Sinnoh needs no setup.
    rootProject.layout.projectDirectory.dir("engine/pokeplatinum")
  } else {
    // No copy travels with this repository. Point DECOMP_DIR at a directory
    // holding the trees and the build reads them there.
    rootProject.layout.projectDirectory.dir("decomp/$name")
  }
}

// Single source-of-truth decomp for the non-region-specific data (moves, species). The two
// GBA decomps agree on the national dex; where they differ (held items, safari flee rate) this
// is the canonical pick, same as byRegion is for maps.
val sourceDecompDir = decompDir("pokeemerald")

// The DS decomp, for the species data the GBA one cannot express. Its species ids are already
// national dex numbers, so tables read from here need no remapping.
val ndsDataDir = decompDir(ndsRegionSources.getValue("sinnoh"))

// The Gen 5 table the live client speaks, not the GBA decomps, which number the same items
// differently.
val itemDataDir = layout.projectDirectory.dir("items")

// Gitignored, and only the manual refresh tasks read them.
val romsDir = rootProject.layout.projectDirectory.dir("roms")

// Committed, because a ROM is the only place these ids exist and no runner has one.
val dialogDataDir = layout.projectDirectory.dir("dialog")

jteCodegen {
  register("maps") {
    mainClass.set("de.fiereu.openmmo.codegen.maps.Main")
    inputDirs.from(mapRegionSources.values.map { decompDir(it) })
    if (portedMaps.isNotEmpty()) {
      inputDirs.from(decompDir(portedMapSource))
      inputFiles.from(rootProject.layout.projectDirectory.file("mmo/MAPS"))
      inputFiles.from(rootProject.layout.projectDirectory.file("mmo/TERRAIN_MAP"))
    }
    extraArgs.set(
        mapRegionSources.map { (region, decomp) ->
          "$region|${decompDir(decomp).asFile.absolutePath}"
        } +
            if (portedMaps.isEmpty()) emptyList()
            else
                listOf(
                    "ported|${decompDir(portedMapSource).asFile.absolutePath}" +
                        "|${rootProject.layout.projectDirectory.dir("mmo").asFile.absolutePath}" +
                        "|${portedMaps.joinToString(",")}"))
  }
  register("item") {
    mainClass.set("de.fiereu.openmmo.codegen.item.Main")
    inputDirs.from(itemDataDir)
    extraArgs.set(listOf(itemDataDir.asFile.absolutePath))
  }
  // Moves come from the DS decomp: the GBA table stops at 354 and the game has 467.
  register("moves") {
    mainClass.set("de.fiereu.openmmo.codegen.move.Main")
    templatesSubdir.set("move")
    inputDirs.from(ndsDataDir)
    extraArgs.set(listOf(ndsDataDir.asFile.absolutePath))
  }
  // Species come from the DS decomp for the same reason moves do: the GBA table is emerald's
  // 386, and the region this server hosts has 493. It is also a generation behind on the 386
  // it has, with none of the second abilities gen 4 handed out.
  register("pokemon") {
    mainClass.set("de.fiereu.openmmo.codegen.pokemon.Main")
    inputDirs.from(ndsDataDir)
    extraArgs.set(listOf(ndsDataDir.asFile.absolutePath))
  }
  // Evolutions and breeding, which only the DS decomp has: the GBA table has neither the gen 4
  // methods nor the 107 species that use them, and its per species data has no offspring at all.
  register("evolution") {
    mainClass.set("de.fiereu.openmmo.codegen.evolution.Main")
    inputDirs.from(ndsDataDir)
    extraArgs.set(listOf(ndsDataDir.asFile.absolutePath))
  }
  // Learnsets follow the species table: the GBA one covers emerald's 386, so every Sinnoh species
  // came out of a wild encounter knowing nothing and falling back to Tackle.
  register("learnset") {
    mainClass.set("de.fiereu.openmmo.codegen.learnset.Main")
    inputDirs.from(ndsDataDir)
    extraArgs.set(listOf(ndsDataDir.asFile.absolutePath))
  }
  // Trainers differ per game, so this is by region like maps rather than from the canonical decomp.
  register("trainer") {
    mainClass.set("de.fiereu.openmmo.codegen.trainer.Main")
    inputDirs.from(mapRegionSources.values.map { decompDir(it) })
    extraArgs.set(
        mapRegionSources.map { (region, decomp) ->
          "$region|${decompDir(decomp).asFile.absolutePath}"
        })
  }
  // Per region flag and var key constants for scripts. Names come from each decomp, so this is by
  // region like maps. The generic story store in server.game does not depend on these, they are the
  // adapter that gives ported scripts readable keys.
  register("story") {
    mainClass.set("de.fiereu.openmmo.codegen.story.Main")
    inputDirs.from(mapRegionSources.values.map { decompDir(it) })
    extraArgs.set(
        mapRegionSources.map { (region, decomp) ->
          "$region|${decompDir(decomp).asFile.absolutePath}"
        })
  }
  register("typechart") {
    mainClass.set("de.fiereu.openmmo.codegen.typechart.Main")
    inputDirs.from(sourceDecompDir)
    extraArgs.set(listOf(sourceDecompDir.asFile.absolutePath))
  }
  register("dialog") {
    mainClass.set("de.fiereu.openmmo.codegen.dialog.Main")
    inputDirs.from(dialogDataDir)
    extraArgs.set(listOf(dialogDataDir.asFile.absolutePath) + regionSources.keys)
  }
  // The DS text archive, which needs no ROM: a bank's index is its line in generated/text_banks.txt
  // and a message's index is its place in that bank's JSON, so the decomp names every string on its
  // own. That is why this is a generator and the GBA table beside it is committed data.
  register("text") {
    mainClass.set("de.fiereu.openmmo.codegen.text.Main")
    inputDirs.from(ndsDataDir)
    extraArgs.set(listOf(ndsDataDir.asFile.absolutePath, "sinnoh"))
  }
}

tasks.register<JavaExec>("refreshDialogTable") {
  group = "codegen"
  description =
      "Re-resolve codegen/dialog from the ROMs in roms/ (manual, run after a decomp bump, commit the result)"
  val fireredDir = decompDir("pokefirered")
  classpath = sourceSets["generator"].runtimeClasspath
  mainClass.set("de.fiereu.openmmo.codegen.dialog.RefreshMain")
  args(
      romsDir.asFile.absolutePath,
      dialogDataDir.asFile.absolutePath,
      "hoenn|BPEE|${sourceDecompDir.asFile.absolutePath}",
      "kanto|BPRE|${fireredDir.asFile.absolutePath}",
  )
}

// What every generator above actually read, resolved the same way it was resolved.
tasks.register("printDecompTrees") {
  group = "openmmo"
  description = "Print which checkout of each decomp this build reads"
  val root = decompRoot
  val itemTable = itemDataDir.asFile.absolutePath
  val trees =
      (mapRegionSources.values + "pokeemerald" + "pokefirered").distinct().sorted().map { name ->
        val dir = decompDir(name).asFile
        "  %-14s %s%s"
            .format(
                name,
                dir.absolutePath,
                if (dir.startsWith(rootDir)) "  (vendored)" else "  (maintained)")
      }
  doLast {
    println("decomp trees, from DECOMP_DIR=$root:")
    trees.forEach { println(it) }
    println("  %-14s %s  (committed tables, not a checkout)".format("gen 5 items", itemTable))
  }
}
