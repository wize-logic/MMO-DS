/* See view_ui.h. SDL-free; tests/view_ui_test.c holds it. */

#include "view_ui.h"

#include <stdio.h>
#include <string.h>

#include "hud_channel.h"
#include "text_channel.h"

int view_ui_clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

struct openmmo_rect view_ui_canvas(int mode, int win_w, int win_h,
                                   const struct openmmo_rect *top,
                                   const struct openmmo_rect *bottom)
{
    struct openmmo_rect r;
    const struct openmmo_rect *pick = NULL;

    if (win_w < 1) win_w = 1;
    if (win_h < 1) win_h = 1;
    if (mode == VIEW_UI_CANVAS_TOP)
        pick = top;
    else if (mode == VIEW_UI_CANVAS_BOTTOM)
        pick = bottom;
    if (pick != NULL && pick->w >= 8 && pick->h >= 8)
        return *pick;
    r.x = 0;
    r.y = 0;
    r.w = win_w;
    r.h = win_h;
    return r;
}

struct openmmo_rect view_ui_place(const struct openmmo_rect *canvas,
                                  int corner, int pad, int w, int h)
{
    struct openmmo_rect r, c;

    c.x = 0; c.y = 0; c.w = 1; c.h = 1;
    if (canvas != NULL)
        c = *canvas;
    if (c.w < 1) c.w = 1;
    if (c.h < 1) c.h = 1;
    if (pad < 0) pad = 0;
    w = view_ui_clampi(w, 1, c.w);
    h = view_ui_clampi(h, 1, c.h);
    r.w = w;
    r.h = h;
    r.x = c.x + ((corner == VIEW_UI_TOP_RIGHT || corner == VIEW_UI_BOTTOM_RIGHT)
                     ? c.w - pad - w : pad);
    r.y = c.y + ((corner == VIEW_UI_BOTTOM_LEFT || corner == VIEW_UI_BOTTOM_RIGHT)
                     ? c.h - pad - h : pad);
    /* An element the pad pushes over an edge is pulled back in whole: w and
     * h are already no larger than the canvas, so both clamps can hold. */
    if (r.x + r.w > c.x + c.w) r.x = c.x + c.w - r.w;
    if (r.y + r.h > c.y + c.h) r.y = c.y + c.h - r.h;
    if (r.x < c.x) r.x = c.x;
    if (r.y < c.y) r.y = c.y;
    return r;
}

