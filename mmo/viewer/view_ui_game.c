/* See view_ui_game.h. SDL-free; tests/view_ui_test.c holds
 * every rectangle in it. */

#include "view_ui_game.h"

#include <stdio.h>
#include <string.h>

#include "status_channel.h"

int view_ui_scale(int v, int text_px)
{
    if (text_px < 1)
        text_px = VIEW_UI_OFFICIAL_PX;
    return v * text_px / VIEW_UI_OFFICIAL_PX;
}

int view_ui_scroll_clamp(int scroll, int total, int rows)
{
    int max = total - rows;

    if (max < 0)
        max = 0;
    return view_ui_clampi(scroll, 0, max);
}

/* Measure a whole label. With no font, the suite, and any element that
 * draws before the glyph cache is up, a character is two thirds of the type
 * size, which is DejaVu Sans's own average advance and keeps the bar the
 * shape it will be once the face loads. */
static int label_w(view_ui_measure measure, void *ctx, const char *s,
                   int text_px)
{
    int n = (int)strlen(s);

    if (measure != NULL)
        return measure(ctx, s, n);
    return n * text_px * 2 / 3;
}

/* ------------------------------------------------------------------ */
/* The tables                                                          */
/* ------------------------------------------------------------------ */

/*
 * f/x71's ten, in the order it lays them out, with the official client's own labels and the keys
 * config/main.properties binds by default, and two of ours before the last: the Poketch, which
 * is the game's own lower screen, and the Pokegear, HeartGold's own device.
 */
static const struct view_ui_item bar_items[VIEW_UI_BAR_N] = {
    { "Bag",     "Bag",  'b', VIEW_UI_ACT_SCREEN, OPENMMO_HUD_SCREEN_BAG,
      NULL },
#if OPENMMO_PIN_DEV_FEATURES
    /* The official client opens the card outright. A working build asks which region's,
     * because a ported region's badges are a card of their own
     * (VIEW_UI_MENU_CARD); a release is a Sinnoh client and opens the card
     * the way the official client does. */
    { "Trainer", "Trnr", 'c', VIEW_UI_ACT_MENU, VIEW_UI_MENU_CARD, NULL },
#else
    { "Trainer", "Trnr", 'c', VIEW_UI_ACT_SCREEN,
      OPENMMO_HUD_SCREEN_TRAINER | (OPENMMO_HUD_CARD_SINNOH << 8), NULL },
#endif
    { "Community", "Comm", 0, VIEW_UI_ACT_MENU, VIEW_UI_MENU_COMMUNITY,
      NULL },
    { "PvP",     "PvP",    0, VIEW_UI_ACT_MENU, VIEW_UI_MENU_PVP, NULL },
    { "Pokedex", "Dex",  'n', VIEW_UI_ACT_SCREEN, OPENMMO_HUD_SCREEN_DEX,
      NULL },
    { "Egg Incubators", "Eggs", 'i', VIEW_UI_ACT_NOTICE, 0,
      "The Egg Incubator has not been unlocked yet." },
    { "Trade",   "GTL",  'p', VIEW_UI_ACT_WINDOW, VIEW_UI_WIN_GTL, NULL },
    { "Mail",    "Mail",   0, VIEW_UI_ACT_WINDOW, VIEW_UI_WIN_MAIL, NULL },
    { "Gift Shop", "Shop", 0, VIEW_UI_ACT_NOTICE, 0,
      "Gift shop is not available at this time." },
    /* Not the official client's either, and not a screen the guest is asked for: the
     * Poketch is already running down there, and this says whether the
     * window draws it. K is a key the official client leaves free. Every build has it, 
     * the device is Sinnoh's and so is the release. */
    { "Poketch", "Ptch", 'k', VIEW_UI_ACT_POKETCH, 0, NULL },
#if OPENMMO_PIN_DEV_FEATURES
    /* Not the official client's: HeartGold's device, drawn by the guest from the package
     * that carries Johto (mods/openmmo/src/openmmo_pokegear.c). M is a key
     * the official client leaves free. A working build's button; a release is a Sinnoh
     * client and has none (endpoint_pin.h, DEV_FEATURES). */
    { "Pokegear", "Gear", 'm', VIEW_UI_ACT_SCREEN, OPENMMO_HUD_SCREEN_POKEGEAR,
      NULL },
#endif
    { "Menu",    "Menu", 'd', VIEW_UI_ACT_MENU, VIEW_UI_MENU_GAME, NULL }
};

/* f/NA0's three popups, entry for entry. The Pokedex button's own popup is
 * not here: official raises it only where the account has the encounter
 * tracker, and takes the official client, the dex itself, otherwise, which is the
 * branch this client is always on. */
