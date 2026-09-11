package de.fiereu.openmmo.maps

import de.fiereu.openmmo.common.enums.EncounterMethod
import de.fiereu.openmmo.common.enums.EvolutionMethod
import de.fiereu.openmmo.common.enums.TimeOfDay
import de.fiereu.openmmo.pokemon.EvolutionRegistry

/**
 * The Unova species this world puts into Sinnoh's wild grass, its water and the far end of a rod.
 */
object WildExpansion {

  /**
   * One family in a habitat's cohort: the first form met by day, the first form met at night, and
   * how prominent it is where it lives.
   */
  data class Cohort(val day: Int, val rank: Int, val night: Int = day)

  /** What an appended slot actually pulls, out of a native table that sums to a hundred. */
  private fun pickWeight(rank: Int): Int =
      if (rank >= SIGNATURE_RANK) SIGNATURE_WEIGHT else RARE_WEIGHT

  /** What [cohort] pulls, for the test that holds the rarity budget to what the owner asked for. */
  internal fun pickWeightOf(cohort: Cohort): Int = pickWeight(cohort.rank)

  /** A kind of place, and what Unova sends to live in it. */
  enum class Habitat(val cohorts: List<Cohort>) {
    /** Open grass within a walk of a town: Route 201 through 204, 207, the windworks meadow. */
    MEADOW(
        listOf(
            Cohort(day = PATRAT, rank = 6),
            Cohort(day = LILLIPUP, rank = 5),
            Cohort(day = PIDOVE, rank = 3, night = PURRLOIN),
        )),

    /** The flowers below Floaroma, where the meadow turns into a garden. */
    FLOWER_MEADOW(
        listOf(
            Cohort(day = PETILIL, rank = 6, night = COTTONEE),
            Cohort(day = FOONGUS, rank = 4),
            Cohort(day = SEWADDLE, rank = 2),
        )),

    /**
     * Eterna Forest itself, which gets a cohort of its own rather than the general woodland one. It
     * is the only closed canopy in Sinnoh, and it is where the two bugs that trade for each other
     * belong.
     */
    ETERNA_FOREST(
        listOf(
            Cohort(day = SEWADDLE, rank = 6),
            Cohort(day = KARRABLAST, rank = 3),
            Cohort(day = SHELMET, rank = 3),
            Cohort(day = EMOLGA, rank = 2, night = JOLTIK),
            Cohort(day = FOONGUS, rank = 1),
        )),

    /** Wooded route: the trees along 205 north, 208, 209, 210 south and 211 west. */
    FOREST(
        listOf(
            Cohort(day = SEWADDLE, rank = 5),
            Cohort(day = DEERLING, rank = 4),
            Cohort(day = EMOLGA, rank = 3, night = VENIPEDE),
            Cohort(day = FOONGUS, rank = 2),
        )),

    /** Mr. Backlot's Trophy Garden, which collects the tame and the ornamental. */
    GARDEN(
        listOf(
            Cohort(day = MINCCINO, rank = 6),
            Cohort(day = AUDINO, rank = 4),
            Cohort(day = PETILIL, rank = 3),
        )),

    /** High open ground: 206, 210 north, 211 east, 214, 215 and the two Coronet hillsides. */
    MOUNTAIN(
        listOf(
            Cohort(day = BLITZLE, rank = 6),
            Cohort(day = BOUFFALANT, rank = 4),
            Cohort(day = MIENFOO, rank = 3),
            Cohort(day = RUFFLET, rank = 2, night = VULLABY),
        )),

    /** Snow: 216, 217, and the two shores at Acuity. */
    SNOW(
        listOf(
            Cohort(day = VANILLITE, rank = 6),
            Cohort(day = CUBCHOO, rank = 5),
            Cohort(day = CRYOGONAL, rank = 2),
        )),

    /** The Great Marsh and the wet ends of Route 212. */
    MARSH(
        listOf(
            Cohort(day = TYMPOLE, rank = 6),
            Cohort(day = STUNFISK, rank = 4),
            Cohort(day = FOONGUS, rank = 3),
            Cohort(day = DUCKLETT, rank = 2),
        )),

