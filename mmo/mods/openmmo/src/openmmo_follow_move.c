/* How HeartGold's follower moves, and the scenes around it. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "generated/movement_actions.h"
#include "generated/movement_types.h"

#include "field/field_system.h"
#include "field_system.h"

#include "constants/heap.h"
#include "constants/map_object.h"
#include "constants/player_avatar.h"
#include "field_task.h"
#include "heap.h"
#include "inlines.h"
#include "map_header.h"
#include "map_object.h"
#include "map_object_move.h"
#include "map_tile_behavior.h"
#include "overlay005/ov5_021F2D20.h"
#include "overlay005/ov5_021F37A8.h"
#include "party.h"
#include "player_avatar.h"
#include "pokemon.h"
#include "save_player.h"
#include "script_manager.h"
#include "terrain_collision_manager.h"
#include "unk_020655F4.h"
#include "unk_020673B8.h"

#include "../../../include/follower.h"
#include "openmmo_follow_internal.h"

/* openmmo_emote.c */
void *openmmo_emote_show(void *mapObject, int emote);
int openmmo_emote_done(void *animManager);
void openmmo_emote_finish(void *animManager);
/* openmmo_follow_fx.c */
void openmmo_follow_fx_scale(MapObject *obj, fx32 x, fx32 y);
void openmmo_follow_fx_palette_save(MapObject *obj, void *buf64);
void openmmo_follow_fx_palette_restore(MapObject *obj, const void *buf64);
/* patches/src/overlay005/ov5_021F2D20.c, ov5_021F37A8.c: the grass rustle
 * at a tile of the caller's choosing, which ov01_021FF0E4 is. */
void openmmo_grass_rustle_at(MapObject *obj, int x, int z, int facing);
void openmmo_very_tall_grass_rustle_at(MapObject *obj, int x, int z, int facing);
/* openmmo_follow.c */
void openmmo_follow_set_inhibit(FieldSystem *fs, int on);
void openmmo_follow_warp_clears_inhibit(void);
void openmmo_follow_save_map(int header);

/* The twelve bytes the source keeps on the object (sub_0205F370(obj, 0xC)),
 * laid out as it lays them out. */
typedef struct {
    u8 state;
    u8 tracked;
    u8 rep;
    u8 frame;
    s16 last_x;
    s16 last_z;
    u16 unused;
    u16 flags;      /* bit 0: left behind in its ball; bits 1-2: speed class */
} FollowMove;

#define FLAG_LEFT_BEHIND 1
#define FLAG_CLASS_MASK  6

/* Which of the three types the object is running. The source keeps this as
 * the object's movement type; here it is this file's, for the reason in the
 * header comment. */
static int g_type = FOLLOW_TYPE_DRIVEN;

int openmmo_follow_type(void)
{
    return g_type;
}

static FollowMove *data_of(MapObject *obj)
{
    return (FollowMove *)sub_02062A78(obj);
}

static PlayerAvatar *avatar_of(MapObject *obj)
{
    FieldSystem *fs = MapObject_FieldSystem(obj);

    return fs != NULL ? fs->playerAvatar : NULL;
}

/* ------------------------------------------------------------------ *
 *  Small helpers
 * ------------------------------------------------------------------ */

/* ov01_0220542C: the same direction one speed class up. */
static int speed_up(int dir, int anim)
{
    static const int slow[4] = { 8, 9, 10, 11 };
    static const int normal[4] = { 12, 13, 14, 15 };
    static const int fast[4] = { 16, 17, 18, 19 };
    static const int faster[4] = { 20, 21, 22, 23 };
    int i;

    for (i = 0; i < 4; i++) {
        if (slow[i] == anim) {
            return normal[dir & 3];
        }
    }
    for (i = 0; i < 4; i++) {
        if (normal[i] == anim) {
            return fast[dir & 3];
        }
    }
    for (i = 0; i < 4; i++) {
        if (fast[i] == anim) {
            return faster[dir & 3];
        }
    }
    printf("openmmo: follower action %d has no faster class\n", anim);
    fflush(stdout);
    return 0;
}

/* sub_02066444: which speed class a step action is in. */
static int anim_class(int anim)
{
    if (anim >= 12 && anim <= 15) {
        return 3;
    }
    if (anim >= 16 && anim <= 19) {
        return 2;
    }
    if (anim >= 20 && anim <= 23) {
        return 1;
    }
    printf("openmmo: follower action %d has no speed class\n", anim);
    fflush(stdout);
    return 0;
}

/* sub_020623C8: a far jump, which is what a ledge is. */
static int is_far_jump(int anim)
{
    return anim >= 0x38 && anim <= 0x3B;
}

/* sub_020623D8. */
static int walk_slow_for(int dir)
{
    return 8 + (dir & 3);
}

/* sub_02065D78: the action the arm carried, with a run lowered to a
 * WALK_FAST because the follower has no run cycle. */
static int armed_anim(void)
{
    int a = openmmo_follow_state()->arm_anim;

    if (a >= 0x58 && a <= 0x5B) {
        return 0x10 + (a - 0x58);
    }
    return a;
}

/* sub_02065DB4: the player object's own current action, lowered the same way. */
static int player_current_anim(MapObject *obj)
{
    PlayerAvatar *av = avatar_of(obj);
    int a;

    if (av == NULL) {
        return MOVEMENT_ACTION_NONE;
    }
    a = (int)MapObject_GetMovementAction(PlayerAvatar_GetMapObject(av));
    if (a >= 0x58 && a <= 0x5B) {
        return 0x10 + (a - 0x58);
    }
    return a;
}

/* ov01_022054E0: the hop arc, by speed class and frame, only while a catch-up
 * step is running. ov01_02209750, three rows of sixteen. */
