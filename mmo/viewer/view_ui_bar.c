/* See view_ui_bar.h. Placement, the button and menu tables
 * and every string are view_ui_game.c's (SDL-free, held by
 * tests/view_ui_test.c); this binds them to the page and the renderer. */

#include "view_ui_bar.h"

#include <stdio.h>
#include <string.h>

/* How long the official client's own popup message stays up. Long enough to read a
 * sentence, short enough that it is gone before the next thing is clicked. */
#define NOTICE_MS 4000u

void view_ui_bar_init(struct view_ui_bar *b, struct view_hud *hud,
                      struct view_ui_wins *wins)
{
    memset(b, 0, sizeof *b);
    b->hud = hud;
    b->wins = wins;
    b->menu = VIEW_UI_MENU_NONE;
}

int view_ui_bar_quit(const struct view_ui_bar *b)
{
    return b != NULL && b->quit;
}

int view_ui_bar_poketch(const struct view_ui_bar *b)
{
    return b != NULL && b->poketch;
}

/* Up while the session is in the world, and never in the lobby; offline, up
 * on a settled field. The rule itself is view_ui_game.c's, which is the half
 * the suite can run; this is the page it reads. */
static int bar_live(const struct view_ui_bar *b)
{
    if (b == NULL || b->hud == NULL || !b->hud->any)
        return 0;
    return view_ui_bar_live(&b->hud->snap);
}

static void say(struct view_ui_bar *b, const struct view_ui_item *it)
{
    view_ui_notice_text(it, b->notice, sizeof b->notice);
    b->notice_at = SDL_GetTicks64();
}

/*
 * A screen this save cannot open yet: a fresh character has no dex and no party, and official
 * disables the entry rather than opening a screen that cannot exist. The guest publishes both
 * facts on the page.
 */
static int item_disabled(const struct view_ui_bar *b,
                         const struct view_ui_item *it)
{
    /* Offline there is nobody to answer one of the window's own panels, and
     * the two that survive the bar's own cull (Instance Info) would open on
     * an empty session. The bar's greyed art plus act()'s sentence is the
     * same answer official gives a button it cannot serve. */
    if (it != NULL && it->act == VIEW_UI_ACT_WINDOW && view_ui_offline())
        return 1;
    /* Carrying a session out to the offline row needs a session to carry and a
     * position the offline game can be resumed on. Offline there is neither;
     * in the Underground the tile is a cavern the server never hears about and
     * a save taken there resumes into one with no server behind it. */
    if (it != NULL && it->act == VIEW_UI_ACT_EXPORT)
        return view_ui_offline() || b->hud->snap.underground;
    if (it == NULL || it->act != VIEW_UI_ACT_SCREEN)
        return 0;
    if (b->hud->snap.underground)
        return 1;
    if (it->arg == OPENMMO_HUD_SCREEN_DEX)
        return !b->hud->snap.has_dex;
    /*
     * party_n is what the server sent, so offline it is zero however many the save holds,
     * the guest counts the engine's own party before it opens that screen and says so when
     * there is nobody in it, which is the right answer here.
     */
    if (it->arg == OPENMMO_HUD_SCREEN_PARTY)
        return !view_ui_offline() && b->hud->snap.party_n == 0;
    return 0;
}

/* One button or row, done. Returns nonzero when the popup should close,
 * which is every action except opening another popup. */
