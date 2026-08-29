/* MOTD and who is on. See guildwin.h. */
#include "guildwin.h"

#include <string.h>

void mmo_guildwin_reset(mmo_guildwin *w)
{
    if (w == NULL)
        return;
    w->scroll = 0;
}

void mmo_guildwin_format_title(const mmo_guildwin_view *in, char *dst,
                               size_t cap)
{
    const char *name;
    const char *tag;
    size_t n = 0;
    size_t name_max;
    size_t tag_n = 0;

    if (dst == NULL || cap == 0)
        return;
    dst[0] = '\0';
    if (in == NULL || !in->in_guild) {
        const char *guild = "GUILD";

        while (guild[n] != '\0' && n + 1 < cap) {
            dst[n] = guild[n];
            n++;
        }
        dst[n] = '\0';
        return;
    }
    name = in->name != NULL ? in->name : "";
    if (name[0] == '\0')
        name = "GUILD";
    tag = in->tag != NULL ? in->tag : "";
    while (tag[tag_n] != '\0')
        tag_n++;
    name_max = cap - 1;
    if (tag[0] != '\0' && cap > tag_n + 2)
        name_max = cap - (tag_n + 2);
    while (name[n] != '\0' && n < name_max) {
        dst[n] = name[n];
        n++;
    }
    if (tag[0] != '\0' && cap > tag_n + 1) {
        while (n + tag_n + 1 < cap)
            dst[n++] = ' ';
        memcpy(dst + n, tag, tag_n);
        n += tag_n;
    }
    dst[n] = '\0';
}

void mmo_guildwin_format_motd(const char *motd, int row, char *dst,
                              size_t cap)
{
    size_t wrap;
    size_t i = 0;
    size_t n = 0;

    if (dst == NULL || cap == 0)
        return;
    dst[0] = '\0';
    if (motd == NULL || row < 0 || row >= MMO_GUILDWIN_MOTD_ROWS)
        return;
    wrap = cap - 1;
    while (motd[i] != '\0' && i < wrap * (size_t)row)
        i++;
    if (motd[i] == '\0')
        return;
    while (motd[i] != '\0' && n + 1 < cap)
        dst[n++] = motd[i++];
    dst[n] = '\0';
}

void mmo_guildwin_format_member(const mmo_guildwin_line *in, char *dst,
                                size_t cap)
{
    const char *name;
    size_t n = 0;
    size_t name_max;

    if (dst == NULL || cap == 0)
        return;
    dst[0] = '\0';
    if (in == NULL)
        return;
    name = in->name != NULL ? in->name : "";
    if (name[0] == '\0')
        name = "?";
    name_max = cap - 1;
    if (in->online && cap > 4)
        name_max = cap - 4;
    while (name[n] != '\0' && n < name_max) {
        dst[n] = name[n];
        n++;
    }
    if (in->online && cap > 3) {
        while (n + 3 < cap)
            dst[n++] = ' ';
        dst[n++] = 'O';
        dst[n++] = 'N';
    }
    dst[n] = '\0';
}

int mmo_guildwin_online_count(const mmo_guildwin_line *list, int count)
{
    int i;
    int n = 0;

    if (list == NULL || count <= 0)
        return 0;
    for (i = 0; i < count; i++) {
        if (list[i].online)
            n++;
    }
    return n;
}

void mmo_guildwin_scroll(mmo_guildwin *w, int delta, int online_count)
{
    int max_scroll;

    if (w == NULL)
        return;
    if (online_count < 0)
        online_count = 0;
    max_scroll = online_count - MMO_GUILDWIN_ONLINE_ROWS;
    if (max_scroll < 0)
        max_scroll = 0;
    w->scroll += delta;
    if (w->scroll < 0)
        w->scroll = 0;
    if (w->scroll > max_scroll)
        w->scroll = max_scroll;
}

static void app_set(mmo_guildwin_app *out, int i, const char *s)
{
    size_t n = 0;

    if (out == NULL || i < 0 || i >= MMO_GUILDWIN_APP_ROWS)
        return;
    if (s == NULL)
        s = "";
    while (s[n] != '\0' && n < MMO_GUILDWIN_APP_COLS) {
        out->line[i][n] = s[n];
        n++;
    }
    out->line[i][n] = '\0';
}

static void app_num(char *buf, size_t cap, int n)
{
    char digits[10];
    int d = 0;
    int i = 0;
    int v = n;

    if (buf == NULL || cap == 0)
        return;
    if (v < 0)
        v = 0;
    if (v == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (v > 0 && d < 10) {
        digits[d++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (d > 0 && i + 1 < (int)cap)
        buf[i++] = digits[--d];
    buf[i] = '\0';
}

static void app_footer(mmo_guildwin_app *out, const mmo_guildwin_view *view,
                       int online)
{
    char buf[MMO_GUILDWIN_APP_COLS + 1];
    char num[12];
    int i = 0;

    if (view == NULL || !view->in_guild) {
        app_set(out, MMO_GUILDWIN_APP_ROWS - 1, "NONE");
        return;
    }
    if (view->log_valid) {
        app_num(num, sizeof num, view->log_count);
        buf[i++] = 'L';
        buf[i++] = 'O';
        buf[i++] = 'G';
        buf[i++] = ' ';
        while (num[i - 4] != '\0' && i < MMO_GUILDWIN_APP_COLS) {
            buf[i] = num[i - 4];
            i++;
        }
        buf[i] = '\0';
        app_set(out, MMO_GUILDWIN_APP_ROWS - 1, buf);
        return;
    }
    app_num(buf, sizeof buf, online);
    app_set(out, MMO_GUILDWIN_APP_ROWS - 1, buf);
}

void mmo_guildwin_render(const mmo_guildwin *w,
                         const mmo_guildwin_view *view,
                         const mmo_guildwin_line *list, int count,
                         mmo_guildwin_app *out)
{
    char row[MMO_GUILDWIN_APP_COLS + 1];
    int i;
    int online;
    int start;
    int shown;
    int seen;
    int first_online;

    if (out == NULL)
        return;
    memset(out, 0, sizeof *out);
    app_set(out, 0, "GUILD");
    for (i = 1; i < MMO_GUILDWIN_APP_ROWS - 1; i++)
        app_set(out, i, "");
    app_set(out, MMO_GUILDWIN_APP_ROWS - 1, "NONE");

    if (w == NULL)
        return;
    if (count < 0)
        count = 0;
    online = mmo_guildwin_online_count(list, count);
    start = w->scroll;
    if (start < 0)
        start = 0;

    mmo_guildwin_format_title(view, row, sizeof row);
    app_set(out, 0, row);

    if (view != NULL && view->in_guild && view->motd != NULL) {
        for (i = 0; i < MMO_GUILDWIN_MOTD_ROWS; i++) {
            mmo_guildwin_format_motd(view->motd, i, row, sizeof row);
            app_set(out, 1 + i, row);
        }
    }

    first_online = 1 + MMO_GUILDWIN_MOTD_ROWS;
    shown = 0;
    seen = 0;
    if (list != NULL && view != NULL && view->in_guild) {
        for (i = 0; i < count && shown < MMO_GUILDWIN_ONLINE_ROWS; i++) {
            if (!list[i].online)
                continue;
            if (seen++ < start)
                continue;
            mmo_guildwin_format_member(&list[i], row, sizeof row);
            app_set(out, first_online + shown, row);
            shown++;
        }
    }
    app_footer(out, view, online);
}