static const struct view_ui_menu_def menus[VIEW_UI_MENU_N] = {
    { NULL, 0, { { NULL, NULL, 0, 0, 0, NULL } } },
    { "Community", 5, {
        { "Friends", NULL, 'v', VIEW_UI_ACT_WINDOW, VIEW_UI_WIN_FRIENDS,
          NULL },
        { "Team", NULL, 'g', VIEW_UI_ACT_WINDOW, VIEW_UI_WIN_TEAM, NULL },
        { "Change Channel", NULL, 0, VIEW_UI_ACT_NOTICE, 0, NULL },
        { "Nearby Players", NULL, 0, VIEW_UI_ACT_WINDOW, VIEW_UI_WIN_NEARBY,
          NULL },
        /* The official client's fifth entry is a submenu of the party (the official client), not a
         * row. This layer has no nested popup and the follower is not the
         * window's to set, so it is the row and says so. */
        { "Select Follower", NULL, 0, VIEW_UI_ACT_NOTICE, 0, NULL }
      } },
    /* The official client's fifth PvP entry, Team Tournament, is behind a feature flag
     * (the official client) and is absent from a default the official client session, so it is
     * absent here too. */
    { "PvP", 4, {
        { "Matchmaking Signup", NULL, 0, VIEW_UI_ACT_NOTICE, 0, NULL },
        { "Tournament Signup",  NULL, 0, VIEW_UI_ACT_NOTICE, 0, NULL },
        { "Tournaments",        NULL, 0, VIEW_UI_ACT_NOTICE, 0, NULL },
        { "PvP Statistics",     NULL, 0, VIEW_UI_ACT_NOTICE, 0, NULL }
      } },
    /*
     * Still no logout row. The action behind it is a fresh boot of the program, which the
     * desktop does by exec and an app cannot do at all, and the attempt to give the app one
     * of its own disturbed the running session in ways nobody wanted before a release.
     */
    { "Menu", 9, {
        /*
         * Saving, which offline is the one thing a player must not miss and had no button
         * anywhere.
         */
        { "Save", NULL, 0, VIEW_UI_ACT_SCREEN, OPENMMO_HUD_SCREEN_START,
          NULL },
        { "Party", NULL, 0, VIEW_UI_ACT_SCREEN, OPENMMO_HUD_SCREEN_PARTY,
          NULL },
        { "Settings", NULL, 0, VIEW_UI_ACT_SCREEN, OPENMMO_HUD_SCREEN_OPTIONS,
          NULL },
        { "FAQ", NULL, 'h', VIEW_UI_ACT_NOTICE, 0, NULL },
        { "Customization", NULL, 0, VIEW_UI_ACT_NOTICE, 0, NULL },
        { "Instance Info", NULL, 0, VIEW_UI_ACT_WINDOW, VIEW_UI_WIN_INSTANCE,
          NULL },
        { "Support Request", NULL, 0, VIEW_UI_ACT_NOTICE, 0, NULL },
        { "Continue Offline", NULL, 0, VIEW_UI_ACT_EXPORT, 0,
          "Continue Offline is not available at this time." },
        { "Exit", NULL, 0, VIEW_UI_ACT_QUIT, 0, NULL }
      } },
    /* The two cards. The screen command's slot byte carries the region
     * (hud_channel.h): 0 is this engine's own case, 1 the ported regions'
     * sixteen, drawn by the same case (mods/openmmo/src/openmmo_card.c). */
    { "Trainer Card", 2, {
        { "Sinnoh", NULL, 0, VIEW_UI_ACT_SCREEN,
          OPENMMO_HUD_SCREEN_TRAINER | (OPENMMO_HUD_CARD_SINNOH << 8), NULL },
        { "Johto & Kanto", NULL, 0, VIEW_UI_ACT_SCREEN,
          OPENMMO_HUD_SCREEN_TRAINER | (OPENMMO_HUD_CARD_JOHTO_KANTO << 8),
          NULL }
      } }
};

/* The official client's own sizes where official opens the window (the official client 470x320,
 * the official client 485x300, f/Gx0 550x370). Nearby Players is ours: the official client defers
 * its list to a frame this decompile does not resolve, so it takes the
 * friends window's shape, which is the same kind of list. */
static const struct view_ui_win_def wins[VIEW_UI_WIN_N] = {
    { "Friends",           470, 320 },
    { "Team",              485, 300 },
    { "Nearby Players",    470, 320 },
    { "Instance Info",     550, 370 },
    /* The official client's broker-window: minWidth 900, minHeight 605 (broker.xml).
     * The body is view_ui_gtl.c's; only the frame is the family's. */
    { "Global Trade Link", 900, 605 },
    /* Mail is built in code (f/kW0, f/rQ0), not from a theme xml, so this
     * size is ours: the official client's own table is 420 of columns (135 sender, 135
     * subject, 90 date, 60), and the reader and the Recipient / Subject /
     * Body form need the height. The body is view_ui_mail.c's. */
    { "Mail", 660, 520 }
};

/* Offline the window is the same window, minus everything that would have to
 * ask a server. It is set once from the command line and never changes, so
 * the layout below can read it as a constant of the run. */
static int g_offline;

void view_ui_offline_set(int on)
{
    g_offline = on ? 1 : 0;
}

int view_ui_offline(void)
{
    return g_offline;
}

int view_ui_bar_shown(int i)
{
    if (i < 0 || i >= VIEW_UI_BAR_N)
        return 0;
    if (!g_offline)
        return 1;
    /* The four with nobody on the other end. Bag, Trainer, Pokedex and Menu
     * are the engine's own screens and are drawn from the save file; the
     * Poketch is a window preference; the two notices are notices. */
    return i != VIEW_UI_BAR_COMMUNITY && i != VIEW_UI_BAR_PVP
        && i != VIEW_UI_BAR_TRADE && i != VIEW_UI_BAR_MAIL
        /*
         * And the two that are only ever a sentence about the service: "the Egg Incubator has
         * not been unlocked yet" and "gift shop is not available at this time" are answers
         * about an account, and there is no account here.
         */
        && i != VIEW_UI_BAR_INCUBATOR && i != VIEW_UI_BAR_SHOP;
}

/* The same question a row at a time. */
int view_ui_menu_row_shown(int id, int row)
{
    const struct view_ui_menu_def *d = view_ui_menu_def(id);

    if (d == NULL || row < 0 || row >= d->n)
        return 0;
    if (id != VIEW_UI_MENU_GAME)
        return 1;
    if (d->item[row].act == VIEW_UI_ACT_SCREEN
        && d->item[row].arg == OPENMMO_HUD_SCREEN_START)
        return g_offline;           /* Save: offline is the only side with one */
    if (!g_offline)
        return 1;
    return d->item[row].act == VIEW_UI_ACT_SCREEN
        || d->item[row].act == VIEW_UI_ACT_QUIT;
}

/* Whether the bar is up at all. */
int view_ui_bar_live(const struct openmmo_hud_snap *s)
{
    if (s == NULL)
        return 0;
    if (g_offline)
        return s->lower == OPENMMO_HUD_LOWER_POKETCH;
    return s->net.state == OPENMMO_ST_IN_GAME;
}

const struct view_ui_item *view_ui_bar_item(int i)
{
    if (i < 0 || i >= VIEW_UI_BAR_N)
        return NULL;
    return &bar_items[i];
}

const char *view_ui_bar_label(int i, int shortened)
{
    const struct view_ui_item *it = view_ui_bar_item(i);

    if (it == NULL)
        return "";
    if (shortened && it->shortl != NULL)
        return it->shortl;
    return it->label;
}

const struct view_ui_menu_def *view_ui_menu_def(int id)
{
    if (id <= VIEW_UI_MENU_NONE || id >= VIEW_UI_MENU_N)
        return NULL;
    return &menus[id];
}

const struct view_ui_win_def *view_ui_win_def(int id)
{
    if (id < 0 || id >= VIEW_UI_WIN_N)
        return NULL;
    return &wins[id];
}

