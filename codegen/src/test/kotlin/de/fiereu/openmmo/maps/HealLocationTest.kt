package de.fiereu.openmmo.maps

import de.fiereu.openmmo.common.HealLocation
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
    })
