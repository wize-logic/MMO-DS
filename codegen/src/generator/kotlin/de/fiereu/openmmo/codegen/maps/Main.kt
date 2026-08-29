@file:JvmName("Main")

package de.fiereu.openmmo.codegen.maps

import java.io.File

private data class RegionSource(val region: RegionConstants, val dir: File)

private fun parseRegionSource(spec: String): RegionSource {
  val parts = spec.split("|")
  require(parts.size == 2) { "Invalid region spec '$spec', expected <region>|<dir>" }
  val region = REGIONS[parts[0]] ?: error("Unknown region '${parts[0]}', known: ${REGIONS.keys}")
  return RegionSource(region = region, dir = File(parts[1]))
}

/**
 * The terrain of maps that reached the client out of another cartridge, which have no region
 * of their own here: `ported|<decomp dir>|<mmo dir>|<name>,<name>`.
 */
private data class PortedSource(val dir: File, val mmoDir: File, val names: List<String>)

private fun parsePortedSource(spec: String): PortedSource {
  val parts = spec.split("|")
  require(parts.size == 4) {
    "Invalid ported spec '$spec', expected ported|<decomp dir>|<mmo dir>|<name>,<name>"
  }
  return PortedSource(
      dir = File(parts[1]),
      mmoDir = File(parts[2]),
      names = parts[3].split(",").map { it.trim() }.filter { it.isNotEmpty() },
  )
}

fun main(args: Array<String>) {
  require(args.size >= 4) {
    "Usage: <output-dir> <templates-dir> <class-cache-dir> <region|dir>... got ${args.toList()}"
  }
  val outputDir = File(args[0])
  val templatesDir = File(args[1])
  val classCacheDir = File(args[2])
  val specs = args.drop(3)
  val sources = specs.filterNot { it.startsWith("ported|") }.map(::parseRegionSource)
  val ported = specs.filter { it.startsWith("ported|") }.map(::parsePortedSource)

  val parsed =
      sources.map { (region, dir) ->
        println("[maps] parsing ${region.name} (region ${region.regionId}) from $dir")
        if (region.name in NDS_REGIONS) {
          PlatinumNdsParser(dir, region).parseAll()
        } else {
          ParsedRegion(
              PretGbaParser(dir, region, MovementTypes.read(dir), MetatileBehaviors.read(dir))
                  .parseAll())
        }
      }
  // A ported map's attribute bytes are translated into the destination's, so the behaviour names
  // they index are the destination's too, and the Sinnoh table is the one that reads them.
  val behaviors = parsed.firstOrNull { it.tileBehaviors.isNotEmpty() }?.tileBehaviors ?: emptyList()
  val all =
      parsed +
          ported.map { (dir, mmoDir, names) ->
            println("[maps] parsing ported map(s) ${names.joinToString(", ")} from $dir")
            PortedTerrainParser(dir, mmoDir, names, behaviors).parse()
          }
  val maps = all.flatMap { it.maps }
  println("[maps] parsed ${maps.size} maps. writing to $outputDir")
  MapsRenderer(templatesDir, outputDir, classCacheDir).render(all)
  println("[maps] done")
}
