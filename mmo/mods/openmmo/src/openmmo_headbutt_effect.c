/* The tree a Headbutt shakes, on a ported map. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nitro.h>

#include "constants/heap.h"
#include "constants/narc.h"
#include "field/field_system.h"
#include "field/field_system_sub2_t.h"
#include "overlay005/ov5_021D1A94.h"
#include "overlay005/struct_ov5_021D1BEC_decl.h"
#include "overlay006/struct_ov6_0223E6EC.h"

#include "camera.h"
#include "easy3d_object.h"
#include "heap.h"
#include "narc.h"
#include "player_avatar.h"
#include "sound_playback.h"

#include "res/sound/pl_sound_data.naix"

#define HEADBUTT_ANIMS_MAX 4
/* Half a tile, the jolt's reach: the same 2 << 14 the source adds. */
#define HEADBUTT_JOLT ((16 * FX32_ONE) >> 1)
#define HEADBUTT_JOLTS 2
/* This game has no SEQ_SE_GS_ZUTUKI; the thud of walking into a wall is the
 * nearest sound it owns. */
#define HEADBUTT_SE SEQ_SE_DP_WALL_HIT_sseq

typedef struct {
    Easy3DObject obj;
    Easy3DModel model;
    Easy3DAnim anims[HEADBUTT_ANIMS_MAX];
    int animCount;
    NNSFndAllocator allocator;
    VecFx32 camPos;      /* the camera as it stood, restored between jolts */
    VecFx32 camTarget;
    VecFx32 offset;      /* half a tile toward the tree */
    int hold;            /* frames until the next toggle */
    int phase;           /* 1 while the camera is pushed */
    int jolts;           /* toggles back so far */
    int loaded;          /* the model is real and drawn */
    int state;           /* 0 shaking, 1 done */
} HeadbuttEffect;

static int s_loaded;
static int s_model = -1;     /* field_effects.txt: the tree, appended */
static int s_anim = -1;      /* its first animation, appended */
static int s_anims;

static void load_rows(void)
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
            if (strcmp(key, "headbutt_model") == 0) {
                s_model = a;
            } else if (strcmp(key, "headbutt_anim") == 0) {
                if (fscanf(f, "%d", &b) != 1)
                    break;
                s_anim = a;
                s_anims = b > HEADBUTT_ANIMS_MAX ? HEADBUTT_ANIMS_MAX : b;
            }
        }
        fclose(f);
    }
}

/* The tile in front of the player, as the rock's effect places itself
 * (ov6_022480BC), and the jolt toward it. */
static void place(PlayerAvatar *avatar, HeadbuttEffect *fx)
{
    VecFx32 pos;
    int x = PlayerAvatar_GetXPos(avatar);
    int z = PlayerAvatar_GetZPos(avatar);

    PlayerAvatar_GetPosPtr(avatar, &pos);
    fx->offset.x = fx->offset.y = fx->offset.z = 0;
    switch (PlayerAvatar_GetFacingDir(avatar)) {
    case 0: z--; fx->offset.z = -HEADBUTT_JOLT; break;
    case 1: z++; fx->offset.z = HEADBUTT_JOLT; break;
    case 2: x--; fx->offset.x = -HEADBUTT_JOLT; break;
    case 3: x++; fx->offset.x = HEADBUTT_JOLT; break;
    default: break;
    }
    if (fx->loaded) {
        Easy3DObject_SetPosition(&fx->obj, ((x << 4) * FX32_ONE) + ((16 * FX32_ONE) >> 1),
                                 pos.y, ((z << 4) * FX32_ONE) + ((16 * FX32_ONE) >> 1));
    }
}