    /** Sand and shingle within reach of a town: 213, 218, 221, 222. */
    COAST(
        listOf(
            Cohort(day = DUCKLETT, rank = 6),
            Cohort(day = DWEBBLE, rank = 5),
            Cohort(day = PIDOVE, rank = 3),
        )),

    /** The far islands and headlands past Sunyshore, where everything is already strong. */
    SEA_ROUTE(
        listOf(
            Cohort(day = RUFFLET, rank = 5, night = VULLABY),
            Cohort(day = THROH, rank = 4),
            Cohort(day = SAWK, rank = 3),
            Cohort(day = SCRAGGY, rank = 2),
        )),

    /**
     * Route 228, which is the one place in Sinnoh a Unova desert animal has any business being: the
     * map runs a permanent sandstorm and its own table is already Hippowdon and Rhyhorn.
     */
    DESERT(
        listOf(
            Cohort(day = SANDILE, rank = 6),
            Cohort(day = MARACTUS, rank = 4),
            Cohort(day = DRILBUR, rank = 4),
            Cohort(day = DARUMAKA, rank = 3),
            Cohort(day = SCRAGGY, rank = 2),
        )),

    /** The first holes in the ground: Oreburgh's mine and gate, and the Ravaged Path. */
    CAVE_EARLY(
        listOf(
            Cohort(day = ROGGENROLA, rank = 6),
            Cohort(day = WOOBAT, rank = 5),
            Cohort(day = TIMBURR, rank = 3),
            Cohort(day = DRILBUR, rank = 2),
        )),

    /** Ordinary cave: Wayward, lower Coronet, the Ruin Maniac's tunnel. */
    CAVE(
        listOf(
            Cohort(day = ROGGENROLA, rank = 5),
            Cohort(day = WOOBAT, rank = 5),
            Cohort(day = DRILBUR, rank = 3),
            Cohort(day = TIMBURR, rank = 2),
        )),

    /** Deep cave: upper Coronet and Turnback, where the light stops and the dragons start. */
    CAVE_DEEP(
        listOf(
            Cohort(day = ROGGENROLA, rank = 5),
            Cohort(day = SWOOBAT, rank = 4),
            Cohort(day = TIMBURR, rank = 3),
            Cohort(day = AXEW, rank = 2),
            Cohort(day = DEINO, rank = 1),
        )),

    /** Victory Road, which is the last thing between a trainer and the league. */
    VICTORY_ROAD(
        listOf(
            Cohort(day = TIMBURR, rank = 5),
            Cohort(day = PAWNIARD, rank = 4),
            Cohort(day = DRUDDIGON, rank = 3),
            Cohort(day = AXEW, rank = 2),
            Cohort(day = DEINO, rank = 1),
        )),

    /** Solaceon Ruins, where Unown are already the point and Unova keeps its own old things. */
    RUINS(
        listOf(
            Cohort(day = YAMASK, rank = 5),
            Cohort(day = SIGILYPH, rank = 4),
            Cohort(day = GOLETT, rank = 4),
            Cohort(day = ELGYEM, rank = 2),
        )),

    /** Snowpoint Temple: the same idea as [RUINS], frozen, and much later. */
    ICE_RUINS(
        listOf(
            Cohort(day = GOLETT, rank = 5),
            Cohort(day = YAMASK, rank = 4),
            Cohort(day = CRYOGONAL, rank = 4),
            Cohort(day = ELGYEM, rank = 2),
        )),

    /** The Old Chateau and the Lost Tower, the two places in Sinnoh that are haunted on purpose. */
    HAUNTED(
        listOf(
            Cohort(day = LITWICK, rank = 6),
            Cohort(day = YAMASK, rank = 4),
            Cohort(day = GOTHITA, rank = 3),
            Cohort(day = ZORUA, rank = 1),
        )),

    /** Stark Mountain, still burning. */
    VOLCANIC(
        listOf(
            Cohort(day = DARUMAKA, rank = 5),
            Cohort(day = HEATMOR, rank = 4),
            Cohort(day = PANSEAR, rank = 3),
            Cohort(day = LITWICK, rank = 2),
            Cohort(day = LARVESTA, rank = 1),
        )),

