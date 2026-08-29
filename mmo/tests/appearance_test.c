/*
 * The drawable-body catalog: what a player is offered, what a remote can
 * wear, and how a kept SkinSet names one.
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
    const mmo_appearance *hiker;
    int i, offered, drawable, type;

    printf("the engine's two trainer models, as gender asked them:\n");
    CHECK(mmo_appearance_gender_gfx(0) == MMO_APPEAR_GFX_PLAYER_M,
        "male walking is graphics id 0");
    CHECK(mmo_appearance_gender_gfx(1) == MMO_APPEAR_GFX_PLAYER_F,
        "female walking is graphics id 97");
    CHECK(mmo_appearance_gender_gfx(2) == MMO_APPEAR_GFX_PLAYER_M,
        "a gender bit other than 1 is still the male model");

    printf("the catalog is the walk-controller people, not every gfx id:\n");
    CHECK(mmo_appearance_count() > 80, "the catalog holds the people, not two rows");
    CHECK(mmo_appearance_by_name("hiker") != NULL &&
              mmo_appearance_by_name("hiker")->gfx == 20,
        "hiker is graphics id 20");
    CHECK(mmo_appearance_by_name("lass") != NULL &&
              mmo_appearance_by_name("lass")->offered,
        "lass is offered");
    CHECK(mmo_appearance_by_name("cynthia") != NULL &&
              mmo_appearance_by_name("cynthia")->offered,
        "cynthia is offered");
    CHECK(mmo_appearance_by_name("volkner") != NULL &&
              mmo_appearance_by_name("volkner")->offered,
        "a gym leader is offered too: nobody is fenced off");
    CHECK(mmo_appearance_by_name("no_such_body") == NULL,
        "an unknown name has no row");
    CHECK(!mmo_appearance_is_drawable(84),
        "a boulder is not a body");

    offered = 0;
    drawable = 0;
    for (i = 0; i < mmo_appearance_count(); i++) {
        const mmo_appearance *a = mmo_appearance_at(i);
        offered += a->offered;
        drawable += a->drawable;
        if (a->offered && a->reason)
            failures++, printf("  FAIL offered row %s carries a reason\n", a->name);
        if (!a->offered && (a->reason == NULL || a->reason[0] == '\0'))
            failures++, printf("  FAIL greyed row %s has no reason\n", a->name);
        if (a->offered && !a->drawable)
            failures++, printf("  FAIL offered row %s is not drawable\n", a->name);
    }
    CHECK(offered == mmo_appearance_count(),
        "every walking face is offered: the whole catalog is the wardrobe");
    CHECK(drawable == mmo_appearance_count(), "every catalog row is drawable");
    CHECK(mmo_appearance_at(-1) == NULL &&
              mmo_appearance_at(mmo_appearance_count()) == NULL,
        "walking off either end of the catalog gives no row");

    printf("an empty SkinSet draws the gender's trainer, and a body type seats one:\n");
    memset(&skins, 0, sizeof skins);
    CHECK(mmo_appearance_resolve(0, &skins) == MMO_APPEAR_GFX_PLAYER_M,
        "empty skins, male, is the male trainer");
    CHECK(mmo_appearance_resolve(1, &skins) == MMO_APPEAR_GFX_PLAYER_F,
        "empty skins, female, is the female trainer");
    CHECK(mmo_appearance_resolve(0, NULL) == MMO_APPEAR_GFX_PLAYER_M,
        "no SkinSet at all is the gender default");

    hiker = mmo_appearance_by_name("hiker");
    CHECK(hiker != NULL, "hiker is in the catalog");
    /* index of hiker in the table */
    type = -1;
    for (i = 0; i < mmo_appearance_count(); i++) {
        if (mmo_appearance_at(i) == hiker) {
            type = mmo_appearance_body_type(i);
            break;
        }
    }
    CHECK(type >= MMO_APPEAR_BODY_TYPE_BASE, "a body type sits in our range");
    CHECK(mmo_appearance_apply_body(&skins, i) == 0, "apply_body writes the slot");
    CHECK(skins.slot[MMO_SKIN_FOREHEAD].present &&
              skins.slot[MMO_SKIN_FOREHEAD].type == (u16)type,
        "the body is stored as a forehead type in our range");
    CHECK(mmo_appearance_resolve(0, &skins) == 20,
        "a kept hiker body draws as graphics id 20");
    CHECK(mmo_appearance_resolve(1, &skins) == 20,
        "the body wins over gender");

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

    if (failures) {
        printf("appearance: %d check(s) FAILED\n", failures);
    } else {
        printf("appearance: all checks passed\n");
    }
    return failures;
}