void view_ui_notice_text(const struct view_ui_item *it, char *dst, size_t cap)
{
    if (dst == NULL || cap == 0)
        return;
    if (it == NULL || it->label == NULL) {
        snprintf(dst, cap, "You cannot do that now.");
        return;
    }
    if (it->notice != NULL) {
        snprintf(dst, cap, "%s", it->notice);
        return;
    }
    snprintf(dst, cap, "%s is not available at this time.", it->label);
}

/* ------------------------------------------------------------------ */
/* The bar                                                             */
/* ------------------------------------------------------------------ */

void view_ui_bar_place(const struct openmmo_rect *canvas,
                       view_ui_measure measure, void *ctx,
                       struct view_ui_bar_layout *out)
{
    struct openmmo_rect c;
    int w[VIEW_UI_BAR_N];
    int i, min_w, icon, margin, total, avail, x;
    int shown = 0;

    if (out == NULL)
        return;
    memset(out, 0, sizeof *out);
    c.x = 0; c.y = 0; c.w = 1; c.h = 1;
    if (canvas != NULL)
        c = *canvas;
    if (c.w < 1) c.w = 1;
    if (c.h < 1) c.h = 1;

    out->text_px = view_ui_text_px(c.y + c.h);
    out->pad = view_ui_scale(7, out->text_px);   /* hud-panel border 7 */
    out->gap = view_ui_scale(5, out->text_px);   /* hud-panel defaultGap */
    out->btn_h = view_ui_scale(32, out->text_px);
    min_w = view_ui_scale(66, out->text_px);
    /* f/HU sizes a button textWidth + 35: the 24px icon reel plus its
     * margins. The reel is official art and is not shipped, so the same 35
     * is the label's own breathing room here. */
    icon = view_ui_scale(35, out->text_px);
    margin = view_ui_scale(10, out->text_px);

    /* What is left of the floor once the chat box has had its share. */
    if (g_offline) {
        avail = c.w - 2 * margin;
    } else {
        struct view_ui_chat_layout chat;

        view_ui_chat_place(&c, &chat);
        avail = (c.x + c.w - margin) - (chat.box.x + chat.box.w) - out->gap;
    }
    if (avail > c.w - 2 * margin)
        avail = c.w - 2 * margin;
    if (avail < 1)
        avail = c.w;

    /* A hidden button is width 0 and takes no gap either, so the strip is as
     * wide as the buttons that are on it rather than carrying the holes where
     * the others were. */
    total = 0;
    for (i = 0; i < VIEW_UI_BAR_N; i++) {
        if (!view_ui_bar_shown(i)) {
            w[i] = 0;
            continue;
        }
        shown++;
        w[i] = label_w(measure, ctx, bar_items[i].label, out->text_px) + icon;
        if (w[i] < min_w)
            w[i] = min_w;
        total += w[i];
    }
    if (shown < 1)
        return;
    total += (shown - 1) * out->gap + 2 * out->pad;

    /* Full labels first; the short forms next, floored at what one of them
     * needs rather than at the official client's icon-sized 66. */
    if (total > avail) {
        int floor_w = view_ui_scale(24, out->text_px);

        out->shortened = 1;
        total = 0;
        for (i = 0; i < VIEW_UI_BAR_N; i++) {
            if (w[i] == 0)
                continue;
            w[i] = label_w(measure, ctx, view_ui_bar_label(i, 1),
                           out->text_px) + 2 * out->text_px;
            if (w[i] < floor_w)
                w[i] = floor_w;
            total += w[i];
        }
        total += (shown - 1) * out->gap + 2 * out->pad;
    }

    /* A window too narrow even for those keeps every button rather than
     * dropping the last three off the edge: the row shrinks evenly and the
     * labels clip inside their own rects. */
    if (total > avail) {
        int room = avail - (shown - 1) * out->gap - 2 * out->pad;
        int sum = 0;

        if (room < shown)
            room = shown;
        for (i = 0; i < VIEW_UI_BAR_N; i++)
            sum += w[i];
        for (i = 0; i < VIEW_UI_BAR_N; i++) {
            if (w[i] == 0)
                continue;
            w[i] = w[i] * room / (sum > 0 ? sum : 1);
            if (w[i] < 1)
                w[i] = 1;
        }
        total = 0;
        for (i = 0; i < VIEW_UI_BAR_N; i++)
            total += w[i];
        total += (shown - 1) * out->gap + 2 * out->pad;
    }

    out->bar = view_ui_place(&c, VIEW_UI_BOTTOM_RIGHT, margin, total,
                             out->btn_h + 2 * out->pad);
    x = out->bar.x + out->pad;
    for (i = 0; i < VIEW_UI_BAR_N; i++) {
        int right = out->bar.x + out->bar.w - out->pad;

        /*
         * A hidden button is placed with no width rather than left at the origin: view_ui_hit
         * refuses a rect narrower than a pixel, so it is not drawn, not hit and not tabbed to,
         * and every property the strip has, inside the bar, left to right, never overlapping,
         * still holds over the row as a whole.
         */
        if (w[i] == 0) {
            out->btn[i].x = x;
            out->btn[i].y = out->bar.y + out->pad;
            out->btn[i].w = 0;
            out->btn[i].h = 0;
            continue;
        }
        out->btn[i].x = x;
        out->btn[i].y = out->bar.y + out->pad;
        out->btn[i].w = w[i];
        out->btn[i].h = out->btn_h;
        /* The even shrink above rounds up by at most one pixel a button, and
         * the strip itself is clamped to the canvas, so the last button is
         * cut to the strip rather than allowed to hang out of it. */
        if (out->btn[i].x > right)
            out->btn[i].x = right;
        if (out->btn[i].x + out->btn[i].w > right)
            out->btn[i].w = right - out->btn[i].x;
        if (out->btn[i].w < 0)
            out->btn[i].w = 0;
        if (out->btn[i].h > out->bar.h - 2 * out->pad)
            out->btn[i].h = out->bar.h - 2 * out->pad;
        if (out->btn[i].h < 0)
            out->btn[i].h = 0;
        x += w[i] + out->gap;
    }
}

int view_ui_bar_hit(const struct view_ui_bar_layout *L, int x, int y)
{
    int i;

    if (L == NULL)
        return -1;
    for (i = 0; i < VIEW_UI_BAR_N; i++)
        if (view_ui_hit(&L->btn[i], x, y))
            return i;
    return -1;
}

/* ------------------------------------------------------------------ */
/* The popup menus                                                     */
/* ------------------------------------------------------------------ */

