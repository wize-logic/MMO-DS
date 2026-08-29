/*
 * The region roster: what a player is offered, and what they are told about
 * everything else.
 */
#include <stdio.h>
#include <string.h>

#include "client.h"
#include "region.h"

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

/* the official client's `xq1.kR`: its five offered regions in its own order, then the region
 * no cartridge serves. */
static const int OFFICIAL_ROSTER[] = { 0, 4, 1, 3, 2, 10 };
#define OFFICIAL_ROSTER_N ((int)(sizeof OFFICIAL_ROSTER / sizeof OFFICIAL_ROSTER[0]))

/* How much room a failure message has when it reaches a player. */
#define MESSAGE_ROOM ((int)sizeof(((openmmo_event *)0)->message))

int region_tests_run(void);

int region_tests_run(void)
{
    printf("region roster, in the official client's own order:\n");
    CHECK(mmo_region_count() == OFFICIAL_ROSTER_N,
        "the roster holds the official client's five regions plus the cartridge-free one");

    int order_ok = mmo_region_count() == OFFICIAL_ROSTER_N;
    for (int i = 0; order_ok && i < OFFICIAL_ROSTER_N; i++) {
        const mmo_region *r = mmo_region_at(i);
        if (!r || r->id != OFFICIAL_ROSTER[i])
            order_ok = 0;
    }
    CHECK(order_ok, "the order is Kanto, Johto, Hoenn, Sinnoh, Unova, then 10");

    /* Region 10 is in the official client's teleport roster and not in the list it offers at
     * creation; ours draws the same line. */
    const mmo_region *custom = mmo_region_by_id(MMO_REGION_CUSTOM);
    CHECK(custom && !custom->offered,
        "the cartridge-free region is not offered at creation");
    int offered = 0;
    for (int i = 0; i < mmo_region_count(); i++)
        offered += mmo_region_at(i)->offered;
    CHECK(offered == 5, "five regions are offered, which is the official client's CH0");

    printf("one world is playable and it is Sinnoh:\n");
    int selectable = 0, drawable = 0;
    for (int i = 0; i < mmo_region_count(); i++) {
        selectable += mmo_region_at(i)->selectable;
        drawable += mmo_region_at(i)->drawable;
    }
    CHECK(selectable == 1, "exactly one region can be chosen");
    CHECK(drawable == 1, "exactly one region can be drawn");
    CHECK(mmo_region_default() == MMO_REGION_SINNOH,
        "a new character is made in Sinnoh");
    CHECK(mmo_region_is_selectable(MMO_REGION_SINNOH) &&
              mmo_region_is_drawable(MMO_REGION_SINNOH),
        "Sinnoh is both selectable and drawable");
    CHECK(!mmo_region_is_drawable(MMO_REGION_KANTO) &&
              !mmo_region_is_drawable(MMO_REGION_HOENN) &&
              !mmo_region_is_drawable(MMO_REGION_JOHTO) &&
              !mmo_region_is_drawable(MMO_REGION_UNOVA),
        "no other region claims a map this client does not have");

    printf("every greyed row says why, and the sentence reaches the player:\n");
    int reasons_ok = 1, fits = 1, offered_selectable = 1;
    for (int i = 0; i < mmo_region_count(); i++) {
        const mmo_region *r = mmo_region_at(i);
        if (r->selectable) {
            if (r->reason != NULL || !r->drawable || !r->offered)
                reasons_ok = 0;
            continue;
        }
        if (r->reason == NULL || r->reason[0] == '\0')
            reasons_ok = 0;
        if (r->selectable && !r->offered)
            offered_selectable = 0;
        char buf[512];
        int want = mmo_region_explain_undrawable(r->id, buf, sizeof buf);
        if (want < 0 || want >= MESSAGE_ROOM)
            fits = 0;
    }
    CHECK(reasons_ok,
        "a greyed region carries a reason and the playable one carries none");
    CHECK(offered_selectable, "nothing selectable is missing from the list");
    CHECK(fits, "every explanation fits the message a failed session carries");
    CHECK(mmo_region_undrawable_reason(MMO_REGION_SINNOH) == NULL,
        "the playable region has nothing to explain");

    printf("an id no row claims is reported, not guessed at:\n");
    CHECK(mmo_region_by_id(7) == NULL, "an unknown region has no row");
    CHECK(!mmo_region_is_selectable(7) && !mmo_region_is_drawable(7),
        "an unknown region is neither selectable nor drawable");
    CHECK(mmo_region_name(7) != NULL && mmo_region_name(7)[0] != '\0',
        "an unknown region still has something to call it");
    char buf[512];
    int want = mmo_region_explain_undrawable(7, buf, sizeof buf);
    CHECK(want > 0 && want < MESSAGE_ROOM && strstr(buf, "Sinnoh") != NULL,
        "an unknown region is explained the same way, and still fits");
    CHECK(mmo_region_explain_undrawable(MMO_REGION_SINNOH, buf, sizeof buf) < 0 &&
              buf[0] == '\0',
        "there is nothing to explain about the region that works");
    CHECK(mmo_region_at(-1) == NULL && mmo_region_at(mmo_region_count()) == NULL,
        "walking off either end of the roster gives no row");

    if (failures) {
        printf("region: %d check(s) FAILED\n", failures);
    } else {
        printf("region: all checks passed\n");
    }
    return failures;
}
