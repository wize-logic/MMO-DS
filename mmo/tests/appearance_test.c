/*
 * The six looks: what a player is offered, what a remote can wear, and how
 * a kept SkinSet names one.
 */
#include <stdio.h>
#include <string.h>

#include "appearance.h"
#include "game.h"

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

int appearance_tests_run(void);

int appearance_tests_run(void)
{
    mmo_skin_set skins;
    mmo_skin_set stored;
    const mmo_appearance *ethan, *hilda;
    int i, type, boys = 0, girls = 0;

    printf("the engine's two trainer models, as gender asked them:\n");
    CHECK(mmo_appearance_gender_gfx(0) == MMO_APPEAR_GFX_PLAYER_M,
        "gender 0 is the male trainer model");
    CHECK(mmo_appearance_gender_gfx(1) == MMO_APPEAR_GFX_PLAYER_F,
        "gender 1 is the female trainer model");
    CHECK(mmo_appearance_gender_gfx(2) == MMO_APPEAR_GFX_PLAYER_M,
        "only the low bit of gender is read");

    printf("the catalog is three games' two trainers, and nothing else:\n");
    CHECK(mmo_appearance_count() == 6, "six rows");
    for (i = 0; i < mmo_appearance_count(); i++) {
        const mmo_appearance *a = mmo_appearance_at(i);

        if (a->gender == 0)
            boys++;
        else
            girls++;
        if (!a->offered || !a->drawable || a->reason != NULL)
            failures++, printf("  FAIL %s is not offered and drawable\n", a->name);
        if (a->label == NULL || a->label[0] == '\0')
            failures++, printf("  FAIL %s has no label for the list\n", a->name);
        if (a->type < MMO_APPEAR_BODY_TYPE_BASE)
            failures++, printf("  FAIL %s has a type below our range\n", a->name);
        if (mmo_appearance_index_from_type(a->type) != i)
            failures++, printf("  FAIL %s's type does not name its own row\n", a->name);
        if (mmo_appearance_body_type(i) != a->type)
            failures++, printf("  FAIL %s's body type is not its type\n", a->name);
    }
    CHECK(boys == 3 && girls == 3, "three boys and three girls");
    CHECK(mmo_appearance_by_name("player_m") != NULL
              && mmo_appearance_by_name("player_m")->gfx == MMO_APPEAR_GFX_PLAYER_M
              && mmo_appearance_by_name("player_m")->game == MMO_APPEAR_GAME_PLATINUM,
        "player_m is Platinum's boy, graphics id 0");
    CHECK(mmo_appearance_by_name("player_f") != NULL
              && mmo_appearance_by_name("player_f")->gfx == MMO_APPEAR_GFX_PLAYER_F,
        "player_f is Platinum's girl, graphics id 97");
    CHECK(mmo_appearance_by_name("hiker") == NULL
              && mmo_appearance_by_name("cynthia") == NULL,
        "the people of Sinnoh are no longer bodies");
    CHECK(mmo_appearance_by_name("no_such_body") == NULL,
        "an unknown name is not a body");
    CHECK(mmo_appearance_at(-1) == NULL &&
              mmo_appearance_at(mmo_appearance_count()) == NULL,
        "walking off either end of the catalog gives no row");

    printf("the two Platinum rows keep the types they have always had:\n");
    CHECK(mmo_appearance_index_from_type(MMO_APPEAR_BODY_TYPE_BASE + 0) == 0
              && mmo_appearance_at(0)->gfx == MMO_APPEAR_GFX_PLAYER_M,
        "type 512 is still Lucas");
    CHECK(mmo_appearance_index_from_type(MMO_APPEAR_BODY_TYPE_BASE + 1) == 1
              && mmo_appearance_at(1)->gfx == MMO_APPEAR_GFX_PLAYER_F,
        "type 513 is still Dawn");
    CHECK(mmo_appearance_index_from_type(MMO_APPEAR_BODY_TYPE_BASE + 2) < 0
              && mmo_appearance_index_from_type(MMO_APPEAR_BODY_TYPE_BASE + 20) < 0
              && mmo_appearance_index_from_type(MMO_APPEAR_LOOK_TYPE_BASE - 1) < 0,
        "an index of the old catalog names no row now");
    CHECK(mmo_appearance_index_from_type(MMO_APPEAR_BODY_TYPE_BASE - 1) < 0
              && mmo_appearance_index_from_type(30) < 0,
        "an official cosmetic type is not ours");

    printf("the composed looks own a band of graphics ids each:\n");
    ethan = mmo_appearance_by_name("ethan");
    hilda = mmo_appearance_by_name("hilda");
    CHECK(ethan != NULL && ethan->game == MMO_APPEAR_GAME_HEARTGOLD
              && ethan->gender == 0 && ethan->look == 0,
        "ethan is HeartGold's boy, look 0");
    CHECK(hilda != NULL && hilda->game == MMO_APPEAR_GAME_BLACKWHITE
              && hilda->gender == 1 && hilda->look == 3,
        "hilda is Black's girl, look 3");
    CHECK(ethan != NULL && ethan->gfx == MMO_APPEAR_LOOK_GFX_BASE,
        "look 0's walk is the first id of the band");
    CHECK(mmo_appearance_look_gfx(0, MMO_APPEAR_STATE_WALK) == MMO_APPEAR_LOOK_GFX_BASE
              && mmo_appearance_look_gfx(3, MMO_APPEAR_STATE_BIKE)
                     == MMO_APPEAR_LOOK_GFX_BASE + 3 * MMO_APPEAR_LOOK_STRIDE + 1,
        "a sheet's id is base + look * stride + state");
    CHECK(mmo_appearance_look_gfx(4, 0) < 0 && mmo_appearance_look_gfx(0, -1) < 0
              && mmo_appearance_look_gfx(0, MMO_APPEAR_LOOK_STRIDE) < 0,
        "a look or a state off the band has no id");
    CHECK(mmo_appearance_look_of_gfx(MMO_APPEAR_LOOK_GFX_BASE + 2 * MMO_APPEAR_LOOK_STRIDE + 5) == 2
              && mmo_appearance_look_of_gfx(MMO_APPEAR_LOOK_GFX_BASE - 1) < 0
              && mmo_appearance_look_of_gfx(MMO_APPEAR_GFX_PLAYER_F) < 0
              && mmo_appearance_look_of_gfx(MMO_APPEAR_LOOK_GFX_BASE
                                            + MMO_APPEAR_LOOK_COUNT * MMO_APPEAR_LOOK_STRIDE) < 0,
        "any sheet of a band names its look, and nothing outside does");
    CHECK(mmo_appearance_by_look(2) != NULL
              && strcmp(mmo_appearance_by_look(2)->name, "hilbert") == 0
              && mmo_appearance_by_look(-1) == NULL && mmo_appearance_by_look(4) == NULL,
        "a look number finds its row");
    CHECK(MMO_APPEAR_STATE_COUNT <= MMO_APPEAR_LOOK_STRIDE,
        "every state fits in the band");
    CHECK(MMO_APPEAR_LOOK_GFX_BASE > 463
              && MMO_APPEAR_LOOK_GFX_BASE + MMO_APPEAR_LOOK_COUNT * MMO_APPEAR_LOOK_STRIDE <= 1024,
        "the bands sit between a world package's ids and the follower band");

    printf("an empty SkinSet draws the gender's trainer, and a body type seats one:\n");
    memset(&skins, 0, sizeof skins);
    CHECK(mmo_appearance_resolve(0, &skins) == MMO_APPEAR_GFX_PLAYER_M,
        "empty skins, male, is the male trainer");
    CHECK(mmo_appearance_resolve(1, &skins) == MMO_APPEAR_GFX_PLAYER_F,
        "empty skins, female, is the female trainer");
    CHECK(mmo_appearance_resolve(0, NULL) == MMO_APPEAR_GFX_PLAYER_M,
        "no SkinSet at all is the gender default");

    i = mmo_appearance_index_from_type(ethan != NULL ? ethan->type : -1);
    type = mmo_appearance_body_type(i);
    CHECK(type == MMO_APPEAR_LOOK_TYPE_BASE, "ethan's type is the first of the look band");
    CHECK(mmo_appearance_apply_body(&skins, i) == 0, "apply_body writes the slot");
    CHECK(skins.slot[MMO_SKIN_FOREHEAD].present &&
              skins.slot[MMO_SKIN_FOREHEAD].type == (u16)type,
        "the look is stored as a forehead type in our range");
    CHECK(mmo_appearance_resolve(0, &skins) == MMO_APPEAR_LOOK_GFX_BASE,
        "a kept ethan draws as look 0's walking sheet");
    CHECK(mmo_appearance_resolve(1, &skins) == MMO_APPEAR_LOOK_GFX_BASE,
        "the look wins over gender");

    /*
     * A character made before the catalog shrank. Its forehead slot holds an index of the old
     * table, a hiker was 514 + 18, and that type names no row now, so resolve answers the
     * gender's trainer instead of a body nobody composes any more.
     */
    memset(&stored, 0, sizeof stored);
    stored.slot[MMO_SKIN_FOREHEAD].present = 1;
    stored.slot[MMO_SKIN_FOREHEAD].type = (u16)(MMO_APPEAR_BODY_TYPE_BASE + 20);
    CHECK(mmo_appearance_resolve(0, &stored) == MMO_APPEAR_GFX_PLAYER_M,
        "a stored old-catalog body draws as the trainer");
    CHECK(mmo_appearance_resolve(1, &stored) == MMO_APPEAR_GFX_PLAYER_F,
        "and as her own gender's trainer, not always the male one");

    /* A official-sized type is kept and does not become a body. */
    memset(&skins, 0, sizeof skins);
    skins.slot[MMO_SKIN_HAIR].present = 1;
    skins.slot[MMO_SKIN_HAIR].type = 30;
    skins.slot[MMO_SKIN_HAIR].color = 10;
    CHECK(mmo_appearance_resolve(0, &skins) == MMO_APPEAR_GFX_PLAYER_M,
        "a cosmetic type below the body range is not a sprite");

    /* Off the catalog there is still no body. */
    CHECK(mmo_appearance_body_type(-1) < 0 &&
              mmo_appearance_body_type(mmo_appearance_count()) < 0,
        "apply_body refuses an index outside the catalog");
    CHECK(!mmo_appearance_is_drawable(20) && !mmo_appearance_is_offered(20),
        "graphics id 20, the hiker, is neither drawable nor offered as a body");

    if (failures) {
        printf("appearance: %d check(s) FAILED\n", failures);
    } else {
        printf("appearance: all checks passed\n");
    }
    return failures;
}
