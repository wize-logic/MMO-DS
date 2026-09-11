/* The engine's own answer to "which picture is that", dumped. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/narc.h"
#include "constants/species.h"

#include "pokemon.h"
#include "pokemon_icon.h"
#include "pokemon_sprite.h"

#include "pc_modfs.h"

#include "openmmo_spriteframe.h"

#include <nnsys.h>

#include "../../../include/sprite.h"

/* Past SPECIES_BAD_EGG in every direction the client might be handed one: the
 * first id with no slot, a Gen 5 starter, the last Gen 5 species, and a number
 * no generation will reach. */
static const int PROBE_SPECIES[] = { 496, 500, 649, 1000 };
/* And three moves a fill appends: the first, Scald, and the last. */
static const int PROBE_MOVES[] = { 468, 503, 559 };

/* Unown has the most (28), and no species has more. */
#define MAX_FORM 32

static const char *archive_name(int narc_id)
{
    if (narc_id == NARC_INDEX_POKETOOL__POKEGRA__PL_POKEGRA)
        return "pokegra";
    if (narc_id == NARC_INDEX_POKETOOL__POKEGRA__PL_OTHERPOKE)
        return "otherpoke";
    return "other";
}

static void sprite_at(PokemonSpriteTemplate *t, int species, int form, int gender,
    int shiny, int face)
{
    memset(t, 0, sizeof *t);
    BuildPokemonSpriteTemplate(t, (u16)species, (u8)gender, (u8)face, (u8)shiny,
        (u8)form, 0);
}

static void dump_species(int species)
{
    static const int faces[2] = { FACE_BACK, FACE_FRONT };
    PokemonSpriteTemplate base;
    int form, gender, shiny, f;

    sprite_at(&base, species, 0, 0, 0, FACE_FRONT);

    for (form = 0; form < MAX_FORM; form++) {
        if (form != 0) {
            PokemonSpriteTemplate t;

            sprite_at(&t, species, form, 0, 0, FACE_FRONT);
            if (t.character == base.character && t.palette == base.palette)
                continue;
        }

        for (gender = 0; gender < 3; gender++) {
            for (shiny = 0; shiny < 2; shiny++) {
                for (f = 0; f < 2; f++) {
                    PokemonSpriteTemplate t;

                    sprite_at(&t, species, form, gender, shiny, faces[f]);
                    printf("sprite %d %d %d %d %d %s %d %d %d\n",
                        species, form, gender, shiny, faces[f],
                        archive_name(t.narcID), t.character, t.palette,
                        t.spindaSpots != 0);
                }
            }
        }

        printf("icon %d %d %u %u\n", species, form,
            PokeIconSpriteIndex((u32)species, 0, (u32)form),
            PokeIconPaletteIndex((u32)species, (u32)form, 0));
    }
}

/* What the engine answers for a species it has no data for at all. Nothing here
 * reads an archive: the arithmetic is what is being measured, and the read that
 * would follow it is exactly what the client exists to prevent. */
static void dump_probes(void)
{
    size_t i;

    for (i = 0; i < sizeof PROBE_SPECIES / sizeof PROBE_SPECIES[0]; i++) {
        int species = PROBE_SPECIES[i];
        PokemonSpriteTemplate t;

        memset(&t, 0, sizeof t);
        BuildPokemonSpriteTemplate(&t, (u16)species, 0, FACE_FRONT, 0, 0, 0);
        printf("probe %d sprite %s %d %d icon %u %u\n", species,
            archive_name(t.narcID), t.character, t.palette,
            PokeIconSpriteIndex((u32)species, 0, 0),
            PokeIconPaletteIndex((u32)species, 0, 0));
        /* The icon is one of five archives a ported species needs and the only
         * one this probe could see. The other two a player would notice, the
         * personal entry and the name, print beside it, and say nothing at
         * all when no fill served the species. */
        {
            extern void openmmo_species_dump_ported(int species);
            openmmo_species_dump_ported(species);
        }
    }
    for (i = 0; i < sizeof PROBE_MOVES / sizeof PROBE_MOVES[0]; i++) {
        extern void openmmo_moves_dump_ported(int move);
        openmmo_moves_dump_ported(PROBE_MOVES[i]);
    }
}

/* --------------------------------------------------------------------------- Black's loops. */

#define STRIP_PAIR_BYTES 6400
#define STRIP_ROW_BYTES  80
#define STRIP_HALF_BYTES 40
#define STRIP_HEADER     0x30       /* RGCN header, RAHC header, data offset */
#define STRIP_MAX_FRAMES 16
#define STRIP_MAGIC      "MMOA"
#define LCG_A            1103515245u
#define LCG_C            24691u

