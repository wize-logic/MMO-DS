/* The client's type tables against a second program's copy. */
#include <stdio.h>
#include <string.h>

#include "display.h"
#include "display_types.gen.h"

static int failures;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) {                                                             \
            printf("  ok   %s\n", msg);                                         \
        } else {                                                                \
            printf("  FAIL %s\n", msg);                                         \
            failures++;                                                         \
        }                                                                       \
    } while (0)

static const u8 OFFICIAL_MATCHUP[MMO_TYPE_COUNT][MMO_TYPE_COUNT] = {
    { 10, 10, 10, 10, 10,  5, 10,  0,  5, 10, 10, 10, 10, 10, 10, 10, 10, 10 }, /* NORMAL   */
    { 20, 10,  5,  5, 10, 20,  5,  0, 20, 10, 10, 10, 10, 10,  5, 20, 10, 20 }, /* FIGHTING */
    { 10, 20, 10, 10, 10,  5, 20, 10,  5, 10, 10, 10, 20,  5, 10, 10, 10, 10 }, /* FLYING   */
    { 10, 10, 10,  5,  5,  5, 10,  5,  0, 10, 10, 10, 20, 10, 10, 10, 10, 10 }, /* POISON   */
    { 10, 10,  0, 20, 10, 20,  5, 10, 20, 10, 20, 10,  5, 20, 10, 10, 10, 10 }, /* GROUND   */
    { 10,  5, 20, 10,  5, 10, 20, 10,  5, 10, 20, 10, 10, 10, 10, 20, 10, 10 }, /* ROCK     */
    { 10,  5,  5,  5, 10, 10, 10,  5,  5, 10,  5, 10, 20, 10, 20, 10, 10, 20 }, /* BUG      */
    {  0, 10, 10, 10, 10, 10, 10, 20,  5, 10, 10, 10, 10, 10, 20, 10, 10,  5 }, /* GHOST    */
    { 10, 10, 10, 10, 10, 20, 10, 10,  5, 10,  5,  5, 10,  5, 10, 20, 10, 10 }, /* STEEL    */
    { 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10 }, /* ???      */
    { 10, 10, 10, 10, 10,  5, 20, 10, 20, 10,  5,  5, 20, 10, 10, 20,  5, 10 }, /* FIRE     */
    { 10, 10, 10, 10, 20, 20, 10, 10, 10, 10, 20,  5,  5, 10, 10, 10,  5, 10 }, /* WATER    */
    { 10, 10,  5,  5, 20, 20,  5, 10,  5, 10,  5, 20,  5, 10, 10, 10,  5, 10 }, /* GRASS    */
    { 10, 10, 20, 10,  0, 10, 10, 10, 10, 10, 10, 20,  5,  5, 10, 10,  5, 10 }, /* ELECTRIC */
    { 10, 20, 10, 20, 10, 10, 10, 10,  5, 10, 10, 10, 10, 10,  5, 10, 10,  0 }, /* PSYCHIC  */
    { 10, 10, 20, 10, 20, 10, 10, 10,  5, 10,  5,  5, 20, 10, 10,  5, 20, 10 }, /* ICE      */
    { 10, 10, 10, 10, 10, 10, 10, 10,  5, 10, 10, 10, 10, 10, 10, 10, 20, 10 }, /* DRAGON   */
    { 10,  5, 10, 10, 10, 10, 10, 20,  5, 10, 10, 10, 10, 10, 20, 10, 10,  5 }, /* DARK     */
};

/* The same client's compact id per engine type id. Its eighteenth slot, 17, is
 * NONE, "this field holds no type", and not the type Gen 6 numbers 17. */
static const int OFFICIAL_WIRE[MMO_TYPE_COUNT] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, -1, 9, 10, 11, 12, 13, 14, 15, 16
};

static void test_display_agrees(void)
{
    int att, def, cells = 0, differing = 0;

    for (att = 0; att < MMO_TYPE_COUNT; att++) {
        for (def = 0; def < MMO_TYPE_COUNT; def++) {
            const char *why = "unset";
            int got = mmo_display_type_matchup(att, def, &why);
            cells++;
            if (got != OFFICIAL_MATCHUP[att][def] || why != 0)
                differing++;
        }
    }
    CHECK(cells == 324 && differing == 0,
          "all 324 cells match the official client's own chart");

    /* A chart that came out all-neutral would pass the loop above if the
     * fixture were also all-neutral, so pin the shape too. */
    for (att = 0, cells = 0; att < MMO_TYPE_COUNT; att++)
        for (def = 0; def < MMO_TYPE_COUNT; def++)
            if (MMO_TYPE_MATCHUP[att][def] != MMO_TYPE_NEUTRAL)
                cells++;
    CHECK(cells == 110, "110 of the 324 cells are not neutral");
}

