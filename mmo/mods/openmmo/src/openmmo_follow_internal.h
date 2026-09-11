/* What the four follower files share. */
#ifndef OPENMMO_FOLLOW_INTERNAL_H
#define OPENMMO_FOLLOW_INTERNAL_H

#include "field/field_system.h"
#include "map_object.h"

/*
 * HeartGold's `obj_partner_poke`: the object id every one of its scripts and routines finds
 * the follower by.
 */
#define OPENMMO_FOLLOW_ID 253

/* The three movement types the follower runs on, in the source's numbers.
 * 0x30 is this game's MOVEMENT_TYPE_FOLLOW_PLAYER slot, which is what the
 * object carries; the other two are modes of the same object here. */
#define FOLLOW_TYPE_DRIVEN   0x30   /* stepped by the player's own steps */
#define FOLLOW_TYPE_POLLED   0x37   /* watches the player's tile itself */
#define FOLLOW_TYPE_LOCKSTEP 0x38   /* hidden, mirrors the player's action */

/* MapObject data slots, as FollowMon_SetObjectParams lays them out. */
#define FOLLOW_PARAM_SPECIES 0
#define FOLLOW_PARAM_TP      1   /* (tp_param[1] << 8) | tp_param[2] */
#define FOLLOW_PARAM_BITS    2   /* the bit field below */

/* struct FollowMon (pokeheartgold include/field_system.h) plus the three
 * fields of SaveFollowMon. `arm_*` are unk4/unk8/unkC/unk1C: what
 * ov01_02205990 writes and the driven movement type reads. */
typedef struct {
    int active;         /* followMon.active */
    int refuse_pending; /* followMon.unk15: the map turned the lead away */
    int species;
    int form;
    int gender;
    int shiny;
    u32 personality;    /* ours: a lead the server changed under us */
    int inhibit;        /* SaveFollowMon.inhibitFlag */
    int saved_map;      /* SaveFollowMon.mapNo */
    int unused2;        /* SaveFollowMon's unused two bits, kept for the record */
    int arm_anim;       /* followMon.unk4 */
    int arm_x;          /* followMon.unk8 */
    int arm_z;          /* followMon.unkC */
    int arm_state;      /* followMon.unk1C */
} openmmo_follow_mon;

openmmo_follow_mon *openmmo_follow_state(void);

/* follow_mon.c */
MapObject *openmmo_follow_object(FieldSystem *fs);          /* FollowMon_GetMapObject */
int openmmo_follow_is_active(FieldSystem *fs);              /* FollowMon_IsActive */
int openmmo_follow_is_visible(FieldSystem *fs);             /* FollowMon_IsVisible */
int openmmo_follow_permission(FieldSystem *fs);             /* FollowMon_GetPermission */
int openmmo_follow_permission_for(int species, int header); /* FollowMon_GetPermissionBySpeciesAndMap */
int openmmo_follow_species_of(MapObject *obj);              /* FollowMon_GetSpecies */
void openmmo_follow_init_map_object(FieldSystem *fs);       /* FollowMon_InitMapObject */
void openmmo_follow_change_mon(FieldSystem *fs);            /* FollowMon_ChangeMon */
void openmmo_follow_hide(MapObject *obj, int on);           /* sub_02069DC8 */
void openmmo_follow_set_hidden_flags(MapObject *obj, int on); /* sub_0206A040 */
void openmmo_follow_hide_quiet(FieldSystem *fs);            /* sub_0206A054 */
void openmmo_follow_bit1_set(MapObject *obj, int on);       /* sub_02069DEC */
int openmmo_follow_bit1_get(MapObject *obj);                /* sub_02069E14 */
void openmmo_follow_pending_set(MapObject *obj, int dir);   /* sub_02069E28 */
int openmmo_follow_pending_get(MapObject *obj);             /* sub_02069EC0 */
void openmmo_follow_stored_anim_set(MapObject *obj, int a); /* sub_02069E50 */
int openmmo_follow_stored_anim_get(MapObject *obj);         /* sub_02069ED4 */
void openmmo_follow_wants_ball_set(MapObject *obj, int on); /* sub_02069E84 */
int openmmo_follow_wants_ball_get(MapObject *obj);          /* sub_02069EAC */
int openmmo_follow_is_large(MapObject *obj);                /* ov01_022055DC */
int openmmo_follow_dip_class(MapObject *obj);               /* ov01_022055B0 */
int openmmo_follow_no_dip(MapObject *obj);                  /* ov01_02205584 */

/* openmmo_follow_move.c */
void openmmo_follow_move_bind(MapObject *obj, int type);    /* the type's init, bound */
void openmmo_follow_switch_type(FieldSystem *fs, int type); /* sub_0205FC94 on the follower */
int openmmo_follow_type(void);
void openmmo_follow_arm(FieldSystem *fs, int anim, int x, int z); /* ov01_02205990 */
void openmmo_follow_place_on_player(FieldSystem *fs, int dir);   /* ov01_02205790 */
int openmmo_follow_hidden(FieldSystem *fs);                       /* ov01_022057C4 */
void openmmo_follow_mark_left_behind(FieldSystem *fs);            /* ov01_022057D0 */
void openmmo_follow_reset_movement(MapObject *obj);               /* sub_020659CC */
void openmmo_follow_face(FieldSystem *fs, int dir);               /* ov01_0220609C */
void openmmo_follow_move_forget(void);

/* openmmo_follow_fx.c */
void *openmmo_follow_fx_ball(MapObject *obj, int kind);     /* ov01_0220329C */
int openmmo_follow_fx_running(void *anim);                  /* sub_02068CCC */
void openmmo_follow_fx_forget(void);

#endif /* OPENMMO_FOLLOW_INTERNAL_H */
