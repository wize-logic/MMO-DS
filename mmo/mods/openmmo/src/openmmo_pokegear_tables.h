/* The shapes of HeartGold's Pokegear tables. */
#ifndef OPENMMO_POKEGEAR_TABLES_H
#define OPENMMO_POKEGEAR_TABLES_H

#include "openmmo_pokegear.h"

#define PGMAP_NUM_FLYPOINTS 27
#define PGMAP_NUM_LOCATIONS 100
#define PGMAP_NUM_LANDINGS 30
#define POKEGEAR_PHONE_SCRIPTS 456
#define POKEGEAR_PHONE_CONTACTS 75

/* A fly point (MapFlypointParam): the town named, the map warped to, its
 * flag, its cell on the plane, and the panel patch it wears unvisited. */
typedef struct MapFlypointParam {
    u16 mapIDforName;
    u16 mapIDforWarp;
    u8 flypoint;
    u8 unk_05;
    u8 x;
    u8 y;
    u8 srcX;
    u8 srcY;
    u8 width : 4;
    u8 height : 4;
    u8 destWidth : 4;
    u8 destHeight : 4;
    u8 destX : 4;
    u8 destY : 4;
} MapFlypointParam;

/* A place the map knows (PokegearMapLocationSpec): HeartGold's map id, the
 * cell rect, the marker's offset inside it, its flavor text row and the
 * detail panel's patch. */
typedef struct PokegearMapLocationSpec {
    u16 mapId;
    u8 x;
    u8 y;
    u16 width : 4;
    u16 height : 4;
    u16 objXoffset : 4;
    u16 objYoffset : 4;
    u8 flavorText;
    u8 blockID;
    u8 srcX;
    u8 srcY;
    u8 destWidth;
    u8 destHeight;
} PokegearMapLocationSpec;

/* Where a Fly lands (sSpawnMaps, the fly columns): HeartGold's map and its
 * tile on the source's own matrix. */
typedef struct FlyLanding {
    u16 mapId;
    u16 x;
    u16 y;
} FlyLanding;

/* One phone script (PhoneCallScriptDef): the line for each player, off the
 * caller's own bank, and the side effect HeartGold keys on it. */
typedef struct PhoneCallScriptDef {
    u8 msgIds[2];
    u16 scriptType;
    u16 param0;
    u16 param1;
} PhoneCallScriptDef;

extern const PokegearMapLocationSpec gPokegearLocationSpecs[PGMAP_NUM_LOCATIONS];
extern const MapFlypointParam gPokegearFlypoints[PGMAP_NUM_FLYPOINTS];
extern const FlyLanding gPokegearFlyLandings[PGMAP_NUM_LANDINGS];
extern const u16 gPokegearAlphMaps[5];
extern const u16 gPokegearTalkFilter[14];
extern const u8 gPokegearMomCallIntro[POKEGEAR_HG_MAPS];
extern const PhoneCallScriptDef gPokegearPhoneScripts[POKEGEAR_PHONE_SCRIPTS];
extern const u8 gPokegearGreetings[8][12];
extern const u16 gPokegearContactBanks[POKEGEAR_PHONE_CONTACTS];
extern const u16 gPokegearFriendMaps[73];
extern const u8 gPokegearFriendMapKinds[73];

#endif