    /** Iron Island and the Fuego Ironworks: worked metal, and the things that live in it. */
    STEEL(
        listOf(
            Cohort(day = KLINK, rank = 5),
            Cohort(day = FERROSEED, rank = 5),
            Cohort(day = DURANT, rank = 3),
            Cohort(day = PAWNIARD, rank = 2),
        )),

    /** The three lakes and Sendoff Spring, which Sinnoh already treats as places where minds go. */
    LAKE(
        listOf(
            Cohort(day = MUNNA, rank = 6),
            Cohort(day = SOLOSIS, rank = 4),
            Cohort(day = DUCKLETT, rank = 3),
        )),
  }

  /**
   * What kind of water a map has, which is a different question from what kind of land it has: the
   * Great Marsh is a marsh whether you are walking in it or surfing on it, but Route 213 is a beach
   * to a walker and open sea to a surfer.
   */
  enum class Shore(
      val surf: List<Cohort>,
      val oldRod: List<Cohort>,
      val goodRod: List<Cohort>,
      val superRod: List<Cohort>,
  ) {
    /** Rivers, lakes and cave pools. */
    FRESH(
        surf =
            listOf(
                Cohort(day = BASCULIN, rank = 7),
                Cohort(day = DUCKLETT, rank = 4),
                Cohort(day = TYMPOLE, rank = 2),
            ),
        oldRod = listOf(Cohort(day = TYMPOLE, rank = 8)),
        goodRod = listOf(Cohort(day = BASCULIN, rank = 7), Cohort(day = TYMPOLE, rank = 4)),
        superRod =
            listOf(
                Cohort(day = BASCULIN, rank = 7),
                Cohort(day = STUNFISK, rank = 3),
                Cohort(day = TYMPOLE, rank = 2),
            )),

    /** Open salt water: the port towns, the beaches and every route past Sunyshore. */
    SEA(
        surf =
            listOf(
                Cohort(day = FRILLISH, rank = 6),
                Cohort(day = DUCKLETT, rank = 5),
                Cohort(day = ALOMOMOLA, rank = 2),
            ),
        oldRod = listOf(Cohort(day = FRILLISH, rank = 8)),
        goodRod = listOf(Cohort(day = FRILLISH, rank = 7), Cohort(day = ALOMOMOLA, rank = 4)),
        superRod =
            listOf(
                Cohort(day = ALOMOMOLA, rank = 7),
                Cohort(day = FRILLISH, rank = 4),
                Cohort(day = BASCULIN, rank = 1),
            )),

    /** The Great Marsh's own water, which is neither of the above and is full of mud. */
    MARSH_WATER(
        surf =
            listOf(
                Cohort(day = TYMPOLE, rank = 7),
                Cohort(day = STUNFISK, rank = 4),
                Cohort(day = DUCKLETT, rank = 2),
            ),
        oldRod = listOf(Cohort(day = TYMPOLE, rank = 8)),
        goodRod = listOf(Cohort(day = STUNFISK, rank = 6), Cohort(day = TYMPOLE, rank = 5)),
        superRod = listOf(Cohort(day = STUNFISK, rank = 7), Cohort(day = TYMPOLE, rank = 5))),

    /**
     * Sunyshore's bay, under the solar panels of the one city in Sinnoh that runs on electricity.
     * Tynamo is the whole reason this shore is not just [SEA].
     */
    ELECTRIC_BAY(
        surf =
            listOf(
                Cohort(day = FRILLISH, rank = 6),
                Cohort(day = DUCKLETT, rank = 4),
                Cohort(day = TYNAMO, rank = 3),
            ),
        oldRod = listOf(Cohort(day = TYNAMO, rank = 8)),
        goodRod = listOf(Cohort(day = TYNAMO, rank = 7), Cohort(day = FRILLISH, rank = 4)),
        superRod =
            listOf(
                Cohort(day = TYNAMO, rank = 7),
                Cohort(day = ALOMOMOLA, rank = 4),
                Cohort(day = FRILLISH, rank = 1),
            ));

    fun cohortsFor(method: EncounterMethod): List<Cohort> =
        when (method) {
          EncounterMethod.WATER -> surf
          EncounterMethod.OLD_ROD -> oldRod
          EncounterMethod.GOOD_ROD -> goodRod
          EncounterMethod.SUPER_ROD -> superRod
          else -> emptyList()
        }
  }

