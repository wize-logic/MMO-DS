package de.fiereu.openmmo.maps

import de.fiereu.openmmo.common.enums.EncounterMethod
import de.fiereu.openmmo.common.enums.TimeOfDay
import io.kotest.assertions.withClue
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.collections.shouldContain
import io.kotest.matchers.collections.shouldContainExactly
import io.kotest.matchers.collections.shouldNotBeEmpty
import io.kotest.matchers.collections.shouldNotContain
import io.kotest.matchers.ints.shouldBeGreaterThan
import io.kotest.matchers.ints.shouldBeInRange
import io.kotest.matchers.ints.shouldBeLessThan
import io.kotest.matchers.ints.shouldBeLessThanOrEqual
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe
import io.kotest.matchers.shouldNotBe

/**
 * The Unova species this world adds to Sinnoh, checked from the two ends that can actually go
 * wrong: that the cartridge's own table is left exactly as it was, and that every map name the
 * sorting mentions is a map that exists and has the table the rule assumes.
 */
class WildExpansionTest :
    FunSpec({
      val maps = MapManager()
      val morning = EncounterConditions(TimeOfDay.MORNING)
      val night = EncounterConditions(TimeOfDay.NIGHT)

      /** The lowest and highest national dex id Black adds, which is the whole of Unova. */
      val unova = 494..649

      fun map(name: String): MapDef = maps.byName(name).also { it.shouldNotBeNull() }!!

      fun landOf(name: String): WildEncounterTable {
        val table = map(name).encounterTable(EncounterMethod.LAND)
        table.shouldNotBeNull()
        return table
      }

      test("the expansion is appended and the cartridge's twelve slots are untouched") {
        val route201 = map("route_201")
        val table = landOf("route_201")

        val rolled = WildExpansion.rolledSlots(route201, table, morning)
        // Everything the cartridge authored is still there, in place, unchanged.
        rolled.take(12) shouldBe table.slotsAt(morning)
        rolled.size shouldBe 12 + WildExpansion.Habitat.MEADOW.cohorts.size
        // And nothing appended is one of Sinnoh's own.
        rolled.drop(12).forEach { it.speciesId shouldBeInRange unova }
      }

      test("an appended slot takes its levels from the map it is on, not from a number here") {
        for (name in listOf("route_201", "route_225", "mt_coronet_6f")) {
          val table = landOf(name)
          val native = table.slotsAt(morning)
          val low = native.minOf { it.minLevel }
          val high = native.maxOf { it.maxLevel }

          WildExpansion.slotsFor(map(name), table, native, morning).forEach {
            it.minLevel shouldBeInRange low..high
            it.maxLevel shouldBeInRange low..high
            // The band is a whole native slot's band, never a blend of two.
            native.any { slot ->
              slot.minLevel == it.minLevel && slot.maxLevel == it.maxLevel
            } shouldBe true
          }
        }
      }

      test("route 201 meets Patrat in the morning and Purrloin after dark") {
        val route201 = map("route_201")
        val table = landOf("route_201")

        val byDay =
            WildExpansion.rolledSlots(route201, table, morning).drop(12).map { it.speciesId }
        val byNight =
            WildExpansion.rolledSlots(route201, table, night).drop(12).map { it.speciesId }

        byDay shouldContainExactly listOf(504, 506, 519)
        byNight shouldContainExactly listOf(504, 506, 509)
        // Twilight is day, which is the cut the cartridge's own slot 2 and 3 swap uses.
        WildExpansion.rolledSlots(route201, table, EncounterConditions(TimeOfDay.TWILIGHT)) shouldBe
            WildExpansion.rolledSlots(route201, table, EncounterConditions(TimeOfDay.DAY))
      }

      test("which stage of a family turns up is the evolution table's answer") {
        // Blitzle evolves at 27. Route 206 is level 16 to 19 and Coronet's north hillside is 36 to
        // 40, so the same one line of data has to produce both animals.
        val route206 = WildExpansion.rolledSlots(map("route_206"), landOf("route_206"), morning)
        val hillside =
            WildExpansion.rolledSlots(
                map("mt_coronet_outside_north"), landOf("mt_coronet_outside_north"), morning)

        route206.map { it.speciesId } shouldContain 522
        hillside.map { it.speciesId } shouldContain 523
        hillside.map { it.speciesId } shouldNotContain 522
      }

      test("a family whose evolution needs a stone or a trade stays where it started") {
        // Cottonee only becomes Whimsicott with a Sun Stone, and Karrablast only trades. Neither is
        // something a wild slot could have done, so both stay at the first form however high the
        // level band is.
        WildExpansion.stageAt(546, 100) shouldBe 546
        WildExpansion.stageAt(588, 100) shouldBe 588
        // Roggenrola's is a plain level up at 25, so it does move.
        WildExpansion.stageAt(524, 24) shouldBe 524
        WildExpansion.stageAt(524, 25) shouldBe 525
      }

      test("the ground never hands over a finished pseudo-legendary") {
        // Axew reaches Haxorus at 48 and Victory Road's rooms are level 47 to 50, so without a
        // stop the last dungeon before the league had a wild Haxorus in it.
        WildExpansion.stageAt(610, 47) shouldBe 611
        WildExpansion.stageAt(610, 100) shouldBe 611
        WildExpansion.stageAt(633, 100) shouldBe 634

        val victoryRoad = map("victory_road_1f_room_1")
        val table = landOf("victory_road_1f_room_1")
        val rolled = WildExpansion.rolledSlots(victoryRoad, table, morning).map { it.speciesId }
        rolled shouldContain 611
        rolled shouldNotContain 612
        rolled shouldNotContain 635
      }

      test("the marsh's day pair still lands on the cartridge's slots, not on an appended one") {
        // The Great Marsh is the one place both features touch: the day writes a species over slots
        // 6 and 7, and its water table is five slots long. Appending before that swap ran would put
        // a Quagsire on a Stunfisk.
        val marsh = map("great_marsh_1")
        val water = marsh.encounterTable(EncounterMethod.WATER)
        water.shouldNotBeNull()
        water.slots.size shouldBeLessThanOrEqual 6

        val daily = EncounterConditions(TimeOfDay.MORNING, dailySpecies = listOf(194, 195))
        val rolled = WildExpansion.rolledSlots(marsh, water, daily)

        rolled.size shouldBeGreaterThan 6
        // The cartridge's own five are exactly what the table produces on its own ...
        rolled.take(water.slots.size) shouldBe water.slotsAt(daily)
        // ... and the appended ones are the marsh's, untouched by the day's pair.
        rolled
            .drop(water.slots.size)
            .map { it.speciesId }
            .forEach {
              it shouldBeInRange unova
              it shouldNotBe 194
              it shouldNotBe 195
            }
      }

      test("Sunyshore's bay is the one water in Sinnoh with a Tynamo in it") {
        val sunyshore = map("sunyshore_city")
        for (rod in listOf(EncounterMethod.OLD_ROD, EncounterMethod.GOOD_ROD)) {
          val table = sunyshore.encounterTable(rod)
          table.shouldNotBeNull()
          WildExpansion.rolledSlots(sunyshore, table, morning).map { it.speciesId } shouldContain
              602
        }
        // Nowhere else. Canalave is a port on the same sea and gets the ordinary salt water list.
        val canalave = map("canalave_city")
        val table = canalave.encounterTable(EncounterMethod.OLD_ROD)
        table.shouldNotBeNull()
        WildExpansion.rolledSlots(canalave, table, morning).map { it.speciesId } shouldNotContain
            602
      }

      test("every whole map name a land rule spells is a real Sinnoh map with grass") {
        val grass =
            maps
                .all()
                .filter { it.regionId.toInt() == 3 && !it.ported }
                .filter { it.encounterTable(EncounterMethod.LAND) != null }
                .map { it.name }
                .toSet()

        WildExpansion.landRuleNames.shouldNotBeEmpty()
        val wrong = WildExpansion.landRuleNames.filterNot { matches(it, grass) }.map { it.first }
        withClue("land rules name maps that have no grass table: ${wrong.sorted()}") {
          wrong shouldBe emptyList()
        }
      }

      test("every Sinnoh map with grass has been sorted into a habitat") {
        // The gap this catches is the silent one: a map nobody thought of gets no expansion and
        // reads exactly as it did before, so only a sweep finds it. Valor Lakefront was one.
        val unsorted =
            maps
                .all()
                .filter { it.regionId.toInt() == 3 && !it.ported }
                .filter { it.encounterTable(EncounterMethod.LAND) != null }
                .filter { WildExpansion.habitatOf(it.name) == null }
                .map { it.name }
                .sorted()

        withClue("Sinnoh maps with grass and no habitat: $unsorted") {
          unsorted shouldBe emptyList()
        }
      }

      test("every whole map name a water rule spells is a real Sinnoh map with water") {
        val watery =
            maps
                .all()
                .filter { it.regionId.toInt() == 3 && !it.ported }
                .filter { map -> WATER_METHODS.any { map.encounterTable(it) != null } }
                .map { it.name }
                .toSet()

        WildExpansion.waterRuleNames.shouldNotBeEmpty()
        val wrong = WildExpansion.waterRuleNames.filterNot { matches(it, watery) }.map { it.first }
        withClue("water rules name maps with no water table: ${wrong.sorted()}") {
          wrong shouldBe emptyList()
        }
      }

      test("every map with water falls into a shore that answers the methods it has") {
        // Fresh is the fallthrough, so this cannot find an unsorted map the way the land sweep can.
        // What it does check is that no shore is missing a list for a method some map really uses.
        val watery =
            maps
                .all()
                .filter { it.regionId.toInt() == 3 && !it.ported }
                .filter { map -> WATER_METHODS.any { map.encounterTable(it) != null } }
        watery.shouldNotBeEmpty()
        for (map in watery) {
          val shore = WildExpansion.shoreOf(map.name)
          withClue("${map.name} has water and no shore") { shore.shouldNotBeNull() }
          for (method in WATER_METHODS) {
            if (map.encounterTable(method) == null) continue
            withClue("${map.name} $method") { shore!!.cohortsFor(method).shouldNotBeEmpty() }
          }
        }
      }

      test("nothing added is a starter, a fossil, a legendary or outside Unova") {
        val forbidden =
            // The three starter families, the two fossil revivals, and everything from Cobalion on.
            (495..503).toSet() + (564..567).toSet() + (638..649).toSet()

        val everything =
            WildExpansion.Habitat.entries.flatMap { it.cohorts } +
                WildExpansion.Shore.entries.flatMap { shore ->
                  WATER_METHODS.flatMap { shore.cohortsFor(it) }
                }
        everything.shouldNotBeEmpty()
        for (cohort in everything) {
          for (species in listOf(cohort.day, cohort.night)) {
            species shouldBeInRange unova
            withClue("species $species is on the excluded list") {
              (species in forbidden) shouldBe false
            }
          }
          cohort.rank shouldBeGreaterThan 0
        }
      }

      test("every unova encounter is rare, and none is commoner than the cartridge's rarest") {
        // The owner's call on 2026-09-06. The cartridge's own slots sum to 100, so an appended
        // slot's weight is its share out of roughly 105: weight 2 is about one meeting in
        // fifty.
        val maps = MapManager()
        val route201 = map("route_201")
        val table = landOf("route_201")

        for (habitat in WildExpansion.Habitat.entries) {
          val total = habitat.cohorts.sumOf { WildExpansion.pickWeightOf(it) }
          withClue("$habitat adds $total") {
            total shouldBeInRange 4..6
            habitat.cohorts.size shouldBeInRange 3..5
          }
          habitat.cohorts.forEach {
            withClue("$habitat ${it.day}") { WildExpansion.pickWeightOf(it) shouldBeInRange 1..2 }
          }
        }
        for (shore in WildExpansion.Shore.entries) {
          for (method in WATER_METHODS) {
            val total = shore.cohortsFor(method).sumOf { WildExpansion.pickWeightOf(it) }
            withClue("$shore $method adds $total") { total shouldBeInRange 0..6 }
          }
        }

        // And on a real table: the appended slots are all at or below the natives' rarest tier.
        val rolled = WildExpansion.rolledSlots(route201, table, morning)
        val nativeRarest = table.slots.minOf { it.weight }
        rolled.drop(12).forEach { it.weight shouldBeLessThanOrEqual nativeRarest + 1 }
        maps.size() shouldBeGreaterThan 0
      }

      test("making them rare did not also make them the strongest thing on the map") {
        // The trap this pins: the level band is chosen by matching a cohort's rank against the
        // native slots, and the natives put their highest levels on their weight-1 slots.
        val lostTower = map("route_209_lost_tower_2f")
        val table = landOf("route_209_lost_tower_2f")
        val native = table.slotsAt(morning)
        val appended = WildExpansion.slotsFor(lostTower, table, native, morning)

        val topBand = native.filter { it.weight == native.minOf { s -> s.weight } }
        val topLevel = topBand.maxOf { it.maxLevel }
        // The map's own slots do put their highest level on the rarest tier, or this proves
        // nothing.
        topLevel shouldBe native.maxOf { it.maxLevel }
        // The signature family is common within the cohort, so it must not have taken that band.
        appended.first().maxLevel shouldBeLessThan topLevel
      }

      test("route 228 puts an excadrill in the only sandstorm in sinnoh") {
        // Added on the owner's call 2026-09-06: Sand Rush and Sand Force need a sandstorm, and
        // this is the one map in Sinnoh that runs one, so without Drilbur here those two abilities
        // could not be met in a wild fight at all.
        val route228 = map("route_228")
        val rolled = WildExpansion.rolledSlots(route228, landOf("route_228"), morning)

        // The band is level 49 upwards and Drilbur becomes Excadrill at 31, so it is the evolved
        // form that turns up, which is the one that carries Sand Rush.
        rolled.map { it.speciesId } shouldContain 530
        rolled.map { it.speciesId } shouldNotContain 529
        // And it is rare like everything else appended.
        rolled.first { it.speciesId == 530 }.weight shouldBeLessThanOrEqual 2
      }

      test("a ported map keeps its own cartridge's animals") {
        // Johto and Kanto travel as region 3 as well, so the guard is MapDef.ported and not the
        // region byte. Nothing here may reach them.
        val ported = maps.all().filter { it.ported }
        ported.shouldNotBeEmpty()
        for (map in ported.take(200)) {
          for (method in EncounterMethod.entries) {
            WildExpansion.cohortsFor(map, method) shouldBe emptyList()
          }
        }
      }
    })

/**
 * Whether a rule name reaches any real map, checked the way the rule itself is: a whole name has to
 * Be one of them, a prefix only has to start one.
 */
private fun matches(rule: Pair<String, Boolean>, names: Set<String>): Boolean {
  val (name, prefix) = rule
  return if (prefix) names.any { it.startsWith(name) } else name in names
}

private val WATER_METHODS =
    listOf(
        EncounterMethod.WATER,
        EncounterMethod.OLD_ROD,
        EncounterMethod.GOOD_ROD,
        EncounterMethod.SUPER_ROD,
    )