int view_ui_hit(const struct openmmo_rect *r, int x, int y)
{
    if (r == NULL || r->w < 1 || r->h < 1)
        return 0;
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

int view_ui_text_px(int canvas_h)
{
    if (canvas_h < 1) canvas_h = 1;
    return view_ui_clampi(canvas_h * 16 / 1080, 12, 20);
}

int view_ui_fade(int alpha, int want, int step, int *started)
{
    if (started != NULL && !*started) {
        *started = 1;
        return want;
    }
    if (step < 1) step = 1;
    if (alpha < want) {
        alpha += step;
        if (alpha > want) alpha = want;
    } else if (alpha > want) {
        alpha -= step;
        if (alpha < want) alpha = want;
    }
    return alpha;
}

/*
 * The one copy of the chat palette; the panel's SDL and raster twins both had one before the
 * box moved onto this layer.
 */
uint32_t view_ui_chat_colour(uint32_t type)
{
    switch (type) {
    case OPENMMO_HUD_CHAT_NORMAL:  return 0xFFFFFFu;
    case OPENMMO_HUD_CHAT_SHOUT:   return 0xFF9900u;
    case OPENMMO_HUD_CHAT_WHISPER: return 0x66FF66u;
    case OPENMMO_HUD_CHAT_TRADE:   return 0xFF99FFu;
    case OPENMMO_HUD_CHAT_GLOBAL:  return 0x81DDF1u;
    case OPENMMO_HUD_CHAT_CHANNEL: return 0xE2C57Eu;
    case OPENMMO_HUD_CHAT_TEAM:    return 0xFF8484u;
    case OPENMMO_HUD_CHAT_LINK:    return 0x00E6B8u;
    case OPENMMO_HUD_CHAT_SYSTEM:  return 0xAACFFFu;
    case OPENMMO_HUD_CHAT_NOTICE:  return 0xFFD800u;
    case OPENMMO_HUD_CHAT_BATTLE:  return 0xFFFF00u;
    default:                       return 0xCECECEu;
    }
}

#define CHAT_HIDE(t) (1u << (t))
static const struct {
    const char *label;
    uint32_t hidden;
    uint32_t send;
} chat_tabs[VIEW_UI_CHAT_TABS] = {
    { "Local",    CHAT_HIDE(OPENMMO_HUD_CHAT_GLOBAL) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_BATTLE) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_TRADE),
                  OPENMMO_HUD_CHAT_NORMAL },
    { "Global",   CHAT_HIDE(OPENMMO_HUD_CHAT_CHANNEL) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_BATTLE) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_NORMAL) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_SHOUT) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_WHISPER) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_TRADE) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_LINK) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_TEAM) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_NOTICE),
                  OPENMMO_HUD_CHAT_GLOBAL },
    { "Trade",    CHAT_HIDE(OPENMMO_HUD_CHAT_CHANNEL) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_GLOBAL) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_BATTLE) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_NORMAL) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_SHOUT) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_WHISPER) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_LINK) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_TEAM) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_NOTICE),
                  OPENMMO_HUD_CHAT_TRADE },
    { "Whispers", CHAT_HIDE(OPENMMO_HUD_CHAT_CHANNEL) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_GLOBAL) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_BATTLE) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_NORMAL) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_SHOUT) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_TRADE) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_LINK) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_TEAM),
                  OPENMMO_HUD_CHAT_WHISPER },
    { "Battle",   CHAT_HIDE(OPENMMO_HUD_CHAT_CHANNEL) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_SYSTEM) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_GLOBAL) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_NORMAL) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_SHOUT) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_WHISPER) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_TRADE) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_LINK) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_TEAM) |
                  CHAT_HIDE(OPENMMO_HUD_CHAT_NOTICE),
                  OPENMMO_HUD_CHAT_BATTLE },
};

uint32_t view_ui_chat_tab_send_type(int tab)
{
    if (tab < 0 || tab >= VIEW_UI_CHAT_TABS)
        return OPENMMO_HUD_CHAT_NORMAL;
    return chat_tabs[tab].send;
}

const char *view_ui_chat_tab_label(int tab)
{
    if (tab < 0 || tab >= VIEW_UI_CHAT_TABS)
        return NULL;
    return chat_tabs[tab].label;
}

/* The label for a send channel, keyed by the channel itself rather than a
 * tab: the input-line prefix names the channel the guest says it will send
 * on, which is not always the tab this window last clicked. NULL for the
 * default channel (and anything unknown), so Local composes bare. */
const char *view_ui_chat_type_label(uint32_t type)
{
    int i;

    if (type == OPENMMO_HUD_CHAT_NORMAL)
        return NULL;
    for (i = 0; i < VIEW_UI_CHAT_TABS; i++)
        if (chat_tabs[i].send == type)
            return chat_tabs[i].label;
    return NULL;
}

int view_ui_chat_tab_shows(int tab, uint32_t type)
{
    if (tab < 0 || tab >= VIEW_UI_CHAT_TABS || type >= 32u)
        return 1;
    return (chat_tabs[tab].hidden & (1u << type)) == 0;
}

unsigned view_ui_utf8(const char *s, uint32_t *cp)
{
    uint16_t u[2];
    unsigned used = 1, n;

    if (cp != NULL)
        *cp = 0xFFFDu;
    if (s == NULL || *s == '\0')
        return 0;
    n = openmmo_text_utf8_to_utf16((const unsigned char *)s, (unsigned)strlen(s),
                                   u, &used);
    /* One unit is the BMP and is the code point. Two is a surrogate pair,
     * above anything a UI face carries a glyph for, so it is named as absent
     * rather than pieced back together for a cache that would miss it. */
    if (n == 1 && cp != NULL)
        *cp = u[0];
    return used;
}

