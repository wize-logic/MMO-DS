/* What a person can look like in this engine. */
#ifndef OPENMMO_APPEARANCE_H
#define OPENMMO_APPEARANCE_H

#include "game.h"

/* Engine graphics ids for the two walking trainer models PlayerAvatar_New
 * builds from gender when the version bit is off. Measured: state 0, gender
 * 0 → 0; state 0, gender 1 → 97. */
#define MMO_APPEAR_GFX_PLAYER_M 0
#define MMO_APPEAR_GFX_PLAYER_F 97

/* A SkinSet type at or above this is an index into this catalog
 * (type - BASE), not an official cosmetic. Observed official types in the
 * committed captures are all below 64. */
#define MMO_APPEAR_BODY_TYPE_BASE 512

typedef struct {
    int         gfx;       /* object-event graphics id */
    const char *name;      /* what a player is shown / what --body accepts */
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

/* True when `gfx` is a row this engine can seat on a remote body. */
int mmo_appearance_is_drawable(int gfx);
int mmo_appearance_is_offered(int gfx);

/* Pack / unpack our catalog index as a SkinSet type. Types below BASE are
 * not ours and these helpers refuse them. */
int mmo_appearance_body_type(int index);
int mmo_appearance_index_from_type(int type);

/* Gender's trainer model, unless a kept SkinSet names an offered (or at
 * least drawable) catalog row via a type >= BASE. Unknown cosmetics stay
 * on the SkinSet and do not change the body. */
int mmo_appearance_resolve(int gender, const mmo_skin_set *skins);

/* Write our catalog index into slot 0 (forehead) as type BASE+index,
 * colour 0. That slot is unused in the committed official creates. Returns
 * 0, or -1 if the index is not an offered row. */
int mmo_appearance_apply_body(mmo_skin_set *skins, int index);

#endif /* OPENMMO_APPEARANCE_H */
