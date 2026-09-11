package de.fiereu.openmmo.maps

import de.fiereu.openmmo.common.enums.EncounterMethod
import de.fiereu.openmmo.common.enums.TimeOfDay
import de.fiereu.openmmo.maps.generated.sinnoh.DailyEncounters
import io.kotest.assertions.throwables.shouldThrow
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

      test("every variant the archive carries is carried through to the table") {
        val table = landTable()

        val byVariant = table.overrides.associateBy { it.variant }
        byVariant.keys shouldBe EncounterVariant.entries.toSet()
        byVariant.getValue(EncounterVariant.SWARM).slots shouldBe listOf(0, 1)
        byVariant.getValue(EncounterVariant.RADAR).slots shouldBe listOf(4, 5, 10, 11)
        byVariant.getValue(EncounterVariant.DUAL_SLOT_FIRERED).slots shouldBe listOf(8, 9)
        // Route 201's FireRed pair is Growlithe, which is on none of its other lists.
        byVariant.getValue(EncounterVariant.DUAL_SLOT_FIRERED).speciesIds shouldBe listOf(58, 58)
      }

      test("a swarm, a shaking radar patch and a second cartridge each write their own slots") {
        val table = landTable()
        val plain = table.slotsAt(TimeOfDay.MORNING)

        val swarm = table.slotsAt(EncounterConditions(TimeOfDay.MORNING, swarming = true))
        val swarmed = table.overrides.first { it.variant == EncounterVariant.SWARM }.speciesIds
        swarm[0].speciesId shouldBe swarmed[0]
        swarm[1].speciesId shouldBe swarmed[1]
        (2 until 12).forEach { swarm[it] shouldBe plain[it] }

        val radar = table.slotsAt(EncounterConditions(TimeOfDay.MORNING, radarPatch = true))
        val radared = table.overrides.first { it.variant == EncounterVariant.RADAR }.speciesIds
        listOf(4, 5, 10, 11).forEachIndexed { i, slot -> radar[slot].speciesId shouldBe radared[i] }
        (0 until 12)
            .filterNot { it in listOf(4, 5, 10, 11) }
            .forEach { radar[it] shouldBe plain[it] }

        val firered =
            table.slotsAt(
                EncounterConditions(
                    TimeOfDay.MORNING, dualSlot = EncounterVariant.DUAL_SLOT_FIRERED))
        firered[8].speciesId shouldBe 58
        firered[9].speciesId shouldBe 58
        (0 until 12).filterNot { it == 8 || it == 9 }.forEach { firered[it] shouldBe plain[it] }
      }

      test("the four families write into disjoint slots, so all of them can hold at once") {
        val table = landTable()

        val all =
            table.slotsAt(
                EncounterConditions(
                    TimeOfDay.NIGHT,
                    swarming = true,
                    radarPatch = true,
                    dualSlot = EncounterVariant.DUAL_SLOT_FIRERED,
                ))
        val night = table.slotsAt(TimeOfDay.NIGHT)
        val swarm = table.slotsAt(EncounterConditions(TimeOfDay.NIGHT, swarming = true))
        // Each family's own positions read the same whether it is alone or in company.
        listOf(2, 3).forEach { all[it] shouldBe night[it] }
        listOf(0, 1).forEach { all[it] shouldBe swarm[it] }
        all[8].speciesId shouldBe 58
        all[4].speciesId shouldBe
            table.overrides.first { it.variant == EncounterVariant.RADAR }.speciesIds[0]
        // Slots 6 and 7 belong to the archive the map does not carry, so nothing writes them.
        listOf(6, 7).forEach { all[it] shouldBe table.slots[it] }
      }

      test("the day's pair is written over slots 6 and 7 and over nothing else") {
        // The Great Marsh's first area, whose grass table is a full twelve like any other.
        val map = MapManager().getMap(sinnoh, 1, 248)
        map.shouldNotBeNull()
        val table = map.encounterTable(EncounterMethod.LAND)
        table.shouldNotBeNull()

        val plain = table.slotsAt(TimeOfDay.MORNING)
        val rotated =
            table.slotsAt(EncounterConditions(TimeOfDay.MORNING, dailySpecies = listOf(194, 195)))
        rotated[6].speciesId shouldBe 194
        rotated[7].speciesId shouldBe 195
        // The slot keeps its own level and weight: only the species is replaced.
        rotated[6].copy(speciesId = plain[6].speciesId) shouldBe plain[6]
        (0..11).filterNot { it == 6 || it == 7 }.forEach { rotated[it] shouldBe plain[it] }
        // A table too short to have those slots is left alone, which is every water and rod table.
        val water = map.encounterTable(EncounterMethod.WATER)
        water.shouldNotBeNull()
        water.slotsAt(
            EncounterConditions(TimeOfDay.MORNING, dailySpecies = listOf(194, 195))) shouldBe
            water.slots
      }

      test("the extra archive's daily lists are the cartridge's own") {
        // The index is five bits of one roll per area, so each list is a full thirty-two, and the
        // six marsh headers are 504 through 509 in the order the areas are numbered.
        DailyEncounters.AREA_BITS shouldBe 5
        DailyEncounters.AREA_MASK shouldBe 31
        DailyEncounters.MARSH_NATIONAL_DEX.size shouldBe 32
        DailyEncounters.MARSH_LOCAL.size shouldBe 32
        DailyEncounters.MARSH_AREAS shouldBe listOf(504, 505, 506, 507, 508, 509)
        // Before the national dex the marsh is mostly Wooper; after it, it is not.
        DailyEncounters.MARSH_LOCAL[0] shouldBe 194
        DailyEncounters.MARSH_NATIONAL_DEX[0] shouldBe 454
        // NUM_TROPHY_GARDEN_SPECIAL_MONS, and Eevee heads the list.
        DailyEncounters.TROPHY_GARDEN_MONS.size shouldBe 16
        DailyEncounters.TROPHY_GARDEN_MONS[0] shouldBe 133
        DailyEncounters.TROPHY_GARDEN shouldBe 287
      }

      test("only a second-cartridge variant may be the second cartridge") {
        shouldThrow<IllegalArgumentException> {
          EncounterConditions(TimeOfDay.MORNING, dualSlot = EncounterVariant.SWARM)
        }
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
