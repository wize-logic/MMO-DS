/* The ball a follower comes out of and goes back into. */

#include <stdio.h>
#include <string.h>

#include <nnsys.h>

#include "constants/heap.h"

#include "field/field_system.h"

#include "billboard.h"
#include "billboard_vram_transfer.h"
#include "map_object.h"
#include "map_object_move.h"
#include "overlay005/field_effect_manager.h"
#include "overworld_anim_manager.h"
#include "simple3d.h"
#include "vram_transfer.h"

#include "openmmo_follow_internal.h"

/* Where the package put the three members. */
int openmmo_follow_talk_ball_members(int *ball, int *flash, int *flash_anim);
/* patches/src/overlay005/ov5_021ECE40.c */
Billboard *openmmo_map_object_billboard(MapObject *obj);

/* ---------------------------------------------------------------- *
 *  The renderer's resources (ov01_022031F8 / ov01_02203270)
 * ---------------------------------------------------------------- */

typedef struct {
    int loaded;
    int said;
    Simple3DModel ball;
    Simple3DModel flash;
    Simple3DAnimation flash_anim;
    Simple3DRenderObj ball_obj;
    Simple3DRenderObj flash_obj;
} BallResources;

static BallResources g_res;

static BallResources *resources(FieldEffectManager *mgr)
{
    int ball, flash, anim;

    if (g_res.loaded) {
        return &g_res;
    }
    if (!openmmo_follow_talk_ball_members(&ball, &flash, &anim)) {
        if (!g_res.said) {
            g_res.said = 1;
            printf("openmmo: no follower ball (no package carries it; fill one"
                   " with tools/portfollow.py)\n");
            fflush(stdout);
        }
        return NULL;
    }
    FieldEffectManager_LoadModel(mgr, &g_res.ball, 0, (u32)ball, FALSE);
    Simple3D_CreateRenderObject(&g_res.ball_obj, &g_res.ball);
    FieldEffectManager_LoadModel(mgr, &g_res.flash, 0, (u32)flash, FALSE);
    FieldEffectManager_LoadAnimation(mgr, &g_res.flash_anim, 0, (u32)anim, FALSE);
    Simple3D_BindModelToAnim(&g_res.flash_anim, &g_res.flash, HEAP_ID_FIELD1);
    Simple3D_CreateRenderObjectWithAnim(&g_res.flash_obj, &g_res.flash, &g_res.flash_anim);
    g_res.loaded = 1;
    return &g_res;
}

/* The field heap they were loaded onto is gone with the map. */
void openmmo_follow_fx_forget(void)
{
    memset(&g_res, 0, sizeof g_res);
}

/* ---------------------------------------------------------------- *
 *  The follower's billboard: scale and palette
 * ---------------------------------------------------------------- */

/* sub_02023E78 on the source's sprite: x and y, z untouched. */
void openmmo_follow_fx_scale(MapObject *obj, fx32 x, fx32 y)
{
    Billboard *bb = obj != NULL ? openmmo_map_object_billboard(obj) : NULL;
    VecFx32 scale;

    if (bb == NULL) {
        return;
    }
    scale.x = x;
    scale.y = y;
    scale.z = FX32_ONE;
    Billboard_SetScale(bb, &scale);
}

static u32 palette_bytes(Billboard *bb)
{
    u32 n = NNS_GfdGetPlttKeySize(bb->plttKey);

    return n > 64 ? 64 : n;
}

/* ov01_02205DB4 state 2: the palette as the texture holds it, so the white
 * can be undone. */
void openmmo_follow_fx_palette_save(MapObject *obj, void *buf64)
{
    Billboard *bb = obj != NULL ? openmmo_map_object_billboard(obj) : NULL;
    const void *src;

    memset(buf64, 0, 64);
    if (bb == NULL || bb->texture == NULL || bb->plttKey == 0) {
        return;
    }
    src = GetPlttDataVRAMBuffer(bb->texture, 0);
    if (src != NULL) {
        memcpy(buf64, src, palette_bytes(bb));
    }
}

void openmmo_follow_fx_palette_restore(MapObject *obj, const void *buf64)
{
    Billboard *bb = obj != NULL ? openmmo_map_object_billboard(obj) : NULL;

    if (bb == NULL || bb->plttKey == 0) {
        return;
    }
    VramTransfer_Request(NNS_GFD_DST_3D_TEX_PLTT, NNS_GfdGetPlttKeyAddr(bb->plttKey),
                         (void *)buf64, palette_bytes(bb));
}

/* ov01_02209B18: sixty-four bytes of 0xFF, which is white in every slot. */
static u8 s_white[64];

static void palette_white(MapObject *obj)
{
    Billboard *bb = obj != NULL ? openmmo_map_object_billboard(obj) : NULL;

    if (bb == NULL || bb->plttKey == 0) {
        return;
    }
    memset(s_white, 0xFF, sizeof s_white);
    VramTransfer_Request(NNS_GFD_DST_3D_TEX_PLTT, NNS_GfdGetPlttKeyAddr(bb->plttKey),
                         s_white, palette_bytes(bb));
}