void view_ui_menu_place(const struct openmmo_rect *canvas,
                        const struct openmmo_rect *anchor, int id,
                        view_ui_measure measure, void *ctx,
                        struct view_ui_menu_layout *out)
{
    const struct view_ui_menu_def *d = view_ui_menu_def(id);
    struct openmmo_rect c;
    int i, w = 0, h, y, shown = 0;

    if (out == NULL)
        return;
    memset(out, 0, sizeof *out);
    if (d == NULL)
        return;
    c.x = 0; c.y = 0; c.w = 1; c.h = 1;
    if (canvas != NULL)
        c = *canvas;
    if (c.w < 1) c.w = 1;
    if (c.h < 1) c.h = 1;

    out->text_px = view_ui_text_px(c.y + c.h);
    out->pad = view_ui_scale(6, out->text_px);
    out->row_h = view_ui_scale(22, out->text_px);
    /* `n` stays the whole table so a row id still names the same item, hidden
     * or not; `shown` is what the popup is actually as tall as. A hidden row
     * measures nothing either, a label nobody can read must not be what
     * decides the width. */
    out->n = d->n;
    for (i = 0; i < d->n; i++) {
        int lw;

        if (!view_ui_menu_row_shown(id, i))
            continue;
        shown++;
        lw = label_w(measure, ctx, d->item[i].label, out->text_px);
        if (d->item[i].key != 0)
            lw += view_ui_scale(28, out->text_px);
        if (lw > w)
            w = lw;
    }
    if (shown < 1)
        shown = 1;
    w += 2 * out->pad + view_ui_scale(16, out->text_px);
    /* The official client's own floor (init.xml menupopup: button minWidth 80). */
    if (w < view_ui_scale(80, out->text_px))
        w = view_ui_scale(80, out->text_px);
    if (w > c.w) w = c.w;
    if (2 * out->pad + 1 > w)
        out->pad = (w - 1) / 2;
    if (out->pad < 0)
        out->pad = 0;
    /* Every entry stays reachable on a canvas too short for the official client's row
     * height: the rows get shorter rather than the last two falling off the
     * bottom, which is the failure a popup cannot have. */
    {
        int avail = c.h - 2 * out->pad;

        if (avail < shown) {
            out->pad = 0;
            avail = c.h;
        }
        if (shown * out->row_h > avail)
            out->row_h = avail / shown;
        if (out->row_h < 1)
            out->row_h = 1;
    }
    h = shown * out->row_h + 2 * out->pad;
    if (h > c.h) h = c.h;

    /*
     * Above the button that opened it, left edges aligned, then pulled inside the canvas whole,
     * the bar sits on the floor, so a popup that opened downward would be off-screen every
     * time.
     */
    {
        int margin = view_ui_scale(10, out->text_px);
        int lo_x = c.x, hi_x = c.x + c.w, lo_y = c.y, hi_y = c.y + c.h;

        if (w + 2 * margin <= c.w) {
            lo_x += margin;
            hi_x -= margin;
        }
        if (h + 2 * margin <= c.h) {
            lo_y += margin;
            hi_y -= margin;
        }
        out->box.w = w;
        out->box.h = h;
        out->box.x = (anchor != NULL) ? anchor->x : lo_x;
        out->box.y = (anchor != NULL) ? anchor->y - h - out->pad : lo_y;
        if (out->box.x + out->box.w > hi_x)
            out->box.x = hi_x - out->box.w;
        if (out->box.x < lo_x)
            out->box.x = lo_x;
        if (out->box.y + out->box.h > hi_y)
            out->box.y = hi_y - out->box.h;
        if (out->box.y < lo_y)
            out->box.y = lo_y;
    }

    y = out->box.y + out->pad;
    for (i = 0; i < d->n; i++) {
        if (!view_ui_menu_row_shown(id, i)) {
            /* An empty rectangle, which nothing can hit and nothing draws:
             * the row keeps its index and takes no floor. */
            memset(&out->row[i], 0, sizeof out->row[i]);
            continue;
        }
        out->row[i].x = out->box.x + out->pad;
        out->row[i].y = y;
        out->row[i].w = out->box.w - 2 * out->pad;
        if (out->row[i].w < 1)
            out->row[i].w = 1;
        out->row[i].h = out->row_h;
        y += out->row_h;
    }
}

int view_ui_menu_hit(const struct view_ui_menu_layout *L, int x, int y)
{
    int i;

    if (L == NULL)
        return -1;
    for (i = 0; i < L->n && i < VIEW_UI_MENU_ROWS; i++)
        if (view_ui_hit(&L->row[i], x, y))
            return i;
    return -1;
}

/* ------------------------------------------------------------------ */
/* The frames                                                          */
/* ------------------------------------------------------------------ */

void view_ui_frame_place(const struct openmmo_rect *canvas, int id,
                         int drag_x, int drag_y,
                         struct view_ui_frame_layout *out)
{
    const struct view_ui_win_def *d = view_ui_win_def(id);
    struct openmmo_rect c;
    int w, h, close_w;

    if (out == NULL)
        return;
    memset(out, 0, sizeof *out);
    if (d == NULL)
        return;
    c.x = 0; c.y = 0; c.w = 1; c.h = 1;
    if (canvas != NULL)
        c = *canvas;
    if (c.w < 1) c.w = 1;
    if (c.h < 1) c.h = 1;

    out->text_px = view_ui_text_px(c.y + c.h);
    out->pad = view_ui_scale(8, out->text_px);
    out->title_h = view_ui_scale(30, out->text_px); /* resizableframe border */
    out->row_h = out->text_px + view_ui_scale(8, out->text_px);

    w = view_ui_scale(d->w, out->text_px);
    h = view_ui_scale(d->h, out->text_px);
    if (w > c.w) w = c.w;
    if (h > c.h) h = c.h;
    out->box.w = w;
    out->box.h = h;
    out->box.x = c.x + (c.w - w) / 2 + drag_x;
    out->box.y = c.y + (c.h - h) / 2 + drag_y;
    /* The title bar stays reachable: a frame dragged off an edge is pulled
     * back so its caption row is still inside the canvas. */
    if (out->box.x > c.x + c.w - out->title_h)
        out->box.x = c.x + c.w - out->title_h;
    if (out->box.x + out->box.w < c.x + out->title_h)
        out->box.x = c.x + out->title_h - out->box.w;
    if (out->box.y > c.y + c.h - out->title_h)
        out->box.y = c.y + c.h - out->title_h;
    if (out->box.y < c.y)
        out->box.y = c.y;

