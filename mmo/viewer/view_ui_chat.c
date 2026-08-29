/* See view_ui_chat.h. Layout and wrapping are view_ui.c's
 * (SDL-free, held by tests/view_ui_test.c); this binds them to the page and
 * the renderer. */

#include "view_ui_chat.h"

#include <string.h>

#include "hud_channel.h"
#include "status_channel.h"

/* Six wrapped rows per delivered line covers a full 136-byte line on the
 * narrowest box the layout allows; a line needing more is cut there. */
#define CHAT_ROW_N ((int)OPENMMO_HUD_CHAT_N * 6)

void view_ui_chat_init(struct view_ui_chat *c, struct view_hud *hud)
{
    memset(c, 0, sizeof *c);
    c->hud = hud;
}

/*
 * Up while the session is in the world, and while there is still a log to read after it drops,
 * a disconnect must not eat the last thing said.
 */
static int chat_live(const struct view_ui_chat *c)
{
    if (c == NULL || c->hud == NULL || !c->hud->any)
        return 0;
    return c->hud->snap.net.state == OPENMMO_ST_IN_GAME ||
           c->hud->snap.chat_n > 0;
}

/* Every delivered line the shown tab does not hide, wrapped to `width`.
 * Statics, not 55KB of stack: one element draws at a time. */
static int chat_rows(const struct view_ui_chat *c, struct view_ui_gpu *g,
                     int width, char rows[][VIEW_UI_ROW_LEN], uint32_t *tags)
{
    const struct openmmo_hud_snap *s = &c->hud->snap;
    char line[192];
    int i, m = 0;

    for (i = 0; i < (int)s->chat_n && i < (int)OPENMMO_HUD_CHAT_N; i++) {
        if (!view_ui_chat_tab_shows(c->tab, s->chat[i].type))
            continue;
        view_hud_format_chat(&s->chat[i], line, sizeof line);
        m = view_ui_wrap(view_ui_measure_gpu, g, width, s->chat[i].type,
                         line, rows, tags, m, CHAT_ROW_N);
    }
    return m;
}

static void chat_draw(void *state, SDL_Renderer *ren, struct view_ui_gpu *g,
                      const struct view_ui_frame *f)
{
    static char rows[CHAT_ROW_N][VIEW_UI_ROW_LEN];
    static uint32_t tags[CHAT_ROW_N];
    struct view_ui_chat *c = state;
    struct view_ui_chat_layout L;
    const struct openmmo_hud_snap *s;
    SDL_Rect clip, prev;
    SDL_BlendMode old;
    char hint[OPENMMO_HUD_TEXT + 8];
    int want, over, m, start, i, y, mx = 0, my = 0, composing;

    if (!chat_live(c))
        return;
    s = &c->hud->snap;
    view_ui_chat_place(&f->canvas, &L);
    if (!view_ui_font_ready(g, ren, L.text_px))
        return;

    /* Quiet while walking, bright under the pointer or a typed line, and
     * near-invisible while the guest's own words are over the world, a
     * translucent log across a dialog box is two programs talking at once,
     * unless the player is the one typing. */
    SDL_GetMouseState(&mx, &my);
    over = view_ui_hit(&L.box, mx, my);
    composing = s->composing || f->typing;
    want = (over || composing) ? 208 : 88;
    if (s->guest_busy && !composing)
        want = 48;
    c->alpha = view_ui_fade(c->alpha, want, 12, &c->faded);
    view_ui_alpha((unsigned)c->alpha);

    SDL_GetRenderDrawBlendMode(ren, &old);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_RenderGetClipRect(ren, &prev);
    clip.x = L.box.x;
    clip.y = L.box.y;
    clip.w = L.box.w;
    clip.h = L.box.h;
    SDL_RenderSetClipRect(ren, &clip);

    if (!view_ui_th(ren, g, VIEW_UI_TH_PANEL, &L.box)) {
        view_ui_fill(ren, L.box.x, L.box.y, L.box.w, L.box.h, 18, 22, 28,
                     255);
        view_ui_border(ren, L.box.x, L.box.y, L.box.w, L.box.h, 74, 85, 96);
    }

    /* The channel tabs, the official client's chatframe top row (ui-tab art). */
    for (i = 0; i < L.tabs; i++) {
        const char *tl = view_ui_chat_tab_label(i);
        int active = i == c->tab;
        int tw = view_ui_text_width(g, tl);
        int tx = L.tab[i].x + (L.tab[i].w - tw) / 2;

        if (!view_ui_th(ren, g,
                        active ? VIEW_UI_TH_TAB_ACTIVE : VIEW_UI_TH_TAB,
                        &L.tab[i]))
            view_ui_panel(ren, &L.tab[i],
                          active ? VIEW_UI_COL_LINE : VIEW_UI_COL_FIELD,
                          VIEW_UI_COL_LINE);
        if (tx < L.tab[i].x)
            tx = L.tab[i].x;
        view_ui_text(ren, g, tx, L.tab[i].y + (L.tab[i].h - g->px) / 2, tl,
                     view_ui_col(active ? VIEW_UI_COL_TEXT
                                        : VIEW_UI_COL_DIM));
    }

    m = chat_rows(c, g, L.log.w, rows, tags);
    if (c->scroll > m - L.rows)
        c->scroll = m - L.rows;
    if (c->scroll < 0)
        c->scroll = 0;
    start = m - L.rows - c->scroll;
    if (start < 0)
        start = 0;
    y = L.log.y;
    for (i = start; i < m && y + L.line_h <= L.log.y + L.log.h; i++) {
        view_ui_text(ren, g, L.log.x, y, rows[i],
                     view_ui_col(view_ui_chat_colour(tags[i])));
        y += L.line_h;
    }

    /* The prefix is the guest's own word on where the line goes
     * (snap.send_type), not this window's tab: a channel switch shows up
     * here only once the guest has taken it, so the prefix cannot promise
     * a channel the send will not use. */
    view_ui_chat_input_line((int)s->composing, s->compose, s->compose_to,
                            view_ui_chat_type_label(s->send_type),
                            hint, sizeof hint);
    if (!view_ui_th(ren, g, VIEW_UI_TH_INPUT, &L.input)) {
        view_ui_fill(ren, L.input.x, L.input.y, L.input.w, L.input.h,
                     22, 26, 32, 255);
        view_ui_border(ren, L.input.x, L.input.y, L.input.w, L.input.h,
                       s->composing ? 106 : 74, s->composing ? 136 : 85,
                       s->composing ? 155 : 96);
    }
    view_ui_text(ren, g, L.input.x + 6,
                 L.input.y + (L.input.h - L.text_px) / 2, hint,
                 view_ui_col(s->composing ? VIEW_UI_COL_TEXT
                                          : VIEW_UI_COL_DIM));

    if (prev.w > 0 && prev.h > 0)
        SDL_RenderSetClipRect(ren, &prev);
    else
        SDL_RenderSetClipRect(ren, NULL);
    SDL_SetRenderDrawBlendMode(ren, old);
    view_ui_alpha(255);
}