struct strip_state {
    int character;                  /* the member this belongs to */
    int frames;                     /* 0 when the slot holds a plain sheet */
    int frame;                      /* the one laid into the first pair */
    int ticks;                      /* left on it */
    u8 hold[STRIP_MAX_FRAMES];
};
static struct strip_state s_strip[MAX_MON_SPRITES];

/* PokemonSprite_DecryptPt over any length: front to back, seeded from the
 * first word, the LCG the engine's own tool uses. */
static void strip_decrypt(u8 *data, u32 bytes)
{
    u16 *w = (u16 *)data;
    u32 seed = w[0], i;

    for (i = 0; i < bytes / 2; i++) {
        w[i] ^= (u16)seed;
        seed = seed * LCG_A + LCG_C;
    }
}

/* Called where the sprite manager would decrypt a freshly read sheet. Answers 1
 * having decrypted a strip and laid its current frame into both halves of the
 * first pair; 0 for a plain sheet, which the engine then decrypts itself. */
extern int openmmo_contest_scene_up(void);   /* openmmo_contest.c */

int openmmo_sprite_strip_prepare(int slot, int character, void *charDataV, u8 *raw, void *man)
{
    NNSG2dCharacterData *cd = charDataV;
    struct strip_state *st;
    const u8 *file, *tail;
    u32 size, count, k, y;
    static u8 tmp[STRIP_HALF_BYTES * 80];
    extern int openmmo_blackanim_lay(int slot, int character, u8 *raw, u32 bytes, int fw, int fh);
    const struct openmmo_spriteframe_layout *L = openmmo_spriteframe_of(man);

    if (slot < 0 || slot >= MAX_MON_SPRITES || cd == NULL || raw == NULL)
        return 0;
    st = &s_strip[slot];
    /* A contest draws the cartridge's own sheets (openmmo_contest.c): the bytes
     * in `raw` are the ROM's, and the compositor, which never looks at them
     * and would lay Black's loop regardless, has to stay out. */
    if (openmmo_contest_scene_up()) {
        openmmo_spriteframe_composed_set(slot, NULL, 0, 0);
        st->frames = 0;
        return 0;
    }
    /* The loop composed live out of the cartridge's own cells beats a baked
     * sample of it; the strip is what plays when the fill has no cells. */
    /* It composes into the frame of the layout this manager draws through
     * (openmmo_spriteframe.c) and lays the 80x80 picture the OBJ mirror and the
     * cartridge's own layout want into the pair. */
    if (openmmo_blackanim_lay(slot, character, raw, cd->szByte, L->frameW, L->frameH)) {
        st->frames = 0;
        return 1;
    }
    openmmo_spriteframe_composed_set(slot, NULL, 0, 0);
    file = raw - STRIP_HEADER;
    size = file[8] | file[9] << 8 | file[10] << 16 | (u32)file[11] << 24;
    if (cd->szByte < STRIP_PAIR_BYTES || cd->szByte % STRIP_PAIR_BYTES
        || size < STRIP_HEADER + cd->szByte + 8) {
        st->frames = 0;
        return 0;
    }
    tail = raw + cd->szByte;
    count = tail[4] | tail[5] << 8;
    if (memcmp(tail, STRIP_MAGIC, 4) != 0 || count < 2 || count > STRIP_MAX_FRAMES
        || (count + 1) / 2 > cd->szByte / STRIP_PAIR_BYTES) {
        st->frames = 0;
        return 0;
    }
    strip_decrypt(raw, cd->szByte);
    if (st->character != character || st->frames != (int)count) {
        st->character = character;
        st->frames = (int)count;
        st->frame = 0;
        memcpy(st->hold, tail + 6, count);
        st->ticks = st->hold[0] ? st->hold[0] : 1;
    }
    k = (u32)st->frame;
    for (y = 0; y < 80; y++) {
        memcpy(tmp + y * STRIP_HALF_BYTES,
            raw + ((k / 2) * 80 + y) * STRIP_ROW_BYTES + (k % 2) * STRIP_HALF_BYTES,
            STRIP_HALF_BYTES);
    }
    for (y = 0; y < 80; y++) {
        memcpy(raw + y * STRIP_ROW_BYTES, tmp + y * STRIP_HALF_BYTES, STRIP_HALF_BYTES);
        memcpy(raw + y * STRIP_ROW_BYTES + STRIP_HALF_BYTES, tmp + y * STRIP_HALF_BYTES,
            STRIP_HALF_BYTES);
    }
    return 1;
}