/* ---------------------------------------------------------------- *
 *  The task (ov01_0220329C and its four tables)
 * ---------------------------------------------------------------- */

typedef struct {
    int kind;
    int state;
    int n;              /* env[0x10] */
    int gfx;            /* env[4] */
    int local_id;       /* env[8] */
    int header;         /* env[0xC] */
    int height_ok;      /* env[0x14] */
    MapObject *obj;     /* env[0x30] */
    VecFx32 pos;        /* env[0x18]: the effect's, six units above the feet */
    VecFx32 opos;       /* env[0x48]: the follower's, for the slide */
    fx32 *slide;        /* env[0x3C] */
    fx32 slide_start;   /* env[0x40] */
    int slide_delta;    /* env[0x44] */
    int ball_on;
    int flash_on;
    BallResources *res;
} BallCtx;

typedef struct {
    MapObject *obj;
    int kind;
    BallResources *res;
} BallUser;

/* ov01_022039BC / ov01_022039E0: which coordinate a large one slides along
 * and by how much a frame, in quarter units. */
static void slide_setup(BallCtx *c)
{
    int dir = MapObject_GetFacingDir(c->obj);

    MapObject_GetPosPtr(c->obj, &c->opos);
    if (dir == 0 || dir == 1) {
        c->slide = &c->opos.z;
    } else {
        c->slide = &c->opos.x;
    }
    c->slide_start = *c->slide;
    switch (dir) {
    case 0: c->slide_delta = -1; break;
    case 1: c->slide_delta = 1; break;
    case 2: c->slide_delta = -10; break;
    default: c->slide_delta = 10; break;
    }
}

static void slide_step(BallCtx *c)
{
    fx32 d;

    if (c->slide == NULL) {
        return;
    }
    d = (fx32)c->n * (c->slide_delta << 12);
    d = (d + ((d >> 31) & 3)) >> 2;
    *c->slide = c->slide_start + d;
    MapObject_SetPos(c->obj, &c->opos);
}

/* The 1/n shrink of ov01_02203654 and ov01_02203890. */
static void shrink_step(BallCtx *c)
{
    fx32 s = FX32_ONE / (c->n > 0 ? c->n : 1);

    openmmo_follow_fx_scale(c->obj, s, s);
}

static int still_there(BallCtx *c)
{
    return sub_020627B4(c->obj, c->gfx, c->local_id, (enum MapHeaderID)c->header) != 0;
}

/* sub_0206121C: the terrain's own height under the effect, once it answers. */
static void settle_height(OverworldAnimManager *mgr, BallCtx *c)
{
    FieldSystem *fs = MapObject_FieldSystem(c->obj);
    VecFx32 p;

    if (c->height_ok) {
        return;
    }
    OverworldAnimManager_GetPosition(mgr, &p);
    c->height_ok = MapObject_RecalculatePositionHeight(fs, &c->pos);
    if (c->height_ok == 1) {
        p.y = c->pos.y;
        OverworldAnimManager_SetPosition(mgr, &p);
    }
}

static BOOL ball_init(OverworldAnimManager *mgr, void *ctxv)
{
    BallCtx *c = ctxv;
    const BallUser *u = OverworldAnimManager_GetUserData(mgr);
    FieldSystem *fs;

    memset(c, 0, sizeof *c);
    c->obj = u->obj;
    c->kind = u->kind;
    c->res = u->res;
    c->gfx = (int)MapObject_GetGraphicsID(c->obj);
    c->local_id = (int)MapObject_GetLocalID(c->obj);
    c->header = (int)MapObject_GetMapHeaderID(c->obj);
    fs = MapObject_FieldSystem(c->obj);
    OverworldAnimManager_GetPosition(mgr, &c->pos);
    c->height_ok = MapObject_RecalculatePositionHeight(fs, &c->pos);
    OverworldAnimManager_SetPosition(mgr, &c->pos);
    c->ball_on = 0;
    c->flash_on = 0;
    return TRUE;
}

static void ball_exit(OverworldAnimManager *mgr, void *ctxv)
{
    (void)mgr;
    (void)ctxv;
}

/* ov01_022033E4 (out). */
static void tick_out(OverworldAnimManager *mgr, BallCtx *c)
{
    switch (c->state) {
    case 0:
        c->n++;
        if (c->n >= 2) {
            c->state = 1;
        }
        break;
    case 1:
        c->ball_on = 0;
        c->flash_on = 1;
        openmmo_follow_hide(c->obj, 0);
        openmmo_follow_fx_scale(c->obj, FX32_ONE, FX32_ONE);
        sub_02062B68(c->obj);
        Simple3D_SetAnimFrame(&c->res->flash_anim, 0);
        c->state = 2;
        /* fallthrough */
    case 2:
        if (Simple3D_UpdateAnim(&c->res->flash_anim, FX32_ONE, FALSE)) {
            FieldEffectManager_FinishAnimManager(mgr);
        }
        break;
    default:
        FieldEffectManager_FinishAnimManager(mgr);
        break;
    }
}

