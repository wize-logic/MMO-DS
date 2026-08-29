/* HeartGold's FollowMon_GetSpriteID, with this game's numbers. */

#include "follower.h"

#include "follower_index.gen.h"

int mmo_follower_species_max(void)
{
    return MMO_FOLLOWER_SPECIES;
}

int mmo_follower_gfx_base(void)
{
    return MMO_FOLLOWER_GFX_BASE;
}

int mmo_follower_gfx_count(void)
{
    /* Both bands: the normal coats and the shiny ones behind them. */
    return MMO_FOLLOWER_SPRITES * 2;
}

int mmo_follower_gfx(int species, int form, int gender, int shiny)
{
    int at, run, off;

    if (species < 1 || species > MMO_FOLLOWER_SPECIES) {
        return -1;
    }

    at = species - 1;
    off = kFollowerOffset[at];
    run = kFollowerRun[at];

    if (kFollowerFemale[at]) {
        /* Two sprites, and the second is the female coat. A form is not a
         * thing these twelve have, so it is not consulted, which is
         * HeartGold's own branch and not a simplification of it. */
        if (gender == MMO_FOLLOWER_GENDER_FEMALE) {
            off += 1;
        }
    } else if (run > 1) {
        /* One sprite per form. Out of range clamps to the ordinary one, the
         * way OverworldModelLookupFormCount's caller does. */
        if (form < 0 || form >= run) {
            form = 0;
        }
        off += form;
    }

    /* The run is what the tables say it is, so this cannot fire on generated
     * data; it is here because the tables are generated and the arithmetic is
     * not, and a band overrun would otherwise be a graphics id belonging to
     * somebody else's package. */
    if (off < 0 || off >= MMO_FOLLOWER_SPRITES) {
        return -1;
    }

    if (shiny) {
        off += MMO_FOLLOWER_SPRITES;
    }
    return MMO_FOLLOWER_GFX_BASE + off;
}
