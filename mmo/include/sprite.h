#ifndef OPENMMO_SPRITE_H
#define OPENMMO_SPRITE_H
/* Which picture a species has, and where this client runs out of them. */
#include "mmo.h"

/* The two archives the engine draws Pokemon out of. Their names are the ROM's:
 * poketool/pokegra/pl_pokegra.narc and .../pl_otherpoke.narc. */
enum mmo_sprite_archive {
    MMO_SPRITE_ARCHIVE_POKEGRA = 0,
    MMO_SPRITE_ARCHIVE_OTHERPOKE = 1
};

/* One resolved picture: two members of one archive. */
typedef struct {
    int archive;   /* enum mmo_sprite_archive */
    int character; /* the member holding the tiles */
    int palette;   /* the member holding the palette */
    /* The engine splotches Spinda's front sprite from its personality value.
     * This says the caller must do that; it is not a second archive member. */
    int spinda_spots;
} mmo_sprite_ref;

/* The faces the engine numbers, and it does not number them 0 and 1: the back
 * sprite is 0 and the front is 2, because both arms of the arithmetic use the
 * value itself. Pass one of these, not a boolean. */
#define MMO_SPRITE_FACE_BACK_ID  0
#define MMO_SPRITE_FACE_FRONT_ID 2

/* Genders in the engine's own numbering, which is what the wire's gender byte
 * is mapped onto elsewhere in this client. Only "is it female" reaches the
 * arithmetic, male and genderless share a sprite. */
#define MMO_SPRITE_GENDER_MALE_ID   0
#define MMO_SPRITE_GENDER_FEMALE_ID 1
#define MMO_SPRITE_GENDER_NONE_ID   2

/*
 * The last species this client has a picture for. On the cartridge that is 493 (pl_pokegra)
 * plus the two egg slots after it; an overlay that grew pl_pokegra past 2964 members raises it
 * to the last complete default-arm id the live count holds.
 */
int mmo_sprite_species_max(void);
#define MMO_SPRITE_EGG_ID     494
#define MMO_SPRITE_BAD_EGG_ID 495
/* Cartridge pl_pokegra fat. The live count starts here and grows with an
 * overlay; it never shrinks below it. */
#define MMO_SPRITE_POKEGRA_ROM_MEMBERS 2964
/* Six of those a species; and height.narc seats each sheet with one byte per
 * (face, gender), four a species over the same 494. sprite.c holds the six
 * to sprite_index.gen.h's stride. */
#define MMO_SPRITE_POKEGRA_PER_SPECIES 6
#define MMO_SPRITE_HEIGHT_PER_SPECIES 4
#define MMO_SPRITE_HEIGHT_ROM_MEMBERS 1976

int mmo_sprite_pokegra_members(void);
void mmo_sprite_set_pokegra_members(int members);

/* How many forms the engine will draw for a species: 1 for almost all of them,
 * 28 for Unown, 18 for Arceus. Traps (returns -1) on a species with no picture,
 * so this is also the cheapest "can this be drawn at all" question. */
int mmo_sprite_form_count(int species, const char **why);

/* The picture, or a refusal. Returns 0 and fills `out` on success; returns -1
 * and sets `*why` otherwise. A form past what the engine draws for that species
 * is a refusal here and a silent clamp to form 0 in the engine, see sprite.c,
 * which explains why this client does not reproduce that. */
int mmo_sprite_locate(int species, int form, int gender, int shiny, int face,
                      mmo_sprite_ref *out, const char **why);

#endif /* OPENMMO_SPRITE_H */
