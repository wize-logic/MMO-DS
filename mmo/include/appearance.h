/* What a person can look like: six trainers, two a game. */
#ifndef OPENMMO_APPEARANCE_H
#define OPENMMO_APPEARANCE_H

#include "game.h"

/* Engine graphics ids for the two walking trainer models PlayerAvatar_New
 * builds from gender when the version bit is off. Measured: state 0, gender
 * 0 → 0; state 0, gender 1 → 97. */
#define MMO_APPEAR_GFX_PLAYER_M 0
#define MMO_APPEAR_GFX_PLAYER_F 97

/* A SkinSet type at or above this names a look. */
#define MMO_APPEAR_BODY_TYPE_BASE 512
#define MMO_APPEAR_LOOK_TYPE_BASE 640

/*
 * The composed looks' overworld graphics ids. Each look owns a band of MMO_APPEAR_LOOK_STRIDE
 * ids, one per player state (MMO_APPEAR_STATE_*), and its walking sheet is the first.
 */
#define MMO_APPEAR_LOOK_GFX_BASE 768
#define MMO_APPEAR_LOOK_STRIDE   16
#define MMO_APPEAR_LOOK_COUNT    4

/* Which sheet of a look a graphics id in its band is. The order is ours;
 * the composer and the client read the same list. A state a game never
 * drew (Platinum's Poketch on Ethan) has no sheet, and the client answers
 * the look's walk for it, which is the standing pose. */
enum {
    MMO_APPEAR_STATE_WALK = 0,   /* walk and run, 32 frames */
    MMO_APPEAR_STATE_BIKE,       /* 24 frames */
    MMO_APPEAR_STATE_SURF,       /* the rider alone, 4 frames */
    MMO_APPEAR_STATE_FIELD_MOVE, /* holding a ball out, 4 frames */
    MMO_APPEAR_STATE_FISHING,    /* 16 frames */
    MMO_APPEAR_STATE_SAVE,       /* 2 frames */
    MMO_APPEAR_STATE_HEAL,       /* the Pokemon Center bow, 12 frames */
    MMO_APPEAR_STATE_POKETCH,    /* looking at the wrist, 12 frames */
    MMO_APPEAR_STATE_SPRAYDUCK,  /* watering, 7 frames */
    MMO_APPEAR_STATE_COUNT
};

typedef enum {
    MMO_APPEAR_GAME_PLATINUM = 0,
    MMO_APPEAR_GAME_HEARTGOLD,
    MMO_APPEAR_GAME_BLACKWHITE
} mmo_appear_game;

typedef struct {
    int         gfx;       /* walking graphics id */
    const char *name;      /* what --body accepts */
    const char *label;     /* what the creator prints for the row */
    int         game;      /* mmo_appear_game */
    int         gender;    /* 0 boy, 1 girl */
    int         look;      /* 0..MMO_APPEAR_LOOK_COUNT-1 for a composed look, -1 for Platinum's */
    int         type;      /* the SkinSet type that names this row */
    int         offered;   /* the creator may pick this */
    int         drawable;  /* a remote PlayerAvatar can wear it and walk */
    /* Why the row is greyed. NULL exactly when the row is offered. */
    const char *reason;
} mmo_appearance;

int                   mmo_appearance_count(void);
const mmo_appearance *mmo_appearance_at(int index);
const mmo_appearance *mmo_appearance_by_name(const char *name);
const mmo_appearance *mmo_appearance_by_gfx(int gfx);

/* The walking trainer model for this gender (0 or 97). */
int mmo_appearance_gender_gfx(int gender);

/* True when `gfx` is a row a remote body can be seated on. */
int mmo_appearance_is_drawable(int gfx);
int mmo_appearance_is_offered(int gfx);

/* Pack / unpack a catalog row as a SkinSet type. Types below BASE are not
 * ours and these helpers refuse them; a type in our range that names no
 * row today (an index of the old catalog) answers -1 the same way. */
int mmo_appearance_body_type(int index);
int mmo_appearance_index_from_type(int type);

/* Gender's trainer model, unless a kept SkinSet names a drawable row via a
 * type in our range. Unknown cosmetics stay on the SkinSet and do not
 * change the body. */
int mmo_appearance_resolve(int gender, const mmo_skin_set *skins);

/* Write a catalog row into slot 0 (forehead) as its type, colour 0. That
 * slot is unused in the committed official creates. Returns 0, or -1 if the
 * index is not an offered row. */
int mmo_appearance_apply_body(mmo_skin_set *skins, int index);

/* Which composed look a graphics id belongs to (any sheet of its band), or
 * -1 for an id outside the bands, Platinum's own, or a person's. */
int mmo_appearance_look_of_gfx(int gfx);

/* The graphics id of one sheet of a composed look. */
int mmo_appearance_look_gfx(int look, int state);

/* The row whose walking sheet is this look's. */
const mmo_appearance *mmo_appearance_by_look(int look);

#endif /* OPENMMO_APPEARANCE_H */
