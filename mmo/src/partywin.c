/* The party, as six rows of name and status. See partywin.h. */
#include "partywin.h"

#include <string.h>

static void app_set(mmo_partywin_app *out, int i, const char *s)
{
    size_t n = 0;

    if (out == NULL || i < 0 || i >= MMO_PARTYWIN_APP_ROWS)
        return;
    if (s == NULL)
        s = "";
    while (s[n] != '\0' && n < MMO_PARTYWIN_APP_COLS) {
        out->line[i][n] = s[n];
        n++;
    }
    out->line[i][n] = '\0';
}

static size_t put_uint(char *dst, size_t cap, size_t n, int v)
{
    char digits[10];
    int d = 0;
    unsigned u;

    if (v < 0)
        v = 0;
    u = (unsigned)v;
    if (u == 0)
        digits[d++] = '0';
    while (u > 0 && d < 10) {
        digits[d++] = (char)('0' + (u % 10));
        u /= 10;
    }
    while (d > 0 && n + 1 < cap)
        dst[n++] = digits[--d];
    return n;
}

void mmo_partywin_format(const mmo_partywin_line *in, char *dst,
                         size_t cap)
{
    const char *name;
    char right[16];
    size_t n = 0;
    size_t r = 0;
    size_t name_max;

    if (dst == NULL || cap == 0)
        return;
    dst[0] = '\0';
    if (in == NULL)
        return;
    name = in->name != NULL ? in->name : "";
    if (name[0] == '\0')
        name = "?";
    if (in->egg) {
        while (name[n] != '\0' && n + 1 < cap) {
            dst[n] = name[n];
            n++;
        }
        dst[n] = '\0';
        return;
    }

    right[r++] = 'L';
    right[r++] = 'v';
    r = put_uint(right, sizeof right, r, in->level);
    if (r + 2 < sizeof right) {
        right[r++] = ' ';
        r = put_uint(right, sizeof right, r, in->hp);
    }
    right[r] = '\0';

    name_max = cap - 1;
    if (r > 0 && cap > r + 2)
        name_max = cap - 1 - r;
    while (name[n] != '\0' && n < name_max) {
        dst[n] = name[n];
        n++;
    }
    if (r > 0 && cap > r + 1) {
        while (n + r + 1 < cap)
            dst[n++] = ' ';
        memcpy(dst + n, right, r);
        n += r;
    }
    dst[n] = '\0';
}

static void app_count(mmo_partywin_app *out, int count)
{
    char buf[MMO_PARTYWIN_APP_COLS + 1];
    int i = 0;
    int n;
    char digits[10];
    int d = 0;

    if (count <= 0) {
        app_set(out, MMO_PARTYWIN_APP_ROWS - 1, "NONE");
        return;
    }
    n = count;
    while (n > 0 && d < 10) {
        digits[d++] = (char)('0' + (n % 10));
        n /= 10;
    }
    while (d > 0 && i < MMO_PARTYWIN_APP_COLS)
        buf[i++] = digits[--d];
    buf[i] = '\0';
    app_set(out, MMO_PARTYWIN_APP_ROWS - 1, buf);
}

void mmo_partywin_render(const mmo_partywin_line *list, int count,
                         mmo_partywin_app *out)
{
    char row[MMO_PARTYWIN_APP_COLS + 1];
    int i;
    int shown;

    if (out == NULL)
        return;
    memset(out, 0, sizeof *out);
    app_set(out, 0, "PARTY");
    for (i = 1; i <= MMO_PARTYWIN_ROWS; i++)
        app_set(out, i, "");
    app_count(out, 0);

    if (count < 0)
        count = 0;
    if (count == 0 || list == NULL) {
        app_count(out, 0);
        return;
    }
    shown = count;
    if (shown > MMO_PARTYWIN_ROWS)
        shown = MMO_PARTYWIN_ROWS;
    for (i = 0; i < shown; i++) {
        mmo_partywin_format(&list[i], row, sizeof row);
        app_set(out, 1 + i, row);
    }
    app_count(out, count);
}
