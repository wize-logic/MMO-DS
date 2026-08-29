package de.fiereu.openmmo.maps

import de.fiereu.openmmo.common.enums.EncounterMethod
import de.fiereu.openmmo.common.enums.TimeOfDay
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe

/**
 * A Gen 4 grass table is not one list per condition: it is one list of twelve slots whose *species*
 * are swapped out before the roll, at slot indices the game hardcodes.
 */
class SinnohEncounterTest :
    FunSpec({
      val sinnoh = 3
      val route201 = Triple(sinnoh, 1, 86)

      val starly = 396
      val bidoof = 399
      val kricketot = 401

      fun landTable(): WildEncounterTable {
        val map = MapManager().getMap(route201.first, route201.second, route201.third)
        map.shouldNotBeNull()
        val table = map.encounterTable(EncounterMethod.LAND)
        table.shouldNotBeNull()
        return table
      }

      test("the grass table is the twelve slots the game rolls against") {
        val table = landTable()

        table.slots.size shouldBe 12
        table.encounterRate shouldBe 30
        // GetGroundEncounterSlot's cut points, as weights.
        table.slots.map { it.weight } shouldBe listOf(20, 20, 10, 10, 10, 10, 5, 5, 4, 4, 1, 1)
      }

      test("the table as authored is the morning one") {
        val table = landTable()

        table.slotsAt(TimeOfDay.MORNING) shouldBe table.slots
        table.slots[2].speciesId shouldBe starly
        table.slots[3].speciesId shouldBe kricketot
      }

      test("day and night swap slots 2 and 3 and leave the levels alone") {
        val table = landTable()

        val day = table.slotsAt(TimeOfDay.DAY)
        day[2].speciesId shouldBe starly
        day[3].speciesId shouldBe bidoof
        val night = table.slotsAt(TimeOfDay.NIGHT)
        night[2].speciesId shouldBe kricketot
        night[3].speciesId shouldBe bidoof

        // The game writes over the slot's species and nothing else, so a Bidoof met at night on
        // route 201 is level 3 because slot 3 is, not because Bidoof is.
        for (i in 0 until 12) {
          night[i].minLevel shouldBe table.slots[i].minLevel
          night[i].maxLevel shouldBe table.slots[i].maxLevel
          night[i].weight shouldBe table.slots[i].weight
        }
        // Every slot but the two is the table's own.
        (0 until 12).filter { it != 2 && it != 3 }.forEach { night[it] shouldBe table.slots[it] }
      }

      test("twilight rolls the day table and late night the night one") {
        val table = landTable()

        table.slotsAt(TimeOfDay.TWILIGHT) shouldBe table.slotsAt(TimeOfDay.DAY)
        table.slotsAt(TimeOfDay.LATE_NIGHT) shouldBe table.slotsAt(TimeOfDay.NIGHT)
      }

      test("the variants the server cannot yet decide are carried, not dropped") {
        val table = landTable()

        val byVariant = table.overrides.associateBy { it.variant }
        byVariant.keys shouldBe EncounterVariant.entries.toSet()
        byVariant.getValue(EncounterVariant.SWARM).slots shouldBe listOf(0, 1)
        byVariant.getValue(EncounterVariant.RADAR).slots shouldBe listOf(4, 5, 10, 11)
        byVariant.getValue(EncounterVariant.DUAL_SLOT_FIRERED).slots shouldBe listOf(8, 9)
        // Route 201's FireRed pair is Growlithe, which is on none of its other lists.
        byVariant.getValue(EncounterVariant.DUAL_SLOT_FIRERED).speciesIds shouldBe listOf(58, 58)
      }

      test("the hour buckets are the game's own") {
        // rtc.c's 24 entry lookup: late night runs to 4, morning to 10, day to 17, twilight to 20.
        TimeOfDay.forHour(0) shouldBe TimeOfDay.LATE_NIGHT
        TimeOfDay.forHour(3) shouldBe TimeOfDay.LATE_NIGHT
        TimeOfDay.forHour(4) shouldBe TimeOfDay.MORNING
        TimeOfDay.forHour(9) shouldBe TimeOfDay.MORNING
        TimeOfDay.forHour(10) shouldBe TimeOfDay.DAY
        TimeOfDay.forHour(16) shouldBe TimeOfDay.DAY
        TimeOfDay.forHour(17) shouldBe TimeOfDay.TWILIGHT
        TimeOfDay.forHour(19) shouldBe TimeOfDay.TWILIGHT
        TimeOfDay.forHour(20) shouldBe TimeOfDay.NIGHT
        TimeOfDay.forHour(23) shouldBe TimeOfDay.NIGHT
      }

      test("a map whose archive names a sea carries the form it names") {
        // Header 442 is Celestic Town, bank 1 map 186. Its archive sets both form words to 100, so
        // its Shellos and Gastrodon are the east sea ones: CreateWildMon reads zero as west and
        // anything else as east.
        val celestic = MapManager().getMap(sinnoh, 1, 186)
        celestic.shouldNotBeNull()
        val forms = celestic.wildEncounterForms
        forms.shouldNotBeNull()
        forms.shellosForm shouldBe 100
        forms.gastrodonForm shouldBe 100
        // The three words the game never reads are carried in file order rather than named.
        forms.unusedFormWords shouldBe listOf(0, 0, 0)
      }
    })
