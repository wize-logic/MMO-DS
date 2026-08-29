/* See view_ui_party.h. Placement and the hp colours are
 * view_ui_game.c's (SDL-free, held by tests/view_ui_test.c); this binds them
 * to the page and the renderer. */

#include "view_ui_party.h"

#include <stdio.h>
#include <string.h>

#include "status_channel.h"

void view_ui_party_init(struct view_ui_party *p, struct view_hud *hud)
{
    memset(p, 0, sizeof *p);
    p->hud = hud;
    p->press_slot = -1;
}

/* How far a press travels before it is a drag and not a click. */
#define PARTY_DRAG_PX 6

/* One slot's card, painted into the given rects: the button ground (washed
 * purple for a fainted monster), the name and level, the numbers, the bar.
 * The strip's loop paints each in place; the drag ghost paints the held one
 * again under the cursor, so both are one brush. */
static void draw_slot(SDL_Renderer *ren, struct view_ui_gpu *g,
                      const struct openmmo_hud_party *m, int ground_th,
                      const struct openmmo_rect *slot,
                      const struct openmmo_rect *name,
                      const struct openmmo_rect *hp_r,
                      const struct openmmo_rect *bar)
{
    char text[32];
    int hp = (int)m->hp, max = (int)m->max_hp, w;
    int fainted = !m->egg && max > 0 && hp <= 0;
    SDL_Color c;

    if (fainted ? !view_ui_th_mod(ren, g, VIEW_UI_TH_BUTTON, slot,
                                  0x9781CFu, 255u)
                : !view_ui_th(ren, g, ground_th, slot))
        view_ui_panel(ren, slot, VIEW_UI_COL_FIELD,
                      ground_th == VIEW_UI_TH_BUTTON_DOWN
                          ? VIEW_UI_COL_ACCENT
                          : VIEW_UI_COL_LINE);
    view_ui_text_in(ren, g, name, 0, 0, m->egg ? "Egg" : m->name,
                    VIEW_UI_COL_TEXT);
    if (!m->egg) {
        snprintf(text, sizeof text, "Lv %u", (unsigned)m->level);
        view_ui_text_in(ren, g, name, 0, 1, text, VIEW_UI_COL_DIM);
        if (max > 0)
            snprintf(text, sizeof text, "%d / %d", hp, max);
        else
            snprintf(text, sizeof text, "%d", hp);
        view_ui_text_in(ren, g, hp_r, 0, 0, text, VIEW_UI_COL_DIM);
        if (m->status != 0) {
            /* The engine keeps sleep as a counter in the low bits and
             * every other condition as its own flag, so the strip says
             * that a monster has one without naming it. */
            view_ui_text_in(ren, g, hp_r, 0, 1, "STA", VIEW_UI_COL_ACCENT);
        }
    }

    /* The bar's trough first, so an empty one still reads as a bar. */
    if (!view_ui_th(ren, g, VIEW_UI_TH_HPBAR, bar)) {
        c = view_ui_col(VIEW_UI_COL_LINE);
        view_ui_fill(ren, bar->x, bar->y, bar->w, bar->h, c.r, c.g, c.b, 255);
    }
    if (!m->egg) {
        w = view_ui_hp_width(hp, max, bar->w);
        c = view_ui_col(view_ui_hp_colour(hp, max));
        if (w > 0)
            view_ui_fill(ren, bar->x, bar->y, w, bar->h, c.r, c.g, c.b, 255);
    }
}

/* Shift one of the layout's rects to where the ghost is. */
static struct openmmo_rect at(const struct openmmo_rect *r, int dx, int dy)
{
    struct openmmo_rect out = *r;

    out.x += dx;
    out.y += dy;
    return out;
}

/* Up while the session is in the world and there is a party to show. */
static int party_live(const struct view_ui_party *p)
{
    if (p == NULL || p->hud == NULL || !p->hud->any || p->hidden)
        return 0;
    return p->hud->snap.net.state == OPENMMO_ST_IN_GAME &&
           p->hud->snap.party_n > 0;
}

