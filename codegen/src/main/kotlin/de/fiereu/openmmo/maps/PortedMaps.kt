package de.fiereu.openmmo.maps

import de.fiereu.openmmo.common.enums.Lighting
import de.fiereu.openmmo.common.enums.MapType
import de.fiereu.openmmo.common.enums.Weather
import de.fiereu.openmmo.maps.generated.ported.terrain.GOLDENROD
import de.fiereu.openmmo.maps.generated.ported.terrain.GOLDENROD_POKECENTER_1F

/**
 * Maps that reached the client out of another cartridge and have no generated source here yet.
 */
object PortedMaps {

  fun loadInto(maps: MapManager) {
    // Goldenrod City, out of Heart Gold / SoulSilver. Six cells of that game's 47x17
    // overworld, cut into a 3x2 matrix of their own, which is 96x64 tiles.
    maps.register(
        MapDef(
            regionId = 3,
            bankId = 2,
            mapId = 82,
            width = GOLDENROD.width,
            height = GOLDENROD.height,
            lighting = Lighting.REGULAR,
            weather = Weather.REGULAR_WEATHER,
            mapType = MapType.CITY,
            terrain = GOLDENROD,
        ))

    // Goldenrod's Pokemon Center, out of the same cartridge and the same package. An interior
    // sits on a matrix of its own rather than on the overworld, so this one is a single 32x32
    // cell.
    maps.register(
        MapDef(
            regionId = 3,
            bankId = 2,
            mapId = 83,
            width = GOLDENROD_POKECENTER_1F.width,
            height = GOLDENROD_POKECENTER_1F.height,
            lighting = Lighting.REGULAR,
            weather = Weather.REGULAR_WEATHER,
            mapType = MapType.INSIDE,
            terrain = GOLDENROD_POKECENTER_1F,
        ))
  }
}
