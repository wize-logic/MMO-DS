/* What a field move meets on a ported map. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "field/field_system.h"
#include "field_system.h"
#include "map_matrix.h"
#include "player_avatar.h"
#include "script_manager.h"
#include "terrain_collision_manager.h"

#include "../../../include/client.h"
#include "../../../include/idmap.h"

#define FIELDMOVE_BASE 24000            /* portmap.FIELDMOVE_BASE */
#define FIELDMOVE_ENTRY_HEADBUTT 0
#define FIELDMOVE_ENTRY_RUBBLE 1
#define FIELDMOVE_VERDICT_FRAMES 600
/* HeartGold's TILE_BEHAVIOR_HEADBUTT. This game names nothing at 0x06, so the
 * porter carries the byte as it is (mmo/TERRAIN_MAP, hgonly-inert). */
#define HG_TILE_BEHAVIOR_HEADBUTT 0x06
/* The porter appends a ported header after the engine's own last one. */
#define PORTED_HEADER_FIRST 594
/* StartDestroyObstacleAnimation's kinds: this game's two, and the tree. */
#define OBSTACLE_ROCK 1
#define OBSTACLE_HEADBUTT 3
#define HEADBUTT_LAND_MAX 8

extern int openmmo_underground_active(void); /* openmmo_underground.c */

static openmmo_client *s_client;
static int s_asked;          /* which kind went up, or 0 */
static int s_frames;
static int s_rubble_item;    /* an item the rubble held, waiting for the field; 0 none */
static int s_loaded;
static int s_land[HEADBUTT_LAND_MAX];  /* the all-trees land members, appended */
static int s_lands;

void openmmo_fieldmove_attach(openmmo_client *c)
{
    s_client = c;
    s_asked = 0;
    s_rubble_item = 0;
}

static int session_up(void)
{
    const openmmo_world_state *ws;

    if (s_client == NULL)
        return 0;
    ws = openmmo_client_world_state(s_client);
    return ws != NULL && ws->valid;
}

static int on_ported_map(const FieldSystem *fs)
{
    return fs != NULL && fs->location != NULL
        && fs->location->mapHeaderID >= PORTED_HEADER_FIRST;
}

/* The all-trees land member HeartGold headbutts anywhere on, by the numbers
 * this package gave its copies (portmap's field_effects.txt, beside the
 * shake's own members). */
static void load(void)
{
    const char *dir = getenv("PC_MODS_DIR");
    const char *mods = getenv("PC_MODS");
    char list[256];
    char *name, *save = NULL;

    s_loaded = 1;
    if (dir == NULL || mods == NULL)
        return;
    snprintf(list, sizeof list, "%s", mods);
    for (name = strtok_r(list, ",", &save); name != NULL;
         name = strtok_r(NULL, ",", &save)) {
        char path[512];
        FILE *f;
        char key[24];
        int a, b;

        snprintf(path, sizeof path, "%s/%s/.cooked/generated/field_effects.txt",
                 dir, name);
        f = fopen(path, "r");
        if (f == NULL)
            continue;
        while (fscanf(f, "%23s %d", key, &a) == 2) {
            if (strcmp(key, "headbutt_anim") == 0 && fscanf(f, "%d", &b) != 1)
                break;
            if (strcmp(key, "headbutt_land") == 0 && s_lands < HEADBUTT_LAND_MAX)
                s_land[s_lands++] = a;
        }
        fclose(f);
    }
}

/* The tile in front of the player. */
static void facing_tile(const FieldSystem *fs, int *x, int *z)
{
    *x = PlayerAvatar_GetXPos(fs->playerAvatar);
    *z = PlayerAvatar_GetZPos(fs->playerAvatar);
    switch (PlayerAvatar_GetFacingDir(fs->playerAvatar)) {
    case 0: (*z)--; break;
    case 1: (*z)++; break;
    case 2: (*x)--; break;
    case 3: (*x)++; break;
    default: break;
    }
}

/*
 * The A press on a ported map, asked after the map's own events: is the tile in front a
 * headbutt tree?
 */
int openmmo_fieldmove_tree_try(FieldSystem *fs, int *event)
{
    int x, z, i;

    if (!on_ported_map(fs) || fs->mapMatrix == NULL)
        return 0;
    if (!s_loaded)
        load();
    facing_tile(fs, &x, &z);
    if (x < 0 || z < 0)
        return 0;
    if (TerrainCollisionManager_GetTileBehavior(fs, x, z) != HG_TILE_BEHAVIOR_HEADBUTT) {
        int width = MapMatrix_GetWidth(fs->mapMatrix);
        int height = MapMatrix_GetHeight(fs->mapMatrix);
        int land;

        if (width <= 0 || x / 32 >= width || z / 32 >= height)
            return 0;
        land = MapMatrix_GetLandDataIDByIndex(x / 32 + (z / 32) * width, fs->mapMatrix);
        for (i = 0; i < s_lands; i++)
            if (s_land[i] == land)
                break;
        if (i == s_lands)
            return 0;
    }
    *event = FIELDMOVE_BASE + FIELDMOVE_ENTRY_HEADBUTT;
    return 1;
}

