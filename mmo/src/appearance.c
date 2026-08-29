/* The drawable-body catalog. */
#include <stddef.h>
#include <string.h>

#include "appearance.h"

static const mmo_appearance catalog[] = {
    {   0, "player_m", 1, 1, NULL },
    {  97, "player_f", 1, 1, NULL },
    {   1, "ninja_boy", 1, 1, NULL },
    {   2, "twin", 1, 1, NULL },
    {   3, "school_kid_m", 1, 1, NULL },
    {   4, "youngster", 1, 1, NULL },
    {   5, "bug_catcher", 1, 1, NULL },
    {   6, "lass", 1, 1, NULL },
    {   7, "battle_girl", 1, 1, NULL },
    {   8, "school_kid_f", 1, 1, NULL },
    {   9, "pokemon_breeder_m", 1, 1, NULL },
    {  10, "guitarist", 1, 1, NULL },
    {  11, "ace_trainer_m", 1, 1, NULL },
    {  12, "pokemon_breeder_f", 1, 1, NULL },
    {  13, "beauty", 1, 1, NULL },
    {  14, "ace_trainer_f", 1, 1, NULL },
    {  15, "pokefan_m", 1, 1, NULL },
    {  16, "pokefan_f", 1, 1, NULL },
    {  17, "expert_m", 1, 1, NULL },
    {  18, "expert_f", 1, 1, NULL },
    {  19, "collector", 1, 1, NULL },
    {  20, "hiker", 1, 1, NULL },
    {  22, "reporter", 1, 1, NULL },
    {  23, "cameraman", 1, 1, NULL },
    {  24, "cashier_m", 1, 1, NULL },
    {  25, "cashier_f", 1, 1, NULL },
    {  27, "teala", 1, 1, NULL },
    {  29, "scientist_m", 1, 1, NULL },
    {  30, "scientist_f", 1, 1, NULL },
    {  31, "roughneck", 1, 1, NULL },
    {  32, "skier_m", 1, 1, NULL },
    {  33, "skier_f", 1, 1, NULL },
    {  34, "policeman", 1, 1, NULL },
    {  35, "idol", 1, 1, NULL },
    {  36, "gentleman", 1, 1, NULL },
    {  37, "socialite", 1, 1, NULL },
    {  38, "cyclist_m", 1, 1, NULL },
    {  39, "cyclist_f", 1, 1, NULL },
    {  40, "worker", 1, 1, NULL },
    {  41, "rancher", 1, 1, NULL },
    {  42, "cowgirl", 1, 1, NULL },
    {  43, "clown", 1, 1, NULL },
    {  44, "artist", 1, 1, NULL },
    {  45, "jogger", 1, 1, NULL },
    {  46, "swimmer_m", 1, 1, NULL },
    {  47, "swimmer_f", 1, 1, NULL },
    {  48, "tuber_f", 1, 1, NULL },
    {  49, "tuber_m", 1, 1, NULL },
    {  50, "ruin_maniac", 1, 1, NULL },
    {  51, "black_belt", 1, 1, NULL },
    {  52, "camper", 1, 1, NULL },
    {  53, "picnicker", 1, 1, NULL },
    {  54, "fisherman", 1, 1, NULL },
    {  55, "parasol_lady", 1, 1, NULL },
    {  56, "sailor", 1, 1, NULL },
    {  59, "waiter", 1, 1, NULL },
    {  60, "waitress", 1, 1, NULL },
    {  62, "rich_boy", 1, 1, NULL },
    {  63, "lady", 1, 1, NULL },
    {  64, "snowpoint_npc_m", 1, 1, NULL },
    {  65, "snowpoint_npc_f", 1, 1, NULL },
    {  68, "ace_trainer_snow_m", 1, 1, NULL },
    {  69, "ace_trainer_snow_f", 1, 1, NULL },
    {  70, "psychic", 1, 1, NULL },
    {  81, "baby_in_pram", 1, 1, NULL },
    {  82, "middle_aged_man", 1, 1, NULL },
    {  83, "middle_aged_woman", 1, 1, NULL },
    {  99, "prof_rowan", 1, 1, NULL },
    { 120, "cyrus", 1, 1, NULL },
    { 121, "mars", 1, 1, NULL },
    { 122, "saturn", 1, 1, NULL },
    { 123, "jupiter", 1, 1, NULL },
    { 124, "grunt_m", 1, 1, NULL },
    { 125, "grunt_f", 1, 1, NULL },
    { 126, "roark", 1, 1, NULL },
    { 127, "gardenia", 1, 1, NULL },
    { 128, "crasher_wake", 1, 1, NULL },
    { 129, "maylene", 1, 1, NULL },
    { 130, "fantina", 1, 1, NULL },
    { 131, "candice", 1, 1, NULL },
    { 132, "byron", 1, 1, NULL },
    { 133, "volkner", 1, 1, NULL },
    { 134, "aaron", 1, 1, NULL },
    { 135, "bertha", 1, 1, NULL },
    { 136, "flint", 1, 1, NULL },
    { 137, "lucian", 1, 1, NULL },
    { 138, "cynthia", 1, 1, NULL },
    { 140, "mom", 1, 1, NULL },
    { 141, "cheryl", 1, 1, NULL },
    { 142, "riley", 1, 1, NULL },
    { 143, "marley", 1, 1, NULL },
    { 144, "buck", 1, 1, NULL },
    { 145, "mira", 1, 1, NULL },
    { 148, "barry", 1, 1, NULL },
    { 163, "receptionist", 1, 1, NULL },
    { 164, "old_man", 1, 1, NULL },
    { 165, "old_woman", 1, 1, NULL },
    { 166, "prof_oak", 1, 1, NULL },
    { 167, "jasmine", 1, 1, NULL },
    { 168, "gym_guide", 1, 1, NULL },
    { 169, "palmer", 1, 1, NULL },
    { 175, "maid", 1, 1, NULL },
    { 193, "mystery_gift_deliveryman", 1, 1, NULL },
    { 194, "kid_with_nds", 1, 1, NULL },
    { 213, "looker", 1, 1, NULL },
    { 214, "charon", 1, 1, NULL },
    { 215, "thorton", 1, 1, NULL },
    { 216, "argenta", 1, 1, NULL },
    { 217, "darach", 1, 1, NULL },
    { 218, "dahlia", 1, 1, NULL },
    { 219, "caitlin", 1, 1, NULL },
    { 231, "frontier_single_attendant", 1, 1, NULL },
    { 232, "frontier_multi_attendant", 1, 1, NULL },
    { 233, "frontier_booth_attendant", 1, 1, NULL },
    { 234, "wifi_plaza_attendant_m", 1, 1, NULL },
    { 235, "wifi_plaza_attendant_f", 1, 1, NULL },
    { 242, "game_director", 1, 1, NULL },
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
    return MMO_APPEAR_BODY_TYPE_BASE + index;
}

int mmo_appearance_index_from_type(int type)
{
    int index;

    if (type < MMO_APPEAR_BODY_TYPE_BASE)
        return -1;
    index = type - MMO_APPEAR_BODY_TYPE_BASE;
    if (index < 0 || index >= CATALOG_COUNT)
        return -1;
    return index;
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