static void party_draw(void *state, SDL_Renderer *ren, struct view_ui_gpu *g,
                       const struct view_ui_frame *f)
{
    struct view_ui_party *p = state;
    const struct openmmo_hud_snap *s;
    struct view_ui_partybar_layout L;
    SDL_Rect saved;
    SDL_BlendMode old;
    int i, want, mx = 0, my = 0;

    if (!party_live(p))
        return;
    s = &p->hud->snap;
    view_ui_partybar_place(&f->canvas, (int)s->party_n, &L);
    if (L.n == 0 || !view_ui_font_ready(g, ren, L.text_px))
        return;

    SDL_GetMouseState(&mx, &my);
    want = view_ui_hit(&L.box, mx, my) ? 240 : 176;
    /* All the way out, not a wash: during a trade or a battle the guest's
     * second screen is the interaction surface, and the strip sits on it. */
    if (s->guest_busy)
        want = 0;
    p->alpha = view_ui_fade(p->alpha, want, 12, &p->faded);
    if (p->alpha == 0)
        return;
    view_ui_alpha((unsigned)p->alpha);

    SDL_GetRenderDrawBlendMode(ren, &old);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    view_ui_clip_push(ren, &L.box, &saved);
    if (!view_ui_th(ren, g, VIEW_UI_TH_PANEL, &L.box))
        view_ui_panel(ren, &L.box, VIEW_UI_COL_GROUND, VIEW_UI_COL_LINE);

    for (i = 0; i < L.n; i++) {
        /* A slot is the official client's own button ground (party.xml monster-slot).
         * Mid-drag the held slot is pressed and the slot under the pointer
         * washes, so the swap about to happen reads before it is let go. */
        draw_slot(ren, g, &s->party[i],
                  p->dragging && i == p->press_slot ? VIEW_UI_TH_BUTTON_DOWN
                                                    : VIEW_UI_TH_BUTTON,
                  &L.slot[i], &L.name[i], &L.hp[i], &L.bar[i]);
        if (p->dragging && i != p->press_slot &&
            view_ui_hit(&L.slot[i], mx, my))
            view_ui_th(ren, g, VIEW_UI_TH_BUTTON_HOVER, &L.slot[i]);
    }

    view_ui_clip_pop(ren, &saved);

    /* The ghost: the held card again, mostly transparent under the cursor
     * at its own grab point, the way a dragged file rides the pointer.
     * After the clip, so it follows the pointer off the strip. */
    if (p->dragging && p->press_slot >= 0 && p->press_slot < L.n) {
        int i2 = p->press_slot;
        int dx = (mx - p->press_off_x) - L.slot[i2].x;
        int dy = (my - p->press_off_y) - L.slot[i2].y;
        struct openmmo_rect gs = at(&L.slot[i2], dx, dy);
        struct openmmo_rect gn = at(&L.name[i2], dx, dy);
        struct openmmo_rect gh = at(&L.hp[i2], dx, dy);
        struct openmmo_rect gb = at(&L.bar[i2], dx, dy);

        view_ui_alpha(96);
        draw_slot(ren, g, &s->party[i2], VIEW_UI_TH_BUTTON, &gs, &gn, &gh,
                  &gb);
    }

    SDL_SetRenderDrawBlendMode(ren, old);
    view_ui_alpha(255);
}

