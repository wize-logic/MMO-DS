/* The six looks. */
#include <stddef.h>
#include <string.h>

#include "appearance.h"

/* Why six rows, and where the other hundred and seven went. */

static const mmo_appearance catalog[] = {
    { MMO_APPEAR_GFX_PLAYER_M, "player_m", "PLATINUM   Lucas",
      MMO_APPEAR_GAME_PLATINUM, 0, -1, MMO_APPEAR_BODY_TYPE_BASE + 0, 1, 1, NULL },
    { MMO_APPEAR_GFX_PLAYER_F, "player_f", "PLATINUM   Dawn",
      MMO_APPEAR_GAME_PLATINUM, 1, -1, MMO_APPEAR_BODY_TYPE_BASE + 1, 1, 1, NULL },
    { MMO_APPEAR_LOOK_GFX_BASE + 0 * MMO_APPEAR_LOOK_STRIDE, "ethan", "HEARTGOLD  Ethan",
      MMO_APPEAR_GAME_HEARTGOLD, 0, 0, MMO_APPEAR_LOOK_TYPE_BASE + 0, 1, 1, NULL },
    { MMO_APPEAR_LOOK_GFX_BASE + 1 * MMO_APPEAR_LOOK_STRIDE, "lyra", "HEARTGOLD  Lyra",
      MMO_APPEAR_GAME_HEARTGOLD, 1, 1, MMO_APPEAR_LOOK_TYPE_BASE + 1, 1, 1, NULL },
    { MMO_APPEAR_LOOK_GFX_BASE + 2 * MMO_APPEAR_LOOK_STRIDE, "hilbert", "BLACK      Hilbert",
      MMO_APPEAR_GAME_BLACKWHITE, 0, 2, MMO_APPEAR_LOOK_TYPE_BASE + 2, 1, 1, NULL },
    { MMO_APPEAR_LOOK_GFX_BASE + 3 * MMO_APPEAR_LOOK_STRIDE, "hilda", "BLACK      Hilda",
      MMO_APPEAR_GAME_BLACKWHITE, 1, 3, MMO_APPEAR_LOOK_TYPE_BASE + 3, 1, 1, NULL },
};

#define CATALOG_COUNT ((int)(sizeof catalog / sizeof catalog[0]))

int mmo_appearance_count(void)
{
    return CATALOG_COUNT;
}

const mmo_appearance *mmo_appearance_at(int index)
{
    if (index < 0 || index >= CATALOG_COUNT)
        return NULL;
    return &catalog[index];
}

const mmo_appearance *mmo_appearance_by_name(const char *name)
{
    int i;

    if (!name)
        return NULL;
    for (i = 0; i < CATALOG_COUNT; i++)
        if (strcmp(catalog[i].name, name) == 0)
            return &catalog[i];
    return NULL;
}

const mmo_appearance *mmo_appearance_by_gfx(int gfx)
{
    int i;

    for (i = 0; i < CATALOG_COUNT; i++)
        if (catalog[i].gfx == gfx)
            return &catalog[i];
    return NULL;
}

int mmo_appearance_gender_gfx(int gender)
{
    return (gender & 1) ? MMO_APPEAR_GFX_PLAYER_F : MMO_APPEAR_GFX_PLAYER_M;
}

int mmo_appearance_is_drawable(int gfx)
{
    const mmo_appearance *a = mmo_appearance_by_gfx(gfx);
    return a != NULL && a->drawable;
}

int mmo_appearance_is_offered(int gfx)
{
    const mmo_appearance *a = mmo_appearance_by_gfx(gfx);
    return a != NULL && a->offered;
}

int mmo_appearance_body_type(int index)
{
    if (index < 0 || index >= CATALOG_COUNT || !catalog[index].offered)
        return -1;
    return catalog[index].type;
}

int mmo_appearance_index_from_type(int type)
{
    int i;

    if (type < MMO_APPEAR_BODY_TYPE_BASE)
        return -1;
    for (i = 0; i < CATALOG_COUNT; i++)
        if (catalog[i].type == type)
            return i;
    return -1;
}

int mmo_appearance_resolve(int gender, const mmo_skin_set *skins)
{
    int i;

    if (skins) {
        for (i = 0; i < MMO_SKIN_SLOTS; i++) {
            int index;

            if (!skins->slot[i].present)
                continue;
            index = mmo_appearance_index_from_type((int)skins->slot[i].type);
            if (index < 0)
                continue;
            if (catalog[index].drawable)
                return catalog[index].gfx;
        }
    }
    return mmo_appearance_gender_gfx(gender);
}

int mmo_appearance_apply_body(mmo_skin_set *skins, int index)
{
    int type;

    if (!skins)
        return -1;
    type = mmo_appearance_body_type(index);
    if (type < 0)
        return -1;
    skins->slot[MMO_SKIN_FOREHEAD].present = 1;
    skins->slot[MMO_SKIN_FOREHEAD].type = (u16)type;
    skins->slot[MMO_SKIN_FOREHEAD].color = 0;
    return 0;
}

int mmo_appearance_look_of_gfx(int gfx)
{
    int off = gfx - MMO_APPEAR_LOOK_GFX_BASE;

    if (off < 0 || off >= MMO_APPEAR_LOOK_COUNT * MMO_APPEAR_LOOK_STRIDE)
        return -1;
    return off / MMO_APPEAR_LOOK_STRIDE;
}

int mmo_appearance_look_gfx(int look, int state)
{
    if (look < 0 || look >= MMO_APPEAR_LOOK_COUNT || state < 0
        || state >= MMO_APPEAR_LOOK_STRIDE)
        return -1;
    return MMO_APPEAR_LOOK_GFX_BASE + look * MMO_APPEAR_LOOK_STRIDE + state;
}

const mmo_appearance *mmo_appearance_by_look(int look)
{
    int i;

    if (look < 0)
        return NULL;
    for (i = 0; i < CATALOG_COUNT; i++)
        if (catalog[i].look == look)
            return &catalog[i];
    return NULL;
}