static void test_mystery_row_is_inert(void)
{
    int i, neutral = 1;

    for (i = 0; i < MMO_TYPE_COUNT; i++) {
        if (MMO_TYPE_MATCHUP[MMO_TYPE_MYSTERY][i] != MMO_TYPE_NEUTRAL)
            neutral = 0;
        if (MMO_TYPE_MATCHUP[i][MMO_TYPE_MYSTERY] != MMO_TYPE_NEUTRAL)
            neutral = 0;
    }
    CHECK(neutral, "the ??? slot is neutral in both directions");
    CHECK(strcmp(mmo_display_type_name(MMO_TYPE_MYSTERY, 0), "???") == 0,
          "the ??? slot keeps the engine's own spelling");
}

static void test_names(void)
{
    const char *why = "unset";

    CHECK(mmo_display_type_count() == MMO_TYPE_COUNT, "eighteen type slots");
    CHECK(strcmp(mmo_display_type_name(0, &why), "NORMAL") == 0 && why == 0,
          "type 0 is NORMAL");
    CHECK(strcmp(mmo_display_type_name(17, 0), "DARK") == 0, "type 17 is DARK");
    CHECK(strcmp(mmo_display_type_name(8, 0), "STEEL") == 0, "type 8 is STEEL");
}

/* The point of the module: an id past the table refuses rather than guessing,
 * and the refusal says why the eighteenth type is missing. */
static void test_eighteenth_type_traps(void)
{
    const char *why = 0;

    CHECK(mmo_display_type_name(MMO_TYPE_COUNT, &why) == 0 && why != 0
              && strstr(why, "Fairy") != 0,
          "a name for the 18th type traps, naming what is missing");

    why = 0;
    CHECK(mmo_display_type_matchup(MMO_TYPE_COUNT, 0, &why) == MMO_TYPE_INVALID
              && why != 0,
          "a matchup against the 18th type traps");

    why = 0;
    CHECK(mmo_display_type_matchup(0, -1, &why) == MMO_TYPE_INVALID && why != 0,
          "a negative type traps");

    why = 0;
    CHECK(mmo_display_type_matchup_dual(0, 0, 99, &why) == MMO_TYPE_INVALID
              && why != 0,
          "an unknown second type traps rather than being ignored");
}

static void test_wire_numbering(void)
{
    int type, round = 1;
    const char *why = "unset";

    for (type = 0; type < MMO_TYPE_COUNT; type++) {
        int wire = mmo_display_type_to_wire(type, 0);
        if (wire != OFFICIAL_WIRE[type])
            round = 0;
        if (mmo_display_type_from_wire(wire, 0) != type)
            round = 0;
    }
    CHECK(round, "every type's compact id is the official client's, and converts back");

    CHECK(mmo_display_type_from_wire(MMO_TYPE_WIRE_NONE, &why) == MMO_TYPE_NO_TYPE
              && why == 0,
          "the compact id 17 is 'no type', not a trap and not a type");

    why = 0;
    CHECK(mmo_display_type_from_wire(18, &why) == MMO_TYPE_INVALID && why != 0,
          "a compact id past the official client's traps");

    CHECK(mmo_display_type_to_wire(MMO_TYPE_NO_TYPE, 0) == MMO_TYPE_WIRE_NONE,
          "'no type' converts back to the compact id 17");
}

static void test_dual_and_buckets(void)
{
    const char *why = "unset";

    /* Ground on a Fire/Rock defender: 2x and 2x. */
    CHECK(mmo_display_type_matchup_dual(4, 10, 5, &why) == 40 && why == 0,
          "a doubly weak defender is 4x");
    /* Electric on Water/Ground: 2x and immune. */
    CHECK(mmo_display_type_matchup_dual(13, 11, 4, 0) == 0,
          "an immunity on either half wins");
    /* Normal on Ghost with one type, said both ways. */
    CHECK(mmo_display_type_matchup_dual(0, 7, MMO_TYPE_NO_TYPE, 0) == 0
              && mmo_display_type_matchup_dual(0, 7, 7, 0) == 0,
          "a mono-typed defender reads the same repeated or absent");
    /* Fighting on Ghost/Steel: immune and 2x, so still immune. */
    CHECK(mmo_display_type_matchup_dual(1, 7, 8, 0) == 0,
          "immunity survives a doubling on the other half");

    CHECK(mmo_display_effect_of(0) == MMO_EFFECT_IMMUNE
              && mmo_display_effect_of(5) == MMO_EFFECT_RESISTED
              && mmo_display_effect_of(10) == MMO_EFFECT_NEUTRAL
              && mmo_display_effect_of(20) == MMO_EFFECT_SUPER
              && mmo_display_effect_of(40) == MMO_EFFECT_SUPER
              && mmo_display_effect_of(2) == MMO_EFFECT_RESISTED,
          "the four buckets a screen tints by");
}

