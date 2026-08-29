/*
 * The UI layer's arithmetic: canvases, placement, the chat box's geometry,
 * wrapping and the compose line.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hud_channel.h"
#include "view_ui.h"
#include "view_ui_game.h"
#include "view_ui_theme.h"

static int failures;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) {                                                             \
            printf("  ok   %s\n", msg);                                         \
        } else {                                                                \
            printf("  FAIL %s\n", msg);                                         \
            failures++;                                                         \
        }                                                                       \
    } while (0)

static int rect_inside(const struct openmmo_rect *in,
                       const struct openmmo_rect *out)
{
    return in->x >= out->x && in->y >= out->y &&
           in->x + in->w <= out->x + out->w &&
           in->y + in->h <= out->y + out->h;
}

static void test_canvas(void)
{
    struct openmmo_rect top = { 0, 0, 1400, 1080 };
    struct openmmo_rect bottom = { 1400, 700, 520, 380 };
    struct openmmo_rect none = { 0, 0, 0, 0 };
    struct openmmo_rect r;

    r = view_ui_canvas(VIEW_UI_CANVAS_BOTH, 1920, 1080, &top, &bottom);
    CHECK(r.x == 0 && r.y == 0 && r.w == 1920 && r.h == 1080,
          "both screens is the whole window");
    r = view_ui_canvas(VIEW_UI_CANVAS_TOP, 1920, 1080, &top, &bottom);
    CHECK(r.x == top.x && r.w == top.w && r.h == top.h,
          "the top canvas is the world's rect");
    r = view_ui_canvas(VIEW_UI_CANVAS_BOTTOM, 1920, 1080, &top, &bottom);
    CHECK(r.x == bottom.x && r.y == bottom.y && r.w == bottom.w,
          "the bottom canvas is the second screen's rect");
    r = view_ui_canvas(VIEW_UI_CANVAS_BOTTOM, 1920, 1080, &top, &none);
    CHECK(r.x == 0 && r.y == 0 && r.w == 1920 && r.h == 1080,
          "a screen nothing was presented on falls back to the window");
}

static void test_place(void)
{
    struct openmmo_rect c = { 100, 50, 800, 600 };
    struct openmmo_rect r;

    r = view_ui_place(&c, VIEW_UI_BOTTOM_LEFT, 10, 300, 200);
    CHECK(r.x == 110 && r.y == 50 + 600 - 10 - 200 && r.w == 300 && r.h == 200,
          "bottom-left hangs from the canvas corner, not the window's");
    r = view_ui_place(&c, VIEW_UI_TOP_RIGHT, 10, 300, 200);
    CHECK(r.x == 100 + 800 - 10 - 300 && r.y == 60,
          "top-right the same");
    r = view_ui_place(&c, VIEW_UI_BOTTOM_LEFT, 10, 5000, 5000);
    CHECK(rect_inside(&r, &c),
          "an element wider than the canvas is clamped into it");
}

static void test_chat_place(void)
{
    static const int sizes[][2] = {
        { 856, 480 }, { 1280, 720 }, { 1920, 1080 }, { 2560, 1440 },
        { 3840, 1080 }, { 640, 480 }, { 320, 200 }, { 64, 64 },
    };
    int i, ok_inside = 1, ok_rows = 1, ok_split = 1;

    for (i = 0; i < (int)(sizeof sizes / sizeof sizes[0]); i++) {
        struct openmmo_rect c = { 0, 0, sizes[i][0], sizes[i][1] };
        struct view_ui_chat_layout L;

        view_ui_chat_place(&c, &L);
        if (!rect_inside(&L.box, &c) || !rect_inside(&L.log, &L.box) ||
            !rect_inside(&L.input, &L.box))
            ok_inside = 0;
        if (L.rows < 1 || L.rows > 14 || L.log.h != L.rows * L.line_h)
            ok_rows = 0;
        if (L.log.y + L.log.h > L.input.y)
            ok_split = 0;
    }
    CHECK(ok_inside, "box in canvas, log and input in box, at every size");
    CHECK(ok_rows, "the log is whole rows, two to fourteen of them");
    CHECK(ok_split, "the log ends before the compose row begins");

    {
        /* The second-screen band: short, offset, and still readable. */
        struct openmmo_rect band = { 1300, 800, 620, 280 };
        struct view_ui_chat_layout L;

        view_ui_chat_place(&band, &L);
        CHECK(rect_inside(&L.box, &band),
              "a band canvas keeps the box inside the band");
        CHECK(L.rows >= 4,
              "a band tall enough for four rows gets at least four");
    }
}

static void test_chat_hit(void)
{
    struct openmmo_rect c = { 0, 0, 1920, 1080 };
    struct view_ui_chat_layout L;

    view_ui_chat_place(&c, &L);
    CHECK(view_ui_chat_hit(&L, L.input.x + L.input.w / 2,
                           L.input.y + L.input.h / 2) ==
              VIEW_UI_CHAT_HIT_INPUT,
          "the middle of the compose row is the compose row");
    CHECK(view_ui_chat_hit(&L, L.log.x + 4, L.log.y + 4) ==
              VIEW_UI_CHAT_HIT_LOG,
          "the log is the log");
    CHECK(view_ui_chat_hit(&L, L.box.x + L.box.w + 40, L.box.y) ==
              VIEW_UI_CHAT_HIT_NONE,
          "beside the box is nobody's");
}

