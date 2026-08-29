/* The chat window's brain. See chatwin.h. */
#include "chatwin.h"

#include <string.h>

void mmo_chatwin_reset(mmo_chatwin *w)
{
    if (w == NULL)
        return;
    w->mode = MMO_CHATWIN_HIDDEN;
    w->scroll = 0;
}

void mmo_chatwin_show(mmo_chatwin *w)
{
    if (w == NULL)
        return;
    if (w->mode == MMO_CHATWIN_HIDDEN)
        w->mode = MMO_CHATWIN_LOG;
}

void mmo_chatwin_hide(mmo_chatwin *w)
{
    if (w == NULL)
        return;
    w->mode = MMO_CHATWIN_HIDDEN;
    w->scroll = 0;
}

int mmo_chatwin_visible(const mmo_chatwin *w)
{
    return w != NULL && w->mode != MMO_CHATWIN_HIDDEN;
}

int mmo_chatwin_composing(const mmo_chatwin *w)
{
    return w != NULL && w->mode == MMO_CHATWIN_COMPOSE;
}

int mmo_chatwin_needs_entry(const mmo_chatwin *w)
{
    return mmo_chatwin_composing(w);
}

int mmo_chatwin_toggle_compose(mmo_chatwin *w)
{
    if (w == NULL || w->mode == MMO_CHATWIN_HIDDEN)
        return 0;
    if (w->mode == MMO_CHATWIN_LOG) {
        w->mode = MMO_CHATWIN_COMPOSE;
        w->scroll = 0;
        return 1;
    }
    w->mode = MMO_CHATWIN_LOG;
    return 1;
}

int mmo_chatwin_row_budget(const mmo_chatwin *w)
{
    if (w == NULL)
        return 0;
    if (w->mode == MMO_CHATWIN_COMPOSE)
        return MMO_CHATWIN_COMPOSE_ROWS;
    if (w->mode == MMO_CHATWIN_LOG)
        return MMO_CHATWIN_LOG_ROWS;
    return 0;
}

void mmo_chatwin_format(const mmo_chatwin_line *in, char *dst, size_t cap)
{
    const char *who;
    const char *text;
    size_t n = 0;

    if (dst == NULL || cap == 0)
        return;
    dst[0] = '\0';
    if (in == NULL)
        return;
    who = in->sender != NULL ? in->sender : "";
    text = in->text != NULL ? in->text : "";

    if (in->type == MMO_CHAT_SYSTEM || in->type == MMO_CHAT_NOTICE
        || who[0] == '\0') {
        while (*text != '\0' && n + 1 < cap)
            dst[n++] = *text++;
        dst[n] = '\0';
        return;
    }
    n = 0;
    while (who[n] != '\0' && n + 2 < cap) {
        dst[n] = who[n];
        n++;
    }
    if (n + 2 < cap) {
        dst[n++] = ':';
        dst[n++] = ' ';
    }
    while (*text != '\0' && n + 1 < cap)
        dst[n++] = *text++;
    dst[n] = '\0';
}

uint32_t mmo_chatwin_color(int type)
{
    switch (type) {
    case MMO_CHAT_NORMAL:  return 0x00F0F0F0u;
    case MMO_CHAT_SHOUT:   return 0x00FF8040u;
    case MMO_CHAT_WHISPER: return 0x00F080F0u;
    case MMO_CHAT_TRADE:   return 0x0040E0F0u;
    case MMO_CHAT_GLOBAL:  return 0x00F0E040u;
    case MMO_CHAT_CHANNEL: return 0x0040E080u;
    case MMO_CHAT_TEAM:    return 0x004080F0u;
    case MMO_CHAT_LINK:    return 0x0040C0C0u;
    case MMO_CHAT_SYSTEM:  return 0x00A0A0A8u;
    case MMO_CHAT_NOTICE:  return 0x00FFD040u;
    case MMO_CHAT_BATTLE:  return 0x00FF6060u;
    default:               return 0x00C0C0C8u;
    }
}

static int first_visible(const mmo_chatwin *w, int log_count)
{
    int vis = mmo_chatwin_row_budget(w);
    int start;

    if (vis <= 0 || log_count <= 0)
        return 0;
    if (vis > log_count)
        vis = log_count;
    start = log_count - vis - (w != NULL ? w->scroll : 0);
    if (start < 0)
        start = 0;
    return start;
}

int mmo_chatwin_visible_count(const mmo_chatwin *w, int log_count)
{
    int vis = mmo_chatwin_row_budget(w);
    int start;

    if (log_count < 0)
        log_count = 0;
    if (vis <= 0)
        return 0;
    start = first_visible(w, log_count);
    if (log_count - start < vis)
        vis = log_count - start;
    return vis < 0 ? 0 : vis;
}

