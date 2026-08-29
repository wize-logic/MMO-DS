/* Who is on this map, as names. See mapwin.h. */
#include "mapwin.h"

#include <string.h>

void mmo_mapwin_reset(mmo_mapwin *w)
{
    if (w == NULL)
        return;
    w->scroll = 0;
}

void mmo_mapwin_format(const mmo_mapwin_line *in, char *dst, size_t cap)
{
    const char *name;
    size_t n = 0;

    if (dst == NULL || cap == 0)
        return;
    dst[0] = '\0';
    if (in == NULL)
        return;
    name = in->name != NULL ? in->name : "";
    if (name[0] == '\0')
        name = "?";
    while (name[n] != '\0' && n < cap - 1) {
        dst[n] = name[n];
        n++;
    }
    dst[n] = '\0';
}

void mmo_mapwin_scroll(mmo_mapwin *w, int delta, int count)
{
    int max_scroll;

    if (w == NULL)
        return;
    if (count < 0)
        count = 0;
    max_scroll = count - MMO_MAPWIN_ROWS;
    if (max_scroll < 0)
        max_scroll = 0;
    w->scroll += delta;
    if (w->scroll < 0)
        w->scroll = 0;
    if (w->scroll > max_scroll)
        w->scroll = max_scroll;
}

static void app_set(mmo_mapwin_app *out, int i, const char *s)
{
    size_t n = 0;

    if (out == NULL || i < 0 || i >= MMO_MAPWIN_APP_ROWS)
        return;
    if (s == NULL)
        s = "";
    while (s[n] != '\0' && n < MMO_MAPWIN_APP_COLS) {
        out->line[i][n] = s[n];
        n++;
    }
    out->line[i][n] = '\0';
}

static void app_count(mmo_mapwin_app *out, int count)
{
    char buf[MMO_MAPWIN_APP_COLS + 1];
    int i = 0;
    int n;
    char digits[10];
    int d = 0;

    if (count <= 0) {
        app_set(out, MMO_MAPWIN_APP_ROWS - 1, "NONE");
        return;
    }
    n = count;
    while (n > 0 && d < 10) {
        digits[d++] = (char)('0' + (n % 10));
        n /= 10;
    }
    while (d > 0 && i < MMO_MAPWIN_APP_COLS)
        buf[i++] = digits[--d];
    buf[i] = '\0';
    app_set(out, MMO_MAPWIN_APP_ROWS - 1, buf);
}

void mmo_mapwin_render(const mmo_mapwin *w, const mmo_mapwin_line *list,
                       int count, mmo_mapwin_app *out)
{
    char row[MMO_MAPWIN_APP_COLS + 1];
    int i;
    int start;
    int shown;

    if (out == NULL)
        return;
    memset(out, 0, sizeof *out);
    app_set(out, 0, "HERE");
    for (i = 1; i <= MMO_MAPWIN_ROWS; i++)
        app_set(out, i, "");
    app_count(out, 0);

    if (w == NULL)
        return;
    if (count < 0)
        count = 0;
    start = w->scroll;
    if (start < 0)
        start = 0;
    if (count == 0 || list == NULL) {
        app_count(out, 0);
        return;
    }
    if (start > count)
        start = count;
    shown = count - start;
    if (shown > MMO_MAPWIN_ROWS)
        shown = MMO_MAPWIN_ROWS;
    for (i = 0; i < shown; i++) {
        mmo_mapwin_format(&list[start + i], row, sizeof row);
        app_set(out, 1 + i, row);
    }
    app_count(out, count);
}