/* ov01_02203654 (in). */
static void tick_in(OverworldAnimManager *mgr, BallCtx *c)
{
    switch (c->state) {
    case 0:
        palette_white(c->obj);
        if (openmmo_follow_is_large(c->obj)) {
            slide_setup(c);
        } else {
            c->slide = NULL;
        }
        c->state = 1;
        /* fallthrough */
    case 1:
        c->n++;
        shrink_step(c);
        slide_step(c);
        if (c->n >= 4) {
            c->state = 2;
            c->ball_on = 1;
            MapObject_SetHidden(c->obj, 1);
            c->n = 0;
        }
        break;
    case 2:
        c->n++;
        if (c->n >= 4) {
            FieldEffectManager_FinishAnimManager(mgr);
        }
        break;
    default:
        FieldEffectManager_FinishAnimManager(mgr);
        break;
    }
}

/* ov01_022034F8 (flash). */
static void tick_flash(OverworldAnimManager *mgr, BallCtx *c)
{
    switch (c->state) {
    case 0:
        c->ball_on = 0;
        c->flash_on = 1;
        Simple3D_SetAnimFrame(&c->res->flash_anim, 0);
        c->state = 1;
        /* fallthrough */
    case 1:
        if (Simple3D_UpdateAnim(&c->res->flash_anim, FX32_ONE, FALSE)) {
            FieldEffectManager_FinishAnimManager(mgr);
        }
        break;
    default:
        FieldEffectManager_FinishAnimManager(mgr);
        break;
    }
}

/* ov01_02203890 (IN, QUIET). */
static void tick_in_quiet(OverworldAnimManager *mgr, BallCtx *c)
{
    switch (c->state) {
    case 0:
        if (openmmo_follow_is_large(c->obj)) {
            slide_setup(c);
        } else {
            c->slide = NULL;
        }
        c->state = 1;
        /* fallthrough */
    case 1:
        c->n++;
        shrink_step(c);
        slide_step(c);
        if (c->n >= 4) {
            c->state = 2;
            c->ball_on = 1;
            openmmo_follow_hide(c->obj, 1);
            c->n = 0;
        }
        break;
    case 2:
        c->n++;
        if (c->n >= 0x10) {
            FieldEffectManager_FinishAnimManager(mgr);
        }
        break;
    default:
        FieldEffectManager_FinishAnimManager(mgr);
        break;
    }
}

static void ball_tick(OverworldAnimManager *mgr, void *ctxv)
{
    BallCtx *c = ctxv;

    if (!still_there(c)) {
        FieldEffectManager_FinishAnimManager(mgr);
        return;
    }
    if (c->kind == 0 || c->kind == 2) {
        settle_height(mgr, c);
    }
    switch (c->kind) {
    case 0: tick_out(mgr, c); break;
    case 1: tick_in(mgr, c); break;
    case 2: tick_flash(mgr, c); break;
    default: tick_in_quiet(mgr, c); break;
    }
}

static void ball_render(OverworldAnimManager *mgr, void *ctxv)
{
    BallCtx *c = ctxv;
    VecFx32 p;

    if (!still_there(c)) {
        FieldEffectManager_FinishAnimManager(mgr);
        return;
    }
    OverworldAnimManager_GetPosition(mgr, &p);
    if (c->ball_on) {
        Simple3D_DrawRenderObjWithPos(&c->res->ball_obj, &p);
    }
    if (c->flash_on) {
        Simple3D_DrawRenderObjWithPos(&c->res->flash_obj, &p);
    }
}

static const OverworldAnimManagerFuncs kBallFuncs = {
    sizeof(BallCtx),
    ball_init,
    ball_exit,
    ball_tick,
    ball_render,
};

/* ov01_0220329C. The effect sits six units above the follower's own
 * position, drawn two priorities behind it. NULL when nothing could be
 * drawn, and then the caller's own hide or show is all that happens. */
void *openmmo_follow_fx_ball(MapObject *obj, int kind)
{
    FieldEffectManager *mgr;
    BallResources *res;
    BallUser u;
    VecFx32 pos;
    int prio;

    if (obj == NULL) {
        return NULL;
    }
    mgr = MapObject_GetFieldEffectManager(obj);
    res = mgr != NULL ? resources(mgr) : NULL;
    if (res == NULL) {
        /* No ball to draw: the follower still ends where the effect would
         * have left it, so the scene around it is not left waiting. */
        if (kind == 0) {
            openmmo_follow_hide(obj, 0);
            openmmo_follow_fx_scale(obj, FX32_ONE, FX32_ONE);
        } else if (kind == 1) {
            MapObject_SetHidden(obj, 1);
        } else if (kind == 3) {
            openmmo_follow_hide(obj, 1);
        }
        return NULL;
    }
    MapObject_GetPosPtr(obj, &pos);
    pos.y += FX32_ONE * 6;
    u.obj = obj;
    u.kind = kind;
    u.res = res;
    prio = MapObject_CalculateTaskPriority(obj, 2);
    return FieldEffectManager_InitAnimManager(mgr, &kBallFuncs, &pos, kind, &u, prio);
}

/* sub_02068CCC: whether a started effect is still running. */
int openmmo_follow_fx_running(void *anim)
{
    return anim != NULL && OverworldAnimManager_IsActive((OverworldAnimManager *)anim);
}
