/* The sprite-archive index, bounded, and the refusal where it ends. */
#include "sprite.h"

#include "sprite_index.gen.h"

/* The public constants a caller compiles against and the generated ones the
 * engine actually uses are two spellings of one number. Keeping them apart lets
 * a caller name a face without pulling in the whole table; keeping them equal is
 * this. */
typedef char sprite_face_ids_agree[
    (MMO_SPRITE_FACE_BACK_ID == MMO_SPRITE_FACE_BACK
     && MMO_SPRITE_FACE_FRONT_ID == MMO_SPRITE_FACE_FRONT) ? 1 : -1];
typedef char sprite_gender_ids_agree[
    (MMO_SPRITE_GENDER_FEMALE_ID == MMO_SPRITE_GENDER_FEMALE) ? 1 : -1];
typedef char sprite_egg_ids_agree[
    (MMO_SPRITE_EGG_ID == MMO_SPRITE_EGG
     && MMO_SPRITE_BAD_EGG_ID == MMO_SPRITE_BAD_EGG) ? 1 : -1];
typedef char sprite_pokegra_rom_agree[
    (MMO_SPRITE_POKEGRA_ROM_MEMBERS == MMO_SPRITE_POKEGRA_MEMBERS) ? 1 : -1];
typedef char sprite_pokegra_stride_agree[
    (MMO_SPRITE_POKEGRA_PER_SPECIES == MMO_SPRITE_POKEGRA_STRIDE) ? 1 : -1];
typedef char sprite_height_rom_agree[
    (MMO_SPRITE_HEIGHT_ROM_MEMBERS == MMO_SPRITE_POKEGRA_ROM_MEMBERS
     / MMO_SPRITE_POKEGRA_STRIDE * MMO_SPRITE_HEIGHT_PER_SPECIES) ? 1 : -1];

static int g_pokegra_members = MMO_SPRITE_POKEGRA_MEMBERS;

static const char *const NO_SUCH_SPECIES =
    "sprite: species id past the 494 this ROM draws, pl_pokegra holds six "
    "members each for species 0..493 and the two egg slots after them; an "
    "overlay can grow that archive, and a species the overlay does not "
    "actually serve still has no picture here";

static const char *const NO_SUCH_FORM =
    "sprite: form past the ones this ROM draws for that species, the engine "
    "clamps this to form 0 and says nothing, which for a form the SERVER chose "
    "means drawing the wrong Pokemon silently";

static const char *const BAD_FACE =
    "sprite: face is neither MMO_SPRITE_FACE_BACK_ID nor _FRONT_ID, the engine "
    "numbers them 0 and 2 and uses the value itself in the arithmetic, so a "
    "boolean passed here indexes the wrong member";

static const char *const BAD_GENDER =
    "sprite: gender is not one of the engine's three ids";

static const char *const BAD_SHINY =
    "sprite: shiny is neither 0 nor 1";

static const char *const NO_OUT =
    "sprite: no output slot given";

static const char *const PAST_ARCHIVE =
    "sprite: the computed member is past the end of the archive it belongs to, "
    "the generated table and the ROM's own archives disagree, which means the "
    "engine moved under a committed sprite_index.gen.h";

static int trap(const char **why, const char *msg)
{
    if (why)
        *why = msg;
    return -1;
}

static void ok(const char **why)
{
    if (why)
        *why = 0;
}

static int drawable(int species)
{
    if (species < 0)
        return 0;
    if (species <= MMO_SPRITE_BAD_EGG)
        return 1;
    /* Default-arm overlay: the live archive must hold this species' last
     * member (palette shiny = species*6+5). A hole is a cook error on the
     * engine side; here we only ask whether the count grew far enough. */
    return g_pokegra_members / MMO_SPRITE_POKEGRA_STRIDE > species;
}

/* The engine's own clamp table, from Pokemon_SanitizeFormId. A species that is
 * not in it has exactly one form. */