    /* Nothing below may leave the box, however little of it survived the
     * clamp: a canvas shorter than one title bar still gets a frame whose
     * parts are all inside it. */
    if (out->title_h > out->box.h)
        out->title_h = out->box.h;
    if (out->pad * 2 > out->box.w)
        out->pad = out->box.w / 2;
    if (out->pad < 0)
        out->pad = 0;

    out->title.x = out->box.x + view_ui_scale(11, out->text_px);
    out->title.y = out->box.y + view_ui_scale(1, out->text_px);
    out->title.w = out->box.w - view_ui_scale(11 + 30, out->text_px);
    out->title.h = view_ui_scale(24, out->text_px);
    if (out->title.w < 1)
        out->title.w = 1;

    if (out->title.x + out->title.w > out->box.x + out->box.w)
        out->title.w = out->box.x + out->box.w - out->title.x;
    if (out->title.w < 0)
        out->title.w = 0;
    if (out->title.h > out->title_h)
        out->title.h = out->title_h;

    close_w = view_ui_scale(14, out->text_px);
    if (close_w > out->title_h)
        close_w = out->title_h;
    if (close_w < 0)
        close_w = 0;
    out->close.w = close_w;
    out->close.h = close_w;
    out->close.x = out->box.x + out->box.w - view_ui_scale(24, out->text_px);
    out->close.y = out->box.y + view_ui_scale(6, out->text_px);
    if (out->close.x < out->box.x)
        out->close.x = out->box.x;
    if (out->close.x + close_w > out->box.x + out->box.w)
        out->close.x = out->box.x + out->box.w - close_w;
    if (out->close.y + close_w > out->box.y + out->title_h)
        out->close.y = out->box.y + out->title_h - close_w;
    if (out->close.y < out->box.y)
        out->close.y = out->box.y;

    /* The tap target around it. view_ui_game.h says why it is this much
     * bigger than the cross; the numbers are the title bar's own height and
     * a width that reaches the right edge, so it costs no geometry of its
     * own and cannot fall outside the frame. */
    {
        int hit_w = view_ui_scale(44, out->text_px);

        if (hit_w < close_w)
            hit_w = close_w;
        /*
         * Never the whole TITLE bar. A frame narrower than the target, only a canvas nobody
         * opens gets one, would otherwise have no draggable bar left at all, and "the title
         * bar can always be grabbed again" is the invariant the placement above exists to
         * keep.
         */
        if (hit_w > out->box.w / 2)
            hit_w = out->box.w / 2;
        if (hit_w < 0)
            hit_w = 0;
        out->close_hit.x = out->box.x + out->box.w - hit_w;
        out->close_hit.y = out->box.y;
        out->close_hit.w = hit_w;
        out->close_hit.h = out->title_h;
        /* A frame clamped down to nothing still gets a rect that at least
         * covers the cross, or the button it draws would be unpressable. */
        if (out->close_hit.x > out->close.x)
            out->close_hit.x = out->close.x;
        if (out->close_hit.x + out->close_hit.w
            < out->close.x + out->close.w) {
            out->close_hit.w = out->close.x + out->close.w
                             - out->close_hit.x;
        }
        if (out->close_hit.h < out->close.h)
            out->close_hit.h = out->close.h;
    }

    out->body.x = out->box.x + out->pad;
    out->body.y = out->box.y + out->title_h;
    out->body.w = out->box.w - 2 * out->pad;
    out->body.h = out->box.h - out->title_h - out->pad;
    if (out->body.w < 0) out->body.w = 0;
    if (out->body.h < 0) out->body.h = 0;
    if (out->row_h < 1) out->row_h = 1;
    /* Whole rows only, so no row is half-drawn at the bottom edge. */
    out->rows = out->body.h / out->row_h;
    out->body.h = out->rows * out->row_h;
}

int view_ui_frame_hit(const struct view_ui_frame_layout *L, int x, int y)
{
    struct openmmo_rect bar;

    if (L == NULL || !view_ui_hit(&L->box, x, y))
        return VIEW_UI_FRAME_HIT_NONE;
    if (view_ui_hit(&L->close_hit, x, y))
        return VIEW_UI_FRAME_HIT_CLOSE;
    bar = L->box;
    bar.h = L->title_h;
    if (view_ui_hit(&bar, x, y))
        return VIEW_UI_FRAME_HIT_TITLE;
    if (view_ui_hit(&L->body, x, y))
        return VIEW_UI_FRAME_HIT_BODY;
    return VIEW_UI_FRAME_HIT_NONE;
}

int view_ui_frame_row(const struct view_ui_frame_layout *L, int x, int y)
{
    int r;

    if (L == NULL || L->row_h < 1 || !view_ui_hit(&L->body, x, y))
        return -1;
    r = (y - L->body.y) / L->row_h;
    if (r < 0 || r >= L->rows)
        return -1;
    return r;
}

/* ------------------------------------------------------------------ */
/* Inside the frames                                                   */
/* ------------------------------------------------------------------ */

/* the official client's own labels: the social window's two tabs (strings 1650, 1660),
 * its action rows (1653, 1665) and the column headers (1651, 1659, 1662).
 * A window with no entry for a part simply does not grow that part. */
static const char *const social_tabs[] = { "Friends List", "Block List" };
static const char *const social_actions[] = { "Add Friend", "Block" };

const char *view_ui_win_tab_label(int id, int tab)
{
    if (tab < 0)
        return NULL;
    if (id == VIEW_UI_WIN_FRIENDS && tab < 2)
        return social_tabs[tab];
    return NULL;
}

const char *view_ui_win_action_label(int id, int tab)
{
    if (tab < 0)
        return NULL;
    if (id == VIEW_UI_WIN_FRIENDS && tab < 2)
        return social_actions[tab];
    return NULL;
}

void view_ui_win_columns(int id, int tab, const char **left,
                         const char **right)
{
    const char *l = NULL, *r = NULL;

    switch (id) {
    case VIEW_UI_WIN_FRIENDS:
        l = "Player Name";
        r = tab == 1 ? "Block Date" : "Last Online";
        break;
    case VIEW_UI_WIN_TEAM:
        l = "Player Name";
        r = "Last Online";
        break;
    case VIEW_UI_WIN_NEARBY:
        l = "Player Name";
        r = "";
        break;
    default: /* the instance table has no header (ui/instance.xml) */
        break;
    }
    if (left != NULL)  *left = l;
    if (right != NULL) *right = r;
}