int view_ui_utf8_trunc(char *s)
{
    int at = 0, last = 0;

    if (s == NULL)
        return 0;
    while (s[at] != '\0') {
        unsigned step = view_ui_utf8(s + at, NULL);

        if (step == 0)
            break;
        last = at;
        at += (int)step;
    }
    s[last] = '\0';
    return last;
}

int view_ui_wrap(view_ui_measure measure, void *ctx, int width, uint32_t tag,
                 const char *s, char rows[][VIEW_UI_ROW_LEN], uint32_t *tags,
                 int at, int cap)
{
    if (measure == NULL || s == NULL || rows == NULL || tags == NULL)
        return at;
    while (*s != '\0' && at < cap) {
        int i = 0, cut = -1;

        while (*s == ' ')
            s++;
        if (*s == '\0')
            break;
        while (s[i] != '\0' && i < VIEW_UI_ROW_LEN - 1) {
            /* A whole character at a time: a cut inside a UTF-8 sequence
             * leaves half of one at the end of this row and a stray
             * continuation byte at the start of the next. */
            unsigned step = view_ui_utf8(s + i, NULL);

            if (step == 0)
                break;
            /* The cut point first: a space sitting exactly at the width is
             * still where the row ends, not part of the word after it. */
            if (s[i] == ' ')
                cut = i;
            if (i + (int)step > VIEW_UI_ROW_LEN - 1)
                break;
            if (measure(ctx, s, i + (int)step) > width && i > 0)
                break;
            i += (int)step;
        }
        if (s[i] != '\0' && i < VIEW_UI_ROW_LEN - 1 && cut > 0)
            i = cut;
        memcpy(rows[at], s, (size_t)i);
        rows[at][i] = '\0';
        tags[at] = tag;
        at++;
        s += i;
    }
    return at;
}

void view_ui_chat_place(const struct openmmo_rect *canvas,
                        struct view_ui_chat_layout *out)
{
    struct openmmo_rect c;
    int margin = 10, pad = 8;
    int input_h, tab_h, avail, want_h, box_w, box_h;

    if (out == NULL)
        return;
    memset(out, 0, sizeof *out);
    c.x = 0; c.y = 0; c.w = 1; c.h = 1;
    if (canvas != NULL)
        c = *canvas;
    if (c.w < 1) c.w = 1;
    if (c.h < 1) c.h = 1;

    out->pad = pad;
    /* Type scales to the window's height even on a small canvas: a band-
     * confined box changes where the words are, not how big they are. The
     * canvas y+h is as close as this gets to the window bottom, and it is
     * exact for every canvas the window hands out. */
    out->text_px = view_ui_text_px(c.y + c.h);
    out->line_h = out->text_px + 6;
    input_h = out->text_px + 10;
    /* The tab strip is compose-row tall, which lands on the official client's own ratio
     * (ui-tab.active is 26 of its 12px pixels). */
    tab_h = input_h;

    box_w = view_ui_clampi(c.w * 35 / 100, 300, 620);
    if (box_w > c.w - 2 * margin)
        box_w = c.w - 2 * margin;
    if (box_w < 8)
        box_w = c.w;

    /* The log is whole rows: the box is sized from the row count, so a row
     * is never half-visible at the top. */
    avail = c.h - 2 * margin;
    want_h = c.h * 28 / 100;
    if (want_h > avail)
        want_h = avail;
    out->rows = view_ui_clampi(
        (want_h - input_h - tab_h - 4 * pad) / out->line_h, 2, 14);
    /* A short canvas (the second-screen band) still deserves a readable
     * log: take at least four rows wherever four rows fit. */
    while (out->rows < 4 &&
           (out->rows + 1) * out->line_h + input_h + tab_h + 4 * pad <= avail)
        out->rows++;
    box_h = out->rows * out->line_h + input_h + tab_h + 4 * pad;
    while (out->rows > 1 && box_h > avail) {
        out->rows--;
        box_h = out->rows * out->line_h + input_h + tab_h + 4 * pad;
    }
    /* Still over at one row: the strip goes before the log does. */
    if (box_h > avail) {
        tab_h = 0;
        box_h = out->rows * out->line_h + input_h + 3 * pad;
    }
    if (box_h > c.h)
        box_h = c.h;