static int act(struct view_ui_bar *b, const struct view_ui_item *it,
               const struct openmmo_rect *anchor)
{
    if (it == NULL)
        return 1;
    /* A disabled entry answers with the sentence rather than with nothing:
     * the grey art says it will not open, this says it out loud when it is
     * pressed anyway. */
    if (item_disabled(b, it)) {
        /* Say why when the reason is the place rather than the save: "Bag is
         * not available at this time" is true underground and unhelpful. */
        if (b->hud->snap.underground && it->act == VIEW_UI_ACT_SCREEN) {
            snprintf(b->notice, sizeof b->notice,
                     "There is no room for that in the Underground.");
            b->notice_at = SDL_GetTicks64();
            return 1;
        }
        say(b, it);
        return 1;
    }
    switch (it->act) {
    case VIEW_UI_ACT_MENU:
        if (b->menu == it->arg) {
            b->menu = VIEW_UI_MENU_NONE;
            return 1;
        }
        b->menu = it->arg;
        if (anchor != NULL)
            b->anchor = *anchor;
        return 0;
    case VIEW_UI_ACT_SCREEN:
        /* The guest opens its own screen when the field is settled; it says
         * so in the log if it cannot. Nothing is drawn here for it. */
        view_hud_push(b->hud, OPENMMO_HUD_CMD_SCREEN, it->arg);
        break;
    case VIEW_UI_ACT_WINDOW:
        view_ui_wins_toggle(b->wins, it->arg);
        break;
    case VIEW_UI_ACT_LOGOUT:
        /* The official client asks first (string 1160); the send happens on Yes. */
        b->confirm = OPENMMO_HUD_CMD_LOGOUT;
        break;
    case VIEW_UI_ACT_EXPORT:
        /* The same box: this one ends the session for good and writes the
         * save, so it is asked about at least as carefully as a logout. */
        b->confirm = OPENMMO_HUD_CMD_EXPORT;
        break;
    case VIEW_UI_ACT_QUIT:
        b->quit = 1;
        break;
    case VIEW_UI_ACT_POKETCH:
        /*
         * Nothing is asked of the guest: the device is already running down there and this is
         * only whether the window draws it.
         */
        b->poketch = !b->poketch;
        break;
    case VIEW_UI_ACT_NOTICE:
    default:
        say(b, it);
        break;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Draw                                                                */
/* ------------------------------------------------------------------ */

static void draw_button(SDL_Renderer *ren, struct view_ui_gpu *g,
                        const struct openmmo_rect *r,
                        const struct view_ui_item *it, const char *label,
                        int hot, int down, int dis)
{
    SDL_Rect saved;
    char key[8];

    /* The official client's button, its states drawn the way the theme composes them:
     * the disabled art for a screen this save cannot open, the pressed art
     * under an open menu, the default otherwise, and the main-color hover
     * wash while the pointer is on a live one. */
    if (view_ui_th(ren, g,
                   dis ? VIEW_UI_TH_BUTTON_DIS
                       : down ? VIEW_UI_TH_BUTTON_DOWN : VIEW_UI_TH_BUTTON,
                   r)) {
        if (hot && !dis)
            view_ui_th(ren, g, VIEW_UI_TH_BUTTON_HOVER, r);
    } else {
        view_ui_panel(ren, r,
                      hot && !dis ? VIEW_UI_COL_LINE : VIEW_UI_COL_FIELD,
                      hot && !dis ? VIEW_UI_COL_ACCENT : VIEW_UI_COL_LINE);
    }
    view_ui_clip_push(ren, r, &saved);
    /*
     * The official client right-aligns the label (ui/main.xml hud-item-button: border 7) because a 24px
     * item icon fills the button's left; those icons live in the client's pak archives, which
     * the theme loader does not read, so the key hint stands where the icon would, dim at
     * rest, accented under the pointer, and a button with no key (official binds none for
     * Community, PvP, Mail or Gift Shop) centres its label rather than keeping an empty icon
     * gap.
     */
    key[0] = '\0';
    if (it->key != 0 && !dis) {
        int pad = view_ui_scale(7, g->px);

        snprintf(key, sizeof key, "%c", it->key - 'a' + 'A');
        /*
         * The hint has to earn its place, because the label is drawn against the opposite edge
         * of the same box and neither one clips the other.
         */
        if (view_ui_text_width(g, label) + view_ui_text_width(g, key)
                + 3 * pad > r->w)
            key[0] = '\0';
    }
    if (key[0] != '\0') {
        int pad = view_ui_scale(7, g->px);

        view_ui_text_in(ren, g, r, pad, 1, label,
                        dis ? 0x999999u : VIEW_UI_COL_TEXT);
        /* Clear of the button art's own 4px edge, so the letter sits
         * on the face rather than on the bevel. */
        view_ui_text_in(ren, g, r, pad, 0, key,
                        hot ? VIEW_UI_COL_ACCENT : VIEW_UI_COL_DIM);
    } else {
        int tw = view_ui_text_width(g, label);
        int x = r->x + (r->w - tw) / 2;

        if (x < r->x + view_ui_scale(7, g->px))
            x = r->x + view_ui_scale(7, g->px);
        view_ui_text(ren, g, x, r->y + (r->h - g->px) / 2, label,
                     view_ui_col(dis ? 0x999999u : VIEW_UI_COL_TEXT));
    }
    view_ui_clip_pop(ren, &saved);
}

static void draw_menu(struct view_ui_bar *b, SDL_Renderer *ren,
                      struct view_ui_gpu *g, const struct view_ui_frame *f,
                      int mx, int my)
{
    const struct view_ui_menu_def *d = view_ui_menu_def(b->menu);
    struct view_ui_menu_layout M;
    SDL_Rect saved;
    int i;

    if (d == NULL)
        return;
    view_ui_menu_place(&f->canvas, &b->anchor, b->menu, view_ui_measure_gpu, g,
                       &M);
    b->laid_menu = M;
    view_ui_clip_push(ren, &M.box, &saved);
    if (!view_ui_th(ren, g, VIEW_UI_TH_PANEL, &M.box))
        view_ui_panel(ren, &M.box, VIEW_UI_COL_GROUND, VIEW_UI_COL_ACCENT);
    for (i = 0; i < M.n; i++) {
        int hot = view_ui_hit(&M.row[i], mx, my);
        uint32_t fg = (d->item[i].act == VIEW_UI_ACT_NOTICE ||
                       item_disabled(b, &d->item[i])) ? VIEW_UI_COL_DIM
                                                      : VIEW_UI_COL_TEXT;

        if (hot) {
            fg = VIEW_UI_COL_TEXT;
            if (!view_ui_th(ren, g, VIEW_UI_TH_ROW_HOVER, &M.row[i])) {
                SDL_Color c = view_ui_col(VIEW_UI_COL_LINE);

                view_ui_fill(ren, M.row[i].x, M.row[i].y, M.row[i].w,
                             M.row[i].h, c.r, c.g, c.b, 255);
            }
        }
        /* Centred, the way the official client's popup rows are (popup-button:
         * textAlignment center); the key hint keeps the right edge. */
        {
            int tw = view_ui_text_width(g, d->item[i].label);
            int x = M.row[i].x + (M.row[i].w - tw) / 2;

            if (x < M.row[i].x + M.pad)
                x = M.row[i].x + M.pad;
            view_ui_text(ren, g,
                         x, M.row[i].y + (M.row[i].h - g->px) / 2,
                         d->item[i].label, view_ui_col(fg));
        }
        if (d->item[i].key != 0) {
            char key[8];

            snprintf(key, sizeof key, "%c", d->item[i].key - 'a' + 'A');
            view_ui_text_in(ren, g, &M.row[i], M.pad, 1, key,
                            hot ? VIEW_UI_COL_ACCENT : VIEW_UI_COL_DIM);
        }
    }
    view_ui_clip_pop(ren, &saved);
}

static void draw_notice(struct view_ui_bar *b, SDL_Renderer *ren,
                        struct view_ui_gpu *g, const struct view_ui_frame *f)
{
    struct view_ui_notice_layout N;
    Uint64 age;

    if (b->notice[0] == '\0')
        return;
    age = SDL_GetTicks64() - b->notice_at;
    if (age > NOTICE_MS) {
        b->notice[0] = '\0';
        return;
    }
    view_ui_notice_place(&f->canvas, view_ui_text_width(g, b->notice), &N);
    /* Full while it is being read, then out over the last half second. */
    if (age > NOTICE_MS - 500u)
        view_ui_alpha((unsigned)(255u * (NOTICE_MS - age) / 500u));
    else
        view_ui_alpha(255);
    if (!view_ui_th(ren, g, VIEW_UI_TH_PANEL, &N.box))
        view_ui_panel(ren, &N.box, VIEW_UI_COL_GROUND, VIEW_UI_COL_ACCENT);
    view_ui_text_in(ren, g, &N.box, N.pad, 0, b->notice, VIEW_UI_COL_TEXT);
}

/* What the box asks, per command it is standing in front of. The official client's logout
 * sentence is string 1160; the other is ours, and says the two things a player
 * needs to know before answering, that the session ends, and that what they
 * are standing in is what gets written down. */
static const char *confirm_text(unsigned cmd)
{
    if (cmd == OPENMMO_HUD_CMD_EXPORT)
        return "Save and continue offline? This ends the session.";
    return "Are you sure you want to logout?";
}

/* The official client's confirm over the warning art, the same box the frames use for a
 * removal. */
static void draw_confirm(struct view_ui_bar *b, SDL_Renderer *ren,
                         struct view_ui_gpu *g,
                         const struct view_ui_frame *f,
                         int mx, int my)
{
    const char *const text = confirm_text(b->confirm);
    struct view_ui_confirm_layout C;
    int i;

    if (!b->confirm)
        return;
    view_ui_alpha(255);
    view_ui_confirm_place(&f->canvas, view_ui_text_width(g, text) / 2, &C);
    b->laid_confirm = C;
    if (!view_ui_th(ren, g, VIEW_UI_TH_WARN, &C.box))
        view_ui_panel(ren, &C.box, VIEW_UI_COL_GROUND, VIEW_UI_COL_ACCENT);
    {
        int tw = view_ui_text_width(g, text);
        int x = C.text.x + (C.text.w - tw) / 2;

        if (x < C.text.x)
            x = C.text.x;
        view_ui_text(ren, g, x, C.text.y, text, view_ui_col(VIEW_UI_COL_TEXT));
    }
    for (i = 0; i < 2; i++) {
        const struct openmmo_rect *r = i == 0 ? &C.yes : &C.no;
        const char *label = i == 0 ? "Yes" : "No";
        int hot = view_ui_hit(r, mx, my);
        int tw = view_ui_text_width(g, label);

        if (view_ui_th(ren, g, VIEW_UI_TH_BUTTON, r)) {
            if (hot)
                view_ui_th(ren, g, VIEW_UI_TH_BUTTON_HOVER, r);
        } else {
            view_ui_panel(ren, r, hot ? VIEW_UI_COL_LINE : VIEW_UI_COL_FIELD,
                          hot ? VIEW_UI_COL_ACCENT : VIEW_UI_COL_LINE);
        }
        view_ui_text(ren, g, r->x + (r->w - tw) / 2,
                     r->y + (r->h - g->px) / 2, label,
                     view_ui_col(VIEW_UI_COL_TEXT));
    }
}

static void bar_draw(void *state, SDL_Renderer *ren, struct view_ui_gpu *g,
                     const struct view_ui_frame *f)
{
    struct view_ui_bar *b = state;
    struct view_ui_bar_layout L;
    SDL_BlendMode old;
    int i, want, mx = 0, my = 0, over;

    if (!bar_live(b))
        return;
    view_ui_bar_place(&f->canvas, view_ui_measure_gpu, g, &L);
    if (!view_ui_font_ready(g, ren, L.text_px))
        return;
    /* The font's real advances only became known on the line above, so the
     * bar is laid out once more with them: the first frame after a resize
     * would otherwise be sized from the fallback measure. */
    view_ui_bar_place(&f->canvas, view_ui_measure_gpu, g, &L);

    SDL_GetMouseState(&mx, &my);
    over = view_ui_hit(&L.bar, mx, my);
    want = (over || b->menu != VIEW_UI_MENU_NONE) ? 255 : 208;
    /* A control strip across the guest's own dialog is two programs talking
     * at once, the same reason the chat box stands aside. */
    if (b->hud->snap.guest_busy && b->menu == VIEW_UI_MENU_NONE)
        want = 64;
    b->alpha = view_ui_fade(b->alpha, want, 12, &b->faded);
    view_ui_alpha((unsigned)b->alpha);

    SDL_GetRenderDrawBlendMode(ren, &old);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    if (!view_ui_th(ren, g, VIEW_UI_TH_PANEL, &L.bar))
        view_ui_panel(ren, &L.bar, VIEW_UI_COL_GROUND, VIEW_UI_COL_LINE);
    for (i = 0; i < VIEW_UI_BAR_N; i++) {
        const struct view_ui_item *it = view_ui_bar_item(i);
        /* The official client's pressed art is for a button whose popup is open; the
         * Poketch has no popup, and "the screen it names is up" is the same
         * thing to look at, so it holds the button down while it is on. */
        int down = (it->act == VIEW_UI_ACT_MENU && b->menu == it->arg)
                || (it->act == VIEW_UI_ACT_POKETCH && b->poketch);
        int hot = view_ui_hit(&L.btn[i], mx, my) || down;

        if (!view_ui_bar_shown(i))
            continue;
        draw_button(ren, g, &L.btn[i], it, view_ui_bar_label(i, L.shortened),
                    hot, down, item_disabled(b, it));
    }
    if (b->menu != VIEW_UI_MENU_NONE)
        draw_menu(b, ren, g, f, mx, my);
    draw_notice(b, ren, g, f);
    draw_confirm(b, ren, g, f, mx, my);
    b->laid_bar = L;
    b->laid = 1;

    SDL_SetRenderDrawBlendMode(ren, old);
    view_ui_alpha(255);
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

/* The item a letter key names, over the bar and over every menu, official
 * binds both halves (config/main.properties: B bag, C trainer, N codex, V
 * friendlist, G team, D gamemenu, I incubator, P tradelink, H faq). */
static const struct view_ui_item *by_key(char k, struct openmmo_rect *anchor,
                                         const struct view_ui_bar_layout *L)
{
    int i, m;

    for (i = 0; i < VIEW_UI_BAR_N; i++) {
        const struct view_ui_item *it = view_ui_bar_item(i);

        /* A hidden button has no key either: the letter would otherwise open
         * a screen the bar just said this run does not have. */
        if (!view_ui_bar_shown(i))
            continue;
        if (it->key == k) {
            if (anchor != NULL && L != NULL)
                *anchor = L->btn[i];
            return it;
        }
    }
    for (m = VIEW_UI_MENU_NONE + 1; m < VIEW_UI_MENU_N; m++) {
        const struct view_ui_menu_def *d = view_ui_menu_def(m);

        for (i = 0; d != NULL && i < d->n; i++)
            if (d->item[i].key == k)
                return &d->item[i];
    }
    return NULL;
}

/* The bar as it was drawn. Before the first frame there is nothing to
 * answer against, so it is placed from the fallback measure, the same
 * shape, within a few pixels a pointer has not had time to reach. */
static void laid_out(struct view_ui_bar *b, const struct view_ui_frame *f,
                     struct view_ui_bar_layout *bar,
                     struct view_ui_menu_layout *menu)
{
    if (b->laid) {
        if (bar != NULL)
            *bar = b->laid_bar;
        if (menu != NULL)
            *menu = b->laid_menu;
        return;
    }
    if (bar != NULL)
        view_ui_bar_place(&f->canvas, NULL, NULL, bar);
    if (menu != NULL)
        view_ui_menu_place(&f->canvas, &b->anchor, b->menu, NULL, NULL, menu);
}

static int bar_event(void *state, const SDL_Event *ev,
                     const struct view_ui_frame *f)
{
    struct view_ui_bar *b = state;
    struct view_ui_bar_layout L;
    struct view_ui_menu_layout M;
    int hit;

    if (!bar_live(b) || ev == NULL)
        return 0;

    /* The confirm is modal, the official client's way: Yes sends whichever command raised
     * it, No or Esc or a click outside puts it away, and nothing underneath
     * hears the press. */
    if (b->confirm) {
        if (ev->type == SDL_KEYDOWN && !ev->key.repeat) {
            SDL_Keycode k = ev->key.keysym.sym;

            if (k == SDLK_y || k == SDLK_RETURN) {
                view_hud_push(b->hud, b->confirm, 0);
                b->confirm = 0;
                return 1;
            }
            if (k == SDLK_ESCAPE || k == SDLK_n) {
                b->confirm = 0;
                return 1;
            }
            return 1;
        }
        if (ev->type == SDL_MOUSEBUTTONDOWN &&
            ev->button.button == SDL_BUTTON_LEFT) {
            struct view_ui_confirm_layout C = b->laid_confirm;

            /* The frame between raising the box and drawing it has no laid
             * layout yet, so fall back to the smallest one rather than to a
             * zeroed rectangle nothing can hit. */
            if (C.box.w <= 0)
                view_ui_confirm_place(&f->canvas, 0, &C);
            if (view_ui_hit(&C.yes, ev->button.x, ev->button.y))
                view_hud_push(b->hud, b->confirm, 0);
            if (view_ui_hit(&C.yes, ev->button.x, ev->button.y) ||
                view_ui_hit(&C.no, ev->button.x, ev->button.y) ||
                !view_ui_hit(&C.box, ev->button.x, ev->button.y))
                b->confirm = 0;
            return 1;
        }
        return ev->type == SDL_MOUSEBUTTONUP || ev->type == SDL_MOUSEWHEEL;
    }
    laid_out(b, f, &L, &M);

    if (ev->type == SDL_KEYDOWN && !ev->key.repeat && !f->typing &&
        !b->hud->snap.composing &&
        (ev->key.keysym.mod & (KMOD_ALT | KMOD_CTRL | KMOD_GUI)) == 0) {
        SDL_Keycode k = ev->key.keysym.sym;

        if (k == SDLK_ESCAPE && b->menu != VIEW_UI_MENU_NONE) {
            b->menu = VIEW_UI_MENU_NONE;
            return 1;
        }
        if (k >= SDLK_a && k <= SDLK_z) {
            struct openmmo_rect anchor = b->anchor;
            const struct view_ui_item *it =
                by_key((char)('a' + (k - SDLK_a)), &anchor, &L);

            if (it != NULL) {
                act(b, it, &anchor);
                return 1;
            }
        }
        return 0;
    }

    if (ev->type == SDL_MOUSEBUTTONDOWN &&
        ev->button.button == SDL_BUTTON_LEFT) {
        int was = b->menu;

        if (was != VIEW_UI_MENU_NONE) {
            const struct view_ui_menu_def *d = view_ui_menu_def(was);
            int row = view_ui_menu_hit(&M, ev->button.x, ev->button.y);

            if (row >= 0 && d != NULL) {
                if (act(b, &d->item[row], NULL))
                    b->menu = VIEW_UI_MENU_NONE;
                return 1;
            }
            if (view_ui_hit(&M.box, ev->button.x, ev->button.y))
                return 1;
            /* A click anywhere else dismisses the popup, and the bar still
             * gets to answer it below: clicking another button swaps menus
             * in one press, the way the official client's do. */
            b->menu = VIEW_UI_MENU_NONE;
        }
        hit = view_ui_bar_hit(&L, ev->button.x, ev->button.y);
        if (hit >= 0) {
            const struct view_ui_item *it = view_ui_bar_item(hit);

            /* Clicking the button whose popup is already up closes it and
             * leaves it closed, the dismiss above already did the work,
             * and acting again would reopen the menu the press meant to
             * put away. */
            if (!(it->act == VIEW_UI_ACT_MENU && it->arg == was))
                act(b, it, &L.btn[hit]);
            return 1;
        }
        return view_ui_hit(&L.bar, ev->button.x, ev->button.y);
    }

    if (ev->type == SDL_MOUSEWHEEL || ev->type == SDL_MOUSEBUTTONUP) {
        int mx = 0, my = 0;

        SDL_GetMouseState(&mx, &my);
        if (b->menu != VIEW_UI_MENU_NONE && view_ui_hit(&M.box, mx, my))
            return 1;
        return view_ui_hit(&L.bar, mx, my);
    }
    return 0;
}

static int bar_owns_pointer(void *state, const struct view_ui_frame *f,
                            int mx, int my)
{
    struct view_ui_bar *b = state;
    struct view_ui_bar_layout L;
    struct view_ui_menu_layout M;

    if (!bar_live(b))
        return 0;
    if (b->confirm)
        return 1;
    laid_out(b, f, &L, &M);
    if (b->menu != VIEW_UI_MENU_NONE && view_ui_hit(&M.box, mx, my))
        return 1;
    return view_ui_hit(&L.bar, mx, my);
}

struct view_ui_element view_ui_bar_element(struct view_ui_bar *b)
{
    struct view_ui_element el;

    memset(&el, 0, sizeof el);
    el.name = "bar";
    el.state = b;
    el.draw = bar_draw;
    el.event = bar_event;
    el.owns_pointer = bar_owns_pointer;
    return el;
}