static void test_foresight_pair(void)
{
    CHECK(MMO_TYPE_FORESIGHT_IMMUNITY_COUNT == 2,
          "the engine lists two Foresight-removable immunities");
    CHECK(mmo_display_type_foresight_clears(0, 7)
              && mmo_display_type_foresight_clears(1, 7),
          "they are Normal and Fighting against Ghost");
    CHECK(!mmo_display_type_foresight_clears(3, 8),
          "Poison against Steel is an ordinary immunity");
    CHECK(mmo_display_type_matchup(0, 7, 0) == 0
              && mmo_display_type_matchup(1, 7, 0) == 0,
          "both are in the grid as well as in the list");
}

static const char OVERLAY_STURDY_DESC[] = "Can't be knocked out in 1 hit.";

static void test_ability_overlay(void)
{
    const char *why = "unset";

    CHECK(strcmp(mmo_display_ability_name(5, &why), "Sturdy") == 0 && why == 0,
          "ability 5 is Sturdy, from the engine bank");
    CHECK(strcmp(mmo_display_ability_desc(5, 0), OVERLAY_STURDY_DESC) == 0,
          "Sturdy's description is the official client overlay, not the ROM paragraph");
    CHECK(strcmp(mmo_display_ability_name(547, 0), "Protean") == 0,
          "ability 547 is Protean, overlay-only");

    why = 0;
    CHECK(mmo_display_ability_name(200, &why) == 0 && why != 0,
          "an ability past the engine table and the overlay traps");
}

static void test_move_overlay(void)
{
    const char *why = "unset";

    CHECK(strcmp(mmo_display_move_name(1, &why), "Pound") == 0 && why == 0,
          "move 1 is Pound");
    CHECK(mmo_display_move_type(1, 0) == 0, "Pound is NORMAL");
    CHECK(strcmp(mmo_display_move_name(258, 0), "Snowscape") == 0,
          "move 258 is Hail in the engine and Snowscape in the overlay");
    CHECK(mmo_display_move_type(258, 0) == 15, "Hail / Snowscape stays ICE");

    CHECK(strcmp(mmo_display_move_name(1000, 0), "Trick-Or-Treat") == 0,
          "move 1000 is Trick-Or-Treat, overlay-only");
    why = 0;
    CHECK(mmo_display_move_type(1000, &why) == MMO_TYPE_INVALID && why != 0,
          "an overlay-only move has no type here");

    why = 0;
    CHECK(mmo_display_move_name(468, &why) == 0 && why != 0,
          "a move past Shadow Force with no overlay traps");
    CHECK(mmo_display_move_name(0, 0) == 0, "move 0 is not a move");
}

static void test_species_typing(void)
{
    int t1 = -3, t2 = -3;
    const char *why = "unset";

    CHECK(mmo_display_species_typing(1, &t1, &t2, &why) == 0 && why == 0
              && t1 == 12 && t2 == 3,
          "Bulbasaur is GRASS / POISON");
    CHECK(mmo_display_species_typing(36, &t1, &t2, 0) == 0
              && t1 == 0 && t2 == 0,
          "Clefable is still NORMAL / NORMAL, no Gen-6 retyping here");
    CHECK(mmo_display_species_typing(282, &t1, &t2, 0) == 0
              && t1 == 14 && t2 == 14,
          "Gardevoir is still PSYCHIC / PSYCHIC");

    why = 0;
    CHECK(mmo_display_species_typing(0, 0, 0, &why) == MMO_TYPE_INVALID
              && why != 0,
          "species 0 traps");
    why = 0;
    CHECK(mmo_display_species_typing(500, 0, 0, &why) == MMO_TYPE_INVALID
              && why != 0,
          "a species past Arceus traps");
}

static void test_catalogue_bases(void)
{
    const char *why = "unset";

    CHECK(strcmp(mmo_display_string(MMO_STR_TYPE_NAME + 0, &why), "NORMAL") == 0
              && why == 0,
          "catalogue 230000 is the type-name bank");
    CHECK(strcmp(mmo_display_string(MMO_STR_MOVE_NAME + 258, 0), "Snowscape")
              == 0,
          "catalogue 110258 is the Hail overlay");
    CHECK(mmo_display_string(1, 0) == 0, "an id with no bank and no overlay traps");
}

int display_tests_run(void)
{
    failures = 0;
    printf("display:\n");
    test_names();
    test_display_agrees();
    test_mystery_row_is_inert();
    test_eighteenth_type_traps();
    test_wire_numbering();
    test_dual_and_buckets();
    test_foresight_pair();
    test_ability_overlay();
    test_move_overlay();
    test_species_typing();
    test_catalogue_bases();
    return failures;
}