static void headbutt_init(UnkStruct_ov5_021D1BEC *effect, FieldSystem *fs, void *data)
{
    HeadbuttEffect *fx = data;
    int i;

    (void)effect;
    memset(fx, 0, sizeof *fx);
    if (!s_loaded)
        load_rows();
    HeapExp_FndInitAllocator(&fx->allocator, HEAP_ID_FIELD1, 32);
    if (s_model >= 0) {
        NARC *narc = NARC_ctor(NARC_INDEX_GRAPHIC__HIDEN_EFFECT, HEAP_ID_FIELD1);

        Easy3DModel_LoadFrom(&fx->model, narc, (u32)s_model, HEAP_ID_FIELD1);
        Easy3DObject_Init(&fx->obj, &fx->model);
        fx->animCount = s_anim >= 0 ? s_anims : 0;
        for (i = 0; i < fx->animCount; i++) {
            Easy3DAnim_LoadFrom(&fx->anims[i], &fx->model, narc, (u32)(s_anim + i),
                                HEAP_ID_FIELD1, &fx->allocator);
            Easy3DObject_AddAnim(&fx->obj, &fx->anims[i]);
        }
        NARC_dtor(narc);
        fx->loaded = 1;
    } else {
        printf("openmmo: headbutt: no tree in this package; the camera alone answers\n");
    }
    place(fs->playerAvatar, fx);
    fx->camTarget = Camera_GetTarget(fs->camera);
    fx->camPos = Camera_GetPosition(fs->camera);
    Sound_PlayEffect(HEADBUTT_SE);
    fx->state = 0;
}

static void headbutt_free(UnkStruct_ov5_021D1BEC *effect, FieldSystem *fs, void *data)
{
    HeadbuttEffect *fx = data;
    int i;

    (void)effect;
    (void)fs;
    if (!fx->loaded)
        return;
    Easy3DModel_Release(&fx->model);
    for (i = 0; i < fx->animCount; i++)
        Easy3DAnim_Release(&fx->anims[i], &fx->allocator);
}

static void restore_camera(FieldSystem *fs, const HeadbuttEffect *fx)
{
    Camera_SetTarget(&fx->camTarget, fs->camera);
    Camera_SetPosition(&fx->camPos, fs->camera);
}

/* The source's frame (ov02_0224DB9C): every other frame the camera is pushed
 * half a tile toward the tree or set back, twice over; the animations run
 * meanwhile, and the tree goes when they end. Done holds the camera home. */
static void headbutt_update(UnkStruct_ov5_021D1BEC *effect, FieldSystem *fs, void *data)
{
    HeadbuttEffect *fx = data;
    BOOL finished = TRUE;
    int i;

    (void)effect;
    if (fx->state != 0) {
        restore_camera(fs, fx);
        return;
    }
    if (fx->jolts < HEADBUTT_JOLTS) {
        if (fx->hold <= 0) {
            fx->hold = 1;
            fx->phase ^= 1;
            if (fx->phase) {
                VecFx32 target = fx->camTarget;
                VecFx32 pos = fx->camPos;

                target.x += fx->offset.x;
                target.z += fx->offset.z;
                pos.x += fx->offset.x;
                pos.z += fx->offset.z;
                Camera_SetTarget(&target, fs->camera);
                Camera_SetPosition(&pos, fs->camera);
            } else {
                restore_camera(fs, fx);
                fx->jolts++;
            }
        } else {
            fx->hold--;
        }
    }
    if (fx->loaded) {
        for (i = 0; i < fx->animCount; i++)
            finished &= Easy3DAnim_Update(&fx->anims[i], FX32_ONE);
    } else {
        finished = fx->jolts >= HEADBUTT_JOLTS;
    }
    if (finished) {
        if (fx->loaded)
            Easy3DObject_SetVisible(&fx->obj, 0);
        restore_camera(fs, fx);
        fx->state = 1;
    }
}

static void headbutt_draw(UnkStruct_ov5_021D1BEC *effect, FieldSystem *fs, void *data)
{
    HeadbuttEffect *fx = data;

    (void)effect;
    (void)fs;
    if (fx->loaded)
        Easy3DObject_Draw(&fx->obj);
}

/* The three rows the obstacle table wants of a kind: make, free, done. */
UnkStruct_ov5_021D1BEC *openmmo_headbutt_effect_new(FieldSystem *fs)
{
    static const UnkStruct_ov6_0223E6EC desc = {
        1024, sizeof(HeadbuttEffect), headbutt_init, headbutt_free, headbutt_update, headbutt_draw
    };

    return ov5_021D1B6C(fs->unk_04->unk_04, &desc);
}

void openmmo_headbutt_effect_free(UnkStruct_ov5_021D1BEC *effect)
{
    ov5_021D1BEC(effect);
}

BOOL openmmo_headbutt_effect_done(UnkStruct_ov5_021D1BEC *effect)
{
    HeadbuttEffect *fx = ov5_021D1C2C(effect);

    return fx->state == 1;
}