  /**
   * The expansion slots for [table] on [map] under [conditions], to be rolled against alongside
   * [slots] rather than in place of them.
   */
  fun slotsFor(
      map: MapDef,
      table: WildEncounterTable,
      slots: List<WildEncounterSlot>,
      conditions: EncounterConditions,
  ): List<WildEncounterSlot> {
    if (slots.isEmpty()) return emptyList()
    val cohorts = cohortsFor(map, table.method)
    if (cohorts.isEmpty()) return emptyList()
    val night = conditions.timeOfDay.isNight
    return cohorts.map { cohort ->
      val band = bandFor(slots, cohort.rank)
      val root = if (night) cohort.night else cohort.day
      WildEncounterSlot(
          speciesId = stageAt(root, band.first),
          minLevel = band.first,
          maxLevel = band.second,
          weight = pickWeight(cohort.rank),
      )
    }
  }

  /** The whole table as this world rolls it: the cartridge's swapped slots, then the expansion. */
  fun rolledSlots(
      map: MapDef,
      table: WildEncounterTable,
      conditions: EncounterConditions,
  ): List<WildEncounterSlot> {
    val slots = table.slotsAt(conditions)
    return slots + slotsFor(map, table, slots, conditions)
  }

  /** Which cohort list [map] offers to [method], empty where it offers none. */
  fun cohortsFor(map: MapDef, method: EncounterMethod): List<Cohort> {
    if (map.regionId.toInt() != SINNOH_REGION || map.ported) return emptyList()
    return when (method) {
      EncounterMethod.LAND -> habitatOf(map.name)?.cohorts.orEmpty()
      EncounterMethod.WATER,
      EncounterMethod.OLD_ROD,
      EncounterMethod.GOOD_ROD,
      EncounterMethod.SUPER_ROD -> shoreOf(map.name)?.cohortsFor(method).orEmpty()
      // Sinnoh's archives carry no rock smash or headbutt table at all, so there is nothing here
      // to add to. Those two are the ported region's, and a ported map keeps its own cartridge's
      // animals.
      else -> emptyList()
    }
  }

  /**
   * The level band an appended slot of [rank] takes: the band of the native slot whose own weight
   * is nearest to it. The rank rather than the pick weight, for the reason [Cohort] gives.
   */
  internal fun bandFor(slots: List<WildEncounterSlot>, rank: Int): Pair<Int, Int> {
    val nearest = slots.minByOrNull { kotlin.math.abs(it.weight - rank) } ?: return 1 to 1
    return nearest.minLevel to nearest.maxLevel
  }

  /**
   * How far along its family [root] has got by [level], following only the plain level-up chain.
   */
  internal fun stageAt(root: Int, level: Int): Int {
    var species = root
    for (step in 0 until MAX_STAGES) {
      val next =
          evolutions
              .get(species)
              .firstOrNull { it.method == EvolutionMethod.LEVEL && it.param <= level }
              ?.targetSpeciesId ?: return species
      if (next == species || next in NEVER_WILD) return species
      species = next
    }
    return species
  }

  /** Which habitat a Sinnoh map's grass belongs to, by name, first match winning. */
  internal fun habitatOf(name: String): Habitat? =
      LAND_RULES.firstOrNull { (names, _) -> names.matches(name) }?.second

  /** Which kind of water a Sinnoh map has, by the same rule as [habitatOf]. */
  internal fun shoreOf(name: String): Shore? =
      WATER_RULES.firstOrNull { (names, _) -> names.matches(name) }?.second

  /** The left half of a sorting rule: some map names, matched whole or as prefixes. */
  private class Names(val names: List<String>, val prefix: Boolean) {
    fun matches(name: String): Boolean =
        if (prefix) names.any { name.startsWith(it) } else name in names
  }

  private fun exactly(vararg names: String) = Names(names.toList(), prefix = false)

  private fun startingWith(vararg prefixes: String) = Names(prefixes.toList(), prefix = true)

