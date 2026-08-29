/* The connection, as ten rows. See netwin.h. */
#include "netwin.h"

#include <string.h>

static void app_set(mmo_netwin_app *out, int i, const char *s)
{
    size_t n = 0;

    if (out == NULL || i < 0 || i >= MMO_NETWIN_APP_ROWS)
        return;
    if (s == NULL)
        s = "";
    while (s[n] != '\0' && n < MMO_NETWIN_APP_COLS) {
        out->line[i][n] = s[n];
        n++;
    }
    out->line[i][n] = '\0';
}

void mmo_netwin_format_status(const char *status, char *dst, size_t cap)
{
    size_t n = 0;

    if (dst == NULL || cap == 0)
        return;
    dst[0] = '\0';
    if (status == NULL)
        return;
    while (status[n] != '\0' && n < cap - 1) {
        dst[n] = status[n];
        n++;
    }
    dst[n] = '\0';
}

void mmo_netwin_format_latency(int latency_ms, char *dst, size_t cap)
{
    char digits[10];
    int d = 0;
    int n;
    size_t i = 0;

    if (dst == NULL || cap == 0)
        return;
    dst[0] = '\0';
    if (latency_ms < 0) {
        const char *none = "NONE";

        while (none[i] != '\0' && i + 1 < cap) {
            dst[i] = none[i];
            i++;
        }
        dst[i] = '\0';
        return;
    }
    n = latency_ms;
    if (n == 0)
        digits[d++] = '0';
    while (n > 0 && d < 10) {
        digits[d++] = (char)('0' + (n % 10));
        n /= 10;
    }
    while (d > 0 && i + 1 < cap)
        dst[i++] = digits[--d];
    if (i + 3 < cap) {
        dst[i++] = ' ';
        dst[i++] = 'M';
        dst[i++] = 'S';
    }
    dst[i] = '\0';
}

void mmo_netwin_format_reason(const char *reason, int row, char *dst,
                              size_t cap)
{
    size_t wrap;
    size_t i = 0;
    size_t n = 0;

    if (dst == NULL || cap == 0)
        return;
    dst[0] = '\0';
    if (reason == NULL || row < 0 || row >= MMO_NETWIN_REASON_ROWS)
        return;
    wrap = cap - 1;
    while (reason[i] != '\0' && i < wrap * (size_t)row)
        i++;
    if (reason[i] == '\0')
        return;
    while (reason[i] != '\0' && n + 1 < cap)
        dst[n++] = reason[i++];
    dst[n] = '\0';
}

void mmo_netwin_render(const mmo_netwin_view *view, mmo_netwin_app *out)
{
    char row[MMO_NETWIN_APP_COLS + 1];
    int i;
    const char *status;
    const char *reason;
    const char *battle;
    int latency;

    if (out == NULL)
        return;
    memset(out, 0, sizeof *out);
    app_set(out, 0, "NET");
    for (i = 1; i < MMO_NETWIN_APP_ROWS - 1; i++)
        app_set(out, i, "");
    app_set(out, MMO_NETWIN_APP_ROWS - 1, "NONE");

    if (view == NULL)
        return;
    status = view->status;
    reason = view->reason;
    battle = view->battle;
    latency = view->latency_ms;

    mmo_netwin_format_status(status, row, sizeof row);
    app_set(out, 1, row);
    mmo_netwin_format_status(battle, row, sizeof row);
    app_set(out, 2, row);
    for (i = 0; i < MMO_NETWIN_REASON_ROWS; i++) {
        mmo_netwin_format_reason(reason, i, row, sizeof row);
        app_set(out, 3 + i, row);
    }
    mmo_netwin_format_latency(latency, row, sizeof row);
    app_set(out, MMO_NETWIN_APP_ROWS - 1, row);
}