/* StartDestroyObstacleAnimation's hook: an obstacle in front of the player is
 * going. A rock's rubble and a tree's shake are the server's to answer; the
 * cut tree is nobody's. */
void openmmo_fieldmove_obstacle(FieldSystem *fs, int obstacle)
{
    int kind;

    if (obstacle == OBSTACLE_ROCK)
        kind = MMO_UG_TALK_ROCK_SMASH;
    else if (obstacle == OBSTACLE_HEADBUTT)
        kind = MMO_UG_TALK_HEADBUTT;
    else
        return;
    if (!session_up() || !on_ported_map(fs))
        return;
    if (openmmo_client_ug_talk_send(s_client, kind, 0, NULL, 0) != 0) {
        printf("openmmo: field move: the %s could not be reported\n",
               obstacle == OBSTACLE_ROCK ? "smash" : "headbutt");
        return;
    }
    s_asked = kind;
    s_frames = 0;
    printf("openmmo: field move: %s reported, asking the server\n",
           obstacle == OBSTACLE_ROCK ? "a rock smashed" : "a tree headbutted");
}

/* One of the two verdicts arrived. */
void openmmo_fieldmove_recv(const mmo_underground_talk *msg)
{
    int verdict;

    if (msg == NULL)
        return;
    if (msg->kind != MMO_UG_TALK_ROCK_SMASH_VERDICT
        && msg->kind != MMO_UG_TALK_HEADBUTT_VERDICT)
        return;
    if (!s_asked) {
        printf("openmmo: field move: a verdict with nothing asked\n");
        return;
    }
    s_asked = 0;
    verdict = msg->len > 0 ? msg->data[0] : 0;
    if (verdict == 1) {
        printf("openmmo: field move: something is coming\n");
        return;
    }
    if (verdict == 2 && msg->kind == MMO_UG_TALK_ROCK_SMASH_VERDICT && msg->len >= 3) {
        /* The server names the item as the wire does; the bank's entry and
         * the obtain routine behind it want this engine's own id, and the
         * report the obtain sends back is translated the other way by the
         * bag (openmmo_bag.c), as every gift's is. */
        u16 wire = (u16)(msg->data[1] | (msg->data[2] << 8));
        const char *why = NULL;
        u16 engine = mmo_id_item_from_server(wire, &why);

        if (engine == 0) {
            printf("openmmo: field move: item %u was in the rubble, and this build "
                   "cannot name it%s%s\n", wire, why ? ": " : "", why ? why : "");
            return;
        }
        s_rubble_item = engine;
        printf("openmmo: field move: item %u (engine %u) was in the rubble\n", wire, engine);
        return;
    }
    printf("openmmo: field move: nothing came of it\n");
}

/* Every underground-talk kind, to whichever module owns it. The cavern's own
 * pump takes over while the player is down there; up here whichever module
 * has something live drains the queue through this. */
void openmmo_talk_route(const mmo_underground_talk *msg)
{
    extern void openmmo_fishing_recv(const mmo_underground_talk *m);
    extern void openmmo_apricorn_recv(const mmo_underground_talk *m);

    if (msg == NULL)
        return;
    if (msg->kind >= MMO_UG_TALK_ROCK_SMASH)
        openmmo_fieldmove_recv(msg);
    else if (msg->kind >= MMO_UG_TALK_APRICORN_PICK)
        openmmo_apricorn_recv(msg);
    else
        openmmo_fishing_recv(msg);
}

static void pump(void)
{
    mmo_underground_talk msg;

    if (s_client == NULL || openmmo_underground_active())
        return;
    while (openmmo_client_ug_talk_recv(s_client, &msg))
        openmmo_talk_route(&msg);
}

/* Once a frame. The rubble's item is handed over the moment the field is free
 * the smash's own routine has to have ended, through the bank's rubble
 * entry, with the item and its count where this game's obtain routine reads
 * them. */
void openmmo_fieldmove_tick(FieldSystem *fs)
{
    if (s_asked) {
        pump();
        if (s_asked && ++s_frames > FIELDMOVE_VERDICT_FRAMES) {
            printf("openmmo: field move: no verdict in %d frames\n",
                   FIELDMOVE_VERDICT_FRAMES);
            s_asked = 0;
        }
    }
    if (s_rubble_item == 0)
        return;
    if (fs == NULL || fs->task != NULL || !FieldSystem_IsRunningFieldMap(fs)
        || FieldSystem_HasChildProcess(fs))
        return;
    ScriptManager_Set(fs, (u16)(FIELDMOVE_BASE + FIELDMOVE_ENTRY_RUBBLE), NULL);
    FieldSystem_SetScriptParameters(fs, (u16)s_rubble_item, 1, 0, 0);
    s_rubble_item = 0;
}