void view_ui_win_body_place(const struct view_ui_frame_layout *L, int id,
                            int tab, struct view_ui_win_body *out)
{
    struct openmmo_rect area;
    int y, bottom, i;

    if (out == NULL)
        return;
    memset(out, 0, sizeof *out);
    if (L == NULL)
        return;
    out->text_px = L->text_px;
    out->row_h = L->row_h;
    out->pad = L->pad;

    /* Everything hangs inside the frame's own body region: below the title
     * band, inside the side padding, above the floor. */
    area.x = L->box.x + L->pad;
    area.w = L->box.w - 2 * L->pad;
    area.y = L->box.y + L->title_h;
    area.h = L->box.h - L->title_h - L->pad;
    if (area.w < 1) area.w = 1;
    if (area.h < 0) area.h = 0;
    y = area.y;
    bottom = area.y + area.h;

    if (view_ui_win_tab_label(id, 0) != NULL) {
        int tab_h = view_ui_scale(VIEW_UI_TAB_H, L->text_px);
        int gap = 1, x = area.x;
        int tn = 0, tw;

        /* Each frame grows exactly its own tabs: the social window's two,
         * the GTL's three. */
        while (tn < VIEW_UI_WIN_TABS &&
               view_ui_win_tab_label(id, tn) != NULL)
            tn++;
        tw = (area.w - (tn - 1) * gap) / tn;
        if (tab_h > bottom - y)
            tab_h = bottom - y;
        if (tw < 1)
            tw = 1;
        out->tab_n = tn;
        for (i = 0; i < tn; i++) {
            out->tab[i].x = x;
            out->tab[i].y = y;
            out->tab[i].w = tw;
            out->tab[i].h = tab_h;
            x += tw + gap;
        }
        y += tab_h;
    }

    {
        const char *l = NULL;

        view_ui_win_columns(id, tab, &l, NULL);
        if (l != NULL) {
            int hh = view_ui_scale(VIEW_UI_HEADER_H, L->text_px);

            if (hh > bottom - y)
                hh = bottom - y;
            out->header.x = area.x;
            out->header.y = y;
            out->header.w = area.w;
            out->header.h = hh;
            y += hh;
        }
    }

    if (view_ui_win_action_label(id, tab) != NULL) {
        int ah = view_ui_scale(VIEW_UI_ACTION_H, L->text_px);
        int lw = area.w * 2 / 5;

        if (ah > bottom - y)
            ah = bottom - y;
        out->action.x = area.x;
        out->action.w = area.w;
        out->action.h = ah;
        out->action.y = bottom - ah;
        out->action_label = out->action;
        out->action_label.w = lw;
        out->action_field = out->action;
        out->action_field.x = area.x + lw + out->pad;
        out->action_field.w = area.w - lw - out->pad;
        if (out->action_field.w < 1)
            out->action_field.w = 1;
        bottom = out->action.y - out->pad;
        if (bottom < y)
            bottom = y;
    }

    out->list.x = area.x;
    out->list.y = y;
    out->list.w = area.w;
    if (out->row_h < 1)
        out->row_h = 1;
    out->rows = (bottom - y) / out->row_h;
    if (out->rows < 0)
        out->rows = 0;
    out->list.h = out->rows * out->row_h;
}

int view_ui_win_tab_hit(const struct view_ui_win_body *B, int x, int y)
{
    int i;

    if (B == NULL)
        return -1;
    for (i = 0; i < B->tab_n && i < VIEW_UI_WIN_TABS; i++)
        if (view_ui_hit(&B->tab[i], x, y))
            return i;
    return -1;
}

int view_ui_win_row_hit(const struct view_ui_win_body *B, int x, int y)
{
    int r;

    if (B == NULL || B->row_h < 1 || !view_ui_hit(&B->list, x, y))
        return -1;
    r = (y - B->list.y) / B->row_h;
    if (r < 0 || r >= B->rows)
        return -1;
    return r;
}

/* ------------------------------------------------------------------ */
/* The player menu                                                     */
/* ------------------------------------------------------------------ */

/* The official client's player menu (strings 2250..2261), the entries the guest's own
 * action layer serves and in its order (openmmo_player.c; Spectate and
 * Whisper Window are the two official rows both halves omit). Copy Name is
 * the window's own verb: the clipboard is the host's. */
static const struct view_ui_player_item player_items[VIEW_UI_PLAYER_ROWS] = {
    { "Challenge",      OPENMMO_HUD_ACT_CHALLENGE, 0 },
    { "Whisper",        OPENMMO_HUD_ACT_WHISPER,   0 },
    { "Trade",          OPENMMO_HUD_ACT_TRADE,     0 },
    { "Invite to Link", OPENMMO_HUD_ACT_LINK,      0 },
    { "Add Friend",     OPENMMO_HUD_ACT_FRIEND,    1 },
    { "Block",          OPENMMO_HUD_ACT_BLOCK,     0 },
    { "Copy Name",      -1,                        0 }
};

const struct view_ui_player_item *view_ui_player_item(int i)
{
    if (i < 0 || i >= VIEW_UI_PLAYER_ROWS)
        return NULL;
    return &player_items[i];
}

const char *view_ui_player_label(int i, int is_friend)
{
    const struct view_ui_player_item *it = view_ui_player_item(i);

    if (it == NULL)
        return "";
    if (it->act == OPENMMO_HUD_ACT_FRIEND && is_friend)
        return "Remove Friend";
    return it->label;
}

void view_ui_player_confirm_text(const char *name, char *dst, size_t cap)
{
    if (dst == NULL || cap == 0)
        return;
    /* the official client string 1654, its {00} filled. */
    snprintf(dst, cap, "Do you wish to remove %s from your friends list?",
             name != NULL ? name : "?");
}