static void test_chat_tabs(void)
{
    struct openmmo_rect c = { 0, 0, 1920, 1080 };
    struct view_ui_chat_layout L;
    int i, inside = 1, above = 1;

    view_ui_chat_place(&c, &L);
    CHECK(L.tabs == VIEW_UI_CHAT_TABS,
          "a full-size canvas carries the whole strip");
    for (i = 0; i < L.tabs; i++) {
        if (!rect_inside(&L.tab[i], &L.box))
            inside = 0;
        if (L.tab[i].y + L.tab[i].h > L.log.y)
            above = 0;
    }
    CHECK(inside, "every tab is in the box");
    CHECK(above, "the strip ends before the log begins");
    CHECK(view_ui_chat_tab_hit(&L, L.tab[2].x + 2, L.tab[2].y + 2) == 2,
          "a tab answers its own index");
    CHECK(view_ui_chat_tab_hit(&L, L.log.x + 2, L.log.y + L.log.h - 2) == -1,
          "the log is no tab");

    /* The official client's own five, in the official client's order (the official client config,
     * client.ui.chat.tab.names). */
    CHECK(strcmp(view_ui_chat_tab_label(0), "Local") == 0 &&
              strcmp(view_ui_chat_tab_label(1), "Global") == 0 &&
              strcmp(view_ui_chat_tab_label(2), "Trade") == 0 &&
              strcmp(view_ui_chat_tab_label(3), "Whispers") == 0 &&
              strcmp(view_ui_chat_tab_label(4), "Battle") == 0 &&
              view_ui_chat_tab_label(5) == NULL,
          "the tabs are the official client's five");

    /* The filters are the official client's hidden_chat_types, held per tab. */
    CHECK(view_ui_chat_tab_shows(0, OPENMMO_HUD_CHAT_NORMAL) &&
              view_ui_chat_tab_shows(0, OPENMMO_HUD_CHAT_WHISPER) &&
              view_ui_chat_tab_shows(0, OPENMMO_HUD_CHAT_TEAM) &&
              !view_ui_chat_tab_shows(0, OPENMMO_HUD_CHAT_GLOBAL) &&
              !view_ui_chat_tab_shows(0, OPENMMO_HUD_CHAT_TRADE) &&
              !view_ui_chat_tab_shows(0, OPENMMO_HUD_CHAT_BATTLE),
          "Local hides global, trade and battle");
    CHECK(view_ui_chat_tab_shows(1, OPENMMO_HUD_CHAT_GLOBAL) &&
              view_ui_chat_tab_shows(1, OPENMMO_HUD_CHAT_SYSTEM) &&
              !view_ui_chat_tab_shows(1, OPENMMO_HUD_CHAT_NORMAL) &&
              !view_ui_chat_tab_shows(1, OPENMMO_HUD_CHAT_NOTICE),
          "Global is global plus system");
    CHECK(view_ui_chat_tab_shows(2, OPENMMO_HUD_CHAT_TRADE) &&
              view_ui_chat_tab_shows(2, OPENMMO_HUD_CHAT_SYSTEM) &&
              !view_ui_chat_tab_shows(2, OPENMMO_HUD_CHAT_GLOBAL),
          "Trade is trade plus system");
    CHECK(view_ui_chat_tab_shows(3, OPENMMO_HUD_CHAT_WHISPER) &&
              view_ui_chat_tab_shows(3, OPENMMO_HUD_CHAT_NOTICE) &&
              !view_ui_chat_tab_shows(3, OPENMMO_HUD_CHAT_TRADE),
          "Whispers keeps whispers and notices");
    CHECK(view_ui_chat_tab_shows(4, OPENMMO_HUD_CHAT_BATTLE) &&
              !view_ui_chat_tab_shows(4, OPENMMO_HUD_CHAT_SYSTEM) &&
              !view_ui_chat_tab_shows(4, OPENMMO_HUD_CHAT_NORMAL),
          "Battle is battle alone");
    CHECK(view_ui_chat_tab_shows(2, 31u) && view_ui_chat_tab_shows(2, 99u),
          "a type the official client never listed shows everywhere");

    /* Each tab is also the channel a composed line sends on, the official client's
     * channel picker follows the tab. */
    CHECK(view_ui_chat_tab_send_type(0) == OPENMMO_HUD_CHAT_NORMAL &&
              view_ui_chat_tab_send_type(1) == OPENMMO_HUD_CHAT_GLOBAL &&
              view_ui_chat_tab_send_type(2) == OPENMMO_HUD_CHAT_TRADE &&
              view_ui_chat_tab_send_type(3) == OPENMMO_HUD_CHAT_WHISPER &&
              view_ui_chat_tab_send_type(4) == OPENMMO_HUD_CHAT_BATTLE,
          "each tab sends on its own channel");
    CHECK(view_ui_chat_tab_send_type(-1) == OPENMMO_HUD_CHAT_NORMAL &&
              view_ui_chat_tab_send_type(5) == OPENMMO_HUD_CHAT_NORMAL,
          "past the strip the send channel is local");

    {
        /* A canvas too short for chrome drops the strip before the log. */
        struct openmmo_rect s = { 0, 0, 64, 64 };

        view_ui_chat_place(&s, &L);
        CHECK(L.tabs == 0, "a tiny canvas has no strip");
        CHECK(view_ui_chat_tab_hit(&L, 10, 10) == -1,
              "no strip, no tab hit");
    }
}

/* Six pixels a character: a fake face the wrap can be held against. */
static int measure6(void *ctx, const char *s, int len)
{
    (void)ctx;
    (void)s;
    return len * 6;
}

static void test_wrap(void)
{
    char rows[8][VIEW_UI_ROW_LEN];
    uint32_t tags[8];
    int n;

    n = view_ui_wrap(measure6, NULL, 60, 7u, "one two three four five",
                     rows, tags, 0, 8);
    CHECK(n == 3 && strcmp(rows[0], "one two") == 0 &&
          strcmp(rows[1], "three four") == 0 &&
          strcmp(rows[2], "five") == 0,
          "a line breaks at the last space that fits");
    CHECK(tags[0] == 7u && tags[n - 1] == 7u,
          "every row keeps its line's tag");

    n = view_ui_wrap(measure6, NULL, 60, 0u, "incomprehensibilities ok",
                     rows, tags, 0, 8);
    CHECK(n == 3 && strlen(rows[0]) == 10 &&
          strcmp(rows[2], "s ok") == 0,
          "a word wider than the box is cut at the width, not lost");

    n = view_ui_wrap(measure6, NULL, 60, 0u, "   ", rows, tags, 0, 8);
    CHECK(n == 0, "spaces alone make no rows");

    /* Six pixels a byte, so a two-byte character costs twelve and a width of
     * 57 puts the byte-counted cut one byte into the fifth of them. The row
     * has to end at the fourth. */
    n = view_ui_wrap(measure6, NULL, 57, 0u,
                     "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9",
                     rows, tags, 0, 8);
    CHECK(n == 2 && strlen(rows[0]) == 8 && strlen(rows[1]) == 4,
          "a row is cut between characters, never inside one");
}

/* The walk every text path on this layer shares. The decode itself is held
 * against the standard in text_channel_test.c; what matters here is that a
 * caller always advances and never sees half a character. */
