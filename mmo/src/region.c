/* The region roster. */
#include <stddef.h>
#include <stdio.h>

#include "region.h"

/*
 * Order is the official client's, and it is generation order rather than id order: Kanto, Johto, Hoenn,
 * Sinnoh, Unova.
 */
/* Each reason is one sentence a player reads, and it has to survive being put
 * in front of "Only Sinnoh can be played here." inside a failure message the
 * session carries in 128 bytes, so they are short on purpose, and the suite
 * holds every row to that width rather than trusting the eye. */
static const mmo_region roster[] = {
    { MMO_REGION_KANTO, "Kanto", 1, 0, 0,
      "Kanto's maps are a Game Boy Advance game's, which this client cannot read." },
    { MMO_REGION_JOHTO, "Johto", 1, 0, 0,
      "Johto is not built yet: neither side of the connection has its maps." },
    { MMO_REGION_HOENN, "Hoenn", 1, 0, 0,
      "Hoenn's maps are a Game Boy Advance game's, and no DS game covers it." },
    { MMO_REGION_SINNOH, "Sinnoh", 1, 1, 1, NULL },
    { MMO_REGION_UNOVA, "Unova", 1, 0, 0,
      "Unova is not built yet: neither side of the connection has its maps." },
    { MMO_REGION_CUSTOM, "Custom", 0, 0, 0,
      "No cartridge holds this region, and no content of our own is built yet." },
};

#define ROSTER_COUNT ((int)(sizeof roster / sizeof roster[0]))

int mmo_region_count(void)
{
    return ROSTER_COUNT;
}

const mmo_region *mmo_region_at(int index)
{
    if (index < 0 || index >= ROSTER_COUNT)
        return NULL;
    return &roster[index];
}

const mmo_region *mmo_region_by_id(int id)
{
    for (int i = 0; i < ROSTER_COUNT; i++)
        if (roster[i].id == id)
            return &roster[i];
    return NULL;
}

const char *mmo_region_name(int id)
{
    const mmo_region *r = mmo_region_by_id(id);
    if (r)
        return r->name;
    /* An id no row claims is a thing to report, not to crash on: the server may
     * name a world this build has never heard of. */
    return "an unknown region";
}

int mmo_region_is_selectable(int id)
{
    const mmo_region *r = mmo_region_by_id(id);
    return r != NULL && r->selectable;
}

int mmo_region_is_drawable(int id)
{
    const mmo_region *r = mmo_region_by_id(id);
    return r != NULL && r->drawable;
}

int mmo_region_default(void)
{
    for (int i = 0; i < ROSTER_COUNT; i++)
        if (roster[i].selectable)
            return roster[i].id;
    return MMO_REGION_SINNOH;
}

const char *mmo_region_undrawable_reason(int id)
{
    const mmo_region *r = mmo_region_by_id(id);
    if (!r)
        return "This client has never heard of that region.";
    if (r->drawable)
        return NULL;
    return r->reason;
}

int mmo_region_explain_undrawable(int id, char *buf, size_t n)
{
    const char *why = mmo_region_undrawable_reason(id);
    if (!buf || n == 0)
        return -1;
    buf[0] = '\0';
    if (!why)
        return -1;
    return snprintf(buf, n, "%s Only %s can be played here.", why,
                    mmo_region_name(mmo_region_default()));
}