static int party_event(void *state, const SDL_Event *ev,
                       const struct view_ui_frame *f)
{
    struct view_ui_party *p = state;
    struct view_ui_partybar_layout L;

    if (p == NULL || p->hud == NULL || !p->hud->any || ev == NULL)
        return 0;
    /* A faded-out strip must not eat the click that was aimed at the
     * guest's screen under it; a drag in flight is simply dropped. */
    if (p->hud->snap.guest_busy) {
        p->dragging = 0;
        p->press_slot = -1;
        return 0;
    }

    /* O folds the strip away and back. The official client's own party frame has a lock
     * and a hide button in its title bar; this client's strip has no title
     * bar to put them in, so the key is the whole of it. */
    if (ev->type == SDL_KEYDOWN && !ev->key.repeat && !f->typing &&
        !p->hud->snap.composing && ev->key.keysym.sym == SDLK_o &&
        (ev->key.keysym.mod & (KMOD_ALT | KMOD_CTRL | KMOD_GUI)) == 0) {
        p->hidden = !p->hidden;
        return 1;
    }
    if (!party_live(p))
        return 0;
    view_ui_partybar_place(&f->canvas, (int)p->hud->snap.party_n, &L);

    /*
     * The official client's slots are where a monster is dragged to reorder the party; these are too. A
     * press arms; a pointer that travels becomes a drag, and a release on another slot is the
     * wire's one move.
     */
    if (ev->type == SDL_MOUSEBUTTONDOWN &&
        ev->button.button == SDL_BUTTON_LEFT &&
        view_ui_hit(&L.box, ev->button.x, ev->button.y)) {
        p->press_slot = view_ui_partybar_hit(&L, ev->button.x, ev->button.y);
        p->press_x = ev->button.x;
        p->press_y = ev->button.y;
        if (p->press_slot >= 0) {
            p->press_off_x = ev->button.x - L.slot[p->press_slot].x;
            p->press_off_y = ev->button.y - L.slot[p->press_slot].y;
        }
        p->dragging = 0;
        return 1;
    }
    if (ev->type == SDL_MOUSEMOTION && p->press_slot >= 0) {
        int dx = ev->motion.x - p->press_x, dy = ev->motion.y - p->press_y;

        if (dx * dx + dy * dy >= PARTY_DRAG_PX * PARTY_DRAG_PX)
            p->dragging = 1;
        return 1;
    }
    if (ev->type == SDL_MOUSEBUTTONUP &&
        ev->button.button == SDL_BUTTON_LEFT &&
        (p->press_slot >= 0 ||
         view_ui_hit(&L.box, ev->button.x, ev->button.y))) {
        int was = p->press_slot, dragged = p->dragging;
        int slot = view_ui_partybar_hit(&L, ev->button.x, ev->button.y);
        int in_box = view_ui_hit(&L.box, ev->button.x, ev->button.y);

        p->press_slot = -1;
        p->dragging = 0;
        if (dragged) {
            if (was >= 0 && slot >= 0 && slot != was)
                view_hud_push(p->hud, OPENMMO_HUD_CMD_PARTY_MOVE,
                              was | (slot << 8));
            return 1;
        }
        if (!in_box)
            return 1;
        if (slot >= 0)
            view_hud_push(p->hud, OPENMMO_HUD_CMD_SCREEN,
                          OPENMMO_HUD_SCREEN_SUMMARY | (slot << 8));
        else
            view_hud_push(p->hud, OPENMMO_HUD_CMD_SCREEN,
                          OPENMMO_HUD_SCREEN_PARTY);
        return 1;
    }
    return 0;
}

static int party_owns_pointer(void *state, const struct view_ui_frame *f,
                              int mx, int my)
{
    struct view_ui_party *p = state;
    struct view_ui_partybar_layout L;

    if (!party_live(p))
        return 0;
    if (p->hud->snap.guest_busy)
        return 0;
    if (p->press_slot >= 0)
        return 1;
    view_ui_partybar_place(&f->canvas, (int)p->hud->snap.party_n, &L);
    return view_ui_hit(&L.box, mx, my);
}

struct view_ui_element view_ui_party_element(struct view_ui_party *p)
{
    struct view_ui_element el;

    memset(&el, 0, sizeof el);
    el.name = "party";
    el.state = p;
    el.draw = party_draw;
    el.event = party_event;
    el.owns_pointer = party_owns_pointer;
    return el;
}