void view_ui_player_place(const struct openmmo_rect *canvas, int at_x,
                          int at_y, view_ui_measure measure, void *ctx,
                          int is_friend, struct view_ui_menu_layout *out)
{
    struct openmmo_rect c;
    int i, w = 0, h, y;

    if (out == NULL)
        return;
    memset(out, 0, sizeof *out);
    c.x = 0; c.y = 0; c.w = 1; c.h = 1;
    if (canvas != NULL)
        c = *canvas;
    if (c.w < 1) c.w = 1;
    if (c.h < 1) c.h = 1;

    out->text_px = view_ui_text_px(c.y + c.h);
    out->pad = view_ui_scale(6, out->text_px);
    out->row_h = view_ui_scale(22, out->text_px);
    out->n = VIEW_UI_PLAYER_ROWS;

    for (i = 0; i < VIEW_UI_PLAYER_ROWS; i++) {
        int lw = label_w(measure, ctx, view_ui_player_label(i, is_friend),
                         out->text_px);

        if (lw > w)
            w = lw;
    }
    w += 2 * out->pad + view_ui_scale(16, out->text_px);
    if (w < view_ui_scale(80, out->text_px))
        w = view_ui_scale(80, out->text_px);
    if (w > c.w) w = c.w;
    if (2 * out->pad + 1 > w)
        out->pad = (w - 1) / 2;
    if (out->pad < 0)
        out->pad = 0;
    {
        int avail = c.h - 2 * out->pad;

        if (avail < out->n) {
            out->pad = 0;
            avail = c.h;
        }
        if (out->n * out->row_h > avail)
            out->row_h = avail / out->n;
        if (out->row_h < 1)
            out->row_h = 1;
    }
    h = out->n * out->row_h + 2 * out->pad;
    if (h > c.h) h = c.h;

    /* Down-right of the pointer, flipped up where the floor is near, and
     * clamped inside either way. */
    out->box.w = w;
    out->box.h = h;
    out->box.x = at_x;
    out->box.y = at_y;
    if (out->box.y + h > c.y + c.h)
        out->box.y = at_y - h;
    if (out->box.x + w > c.x + c.w)
        out->box.x = c.x + c.w - w;
    if (out->box.x < c.x)
        out->box.x = c.x;
    if (out->box.y < c.y)
        out->box.y = c.y;
    if (out->box.y + h > c.y + c.h)
        out->box.y = c.y + c.h - h;

    y = out->box.y + out->pad;
    for (i = 0; i < out->n; i++) {
        out->row[i].x = out->box.x + out->pad;
        out->row[i].y = y;
        out->row[i].w = out->box.w - 2 * out->pad;
        if (out->row[i].w < 1)
            out->row[i].w = 1;
        out->row[i].h = out->row_h;
        y += out->row_h;
    }
}

/* ------------------------------------------------------------------ */
/* The confirm box                                                    */
/* ------------------------------------------------------------------ */

void view_ui_confirm_place(const struct openmmo_rect *canvas, int text_w,
                           struct view_ui_confirm_layout *out)
{
    struct openmmo_rect c;
    int w, h, bw, bh, line_h, gap;

    if (out == NULL)
        return;
    memset(out, 0, sizeof *out);
    c.x = 0; c.y = 0; c.w = 1; c.h = 1;
    if (canvas != NULL)
        c = *canvas;
    if (c.w < 1) c.w = 1;
    if (c.h < 1) c.h = 1;

    out->text_px = view_ui_text_px(c.y + c.h);
    out->pad = view_ui_scale(10, out->text_px); /* popup-warning border */
    line_h = out->text_px + out->text_px / 3;
    bw = view_ui_scale(66, out->text_px);
    bh = view_ui_scale(26, out->text_px);
    gap = view_ui_scale(10, out->text_px);

    if (text_w < 0)
        text_w = 0;
    w = text_w + 2 * out->pad;
    if (w < view_ui_scale(260, out->text_px))
        w = view_ui_scale(260, out->text_px);
    if (w > c.w) w = c.w;
    /* Two buttons always fit the box, however small the canvas made it. */
    if (2 * bw + gap > w) {
        gap = w / 8;
        bw = (w - gap) / 2;
        if (bw < 1)
            bw = 1;
    }
    /* Two wrapped lines of sentence over the button row. */
    h = 2 * line_h + bh + 3 * out->pad;
    if (h > c.h) h = c.h;

    out->box.w = w;
    out->box.h = h;
    out->box.x = c.x + (c.w - w) / 2;
    out->box.y = c.y + (c.h - h) / 2;

    out->text.x = out->box.x + out->pad;
    out->text.y = out->box.y + out->pad;
    out->text.w = w - 2 * out->pad;
    if (out->text.w < 1)
        out->text.w = 1;
    out->text.h = 2 * line_h;
    if (out->text.h > h - bh - 3 * out->pad)
        out->text.h = h - bh - 3 * out->pad;
    if (out->text.h < 0)
        out->text.h = 0;

    out->yes.w = bw;
    out->yes.h = bh;
    if (out->yes.h > out->box.h)
        out->yes.h = out->box.h;
    out->no = out->yes;
    out->yes.x = out->box.x + (w - 2 * bw - gap) / 2;
    out->no.x = out->yes.x + bw + gap;
    out->yes.y = out->box.y + out->box.h - out->pad - out->yes.h;
    if (out->yes.y < out->box.y)
        out->yes.y = out->box.y;
    out->no.y = out->yes.y;
    if (out->yes.x < out->box.x)
        out->yes.x = out->box.x;
    if (out->no.x + out->no.w > out->box.x + out->box.w)
        out->no.x = out->box.x + out->box.w - out->no.w;
    if (out->no.x < out->box.x)
        out->no.x = out->box.x;
}

/* ------------------------------------------------------------------ */
/* The party strip                                                     */
/* ------------------------------------------------------------------ */

void view_ui_partybar_place(const struct openmmo_rect *canvas, int n,
                            struct view_ui_partybar_layout *out)
{
    struct openmmo_rect c;
    int i, w, h, margin, y, bar_h, inner;

    if (out == NULL)
        return;
    memset(out, 0, sizeof *out);
    c.x = 0; c.y = 0; c.w = 1; c.h = 1;
    if (canvas != NULL)
        c = *canvas;
    if (c.w < 1) c.w = 1;
    if (c.h < 1) c.h = 1;
    out->n = view_ui_clampi(n, 0, OPENMMO_HUD_PARTY_N);
    if (out->n == 0)
        return;