    out->box = view_ui_place(&c, VIEW_UI_BOTTOM_LEFT, margin, box_w, box_h);
    if (tab_h > 0) {
        int tw = (out->box.w - 2 * pad) / VIEW_UI_CHAT_TABS;
        int tx = out->box.x + pad;
        int i;

        if (tw < 1)
            tw = 1;
        out->tabs = VIEW_UI_CHAT_TABS;
        for (i = 0; i < VIEW_UI_CHAT_TABS; i++) {
            out->tab[i].x = tx;
            out->tab[i].y = out->box.y + pad;
            out->tab[i].w = tw;
            out->tab[i].h = tab_h;
            tx += tw;
        }
        /* The last tab takes the remainder, so the strip spans the log. */
        out->tab[VIEW_UI_CHAT_TABS - 1].w =
            out->box.x + out->box.w - pad - out->tab[VIEW_UI_CHAT_TABS - 1].x;
        if (out->tab[VIEW_UI_CHAT_TABS - 1].w < 1)
            out->tab[VIEW_UI_CHAT_TABS - 1].w = 1;
    }
    out->log.x = out->box.x + pad;
    out->log.y = out->box.y + pad + (tab_h > 0 ? tab_h + pad : 0);
    out->log.w = out->box.w - 2 * pad;
    out->log.h = out->rows * out->line_h;
    out->input.x = out->box.x + pad;
    out->input.y = out->box.y + out->box.h - pad - input_h;
    out->input.w = out->box.w - 2 * pad;
    out->input.h = input_h;
    if (out->log.w < 1) out->log.w = 1;
    if (out->input.w < 1) out->input.w = 1;
}

int view_ui_chat_hit(const struct view_ui_chat_layout *L, int x, int y)
{
    if (L == NULL)
        return VIEW_UI_CHAT_HIT_NONE;
    if (view_ui_hit(&L->input, x, y))
        return VIEW_UI_CHAT_HIT_INPUT;
    if (view_ui_hit(&L->box, x, y))
        return VIEW_UI_CHAT_HIT_LOG;
    return VIEW_UI_CHAT_HIT_NONE;
}

int view_ui_chat_tab_hit(const struct view_ui_chat_layout *L, int x, int y)
{
    int i;

    if (L == NULL)
        return -1;
    for (i = 0; i < L->tabs && i < VIEW_UI_CHAT_TABS; i++)
        if (view_ui_hit(&L->tab[i], x, y))
            return i;
    return -1;
}

int view_ui_chat_input_line(int composing, const char *compose,
                            const char *to, const char *channel,
                            char *dst, size_t cap)
{
    if (dst == NULL || cap == 0)
        return composing ? 1 : 0;
    if (!composing) {
        snprintf(dst, cap, "Enter to chat");
        return 0;
    }
    /* A whisper says who it is going to, the way the official client's prefilled
     * whisper line does; failing that, a non-default tab names the channel
     * the line is about to go out on. */
    if (to != NULL && to[0] != '\0')
        snprintf(dst, cap, "To %s: %s_", to,
                 compose != NULL ? compose : "");
    else if (channel != NULL && channel[0] != '\0')
        snprintf(dst, cap, "[%s] %s_", channel,
                 compose != NULL ? compose : "");
    else if (compose != NULL && compose[0] != '\0')
        snprintf(dst, cap, "%s_", compose);
    else
        snprintf(dst, cap, "_");
    return 1;
}
