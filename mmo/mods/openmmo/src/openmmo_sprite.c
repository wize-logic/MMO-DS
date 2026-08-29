/* The engine's own answer to "which picture is that", dumped. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/narc.h"

#include "pokemon.h"
#include "pokemon_icon.h"
#include "pokemon_sprite.h"

#include "pc_modfs.h"

#include "../../../include/sprite.h"

/* Past SPECIES_BAD_EGG in every direction the client might be handed one: the
 * first id with no slot, a Gen 5 starter, the last Gen 5 species, and a number
 * no generation will reach. */
static const int PROBE_SPECIES[] = { 496, 500, 649, 1000 };

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
    }
}

/* Tell sprite.c the live pl_pokegra count. pc_modfs_boot has already
 * run (host main, before the guest reaches NitroMain). A species past
 * BAD_EGG locates only when that count holds its last member. */
void openmmo_sprite_bind_overlay(void)
{
    static int bound;
    unsigned count;
    int species;

    if (bound)
        return;
    bound = 1;

    count = pc_modfs_narc_file_count("poketool/pokegra/pl_pokegra.narc",
                                     (unsigned)MMO_SPRITE_POKEGRA_ROM_MEMBERS);
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