/* Once a frame, per drawn sprite. Answers 1 when the strip has moved on to its
 * next frame and the page wants buffering again, 2 when it settled for a
 * partial draw and the page must be rebuilt before this draw. */
int openmmo_sprite_strip_tick(int slot, int partial)
{
    struct strip_state *st;
    int step;
    extern int openmmo_blackanim_tick(int slot, int partial);

    if (slot < 0 || slot >= MAX_MON_SPRITES)
        return 0;
    step = openmmo_blackanim_tick(slot, partial);
    if (step)
        return step;
    st = &s_strip[slot];
    if (st->frames < 2)
        return 0;
    if (partial) {
        /* the baked strip settles on its first frame for a partial draw */
        if (st->frame != 0) {
            st->frame = 0;
            st->ticks = st->hold[0] ? st->hold[0] : 1;
            return 2;
        }
        return 0;
    }
    if (--st->ticks > 0)
        return 0;
    st->frame = (st->frame + 1) % st->frames;
    st->ticks = st->hold[st->frame] ? st->hold[st->frame] : 1;
    return 1;
}

/* Tell sprite.c the live pl_pokegra count. pc_modfs_boot has already
 * run (host main, before the guest reaches NitroMain). A species past
 * BAD_EGG locates only when that count holds its last member. */
void openmmo_sprite_bind_overlay(void)
{
    static int bound;
    unsigned count, heights;
    int species;

    if (bound)
        return;
    bound = 1;

    count = pc_modfs_narc_file_count("poketool/pokegra/pl_pokegra.narc",
                                     (unsigned)MMO_SPRITE_POKEGRA_ROM_MEMBERS);
    /* A fill appends the sheets and the bytes that seat them in step. A
     * package that grew one without the other would draw a ported species
     * with its feet at the top of its slot, so it grows neither here. */
    heights = pc_modfs_narc_file_count("poketool/pokegra/height.narc",
                                       (unsigned)MMO_SPRITE_HEIGHT_ROM_MEMBERS);
    if (heights < count / MMO_SPRITE_POKEGRA_PER_SPECIES * MMO_SPRITE_HEIGHT_PER_SPECIES) {
        printf("openmmo: sprite overlay refused: pl_pokegra=%u but height.narc=%u\n",
            count, heights);
        count = MMO_SPRITE_POKEGRA_ROM_MEMBERS;
    }
    mmo_sprite_set_pokegra_members((int)count);
    printf("openmmo: sprite overlay pl_pokegra=%u\n", count);

    for (species = MMO_SPRITE_BAD_EGG_ID + 1;
         mmo_sprite_form_count(species, NULL) > 0;
         species++) {
        mmo_sprite_ref ref;
        const char *why = NULL;

        if (mmo_sprite_locate(species, 0, MMO_SPRITE_GENDER_MALE_ID, 0,
                              MMO_SPRITE_FACE_FRONT_ID, &ref, &why) != 0)
            break;
        printf("openmmo: sprite species %d %s %d %d\n", species,
            ref.archive == MMO_SPRITE_ARCHIVE_OTHERPOKE ? "otherpoke" : "pokegra",
            ref.character, ref.palette);
    }
}

/* Whether BuildPokemonSpriteTemplate would compute a member the archive does not hold. */
int openmmo_sprite_undrawable(unsigned species)
{
    openmmo_sprite_bind_overlay();
    return mmo_sprite_form_count((int)species, NULL) < 0;
}

/*
 * The same question as an answer, for the two sprite-template builders. Species 494 computes
 * member 2964 of an archive with 2964, and the read after it is guarded by one assertion this
 * port used to discard.
 */
u16 openmmo_sprite_drawable_species(u16 species)
{
    if (openmmo_sprite_undrawable(species)) {
        return SPECIES_NONE;
    }

    return species;
}

void openmmo_sprite_dump_once(void)
{
    static int done;
    const char *mode = getenv("OPENMMO_SPRITE_DUMP");
    int species;

    openmmo_sprite_bind_overlay();

    if (done || mode == NULL || mode[0] == '\0' || mode[0] == '0')
        return;
    done = 1;

    printf("sprite-dump: begin\n");
    if (strcmp(mode, "probe") != 0) {
        for (species = 0; species <= MAX_SPECIES; species++)
            dump_species(species);
    }
    dump_probes();
    printf("sprite-dump: end\n");
    fflush(stdout);
}