static void test_utf8(void)
{
    uint32_t cp = 0;

    CHECK(view_ui_utf8("A", &cp) == 1 && cp == 'A',
          "an ascii character is one byte");
    CHECK(view_ui_utf8("\xc3\xa9" " Radar", &cp) == 2 && cp == 0xE9u,
          "e-acute is one character of two bytes");
    CHECK(view_ui_utf8("", &cp) == 0, "the terminator ends the walk");
    CHECK(view_ui_utf8("\xc3", &cp) == 1 && cp == 0xFFFDu,
          "a truncated sequence costs a byte and is not text");
    CHECK(view_ui_utf8("\xa9", &cp) == 1 && cp == 0xFFFDu,
          "a stray continuation byte costs a byte and is not text");

    {
        char buf[16];

        snprintf(buf, sizeof buf, "Pok\xc3\xa9");
        CHECK(view_ui_utf8_trunc(buf) == 3 && strcmp(buf, "Pok") == 0,
              "backspace over an accent takes the whole character");
        CHECK(view_ui_utf8_trunc(buf) == 2 && strcmp(buf, "Po") == 0,
              "backspace over a letter takes one byte");
        buf[0] = '\0';
        CHECK(view_ui_utf8_trunc(buf) == 0 && buf[0] == '\0',
              "backspace on an empty field is not an underflow");
    }
}

static void test_input_line(void)
{
    char line[160];

    CHECK(view_ui_chat_input_line(1, "hello", "", NULL, line,
                                  sizeof line) == 1 &&
              strcmp(line, "hello_") == 0,
          "the compose line echoes the typed text with a caret");
    CHECK(view_ui_chat_input_line(1, "", NULL, NULL, line, sizeof line) == 1 &&
              strcmp(line, "_") == 0,
          "an empty compose is the caret alone");
    CHECK(view_ui_chat_input_line(0, "stale", "x", "Trade", line,
                                  sizeof line) == 0 &&
              strcmp(line, "Enter to chat") == 0,
          "not composing is the standing hint, whatever the buffer holds");
    CHECK(view_ui_chat_input_line(1, "hi", "Dawn", NULL, line,
                                  sizeof line) == 1 &&
              strcmp(line, "To Dawn: hi_") == 0,
          "a whisper compose says who it is going to");
    CHECK(view_ui_chat_input_line(1, "wts", "", "Trade", line,
                                  sizeof line) == 1 &&
              strcmp(line, "[Trade] wts_") == 0,
          "a non-default tab names the channel the line goes out on");
    CHECK(view_ui_chat_input_line(1, "hi", "Dawn", "Whispers", line,
                                  sizeof line) == 1 &&
              strcmp(line, "To Dawn: hi_") == 0,
          "a whisper target outranks the channel label");

    /* The prefix's key is the guest's published channel, so the label
     * lookup is by channel, not by tab. */
    CHECK(view_ui_chat_type_label(OPENMMO_HUD_CHAT_NORMAL) == NULL,
          "the default channel composes bare");
    CHECK(view_ui_chat_type_label(OPENMMO_HUD_CHAT_TRADE) != NULL &&
              strcmp(view_ui_chat_type_label(OPENMMO_HUD_CHAT_TRADE),
                     "Trade") == 0,
          "the trade channel names the Trade tab");
    CHECK(view_ui_chat_type_label(OPENMMO_HUD_CHAT_WHISPER) != NULL &&
              strcmp(view_ui_chat_type_label(OPENMMO_HUD_CHAT_WHISPER),
                     "Whispers") == 0,
          "the whisper channel names the Whispers tab");
    CHECK(view_ui_chat_type_label(OPENMMO_HUD_CHAT_SYSTEM) == NULL,
          "a channel no tab sends on has no label");
}

static void test_fade(void)
{
    int started = 0;
    int a;

    a = view_ui_fade(0, 208, 12, &started);
    CHECK(a == 208 && started == 1, "the first step lands, not ramps");
    a = view_ui_fade(a, 88, 12, &started);
    CHECK(a == 196, "after that it walks by the step");
    a = view_ui_fade(90, 88, 12, &started);
    CHECK(a == 88, "and never overshoots");
}

static void test_text_px(void)
{
    CHECK(view_ui_text_px(1080) == 16 && view_ui_text_px(200) == 12 &&
              view_ui_text_px(4320) == 20,
          "type is 16px at 1080, floored at 12 and capped at 20");
}

static void test_chat_colour(void)
{
    CHECK(view_ui_chat_colour(OPENMMO_HUD_CHAT_NOTICE) == 0xFFD800u &&
              view_ui_chat_colour(OPENMMO_HUD_CHAT_WHISPER) == 0x66FF66u &&
              view_ui_chat_colour(OPENMMO_HUD_CHAT_NORMAL) == 0xFFFFFFu,
          "the chat palette answers by wire type");
    /* The official client's own tints, out of chat.xml and fonts.xml. A shout that is
     * not #FF9900 is a shout somebody re-picked by eye. */
    CHECK(view_ui_chat_colour(OPENMMO_HUD_CHAT_SHOUT) == 0xFF9900u &&
              view_ui_chat_colour(OPENMMO_HUD_CHAT_TRADE) == 0xFF99FFu &&
              view_ui_chat_colour(OPENMMO_HUD_CHAT_GLOBAL) == 0x81DDF1u &&
              view_ui_chat_colour(OPENMMO_HUD_CHAT_CHANNEL) == 0xE2C57Eu &&
              view_ui_chat_colour(OPENMMO_HUD_CHAT_TEAM) == 0xFF8484u &&
              view_ui_chat_colour(OPENMMO_HUD_CHAT_LINK) == 0x00E6B8u &&
              view_ui_chat_colour(OPENMMO_HUD_CHAT_SYSTEM) == 0xAACFFFu &&
              view_ui_chat_colour(OPENMMO_HUD_CHAT_BATTLE) == 0xFFFF00u,
          "and they are the official client's own font tints, value for value");
}

/* Every window shape the layer has to survive, from the smallest the
 * launcher will open to a 32:9 desktop. */
static const int win_sizes[][2] = {
    { 856, 480 }, { 1280, 720 }, { 1920, 1080 }, { 2560, 1440 },
    { 3840, 2160 }, { 3840, 1080 }, { 640, 480 }, { 320, 200 },
    /* Absurd, and the point: every rect below is claimed to be inside its
     * parent at any size, so the sizes that break an invariant are the
     * ones nobody would open. */
    { 200, 150 }, { 120, 90 }, { 64, 64 }, { 40, 24 }
};
#define WIN_N ((int)(sizeof win_sizes / sizeof win_sizes[0]))

