/* The character list and the creator a person meets. */
#include "appearance.h"
#include "creator.h"
#include "entry.h"
#include "game.h"
#include "region.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (cond) {                                                            \
            printf("  ok   %s\n", msg);                                        \
        } else {                                                               \
            printf("  FAIL %s\n", msg);                                        \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static mmo_character_list two_chars(void)
{
    mmo_character_list list;

    memset(&list, 0, sizeof list);
    list.count = 2;
    list.held = 2;
    list.entry[0].id = 1;
    snprintf(list.entry[0].name, sizeof list.entry[0].name, "Lucas");
    list.entry[0].gender = 0;
    list.entry[0].region = MMO_REGION_SINNOH;
    list.entry[1].id = 2;
    snprintf(list.entry[1].name, sizeof list.entry[1].name, "Dawn");
    list.entry[1].gender = 1;
    list.entry[1].region = MMO_REGION_SINNOH;
    return list;
}

static void test_empty_list_is_new(void)
{
    mmo_creator c;
    mmo_creator_row row;

    printf("an empty list is a new player's first screen:\n");
    mmo_creator_reset(&c);
    mmo_creator_set_list(&c, NULL);
    CHECK(c.step == MMO_CREATOR_SELECT, "the list opens");
    CHECK(mmo_creator_visible_count(&c) == 1, "one row");
    CHECK(mmo_creator_get_row(&c, 0, &row) == 1, "the row reads");
    CHECK(strcmp(row.text, "NEW CHARACTER") == 0, "and it is NEW CHARACTER");
    CHECK(mmo_creator_confirm(&c) == 1 && c.step == MMO_CREATOR_NAME,
          "A starts the creator");
    CHECK(mmo_entry_path_for(MMO_ENTRY_NAME) == MMO_ENTRY_FIELD,
          "the name is the field, not the naming screen");
    CHECK(mmo_creator_needs_entry(&c), "the name step wants the field");
}

static void test_pick_existing(void)
{
    mmo_creator c;
    mmo_character_list list = two_chars();
    mmo_creator_row row;

    printf("a list with people on it is a picker, plus NEW CHARACTER:\n");
    mmo_creator_reset(&c);
    mmo_creator_set_list(&c, &list);
    CHECK(mmo_creator_visible_count(&c) == 3, "two characters and NEW");
    CHECK(mmo_creator_get_row(&c, 0, &row) == 1
              && strstr(row.text, "Lucas") != NULL,
          "the first row is Lucas");
    mmo_creator_move(&c, MMO_CREATOR_DOWN);
    CHECK(mmo_creator_confirm(&c) == 1 && c.step == MMO_CREATOR_ACTION,
          "A on Dawn opens what to do with her");
    CHECK(mmo_creator_get_row(&c, 0, &row) == 1
              && strstr(row.text, "Dawn") != NULL
              && strstr(row.text, "PLAY") != NULL,
          "the first thing offered is playing as her");
    CHECK(mmo_creator_confirm(&c) == 1, "A on PLAY picks her");
    CHECK(mmo_creator_has_pick(&c) && mmo_creator_pick_index(&c) == 1,
          "the pick is index 1");
    CHECK(!mmo_creator_ready(&c), "picking is not creating");
    CHECK(!mmo_creator_has_delete(&c), "and it deletes nothing");
}

/* A delete is two presses of A on two different rows, and the row it lands on
 * has to be the one the list said. The wire half is client.c's; this is the
 * screen deciding what to ask it for. */
static void test_delete_existing(void)
{
    mmo_creator c;
    mmo_character_list list = two_chars();
    mmo_creator_row row;

    printf("deleting a character takes two deliberate presses:\n");
    mmo_creator_reset(&c);
    mmo_creator_set_list(&c, &list);
    mmo_creator_move(&c, MMO_CREATOR_DOWN);
    mmo_creator_confirm(&c);
    CHECK(c.step == MMO_CREATOR_ACTION, "Dawn's row opens the menu");

    mmo_creator_move(&c, MMO_CREATOR_DOWN);
    CHECK(mmo_creator_get_row(&c, 1, &row) == 1
              && strstr(row.text, "DELETE") != NULL
              && strstr(row.text, "Dawn") != NULL,
          "the second row deletes her by name");
    CHECK(mmo_creator_confirm(&c) == 1 && c.step == MMO_CREATOR_DELETE,
          "A on DELETE asks again");
    CHECK(c.cursor == 0, "the confirmation opens on KEEP");
    CHECK(!mmo_creator_has_delete(&c), "and has deleted nothing yet");

    CHECK(mmo_creator_get_row(&c, 0, &row) == 1
              && strstr(row.text, "KEEP") != NULL,
          "KEEP is what the cursor is sitting on");
    CHECK(mmo_creator_confirm(&c) == 1 && c.step == MMO_CREATOR_ACTION,
          "A on KEEP goes back without deleting");
    CHECK(!mmo_creator_has_delete(&c), "still nothing deleted");

    mmo_creator_move(&c, MMO_CREATOR_DOWN);
    mmo_creator_confirm(&c);
    mmo_creator_move(&c, MMO_CREATOR_DOWN);
    CHECK(mmo_creator_confirm(&c) == 1, "A on DELETE the second time takes it");
    CHECK(mmo_creator_has_delete(&c) && mmo_creator_delete_index(&c) == 1,
          "the row to delete is Dawn's, index 1");
    CHECK(!mmo_creator_has_pick(&c), "a delete is not a pick");

    /* The shorter list the server sends back closes the whole thing. */
    mmo_creator_set_list(&c, &list);
    CHECK(!mmo_creator_has_delete(&c) && c.step == MMO_CREATOR_SELECT,
          "the refreshed list puts the screen back on the list");
}

/* B walks the two new steps back out again rather than stranding a person on
 * a confirmation. */
static void test_delete_back_out(void)
{
    mmo_creator c;
    mmo_character_list list = two_chars();

    printf("B leaves the delete steps the way it came:\n");
    mmo_creator_reset(&c);
    mmo_creator_set_list(&c, &list);
    mmo_creator_confirm(&c);
    mmo_creator_move(&c, MMO_CREATOR_DOWN);
    mmo_creator_confirm(&c);
    CHECK(c.step == MMO_CREATOR_DELETE, "on the confirmation");
    CHECK(mmo_creator_back(&c) == 1 && c.step == MMO_CREATOR_ACTION,
          "B goes back to the menu");
    CHECK(mmo_creator_back(&c) == 1 && c.step == MMO_CREATOR_SELECT,
          "B again goes back to the list");
    CHECK(c.cursor == 0, "on the row it came from");
    CHECK(!mmo_creator_has_delete(&c), "and nothing was deleted");
}

static void walk_to_ready(mmo_creator *c)
{
    mmo_creator_set_list(c, NULL);
    mmo_creator_confirm(c);
    mmo_creator_set_name(c, "Barry");
    mmo_creator_confirm(c); /* gender: boy */
    /* The region cursor opens on the selectable row. */
    mmo_creator_confirm(c);
    mmo_creator_confirm(c); /* default body */
}

static void test_create_walk(void)
{
    mmo_creator c;
    mmo_create_character req;
    const mmo_appearance *body;

    printf("the four steps fill a CreateCharacter the writer already sends:\n");
    mmo_creator_reset(&c);
    walk_to_ready(&c);
    CHECK(mmo_creator_ready(&c), "A on the body finishes");
    CHECK(mmo_creator_fill_create(&c, &req) == 0, "the request fills");
    CHECK(strcmp(req.name, "Barry") == 0, "the name is the one typed");
    CHECK(req.gender == 0, "boy is gender 0");
    CHECK(req.starting_region == MMO_REGION_SINNOH, "the region is Sinnoh");
    CHECK(mmo_region_is_selectable(req.starting_region),
          "and Sinnoh is the selectable row");
    body = mmo_appearance_at(mmo_appearance_index_from_type(
        (int)req.appearance.slot[MMO_SKIN_FOREHEAD].type));
    CHECK(body != NULL && body->offered && body->gfx == MMO_APPEAR_GFX_PLAYER_M,
          "the default body is the boy's trainer");
}

static void test_grey_region(void)
{
    mmo_creator c;
    mmo_creator_row row;
    int i, grey = 0, live = 0;

    printf("a greyed region does not create, and the whole roster is shown:\n");
    mmo_creator_reset(&c);
    mmo_creator_set_list(&c, NULL);
    mmo_creator_confirm(&c);
    mmo_creator_set_name(&c, "Gardenia");
    mmo_creator_confirm(&c);
    CHECK(c.step == MMO_CREATOR_REGION, "the region step is next");
    CHECK(mmo_creator_visible_count(&c) == 5,
          "five offered regions: Kanto Johto Hoenn Sinnoh Unova");
    for (i = 0; i < mmo_creator_visible_count(&c); i++) {
        CHECK(mmo_creator_get_row(&c, i, &row), "each region row reads");
        if (row.greyed)
            grey++;
        else
            live++;
    }
    CHECK(grey == 4 && live == 1, "four greyed, Sinnoh live");
    /* Cursor opened on Sinnoh; wrap back to Kanto. */
    mmo_creator_move(&c, MMO_CREATOR_UP);
    mmo_creator_move(&c, MMO_CREATOR_UP);
    mmo_creator_move(&c, MMO_CREATOR_UP);
    CHECK(mmo_creator_get_row(&c, 0, &row) && row.selected && row.greyed,
          "Kanto is selected and greyed");
    CHECK(mmo_creator_confirm(&c) == 0 && c.step == MMO_CREATOR_REGION,
          "A on Kanto does not advance");
}

static void test_back_and_name_cap(void)
{
    mmo_creator c;
    char longn[40];
    int i;

    printf("B walks back, and a name stops where the wire stops:\n");
    mmo_creator_reset(&c);
    mmo_creator_set_list(&c, NULL);
    mmo_creator_confirm(&c);
    CHECK(mmo_creator_set_name(&c, "") == 0 && c.step == MMO_CREATOR_NAME,
          "an empty name is refused");
    for (i = 0; i < 33; i++)
        longn[i] = 'A';
    longn[33] = '\0';
    CHECK(mmo_creator_set_name(&c, longn) == 0, "33 characters are refused");
    CHECK(mmo_entry_max_units(MMO_ENTRY_NAME) == MMO_CHAR_NAME_MAX,
          "the field cap is the wire's 32");
    CHECK(mmo_creator_set_name(&c, "Cynthia") == 1
              && c.step == MMO_CREATOR_GENDER,
          "a real name advances");
    CHECK(mmo_creator_back(&c) == 1 && c.step == MMO_CREATOR_NAME,
          "B from gender returns to the name");
    CHECK(mmo_creator_back(&c) == 1 && c.step == MMO_CREATOR_SELECT,
          "B from the name returns to the list");
}

static void test_girl_default_body(void)
{
    mmo_creator c;
    mmo_create_character req;
    const mmo_appearance *body;

    printf("a girl defaults to the girl's trainer, not a leftover boy sprite:\n");
    mmo_creator_reset(&c);
    mmo_creator_set_list(&c, NULL);
    mmo_creator_confirm(&c);
    mmo_creator_set_name(&c, "Dawn");
    mmo_creator_move(&c, MMO_CREATOR_RIGHT);
    CHECK(c.gender == 1, "right is the girl, as the opening's pad is");
    mmo_creator_confirm(&c);
    mmo_creator_confirm(&c);
    mmo_creator_confirm(&c);
    CHECK(mmo_creator_fill_create(&c, &req) == 0, "the girl request fills");
    CHECK(req.gender == 1, "gender 1");
    body = mmo_appearance_at(mmo_appearance_index_from_type(
        (int)req.appearance.slot[MMO_SKIN_FOREHEAD].type));
    CHECK(body != NULL && body->gfx == MMO_APPEAR_GFX_PLAYER_F,
          "the default body is the girl's trainer");
}

/* The list widget owns its own scrolling and asks for any row by absolute
 * index; the appearance step is the long one and offers the whole catalog. */
/* The i-th catalog row of one gender, the way the creator lists them. */
static int offered_row_of_gender(int gender, int want)
{
    int i, seen = 0;

    for (i = 0; i < mmo_appearance_count(); i++) {
        const mmo_appearance *a = mmo_appearance_at(i);

        if (!a->offered || a->gender != gender)
            continue;
        if (seen == want)
            return i;
        seen++;
    }
    return -1;
}

static void test_appear_full_list(void)
{
    mmo_creator c;
    mmo_creator_row row;
    int n;

    printf("the appearance step lists the chosen gender's looks, absolutely addressed:\n");
    mmo_creator_reset(&c);
    mmo_creator_set_list(&c, NULL);
    mmo_creator_confirm(&c);
    mmo_creator_set_name(&c, "Rowan");
    mmo_creator_confirm(&c);
    mmo_creator_confirm(&c);
    CHECK(c.step == MMO_CREATOR_APPEAR, "on the appearance step");
    n = mmo_creator_row_count(&c);
    {
        /* Not the whole catalog: the gender was chosen a step ago, so the
         * list is that gender's rows, one look a game, and the other
         * gender's are not on it. */
        int offered = 0, i;

        for (i = 0; i < mmo_appearance_count(); i++) {
            const mmo_appearance *a = mmo_appearance_at(i);

            if (a->offered && a->gender == c.gender)
                offered++;
        }
        CHECK(n == offered, "every offered look of the gender is a list row");
        CHECK(n < mmo_appearance_count(),
            "and the other gender's are not, so the list is not the raw catalog");
        CHECK(n == 3, "three games, one boy each");
        for (i = 0; i < n; i++)
            CHECK(mmo_creator_appear_gfx_at(&c, i)
                      == mmo_appearance_at(offered_row_of_gender(c.gender, i))->gfx,
                  "the row's preview gfx is that row's walking sheet");
        CHECK(mmo_creator_appear_gfx_at(&c, n) < 0, "and off the end there is none");
    }
    CHECK(mmo_creator_visible_count(&c) == n && n < MMO_CREATOR_VISIBLE,
          "three rows fit the window without scrolling");
    CHECK(mmo_creator_row_at(&c, 0, &row) && row.selected,
          "row 0 is the cursor's");
    CHECK(mmo_creator_row_at(&c, n - 1, &row) && !row.selected
              && row.text[0] != '\0',
          "the last row reads without scrolling");
    CHECK(!mmo_creator_row_at(&c, n, &row), "off the end is refused");
}

/* PLAY offline's front door: one save per cartridge, so the list is the
 * screen. No NEW CHARACTER row to meet before the character, and no action
 * menu behind the row, A on it is the pick, because PLAY is the only verb
 * offline and the launcher owns restore. */
static void test_fixed_list_is_the_whole_screen(void)
{
    mmo_creator c;
    mmo_character_list list = two_chars();
    mmo_creator_row row;

    printf("a fixed list has no create row and no action menu:\n");
    list.count = 1;
    list.held = 1;
    mmo_creator_reset(&c);
    mmo_creator_fix_list(&c, 1);
    mmo_creator_set_list(&c, &list);
    CHECK(c.step == MMO_CREATOR_SELECT, "the list opens");
    CHECK(mmo_creator_row_count(&c) == 1, "one row, and it is the character");
    CHECK(mmo_creator_row_at(&c, 0, &row) == 1
              && strstr(row.text, "Lucas") != NULL,
          "the row is Lucas");
    CHECK(!mmo_creator_row_at(&c, 1, &row), "there is no NEW CHARACTER after it");
    CHECK(strstr(mmo_creator_hint(&c), "NEW CHARACTER") == NULL,
          "and the footer does not offer one");
    mmo_creator_move(&c, MMO_CREATOR_DOWN);
    CHECK(c.cursor == 0, "DOWN cannot walk off the only row");
    CHECK(mmo_creator_confirm(&c) == 1 && c.step == MMO_CREATOR_SELECT,
          "A does not open the action menu");
    CHECK(mmo_creator_has_pick(&c) && mmo_creator_pick_index(&c) == 0,
          "A is the pick itself");
    CHECK(!mmo_creator_has_delete(&c), "and nothing was offered to delete");

    printf("a refusal on a fixed list is why the row will not open:\n");
    mmo_creator_reset(&c);
    mmo_creator_fix_list(&c, 1);
    mmo_creator_set_list(&c, &list);
    mmo_creator_refuse(&c, "SPECIES 634 NEEDS YOUR BLACK CARTRIDGE");
    CHECK(strcmp(mmo_creator_hint(&c), "SPECIES 634 NEEDS YOUR BLACK CARTRIDGE") == 0,
          "the sentence is the hint under the row");
    CHECK(mmo_creator_confirm(&c) == 0 && !mmo_creator_has_pick(&c),
          "and A does not pick it");
    CHECK(strcmp(mmo_creator_hint(&c), "SPECIES 634 NEEDS YOUR BLACK CARTRIDGE") == 0,
          "the sentence stays up, because it is still the reason");

    printf("the same list without the flag is the online screen:\n");
    mmo_creator_reset(&c);
    mmo_creator_set_list(&c, &list);
    CHECK(mmo_creator_row_count(&c) == 2, "one character and NEW CHARACTER");
    CHECK(mmo_creator_confirm(&c) == 1 && c.step == MMO_CREATOR_ACTION,
          "and A still opens what to do with the row");
}

/* The other half of that front door: nothing saved here yet. The list is empty,
 * so the only row is the one past the end, and offline that row is NEW GAME,
 * one press, because the cartridge's own opening asks the name and the gender
 * the four creator steps would ask first. */
static void test_direct_new_is_one_press(void)
{
    mmo_creator c;
    mmo_creator_row row;

    printf("an empty list offline is one row that starts a new game:\n");
    mmo_creator_reset(&c);
    mmo_creator_direct_new(&c, 1);
    mmo_creator_set_list(&c, NULL);
    CHECK(mmo_creator_row_count(&c) == 1, "one row");
    CHECK(mmo_creator_row_at(&c, 0, &row) && strcmp(row.text, "NEW GAME") == 0,
          "and it says NEW GAME, not NEW CHARACTER");
    CHECK(strstr(mmo_creator_hint(&c), "NEW GAME") != NULL,
          "the footer says so too");
    CHECK(mmo_creator_confirm(&c) == 1, "A takes it");
    CHECK(c.step == MMO_CREATOR_SELECT,
          "and the step does not move, so no name screen opens");
    CHECK(!mmo_creator_needs_entry(&c), "nothing is waiting to be typed");
    CHECK(mmo_creator_wants_new(&c), "the caller is told to start one");
    CHECK(!mmo_creator_has_pick(&c) && !mmo_creator_ready(&c),
          "and it is neither a pick nor a filled-in create");
    mmo_creator_begin_wait(&c);
    CHECK(!mmo_creator_wants_new(&c), "the answer is taken once");

    printf("the same empty list online still walks the creator:\n");
    mmo_creator_reset(&c);
    mmo_creator_set_list(&c, NULL);
    CHECK(mmo_creator_row_at(&c, 0, &row)
              && strcmp(row.text, "NEW CHARACTER") == 0,
          "the row is NEW CHARACTER");
    CHECK(mmo_creator_confirm(&c) == 1 && c.step == MMO_CREATOR_NAME,
          "and A starts the four steps");
    CHECK(!mmo_creator_wants_new(&c), "nothing asks the caller for a new game");
}

int creator_tests_run(void)
{
    failures = 0;
    test_empty_list_is_new();
    test_pick_existing();
    test_delete_existing();
    test_delete_back_out();
    test_create_walk();
    test_grey_region();
    test_back_and_name_cap();
    test_girl_default_body();
    test_appear_full_list();
    test_fixed_list_is_the_whole_screen();
    test_direct_new_is_one_press();

    if (failures)
        printf("creator: %d check(s) FAILED\n", failures);
    else
        printf("creator: all checks passed\n");
    return failures;
}