static int form_count(int species)
{
    int i;

    for (i = 0; i < MMO_SPRITE_FORMS_COUNT; i++) {
        if (MMO_SPRITE_FORMS[i].species == species)
            return MMO_SPRITE_FORMS[i].forms;
    }
    return 1;
}

static int otherpoke_case(int species)
{
    int i;

    for (i = 0; i < MMO_SPRITE_OTHERPOKE_COUNT; i++) {
        if (MMO_SPRITE_OTHERPOKE[i].species == species)
            return i;
    }
    return -1;
}

int mmo_sprite_pokegra_members(void)
{
    return g_pokegra_members;
}

void mmo_sprite_set_pokegra_members(int members)
{
    if (members < MMO_SPRITE_POKEGRA_MEMBERS)
        members = MMO_SPRITE_POKEGRA_MEMBERS;
    g_pokegra_members = members;
}

int mmo_sprite_species_max(void)
{
    int last = g_pokegra_members / MMO_SPRITE_POKEGRA_STRIDE - 1;

    if (last < MMO_SPRITE_SPECIES_MAX)
        return MMO_SPRITE_SPECIES_MAX;
    return last;
}

int mmo_sprite_form_count(int species, const char **why)
{
    if (!drawable(species))
        return trap(why, NO_SUCH_SPECIES);
    ok(why);
    return form_count(species);
}

int mmo_sprite_locate(int species, int form, int gender, int shiny, int face,
                      mmo_sprite_ref *out, const char **why)
{
    int members, i;

    if (out == 0)
        return trap(why, NO_OUT);
    if (!drawable(species))
        return trap(why, NO_SUCH_SPECIES);
    if (form < 0 || form >= form_count(species))
        return trap(why, NO_SUCH_FORM);
    if (face != MMO_SPRITE_FACE_BACK && face != MMO_SPRITE_FACE_FRONT)
        return trap(why, BAD_FACE);
    if (gender < MMO_SPRITE_GENDER_MALE_ID || gender > MMO_SPRITE_GENDER_NONE_ID)
        return trap(why, BAD_GENDER);
    if (shiny != 0 && shiny != 1)
        return trap(why, BAD_SHINY);

    out->spinda_spots = 0;
    i = otherpoke_case(species);
    if (i >= 0) {
        out->archive = MMO_SPRITE_ARCHIVE_OTHERPOKE;
        out->character = MMO_SPRITE_OTHERPOKE[i].char_base
            + MMO_SPRITE_OTHERPOKE[i].char_face * (face / MMO_SPRITE_OTHERPOKE[i].char_face_div)
            + MMO_SPRITE_OTHERPOKE[i].char_form * form;
        out->palette = MMO_SPRITE_OTHERPOKE[i].pal_base
            + MMO_SPRITE_OTHERPOKE[i].pal_shiny * shiny
            + MMO_SPRITE_OTHERPOKE[i].pal_form * form;
        members = MMO_SPRITE_OTHERPOKE_MEMBERS;
    } else {
        /* BuildPokemonSpriteTemplate's default arm, transcribed. The gender
         * term is the engine's own: male and genderless share the member after
         * the female one, and every species has both whether or not the two
         * pictures differ. */
        out->archive = MMO_SPRITE_ARCHIVE_POKEGRA;
        out->character = species * MMO_SPRITE_POKEGRA_STRIDE + face
            + (gender != MMO_SPRITE_GENDER_FEMALE ? 1 : 0);
        out->palette = species * MMO_SPRITE_POKEGRA_STRIDE + 4 + shiny;
        if (species == MMO_SPRITE_SPINDA && face == MMO_SPRITE_FACE_FRONT)
            out->spinda_spots = 1;
        members = g_pokegra_members;
    }

    if (out->character < 0 || out->character >= members
        || out->palette < 0 || out->palette >= members)
        return trap(why, PAST_ARCHIVE);

    ok(why);
    return 0;
}