static struct openmmo_rect win_canvas(int i)
{
    struct openmmo_rect c;

    c.x = 0;
    c.y = 0;
    c.w = win_sizes[i][0];
    c.h = win_sizes[i][1];
    return c;
}

static int mid_x(const struct openmmo_rect *r) { return r->x + r->w / 2; }
static int mid_y(const struct openmmo_rect *r) { return r->y + r->h / 2; }

static void test_scale(void)
{
    CHECK(view_ui_scale(66, 12) == 66 && view_ui_scale(66, 24) == 132 &&
              view_ui_scale(630, 16) == 840,
          "an official pixel is the official client's at 12px type and scales from there");
    CHECK(view_ui_scroll_clamp(9, 3, 8) == 0 &&
              view_ui_scroll_clamp(-4, 40, 8) == 0 &&
              view_ui_scroll_clamp(99, 40, 8) == 32,
          "a scroll never runs past either end of the list");
}

static void test_bar_table(void)
{
    static const char *const order[VIEW_UI_BAR_N] = {
        "Bag", "Trainer", "Community", "PvP", "Pokedex",
        "Egg Incubators", "Trade", "Mail", "Gift Shop", "Menu"
    };
    int i, j, ok_order = 1, ok_keys = 1, ok_act = 1;

    for (i = 0; i < VIEW_UI_BAR_N; i++) {
        const struct view_ui_item *it = view_ui_bar_item(i);

        if (it == NULL || strcmp(it->label, order[i]) != 0)
            ok_order = 0;
        if (it == NULL)
            continue;
        if (it->act == VIEW_UI_ACT_SCREEN &&
            (it->arg < 0 || it->arg >= OPENMMO_HUD_SCREEN_N))
            ok_act = 0;
        if (it->act == VIEW_UI_ACT_MENU && view_ui_menu_def(it->arg) == NULL)
            ok_act = 0;
        if (it->act == VIEW_UI_ACT_WINDOW && view_ui_win_def(it->arg) == NULL)
            ok_act = 0;
    }
    CHECK(ok_order, "the bar is the official client's ten buttons in the official client's order");
    CHECK(view_ui_bar_item(-1) == NULL &&
              view_ui_bar_item(VIEW_UI_BAR_N) == NULL,
          "and asking past either end answers nothing");
    CHECK(ok_act, "every button names a screen, a popup or a window it has");

    /* One key, one thing: a letter bound twice would fire both. */
    for (i = 0; i < VIEW_UI_BAR_N; i++) {
        const struct view_ui_item *a = view_ui_bar_item(i);
        int m;

        if (a->key == 0)
            continue;
        for (j = i + 1; j < VIEW_UI_BAR_N; j++)
            if (view_ui_bar_item(j)->key == a->key)
                ok_keys = 0;
        for (m = VIEW_UI_MENU_NONE + 1; m < VIEW_UI_MENU_N; m++) {
            const struct view_ui_menu_def *d = view_ui_menu_def(m);
            int r;

            for (r = 0; r < d->n; r++)
                if (d->item[r].key == a->key)
                    ok_keys = 0;
        }
    }
    CHECK(ok_keys, "no letter is bound to two of them");
}

static void test_bar_place(void)
{
    int i, ok_inside = 1, ok_order = 1, ok_hit = 1, ok_corner = 1;

    for (i = 0; i < WIN_N; i++) {
        struct openmmo_rect c = win_canvas(i);
        struct view_ui_bar_layout L;
        int k;

        view_ui_bar_place(&c, NULL, NULL, &L);
        if (!rect_inside(&L.bar, &c))
            ok_inside = 0;
        /* The official client hangs it off the bottom-right corner. */
        if (L.bar.y + L.bar.h > c.y + c.h ||
            L.bar.x + L.bar.w > c.x + c.w)
            ok_corner = 0;
        for (k = 0; k < VIEW_UI_BAR_N; k++) {
            if (!rect_inside(&L.btn[k], &L.bar))
                ok_inside = 0;
            if (k > 0 && L.btn[k].x < L.btn[k - 1].x + L.btn[k - 1].w)
                ok_order = 0;
            /* A canvas with no room for ten buttons leaves some of them
               nothing to be clicked in; the ones with a rect answer for
               themselves. */
            if (L.btn[k].w > 0 && L.btn[k].h > 0 &&
                view_ui_bar_hit(&L, mid_x(&L.btn[k]), mid_y(&L.btn[k])) != k)
                ok_hit = 0;
        }
        if (view_ui_bar_hit(&L, c.x, c.y) != -1)
            ok_hit = 0;
    }
    CHECK(ok_inside, "the bar and its ten buttons stay inside the canvas");
    CHECK(ok_corner, "and sit on the bottom-right corner, as the official client's does");
    CHECK(ok_order, "the buttons run left to right and never overlap");
    CHECK(ok_hit, "a click in a button is that button, and nowhere else is");
}

static void test_menu_place(void)
{
    int i, m, ok_inside = 1, ok_above = 1, ok_hit = 1;

    for (i = 0; i < WIN_N; i++) {
        struct openmmo_rect c = win_canvas(i);
        struct view_ui_bar_layout L;

        view_ui_bar_place(&c, NULL, NULL, &L);
        for (m = VIEW_UI_MENU_NONE + 1; m < VIEW_UI_MENU_N; m++) {
            const struct view_ui_menu_def *d = view_ui_menu_def(m);
            struct view_ui_menu_layout M;
            int r;

            view_ui_menu_place(&c, &L.btn[VIEW_UI_BAR_MENU], m, NULL, NULL,
                               &M);
            if (!rect_inside(&M.box, &c))
                ok_inside = 0;
            /* The bar is on the floor, so a popup opens upward wherever
             * there is room above the button. It may cover that button
             * only on a canvas with no room to open above it at all; the
             * check above keeps it inside the window either way. */
            {
                int room = L.btn[VIEW_UI_BAR_MENU].y - c.y;
                int need = M.box.h + M.pad + view_ui_scale(10, M.text_px);

                if (need <= room &&
                    M.box.y + M.box.h > L.btn[VIEW_UI_BAR_MENU].y)
                    ok_above = 0;
            }
            if (M.n != d->n)
                ok_hit = 0;
            for (r = 0; r < M.n; r++) {
                if (!rect_inside(&M.row[r], &M.box))
                    ok_inside = 0;
                if (view_ui_menu_hit(&M, mid_x(&M.row[r]),
                                     mid_y(&M.row[r])) != r)
                    ok_hit = 0;
            }
            /* The official client's menupopup has no title band: the first row starts at
             * the popup's own border and nothing above it answers a click. */
            if (M.n > 0 && M.row[0].y - M.box.y > M.pad)
                ok_hit = 0;
        }
    }
    CHECK(ok_inside, "a popup and its rows stay inside the canvas");
    CHECK(ok_above, "and open above the button that raised them");
    CHECK(ok_hit, "a click in a row is that row, and there is no title band");
}