  /**
   * Every map name a land rule spells, each paired with whether it is a prefix rather than a whole
   * name, so a test can check the two kinds the way each is used.
   */
  internal val landRuleNames: List<Pair<String, Boolean>>
    get() = LAND_RULES.flatMap { (names, _) -> names.names.map { it to names.prefix } }

  /**
   * The same for the water rules, minus the empty prefix the fresh water fallthrough matches
   * everything with, which names no map and is not a typo anybody could make.
   */
  internal val waterRuleNames: List<Pair<String, Boolean>>
    get() =
        WATER_RULES.flatMap { (names, _) -> names.names.map { it to names.prefix } }
            .filterNot { (name, _) -> name.isEmpty() }

  /**
   * The land sorting. Reading down it is the fastest way to see what was decided about a place, so
   * it is one rule a line and the specific ones are named outright rather than folded into a
   * pattern.
   */
  private val LAND_RULES: List<Pair<Names, Habitat>> =
      listOf(
          // Haunted before wooded, or the Lost Tower reads as Route 209.
          startingWith("route_209_lost_tower", "old_chateau") to Habitat.HAUNTED,
          startingWith("solaceon_ruins") to Habitat.RUINS,
          startingWith("snowpoint_temple") to Habitat.ICE_RUINS,
          startingWith("stark_mountain") to Habitat.VOLCANIC,
          startingWith("iron_island") to Habitat.STEEL,
          exactly("fuego_ironworks_outside") to Habitat.STEEL,
          startingWith("victory_road") to Habitat.VICTORY_ROAD,
          // Coronet's own floors: the two hillsides are mountain, the first floor and the basement
          // are ordinary cave, and everything above is deep.
          startingWith("mt_coronet_outside") to Habitat.MOUNTAIN,
          startingWith("mt_coronet_1f", "mt_coronet_b1f") to Habitat.CAVE,
          startingWith("mt_coronet") to Habitat.CAVE_DEEP,
          startingWith("turnback_cave") to Habitat.CAVE_DEEP,
          startingWith("oreburgh_mine", "oreburgh_gate") to Habitat.CAVE_EARLY,
          exactly("ravaged_path") to Habitat.CAVE_EARLY,
          startingWith("wayward_cave", "ruin_maniac_cave") to Habitat.CAVE,
          exactly("maniac_tunnel") to Habitat.CAVE,
          exactly("eterna_forest") to Habitat.ETERNA_FOREST,
          exactly("trophy_garden") to Habitat.GARDEN,
          startingWith("great_marsh") to Habitat.MARSH,
          exactly("route_212_north", "route_212_south") to Habitat.MARSH,
          // Acuity is snow on both shores; the other two lakes are not.
          exactly("lake_acuity", "acuity_lakefront") to Habitat.SNOW,
          exactly("route_216", "route_217") to Habitat.SNOW,
          exactly(
              "lake_verity",
              "lake_verity_low_water",
              "lake_valor",
              "valor_lakefront",
              "sendoff_spring") to Habitat.LAKE,
          exactly("route_228") to Habitat.DESERT,
          // Route 223 is open sea with no ground to stand on, so it is in the water list only.
          exactly("route_224", "route_225", "route_226", "route_227", "route_229", "route_230") to
              Habitat.SEA_ROUTE,
          exactly("route_213", "route_218", "route_221", "route_222") to Habitat.COAST,
          exactly("route_206", "route_210_north", "route_211_east", "route_214", "route_215") to
              Habitat.MOUNTAIN,
          exactly("route_205_south") to Habitat.FLOWER_MEADOW,
          exactly(
              "route_205_north", "route_208", "route_209", "route_210_south", "route_211_west") to
              Habitat.FOREST,
          exactly(
              "route_201",
              "route_202",
              "route_203",
              "route_204_north",
              "route_204_south",
              "route_207",
              "valley_windworks_outside") to Habitat.MEADOW,
      )

  /** The water sorting. */
  private val WATER_RULES: List<Pair<Names, Shore>> =
      listOf(
          exactly("sunyshore_city") to Shore.ELECTRIC_BAY,
          startingWith("great_marsh") to Shore.MARSH_WATER,
          exactly("canalave_city", "pastoria_city", "resort_area", "iron_island") to Shore.SEA,
          exactly(
              "route_213",
              "route_218",
              "route_219",
              "route_220",
              "route_221",
              "route_222",
              "route_223",
              "route_224",
              "route_225",
              "route_226",
              "route_227",
              "route_228",
              "route_229",
              "route_230") to Shore.SEA,
          // Everything else Sinnoh lets a rod into is fresh: the lakes, the rivers, the cave pools
          // and the water inside Victory Road.
          startingWith("") to Shore.FRESH,
      )

