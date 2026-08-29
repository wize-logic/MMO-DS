package de.fiereu.openmmo.server.game.services.command

/** Where the game itself puts a player who flies to a town. */
data class FlyPoint(val bankId: Int, val mapId: Int, val x: Int, val y: Int)

/** Sinnoh is region 3, the only region this table covers. */
const val FLY_POINT_REGION = 3

val FLY_POINTS: Map<Int, FlyPoint> =
    listOf(
            FlyPoint(1, 155, 116, 886), // twinleaf_town
            FlyPoint(1, 162, 177, 843), // sandgem_town
            FlyPoint(1, 170, 176, 667), // floaroma_town
            FlyPoint(1, 177, 566, 657), // solaceon_town
            FlyPoint(1, 186, 472, 539), // celestic_town
            FlyPoint(0, 3, 180, 777), // jubilife_city
            FlyPoint(0, 33, 58, 723), // canalave_city
            FlyPoint(0, 45, 303, 757), // oreburgh_city
            FlyPoint(0, 65, 305, 531), // eterna_city
            FlyPoint(0, 86, 465, 698), // hearthome_city
            FlyPoint(0, 120, 600, 816), // pastoria_city
            FlyPoint(0, 132, 717, 612), // veilstone_city
            FlyPoint(0, 150, 860, 785), // sunyshore_city
            FlyPoint(0, 165, 379, 234), // snowpoint_city
            FlyPoint(0, 172, 842, 599), // pokemon_league
            FlyPoint(0, 188, 647, 430), // fight_area
            FlyPoint(1, 194, 659, 339), // survival_area
            FlyPoint(1, 201, 802, 473), // resort_area
            FlyPoint(1, 136, 306, 910), // route_221
        )
        .associateBy { (it.bankId shl 8) or it.mapId }