static void test_menu_defs(void)
{
    const struct view_ui_menu_def *d;
    int ok = 1;

    CHECK(view_ui_menu_def(VIEW_UI_MENU_NONE) == NULL &&
              view_ui_menu_def(VIEW_UI_MENU_N) == NULL,
          "there is no menu zero and none past the table");
    d = view_ui_menu_def(VIEW_UI_MENU_COMMUNITY);
    ok = d != NULL && d->n == 5 && strcmp(d->item[0].label, "Friends") == 0 &&
         strcmp(d->item[1].label, "Team") == 0 &&
         strcmp(d->item[2].label, "Change Channel") == 0 &&
         strcmp(d->item[3].label, "Nearby Players") == 0 &&
         strcmp(d->item[4].label, "Select Follower") == 0;
    CHECK(ok, "Community is the official client's five entries in the official client's order");
    /*
     * The official client's eight less the Logout row, which was pulled before the first release: the
     * action behind it reboots the program, and an app cannot. Exit still ends the list and
     * still ends the game, which is the part that has to stay true.
     */
    d = view_ui_menu_def(VIEW_UI_MENU_GAME);
    ok = d != NULL && d->n == 7 && strcmp(d->item[0].label, "Party") == 0 &&
         strcmp(d->item[4].label, "Instance Info") == 0 &&
         strcmp(d->item[6].label, "Exit") == 0 &&
         d->item[6].act == VIEW_UI_ACT_QUIT;
    CHECK(ok, "Menu is the official client's list without Logout, and Exit still ends it");
    {
        int i, found = 0;

        for (i = 0; i < d->n; i++)
            if (strcmp(d->item[i].label, "Logout") == 0)
                found = 1;
        CHECK(!found, "and no Logout row is offered anywhere in it");
    }
}

static void test_notice_text(void)
{
    char buf[192];
    const struct view_ui_item *it = view_ui_bar_item(VIEW_UI_BAR_SHOP);

    view_ui_notice_text(it, buf, sizeof buf);
    CHECK(strcmp(buf, "Gift shop is not available at this time.") == 0,
          "a surface the official client has a sentence for keeps the official client's sentence");
    /* Mail is a window now; the generated sentence is still what a surface
     * with no text of its own gets, so a menu row stands in for it. */
    view_ui_notice_text(&view_ui_menu_def(VIEW_UI_MENU_PVP)->item[2], buf,
                        sizeof buf);
    CHECK(strcmp(buf, "Tournaments is not available at this time.") == 0,
          "and one it does not takes its own name as the subject");
    CHECK(view_ui_bar_item(VIEW_UI_BAR_MAIL)->act == VIEW_UI_ACT_WINDOW &&
              view_ui_bar_item(VIEW_UI_BAR_MAIL)->arg == VIEW_UI_WIN_MAIL,
          "the Mail button raises the mailbox rather than refusing");
    view_ui_notice_text(NULL, buf, sizeof buf);
    CHECK(strcmp(buf, "You cannot do that now.") == 0,
          "nothing at all is the official client's own refusal");
}

static void test_frame_place(void)
{
    int i, id, ok_inside = 1, ok_rows = 1, ok_hit = 1, ok_centre = 1;

    for (i = 0; i < WIN_N; i++) {
        struct openmmo_rect c = win_canvas(i);

        for (id = 0; id < VIEW_UI_WIN_N; id++) {
            struct view_ui_frame_layout L;
            struct openmmo_rect drag;

            view_ui_frame_place(&c, id, 0, 0, &L);
            if (!rect_inside(&L.box, &c))
                ok_inside = 0;
            if (!rect_inside(&L.body, &L.box) ||
                !rect_inside(&L.close, &L.box) ||
                !rect_inside(&L.close_hit, &L.box) ||
                !rect_inside(&L.close, &L.close_hit))
                ok_inside = 0;
            /* Centred with no drag, within the rounding of an odd margin. */
            if (L.box.w < c.w &&
                (L.box.x - c.x) - (c.x + c.w - L.box.x - L.box.w) > 1)
                ok_centre = 0;
            if (L.rows * L.row_h != L.body.h)
                ok_rows = 0;
            if (view_ui_frame_hit(&L, mid_x(&L.close), mid_y(&L.close)) !=
                VIEW_UI_FRAME_HIT_CLOSE)
                ok_hit = 0;
            /* Every corner of the tap target closes too, which is the whole
             * point of it being bigger than the cross: a finger that lands
             * anywhere in the title bar's right end must not drag instead. */
            if (view_ui_frame_hit(&L, L.close_hit.x,
                                  L.close_hit.y) != VIEW_UI_FRAME_HIT_CLOSE ||
                view_ui_frame_hit(&L, L.close_hit.x + L.close_hit.w - 1,
                                  L.close_hit.y + L.close_hit.h - 1)
                    != VIEW_UI_FRAME_HIT_CLOSE)
                ok_hit = 0;
            /* And it is genuinely bigger: the official client's cross is 14 wide, so a
             * target that has not grown past it has not been applied. */
            if (L.box.w > L.title_h * 2 &&
                L.close_hit.w * L.close_hit.h <= L.close.w * L.close.h)
                ok_hit = 0;
            if (view_ui_frame_hit(&L, L.box.x + 1, L.box.y + 1) !=
                VIEW_UI_FRAME_HIT_TITLE)
                ok_hit = 0;
            if (L.rows > 0 &&
                view_ui_frame_row(&L, L.body.x + 1,
                                  L.body.y + L.row_h / 2) != 0)
                ok_hit = 0;
            if (view_ui_frame_row(&L, c.x, c.y) != -1)
                ok_hit = 0;

            /* Dragged hard into a corner, the title bar is still on screen
             * to be grabbed back. */
            view_ui_frame_place(&c, id, 100000, 100000, &L);
            drag = L.box;
            drag.h = L.title_h;
            if (drag.x >= c.x + c.w || drag.y >= c.y + c.h ||
                drag.x + drag.w <= c.x)
                ok_inside = 0;
            view_ui_frame_place(&c, id, -100000, -100000, &L);
            drag = L.box;
            drag.h = L.title_h;
            if (drag.x + drag.w <= c.x || drag.y + drag.h <= c.y)
                ok_inside = 0;
        }
    }
    CHECK(ok_inside, "a frame, its body and its close button stay reachable");
    CHECK(ok_centre, "an undragged frame is centred, the way official opens it");
    CHECK(ok_rows, "the body is whole rows, so none is half-drawn");
    CHECK(ok_hit, "close beats title beats body, and outside is none of them");
}

