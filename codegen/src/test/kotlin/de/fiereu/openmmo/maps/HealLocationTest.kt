package de.fiereu.openmmo.maps

import de.fiereu.openmmo.common.FlyLocation
import de.fiereu.openmmo.common.HealLocation
import de.fiereu.openmmo.maps.generated.sinnoh.SpawnLocations
import io.kotest.core.spec.style.FunSpec
import io.kotest.matchers.nulls.shouldNotBeNull
import io.kotest.matchers.shouldBe

/**
 * A map's heal location is a join across two decomp files, the `setrespawn` in the map's scripts
 * and the tile that constant names in `heal_locations.json`, so the values below are checked
 * against both decomps by hand rather than against anything this build produced.
 */
class HealLocationTest :
    FunSpec({
      val maps = MapManager()

      test("a Hoenn Pokemon Center sets the respawn its town owns") {
        // OldaleTown_PokemonCenter_1F: setrespawn HEAL_LOCATION_OLDALE_TOWN, MAP_OLDALE_TOWN 6,17.
        maps.getMap(1, 52, 2).shouldNotBeNull().healLocation shouldBe
            HealLocation(regionId = 1, bankId = 50, mapId = 10, x = 6, y = 17)
      }

      test("a Kanto Pokemon Center sets the respawn its city owns") {
        // ViridianCity_PokemonCenter_1F: setrespawn HEAL_LOCATION_VIRIDIAN_CITY, 26,27.
        maps.getMap(0, 5, 4).shouldNotBeNull().healLocation shouldBe
            HealLocation(regionId = 0, bankId = 3, mapId = 1, x = 26, y = 27)
      }

      test("the town itself sets no respawn, only the Center inside it does") {
        maps.getMap(1, 50, 10).shouldNotBeNull().healLocation shouldBe null
      }

      test("a map that sets a different respawn down each branch carries none") {
        // InsideOfTruck picks the player's own bedroom by gender, which is a script's decision.
        maps.getMap(1, 75, 40).shouldNotBeNull().healLocation shouldBe null
      }

      // Platinum has no setrespawn: sSpawnLocations[] in src/spawn_locations.c, indexed by the
      // save's black-out warp id, and entering a row's map sets that id. Header numbers are the
      // row's index in generated/map_headers.txt, split bank:map as the wire does.
      test("a Sinnoh Pokemon Center sets the respawn the spawn table gives it") {
        // Row 2: MAP_HEADER_SANDGEM_TOWN_POKECENTER_1F (header 420, bank 1 map 164) at 8,6.
        maps.getMap(3, 1, 164).shouldNotBeNull().healLocation shouldBe
            HealLocation(regionId = 3, bankId = 1, mapId = 164.toByte(), x = 8, y = 6)
      }

      test("the player's own house in Twinleaf is the first row, at the bed") {
        // Row 1: MAP_HEADER_TWINLEAF_TOWN_PLAYER_HOUSE_1F (header 414, bank 1 map 158) at 8,8.
        maps.getMap(3, 1, 158).shouldNotBeNull().healLocation shouldBe
            HealLocation(regionId = 3, bankId = 1, mapId = 158.toByte(), x = 8, y = 8)
      }

      test("a Sinnoh town sets no respawn, only the Center inside it does") {
        // MAP_HEADER_TWINLEAF_TOWN is header 411, bank 1 map 155.
        maps.getMap(3, 1, 155).shouldNotBeNull().healLocation shouldBe null
      }

      test("the spawn table answers a warp id by row and a row by its location") {
        SpawnLocations.ROWS.size shouldBe 20
        SpawnLocations.healLocation(2) shouldBe
            HealLocation(regionId = 3, bankId = 1, mapId = 164.toByte(), x = 8, y = 6)
        SpawnLocations.idFor(SpawnLocations.healLocation(14)) shouldBe 14
        SpawnLocations.healLocation(0) shouldBe null
        SpawnLocations.healLocation(21) shouldBe null
        SpawnLocations.idFor(
            HealLocation(regionId = 3, bankId = 1, mapId = 155.toByte(), x = 116, y = 886)) shouldBe
            null
      }

      // The same table's Fly half. The flag ids are the decomp's own FLAG_FIRST_ARRIVAL_* values
      // (generated/vars_flags.h: Twinleaf 2480, Jubilife 2487), which is what the client's VM
      // reports and what the destination is gated on.
      test("the spawn table's Fly half lands where the cartridge lands one") {
        SpawnLocations.FLY.size shouldBe SpawnLocations.ROWS.size
        SpawnLocations.flyLocation(1) shouldBe
            FlyLocation(mapHeaderId = 411, x = 116, z = 886, firstArrivalFlagId = 2480)
        SpawnLocations.flyLocation(6) shouldBe
            FlyLocation(mapHeaderId = 3, x = 180, z = 777, firstArrivalFlagId = 2487)
        SpawnLocations.flyLocation(0) shouldBe null
        SpawnLocations.flyLocation(21) shouldBe null
      }

      test("Fly's origin is the header's own isFlyAllowed") {
        // Jubilife City (header 3) is .isFlyAllowed = TRUE; its mart (header 4) is not, and
        // neither is Oreburgh Gate's first floor (header 258, bank 1 map 2).
        maps.getMap(3, 0, 3).shouldNotBeNull().flyAllowed shouldBe true
        maps.getMap(3, 0, 4).shouldNotBeNull().flyAllowed shouldBe false
        maps.getMap(3, 1, 2).shouldNotBeNull().flyAllowed shouldBe false
      }
    })
