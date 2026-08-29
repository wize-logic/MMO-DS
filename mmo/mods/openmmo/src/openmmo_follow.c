/* The Pokemon that walks on the tile you just left. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "generated/movement_types.h"

#include "field/field_system.h"

#include "map_header.h"
#include "map_object.h"
#include "party.h"
#include "pokemon.h"
#include "save_player.h"

#include "pc_modfs.h"

#include "../../../include/follower.h"

/* The local-id band. 0x100 is the remote-avatar band and 0x200 the npc one
 * (openmmo_boot.c); this is the third, and it holds exactly one object, so the
 * band is a single number rather than a base and a slot. */
#define OPENMMO_FOLLOW_LOCALID 0x300

/* enum BillboardModel. A cooked follower row names one of these two, and which
 * one is the answer to "is this one of the big ones", so the height rule
 * reads the package rather than a size table of our own. */
#define BILLBOARD_MODEL_GENERIC_64x64 5

/* What is on the map right now, so a change of party or of map is a change of
 * one number rather than a re-derivation. -1 is "nothing seated". */
static int g_gfx = -1;

/* Why there is no follower, said once per reason. */
static const char *g_said;

static void no_follower(const char *why)
{
    if (g_said == why) {
        return;
    }
    g_said = why;
    printf("openmmo: no follower (%s)\n", why);
    fflush(stdout);
}

static void forget(void)
{
    g_gfx = -1;
}

/* Our object on this map, by the local id we gave it, or NULL. */
static MapObject *seated(FieldSystem *fs)
{
    return MapObjMan_LocalMapObjByIndex(fs->mapObjMan, OPENMMO_FOLLOW_LOCALID);
}

/* The party member that follows you. */
static Pokemon *follower_of(Party *party)
{
    int n = Party_GetCurrentCount(party);
    int i;
    Pokemon *first_alive = NULL;
    Pokemon *first_whole = NULL;

    for (i = 0; i < n; i++) {
        Pokemon *mon = Party_GetPokemonBySlotIndex(party, i);

        if (mon == NULL || Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL)) {
            continue;
        }
        if (first_whole == NULL) {
            first_whole = mon;
        }
        if (first_alive == NULL
            && Pokemon_GetValue(mon, MON_DATA_HP, NULL) > 0) {
            first_alive = mon;
        }
    }
    return first_alive != NULL ? first_alive : first_whole;
}

/* Ours, not HeartGold's, and deliberately readable in one screen: a map either
 * takes a follower or it does not, and the only thing size changes is whether a
 * Steelix fits in a room. */
static int allowed_here(enum MapHeaderID header, int large)
{
    if (MapHeader_IsUnionRoom(header)) {
        return 0;
    }
    if (large && !MapHeader_IsOutdoors(header)) {
        return 0;
    }
    return 1;
}

/* Whether the package planted this graphics id, and whether it is a big one.
 * Both answers come from the cooked row, so a package that carries only some
 * of the set is handled by the same door that carries all of it. */
static int cooked(int gfx, int *large)
{
    struct pc_modfs_billboard row;

    if (!pc_modfs_billboard(gfx, &row)) {
        return 0;
    }
    *large = (row.model == BILLBOARD_MODEL_GENERIC_64x64);
    return 1;
}

