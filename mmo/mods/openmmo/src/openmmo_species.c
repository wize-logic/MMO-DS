/* What the engine asks about a species this game did not ship with. */

#include <stdio.h>
#include <string.h>

#include "constants/narc.h"
#include "constants/species.h"

#include "message.h"
#include "message_util.h"
#include "narc.h"
#include "pc_modfs.h"
#include "pokemon.h"
#include "string_gf.h"
#include "struct_defs/species_sprite_data.h"

#include "../../../include/charcode.h"
#include "../../../include/species_port.h"

#define SPECIES_NARC "poketool/personal/pl_personal.narc"

static void bind_once(void)
{
    static int bound;
    unsigned count;

    if (bound)
        return;
    bound = 1;
    count = pc_modfs_narc_file_count(SPECIES_NARC,
        (unsigned)MMO_PORTED_SPECIES_ROM_MEMBERS);
    mmo_species_port_set_members((int)count);
    printf("openmmo: species overlay pl_personal=%u, species to %d\n",
        count, mmo_species_port_max());
}

/* Ask now rather than on the first species question. */
void openmmo_species_bind(void)
{
    bind_once();
}

/* Each of these returns 1 and writes its answer when a fill served the species,
 * and 0 otherwise, which is the engine carrying on into its own arithmetic and
 * its own clamp. None of them substitutes anything. */

int openmmo_species_member(int species, int *member)
{
    const char *why = NULL;

    bind_once();
    return mmo_species_port_member(species, member, &why) == 0;
}

/*
 * The member of the four species-indexed personal archives to read for one species: the fill's
 * appended one where a fill served it, and the species id itself everywhere else, which is the
 * engine's own arithmetic unchanged.
 */
int openmmo_species_member_index(int species)
{
    int member;

    if (openmmo_species_member(species, &member)) {
        return member;
    }

    return species;
}

int openmmo_species_icon_member(unsigned species, unsigned *out)
{
    int member, palette;
    const char *why = NULL;

    bind_once();
    if (mmo_species_port_icon((int)species, &member, &palette, &why) != 0)
        return 0;
    *out = (unsigned)member;
    return 1;
}

int openmmo_species_icon_palette(unsigned species, unsigned *out)
{
    int member, palette;
    const char *why = NULL;

    bind_once();
    if (mmo_species_port_icon((int)species, &member, &palette, &why) != 0)
        return 0;
    *out = (unsigned)palette;
    return 1;
}

/*
 * The per-species sprite row of pl_poke_data, cry delay, the Gen 4 entry animation and its
 * ten frames, the front's extra y offset, the shadow, is a table of 494 with no ceiling on
 * the read, so a ported species read whatever lay past it: nonsense frame indices that paged
 * through the texture as glitch, offsets that walked the sprite off the summary screen, delays
 * of seconds.
 */
int openmmo_species_sprite_data(unsigned species, void *out)
{
    SpeciesSpriteData *data = out;
    int face, k;

    bind_once();
    if (!mmo_species_port_is_ported((int)species))
        return 0;
    memset(data, 0, sizeof *data);
    for (face = 0; face < MAX_FACES; face++) {
        for (k = 0; k < MAX_ANIMATION_FRAMES; k++) {
            data->faceAnims[face].frames[k].spriteFrame = -1;
        }
    }
    /*
     * The appear animation. A fabricated row cannot say "none", InitAnim clamps every number
     * to pattern 0, which 36 real species wear and a player read as a borrowed identity.
     */
    data->faceAnims[0].animation = 2;
    data->faceAnims[1].animation = 1;
    data->shadowSize = 2;
    return 1;
}

/* The engine's own read of that row, with the fabricated one in front of it.
 * A ported species never reaches the archive; every other species reads the
 * member it always read. */
void openmmo_species_sprite_read(NARC *narc, u16 species, void *out)
{
    if (openmmo_species_sprite_data(species, out)) {
        return;
    }

    NARC_ReadFromMember(narc, 0, species * sizeof(SpeciesSpriteData), sizeof(SpeciesSpriteData), out);
}

int openmmo_species_name_entry(unsigned species, unsigned *out)
{
    int entry;
    const char *why = NULL;

    bind_once();
    if (mmo_species_port_name((int)species, &entry, &why) != 0)
        return 0;
    *out = (unsigned)entry;
    return 1;
}

/*
 * The message loader's one door (src/message.c, all three getters): on the species-name bank
 * of pl_msg the entry of a ported species is where the fill put its name, after the two egg
 * names; the bank with articles ("a", "an") beside it is grown the same way by the same fill.
 */
u32 openmmo_message_entry(u32 narcID, u32 bankID, u32 entryID)
{
    unsigned ported;

    if (narcID != NARC_INDEX_MSGDATA__PL_MSG
        || (bankID != TEXT_BANK_SPECIES_NAME && bankID != TEXT_BANK_SPECIES_NAME_WITH_ARTICLES))
        return entryID;
    if (openmmo_species_name_entry((unsigned)entryID, &ported))
        return (u32)ported;
    return entryID;
}


/* The Sinnoh dex number of a species Sinnoh does not have is zero, and the
 * table that would otherwise be indexed for it is one member long. This is a
 * refusal wearing an answer's clothes and it is the right one: the engine's own
 * callers read 0 as "not in this dex". */
int openmmo_species_no_sinnoh_dex(unsigned species)
{
    bind_once();
    return mmo_species_port_is_ported((int)species);
}


/*
 * What the engine actually reads for a ported species, asked through its own getters rather
 * than through the tables above.
 */
void openmmo_species_dump_ported(int species)
{
    charcode_t name[32];
    char utf8[64], utf8_util[64];
    int member;
    const char *why = NULL;

    bind_once();
    if (mmo_species_port_member(species, &member, &why) != 0)
        return;

    memset(name, 0, sizeof name);
    MessageLoader_GetSpeciesName((u32)species, HEAP_ID_SYSTEM, name);
    mmo_charcode_to_utf8((const mmo_charcode *)name, utf8, sizeof utf8);
    /* The other door to the same bank, the one a new monster's default name
     * comes through (MON_DATA_SPECIES_NAME): it read the raw id until
     * 2026-08-31 and named every ported monster two species early. */
    {
        String *other = MessageUtil_SpeciesName((u32)species, HEAP_ID_SYSTEM);

        memset(name, 0, sizeof name);
        if (other != NULL) {
            String_ToChars(other, name, 31);
            String_Free(other);
        }
        mmo_charcode_to_utf8((const mmo_charcode *)name, utf8_util, sizeof utf8_util);
    }

    printf("ported %d type %u %u stats %u %u %u %u %u %u name %s util %s\n", species,
        SpeciesData_GetSpeciesValue(species, SPECIES_DATA_TYPE_1),
        SpeciesData_GetSpeciesValue(species, SPECIES_DATA_TYPE_2),
        SpeciesData_GetSpeciesValue(species, SPECIES_DATA_BASE_HP),
        SpeciesData_GetSpeciesValue(species, SPECIES_DATA_BASE_ATK),
        SpeciesData_GetSpeciesValue(species, SPECIES_DATA_BASE_DEF),
        SpeciesData_GetSpeciesValue(species, SPECIES_DATA_BASE_SPEED),
        SpeciesData_GetSpeciesValue(species, SPECIES_DATA_BASE_SP_ATK),
        SpeciesData_GetSpeciesValue(species, SPECIES_DATA_BASE_SP_DEF),
        utf8, utf8_util);
}
