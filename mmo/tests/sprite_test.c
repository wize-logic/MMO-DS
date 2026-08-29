/* The sprite index, and the refusals that are the point of it. */
#include <stdio.h>
#include <string.h>

#include "sprite.h"
#include "sprite_index.gen.h"

static int failures;

static void ok(const char *what)
{
    printf("  ok   %s\n", what);
}

static void bad(const char *what)
{
    printf("  FAIL %s\n", what);
    failures++;
}

static void check(int cond, const char *what)
{
    if (cond)
        ok(what);
    else
        bad(what);
}

/* Rows the fused build printed, from the engine's own function. Bulbasaur's
 * front sprites are the default arm; Unown's are a pl_otherpoke case with a
 * per-form stride; the egg is the case with no face term at all. */
static void test_pinned_rows(void)
{
    struct {
        int species, form, gender, shiny, face;
        int archive, character, palette;
    } rows[] = {
        { 1, 0, 0, 0, MMO_SPRITE_FACE_FRONT_ID, MMO_SPRITE_ARCHIVE_POKEGRA, 9, 10 },
        { 1, 0, 1, 0, MMO_SPRITE_FACE_FRONT_ID, MMO_SPRITE_ARCHIVE_POKEGRA, 8, 10 },
        { 1, 0, 0, 1, MMO_SPRITE_FACE_BACK_ID, MMO_SPRITE_ARCHIVE_POKEGRA, 7, 11 },
        { 201, 1, 0, 0, MMO_SPRITE_FACE_FRONT_ID, MMO_SPRITE_ARCHIVE_OTHERPOKE, 11, 156 },
        { 493, 17, 0, 0, MMO_SPRITE_FACE_FRONT_ID, MMO_SPRITE_ARCHIVE_OTHERPOKE, 131, 224 },
        { 494, 1, 0, 0, MMO_SPRITE_FACE_FRONT_ID, MMO_SPRITE_ARCHIVE_OTHERPOKE, 133, 227 },
    };
    size_t i;
    int wrong = 0;

    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        mmo_sprite_ref ref;
        const char *why = "unset";

        if (mmo_sprite_locate(rows[i].species, rows[i].form, rows[i].gender,
                              rows[i].shiny, rows[i].face, &ref, &why) != 0
            || why != NULL
            || ref.archive != rows[i].archive
            || ref.character != rows[i].character
            || ref.palette != rows[i].palette) {
            printf("       species %d form %d: got archive %d %d/%d, want %d %d/%d\n",
                   rows[i].species, rows[i].form, ref.archive, ref.character,
                   ref.palette, rows[i].archive, rows[i].character, rows[i].palette);
            wrong++;
        }
    }
    check(wrong == 0, "the rows the engine was measured answering still answer the same");
}

static void test_spinda(void)
{
    mmo_sprite_ref front, back;

    if (mmo_sprite_locate(MMO_SPRITE_SPINDA, 0, 0, 0, MMO_SPRITE_FACE_FRONT_ID,
                          &front, NULL) != 0
        || mmo_sprite_locate(MMO_SPRITE_SPINDA, 0, 0, 0, MMO_SPRITE_FACE_BACK_ID,
                             &back, NULL) != 0) {
        bad("Spinda resolves at all");
        return;
    }
    check(front.spinda_spots && !back.spinda_spots,
          "only Spinda's front sprite asks for personality-driven spots");
}

/* The whole space, against the archives that hold it. This is the invariant the
 * engine does not have: every member this client asks for exists. */