  /** Sinnoh's own wire region. A ported map travels as region 3 too, hence [MapDef.ported]. */
  private const val SINNOH_REGION = 3

  /** One more than the longest plain level-up chain in the table, so bad data cannot loop. */
  private const val MAX_STAGES = 4

  /** The rank at or above which a family is its habitat's signature one. See [pickWeight]. */
  private const val SIGNATURE_RANK = 5

  /** What a signature family pulls, and what every other one does. Both are rare on purpose. */
  private const val SIGNATURE_WEIGHT = 2
  private const val RARE_WEIGHT = 1

  /**
   * The finished forms of the two pseudo-legendary families Unova adds, which no wild slot here
   * hands over. Fraxure and Zweilous are as far as the ground goes; the last evolution is the
   * player's.
   */
  private val NEVER_WILD = setOf(HAXORUS, HYDREIGON)

  private val evolutions: EvolutionRegistry by lazy { EvolutionRegistry() }

  // The families this file names, by national dex id. Written out rather than reached for through
  // the species registry by name, because a typo in a name would be a lookup that quietly finds
  // nothing where a wrong number is a wrong animal somebody notices.
  private const val MUNNA = 517
  private const val PANSEAR = 513
  private const val PATRAT = 504
  private const val LILLIPUP = 506
  private const val PURRLOIN = 509
  private const val PIDOVE = 519
  private const val BLITZLE = 522
  private const val ROGGENROLA = 524
  private const val WOOBAT = 527
  private const val SWOOBAT = 528
  private const val DRILBUR = 529
  private const val AUDINO = 531
  private const val TIMBURR = 532
  private const val TYMPOLE = 535
  private const val THROH = 538
  private const val SAWK = 539
  private const val SEWADDLE = 540
  private const val VENIPEDE = 543
  private const val COTTONEE = 546
  private const val PETILIL = 548
  private const val BASCULIN = 550
  private const val SANDILE = 551
  private const val DARUMAKA = 554
  private const val MARACTUS = 556
  private const val DWEBBLE = 557
  private const val SCRAGGY = 559
  private const val SIGILYPH = 561
  private const val YAMASK = 562
  private const val ZORUA = 570
  private const val MINCCINO = 572
  private const val GOTHITA = 574
  private const val SOLOSIS = 577
  private const val DUCKLETT = 580
  private const val VANILLITE = 582
  private const val DEERLING = 585
  private const val EMOLGA = 587
  private const val KARRABLAST = 588
  private const val FOONGUS = 590
  private const val FRILLISH = 592
  private const val ALOMOMOLA = 594
  private const val JOLTIK = 595
  private const val FERROSEED = 597
  private const val KLINK = 599
  private const val TYNAMO = 602
  private const val ELGYEM = 605
  private const val LITWICK = 607
  private const val AXEW = 610
  private const val HAXORUS = 612
  private const val CUBCHOO = 613
  private const val CRYOGONAL = 615
  private const val SHELMET = 616
  private const val STUNFISK = 618
  private const val MIENFOO = 619
  private const val DRUDDIGON = 621
  private const val GOLETT = 622
  private const val PAWNIARD = 624
  private const val BOUFFALANT = 626
  private const val RUFFLET = 627
  private const val VULLABY = 629
  private const val HEATMOR = 631
  private const val DURANT = 632
  private const val DEINO = 633
  private const val HYDREIGON = 635
  private const val LARVESTA = 636
}

/**
 * Whether this hour is one the night species are out for, by the same cut the encounter tables use:
 * `TimeOfDay.NIGHT` and `LATE_NIGHT` are night, and morning, day and twilight are not.
 */
private val TimeOfDay.isNight: Boolean
  get() = this == TimeOfDay.NIGHT || this == TimeOfDay.LATE_NIGHT