    out->text_px = view_ui_text_px(c.y + c.h);
    out->pad = view_ui_scale(5, out->text_px);   /* party.xml content border */
    out->line_h = out->text_px + view_ui_scale(4, out->text_px);
    bar_h = view_ui_scale(5, out->text_px);
    if (bar_h < 2)
        bar_h = 2;
    /* A slot is the two rows the official client's slot labels, name with level, then
     * the numbers, and the bar under them. The official client fits both beside a 34px
     * icon; without the icon reel they stack, which is why this is taller
     * than party.xml's 34 rather than equal to it. */
    out->slot_h = 2 * out->line_h + bar_h + 2 * out->pad;
    margin = view_ui_scale(10, out->text_px);
    /* Wide enough for a name, a level and the bar under them. The official client's own
     * slot is 70x32 of icon plus text; without the icon reel the same row is
     * the two lines of text it labelled. */
    w = view_ui_scale(132, out->text_px) + 2 * out->pad;
    if (w > c.w) w = c.w;
    /*
     * Six slots on a short canvas shrink rather than run past the bottom, the way the popup's
     * rows do; and a canvas that cannot hold six slots a pixel tall each shows the slots that
     * fit rather than six that do not.
     */
    {
        int room = c.h - 2 * out->pad;

        if (room < 0) {
            out->pad = 0;
            room = c.h;
        }
        if (out->n * out->slot_h + (out->n - 1) * out->pad > room) {
            int per = (room - (out->n - 1) * out->pad) / out->n;

            if (per < 1) {
                out->pad = 0;
                room = c.h;
                per = room / out->n;
            }
            if (per < 1) {
                per = 1;
                out->n = room > 0 ? room : 0;
            }
            out->slot_h = per;
        }
    }
    if (out->n == 0)
        return;
    h = out->n * out->slot_h + (out->n + 1) * out->pad;
    if (h > c.h) h = c.h;

    /* f/NA0 hangs f/Qc off the right edge, vertically centred. */
    out->box.w = w;
    out->box.h = h;
    out->box.x = c.x + c.w - margin - w;
    out->box.y = c.y + (c.h - h) / 2;
    if (out->box.x < c.x) out->box.x = c.x;
    if (out->box.y < c.y) out->box.y = c.y;

    /* The slot's three bands, recomputed from whatever height survived the
     * clamp above so none of them can hang out of it. */
    inner = out->pad;
    if (out->slot_h - 2 * inner < 3)
        inner = 0;
    {
        int room = out->slot_h - 2 * inner;   /* the three bands share this */

        if (room < 0)
            room = 0;
        if (bar_h > room / 3)
            bar_h = room / 3;
        if (bar_h < 0)
            bar_h = 0;
        /* Integer division downward, so 2 * line_h + bar_h <= room holds by
         * construction and the bands cannot leave the slot. */
        out->line_h = (room - bar_h) / 2;
        if (out->line_h < 0)
            out->line_h = 0;
    }

    y = out->box.y + out->pad;
    for (i = 0; i < out->n; i++) {
        out->slot[i].x = out->box.x + out->pad;
        out->slot[i].y = y;
        out->slot[i].w = out->box.w - 2 * out->pad;
        if (out->slot[i].w < 1)
            out->slot[i].w = 1;
        out->slot[i].h = out->slot_h;

        out->name[i].x = out->slot[i].x + inner;
        out->name[i].y = out->slot[i].y + inner;
        out->name[i].w = out->slot[i].w - 2 * inner;
        if (out->name[i].w < 1)
            out->name[i].w = 1;
        out->name[i].h = out->line_h;
        out->hp[i] = out->name[i];
        out->hp[i].y = out->name[i].y + out->line_h;
        out->bar[i] = out->name[i];
        out->bar[i].y = out->hp[i].y + out->line_h;
        out->bar[i].h = bar_h;

        y += out->slot_h + out->pad;
    }
}

int view_ui_partybar_hit(const struct view_ui_partybar_layout *L, int x, int y)
{
    int i;

    if (L == NULL)
        return -1;
    for (i = 0; i < L->n && i < OPENMMO_HUD_PARTY_N; i++)
        if (view_ui_hit(&L->slot[i], x, y))
            return i;
    return -1;
}

/* The four fills are the mi-hpbar progressImage texels themselves (the 5x4
 * areas at 72,89 / 67,89 / 77,89 / 62,89 of monster-info.png), not the
 * font tints that share their names, those are lighter, made to sit on a
 * dark ground rather than to be a bar. */
uint32_t view_ui_hp_colour(int hp, int max_hp)
{
    if (max_hp > 0 && hp <= 0)
        return 0xA731CDu;               /* mi-hpbar-violet: fainted */
    if (max_hp <= 0)
        return 0x8DC655u;               /* mi-hpbar-green */
    if (hp * 2 >= max_hp)
        return 0x8DC655u;
    if (hp * 5 >= max_hp)
        return 0xCDB531u;               /* mi-hpbar-orange */
    return 0xC65555u;                   /* mi-hpbar */
}

int view_ui_hp_width(int hp, int max_hp, int w)
{
    if (w < 0)
        w = 0;
    if (max_hp <= 0)
        return w;
    if (hp <= 0)
        return 0;
    if (hp >= max_hp)
        return w;
    return w * hp / max_hp;
}

/* ------------------------------------------------------------------ */
/* The notice                                                          */
/* ------------------------------------------------------------------ */

void view_ui_notice_place(const struct openmmo_rect *canvas, int text_w,
                          struct view_ui_notice_layout *out)
{
    struct openmmo_rect c;
    int w, h;

    if (out == NULL)
        return;
    memset(out, 0, sizeof *out);
    c.x = 0; c.y = 0; c.w = 1; c.h = 1;
    if (canvas != NULL)
        c = *canvas;
    if (c.w < 1) c.w = 1;
    if (c.h < 1) c.h = 1;

    out->text_px = view_ui_text_px(c.y + c.h);
    out->pad = view_ui_scale(10, out->text_px);
    if (text_w < 0)
        text_w = 0;
    /* A canvas shorter than one padded line drops the padding and then the
     * line: the notice is clipped rather than hung off the bottom. */
    while (out->pad > 0 && out->text_px + 2 * out->pad > c.h)
        out->pad--;
    w = text_w + 2 * out->pad;
    if (w > c.w) w = c.w;
    h = out->text_px + 2 * out->pad;
    if (h > c.h) h = c.h;
    out->box.w = w;
    out->box.h = h;
    out->box.x = c.x + (c.w - w) / 2;
    /* A third of the way up, clear of the bar and of the chat box. */
    out->box.y = c.y + c.h - c.h / 3 - h / 2;
    if (out->box.y < c.y) out->box.y = c.y;
    if (out->box.y + h > c.y + c.h) out->box.y = c.y + c.h - h;
}
