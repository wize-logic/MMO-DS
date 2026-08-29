/* Attaching to the session-state page, and deciding what of it
 * is worth putting in front of a player. view_status.h says why it is separate
 * from the drawing. */

#include "view_status.h"

#include <stdio.h>
#include <string.h>

void view_status_init(struct view_status *st)
{
    memset(st, 0, sizeof *st);
    st->page_mem.fd = -1;
}

void view_status_attach(struct view_status *st, const char *channel)
{
    char name[192];
    const struct openmmo_status_shm *p = NULL;

    if (st == NULL || st->page != NULL || channel == NULL) return;
    snprintf(name, sizeof name, "%s%s", channel, OPENMMO_STATUS_SUFFIX);

    if (mmo_shm_attach(&st->page_mem, name, sizeof *p, 0) != 0)
        return;
    p = (const struct openmmo_status_shm *)st->page_mem.addr;

    /*
     * The game zeroes the page and writes the magic last, so a page caught mid-setup is
     * dropped and tried again next frame rather than read as a session that is somehow
     * Disconnected with no words.
     */
    if (p->magic != OPENMMO_STATUS_MAGIC) {
        view_status_close(st);
        return;
    }
    if (p->version != OPENMMO_STATUS_VERSION) {
        fprintf(stderr, "openmmo-view: status page '%s' is version %u, this "
                        "window reads %u; the session goes uncaptioned\n",
                name, (unsigned)p->version, (unsigned)OPENMMO_STATUS_VERSION);
        view_status_close(st);
        return;
    }
    st->page = p;
}

void view_status_close(struct view_status *st)
{
    if (st == NULL) return;
    mmo_shm_close(&st->page_mem);
    st->page = NULL;
}

int view_status_poll(struct view_status *st)
{
    uint32_t state = 0, flags = 0, gen = 0;
    char reason[OPENMMO_STATUS_REASON];

    if (st == NULL || st->page == NULL) return 0;
    if (!openmmo_status_read(st->page, &state, &flags, &gen, reason)) return 0;
    if (st->any && gen == st->gen) return 0;
    st->any = 1;
    st->gen = gen;
    st->state = state;
    st->flags = flags;
    snprintf(st->reason, sizeof st->reason, "%s", reason);
    return 1;
}

int view_status_banner(const struct view_status *st, unsigned ms_since_change,
                       char *line, size_t line_cap,
                       char *detail, size_t detail_cap, uint32_t *colour)
{
    if (st == NULL || !st->any) return 0;
    if (!(st->flags & OPENMMO_STATUS_F_SESSION)) return 0;

    /*
     * A session that is over stays on screen: it is the one thing the player has to be told
     * and there is nothing else happening.
     */
    if (!openmmo_status_over(st->state, st->flags) &&
        (st->state == OPENMMO_ST_IN_GAME ||
         st->state == OPENMMO_ST_AUTHED ||
         st->state == OPENMMO_ST_REQUESTING_GAME ||
         st->state == OPENMMO_ST_JOINING_GAME) &&
        ms_since_change >= OPENMMO_VIEW_STATUS_SETTLED_MS)
        return 0;

    if (line != NULL)
        snprintf(line, line_cap, "%s",
                 openmmo_status_caption(st->state, st->flags));
    if (detail != NULL)
        snprintf(detail, detail_cap, "%s", st->reason);
    if (colour != NULL)
        *colour = openmmo_status_colour(st->state);
    return 1;
}
