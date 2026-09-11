/*
 * The source game's own healing-machine scene, for the rooms that came
 * from it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nitro.h>

#include "field/field_system.h"
#include "field_task.h"
#include "map_matrix.h"
#include "overlay005/area_data.h"
#include "overlay005/map_prop.h"
#include "overlay005/map_prop_animation.h"
#include "sound_playback.h"
#include "terrain_collision_manager.h"
#include "heap.h"

#include "res/sound/pl_sound_data.naix" /* SEQ_*_sseq, the engine's own ids */

#define HEAL_BALLS_MAX 6
#define HEAL_BALL_FRAMES 12
#define HEAL_TAG_BALL 0x10
#define HEAL_TAG_MACHINE 0x20

/* 0x02253D90: VecFx32[6], the two-by-three grid on the tray. */
static const VecFx32 sBallOffsets[HEAL_BALLS_MAX] = {
    { -0x4800, 0xC000, -0x4800 },
    { 0x4800, 0xC000, -0x4800 },
    { -0x4800, 0xC000, 0 },
    { 0x4800, 0xC000, 0 },
    { -0x4800, 0xC000, 0x4800 },
    { 0x4800, 0xC000, 0x4800 },
};

struct healanim {
    VecFx32 base;
    u8 count;
    u8 ball;
    u8 ticks;
    u8 state;
    u8 oneshot;
    u8 props[HEAL_BALLS_MAX];
};

static int s_tray = -1;
static int s_ball = -1;
static int s_machine = -1;
static int s_loaded;

static void healanim_load(void)
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
        char role[16];
        int id;

        snprintf(path, sizeof path, "%s/%s/.cooked/generated/heal_props.txt",
                 dir, name);
        f = fopen(path, "r");
        if (f == NULL)
            continue;
        while (fscanf(f, "%15s %d", role, &id) == 2) {
            if (strcmp(role, "tray") == 0)
                s_tray = id;
            else if (strcmp(role, "ball") == 0)
                s_ball = id;
            else if (strcmp(role, "machine") == 0)
                s_machine = id;
        }
        fclose(f);
    }
}

static NNSG3dResMdl *healanim_model(FieldSystem *fs, int modelID)
{
    NNSG3dResFileHeader **file =
        AreaDataManager_GetMapPropModelFile(modelID, fs->areaDataManager);

    if (file == NULL || *file == NULL)
        return NULL;
    return NNS_G3dGetMdlByIdx(NNS_G3dGetMdlSet(*file), 0);
}