int mmo_chatwin_get_row(const mmo_chatwin *w, const mmo_chatwin_line *log,
                        int log_count, int i, mmo_chatwin_row *out)
{
    int start;
    int vis = mmo_chatwin_visible_count(w, log_count);

    if (out == NULL)
        return 0;
    memset(out, 0, sizeof *out);
    if (w == NULL || log == NULL || i < 0 || i >= vis)
        return 0;
    start = first_visible(w, log_count);
    out->type = log[start + i].type;
    mmo_chatwin_format(&log[start + i], out->text, sizeof out->text);
    return 1;
}

void mmo_chatwin_scroll(mmo_chatwin *w, int delta, int log_count)
{
    int vis;
    int max_scroll;

    if (w == NULL)
        return;
    if (log_count < 0)
        log_count = 0;
    vis = mmo_chatwin_row_budget(w);
    max_scroll = log_count - vis;
    if (max_scroll < 0)
        max_scroll = 0;
    w->scroll += delta;
    if (w->scroll < 0)
        w->scroll = 0;
    if (w->scroll > max_scroll)
        w->scroll = max_scroll;
}

static void app_set(mmo_chatwin_app *out, int i, const char *s, int type)
{
    size_t n = 0;

    if (out == NULL || i < 0 || i >= MMO_CHATWIN_APP_ROWS)
        return;
    if (s == NULL)
        s = "";
    while (s[n] != '\0' && n < MMO_CHATWIN_APP_COLS) {
        out->line[i][n] = s[n];
        n++;
    }
    out->line[i][n] = '\0';
    out->type[i] = type;
}

static int key_row(const osk_key_view *v)
{
    int y = v->y - OSK_ORIGIN_Y - 16;

    if (y < 0)
        return -1;
    return y / 24;
}

static void render_keys(const osk_state *osk, mmo_chatwin_app *out)
{
    char typed[MMO_CHATWIN_APP_COLS + 1];
    int i, n, r;
    size_t t, tn;

    app_set(out, MMO_CHATWIN_APP_ROWS - 1, "START CLOSES", -1);
    if (osk == NULL)
        return;

    tn = osk_text_len(osk);
    if (tn > MMO_CHATWIN_APP_COLS)
        tn = MMO_CHATWIN_APP_COLS;
    for (t = 0; t < tn; t++) {
        unsigned u = osk->text[t];

        typed[t] = (u > 0 && u < 128) ? (char)u : '?';
    }
    typed[tn] = '\0';
    app_set(out, 1, tn ? typed : "TYPE...", -1);

    n = osk_key_count(osk);
    for (r = 0; r < 6 && 2 + r < MMO_CHATWIN_APP_ROWS - 1; r++) {
        char line[MMO_CHATWIN_APP_COLS + 1];
        int at = 0;

        memset(line, 0, sizeof line);
        for (i = 0; i < n && at < MMO_CHATWIN_APP_COLS; i++) {
            osk_key_view v;
            const char *s;

            if (!osk_get_key(osk, i, &v) || key_row(&v) != r)
                continue;
            if (v.selected && at < MMO_CHATWIN_APP_COLS)
                line[at++] = '[';
            if (v.kind == OSK_KEY_PAGE || v.kind == OSK_KEY_BACKSPACE
                || v.kind == OSK_KEY_ENTER) {
                if (at > 0 && !v.selected)
                    line[at++] = ' ';
                s = v.label;
                while (*s != '\0' && at < MMO_CHATWIN_APP_COLS)
                    line[at++] = *s++;
            } else if (v.label[0] != '\0' && v.label[1] == '\0') {
                line[at++] = v.label[0];
            } else {
                line[at++] = v.label[0] ? '?' : ' ';
            }
            if (v.selected && at < MMO_CHATWIN_APP_COLS)
                line[at++] = ']';
        }
        line[at] = '\0';
        app_set(out, 2 + r, line, -1);
    }
}

void mmo_chatwin_render_app(const mmo_chatwin *w, const mmo_chatwin_line *log,
                            int log_count, mmo_chatwin_app *out,
                            const osk_state *osk)
{
    int i, vis;
    mmo_chatwin_row row;

    if (out == NULL)
        return;
    memset(out, 0, sizeof *out);
    app_set(out, 0, "CHAT", -1);
    for (i = 1; i <= MMO_CHATWIN_LOG_ROWS; i++)
        app_set(out, i, "", -1);
    app_set(out, MMO_CHATWIN_APP_ROWS - 1, "START TO TALK", -1);

    if (w == NULL)
        return;
    if (w->mode == MMO_CHATWIN_COMPOSE) {
        render_keys(osk, out);
        return;
    }
    if (w->mode != MMO_CHATWIN_LOG || log == NULL)
        return;
    vis = mmo_chatwin_visible_count(w, log_count);
    for (i = 0; i < vis && i < MMO_CHATWIN_LOG_ROWS; i++) {
        if (!mmo_chatwin_get_row(w, log, log_count, i, &row))
            continue;
        app_set(out, 1 + i, row.text, row.type);
    }
}