static void test_every_member_exists(void)
{
    int species, form, gender, shiny, f;
    static const int faces[2] = { MMO_SPRITE_FACE_BACK_ID, MMO_SPRITE_FACE_FRONT_ID };
    int resolved = 0, past = 0;

    for (species = 0; species <= MMO_SPRITE_BAD_EGG_ID; species++) {
        int forms = mmo_sprite_form_count(species, NULL);

        for (form = 0; form < forms; form++)
            for (gender = 0; gender < 3; gender++)
                for (shiny = 0; shiny < 2; shiny++)
                    for (f = 0; f < 2; f++) {
                        mmo_sprite_ref ref;
                        int members;

                        if (mmo_sprite_locate(species, form, gender, shiny,
                                              faces[f], &ref, NULL) != 0) {
                            past++;
                            continue;
                        }
                        resolved++;
                        members = ref.archive == MMO_SPRITE_ARCHIVE_POKEGRA
                            ? MMO_SPRITE_POKEGRA_MEMBERS
                            : MMO_SPRITE_OTHERPOKE_MEMBERS;
                        if (ref.character >= members || ref.palette >= members)
                            past++;
                    }
    }
    check(past == 0 && resolved == 6732,
          "every drawable combination lands inside its archive (6732 of them)");
}

static void test_form_counts(void)
{
    const char *why = "unset";

    check(mmo_sprite_form_count(1, &why) == 1 && why == NULL,
          "a species with one picture has one form");
    check(mmo_sprite_form_count(201, NULL) == 28, "Unown has 28");
    check(mmo_sprite_form_count(493, NULL) == 18, "Arceus has 18");
    check(mmo_sprite_form_count(MMO_SPRITE_BAD_EGG_ID, NULL) == 1,
          "the bad egg has one");
    check(mmo_sprite_form_count(496, &why) == -1 && why != NULL,
          "a species past the last one traps rather than answering 1");
}

/* Each refusal, with its own reason. The species one is the one that matters, 
 * it is what a server naming post-Gen-4 content meets, and the form one is a
 * deliberate divergence from the engine, which clamps silently. */
static void test_refusals(void)
{
    mmo_sprite_ref ref;
    const char *species_why = NULL, *form_why = NULL, *face_why = NULL;
    const char *gender_why = NULL, *shiny_why = NULL, *out_why = NULL;

    check(mmo_sprite_locate(496, 0, 0, 0, MMO_SPRITE_FACE_FRONT_ID, &ref,
                            &species_why) != 0 && species_why != NULL,
          "the first species with no picture is refused, with a reason");
    check(mmo_sprite_locate(649, 0, 0, 0, MMO_SPRITE_FACE_FRONT_ID, &ref, NULL) != 0,
          "a Gen 5 species is refused rather than indexed past the archive");
    check(mmo_sprite_locate(-1, 0, 0, 0, MMO_SPRITE_FACE_FRONT_ID, &ref, NULL) != 0,
          "a negative species is refused");
    check(mmo_sprite_locate(1, 1, 0, 0, MMO_SPRITE_FACE_FRONT_ID, &ref,
                            &form_why) != 0 && form_why != NULL
              && form_why != species_why,
          "a form the ROM does not draw is refused, not clamped to form 0");
    check(mmo_sprite_locate(201, 28, 0, 0, MMO_SPRITE_FACE_FRONT_ID, &ref, NULL) != 0,
          "the form after Unown's last is refused");
    check(mmo_sprite_locate(1, 0, 0, 0, 1, &ref, &face_why) != 0 && face_why != NULL,
          "a face of 1, the boolean a caller would reach for, is refused");
    check(mmo_sprite_locate(1, 0, 3, 0, MMO_SPRITE_FACE_FRONT_ID, &ref,
                            &gender_why) != 0 && gender_why != NULL,
          "a gender id the engine does not have is refused");
    check(mmo_sprite_locate(1, 0, 0, 2, MMO_SPRITE_FACE_FRONT_ID, &ref,
                            &shiny_why) != 0 && shiny_why != NULL,
          "a shiny flag that is not a flag is refused");
    check(mmo_sprite_locate(1, 0, 0, 0, MMO_SPRITE_FACE_FRONT_ID, NULL,
                            &out_why) != 0 && out_why != NULL,
          "no output slot is refused rather than written through");
}