/* What should be walking behind the player this frame, or -1 for nothing. */
static int want_gfx(FieldSystem *fs, enum MapHeaderID header)
{
    Party *party;
    Pokemon *mon;
    int species, form, gender, shiny, gfx, large = 0;

    if (fs->saveData == NULL) {
        no_follower("no save");
        return -1;
    }
    party = SaveData_GetParty(fs->saveData);
    if (party == NULL) {
        no_follower("no party");
        return -1;
    }
    mon = follower_of(party);
    if (mon == NULL) {
        /* Keyed on the count rather than said once: "empty" that never changes
         * and "empty" that a server is about to fill look identical otherwise,
         * and which of the two it is was the whole question the first time this
         * was driven headless. */
        static int said_count = -1;
        int have = Party_GetCurrentCount(party);

        if (have != said_count) {
            said_count = have;
            printf("openmmo: no follower (party holds %d, none of it can"
                   " walk)\n", have);
            fflush(stdout);
        }
        return -1;
    }

    species = (int)Pokemon_GetValue(mon, MON_DATA_SPECIES, NULL);
    form = (int)Pokemon_GetValue(mon, MON_DATA_FORM, NULL);
    gender = (int)Pokemon_GetValue(mon, MON_DATA_GENDER, NULL);
    shiny = Pokemon_IsShiny(mon) ? 1 : 0;

    gfx = mmo_follower_gfx(species, form, gender, shiny);
    if (gfx < 0) {
        no_follower("this species has no follower in the table");
        return -1;
    }
    if (!cooked(gfx, &large)) {
        no_follower("no package carries this one; fill one with"
                    " tools/portfollow.py");
        return -1;
    }
    if (!allowed_here(header, large)) {
        no_follower(large ? "too big for this map" : "this map takes none");
        return -1;
    }
    g_said = NULL;
    return gfx;
}

/*
 * One settled frame. `field_ready` is the caller's own settled test, so this
 * file does not keep a second opinion about when the object table is safe.
 */
void openmmo_follow_tick(FieldSystem *fs, int field_ready);

void openmmo_follow_tick(FieldSystem *fs, int field_ready)
{
    enum MapHeaderID header;
    MapObject *obj;
    MapObject *player;
    int want;

    if (fs == NULL || fs->mapObjMan == NULL) {
        forget();
        return;
    }
    if (!field_ready) {
        /* The map going away is what ends a seat, not a dialog box: a message
         * is a field task and the objects come back after it. Only a field that
         * is no longer running its map has really lost ours. */
        if (!FieldSystem_IsRunningFieldMap(fs)) {
            forget();
        }
        return;
    }

    header = fs->location != NULL ? fs->location->mapHeaderID
                                  : (enum MapHeaderID)0;
    want = want_gfx(fs, header);
    obj = seated(fs);

    if (want < 0) {
        if (obj != NULL) {
            printf("openmmo: follower put away\n");
            fflush(stdout);
            MapObject_Delete(obj);
        }
        forget();
        return;
    }

    if (obj != NULL) {
        /* A lead that changed is a different picture on the same object, which
         * is what HeartGold's FollowMon_ChangeMon does rather than respawning:
         * a respawn would put the follower back on the player's tile and lose
         * the step it was in the middle of. */
        if (g_gfx != want) {
            MapObject_SetGraphicsID(obj, (u32)want);
            printf("openmmo: follower gfx %d -> %d\n", g_gfx, want);
            fflush(stdout);
            g_gfx = want;
        }
        return;
    }

    /* Nothing seated. */
    player = MapObjectMan_GetPlayerMapObject(fs->mapObjMan);
    if (player == NULL) {
        return;
    }

    obj = MapObjectMan_AddMapObject(fs->mapObjMan,
                                    MapObject_GetX(player),
                                    MapObject_GetZ(player),
                                    MapObject_GetFacingDir(player),
                                    want, MOVEMENT_TYPE_FOLLOW_PLAYER, header);
    if (obj == NULL) {
        /* The table is full. A missing follower is a missing sprite; the
         * engine's own answer to a full table is a NULL it writes through, and
         * the crowd cull in openmmo_boot.c is what keeps us away from it. */
        return;
    }
    MapObject_SetLocalID(obj, (u32)OPENMMO_FOLLOW_LOCALID);
    g_gfx = want;
    printf("openmmo: follower gfx %d at (%d,%d)\n", want,
           MapObject_GetX(obj), MapObject_GetZ(obj));
    fflush(stdout);
}

/* Called when the world is left, so a new session does not believe in the last
 * one's follower. */
void openmmo_follow_reset(void);

void openmmo_follow_reset(void)
{
    forget();
    g_said = NULL;
}