static void test_win_defs(void)
{
    CHECK(view_ui_win_def(VIEW_UI_WIN_FRIENDS)->w == 470 &&
              view_ui_win_def(VIEW_UI_WIN_FRIENDS)->h == 320 &&
              view_ui_win_def(VIEW_UI_WIN_TEAM)->w == 485 &&
              view_ui_win_def(VIEW_UI_WIN_TEAM)->h == 300 &&
              view_ui_win_def(VIEW_UI_WIN_INSTANCE)->w == 550 &&
              view_ui_win_def(VIEW_UI_WIN_INSTANCE)->h == 370,
          "the frames are the sizes official opens them at");
    CHECK(view_ui_win_def(-1) == NULL && view_ui_win_def(VIEW_UI_WIN_N) == NULL,
          "and there is nothing past either end");
}

static void test_party_place(void)
{
    int i, n, ok_inside = 1, ok_right = 1, ok_hit = 1, ok_n = 1;

    for (i = 0; i < WIN_N; i++) {
        struct openmmo_rect c = win_canvas(i);

        for (n = 0; n <= OPENMMO_HUD_PARTY_N + 2; n++) {
            struct view_ui_partybar_layout L;
            int k;

            view_ui_partybar_place(&c, n, &L);
            /* A canvas too short for the party shows fewer slots; it never
               shows more than there are. */
            if (L.n > (n > OPENMMO_HUD_PARTY_N ? OPENMMO_HUD_PARTY_N : n))
                ok_n = 0;
            if (L.n == 0)
                continue;
            if (!rect_inside(&L.box, &c))
                ok_inside = 0;
            /* The official client hangs the strip off the right edge: when it and its
               margin fit, its right edge is exactly there. */
            {
                int margin = view_ui_scale(10, L.text_px);

                if (L.box.x + L.box.w > c.x + c.w)
                    ok_right = 0;
                if (L.box.w + 2 * margin <= c.w &&
                    L.box.x + L.box.w != c.x + c.w - margin)
                    ok_right = 0;
            }
            for (k = 0; k < L.n; k++) {
                if (!rect_inside(&L.slot[k], &L.box) ||
                    !rect_inside(&L.bar[k], &L.slot[k]) ||
                    !rect_inside(&L.name[k], &L.slot[k]) ||
                    !rect_inside(&L.hp[k], &L.slot[k]))
                    ok_inside = 0;
                if (L.slot[k].w > 0 && L.slot[k].h > 0 &&
                    view_ui_partybar_hit(&L, mid_x(&L.slot[k]),
                                         mid_y(&L.slot[k])) != k)
                    ok_hit = 0;
            }
        }
    }
    CHECK(ok_n, "the strip holds at most a party and never more");
    CHECK(ok_inside, "every slot and its HP bar stay inside the strip");
    CHECK(ok_right, "which hangs off the right edge, as the official client's does");
    CHECK(ok_hit, "a click in a slot is that slot");
}

static void test_hp(void)
{
    CHECK(view_ui_hp_colour(100, 100) == 0x8DC655u &&
              view_ui_hp_colour(50, 100) == 0x8DC655u &&
              view_ui_hp_colour(49, 100) == 0xCDB531u &&
              view_ui_hp_colour(20, 100) == 0xCDB531u &&
              view_ui_hp_colour(19, 100) == 0xC65555u &&
              view_ui_hp_colour(0, 100) == 0xA731CDu,
          "the HP bar is mi-hpbar's own green, orange, red and violet texels");
    CHECK(view_ui_hp_colour(7, 0) == 0x8DC655u,
          "and a maximum nobody sent draws full rather than empty");
    CHECK(view_ui_hp_width(50, 100, 200) == 100 &&
              view_ui_hp_width(0, 100, 200) == 0 &&
              view_ui_hp_width(200, 100, 200) == 200 &&
              view_ui_hp_width(3, 0, 200) == 200,
          "and it fills its fraction of the trough, clamped both ways");
}

static void test_notice_place(void)
{
    int i, ok = 1;

    for (i = 0; i < WIN_N; i++) {
        struct openmmo_rect c = win_canvas(i);
        struct view_ui_notice_layout N;

        view_ui_notice_place(&c, 400, &N);
        if (!rect_inside(&N.box, &c))
            ok = 0;
        view_ui_notice_place(&c, 100000, &N);
        if (!rect_inside(&N.box, &c))
            ok = 0;
    }
    CHECK(ok, "the notice stays inside the canvas at any line width");
}

/* ------------------------------------------------------------------ */
/* Inside the frames                                                   */
/* ------------------------------------------------------------------ */

