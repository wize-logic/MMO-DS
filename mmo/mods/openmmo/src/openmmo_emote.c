/* The bubble over a follower Pokemon's head. */

#include <stdio.h>
#include <string.h>

#include "constants/heap.h"

#include "field/field_system.h"

#include "billboard.h"
#include "resource_collection.h"
#include "map_object.h"
#include "overlay005/field_effect_manager.h"
#include "overlay005/ov5_021F5A10.h"
#include "overworld_anim_manager.h"

#include "pc_modfs.h"

/* The four slots this file owns, all clear of every renderer the engine ships.
 * One of each, because one bubble is live at a time. */
#define EMOTE_MODEL_SLOT 20
#define EMOTE_META_SLOT  20
#define EMOTE_TEX_SLOT   20
#define EMOTE_RESOURCE   20

/* What `ov5_021F5D8C` is handed. 0 and 1 are this game's own two bubbles; ours
 * start after them, and the engine patch subtracts this before asking here. */
#define EMOTE_FIRST 2

/* HeartGold's own emote sequence is four steps flickering between two textures
 * of one sheet, and its `BillboardAnim` list is the same two rows this game
 * uses for its own bubble: frames 0..1 on a loop, then the table's end. Both
 * are in both games byte for byte, so this is carried rather than invented. */
static const BillboardAnim kEmoteAnim[] = {
    { 0, 1, BILLBOARD_ANIM_TYPE_LOOP },
    { 0, 0, BILLBOARD_ANIM_TYPE_TABLE_END },
};

/* Where the package put them, read out of the talk's header member rather than
 * compiled in, a fill that lands somewhere else still works. */
struct openmmo_emote_members {
    int model;
    int sequence;
    int first_sheet;
    int count;
};

int openmmo_follow_talk_emote_members(struct openmmo_emote_members *out);

/* Which bubble is registered at the slots right now, and how many live effects
 * are holding it. -1 is "nothing registered". */
static int g_at = -1;
static int g_held;
static int g_said_missing;

int openmmo_emote_resource(int emote);
void openmmo_emote_acquire(FieldEffectManager *fieldEffMan, int emote);
void openmmo_emote_release(FieldEffectManager *fieldEffMan, int emote);
void *openmmo_emote_show(void *mapObjectVoid, int emote);
void openmmo_emote_forget(void);

static void unregister_at(FieldEffectManager *mgr)
{
    if (g_at < 0) {
        return;
    }
    ov5_021DF9D4(mgr, EMOTE_RESOURCE);
    ov5_021DFA7C(mgr, EMOTE_TEX_SLOT);
    ov5_021DFA30(mgr, EMOTE_META_SLOT);
    ov5_021DFA08(mgr, EMOTE_MODEL_SLOT);
    g_at = -1;
}

/* Build the resource for one bubble at the four slots. The order is the
 * engine's own (`ov5_021F5AB4`): model, then the frame sequence, then the
 * texture, then the resource that pairs the three. */
static int register_at(FieldEffectManager *mgr, int emote)
{
    struct openmmo_emote_members m;

    if (!openmmo_follow_talk_emote_members(&m) || m.count <= 0) {
        if (!g_said_missing) {
            g_said_missing = 1;
            printf("openmmo: no emote bubbles (no package carries them; fill"
                   " one with tools/portfollow.py)\n");
            fflush(stdout);
        }
        return 0;
    }
    if (emote < 0 || emote >= m.count) {
        printf("openmmo: emote %d is outside the %d the package carries\n",
               emote, m.count);
        fflush(stdout);
        return 0;
    }

    unregister_at(mgr);
    ov5_021DF9E0(mgr, EMOTE_MODEL_SLOT, (u32)m.model);
    ov5_021DFA14(mgr, EMOTE_META_SLOT, (u32)m.sequence);
    /*
     * The last argument is a TextureResourceMode and not a texture count, a mistake that
     * costs a segfault rather than a wrong picture, because
     * `TextureResource_GetTexResWithData` reads a different pointer for a mode it does not
     * know.
     */
    ov5_021DFA3C(mgr, EMOTE_TEX_SLOT, (u32)(m.first_sheet + emote),
                 TEX_RESOURCE_MODE_SEPARATED);
    ov5_021DF864(mgr, EMOTE_RESOURCE, EMOTE_MODEL_SLOT, EMOTE_META_SLOT,
                 EMOTE_TEX_SLOT, 0, kEmoteAnim);
    g_at = emote;
    return 1;
}

/* The engine patch's four hooks. */

int openmmo_emote_resource(int emote)
{
    return (g_at == emote) ? EMOTE_RESOURCE : -1;
}

void openmmo_emote_acquire(FieldEffectManager *fieldEffMan, int emote)
{
    /* A second effect asking for the same bubble while one is up costs
     * nothing; a different one rebuilds, which is safe because the engine's
     * emote only ever has one live, and the count is what says so. */
    if (g_at == emote) {
        g_held++;
        return;
    }
    if (g_held > 0) {
        printf("openmmo: emote %d is still up; %d is not drawn over it\n",
               g_at, emote);
        fflush(stdout);
        return;
    }
    if (register_at(fieldEffMan, emote)) {
        g_held = 1;
    }
}

void openmmo_emote_release(FieldEffectManager *fieldEffMan, int emote)
{
    if (g_at != emote) {
        return;
    }
    if (g_held > 0) {
        g_held--;
    }
    if (g_held == 0) {
        unregister_at(fieldEffMan);
    }
}

/* Put bubble `emote` (1..14, HeartGold's own numbering) over `mapObject`, and
 * hand the effect back so a caller can wait for it, which is what HeartGold's
 * own follower does, since its emote is a task the talk blocks on. NULL when
 * nothing was drawn. */
void *openmmo_emote_show(void *mapObjectVoid, int emote)
{
    MapObject *obj = mapObjectVoid;
    struct openmmo_emote_members m;

    if (obj == NULL || emote < 1) {
        return NULL;
    }
    if (!openmmo_follow_talk_emote_members(&m) || emote > m.count) {
        if (!g_said_missing) {
            g_said_missing = 1;
            printf("openmmo: no emote bubble %d (no package carries one)\n",
                   emote);
            fflush(stdout);
        }
        return NULL;
    }
    /* param2 = 1 asks the effect to play its sound, param3 = 1 picks the
     * variant that follows the object rather than standing where it started,
     * which is the one HeartGold's follower emote is. */
    return ov5_021F5D8C(obj, EMOTE_FIRST + emote - 1, 1, 1);
}

/* Whether the bubble has finished its rise and its hold. */
int openmmo_emote_done(void *animManagerVoid);

int openmmo_emote_done(void *animManagerVoid)
{
    return animManagerVoid == NULL
        || ov5_021F5C4C((OverworldAnimManager *)animManagerVoid) == 1;
}

/* And take it away, which is the pair of the line above: the effect holds its
 * billboard until somebody finishes it. */
void openmmo_emote_finish(void *animManagerVoid);

void openmmo_emote_finish(void *animManagerVoid)
{
    if (animManagerVoid != NULL) {
        OverworldAnimManager_Finish((OverworldAnimManager *)animManagerVoid);
    }
}

/* A map that went away took the manager and its slots with it. */
void openmmo_emote_forget(void)
{
    g_at = -1;
    g_held = 0;
}
