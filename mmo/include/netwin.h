#ifndef OPENMMO_NETWIN_H
#define OPENMMO_NETWIN_H
/* The connection, as ten rows. */

#include <stddef.h>

#define MMO_NETWIN_REASON_ROWS 6
#define MMO_NETWIN_APP_ROWS    10
#define MMO_NETWIN_APP_COLS    24
#if 1 + 1 + 1 + MMO_NETWIN_REASON_ROWS + 1 != MMO_NETWIN_APP_ROWS
#error net app is title + status + battle + reason + footer
#endif

typedef struct {
    const char *status;   /* player caption, e.g. "ONLINE" */
    int         latency_ms; /* < 0 = none */
    const char *reason;   /* last disconnect; empty while connected */
    const char *battle;   /* hold line, or empty */
} mmo_netwin_view;

typedef struct {
    char line[MMO_NETWIN_APP_ROWS][MMO_NETWIN_APP_COLS + 1];
} mmo_netwin_app;

/* Format the status caption. Empty in is an empty row. */
void mmo_netwin_format_status(const char *status, char *dst, size_t cap);

/* Footer: "n MS" when latency_ms >= 0, else "none". */
void mmo_netwin_format_latency(int latency_ms, char *dst, size_t cap);

/* One wrapped row of the last-disconnect sentence. row is 0-based. */
void mmo_netwin_format_reason(const char *reason, int row, char *dst,
                              size_t cap);

/* Ten-row layout: title net, status, battle, six reason rows,
 * footer the latency or none. */
void mmo_netwin_render(const mmo_netwin_view *view, mmo_netwin_app *out);

#endif /* OPENMMO_NETWIN_H */