static void test_win_body(void)
{
    int i, id, tab, ok_inside = 1, ok_parts = 1, ok_hit = 1;

    for (i = 0; i < WIN_N; i++) {
        struct openmmo_rect c = win_canvas(i);

        for (id = 0; id < VIEW_UI_WIN_N; id++) {
            for (tab = 0; tab < VIEW_UI_WIN_TABS; tab++) {
                struct view_ui_frame_layout L;
                struct view_ui_win_body B;
                int t;

                if (tab > 0 && view_ui_win_tab_label(id, tab) == NULL)
                    continue;
                view_ui_frame_place(&c, id, 0, 0, &L);
                view_ui_win_body_place(&L, id, tab, &B);
                for (t = 0; t < B.tab_n; t++) {
                    if (!rect_inside(&B.tab[t], &L.box))
                        ok_inside = 0;
                    /* A tab a degenerate canvas flattened away answers no
                     * click, which is right, nothing is drawn there. */
                    if (B.tab[t].w > 2 && B.tab[t].h > 2 &&
                        view_ui_win_tab_hit(&B, mid_x(&B.tab[t]),
                                            mid_y(&B.tab[t])) != t)
                        ok_hit = 0;
                }
                if (B.header.h > 0 && !rect_inside(&B.header, &L.box))
                    ok_inside = 0;
                if (B.list.h > 0 && !rect_inside(&B.list, &L.box))
                    ok_inside = 0;
                if (B.action.h > 0 &&
                    (!rect_inside(&B.action, &L.box) ||
                     !rect_inside(&B.action_field, &L.box)))
                    ok_inside = 0;
                /* The list begins under the tabs and the header and ends
                 * above the action row: nothing may sit on anything. */
                if (B.tab_n > 0 && B.list.y < B.tab[0].y + B.tab[0].h)
                    ok_parts = 0;
                if (B.header.h > 0 && B.list.y < B.header.y + B.header.h)
                    ok_parts = 0;
                if (B.action.h > 0 &&
                    B.list.y + B.list.h > B.action.y)
                    ok_parts = 0;
                if (B.rows > 0 &&
                    view_ui_win_row_hit(&B, mid_x(&B.list),
                                        B.list.y + B.row_h / 2) != 0)
                    ok_hit = 0;
            }
        }
    }
    CHECK(ok_inside, "tabs, header, list and action row stay in the frame");
    CHECK(ok_parts, "and stack without sitting on each other");
    CHECK(ok_hit, "a click answers with the tab or the row it landed in");
    CHECK(view_ui_win_tab_label(VIEW_UI_WIN_FRIENDS, 0) != NULL &&
              view_ui_win_tab_label(VIEW_UI_WIN_FRIENDS, 1) != NULL &&
              view_ui_win_tab_label(VIEW_UI_WIN_TEAM, 0) == NULL,
          "the social window has the official client's two tabs and the others none");
    CHECK(strcmp(view_ui_win_action_label(VIEW_UI_WIN_FRIENDS, 0),
                 "Add Friend") == 0 &&
              strcmp(view_ui_win_action_label(VIEW_UI_WIN_FRIENDS, 1),
                     "Block") == 0,
          "and its action rows are the official client's Add Friend and Block");
}

static void test_player_menu(void)
{
    const struct view_ui_player_item *it;
    struct view_ui_menu_layout M;
    struct openmmo_rect c = { 0, 0, 1920, 1080 };
    char text[VIEW_UI_ROW_LEN];
    int i, ok = 1;

    it = view_ui_player_item(0);
    CHECK(it != NULL && strcmp(it->label, "Challenge") == 0 &&
              it->act == OPENMMO_HUD_ACT_CHALLENGE,
          "the player menu opens on Challenge, as the official client's does");
    it = view_ui_player_item(4);
    CHECK(it != NULL && it->act == OPENMMO_HUD_ACT_FRIEND && it->confirm &&
              strcmp(view_ui_player_label(4, 0), "Add Friend") == 0 &&
              strcmp(view_ui_player_label(4, 1), "Remove Friend") == 0,
          "the friend row reads by the list and removal asks first");
    it = view_ui_player_item(VIEW_UI_PLAYER_ROWS - 1);
    CHECK(it != NULL && it->act < 0 &&
              strcmp(it->label, "Copy Name") == 0 &&
              view_ui_player_item(VIEW_UI_PLAYER_ROWS) == NULL,
          "Copy Name is the window's own verb and the list ends there");

    /* At every corner the menu opens inside the canvas and each row
     * answers a click in its middle. */
    {
        static const int at[4][2] = {
            { 0, 0 }, { 1919, 0 }, { 0, 1079 }, { 1919, 1079 }
        };
        int k;

        for (k = 0; k < 4; k++) {
            view_ui_player_place(&c, at[k][0], at[k][1], NULL, NULL, k & 1,
                                 &M);
            if (!rect_inside(&M.box, &c))
                ok = 0;
            for (i = 0; i < M.n; i++)
                if (view_ui_menu_hit(&M, mid_x(&M.row[i]),
                                     mid_y(&M.row[i])) != i)
                    ok = 0;
        }
    }
    CHECK(ok, "the menu stays inside the canvas from any pointer corner");

    view_ui_player_confirm_text("Dawn", text, sizeof text);
    CHECK(strcmp(text,
                 "Do you wish to remove Dawn from your friends list?") == 0,
          "the removal confirm is the official client's own sentence with the name in");
}

static void test_confirm_place(void)
{
    int i, ok = 1;

    for (i = 0; i < WIN_N; i++) {
        struct openmmo_rect c = win_canvas(i);
        struct view_ui_confirm_layout C;

        view_ui_confirm_place(&c, 300, &C);
        if (!rect_inside(&C.box, &c) || !rect_inside(&C.yes, &C.box) ||
            !rect_inside(&C.no, &C.box) || !rect_inside(&C.text, &C.box))
            ok = 0;
        if (C.yes.x + C.yes.w > C.no.x && C.no.w > 0 && C.yes.w > 0 &&
            C.yes.y == C.no.y && C.yes.x < C.no.x + C.no.w)
            ok = ok && C.yes.x + C.yes.w <= C.no.x;
    }
    CHECK(ok, "the confirm, its sentence and its two buttons nest cleanly");
}

/* ------------------------------------------------------------------ */
/* The theme                                                           */
/* ------------------------------------------------------------------ */

