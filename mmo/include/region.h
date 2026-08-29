/*
 * Which of the wire's regions a character may be made in, and which of them this
 * client can draw.
 */
#ifndef OPENMMO_REGION_H
#define OPENMMO_REGION_H

#include <stddef.h>

/* Region ids as they appear on the wire. */
enum {
    MMO_REGION_KANTO  = 0,
    MMO_REGION_HOENN  = 1,
    MMO_REGION_UNOVA  = 2,
    MMO_REGION_SINNOH = 3,
    MMO_REGION_JOHTO  = 4,
    /* The one region with no cartridge behind it: official serves it from map
     * files of its own. Nothing is built for it here yet, so it is listed for
     * completeness and never offered at creation, the official client does not offer it
     * either. */
    MMO_REGION_CUSTOM = 10
};

typedef struct {
    int         id;         /* the region id as it appears on the wire */
    const char *name;       /* what a player is shown */
    int         offered;    /* appears on a create-a-character region list */
    int         selectable; /* a character may actually be created here */
    int         drawable;   /* this client can render a map in this region */
    /* Why the row is greyed, as a sentence a player can read. NULL exactly when
     * the region is both selectable and drawable. */
    const char *reason;
} mmo_region;

/* The roster, in the order a region list draws it. */
int               mmo_region_count(void);
const mmo_region *mmo_region_at(int index);

/* NULL for an id no roster row claims, an unknown region is not an error here,
 * it is a thing to report. */
const mmo_region *mmo_region_by_id(int id);

const char *mmo_region_name(int id);     /* "region 7" for an unknown id */
int         mmo_region_is_selectable(int id);
int         mmo_region_is_drawable(int id);

/* The region a fresh character is made in when nothing else is chosen: the one
 * selectable row. Traps at build time if that stops being exactly one. */
int mmo_region_default(void);

/* Why this client cannot draw `id`, or NULL if it can. */
const char *mmo_region_undrawable_reason(int id);

/* The whole explanation a player is shown when their character is somewhere
 * this build has no maps for: the reason, then which region can be played. The
 * length it would have written (snprintf's answer), or -1 when `id` is drawable
 * after all and there is nothing to explain. */
int mmo_region_explain_undrawable(int id, char *buf, size_t n);

#endif /* OPENMMO_REGION_H */
