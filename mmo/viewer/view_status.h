/* The window's end of the session-state page. */

#ifndef OPENMMO_VIEW_STATUS_H
#define OPENMMO_VIEW_STATUS_H

#include <stddef.h>
#include <stdint.h>

#include "platform.h"
#include "status_channel.h"

#ifdef __cplusplus
extern "C" {
#endif

struct view_status {
    const struct openmmo_status_shm *page;
    mmo_shm  page_mem;
    int      any;              /* a state has been read at least once */
    uint32_t state, flags, gen;
    char     reason[OPENMMO_STATUS_REASON];
};

/* Nothing attached, nothing read. */
void view_status_init(struct view_status *st);

/*
 * One attempt to attach, named after the frame channel. Cheap and safe to call every frame
 * until it succeeds: the game may not have started its session yet when the window opens, and
 * a session that starts a minute in is still worth reporting.
 */
void view_status_attach(struct view_status *st, const char *channel);

void view_status_close(struct view_status *st);

/* Re-read the page. Returns nonzero when the state moved. */
int view_status_poll(struct view_status *st);

/* How long a state that arrived somewhere good stays on screen before the
 * window gets out of the way. Long enough to read four words. */
#define OPENMMO_VIEW_STATUS_SETTLED_MS 2500u

/*
 * What to draw over the picture, if anything. Returns 0 when the window should stay out of the
 * way, no session in this run, or a session that got where it was going and said so long
 * enough ago, which is every frame of a normal game and is not news.
 */
int view_status_banner(const struct view_status *st, unsigned ms_since_change,
                       char *line, size_t line_cap,
                       char *detail, size_t detail_cap, uint32_t *colour);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_VIEW_STATUS_H */