static void test_theme_table(void)
{
    struct view_ui_th_pair cells[VIEW_UI_TH_CELLS];
    struct openmmo_rect dst = { 40, 30, 200, 100 };
    int id, i, ok_sheet = 1, ok_src = 1, ok_cover = 1, ok_scale = 1;

    for (id = 0; id < VIEW_UI_TH_N; id++) {
        int sheet = view_ui_theme_sheet(id);
        int sw = 0, sh = 0, n, area = 0;

        if (sheet < 0 || sheet >= VIEW_UI_SHEET_N ||
            view_ui_sheet_file(sheet) == NULL)
            ok_sheet = 0;
        view_ui_sheet_min(sheet, &sw, &sh);
        n = view_ui_theme_cells(id, &dst, VIEW_UI_OFFICIAL_PX, cells);
        if (n < 1)
            ok_cover = 0;
        for (i = 0; i < n; i++) {
            struct openmmo_rect sheet_r = { 0, 0, 0, 0 };

            sheet_r.w = sw;
            sheet_r.h = sh;
            /* Every texel named is on the sheet the table says it is. */
            if (!rect_inside(&cells[i].src, &sheet_r))
                ok_src = 0;
            if (!rect_inside(&cells[i].dst, &dst))
                ok_cover = 0;
            area += cells[i].dst.w * cells[i].dst.h;
        }
        /* The grid's own construction cannot overlap, so covering the
         * area is covering the rect. */
        if (area != dst.w * dst.h)
            ok_cover = 0;
    }
    CHECK(ok_sheet, "every surface names a sheet the loader knows");
    CHECK(ok_src, "and only texels that sheet has");
    CHECK(ok_cover, "and its cells tile the target exactly");

    /* At double the type size a nine-patch's corner is twice the texels'
     * size, the same scaling every official number gets. */
    {
        struct view_ui_th_pair big[VIEW_UI_TH_CELLS];
        int n = view_ui_theme_cells(VIEW_UI_TH_PANEL, &dst,
                                    VIEW_UI_OFFICIAL_PX, cells);
        int m = view_ui_theme_cells(VIEW_UI_TH_PANEL, &dst,
                                    2 * VIEW_UI_OFFICIAL_PX, big);

        ok_scale = n == 9 && m == 9 && cells[0].dst.w * 2 == big[0].dst.w &&
                   cells[0].dst.h * 2 == big[0].dst.h &&
                   cells[0].src.w == big[0].src.w;
    }
    CHECK(ok_scale, "a fixed edge scales with the type and its texels do not");

    /* A target smaller than the fixed edges alone still comes back tiled, 
     * shrunk, not overflowed. */
    {
        struct openmmo_rect tiny = { 0, 0, 5, 4 };
        int n = view_ui_theme_cells(VIEW_UI_TH_FRAME, &tiny,
                                    2 * VIEW_UI_OFFICIAL_PX, cells);
        int area = 0, ok = n > 0;

        for (i = 0; i < n; i++) {
            if (!rect_inside(&cells[i].dst, &tiny))
                ok = 0;
            area += cells[i].dst.w * cells[i].dst.h;
        }
        CHECK(ok && area == tiny.w * tiny.h,
              "a target smaller than the corners shrinks them to fit");
    }
}

static void test_theme_png(void)
{
    /* A 3x3 RGBA whose three rows use filters none, sub and up, and a 2x2
     * RGB on average and Paeth, encoded once, checked byte for byte. */
    static const unsigned char rgba_png[] = {
        0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,
        0x44,0x52,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x03,0x08,0x06,0x00,0x00,
        0x00,0x56,0x28,0xb5,0xbf,0x00,0x00,0x00,0x25,0x49,0x44,0x41,0x54,0x78,
        0xda,0x63,0xf8,0xcf,0xc0,0xf0,0x1f,0x08,0x1b,0x80,0xd4,0x7f,0x46,0x2e,
        0x11,0x39,0x0d,0x18,0x60,0xda,0xd7,0xe4,0xc6,0xf5,0x1c,0x08,0x96,0xcf,
        0x9b,0xda,0x03,0x00,0xfe,0x4c,0x0e,0xb5,0x18,0x25,0xb5,0x01,0x00,0x00,
        0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82
    };
    static const unsigned char rgba_px[36] = {
        255,0,0,255, 0,255,0,128, 0,0,255,255,
        10,20,30,40, 50,60,70,80, 90,100,110,120,
        200,150,100,50, 25,35,45,55, 1,2,3,4
    };
    static const unsigned char rgb_png[] = {
        0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,
        0x44,0x52,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x02,0x08,0x02,0x00,0x00,
        0x00,0xfd,0xd4,0x9a,0x73,0x00,0x00,0x00,0x16,0x49,0x44,0x41,0x54,0x78,
        0xda,0x63,0xe6,0xe4,0x60,0x67,0x62,0x64,0x64,0xf9,0xf5,0xeb,0xd7,0xf7,
        0x9f,0xbf,0x01,0x16,0x13,0x05,0xfd,0xfd,0x3f,0x67,0x7b,0x00,0x00,0x00,
        0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82
    };
    static const unsigned char rgb_px[16] = {
        9,8,7,255, 6,5,4,255,
        3,2,1,255, 250,251,252,255
    };
    unsigned char *px;
    int w = 0, h = 0;

    px = view_ui_png(rgba_png, sizeof rgba_png, &w, &h);
    CHECK(px != NULL && w == 3 && h == 3 &&
              memcmp(px, rgba_px, sizeof rgba_px) == 0,
          "an RGBA PNG decodes texel for texel through none, sub and up");
    free(px);
    px = view_ui_png(rgb_png, sizeof rgb_png, &w, &h);
    CHECK(px != NULL && w == 2 && h == 2 &&
              memcmp(px, rgb_px, sizeof rgb_px) == 0,
          "an RGB one gains its alpha through average and Paeth");
    free(px);
    CHECK(view_ui_png(rgba_png, 20, &w, &h) == NULL &&
              view_ui_png(NULL, 0, &w, &h) == NULL,
          "and a truncated or absent file is refused, not half-read");
    {
        /* The same RGBA bytes with the depth claimed as 16: refused. */
        unsigned char bad[sizeof rgba_png];

        memcpy(bad, rgba_png, sizeof rgba_png);
        bad[24] = 16;
        CHECK(view_ui_png(bad, sizeof bad, &w, &h) == NULL,
              "as is any depth or layout the sheets never use");
    }
}

static void test_theme_read(void)
{
    struct view_ui_theme t;

    CHECK(!view_ui_theme_read(&t, "/nonexistent-theme-dir") && !t.ready,
          "a theme that is not there leaves the window unthemed");
    CHECK(!view_ui_theme_read(&t, NULL) && !view_ui_theme_read(NULL, "x"),
          "and no dir at all is the same answer, not a crash");
}

int view_ui_tests_run(void)
{
    printf("view ui\n");
    test_canvas();
    test_place();
    test_chat_place();
    test_chat_hit();
    test_chat_tabs();
    test_wrap();
    test_utf8();
    test_input_line();
    test_fade();
    test_text_px();
    test_chat_colour();
    test_scale();
    test_bar_table();
    test_bar_place();
    test_menu_place();
    test_menu_defs();
    test_notice_text();
    test_frame_place();
    test_win_defs();
    test_party_place();
    test_hp();
    test_notice_place();
    test_win_body();
    test_player_menu();
    test_confirm_place();
    test_theme_table();
    test_theme_png();
    test_theme_read();
    return failures;
}