/* The refuse lifts only as far as the live archive. 2982 is what
 * mods/sprigatito grows pl_pokegra to (species 496, members 2976..2981,
 * hole 2964..2975 filled); a count that only fills the hole still
 * names no picture for 496. */
static void test_overlayed(void)
{
    mmo_sprite_ref ref;
    const char *why = "unset";

    check(mmo_sprite_pokegra_members() == MMO_SPRITE_POKEGRA_MEMBERS,
          "the live count starts at the cartridge's 2964");
    check(mmo_sprite_species_max() == MMO_SPRITE_SPECIES_MAX,
          "without an overlay the last species is still 493");

    mmo_sprite_set_pokegra_members(2970);
    check(mmo_sprite_locate(496, 0, 0, 0, MMO_SPRITE_FACE_FRONT_ID, &ref,
                            &why) != 0 && why != NULL,
          "filling the 494/495 hole does not make 496 drawable");

    mmo_sprite_set_pokegra_members(2982);
    check(mmo_sprite_pokegra_members() == 2982,
          "an overlay-grown count is the live ceiling");
    check(mmo_sprite_species_max() == 496,
          "species_max follows the last complete default-arm id");
    check(mmo_sprite_form_count(496, &why) == 1 && why == NULL,
          "overlayed 496 has one form, not a trap");
    if (mmo_sprite_locate(496, 0, 0, 0, MMO_SPRITE_FACE_FRONT_ID, &ref,
                          &why) != 0 || why != NULL
            || ref.archive != MMO_SPRITE_ARCHIVE_POKEGRA
            || ref.character != 2979 || ref.palette != 2980) {
        printf("       496 front male: got archive %d %d/%d, want pokegra 2979/2980\n",
               ref.archive, ref.character, ref.palette);
        bad("496 locates to the default-arm members the overlay serves");
    } else {
        ok("496 locates to the default-arm members the overlay serves");
    }
    check(mmo_sprite_locate(497, 0, 0, 0, MMO_SPRITE_FACE_FRONT_ID, &ref,
                            NULL) != 0,
          "497 still refuses at 2982 members (it needs 2988)");
    check(mmo_sprite_locate(649, 0, 0, 0, MMO_SPRITE_FACE_FRONT_ID, &ref,
                            NULL) != 0,
          "a Gen 5 species still refuses against an overlay that stops at 496");

    mmo_sprite_set_pokegra_members(0);
    check(mmo_sprite_pokegra_members() == MMO_SPRITE_POKEGRA_MEMBERS
              && mmo_sprite_locate(496, 0, 0, 0, MMO_SPRITE_FACE_FRONT_ID,
                                   &ref, NULL) != 0,
          "the live count will not shrink below the cartridge");
}

/* Male and genderless share a member and the female one is the member before
 * it: the engine's own comparison is `gender != GENDER_FEMALE`, so this is the
 * only thing gender does to a sprite. */
static void test_gender(void)
{
    mmo_sprite_ref male, none, female;

    if (mmo_sprite_locate(1, 0, MMO_SPRITE_GENDER_MALE_ID, 0,
                          MMO_SPRITE_FACE_FRONT_ID, &male, NULL) != 0
        || mmo_sprite_locate(1, 0, MMO_SPRITE_GENDER_NONE_ID, 0,
                             MMO_SPRITE_FACE_FRONT_ID, &none, NULL) != 0
        || mmo_sprite_locate(1, 0, MMO_SPRITE_GENDER_FEMALE_ID, 0,
                             MMO_SPRITE_FACE_FRONT_ID, &female, NULL) != 0) {
        bad("all three genders resolve");
        return;
    }
    check(male.character == none.character && female.character == male.character - 1,
          "male and genderless share a member; the female one is the member before");
}

int sprite_tests_run(void)
{
    failures = 0;
    printf("the sprite archives, and where they end:\n");
    test_pinned_rows();
    test_spinda();
    test_gender();
    test_form_counts();
    test_every_member_exists();
    test_refusals();
    test_overlayed();
    return failures;
}