/* Open compose on the active tab's channel. The channel rides ahead of the
 * compose so a guest that restarted mid-session (its channel back at the
 * default) still sends where the tab on screen says. */
static void view_ui_chat_open_compose(struct view_ui_chat *c)
{
    view_hud_push(c->hud, OPENMMO_HUD_CMD_CHANNEL,
                  (int32_t)view_ui_chat_tab_send_type(c->tab));
    view_hud_push(c->hud, OPENMMO_HUD_CMD_COMPOSE, 1);
}

static int chat_event(void *state, const SDL_Event *ev,
                      const struct view_ui_frame *f)
{
    struct view_ui_chat *c = state;
    struct view_ui_chat_layout L;

    if (!chat_live(c) || ev == NULL)
        return 0;

    /* Enter opens compose from anywhere, while a field is open the typed
     * line owns the key (view_input.c commits it), and Alt+Enter stays the
     * fullscreen chord. */
    if (ev->type == SDL_KEYDOWN && !ev->key.repeat &&
        (ev->key.keysym.sym == SDLK_RETURN ||
         ev->key.keysym.sym == SDLK_KP_ENTER) &&
        (ev->key.keysym.mod & KMOD_ALT) == 0 &&
        !f->typing && !c->hud->snap.composing &&
        c->hud->snap.net.state == OPENMMO_ST_IN_GAME) {
        view_ui_chat_open_compose(c);
        return 1;
    }

    view_ui_chat_place(&f->canvas, &L);

    if (ev->type == SDL_MOUSEWHEEL) {
        int mx = 0, my = 0;

        SDL_GetMouseState(&mx, &my);
        if (!view_ui_hit(&L.box, mx, my))
            return 0;
        c->scroll += ev->wheel.y > 0 ? 3 : -3;
        if (c->scroll < 0)
            c->scroll = 0;
        /* The guest's own log keeps step, so a headless reader and this
         * box tell the same story about what is on screen. */
        view_hud_push(c->hud, OPENMMO_HUD_CMD_SCROLL,
                      ev->wheel.y > 0 ? 1 : -1);
        return 1;
    }

    if (ev->type == SDL_MOUSEBUTTONDOWN &&
        ev->button.button == SDL_BUTTON_LEFT) {
        int hit;
        int t = view_ui_chat_tab_hit(&L, ev->button.x, ev->button.y);

        /* A new tab snaps back to the tail: the scroll was a place in the
         * old tab's rows, not this one's. It is also the channel the next
         * composed line goes out on, so the guest is told now, the official client's
         * rule that the tab you are reading is the one you speak on. */
        if (t >= 0) {
            if (t != c->tab) {
                c->tab = t;
                c->scroll = 0;
                view_hud_push(c->hud, OPENMMO_HUD_CMD_CHANNEL,
                              (int32_t)view_ui_chat_tab_send_type(t));
            }
            return 1;
        }
        hit = view_ui_chat_hit(&L, ev->button.x, ev->button.y);

        if (hit == VIEW_UI_CHAT_HIT_INPUT && !c->hud->snap.composing &&
            c->hud->snap.net.state == OPENMMO_ST_IN_GAME)
            view_ui_chat_open_compose(c);
        return hit != VIEW_UI_CHAT_HIT_NONE;
    }
    return 0;
}

int view_ui_chat_pointer_on(struct view_ui_chat *c,
                            const struct view_ui_frame *f, int mx, int my)
{
    struct view_ui_chat_layout L;

    if (!chat_live(c))
        return 0;
    view_ui_chat_place(&f->canvas, &L);
    return view_ui_hit(&L.box, mx, my);
}

static int chat_owns_pointer(void *state, const struct view_ui_frame *f,
                             int mx, int my)
{
    return view_ui_chat_pointer_on(state, f, mx, my);
}

struct view_ui_element view_ui_chat_element(struct view_ui_chat *c)
{
    struct view_ui_element el;

    memset(&el, 0, sizeof el);
    el.name = "chat";
    el.state = c;
    el.draw = chat_draw;
    el.event = chat_event;
    el.owns_pointer = chat_owns_pointer;
    return el;
}
