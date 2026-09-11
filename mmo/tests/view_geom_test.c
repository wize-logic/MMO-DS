/* The properties a picture must not quietly lose. */

#include <stdio.h>
#include <stdlib.h>

#include "view_geom.h"

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

static uint32_t xs32(uint32_t *s)          /* xorshift; determinism, not art */
{
    uint32_t x = *s;

    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

/* The fit: inside the window, centred, proportions kept, and whole multiples
 * when integer scaling was asked for and the window can hold one. */
static void test_fit(void)
{
    static const int wins[][2] = {
        { 512, 768 }, { 511, 767 }, { 1920, 1080 }, { 100, 900 },
        { 900, 100 }, { 256, 384 }, { 3, 5 }, { 4096, 4096 },
    };
    static const int widths[] = { OPENMMO_VIEW_W, 300,
                                  (int)OPENMMO_VIEW_WIDE_MAX };
    int inside = 0, centred = 0, proportions = 0, whole = 0;
    int layout, integer, i, wi;

    for (layout = 0; layout < 2; layout++)
    for (wi = 0; wi < (int)(sizeof widths / sizeof *widths); wi++) {
        int cw, ch;

        openmmo_view_composed_wh(layout, widths[wi], OPENMMO_VIEW_H,
                                 OPENMMO_VIEW_SEC_MIN, &cw, &ch);
        for (integer = 0; integer < 2; integer++) {
            for (i = 0; i < (int)(sizeof wins / sizeof *wins); i++) {
                int ww = wins[i][0], wh = wins[i][1];
                struct openmmo_rect r = openmmo_view_fit(cw, ch, ww, wh,
                                                         integer,
                                                         OPENMMO_ASPECT_NATIVE);

                if (!(r.x >= 0 && r.y >= 0 &&
                      r.x + r.w <= ww && r.y + r.h <= wh)) inside++;
                if (!(abs((ww - r.w) - 2 * r.x) <= 1 &&
                      abs((wh - r.h) - 2 * r.y) <= 1)) centred++;
                /* r.w/r.h == cw/ch, within a pixel's worth of rounding. */
                if (!(labs((long)r.w * ch - (long)r.h * cw) <= cw ||
                      labs((long)r.w * ch - (long)r.h * cw) <= ch)) proportions++;
                if (integer && ww >= cw && wh >= ch &&
                    !(r.w % cw == 0 && r.h % ch == 0)) whole++;
            }
        }
    }
    CHECK(inside == 0, "the picture stays inside the window");
    CHECK(centred == 0, "the picture is centred");
    CHECK(proportions == 0, "proportions survive the fit");
    CHECK(whole == 0, "integer mode scales by whole source pixels");

    {
        struct openmmo_rect r = openmmo_view_fit(256, 384, 777, 333, 0,
                                                 OPENMMO_ASPECT_STRETCH);

        CHECK(r.x == 0 && r.y == 0 && r.w == 777 && r.h == 333,
              "stretch fills the window");
    }
}

/* The two screens against each other, at every layout and every published
 * width: glued, sized as asked, never stretched. */
static void test_screen_rects(void)
{
    struct openmmo_view_geom g = { 0, OPENMMO_ASPECT_NATIVE, 0, 0 };
    struct openmmo_rect d[2];
    int smart_abut = 0, smart_frac = 0, smart_centre = 0, smart_prop = 0;
    int wide_row = 0, wide_abut = 0, stack_col = 0, stack_abut = 0;
    int layout, sw;

    for (layout = 0; layout < 3; layout++)
    for (sw = OPENMMO_VIEW_W; sw <= (int)OPENMMO_VIEW_WIDE_MAX; sw += 43) {
        g.layout = layout;
        openmmo_view_screen_rects(&g, sw, OPENMMO_VIEW_H,
                                  OPENMMO_VIEW_SEC_MIN, 1000, 700, d);
        /* The two screens tile the fitted rectangle exactly: no gap, no
         * overlap, whichever way the remainder pixel fell. Smart is the
         * exception, its touch screen is deliberately smaller and centred, so
         * what it owes is proportions rather than tiling. */
        if (layout == OPENMMO_LAYOUT_SMART) {
            if (d[0].x + d[0].w != d[1].x) smart_abut++;
            if (!(d[1].w * 100 <= d[0].w * OPENMMO_VIEW_SEC_MIN + 100 &&
                  d[1].w * 100 >= d[0].w * OPENMMO_VIEW_SEC_MIN - 100))
                smart_frac++;
            if (!(d[1].y + d[1].h / 2 >= d[0].y + d[0].h / 2 - 1 &&
                  d[1].y + d[1].h / 2 <= d[0].y + d[0].h / 2 + 1))
                smart_centre++;
            if (!(d[0].w * d[1].h <= d[1].w * d[0].h + d[0].w &&
                  d[0].w * d[1].h >= d[1].w * d[0].h - d[0].w))
                smart_prop++;
        } else if (layout == OPENMMO_LAYOUT_WIDE) {
            if (!(d[0].y == d[1].y && d[0].h == d[1].h)) wide_row++;
            if (d[0].x + d[0].w != d[1].x) wide_abut++;
        } else {
            if (!(d[0].x == d[1].x && d[0].w == d[1].w)) stack_col++;
            if (d[0].y + d[0].h != d[1].y) stack_abut++;
        }
    }
    CHECK(smart_abut == 0, "smart: the screens abut");
    CHECK(smart_frac == 0, "smart: the touch screen is the asked-for fraction");
    CHECK(smart_centre == 0, "smart: the touch screen is centred beside the game");
    CHECK(smart_prop == 0, "smart: both screens keep the same proportions");
    CHECK(wide_row == 0, "wide: the screens share a row");
    CHECK(wide_abut == 0, "wide: the screens abut");
    CHECK(stack_col == 0, "stacked: the screens share a column");
    CHECK(stack_abut == 0, "stacked: the screens abut");
}

/* The pen, inverted back through the layout it was drawn with. */
static void test_window_to_touch(void)
{
    struct openmmo_view_geom g = { 0, OPENMMO_ASPECT_NATIVE, 0, 0 };
    int lands = 0, centre = 0, top = 0, letterbox = 0, tl = 0, br = 0;
    int layout, integer, tx, ty;

    for (layout = 0; layout < 3; layout++)
    for (integer = 0; integer < 2; integer++) {
        struct openmmo_rect d[2];
        int px, py;

        g.layout = layout;
        g.integer = integer;
        openmmo_view_screen_rects(&g, OPENMMO_VIEW_W, OPENMMO_VIEW_H,
                                  OPENMMO_VIEW_SEC_MIN, 1024, 768, d);

        /* The touch screen's centre is its own middle pixel. */
        px = d[1].x + d[1].w / 2;
        py = d[1].y + d[1].h / 2;
        if (openmmo_view_window_to_touch(&g, OPENMMO_VIEW_W, OPENMMO_VIEW_H,
                                         OPENMMO_VIEW_SEC_MIN, 1024, 768,
                                         px, py, &tx, &ty) != 0) lands++;
        else if (!(abs(tx - OPENMMO_VIEW_W / 2) <= 1 &&
                   abs(ty - OPENMMO_VIEW_H / 2) <= 1)) centre++;

        /* A click on the top screen is not a touch, and nor is the letterbox. */
        px = d[0].x + d[0].w / 2;
        py = d[0].y + d[0].h / 2;
        if (openmmo_view_window_to_touch(&g, OPENMMO_VIEW_W, OPENMMO_VIEW_H,
                                         OPENMMO_VIEW_SEC_MIN, 1024, 768,
                                         px, py, &tx, &ty) == 0) top++;
        if (openmmo_view_window_to_touch(&g, OPENMMO_VIEW_W, OPENMMO_VIEW_H,
                                         OPENMMO_VIEW_SEC_MIN, 1024, 768,
                                         0, 0, &tx, &ty) == 0) letterbox++;

        /* Both far corners invert inside range. */
        if (!(openmmo_view_window_to_touch(&g, OPENMMO_VIEW_W, OPENMMO_VIEW_H,
                                           OPENMMO_VIEW_SEC_MIN, 1024, 768,
                                           d[1].x, d[1].y, &tx, &ty) == 0 &&
              tx == 0 && ty == 0)) tl++;
        if (!(openmmo_view_window_to_touch(&g, OPENMMO_VIEW_W, OPENMMO_VIEW_H,
                                           OPENMMO_VIEW_SEC_MIN, 1024, 768,
                                           d[1].x + d[1].w - 1,
                                           d[1].y + d[1].h - 1,
                                           &tx, &ty) == 0 &&
              tx == OPENMMO_VIEW_W - 1 && ty == OPENMMO_VIEW_H - 1)) br++;
    }
    CHECK(lands == 0, "a click on the touch screen lands");
    CHECK(centre == 0, "the touch screen's centre is its centre");
    CHECK(top == 0, "the top screen has no panel on it");
    CHECK(letterbox == 0, "the letterbox has no panel on it");
    CHECK(tl == 0, "the touch screen's top-left corner is (0,0)");
    CHECK(br == 0, "the touch screen's bottom-right corner is (255,191)");
}

/*
 * The Underground, which is the one place the game hands the two screens over the other way
 * round: the world is the touch screen and the second screen is a map of it.
 */
static void test_window_to_touch_swapped(void)
{
    struct openmmo_view_geom g = { 0, OPENMMO_ASPECT_NATIVE, 0, 0 };
    const int layouts[2] = { OPENMMO_LAYOUT_FILL, OPENMMO_LAYOUT_SMART };
    int big = 0, small_ = 0, hidden = 0, unswapped = 0;
    int i, tx, ty;

    g.swapped = 1;
    for (i = 0; i < 2; i++) {
        struct openmmo_rect d[2];

        g.layout = layouts[i];
        g.hide_second = 0;
        openmmo_view_screen_rects(&g, OPENMMO_VIEW_W, OPENMMO_VIEW_H,
                                  OPENMMO_VIEW_SEC_MIN, 1024, 768, d);
        /* The pen is the big rect's, and its middle is the middle. */
        if (openmmo_view_window_to_touch(&g, OPENMMO_VIEW_W, OPENMMO_VIEW_H,
                                         OPENMMO_VIEW_SEC_MIN, 1024, 768,
                                         d[0].x + d[0].w / 2,
                                         d[0].y + d[0].h / 2,
                                         &tx, &ty) != 0
            || abs(tx - OPENMMO_VIEW_W / 2) > 1
            || abs(ty - OPENMMO_VIEW_H / 2) > 1) big++;
        if (openmmo_view_window_to_touch(&g, OPENMMO_VIEW_W, OPENMMO_VIEW_H,
                                         OPENMMO_VIEW_SEC_MIN, 1024, 768,
                                         d[1].x + d[1].w / 2,
                                         d[1].y + d[1].h / 2,
                                         &tx, &ty) == 0) small_++;

        /* ...and it stays the pen with no second screen drawn. */
        g.hide_second = 1;
        if (openmmo_view_window_to_touch(&g, OPENMMO_VIEW_W, OPENMMO_VIEW_H,
                                         OPENMMO_VIEW_SEC_MIN, 1024, 768,
                                         d[0].x + d[0].w / 2,
                                         d[0].y + d[0].h / 2,
                                         &tx, &ty) != 0) hidden++;

        /* Unswapped, the hidden screen is the pen's, and there is nothing
         * under a tap on it. */
        g.swapped = 0;
        openmmo_view_screen_rects(&g, OPENMMO_VIEW_W, OPENMMO_VIEW_H,
                                  OPENMMO_VIEW_SEC_MIN, 1024, 768, d);
        if (openmmo_view_window_to_touch(&g, OPENMMO_VIEW_W, OPENMMO_VIEW_H,
                                         OPENMMO_VIEW_SEC_MIN, 1024, 768,
                                         d[1].x + d[1].w / 2,
                                         d[1].y + d[1].h / 2,
                                         &tx, &ty) == 0) unswapped++;
        g.swapped = 1;
    }
    CHECK(big == 0, "swapped: the pen is the big rect's, middle for middle");
    CHECK(small_ == 0, "swapped: the small rect is the map, and takes no pen");
    CHECK(hidden == 0,
          "swapped: no second screen drawn still leaves the pen alone");
    CHECK(unswapped == 0,
          "unswapped: no second screen drawn is no pen on it");
}

/*
 * The pen against a wide frame. Extra published columns do not take layout space, so the dest
 * *is* the 256-column panel: the left edge is column 0, the right edge is column 255, and a
 * stylus over the dest maps the same 8-bit coordinates the hardware had.
 */
static void test_window_to_touch_wide(void)
{
    struct openmmo_view_geom g = { 0, OPENMMO_ASPECT_NATIVE, 0, 0 };
    int centre = 0, left = 0, right = 0, outside = 0, spans = 0, margins = 0;
    int layout, sw, tx, ty;

    for (layout = 0; layout < 2; layout++)
    for (sw = 258; sw <= (int)OPENMMO_VIEW_WIDE_MAX; sw += 42) {
        struct openmmo_rect d[2];
        int margin = (sw - OPENMMO_VIEW_W) / 2;
        int px, py, seen_lo = 0, seen_hi = 0, i;

        g.layout = layout;
        openmmo_view_screen_rects(&g, sw, OPENMMO_VIEW_H,
                                  OPENMMO_VIEW_SEC_MIN, 1024, 768, d);

        px = d[1].x + d[1].w / 2;
        py = d[1].y + d[1].h / 2;
        if (openmmo_view_window_to_touch(&g, sw, OPENMMO_VIEW_H,
                                         OPENMMO_VIEW_SEC_MIN, 1024, 768,
                                         px, py, &tx, &ty) != 0 ||
            !(abs(tx - OPENMMO_VIEW_W / 2) <= 1 &&
              abs(ty - OPENMMO_VIEW_H / 2) <= 1)) centre++;

        if (!(openmmo_view_window_to_touch(&g, sw, OPENMMO_VIEW_H,
                                           OPENMMO_VIEW_SEC_MIN, 1024, 768,
                                           d[1].x, py, &tx, &ty) == 0 &&
              tx == 0)) left++;
        if (!(openmmo_view_window_to_touch(&g, sw, OPENMMO_VIEW_H,
                                           OPENMMO_VIEW_SEC_MIN, 1024, 768,
                                           d[1].x + d[1].w - 1, py,
                                           &tx, &ty) == 0 &&
              tx == OPENMMO_VIEW_W - 1)) right++;

        for (i = 0; i < d[1].w; i++) {
            if (openmmo_view_window_to_touch(&g, sw, OPENMMO_VIEW_H,
                                             OPENMMO_VIEW_SEC_MIN, 1024, 768,
                                             d[1].x + i, py, &tx, &ty) != 0)
                continue;
            if (tx == 0) seen_lo = 1;
            if (tx == OPENMMO_VIEW_W - 1) seen_hi = 1;
            if (tx < 0 || tx >= OPENMMO_VIEW_W) outside++;
        }
        if (!(seen_lo && seen_hi)) spans++;
        if (margin <= 0) margins++;
    }
    CHECK(centre == 0, "wide: the panel's centre is still (128,96)");
    CHECK(left == 0, "wide: the dest's left edge is column 0");
    CHECK(right == 0, "wide: the dest's right edge is column 255");
    CHECK(outside == 0, "wide: every touch is inside the panel");
    CHECK(spans == 0, "wide: the panel still spans column 0 to column 255");
    CHECK(margins == 0, "wide: a wide frame still publishes extra columns");
}

/*
 * The 256-column panel is what the window scales. A 16:9 frame (340) and a 4:3 frame (256)
 * occupy the same dest in the same window; integer mode snaps to whole panel pixels, not to
 * the published width.
 */
static void test_panel_scale(void)
{
    struct openmmo_view_geom g = { 0, OPENMMO_ASPECT_NATIVE, 0, 0 };
    int same = 0, whole = 0, src_w, layout;
    struct openmmo_rect panel;
    static const int widths[] = { OPENMMO_VIEW_W, 300,
                                  (int)OPENMMO_VIEW_WIDE_MAX };

    for (layout = 0; layout < 3; layout++) {
        struct openmmo_rect native[2];

        g.layout = layout;
        g.integer = 0;
        openmmo_view_screen_rects(&g, OPENMMO_VIEW_W, OPENMMO_VIEW_H,
                                  OPENMMO_VIEW_SEC_MIN, 1920, 1080, native);
        for (src_w = 0; src_w < (int)(sizeof widths / sizeof *widths); src_w++) {
            struct openmmo_rect d[2];

            openmmo_view_screen_rects(&g, widths[src_w], OPENMMO_VIEW_H,
                                      OPENMMO_VIEW_SEC_MIN, 1920, 1080, d);
            if (!(d[0].x == native[0].x && d[0].y == native[0].y &&
                  d[0].w == native[0].w && d[0].h == native[0].h &&
                  d[1].x == native[1].x && d[1].y == native[1].y &&
                  d[1].w == native[1].w && d[1].h == native[1].h))
                same++;
        }
    }
    CHECK(same == 0, "a wide frame's dest is the 256-column panel's dest");

    {
        struct openmmo_rect d[2];

        g.layout = OPENMMO_LAYOUT_SMART;
        g.integer = 0;
        openmmo_view_screen_rects(&g, (int)OPENMMO_VIEW_WIDE_MAX, OPENMMO_VIEW_H,
                                  OPENMMO_VIEW_SEC_MIN, 1920, 1080, d);
        CHECK(d[0].w == 1280 && d[0].h == 960,
              "1080p smart: the panel fills 1280x960, 16:9 or not");
    }

    for (layout = 0; layout < 3; layout++) {
        int cw256, ch256, cw340, ch340;
        struct openmmo_rect r;

        openmmo_view_composed_wh(layout, OPENMMO_VIEW_W, OPENMMO_VIEW_H,
                                 OPENMMO_VIEW_SEC_MIN, &cw256, &ch256);
        openmmo_view_composed_wh(layout, (int)OPENMMO_VIEW_WIDE_MAX,
                                 OPENMMO_VIEW_H, OPENMMO_VIEW_SEC_MIN,
                                 &cw340, &ch340);
        if (!(cw256 == cw340 && ch256 == ch340)) whole++;
        r = openmmo_view_fit(cw340, ch340, 1920, 1080, 1,
                             OPENMMO_ASPECT_NATIVE);
        if (1920 >= cw340 && 1080 >= ch340 &&
            !(r.w % cw340 == 0 && r.h % ch340 == 0)) whole++;
    }
    CHECK(whole == 0, "integer mode snaps to whole 256-column panel pixels");

    openmmo_view_panel_src(256, 192, 1, &panel);
    CHECK(panel.x == 0 && panel.y == 0 && panel.w == 256 && panel.h == 192,
          "a native frame's panel is the whole frame");
    openmmo_view_panel_src(340, 192, 1, &panel);
    CHECK(panel.x == 42 && panel.w == 256 && panel.h == 192,
          "a 16:9 frame's panel is the centre 256 columns");
    openmmo_view_panel_src(680, 384, 2, &panel);
    CHECK(panel.x == 84 * 2 && panel.w == 512 * 2 && panel.h == 384 * 2,
          "HD 16:9: the panel is 512 columns, render-scale applied");
}

/*
 * The scalers are enhancements with no console pixel to compare against, so what is pinned is
 * what the algorithms themselves claim: sizes, block equality, identity on flat colour, and
 * never a colour the source does not already hold.
 */
static void test_scalers(void)
{
    enum { W = 16, H = 12 };
    static uint32_t src[W * H], d2[W * 2 * H * 2], d3[W * 3 * H * 3];
    static uint32_t rep[W * 3 * H * 3];
    uint32_t seed = 0x12345u;
    int i, x, y, blocks = 0;

    for (i = 0; i < W * H; i++) src[i] = 0x336699;
    openmmo_view_scale2x(src, W, H, d2);
    for (i = 0; i < W * 2 * H * 2; i++) if (d2[i] != 0x336699) break;
    CHECK(i == W * 2 * H * 2, "scale2x is the identity on flat colour");
    openmmo_view_scale3x(src, W, H, d3);
    for (i = 0; i < W * 3 * H * 3; i++) if (d3[i] != 0x336699) break;
    CHECK(i == W * 3 * H * 3, "scale3x is the identity on flat colour");

    for (i = 0; i < W * H; i++) src[i] = xs32(&seed) & 0xFFFFFF;
    openmmo_view_scale2x(src, W, H, d2);
    for (i = 0; i < W * 2 * H * 2; i++) {
        int sx = (i % (W * 2)) / 2, sy = (i / (W * 2)) / 2, ok = 0, dx, dy;

        for (dy = -1; dy <= 1 && !ok; dy++)
            for (dx = -1; dx <= 1 && !ok; dx++) {
                int nx = sx + dx, ny = sy + dy;

                if (nx >= 0 && nx < W && ny >= 0 && ny < H &&
                    src[ny * W + nx] == d2[i]) ok = 1;
            }
        if (!ok) break;
    }
    CHECK(i == W * 2 * H * 2, "scale2x never invents a colour");

    openmmo_view_replicate(src, W, H, rep, 3);
    for (y = 0; y < H * 3; y++)
        for (x = 0; x < W * 3; x++)
            if (rep[y * (W * 3) + x] != src[(y / 3) * W + (x / 3)]) blocks++;
    CHECK(blocks == 0, "replicate makes NxN blocks");

    /* Render scale 1 is a copy, so the window has one path and not two. */
    openmmo_view_upscale(src, W, H, d2, NULL, 1, OPENMMO_FILTER_NEAREST);
    for (i = 0; i < W * H; i++) if (d2[i] != src[i]) break;
    CHECK(i == W * H, "render scale 1 is the source, copied");
}

static void test_pace(void)
{
    unsigned i, sum = 0, sum0 = 0;

    CHECK(openmmo_view_pace_ms(0) == 16, "tick 0 is 16 ms");
    CHECK(openmmo_view_pace_ms(1) == 17, "tick 1 is 17 ms");
    CHECK(openmmo_view_pace_ms(2) == 17, "tick 2 is 17 ms");
    for (i = 1; i <= 60; i++)
        sum += openmmo_view_pace_ms(i);
    CHECK(sum == 1000, "ticks 1..60 are 1000 ms (60.000 Hz)");
    for (i = 0; i < 60; i++)
        sum0 += openmmo_view_pace_ms(i);
    CHECK(sum0 == 1000, "ticks 0..59 are 1000 ms too");
}

/*
 * Fill overlay (poketch): 3:2 in the corner, guest inset 4:3. Fill split
 * (bag / battle): a full-height band to the right of the world.
 */
static void test_fill(void)
{
    static const struct { int ww, wh, pw, ph, gw; } over[] = {
        {  640,  384, 256, 192, 256 },
        {  854,  480, 288, 192, 256 },
        { 1280,  720, 321, 214, 285 },
        { 1366,  768, 331, 220, 293 },
        { 1600,  900, 360, 240, 320 },
        { 1920, 1080, 393, 262, 349 },
        { 2560, 1440, 454, 302, 402 },
        { 3840, 2160, 556, 370, 493 },
    };
    static const struct { int ww, wh, pw, gh; } split[] = {
        {  640,  384, 277, 207 },
        {  854,  480, 320, 240 },
        { 1280,  720, 391, 293 },
        { 1366,  768, 404, 303 },
        { 1600,  900, 438, 328 },
        { 1920, 1080, 480, 360 },
        { 2560, 1440, 554, 415 },
        { 3840, 2160, 678, 508 },
    };
    struct openmmo_view_geom g = { OPENMMO_LAYOUT_FILL, OPENMMO_ASPECT_NATIVE, 0, 0 };
    struct openmmo_rect panel, guest, d[2];
    int i, inside = 0, ratio = 0, inset = 0, floor_ = 0, ceil_ = 0;
    int band = 0, want_end = 0, world = 0, pen = 0, chrome = 0, tx, ty;
    int smaller = 0;

    for (i = 0; i < (int)(sizeof over / sizeof over[0]); i++) {
        openmmo_view_fill_panel(over[i].ww, over[i].wh,
                                OPENMMO_VIEW_SEC_MIN, &panel);
        openmmo_view_fill_guest(&panel, &guest);
        if (!(panel.x >= 0 && panel.y >= 0 &&
              panel.x + panel.w <= over[i].ww &&
              panel.y + panel.h <= over[i].wh)) inside++;
        if (!(panel.w == over[i].pw && panel.h == over[i].ph)) ratio++;
        if (!(guest.w == over[i].gw && guest.h == over[i].ph &&
              guest.x == panel.x + (panel.w - guest.w) / 2 &&
              guest.y == panel.y)) inset++;
        if (panel.w < OPENMMO_VIEW_W || panel.h < OPENMMO_VIEW_SEC_MIN_PX)
            floor_++;
        if (panel.h * 2 > over[i].wh) ceil_++;
        if (labs((long)guest.w * 3 - (long)guest.h * 4) > guest.h) inset++;
    }
    CHECK(inside == 0, "fill overlay: the panel stays inside the window");
    CHECK(ratio == 0, "fill overlay: the rest panel matches the size table");
    CHECK(inset == 0, "fill overlay: the guest picture is inset 4:3");
    CHECK(floor_ == 0, "fill overlay: never below 256x192");
    CHECK(ceil_ == 0, "fill overlay: never past half the window");

    openmmo_view_fill_panel(1600, 900, OPENMMO_VIEW_SEC_MAX, &panel);
    if (!(panel.w == 576 && panel.h == 384 &&
          panel.x == 1600 - 576 && panel.y == 900 - 384)) want_end++;
    CHECK(want_end == 0, "fill overlay: the pen-wanted panel is 576x384 at 1600x900");

    g.overlay = 1;
    openmmo_view_screen_rects(&g, 340, 192, OPENMMO_VIEW_SEC_MIN,
                              1600, 900, d);
    if (!(d[0].w > 0 && d[0].h > 0 &&
          d[0].x >= 0 && d[0].y >= 0 &&
          d[0].x + d[0].w <= 1600 && d[0].y + d[0].h <= 900 &&
          labs((long)d[0].w * 192 - (long)d[0].h * 340) <= 340)) world++;
    CHECK(world == 0, "fill overlay: the world is the published top screen, fitted");
    CHECK(d[1].w == 320 && d[1].h == 240,
          "fill overlay: dest[1] is the guest inset, not the 3:2 chrome");

    g.overlay = 0;
    inside = inset = floor_ = ceil_ = 0;
    for (i = 0; i < (int)(sizeof split / sizeof split[0]); i++) {
        openmmo_view_fill_split(split[i].ww, split[i].wh,
                                OPENMMO_VIEW_SEC_MIN, &panel);
        openmmo_view_fill_guest(&panel, &guest);
        if (!(panel.y == 0 && panel.h == split[i].wh &&
              panel.w == split[i].pw &&
              panel.x == split[i].ww - split[i].pw)) band++;
        if (!(guest.w == split[i].pw && guest.h == split[i].gh &&
              guest.x == panel.x &&
              guest.y == panel.y + (panel.h - guest.h) / 2)) inset++;
        if (panel.w < OPENMMO_VIEW_W)
            floor_++;
        if (panel.w * 2 > split[i].ww) ceil_++;
    }
    CHECK(band == 0, "fill split: a full-height band to the right of the world");
    CHECK(inset == 0, "fill split: the guest picture is inset 4:3");
    CHECK(floor_ == 0, "fill split: never below 256 columns");
    CHECK(ceil_ == 0, "fill split: never past half the window");

    /* And at the top of the ramp too, which is where it was not. */
    ceil_ = 0;
    smaller = 0;
    for (i = 0; i < (int)(sizeof split / sizeof split[0]); i++) {
        int sec;

        for (sec = OPENMMO_VIEW_SEC_MIN; sec <= OPENMMO_VIEW_SEC_MAX; sec++) {
            long top, bottom;

            openmmo_view_fill_split(split[i].ww, split[i].wh, sec, &panel);
            if (panel.w * 2 > split[i].ww) ceil_++;
            openmmo_view_screen_rects(&g, 256, 192, sec,
                                      split[i].ww, split[i].wh, d);
            top = (long)d[0].w * d[0].h;
            bottom = (long)d[1].w * d[1].h;
            if (top <= bottom) smaller++;
        }
    }
    CHECK(ceil_ == 0,
          "fill split: never past half the window at any point of the ramp");
    CHECK(smaller == 0,
          "fill split: the world is the larger screen at every window and step");

    openmmo_view_fill_split(1600, 900, OPENMMO_VIEW_SEC_MAX, &panel);
    want_end = 0;
    if (!(panel.h == 900 && panel.w == 700 &&
          panel.y == 0 && panel.x == 1600 - 700)) want_end++;
    CHECK(want_end == 0, "fill split: the pen-wanted band is 700x900 at 1600x900");

    openmmo_view_screen_rects(&g, 340, 192, OPENMMO_VIEW_SEC_MIN,
                              1600, 900, d);
    openmmo_view_fill_split(1600, 900, OPENMMO_VIEW_SEC_MIN, &panel);
    world = 0;
    if (!(d[0].w > 0 && d[0].h > 0 &&
          d[0].x + d[0].w <= panel.x && d[0].y + d[0].h <= 900)) world++;
    CHECK(world == 0, "fill split: the world is fitted left of the band");
    CHECK(d[0].x + d[0].w <= panel.x,
          "fill split: the world does not sit on the second screen");
    CHECK(d[1].w == 438 && d[1].h == 328,
          "fill split: dest[1] is the guest inset in the band");

    if (openmmo_view_window_to_touch(&g, 340, 192, OPENMMO_VIEW_SEC_MIN,
                                     1600, 900,
                                     d[1].x + d[1].w / 2,
                                     d[1].y + d[1].h / 2,
                                     &tx, &ty) != 0) pen++;
    if (openmmo_view_window_to_touch(&g, 340, 192, OPENMMO_VIEW_SEC_MIN,
                                     1600, 900, 8, 8, &tx, &ty) == 0) pen++;
    if (openmmo_view_window_to_touch(&g, 340, 192, OPENMMO_VIEW_SEC_MIN,
                                     1600, 900, panel.x + 2, panel.y + 2,
                                     &tx, &ty) == 0) chrome++;
    CHECK(pen == 0, "fill split: a click on the guest picture is a touch, the world is not");
    CHECK(chrome == 0, "fill split: the band around the guest has no pen on it");
}

static void test_auto_wh(void)
{
    int w, h;

    openmmo_view_auto_wh(1920, 1080, &w, &h);
    CHECK(w == 1600 && h == 900, "a 1080p panel opens at 1600x900");
    openmmo_view_auto_wh(2560, 1440, &w, &h);
    CHECK(w == 1920 && h == 1080, "a 1440p panel opens at 1920x1080");
    openmmo_view_auto_wh(1366, 768, &w, &h);
    CHECK(w == 1024 && h == 576, "a 768-tall laptop opens at 1024x576");
    openmmo_view_auto_wh(800, 600, &w, &h);
    CHECK(w == 856 && h == 480, "below the table still names the 856x480 floor");
    openmmo_view_auto_wh(1920, 1040, &w, &h);
    CHECK(w == 1600 && h == 900,
          "90 % of a 1080p usable rect (taskbar) still picks 1600x900");
}

int view_geom_tests_run(void)
{
    failures = 0;
    printf("view geometry:\n");
    test_fit();
    test_screen_rects();
    test_window_to_touch();
    test_window_to_touch_swapped();
    test_window_to_touch_wide();
    test_panel_scale();
    test_fill();
    test_auto_wh();
    test_scalers();
    test_pace();
    return failures;
}