static BOOL healanim_task(FieldTask *task)
{
    FieldSystem *fs = FieldTask_GetFieldSystem(task);
    struct healanim *a = FieldTask_GetEnv(task);

    switch (a->state) {
    case 0: { /* the two one-shots, before the first ball lands */
        MapProp *machine;
        NNSG3dResMdl *ballModel, *machineModel;

        if (s_machine < 0)
            break;
        ballModel = healanim_model(fs, s_ball);
        machineModel = healanim_model(fs, s_machine);
        if (ballModel == NULL || machineModel == NULL)
            break;
        if (!FieldSystem_FindLoadedMapPropByModelID(fs, s_machine, &machine,
                                                    NULL))
            break;
        MapPropOneShotAnimationManager_LoadPropAnimations(
            fs->mapPropAnimMan, fs->mapPropOneShotAnimMan, HEAL_TAG_BALL,
            s_ball, NULL, ballModel,
            AreaDataManager_GetMapPropTexture(fs->areaDataManager), 1, 1,
            FALSE);
        MapPropOneShotAnimationManager_LoadPropAnimations(
            fs->mapPropAnimMan, fs->mapPropOneShotAnimMan, HEAL_TAG_MACHINE,
            s_machine, MapProp_GetRenderObj(machine), machineModel,
            AreaDataManager_GetMapPropTexture(fs->areaDataManager), 1, 1,
            FALSE);
        a->oneshot = 1;
        break;
    }
    case 1: /* one ball onto the tray */
        {
            VecFx32 pos = a->base;
            VecFx32 rot = { 0, 0, 0 };

            pos.x += sBallOffsets[a->ball].x;
            pos.y += sBallOffsets[a->ball].y;
            pos.z += sBallOffsets[a->ball].z;
            Sound_PlayEffect(SEQ_SE_DP_BOWA_sseq);
            a->props[a->ball] = MapPropManager_LoadOne(
                fs->mapPropManager, fs->areaDataManager, s_ball, &pos, &rot,
                fs->mapPropAnimMan);
        }
        /* Every placed ball wears the glint. The overlay registers only
         * the first, but on the machine every ball blinks, so this is the
         * owner's official memory over a literal read of the asm. */
        if (a->oneshot) {
            MapProp *ball = MapPropManager_GetLoadedPropSafely(
                fs->mapPropManager, a->props[a->ball]);

            if (ball != NULL)
                MapPropOneShotAnimationManager_SetAnimationRenderObj(
                    fs->mapPropOneShotAnimMan, HEAL_TAG_BALL, a->ball,
                    MapProp_GetRenderObj(ball));
        }
        a->ticks = 0;
        break;
    case 2:
        if (++a->ticks < HEAL_BALL_FRAMES)
            return FALSE;
        a->ball++;
        if (a->ball < a->count) {
            a->state = 1;
            return FALSE;
        }
        break;
    case 3: /* the balls were dressed as they landed */
        break;
    case 4:
        if (a->oneshot) {
            MapPropOneShotAnimationManager_PlayAnimation(
                fs->mapPropOneShotAnimMan, HEAL_TAG_BALL, 0);
            MapPropOneShotAnimationManager_PlayAnimation(
                fs->mapPropOneShotAnimMan, HEAL_TAG_MACHINE, 0);
        }
        Sound_PlayFanfare(SEQ_ASA_sseq);
        break;
    case 5:
        if (a->oneshot
            && (!MapPropOneShotAnimationManager_IsAnimationLoopFinished(
                    fs->mapPropOneShotAnimMan, HEAL_TAG_BALL)
                || !MapPropOneShotAnimationManager_IsAnimationLoopFinished(
                    fs->mapPropOneShotAnimMan, HEAL_TAG_MACHINE)))
            return FALSE;
        if (Sound_IsBGMPausedByFanfare() != FALSE)
            return FALSE;
        break;
    case 6: {
        u8 i;

        if (a->oneshot) {
            MapPropOneShotAnimationManager_UnloadAnimation(
                fs->mapPropAnimMan, fs->mapPropOneShotAnimMan,
                HEAL_TAG_MACHINE);
            MapPropOneShotAnimationManager_UnloadAnimation(
                fs->mapPropAnimMan, fs->mapPropOneShotAnimMan, HEAL_TAG_BALL);
        }
        for (i = 0; i < a->ball; i++)
            MapPropManager_ClearOne(a->props[i], fs->mapPropManager);
        Heap_Free(a);
        return TRUE;
    }
    }

    a->state++;
    return FALSE;
}

/* The patched FieldSystem_PlayHealingAnimation_Pokecenter asks here first.
 * A room with the source game's tray runs the source game's scene; any
 * other room answers 0 and this game's own plays as always. */
int openmmo_hg_healanim(FieldSystem *fs, int count)
{
    MapProp *tray;
    int matrixIndex;
    struct healanim *a;
    VecFx32 origin;

    if (!s_loaded)
        healanim_load();
    if (s_tray < 0 || s_ball < 0)
        return 0;
    if (!FieldSystem_FindLoadedMapPropByModelID(fs, s_tray, &tray,
                                                &matrixIndex))
        return 0;
    a = Heap_AllocAtEnd(HEAP_ID_FIELD1, sizeof *a);
    if (a == NULL)
        return 0;
    memset(a, 0, sizeof *a);
    a->count = (u8)(count > HEAL_BALLS_MAX ? HEAL_BALLS_MAX
                                           : (count < 1 ? 1 : count));
    a->base = MapProp_GetPosition(tray);
    TerrainCollisionManager_GetMapAbsoluteOrigin(
        matrixIndex, MapMatrix_GetWidth(fs->mapMatrix), &origin);
    a->base.x += origin.x;
    a->base.z += origin.z;
    FieldTask_InitCall(fs->task, healanim_task, a);
    return 1;
}

/* The animation manager ran out of slots on this area (64 since 2026-09-02,
 * against the 16 the engine ships): the row for `model` stays still. Said
 * once a run; the count is what a bigger raise would want to know. */
void openmmo_prop_anim_overflow(int model)
{
    static int said;

    if (!said)
        printf("openmmo: the prop animation slots are full; model %d and any "
               "after it stand still on this area\n", model);
    said = 1;
}
