/* Which picture walks behind a player, checked without a ROM. */
#include <stdio.h>
#include <string.h>

#include "follower.h"

/* The generated table, so the tiling check can size itself off the same numbers
 * the code under test reads. */
#include "../src/follower_index.gen.h"

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

#define MALE   0
#define FEMALE MMO_FOLLOWER_GENDER_FEMALE

int follower_tests_run(void);

int follower_tests_run(void)
{
    static unsigned char hit[MMO_FOLLOWER_SPRITES];
    int base = mmo_follower_gfx_base();
    int species, form, gender;
    int reached = 0, out_of_band = 0, refused = 0;
    int i;

    printf("which picture walks behind a player:\n");

    failures = 0;
    memset(hit, 0, sizeof hit);

    /* Every species, every form the table gives it, and both genders. 32 forms
     * is past anything (Unown has the most at 28) so the clamp is exercised too. */
    for (species = 1; species <= MMO_FOLLOWER_SPECIES; species++) {
        for (form = 0; form < 32; form++) {
            for (gender = MALE; gender <= FEMALE; gender++) {
                int gfx = mmo_follower_gfx(species, form, gender, 0);

                if (gfx < 0) {
                    refused++;
                    continue;
                }
                if (gfx < base || gfx >= base + MMO_FOLLOWER_SPRITES) {
                    out_of_band++;
                    continue;
                }
                hit[gfx - base] = 1;
            }
        }
    }
    for (i = 0; i < MMO_FOLLOWER_SPRITES; i++) {
        reached += hit[i];
    }

    CHECK(refused == 0, "every species in the table answers");
    CHECK(out_of_band == 0, "no answer lands outside the follower band");
    CHECK(reached == MMO_FOLLOWER_SPRITES,
        "every one of the band's sprites is reachable, and nothing past it");

    /* The four branches, by hand. Bulbasaur is the first of the band and owns
     * one sprite; Venusaur owns two and the second is the female coat (offset 2
     * and 3, so the run before it is Bulbasaur and Ivysaur); Arceus owns
     * eighteen and is the last species in the table. */
    CHECK(mmo_follower_gfx(1, 0, MALE, 0) == base,
        "Bulbasaur is the first id of the band");
    CHECK(mmo_follower_gfx(1, 0, FEMALE, 0) == base,
        "a species with one sprite ignores gender");
    CHECK(mmo_follower_gfx(3, 0, MALE, 0) + 1
            == mmo_follower_gfx(3, 0, FEMALE, 0),
        "a female coat is the sprite after the male one");
    CHECK(mmo_follower_gfx(3, 5, FEMALE, 0)
            == mmo_follower_gfx(3, 0, FEMALE, 0),
        "a form is not consulted for a species that has a female coat");

    {
        int arceus = MMO_FOLLOWER_SPECIES;
        int first = mmo_follower_gfx(arceus, 0, MALE, 0);

        CHECK(mmo_follower_gfx(arceus, 17, MALE, 0) == first + 17,
            "a form picks the id that far into the species' run");
        CHECK(mmo_follower_gfx(arceus, 17, MALE, 0)
                == base + MMO_FOLLOWER_SPRITES - 1,
            "and the last form of the last species is the band's last id");
        CHECK(mmo_follower_gfx(arceus, 18, MALE, 0) == first
                && mmo_follower_gfx(arceus, 99, MALE, 0) == first,
            "a form past the run clamps to the ordinary one");
        CHECK(mmo_follower_gfx(arceus, -1, MALE, 0) == first,
            "and so does a negative one");
    }

    /* The shiny band is the normal one shifted by its own length, which is what
     * lets a caller turn one into the other by adding a constant. */
    CHECK(mmo_follower_gfx(1, 0, MALE, 1)
            == mmo_follower_gfx(1, 0, MALE, 0) + MMO_FOLLOWER_SPRITES,
        "a shiny follower is its own id plus the band length");
    CHECK(mmo_follower_gfx_count() == MMO_FOLLOWER_SPRITES * 2,
        "and the two bands together are what the package fills");

    /* The refusals that are ours. HeartGold answers an unknown species with
     * Bulbasaur's sprite; a server-authoritative client cannot use that. */
    CHECK(mmo_follower_gfx(0, 0, MALE, 0) == -1,
        "species 0 is refused rather than drawn as Bulbasaur");
    CHECK(mmo_follower_gfx(MMO_FOLLOWER_SPECIES + 1, 0, MALE, 0) == -1,
        "a species past the table is refused, not clamped");
    CHECK(mmo_follower_gfx(-7, 0, MALE, 0) == -1,
        "and so is a negative one");
    CHECK(mmo_follower_species_max() == MMO_FOLLOWER_SPECIES,
        "the species ceiling is reportable without the generated header");

    if (failures) {
        printf("follower: %d check(s) FAILED\n", failures);
    } else {
        printf("follower: all checks passed\n");
    }
    return failures;
}