static fx32 hop_offset(MapObject *obj)
{
    static const fx32 arc[3][16] = {
        { 0x0000, 0x0800, 0x0C00, 0x0800, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { 0x0000, 0x0600, 0x0800, 0x0A00, 0x0C00, 0x0A00, 0x0800, 0x0600,
          0, 0, 0, 0, 0, 0, 0, 0 },
        { 0x0000, 0x0600, 0x0800, 0x0800, 0x0A00, 0x0A00, 0x0C00, 0x0C00,
          0x0C00, 0x0C00, 0x0C00, 0x0A00, 0x0A00, 0x0800, 0x0800, 0x0600 },
    };
    FollowMove *d = data_of(obj);
    int cls, frame;

    if (openmmo_follow_no_dip(obj)) {
        return 0;
    }
    cls = (d->flags >> 1) & 3;
    if (cls == 0) {
        return 0;
    }
    frame = d->frame;
    if (frame >= 16) {
        frame = 15;
    }
    return arc[cls - 1][frame];
}

/* ov01_02205604: the tile behind the object, by its facing. */
static void tile_behind(MapObject *obj, int *x, int *z)
{
    *x = MapObject_GetX(obj);
    *z = MapObject_GetZ(obj);
    switch (MapObject_GetFacingDir(obj)) {
    case DIR_NORTH: (*z)++; break;
    case DIR_SOUTH: (*z)--; break;
    case DIR_WEST: (*x)++; break;
    case DIR_EAST: (*x)--; break;
    default: break;
    }
}

/* sub_020664D8: the grass rustles where the follower is shown, and a large
 * one facing west or east rustles the tile behind it too. */
static void grass_effects(MapObject *obj)
{
    FieldSystem *fs = MapObject_FieldSystem(obj);
    int x = MapObject_GetX(obj);
    int z = MapObject_GetZ(obj);
    u8 b = TerrainCollisionManager_GetTileBehavior(fs, x, z);
    int facing;

    if (TileBehavior_IsTallGrass(b) == TRUE) {
        ov5_021F2EA4(obj, 0);
    } else if (TileBehavior_IsVeryTallGrass(b) == TRUE) {
        ov5_021F3844(obj, 0);
    }
    if (!openmmo_follow_is_large(obj)) {
        return;
    }
    facing = MapObject_GetFacingDir(obj);
    if (facing != DIR_WEST && facing != DIR_EAST) {
        return;
    }
    tile_behind(obj, &x, &z);
    b = TerrainCollisionManager_GetTileBehavior(fs, x, z);
    if (TileBehavior_IsTallGrass(b) == TRUE) {
        openmmo_grass_rustle_at(obj, x, z, 1);
    } else if (TileBehavior_IsVeryTallGrass(b) == TRUE) {
        openmmo_very_tall_grass_rustle_at(obj, x, z, 1);
    }
}

/* The block every step runs before it moves: a follower hidden by the follow
 * logic and not left behind in its ball is shown, out of the ball if that
 * was asked for, and the grass answers. */
static void show_if_waiting(MapObject *obj, FollowMove *d)
{
    if (!openmmo_follow_bit1_get(obj) || (d->flags & FLAG_LEFT_BEHIND)) {
        return;
    }
    if (openmmo_follow_wants_ball_get(obj)) {
        openmmo_follow_fx_ball(obj, 0);
        openmmo_follow_wants_ball_set(obj, 0);
    } else {
        openmmo_follow_hide(obj, 0);
    }
    grass_effects(obj);
}

/* sub_02065CFC / sub_02065CD0: remember the player's tile once. */
static void track_start(MapObject *obj, FollowMove *d)
{
    PlayerAvatar *av = avatar_of(obj);

    d->tracked = 1;
    d->last_x = (s16)PlayerAvatar_GetXPos(av);
    d->last_z = (s16)PlayerAvatar_GetZPos(av);
    d->unused = 0xFF;
}

static int track(MapObject *obj, FollowMove *d)
{
    const MapObjectManager *man = MapObject_MapObjectManager(obj);

    if (MapObjectMan_GetPlayerMapObject(man) == NULL) {
        d->tracked = 0;
        return 0;
    }
    if (d->tracked == 0) {
        track_start(obj, d);
    }
    return 1;
}

/* sub_02065D24 / sub_02065D58. */
static int player_moved(MapObject *obj, FollowMove *d)
{
    PlayerAvatar *av = avatar_of(obj);

    if (av == NULL) {
        return 0;
    }
    return PlayerAvatar_GetXPos(av) != d->last_x || PlayerAvatar_GetZPos(av) != d->last_z;
}

static void record_player(MapObject *obj, FollowMove *d)
{
    PlayerAvatar *av = avatar_of(obj);

    d->last_x = (s16)PlayerAvatar_GetXPos(av);
    d->last_z = (s16)PlayerAvatar_GetZPos(av);
}

/* ------------------------------------------------------------------ *
 *  The three step routines
 * ------------------------------------------------------------------ */

/* sub_02065DF4: one step toward the tile the player is leaving, with the
 * action the arm carried. A ledge is walked to slowly and remembered; the
 * remembered facing is crossed with one-class-faster steps, twice. */
static int step_driven(MapObject *obj, FollowMove *d)
{
    PlayerAvatar *av = avatar_of(obj);
    int x = MapObject_GetX(obj);
    int z = MapObject_GetZ(obj);
    int px, pz, anim, dir, pend, jump, act;
    int next = 1;

    if (av == NULL) {
        return 0;
    }
    px = PlayerAvatar_XPosPrev(av);
    pz = PlayerAvatar_ZPosPrev(av);
    if (x == px && z == pz) {
        return 0;
    }
    anim = armed_anim();
    dir = GetDirectionBetweenPoints(x, z, px, pz);
    pend = openmmo_follow_pending_get(obj);
    jump = is_far_jump(anim);
    if (pend != 0) {
        if (jump) {
            act = speed_up(pend, walk_slow_for(pend));
            d->flags = (u16)((d->flags & ~FLAG_CLASS_MASK) | (anim_class(act) << 1));
            openmmo_follow_stored_anim_set(obj, act);
            next = 2;
            d->rep = 0;
            d->frame = 0;
            openmmo_follow_pending_set(obj, PlayerAvatar_GetFacingDir(av));
        } else {
            if (!PlayerAvatar_CheckStep(av)) {
                return 0;
            }
            act = speed_up(pend, anim);
            d->flags = (u16)((d->flags & ~FLAG_CLASS_MASK) | (anim_class(act) << 1));
            openmmo_follow_stored_anim_set(obj, act);
            next = 2;
            d->rep = 0;
            d->frame = 0;
            openmmo_follow_pending_set(obj, 0);
        }
    } else if (jump) {
        act = walk_slow_for(dir);
        openmmo_follow_pending_set(obj, PlayerAvatar_GetFacingDir(av));
    } else {
        act = (int)MovementAction_TurnActionTowardsDir(dir, (enum MovementAction)anim);
    }
    sub_02065668(obj, (enum MovementAction)act);
    d->state = (u8)next;
    return 1;
}

/* sub_02065F44: the 0x37 step, toward the player's previous tile with the
 * player object's own action. */
static int step_polled(MapObject *obj)
{
    PlayerAvatar *av = avatar_of(obj);
    int x = MapObject_GetX(obj);
    int z = MapObject_GetZ(obj);
    int px, pz, anim, dir;

    if (av == NULL) {
        return 0;
    }
    px = PlayerAvatar_XPosPrev(av);
    pz = PlayerAvatar_ZPosPrev(av);
    if (x == px && z == pz) {
        return 0;
    }
    anim = player_current_anim(obj);
    dir = GetDirectionBetweenPoints(x, z, px, pz);
    if (anim == MOVEMENT_ACTION_NONE) {
        printf("openmmo: follower asked to follow a player with no action\n");
        fflush(stdout);
        return 0;
    }
    sub_02065668(obj, MovementAction_TurnActionTowardsDir(dir, (enum MovementAction)anim));
    return 1;
}

/* sub_02065FBC: the 0x38 step, the player's action as it is. */
static int step_lockstep(MapObject *obj)
{
    int anim = player_current_anim(obj);

    if (anim == MOVEMENT_ACTION_NONE) {
        return 0;
    }
    sub_02065668(obj, (enum MovementAction)anim);
    return 1;
}

/* sub_02065A4C: 0x30, state 0. Waits to be armed. */
static int st_driven(MapObject *obj, FollowMove *d)
{
    openmmo_follow_mon *fm = openmmo_follow_state();
    PlayerAvatar *av = avatar_of(obj);

    sub_02062D10(obj);
    MapObject_SetEndMovementOff(obj);
    if (fm->arm_state == 1) {
        fm->arm_state = 2;
        return 0;
    }
    if (fm->arm_state == 2) {
        record_player(obj, d);
        if (MapObject_GetX(obj) == fm->arm_x && MapObject_GetZ(obj) == fm->arm_z) {
            fm->arm_state = 0;
            d->state = 3;
            show_if_waiting(obj, d);
            if (is_far_jump(armed_anim()) && av != NULL) {
                openmmo_follow_pending_set(obj, PlayerAvatar_GetFacingDir(av));
            }
            return 1;
        }
        if (step_driven(obj, d) == 1) {
            show_if_waiting(obj, d);
            sub_02062D04(obj);
            fm->arm_state = 3;
            return 1;
        }
        return 0;
    }
    if (fm->arm_state == 3) {
        fm->arm_state = 0;
    }
    return 0;
}

/* sub_02065B70: 0x37, state 0. */
static int st_polled(MapObject *obj, FollowMove *d)
{
    sub_02062D10(obj);
    MapObject_SetEndMovementOff(obj);
    if (player_moved(obj, d) == 1) {
        record_player(obj, d);
        show_if_waiting(obj, d);
        if (step_polled(obj) == 1) {
            sub_02062D04(obj);
            d->state++;
            return 1;
        }
    }
    return 0;
}

/* sub_02065BE8: 0x38, state 0. */
static int st_lockstep(MapObject *obj, FollowMove *d)
{
    sub_02062D10(obj);
    MapObject_SetEndMovementOff(obj);
    if (player_moved(obj, d) == 1) {
        record_player(obj, d);
        if (step_lockstep(obj) == 1) {
            sub_02062D04(obj);
            d->state++;
            return 1;
        }
    }
    return 0;
}

/* sub_02065C2C: state 1, the step runs out. */
static int st_stepping(MapObject *obj, FollowMove *d)
{
    if (sub_020658DC(obj) == TRUE) {
        sub_02062D10(obj);
        d->state = 0;
    }
    return 0;
}

/* sub_02065C48: state 2, the catch-up step, taken twice. */
static int st_catching_up(MapObject *obj, FollowMove *d)
{
    if (sub_020658DC(obj) == TRUE) {
        d->rep++;
        if (d->rep >= 2) {
            sub_02062D10(obj);
            d->state = 0;
            d->frame = 0;
            d->flags &= (u16)~FLAG_CLASS_MASK;
            return 0;
        }
        sub_02065668(obj, (enum MovementAction)openmmo_follow_stored_anim_get(obj));
    }
    d->frame++;
    return 0;
}

/* sub_02065C90: state 3, waiting for the player's own step to end. */
static int st_settling(MapObject *obj, FollowMove *d)
{
    PlayerAvatar *av = avatar_of(obj);
    MapObject *player;

    if (av == NULL) {
        return 0;
    }
    player = PlayerAvatar_GetMapObject(av);
    if (MapObject_CheckStatusFlag(player, MAP_OBJ_STATUS_4) == TRUE
        && MapObject_CheckStatusFlag(player, MAP_OBJ_STATUS_5) == TRUE) {
        d->state = 0;
    }
    if (PlayerAvatar_GetPlayerMoveState(av) == PLAYER_MOVE_STATE_END) {
        d->state = 0;
    }
    return 0;
}

typedef int (*FollowState)(MapObject *, FollowMove *);

static const FollowState kDriven[4] = { st_driven, st_stepping, st_catching_up, st_settling };
static const FollowState kPolled[4] = { st_polled, st_stepping, st_catching_up, st_settling };
static const FollowState kLockstep[4] = { st_lockstep, st_stepping, st_catching_up, st_settling };

/* sub_020658D4: init, shared by the three. */
static void move_init(MapObject *obj)
{
    FollowMove *d = sub_02062A54(obj, sizeof(FollowMove));

    track(obj, d);
    sub_02062A0C(obj, 0);
    sub_02062D10(obj);
    sub_02062D80(obj, 0);
}

/* sub_02065900 / sub_02065938 / sub_02065968: the step, by type. */
static void move_step(MapObject *obj)
{
    FollowMove *d = data_of(obj);
    const FollowState *table;

    if (!track(obj, d)) {
        return;
    }
    switch (g_type) {
    case FOLLOW_TYPE_POLLED:
        table = kPolled;
        break;
    case FOLLOW_TYPE_LOCKSTEP:
        table = kLockstep;
        break;
    default:
        sub_02062D80(obj, 0);
        table = kDriven;
        break;
    }
    if (d->state > 3) {
        d->state = 0;
    }
    while (table[d->state](obj, d) == 1) {
        if (d->state > 3) {
            d->state = 0;
        }
    }
}

/* sub_02065998: the end, empty. */
static void move_end(MapObject *obj)
{
    (void)obj;
}

/* This game binds init, step and end from its table by movement type; these
 * three are put on the object by hand instead, and the object keeps 0x30. */
void openmmo_follow_move_bind(MapObject *obj, int type)
{
    g_type = type;
    sub_02062AF8(obj, move_init);
    sub_02062B0C(obj, move_step);
    sub_02062B20(obj, move_end);
    move_init(obj);
    sub_020673B8(obj);
}

/* sub_0205FC94 on the follower: end, set, bind, init. */
void openmmo_follow_switch_type(FieldSystem *fs, int type)
{
    MapObject *obj = openmmo_follow_object(fs);

    if (obj == NULL) {
        return;
    }
    move_end(obj);
    openmmo_follow_move_bind(obj, type);
}

/* ------------------------------------------------------------------ *
 *  ov01_022053EC.s: the field's handles on the follower
 * ------------------------------------------------------------------ */

/* ov01_02205990. */
void openmmo_follow_arm(FieldSystem *fs, int anim, int x, int z)
{
    openmmo_follow_mon *fm = openmmo_follow_state();

    (void)fs;
    fm->arm_anim = anim;
    fm->arm_x = x;
    fm->arm_z = z;
    if (fm->arm_state == 3) {
        fm->arm_state = 2;
    } else if (fm->arm_state == 0) {
        fm->arm_state = 1;
    }
}

/* MapObject_SetPositionFromVectorAndDirection. */
static void set_pos_from_vec(MapObject *obj, const VecFx32 *v, int dir)
{
    MapObject_SetX(obj, (v->x >> 4) / FX32_ONE);
    MapObject_SetY(obj, (v->y >> 3) / FX32_ONE);
    MapObject_SetZ(obj, (v->z >> 4) / FX32_ONE);
    MapObject_SetPos(obj, v);
    MapObject_UpdateCoords(obj);
    MapObject_Face(obj, dir);
    MapObject_SetStatusFlagOff(obj, MAP_OBJ_STATUS_4 | MAP_OBJ_STATUS_5);
    MapObject_SetMovementAction(obj, MOVEMENT_ACTION_NONE);
    MapObject_SetMovementStep(obj, 0);
    MapObject_SetStartMovement(obj);
    MapObject_SetStatusFlagOff(obj, MAP_OBJ_STATUS_END_MOVEMENT | MAP_OBJ_STATUS_1);
}

/* ov01_02205790: on the player's own tile, facing `dir`. */
void openmmo_follow_place_on_player(FieldSystem *fs, int dir)
{
    MapObject *obj;
    VecFx32 v;

    if (!openmmo_follow_is_active(fs)) {
        return;
    }
    obj = openmmo_follow_object(fs);
    MapObject_GetPosPtr(PlayerAvatar_GetMapObject(fs->playerAvatar), &v);
    set_pos_from_vec(obj, &v, dir);
}

/* ov01_022057C4. */
int openmmo_follow_hidden(FieldSystem *fs)
{
    MapObject *obj = openmmo_follow_object(fs);

    return obj != NULL && MapObject_IsHidden(obj);
}

/* ov01_022057D0 -> sub_020659B8. */
void openmmo_follow_mark_left_behind(FieldSystem *fs)
{
    MapObject *obj = openmmo_follow_object(fs);

    if (obj != NULL) {
        data_of(obj)->flags |= FLAG_LEFT_BEHIND;
    }
}

/* sub_020659CC: the follow put back to its first frame, on its own tile. */
void openmmo_follow_reset_movement(MapObject *obj)
{
    openmmo_follow_mon *fm = openmmo_follow_state();
    FollowMove *d = data_of(obj);

    sub_02062D10(obj);
    MapObject_SetEndMovementOff(obj);
    MapObject_SetStatusFlagOff(obj, MAP_OBJ_STATUS_5);
    MapObject_SetMovementAction(obj, MOVEMENT_ACTION_NONE);
    MapObject_SetMovementStep(obj, 0);
    d->state = 0;
    fm->arm_anim = 0;
    fm->arm_state = 0;
    fm->arm_x = 0;
    fm->arm_z = 0;
    MapObject_SetPosDirFromCoords(obj, MapObject_GetX(obj), MapObject_GetY(obj),
                                  MapObject_GetZ(obj), MapObject_GetFacingDir(obj));
}

/* ov01_0220609C. */
void openmmo_follow_face(FieldSystem *fs, int dir)
{
    if (openmmo_follow_is_active(fs)) {
        MapObject_TryFace(openmmo_follow_object(fs), dir);
    }
}

/* ov01_02205720: a tile off the player in `dir`, facing `facing`. */
static void place_beside_player(MapObject *player, MapObject *obj, int dir, int facing)
{
    VecFx32 v;

    MapObject_GetPosPtr(player, &v);
    switch (dir) {
    case 0: v.z -= FX32_ONE * 16; break;
    case 1: v.z += FX32_ONE * 16; break;
    case 2: v.x += FX32_ONE * 16; break;
    case 3: v.x -= FX32_ONE * 16; break;
    default: break;
    }
    set_pos_from_vec(obj, &v, facing);
}

/* ov01_02206268: shown and on a tile next to the player. */
static int adjacent_to_player(FieldSystem *fs)
{
    MapObject *player, *obj;
    int px, pz, x, z;

    if (!openmmo_follow_is_visible(fs)) {
        return 0;
    }
    player = PlayerAvatar_GetMapObject(fs->playerAvatar);
    obj = openmmo_follow_object(fs);
    px = MapObject_GetX(player);
    pz = MapObject_GetZ(player);
    x = MapObject_GetX(obj);
    z = MapObject_GetZ(obj);
    if (px == x) {
        return pz + 1 == z || pz - 1 == z;
    }
    if (pz == z) {
        return px + 1 == x || px - 1 == x;
    }
    return 0;
}

/* ov01_02206028: one slow step toward the player. */
static void step_toward_player(MapObject *player, MapObject *obj)
{
    int dx = MapObject_GetX(player) - MapObject_GetX(obj);
    int dz = MapObject_GetZ(player) - MapObject_GetZ(obj);

    if (dx < 0) {
        LocalMapObj_SetAnimationCode(obj, (enum MovementAction)0xA);
    } else if (dx > 0) {
        LocalMapObj_SetAnimationCode(obj, (enum MovementAction)0xB);
    } else if (dz < 0) {
        LocalMapObj_SetAnimationCode(obj, (enum MovementAction)8);
    } else if (dz > 0) {
        LocalMapObj_SetAnimationCode(obj, (enum MovementAction)9);
    }
}

/* ------------------------------------------------------------------ *
 *  The three scene tasks
 * ------------------------------------------------------------------ */

/* ov01_02205AEC / ov01_02205B14: the follower goes round to the player's
 * side, faces up, hops toward the counter and is recalled, the Pokemon
 * Centre's heal, in the common script bank. */
typedef struct {
    u8 state;
    u8 i;
    u8 wait;
    u8 n;
    int sign;
} HopEnv;

/* ov01_02205CF0. */
static int hop_route(FieldSystem *fs, HopEnv *env)
{
    MapObject *player = PlayerAvatar_GetMapObject(fs->playerAvatar);
    MapObject *obj = openmmo_follow_object(fs);
    int px = MapObject_GetX(player), pz = MapObject_GetZ(player);
    int x = MapObject_GetX(obj), z = MapObject_GetZ(obj);

    if (x == px && z == pz + 1) {
        env->sign = 1;
        return 2;
    }
    if (x == px + 1 && z == pz) {
        env->sign = 0;
        return 3;
    }
    if (x + 1 == px && z == pz) {
        env->sign = 1;
        return 3;
    }
    printf("openmmo: the follower is not beside the player for the heal hop\n");
    fflush(stdout);
    return 2;
}

static BOOL hop_task(FieldTask *task)
{
    FieldSystem *fs = FieldTask_GetFieldSystem(task);
    HopEnv *env = FieldTask_GetEnv(task);
    MapObject *obj = openmmo_follow_object(fs);
    static const int route[2] = { 0xE, 0xC };
    static const s8 dy[8] = { 4, 4, 4, 2, 2, 2, 0, 0 };
    static const s8 dz[8] = { 1, 2, 2, 3, 3, 2, 2, 0 };

    switch (env->state) {
    case 0:
        if (!openmmo_follow_is_active(fs) || openmmo_follow_hidden(fs)) {
            Heap_Free(env);
            return TRUE;
        }
        MapObject_SetPauseMovementOff(obj);
        env->state++;
        /* fallthrough */
    case 1:
        if (LocalMapObj_CheckAnimationFinished(obj)) {
            env->state = (u8)hop_route(fs, env);
        }
        break;
    case 2:
        if (LocalMapObj_CheckAnimationFinished(obj)) {
            LocalMapObj_SetAnimationCode(obj, (enum MovementAction)route[env->n]);
            env->n++;
            if (env->n >= 2) {
                env->state++;
            }
        }
        break;
    case 3:
        if (LocalMapObj_CheckAnimationFinished(obj)) {
            LocalMapObj_SetAnimationCode(obj, MOVEMENT_ACTION_FACE_NORTH);
            env->state++;
        }
        break;
    case 4: {
        VecFx32 v;
        fx32 step = env->sign ? FX32_ONE * 2 : -(FX32_ONE * 2);

        MapObject_GetPosPtr(obj, &v);
        v.y -= dy[env->i] * FX32_ONE;
        v.x += step;
        v.z += dz[env->i] * FX32_ONE;
        MapObject_SetPos(obj, &v);
        env->i++;
        if (env->i >= 8) {
            env->state++;
        }
    } break;
    case 5:
        openmmo_follow_fx_ball(obj, 3);
        env->state++;
        break;
    case 6:
        env->wait++;
        if (env->wait >= 0x14) {
            openmmo_follow_place_on_player(fs, 0);
            openmmo_follow_fx_scale(obj, FX32_ONE, FX32_ONE);
            openmmo_follow_wants_ball_set(obj, 1);
            env->state++;
        }
        break;
    default:
        Heap_Free(env);
        return TRUE;
    }
    return FALSE;
}

static void hop_start(FieldSystem *fs)
{
    HopEnv *env = Heap_AllocAtEnd(HEAP_ID_FIELD1, sizeof(HopEnv));

    memset(env, 0, sizeof *env);
    FieldTask_InitCall(fs->task, hop_task, env);
}

/* ov01_02205D68 / ov01_02205DB4: back into the ball, with the white flash.
 * 1 when a task was started, 0 when the follower was already put away. */
typedef struct {
    int wait;
    u8 palette[64];
} RecallEnv;

static BOOL recall_task(FieldTask *task)
{
    FieldSystem *fs = FieldTask_GetFieldSystem(task);
    RecallEnv *env = FieldTask_GetEnv(task);
    int *state = FieldTask_GetState(task);
    MapObject *obj = openmmo_follow_object(fs);

    if (obj == NULL) {
        Heap_Free(env);
        return TRUE;
    }
    switch (*state) {
    case 0:
        MapObject_SetPauseMovementOff(obj);
        (*state)++;
        /* fallthrough */
    case 1:
        if (LocalMapObj_CheckAnimationFinished(obj)) {
            (*state)++;
        }
        break;
    case 2:
        openmmo_follow_fx_palette_save(obj, env->palette);
        openmmo_follow_fx_ball(obj, 1);
        (*state)++;
        break;
    case 3:
        env->wait++;
        if (env->wait >= 0x14) {
            openmmo_follow_place_on_player(fs, 0);
            openmmo_follow_fx_palette_restore(obj, env->palette);
            openmmo_follow_hide_quiet(fs);
            openmmo_follow_pending_set(obj, 0);
            (*state)++;
        }
        break;
    default:
        Heap_Free(env);
        return TRUE;
    }
    return FALSE;
}

static int recall_start(FieldSystem *fs, FieldTask *task)
{
    RecallEnv *env;

    if (!openmmo_follow_is_active(fs)) {
        return 0;
    }
    if (openmmo_follow_hidden(fs)) {
        openmmo_follow_hide_quiet(fs);
        openmmo_follow_place_on_player(fs, 0);
        return 0;
    }
    env = Heap_AllocAtEnd(HEAP_ID_FIELD1, sizeof(RecallEnv));
    memset(env, 0, sizeof *env);
    FieldTask_InitCall(task, recall_task, env);
    return 1;
}

/* ov01_02205EE0 / ov01_02205F00: the follower comes to the player, looks
 * where they look, hops and is recalled with the flash, before a surf. */
static BOOL ride_task(FieldTask *task)
{
    FieldSystem *fs = FieldTask_GetFieldSystem(task);
    int *env = FieldTask_GetEnv(task);
    int *state = FieldTask_GetState(task);
    MapObject *obj = openmmo_follow_object(fs);
    MapObject *player = PlayerAvatar_GetMapObject(fs->playerAvatar);

    if (obj == NULL) {
        Heap_Free(env);
        return TRUE;
    }
    switch (*state) {
    case 0:
        openmmo_follow_reset_movement(obj);
        sub_02062B68(obj);
        (*state)++;
        break;
    case 1:
        if (LocalMapObj_IsAnimationSet(obj) == TRUE) {
            step_toward_player(player, obj);
            (*state)++;
        }
        break;
    case 2:
        if (LocalMapObj_IsAnimationSet(obj) == TRUE) {
            MapObject_Face(obj, MapObject_GetFacingDir(player));
            (*state)++;
        }
        break;
    case 3:
        (*env)++;
        if (*env > 0xA) {
            (*state)++;
        }
        break;
    case 4:
        if (LocalMapObj_IsAnimationSet(obj) == TRUE) {
            openmmo_follow_set_hidden_flags(obj, 0);
            LocalMapObj_SetAnimationCode(obj, MovementAction_TurnActionTowardsDir(
                MapObject_GetFacingDir(player), (enum MovementAction)0x34));
            (*state)++;
        }
        break;
    case 5:
        if (LocalMapObj_IsAnimationSet(obj) == TRUE) {
            openmmo_follow_fx_ball(obj, 2);
            openmmo_follow_hide_quiet(fs);
            (*state)++;
        }
        break;
    default:
        Heap_Free(env);
        return TRUE;
    }
    return FALSE;
}

static void ride_start(FieldTask *task)
{
    int *env = Heap_AllocAtEnd(HEAP_ID_FIELD1, sizeof(int));

    *env = 0;
    FieldTask_InitCall(task, ride_task, env);
}

/* ov02_0224E0BC / ov02_0224E0EC (ScrCmd_598): the player and the follower
 * change places and turn to face each other. */
typedef struct {
    int state;
    MapObject *a;
    MapObject *b;
    int ax, az, afacing, bx, bz;
} SwapEnv;

/* ov02_0224E224 / ov02_0224E2D4 / ov02_0224E2A0 / ov02_0224E26C. */
static int step_between(int ax, int az, int bx, int bz)
{
    if (ax == bx) {
        if (az > bz) {
            return 0xC;
        }
        return 0xD;
    }
    if (az == bz) {
        if (ax > bx) {
            return 0xE;
        }
        return 0xF;
    }
    printf("openmmo: the two are not in line for a swap\n");
    fflush(stdout);
    return 0xD;
}

static int step_opposite(int mv)
{
    switch (mv) {
    case 0xC: return 0xD;
    case 0xD: return 0xC;
    case 0xE: return 0xF;
    case 0xF: return 0xE;
    default: return 0;
    }
}

static int face_opposite(int dir)
{
    switch (dir & 3) {
    case 0: return 1;
    case 1: return 0;
    case 2: return 3;
    default: return 2;
    }
}

static BOOL swap_task(FieldTask *task)
{
    FieldSystem *fs = FieldTask_GetFieldSystem(task);
    SwapEnv *env = FieldTask_GetEnv(task);
    MapObject *obj = openmmo_follow_object(fs);

    if (obj == NULL || env->a == NULL || env->b == NULL) {
        Heap_Free(env);
        return TRUE;
    }
    switch (env->state) {
    case 0:
        MapObject_SetPauseMovementOff(obj);
        env->state++;
        /* fallthrough */
    case 1:
        if (LocalMapObj_IsAnimationSet(env->a) && LocalMapObj_IsAnimationSet(env->b)) {
            MapObject_SetPauseMovementOn(obj);
            env->state++;
        }
        break;
    case 2: {
        int mv;

        env->ax = MapObject_GetX(env->a);
        env->az = MapObject_GetZ(env->a);
        env->afacing = MapObject_GetFacingDir(env->a);
        env->bx = MapObject_GetX(env->b);
        env->bz = MapObject_GetZ(env->b);
        mv = step_between(env->ax, env->az, env->bx, env->bz);
        LocalMapObj_SetAnimationCode(env->a, (enum MovementAction)mv);
        LocalMapObj_SetAnimationCode(env->b, (enum MovementAction)step_opposite(mv));
        env->state++;
    } break;
    case 3:
        if (LocalMapObj_IsAnimationSet(env->a) && LocalMapObj_IsAnimationSet(env->b)) {
            env->state++;
        }
        break;
    case 4:
        LocalMapObj_SetAnimationCode(env->a, (enum MovementAction)face_opposite(MapObject_GetFacingDir(env->a)));
        LocalMapObj_SetAnimationCode(env->b, (enum MovementAction)(env->afacing & 3));
        env->state++;
        break;
    case 5:
        if (LocalMapObj_IsAnimationSet(env->a) && LocalMapObj_IsAnimationSet(env->b)) {
            sub_020656AC(env->a);
            sub_020656AC(env->b);
            Heap_Free(env);
            return TRUE;
        }
        break;
    default:
        Heap_Free(env);
        return TRUE;
    }
    return FALSE;
}

/* ov01_02203AB4 / ov01_02203AD8 (ScrCmd_597): bubble 0 over the follower,
 * waited for. */
static BOOL emote_task(FieldTask *task)
{
    FieldSystem *fs = FieldTask_GetFieldSystem(task);
    void **env = FieldTask_GetEnv(task);
    int *state = FieldTask_GetState(task);

    switch (*state) {
    case 0:
        *env = openmmo_emote_show(openmmo_follow_object(fs), 1);
        (*state)++;
        break;
    default:
        if (openmmo_emote_done(*env)) {
            openmmo_emote_finish(*env);
            Heap_Free(env);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ *
 *  The script commands (src/scrcmd_c.c)
 * ------------------------------------------------------------------ */

BOOL openmmo_scrcmd_follow_599(ScriptContext *ctx);
BOOL openmmo_scrcmd_follow_600(ScriptContext *ctx);
BOOL openmmo_scrcmd_follow_601(ScriptContext *ctx);
BOOL openmmo_scrcmd_follow_602(ScriptContext *ctx);
BOOL openmmo_scrcmd_follow_603(ScriptContext *ctx);
BOOL openmmo_scrcmd_follow_604(ScriptContext *ctx);
BOOL openmmo_scrcmd_follow_605(ScriptContext *ctx);
BOOL openmmo_scrcmd_follow_606(ScriptContext *ctx);
BOOL openmmo_scrcmd_follow_607(ScriptContext *ctx);
BOOL openmmo_scrcmd_follow_608(ScriptContext *ctx);
BOOL openmmo_scrcmd_follow_609(ScriptContext *ctx);
BOOL openmmo_scrcmd_follow_729(ScriptContext *ctx);
BOOL openmmo_scrcmd_follow_730(ScriptContext *ctx);
BOOL openmmo_scrcmd_follow_783(ScriptContext *ctx);
BOOL openmmo_scrcmd_follow_596(ScriptContext *ctx);
BOOL openmmo_scrcmd_follow_597(ScriptContext *ctx);
BOOL openmmo_scrcmd_follow_598(ScriptContext *ctx);
BOOL openmmo_scrcmd_follow_698(ScriptContext *ctx);
BOOL openmmo_scrcmd_follow_727(ScriptContext *ctx);
int openmmo_follow_take_movement_type(MapObject *obj, int type);
int openmmo_follow_lock_all_take(FieldSystem *fs, MapObject *obj);
BOOL openmmo_follow_lock_all_wait(ScriptContext *ctx);

/* ScrCmd_599. */
BOOL openmmo_scrcmd_follow_599(ScriptContext *ctx)
{
    hop_start(ctx->fieldSystem);
    return TRUE;
}

/* ScrCmd_600. */
BOOL openmmo_scrcmd_follow_600(ScriptContext *ctx)
{
    return recall_start(ctx->fieldSystem, ctx->fieldSystem->task) ? TRUE : FALSE;
}

/* ScrCmd_FollowMonFacePlayer: the follower faces the player unless it is a
 * large one whose tile two ahead of the player is blocked, water, or taken. */
BOOL openmmo_scrcmd_follow_601(ScriptContext *ctx)
{
    FieldSystem *fs = ctx->fieldSystem;
    MapObject *obj = openmmo_follow_object(fs);
    int do_face = 1;

    if (!openmmo_follow_is_active(fs) || obj == NULL) {
        return FALSE;
    }
    if (openmmo_follow_is_large(obj)) {
        MapObject *player = PlayerAvatar_GetMapObject(fs->playerAvatar);
        int facing = PlayerAvatar_GetFacingDir(fs->playerAvatar);
        int x = MapObject_GetX(player) + MapObject_GetDxFromDir(facing) * 2;
        int z = MapObject_GetZ(player) + MapObject_GetDzFromDir(facing) * 2;
        u8 b = TerrainCollisionManager_GetTileBehavior(fs, x, z);

        if (TerrainCollisionManager_CheckCollision(fs, x, z)
            || TileBehavior_IsSurfable(b)
            || sub_0206326C(fs->mapObjMan, x, z, 0) != NULL) {
            do_face = 0;
        }
    }
    if (do_face) {
        MapObject_TryFace(obj, face_opposite(PlayerAvatar_GetFacingDir(fs->playerAvatar)));
    }
    return FALSE;
}

/* ScrCmd_ToggleFollowingPokemonMovement. */
BOOL openmmo_scrcmd_follow_602(ScriptContext *ctx)
{
    u16 mode = ScriptContext_ReadHalfWord(ctx);
    MapObject *obj = openmmo_follow_object(ctx->fieldSystem);

    if (openmmo_follow_is_active(ctx->fieldSystem) && obj != NULL) {
        if (mode) {
            MapObject_SetPauseMovementOn(obj);
        } else {
            MapObject_SetPauseMovementOff(obj);
        }
    }
    return FALSE;
}

static BOOL wait_follow_paused(ScriptContext *ctx)
{
    MapObject *obj = openmmo_follow_object(ctx->fieldSystem);

    return obj == NULL || LocalMapObj_CheckAnimationFinished(obj);
}

/* ScrCmd_WaitFollowingPokemonMovement. */
BOOL openmmo_scrcmd_follow_603(ScriptContext *ctx)
{
    if (openmmo_follow_is_active(ctx->fieldSystem)) {
        ScriptContext_Pause(ctx, wait_follow_paused);
    }
    return TRUE;
}

/* ScrCmd_FollowingPokemonMovement: a movement type, 48, 55 or 56. */
BOOL openmmo_scrcmd_follow_604(ScriptContext *ctx)
{
    u16 type = ScriptContext_ReadHalfWord(ctx);

    if (openmmo_follow_is_active(ctx->fieldSystem)) {
        openmmo_follow_switch_type(ctx->fieldSystem, type);
    }
    return TRUE;
}

/* ScrCmd_605. */
BOOL openmmo_scrcmd_follow_605(ScriptContext *ctx)
{
    u8 dir = ScriptContext_ReadByte(ctx);
    u8 facing = ScriptContext_ReadByte(ctx);
    FieldSystem *fs = ctx->fieldSystem;

    if (openmmo_follow_is_active(fs)) {
        place_beside_player(PlayerAvatar_GetMapObject(fs->playerAvatar),
                            openmmo_follow_object(fs), dir, facing);
    }
    return FALSE;
}

/* ScrCmd_606. */
BOOL openmmo_scrcmd_follow_606(ScriptContext *ctx)
{
    FieldSystem *fs = ctx->fieldSystem;
    MapObject *obj = openmmo_follow_object(fs);

    if (openmmo_follow_is_active(fs) && obj != NULL
        && openmmo_follow_permission_for(openmmo_follow_species_of(obj), fs->location->mapHeaderID)) {
        openmmo_follow_wants_ball_set(obj, 1);
        openmmo_follow_bit1_set(obj, 1);
        openmmo_follow_place_on_player(fs, 1);
    }
    return FALSE;
}

/* ScrCmd_607. */
BOOL openmmo_scrcmd_follow_607(ScriptContext *ctx)
{
    FieldSystem *fs = ctx->fieldSystem;
    MapObject *obj = openmmo_follow_object(fs);

    if (openmmo_follow_is_active(fs) && obj != NULL
        && openmmo_follow_permission_for(openmmo_follow_species_of(obj), fs->location->mapHeaderID)) {
        openmmo_follow_place_on_player(fs, 1);
    }
    return FALSE;
}

/* ScrCmd_608. */
BOOL openmmo_scrcmd_follow_608(ScriptContext *ctx)
{
    if (openmmo_follow_is_active(ctx->fieldSystem)) {
        openmmo_follow_fx_ball(openmmo_follow_object(ctx->fieldSystem), 0);
    }
    return FALSE;
}

/* ScrCmd_609. */
BOOL openmmo_scrcmd_follow_609(ScriptContext *ctx)
{
    if (openmmo_follow_is_active(ctx->fieldSystem)) {
        openmmo_follow_reset_movement(openmmo_follow_object(ctx->fieldSystem));
    }
    return TRUE;
}

/* ScrCmd_729. */
BOOL openmmo_scrcmd_follow_729(ScriptContext *ctx)
{
    u16 *dest = ScriptContext_GetVarPointer(ctx);

    *dest = openmmo_follow_is_active(ctx->fieldSystem) ? 1 : 0;
    return FALSE;
}

/* ScrCmd_730. */
BOOL openmmo_scrcmd_follow_730(ScriptContext *ctx)
{
    u16 *dest = ScriptContext_GetVarPointer(ctx);

    if (!openmmo_follow_is_active(ctx->fieldSystem)) {
        *dest = 1;
    } else {
        *dest = openmmo_follow_hidden(ctx->fieldSystem) ? 1 : 0;
    }
    return FALSE;
}

/* ScrCmd_SetFollowMonInhibitState. */
BOOL openmmo_scrcmd_follow_783(ScriptContext *ctx)
{
    openmmo_follow_set_inhibit(ctx->fieldSystem, ScriptContext_ReadByte(ctx));
    return FALSE;
}

/* ScrCmd_596: whether the follower is one of the large ones. */
BOOL openmmo_scrcmd_follow_596(ScriptContext *ctx)
{
    u16 *dest = ScriptContext_GetVarPointer(ctx);
    MapObject *obj = openmmo_follow_object(ctx->fieldSystem);

    *dest = obj != NULL ? (u16)openmmo_follow_is_large(obj) : 0;
    return FALSE;
}

/* ScrCmd_597. */
BOOL openmmo_scrcmd_follow_597(ScriptContext *ctx)
{
    void **env = Heap_AllocAtEnd(HEAP_ID_FIELD1, sizeof(void *));

    *env = NULL;
    FieldTask_InitCall(ctx->fieldSystem->task, emote_task, env);
    return TRUE;
}

/* ScrCmd_598. */
BOOL openmmo_scrcmd_follow_598(ScriptContext *ctx)
{
    u16 mode = ScriptContext_ReadHalfWord(ctx);
    FieldSystem *fs = ctx->fieldSystem;
    SwapEnv *env;
    MapObject *player = PlayerAvatar_GetMapObject(fs->playerAvatar);
    MapObject *obj = openmmo_follow_object(fs);

    if (mode != 1 && mode != 2) {
        printf("openmmo: swap mode %u is not one of the two\n", (unsigned)mode);
        fflush(stdout);
        return FALSE;
    }
    env = Heap_AllocAtEnd(HEAP_ID_FIELD1, sizeof(SwapEnv));
    memset(env, 0, sizeof *env);
    env->a = mode == 1 ? player : obj;
    env->b = mode == 1 ? obj : player;
    FieldTask_InitCall(fs->task, swap_task, env);
    return TRUE;
}

/* ScrCmd_FollowerPokeIsEventTrigger: whether the party member in `slot` is
 * the event Pokemon named. The metadata it matches (MonMetadataMatchesEvent)
 * is a Wonder Card's, which no Pokemon of this game carries; so the answer
 * a party without one gives, which is no. */
BOOL openmmo_scrcmd_follow_698(ScriptContext *ctx)
{
    u16 *dest;

    (void)ScriptContext_ReadByte(ctx);
    (void)ScriptContext_GetVar(ctx);
    dest = ScriptContext_GetVarPointer(ctx);
    *dest = 0;
    return FALSE;
}

/* ScrCmd_GetFollowPokePartyIndex: the slot of the first alive member. */
BOOL openmmo_scrcmd_follow_727(ScriptContext *ctx)
{
    u16 *dest = ScriptContext_GetVarPointer(ctx);
    Party *party = ctx->fieldSystem->saveData != NULL ? SaveData_GetParty(ctx->fieldSystem->saveData) : NULL;
    int i, n;

    *dest = 0;
    if (party == NULL) {
        return FALSE;
    }
    n = Party_GetCurrentCount(party);
    for (i = 0; i < n; i++) {
        Pokemon *mon = Party_GetPokemonBySlotIndex(party, i);

        if (mon != NULL && !Pokemon_GetValue(mon, MON_DATA_IS_EGG, NULL)
            && Pokemon_GetValue(mon, MON_DATA_HP, NULL) > 0) {
            *dest = (u16)i;
            break;
        }
    }
    return FALSE;
}

/* ScrCmd_SetMovementType on the follower (the source's `ScrCmd_109 253, N`):
 * the engine's own switch would bind this game's type N, which is another
 * thing entirely; the follower's is the mode switch. 1 when taken. */
int openmmo_follow_take_movement_type(MapObject *obj, int type)
{
    FieldSystem *fs;

    if (obj == NULL || MapObject_GetLocalID(obj) != OPENMMO_FOLLOW_ID) {
        return 0;
    }
    fs = MapObject_FieldSystem(obj);
    openmmo_follow_switch_type(fs, type);
    return 1;
}

/* ScrCmd_LockAll's follower clause: a follower in the middle of a step is
 * let finish it before it is paused. 1 when the script should wait. */
int openmmo_follow_lock_all_take(FieldSystem *fs, MapObject *obj)
{
    if (obj == NULL || MapObject_GetLocalID(obj) != OPENMMO_FOLLOW_ID) {
        return 0;
    }
    if (!openmmo_follow_is_active(fs) || !MapObject_IsMoving(obj)) {
        return 0;
    }
    MapObject_SetPauseMovementOff(obj);
    return 1;
}

/* _WaitFollowMonPaused. */
BOOL openmmo_follow_lock_all_wait(ScriptContext *ctx)
{
    MapObject *obj = openmmo_follow_object(ctx->fieldSystem);

    if (obj == NULL || MapObject_IsMoving(obj) == FALSE) {
        if (obj != NULL) {
            MapObject_SetPauseMovementOn(obj);
        }
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ *
 *  The field around it: steps, doors, warps, the bike, the water
 * ------------------------------------------------------------------ */

/* sub_0205D4B4 / sub_0205D2D0: every action the player commits arms the
 * follower with that action and the tile they stand on, except a turn in
 * place and anything committed while surfing. The tile is the current one,
 * which is the source's "previous" between steps. */
void openmmo_follow_player_moved(FieldSystem *fs, MapObject *player, int action);

static int g_landing_pending;   /* the surf's end: place it once the step out lands */
static int g_landing_dir;

void openmmo_follow_player_moved(FieldSystem *fs, MapObject *player, int action)
{
    if (fs == NULL || player == NULL || !openmmo_follow_is_active(fs)) {
        return;
    }
    if (action >= MOVEMENT_ACTION_FACE_NORTH && action <= MOVEMENT_ACTION_FACE_EAST) {
        return;
    }
    if (PlayerAvatar_GetPlayerState(fs->playerAvatar) == PLAYER_AVATAR_SURFING) {
        return;
    }
    if (g_landing_pending) {
        return;
    }
    openmmo_follow_arm(fs, action, MapObject_GetX(player), MapObject_GetZ(player));
}

/* The door's own step in (ov01_021E926A): the follower is sent after the
 * player with the same action, if it is out. */
void openmmo_follow_door_step(FieldSystem *fs, int action);

void openmmo_follow_door_step(FieldSystem *fs, int action)
{
    MapObject *player;

    if (fs == NULL || !openmmo_follow_is_active(fs) || openmmo_follow_hidden(fs)) {
        return;
    }
    player = PlayerAvatar_GetMapObject(fs->playerAvatar);
    openmmo_follow_arm(fs, action, MapObject_GetX(player), MapObject_GetZ(player));
}

/* The escalator's step in (ov01_021E9A00): the same, out or not. */
void openmmo_follow_escalator_step(FieldSystem *fs, int action);

void openmmo_follow_escalator_step(FieldSystem *fs, int action)
{
    MapObject *player;

    if (fs == NULL || !openmmo_follow_is_active(fs)) {
        return;
    }
    player = PlayerAvatar_GetMapObject(fs->playerAvatar);
    openmmo_follow_arm(fs, action, MapObject_GetX(player), MapObject_GetZ(player));
}

/* A step out of a door, off stairs or an escalator done (ov01_021E952E,
 * _02056760, _021E988E): the follower is put under the player, facing as
 * given, and left hidden to emerge on their first step. */
void openmmo_follow_stepped_out(FieldSystem *fs, int dir);

void openmmo_follow_stepped_out(FieldSystem *fs, int dir)
{
    if (fs == NULL || !openmmo_follow_is_active(fs)) {
        return;
    }
    openmmo_follow_place_on_player(fs, dir);
}

/* The arrivals that also start the follow over (unk_02056680.s: fly, dig,
 * an escape rope, a warp tile): under the player, facing `dir`, the driven
 * type, hidden and waiting; and out of the ball when it shows, because the
 * driver marked the map left as one it could not follow into. */
void openmmo_follow_arrived_by_air(FieldSystem *fs, int dir);

void openmmo_follow_arrived_by_air(FieldSystem *fs, int dir)
{
    MapObject *obj;

    if (fs == NULL || !openmmo_follow_is_active(fs)) {
        return;
    }
    obj = openmmo_follow_object(fs);
    openmmo_follow_wants_ball_set(obj, 1);
    openmmo_follow_place_on_player(fs, dir);
    openmmo_follow_switch_type(fs, FOLLOW_TYPE_DRIVEN);
    openmmo_follow_hide(obj, 1);
}

/* The warp driver (sub_02055DBC cases 0-2 and 7; sub_02053CCC cases 0 and 5). */
static void *g_leave_fx;
static int g_leave_phase;

int openmmo_follow_leave_ready(FieldSystem *fs, int dest_header, int always);

int openmmo_follow_leave_ready(FieldSystem *fs, int dest_header, int always)
{
    MapObject *obj;

    if (fs == NULL) {
        return 1;
    }
    if (g_leave_phase == 0) {
        g_leave_fx = NULL;
        g_leave_phase = 1;
        obj = openmmo_follow_object(fs);
        if (openmmo_follow_is_active(fs) && obj != NULL && !openmmo_follow_hidden(fs)
            && PlayerAvatar_GetPlayerState(fs->playerAvatar) != PLAYER_AVATAR_CYCLING) {
            if (always || !openmmo_follow_permission_for(openmmo_follow_species_of(obj), dest_header)) {
                g_leave_fx = openmmo_follow_fx_ball(obj, 1);
            }
        }
    }
    if (g_leave_fx != NULL && openmmo_follow_fx_running(g_leave_fx)) {
        return 0;
    }
    g_leave_fx = NULL;
    g_leave_phase = 0;
    return 1;
}

void openmmo_follow_left(FieldSystem *fs);

void openmmo_follow_left(FieldSystem *fs)
{
    if (fs == NULL) {
        return;
    }
    if (openmmo_follow_is_active(fs) && openmmo_follow_hidden(fs) && !openmmo_follow_permission(fs)) {
        openmmo_follow_mark_left_behind(fs);
    }
    if (fs->location != NULL) {
        openmmo_follow_save_map(fs->location->mapHeaderID);
    }
    /* field_warp_tasks.c:271: every map change clears the scene's inhibit. */
    openmmo_follow_warp_clears_inhibit();
}

void openmmo_follow_arrived(FieldSystem *fs);

void openmmo_follow_arrived(FieldSystem *fs)
{
    MapObject *obj;

    if (fs == NULL || !openmmo_follow_is_active(fs)) {
        return;
    }
    obj = openmmo_follow_object(fs);
    if (obj != NULL && !openmmo_follow_permission(fs)) {
        openmmo_follow_wants_ball_set(obj, 1);
        openmmo_follow_place_on_player(fs, PlayerAvatar_GetFacingDir(fs->playerAvatar));
    }
}

/*
 * The bike (Task_MountOrDismountBicycle). Before the change: wait for the follower's step to
 * end, then its type is the hidden lockstep for a mount and the driven one for a dismount.
 */
int openmmo_follow_bike_ready(FieldSystem *fs);

int openmmo_follow_bike_ready(FieldSystem *fs)
{
    MapObject *obj;

    if (fs == NULL || !openmmo_follow_is_active(fs)) {
        return 1;
    }
    obj = openmmo_follow_object(fs);
    if (obj != NULL && !LocalMapObj_CheckAnimationFinished(obj)) {
        return 0;
    }
    openmmo_follow_switch_type(fs, PlayerAvatar_GetPlayerState(fs->playerAvatar) == PLAYER_AVATAR_CYCLING
                                       ? FOLLOW_TYPE_DRIVEN : FOLLOW_TYPE_LOCKSTEP);
    return 1;
}

void openmmo_follow_dismounted(FieldSystem *fs);

void openmmo_follow_dismounted(FieldSystem *fs)
{
    MapObject *obj;

    if (fs == NULL) {
        return;
    }
    openmmo_follow_place_on_player(fs, PlayerAvatar_GetFacingDir(fs->playerAvatar));
    obj = openmmo_follow_object(fs);
    if (openmmo_follow_is_active(fs) && obj != NULL) {
        openmmo_follow_wants_ball_set(obj, 1);
        openmmo_follow_hide(obj, 1);
    }
}

void openmmo_follow_mounted(FieldSystem *fs, void *task);

void openmmo_follow_mounted(FieldSystem *fs, void *task)
{
    MapObject *obj;

    if (fs == NULL) {
        return;
    }
    recall_start(fs, (FieldTask *)task);
    obj = openmmo_follow_object(fs);
    if (openmmo_follow_is_active(fs) && obj != NULL) {
        openmmo_follow_wants_ball_set(obj, 0);
    }
}

/*
 * The water (ov01_021F2118). After the cut-in and before the mount, a follower that is out
 * comes to the player and hops into its ball; one already put away is placed under them.
 */
static int g_ride_queued;

int openmmo_follow_ride_prepare(FieldSystem *fs, void *task);

int openmmo_follow_ride_prepare(FieldSystem *fs, void *task)
{
    if (fs == NULL || !openmmo_follow_is_active(fs)) {
        return 0;
    }
    if (g_ride_queued) {
        g_ride_queued = 0;
        return 0;
    }
    g_ride_queued = 1;
    if (openmmo_follow_hidden(fs)) {
        return recall_start(fs, (FieldTask *)task);
    }
    ride_start((FieldTask *)task);
    return 1;
}

void openmmo_follow_ride_mounted(FieldSystem *fs, int dir);

void openmmo_follow_ride_mounted(FieldSystem *fs, int dir)
{
    g_ride_queued = 0;
    if (fs == NULL || !openmmo_follow_is_active(fs)) {
        return;
    }
    openmmo_follow_place_on_player(fs, dir);
    openmmo_follow_switch_type(fs, FOLLOW_TYPE_LOCKSTEP);
}

/* The water's end (ov01_021F2628's last act): once the player's step onto
 * land is over, the follower is under them, hidden and waiting, facing
 * south, on the driven type. The state change comes before that step is
 * committed here, so it is noted and finished when the step lands. */
void openmmo_follow_ride_ending(FieldSystem *fs, int dir);

void openmmo_follow_ride_ending(FieldSystem *fs, int dir)
{
    if (fs == NULL || !openmmo_follow_is_active(fs)) {
        return;
    }
    g_landing_pending = 1;
    g_landing_dir = dir;
}

static void landing_tick(FieldSystem *fs)
{
    MapObject *player, *obj;

    if (!g_landing_pending) {
        return;
    }
    if (fs->playerAvatar == NULL) {
        g_landing_pending = 0;
        return;
    }
    player = PlayerAvatar_GetMapObject(fs->playerAvatar);
    if (!LocalMapObj_CheckAnimationFinished(player)) {
        return;
    }
    g_landing_pending = 0;
    obj = openmmo_follow_object(fs);
    if (obj == NULL || !openmmo_follow_is_active(fs)) {
        return;
    }
    openmmo_follow_place_on_player(fs, g_landing_dir);
    openmmo_follow_hide(obj, 1);
    openmmo_follow_face(fs, 1);
    openmmo_follow_switch_type(fs, FOLLOW_TYPE_DRIVEN);
}

/* Once a frame, from the field. */
void openmmo_follow_move_tick(FieldSystem *fs);

void openmmo_follow_move_tick(FieldSystem *fs)
{
    if (fs == NULL) {
        return;
    }
    landing_tick(fs);
}

/* ------------------------------------------------------------------ *
 *  The draw: the hop and the dip
 * ------------------------------------------------------------------ */

/*
 * ov01_021F8D80's two additions to the follower's own height, in units: the hop arc while a
 * catch-up step runs, and ov01_021F8FC0's two-unit dip on the frames of the walk cycle where a
 * foot is down, taken from the sprite's own frame counter.
 */
fx32 openmmo_follow_draw_offset(MapObject *obj, fx32 frame_num, int walking);

fx32 openmmo_follow_draw_offset(MapObject *obj, fx32 frame_num, int walking)
{
    fx32 y = 0;
    int dir, frame, cls, dip = 0;

    if (obj == NULL || MapObject_GetLocalID(obj) != OPENMMO_FOLLOW_ID) {
        return 0;
    }
    y += hop_offset(obj);
    if (!walking) {
        return y;
    }
    dir = MapObject_GetFacingDir(obj);
    frame = (frame_num + (FX32_ONE >> 1)) / FX32_ONE;
    switch (dir) {
    case 1: frame -= 0x14; break;
    case 2: frame -= 0x28; break;
    case 3: frame -= 0x3C; break;
    default: break;
    }
    cls = openmmo_follow_dip_class(obj);
    if (cls != 0) {
        if (dir == 1) {
            dip = frame < 5 || frame >= 15;
        } else {
            dip = frame < 10;
        }
    } else {
        dip = frame < 5 || (frame >= 10 && frame < 15);
    }
    if (dip) {
        y -= FX32_ONE * 2;
    }
    return y;
}

void openmmo_follow_move_forget(void)
{
    g_type = FOLLOW_TYPE_DRIVEN;
    g_leave_fx = NULL;
    g_leave_phase = 0;
    g_ride_queued = 0;
    g_landing_pending = 0;
}
