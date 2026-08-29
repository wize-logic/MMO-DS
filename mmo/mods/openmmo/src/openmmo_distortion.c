/*
 * Distortion World and platform-lift rooms store their overlay id in the engine's 36-byte
 * PersistedMapFeatures save slot.
 */
#include <stdio.h>

#include "constants/field/dynamic_map_features.h"
#include "field/field_system.h"
#include "field_system.h"
#include "generated/map_headers.h"
#include "persisted_map_features_init.h"
#include "platform_lift.h"

void openmmo_prepare_dynamic_map_features(FieldSystem *fieldSystem)
{
    enum MapHeaderID header;

    if (fieldSystem == NULL || fieldSystem->location == NULL)
        return;

    header = fieldSystem->location->mapHeaderID;

    if (header >= MAP_HEADER_DISTORTION_WORLD_1F
        && header <= MAP_HEADER_DISTORTION_WORLD_TURNBACK_CAVE_ROOM) {
        if (!PersistedMapFeatures_IsCurrentDynamicMap(
                fieldSystem, DYNAMIC_MAP_FEATURES_DISTORTION_WORLD)) {
            PersistedMapFeatures_InitForDistortionWorld(fieldSystem);
            printf("openmmo: distortion-world features for header %d\n", (int)header);
        }
        return;
    }

    switch (header) {
    case MAP_HEADER_IRON_ISLAND_B1F_RIGHT_ROOM:
    case MAP_HEADER_IRON_ISLAND_B2F_LEFT_ROOM:
    case MAP_HEADER_IRON_ISLAND_B3F:
    case MAP_HEADER_POKEMON_LEAGUE_ELEVATOR_TO_AARON_ROOM:
    case MAP_HEADER_POKEMON_LEAGUE_ELEVATOR_TO_BERTHA_ROOM:
    case MAP_HEADER_POKEMON_LEAGUE_ELEVATOR_TO_FLINT_ROOM:
    case MAP_HEADER_POKEMON_LEAGUE_ELEVATOR_TO_LUCIAN_ROOM:
    case MAP_HEADER_POKEMON_LEAGUE_ELEVATOR_TO_CHAMPION_ROOM:
    case MAP_HEADER_POKEMON_LEAGUE_CHAMPION_ROOM:
        if (!PersistedMapFeatures_IsCurrentDynamicMap(
                fieldSystem, DYNAMIC_MAP_FEATURES_PLATFORM_LIFT_ROOM)) {
            PersistedMapFeatures_InitForPlatformLift(fieldSystem);
            printf("openmmo: platform-lift features for header %d\n", (int)header);
        }
        return;
    default:
        return;
    }
}

void openmmo_on_distortion_floor_change(int header)
{
    printf("openmmo: distortion-world floor header %d\n", header);
}
