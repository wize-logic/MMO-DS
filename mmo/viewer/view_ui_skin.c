/* view_ui_skin.c, see view_ui_skin.h. */

#include "view_ui_skin.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The shades                                                          */
/* ------------------------------------------------------------------ */

/*
 * One family, lifted off VIEW_UI_COL_GROUND, _FIELD and _LINE so that a themed window and the
 * flat primitives an unthemed one falls back to still read as the same program.
 */
#define SK_EDGE      0x5A6675u  /* the lit rim around a raised face */
#define SK_EDGE_DIM  0x39424Du  /* the same rim, pressed or disabled */
#define SK_HILITE    0x78828Fu  /* the gleam under a top edge */

#define SK_BTN_TOP   0x39434Fu
#define SK_BTN_BOT   0x232B35u
#define SK_DOWN_TOP  0x1B222Au
#define SK_DOWN_BOT  0x2E3742u
#define SK_DIS_TOP   0x252B33u
#define SK_DIS_BOT   0x1E242Bu

#define SK_PAN_TOP   0x232A33u
#define SK_PAN_BOT   0x151A21u
#define SK_BODY      0x171C24u

#define SK_SUNK_TOP  0x0D1116u
#define SK_SUNK_BOT  0x171D25u

#define SK_TITLE_TOP 0x323C48u
#define SK_TITLE_BOT 0x212932u
#define SK_FLOOR_TOP 0x1E242Cu
#define SK_FLOOR_BOT 0x161B22u

#define SK_HEAD_TOP  0x2C343Eu
#define SK_HEAD_BOT  0x1F262Du

/* The resting tab carries the table's own #C5C5C5 down to four fifths
 * (view_ui_theme_tint), so it is painted well clear of the window body it
 * sits against or it arrives the same value as the body and disappears. */
#define SK_TAB_TOP   0x39424Eu
#define SK_TAB_BOT   0x2A323Cu
#define SK_TABA_TOP  0x3B4653u
#define SK_TABA_BOT  0x2A333Eu

#define SK_TROUGH_TOP 0x0B0F14u
#define SK_TROUGH_BOT 0x1A212Au

#define SK_WARN_EDGE 0xC08A3Cu  /* the confirm box says so at its rim */

/* ------------------------------------------------------------------ */
/* The raster                                                          */
/* ------------------------------------------------------------------ */

struct sk_buf {
    unsigned char *p;
    int w, h;
};

/* Which corners a shape rounds. A tab and a title band round only the two at
 * the top; a floor only the two at the bottom. */
#define SK_TL  1u
#define SK_TR  2u
#define SK_BL  4u
#define SK_BR  8u
#define SK_ALL (SK_TL | SK_TR | SK_BL | SK_BR)

static uint32_t sk_mix(uint32_t a, uint32_t b, int t)
{
    unsigned r = (((a >> 16) & 255u) * (unsigned)(255 - t)
                  + ((b >> 16) & 255u) * (unsigned)t) / 255u;
    unsigned g = (((a >> 8) & 255u) * (unsigned)(255 - t)
                  + ((b >> 8) & 255u) * (unsigned)t) / 255u;
    unsigned l = ((a & 255u) * (unsigned)(255 - t)
                  + (b & 255u) * (unsigned)t) / 255u;

    return r << 16 | g << 8 | l;
}

/* Source-over one pixel, so a gleam laid on a gradient is the gradient plus
 * the gleam rather than a hole punched in it. */
static void sk_over(struct sk_buf *b, int x, int y, uint32_t rgb, unsigned a)
{
    unsigned char *o;
    unsigned da, keep, out;

    if (b == NULL || b->p == NULL || a == 0u ||
        x < 0 || y < 0 || x >= b->w || y >= b->h)
        return;
    if (a > 255u)
        a = 255u;
    o = b->p + ((size_t)y * (size_t)b->w + (size_t)x) * 4u;
    da = o[3];
    keep = da * (255u - a) / 255u;
    out = a + keep;
    if (out == 0u)
        return;
    o[0] = (unsigned char)((((rgb >> 16) & 255u) * a + o[0] * keep) / out);
    o[1] = (unsigned char)((((rgb >> 8) & 255u) * a + o[1] * keep) / out);
    o[2] = (unsigned char)(((rgb & 255u) * a + o[2] * keep) / out);
    o[3] = (unsigned char)out;
}

/*
 * How much of pixel (x,y) falls inside the rounded rect [0,w) by [0,h), out of sixteen, at
 * four-by-four supersampling. Eighths of a pixel throughout, which is what keeps the answer
 * the same on both word sizes.
 */
static int sk_cover(int x, int y, int w, int h, int r, unsigned corners)
{
    int sx, sy, n = 0, w8, h8, r8;

    if (w < 1 || h < 1)
        return 0;
    if (r < 0)
        r = 0;
    if (r * 2 > w)
        r = w / 2;
    if (r * 2 > h)
        r = h / 2;
    w8 = w * 8;
    h8 = h * 8;
    r8 = r * 8;
    for (sy = 0; sy < 4; sy++) {
        for (sx = 0; sx < 4; sx++) {
            int px = x * 8 + sx * 2 + 1;
            int py = y * 8 + sy * 2 + 1;
            int cx, cy, dx, dy;

            if (px < 0 || py < 0 || px > w8 || py > h8)
                continue;
            if (r8 == 0) {
                n++;
                continue;
            }
            if (px < r8 && py < r8 && (corners & SK_TL) != 0u) {
                cx = r8; cy = r8;
            } else if (px > w8 - r8 && py < r8 && (corners & SK_TR) != 0u) {
                cx = w8 - r8; cy = r8;
            } else if (px < r8 && py > h8 - r8 && (corners & SK_BL) != 0u) {
                cx = r8; cy = h8 - r8;
            } else if (px > w8 - r8 && py > h8 - r8 && (corners & SK_BR) != 0u) {
                cx = w8 - r8; cy = h8 - r8;
            } else {
                n++;
                continue;
            }
            dx = px - cx;
            dy = py - cy;
            if (dx * dx + dy * dy <= r8 * r8)
                n++;
        }
    }
    return n;
}

/* A filled rounded rect under a vertical gradient. */
static void sk_rrect(struct sk_buf *b, int x0, int y0, int w, int h, int r,
                     unsigned corners, uint32_t top, uint32_t bot, unsigned a)
{
    int x, y;

    for (y = 0; y < h; y++) {
        uint32_t c = sk_mix(top, bot, h > 1 ? y * 255 / (h - 1) : 0);

        for (x = 0; x < w; x++) {
            int cov = sk_cover(x, y, w, h, r, corners);

            if (cov > 0)
                sk_over(b, x0 + x, y0 + y, c, a * (unsigned)cov / 16u);
        }
    }
}

/* Its one-pixel rim: the difference between the shape and the same shape
 * inset by one, which is a rounded border with the corners already eased. */
static void sk_rring(struct sk_buf *b, int x0, int y0, int w, int h, int r,
                     unsigned corners, uint32_t rgb, unsigned a)
{
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int o = sk_cover(x, y, w, h, r, corners);
            int i = sk_cover(x - 1, y - 1, w - 2, h - 2, r - 1, corners);

            if (o - i > 0)
                sk_over(b, x0 + x, y0 + y, rgb, a * (unsigned)(o - i) / 16u);
        }
    }
}

static void sk_hline(struct sk_buf *b, int x0, int y0, int w, uint32_t rgb,
                     unsigned a)
{
    int x;

    for (x = 0; x < w; x++)
        sk_over(b, x0 + x, y0, rgb, a);
}

/* ------------------------------------------------------------------ */
/* The surfaces                                                        */
/* ------------------------------------------------------------------ */

/* The close handle is the one shape that is not a rectangle: two diagonals
 * inside the box, which in eighths is just a pair of distances to compare. */
static void sk_cross(struct sk_buf *b, int w, int h)
{
    int x, y, in0 = 3 * 8, in1 = (w - 3) * 8;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int sx, sy, n = 0;

            for (sy = 0; sy < 4; sy++) {
                for (sx = 0; sx < 4; sx++) {
                    int px = x * 8 + sx * 2 + 1;
                    int py = y * 8 + sy * 2 + 1;
                    int d1, d2;

                    if (px < in0 || px > in1 || py < in0 || py > in1)
                        continue;
                    d1 = px - py;
                    d2 = px + py - in0 - in1;
                    if (d1 < 0) d1 = -d1;
                    if (d2 < 0) d2 = -d2;
                    if (d1 <= 9 || d2 <= 9)
                        n++;
                }
            }
            if (n > 0)
                sk_over(b, x, y, VIEW_UI_COL_DIM, 255u * (unsigned)n / 16u);
        }
    }
}

static void sk_paint(int id, struct sk_buf *b, int w, int h)
{
    switch (id) {
    case VIEW_UI_TH_PANEL:
        sk_rrect(b, 0, 0, w, h, 5, SK_ALL, SK_PAN_TOP, SK_PAN_BOT, 250);
        sk_rring(b, 0, 0, w, h, 5, SK_ALL, SK_EDGE, 255);
        sk_hline(b, 6, 1, w - 12, SK_HILITE, 46);
        break;
    case VIEW_UI_TH_WARN:
        sk_rrect(b, 0, 0, w, h, 8, SK_ALL, SK_PAN_TOP, SK_PAN_BOT, 252);
        sk_rring(b, 0, 0, w, h, 8, SK_ALL, SK_WARN_EDGE, 255);
        sk_hline(b, 9, 1, w - 18, SK_HILITE, 40);
        break;
    case VIEW_UI_TH_BUTTON:
        sk_rrect(b, 0, 0, w, h, 4, SK_ALL, SK_BTN_TOP, SK_BTN_BOT, 255);
        sk_rring(b, 0, 0, w, h, 4, SK_ALL, SK_EDGE, 255);
        sk_hline(b, 5, 1, w - 10, SK_HILITE, 70);
        break;
    /*
     * The hover is a wash over the button, not a button of its own: the bar draws the default
     * first and this second (view_ui_bar.c), and the table hands it the official client's own main-color at
     * the hover animation's alpha (0x995DB1FF, view_ui_theme_tint).
     */
    case VIEW_UI_TH_BUTTON_HOVER:
        sk_rrect(b, 0, 0, w, h, 4, SK_ALL, 0xFFFFFFu, 0xA8C0D8u, 255);
        sk_rring(b, 0, 0, w, h, 4, SK_ALL, 0xFFFFFFu, 255);
        break;
    case VIEW_UI_TH_BUTTON_DOWN:
        sk_rrect(b, 0, 0, w, h, 4, SK_ALL, SK_DOWN_TOP, SK_DOWN_BOT, 255);
        sk_rring(b, 0, 0, w, h, 4, SK_ALL, SK_EDGE_DIM, 255);
        break;
    case VIEW_UI_TH_BUTTON_DIS:
        sk_rrect(b, 0, 0, w, h, 4, SK_ALL, SK_DIS_TOP, SK_DIS_BOT, 255);
        sk_rring(b, 0, 0, w, h, 4, SK_ALL, SK_EDGE_DIM, 200);
        break;
    case VIEW_UI_TH_ROW_HOVER:
        sk_rrect(b, 0, 0, w, h, 4, SK_ALL, 0x2E3E4Bu, 0x22303Bu, 255);
        sk_rring(b, 0, 0, w, h, 4, SK_ALL, VIEW_UI_COL_ACCENT, 130);
        break;
    /*
     * One painter for two ids on purpose: the table cuts the table row out of the input box's
     * own texels (326,8,16,16) and insets it at draw time, so they are the same region of the
     * same sheet. Painting them differently would mean whichever ran second won.
     */
    case VIEW_UI_TH_INPUT:
    case VIEW_UI_TH_ROW:
        sk_rrect(b, 0, 0, w, h, 3, SK_ALL, SK_SUNK_TOP, SK_SUNK_BOT, 255);
        sk_hline(b, 3, 1, w - 6, 0x000000u, 60);
        sk_rring(b, 0, 0, w, h, 3, SK_ALL, SK_EDGE_DIM, 255);
        break;
    case VIEW_UI_TH_HEADER:
        sk_rrect(b, 0, 0, w, h, 3, SK_ALL, SK_HEAD_TOP, SK_HEAD_BOT, 255);
        sk_rring(b, 0, 0, w, h, 3, SK_ALL, SK_EDGE_DIM, 255);
        sk_hline(b, 3, 1, w - 6, SK_HILITE, 48);
        sk_hline(b, 1, h - 1, w - 2, SK_EDGE, 190);
        break;
    case VIEW_UI_TH_TAB:
        sk_rrect(b, 0, 0, w, h, 5, SK_TL | SK_TR, SK_TAB_TOP, SK_TAB_BOT, 255);
        sk_rring(b, 0, 0, w, h, 5, SK_TL | SK_TR, SK_EDGE_DIM, 255);
        break;
    case VIEW_UI_TH_TAB_ACTIVE:
        sk_rrect(b, 0, 0, w, h, 5, SK_TL | SK_TR, SK_TABA_TOP, SK_TABA_BOT,
                 255);
        sk_rring(b, 0, 0, w, h, 5, SK_TL | SK_TR, SK_EDGE, 255);
        sk_hline(b, 5, 1, w - 10, VIEW_UI_COL_ACCENT, 210);
        sk_hline(b, 5, 2, w - 10, VIEW_UI_COL_ACCENT, 90);
        break;
    /*
     * The window chrome, in the four bands the table cuts it into: the corner row and the
     * title band above the stretching body, and the floor under it.
     */
    case VIEW_UI_TH_FRAME: {
        int title = 26, floor_y = h - 6;

        sk_rrect(b, 0, 0, w, h, 6, SK_ALL, SK_BODY, SK_BODY, 252);
        sk_rrect(b, 0, 0, w, title, 6, SK_TL | SK_TR, SK_TITLE_TOP,
                 SK_TITLE_BOT, 255);
        sk_rrect(b, 0, floor_y, w, h - floor_y, 6, SK_BL | SK_BR,
                 SK_FLOOR_TOP, SK_FLOOR_BOT, 255);
        sk_hline(b, 1, title - 1, w - 2, SK_EDGE_DIM, 220);
        sk_hline(b, 6, 1, w - 12, SK_HILITE, 60);
        sk_rring(b, 0, 0, w, h, 6, SK_ALL, SK_EDGE, 255);
        break;
    }
    case VIEW_UI_TH_CLOSE:
        sk_cross(b, w, h);
        break;
    /* Three one-texel columns and no room to round anything: the trough is a
     * gradient with a rim on the two edges that are actually visible. */
    case VIEW_UI_TH_HPBAR: {
        int x, y;

        for (y = 0; y < h; y++) {
            uint32_t c = sk_mix(SK_TROUGH_TOP, SK_TROUGH_BOT,
                                h > 1 ? y * 255 / (h - 1) : 0);

            for (x = 0; x < w; x++)
                sk_over(b, x, y, c, 255);
        }
        sk_hline(b, 0, 0, w, SK_EDGE_DIM, 255);
        sk_hline(b, 0, h - 1, w, SK_EDGE_DIM, 160);
        break;
    }
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* The sheets                                                          */
/* ------------------------------------------------------------------ */

int view_ui_skin_make(struct view_ui_theme *t)
{
    int i, id;

    if (t == NULL)
        return 0;
    memset(t, 0, sizeof *t);
    for (i = 0; i < VIEW_UI_SHEET_N; i++) {
        int w = 0, h = 0;

        view_ui_sheet_min(i, &w, &h);
        if (w < 1 || h < 1)
            goto fail;
        t->sheet[i].rgba = calloc((size_t)w * (size_t)h, 4u);
        if (t->sheet[i].rgba == NULL)
            goto fail;
        t->sheet[i].w = w;
        t->sheet[i].h = h;
    }
    for (id = 0; id < VIEW_UI_TH_N; id++) {
        struct view_ui_th_pair cells[VIEW_UI_TH_CELLS];
        struct view_ui_theme_px *sh;
        struct sk_buf art;
        int n, w = 0, h = 0, c, sheet;

        n = view_ui_theme_source(id, cells, &w, &h);
        sheet = view_ui_theme_sheet(id);
        if (n < 1 || w < 1 || h < 1 || sheet < 0 || sheet >= VIEW_UI_SHEET_N)
            goto fail;
        art.w = w;
        art.h = h;
        art.p = calloc((size_t)w * (size_t)h, 4u);
        if (art.p == NULL)
            goto fail;
        sk_paint(id, &art, w, h);
        /*
         * Out of the surface's own footprint and onto the sheet cell by cell, because the
         * table is free to scatter them: frame-draggable's four rows are not one rectangle of
         * anybody's atlas, and its floor samples three texels of an eight-wide column.
         */
        sh = &t->sheet[sheet];
        for (c = 0; c < n; c++) {
            const struct openmmo_rect *s = &cells[c].src;
            const struct openmmo_rect *d = &cells[c].dst;
            int y;

            if (s->w < 1 || s->h < 1 || s->x < 0 || s->y < 0 ||
                s->x + s->w > sh->w || s->y + s->h > sh->h ||
                d->x < 0 || d->y < 0 ||
                d->x + s->w > w || d->y + s->h > h) {
                free(art.p);
                goto fail;
            }
            for (y = 0; y < s->h; y++)
                memcpy(sh->rgba
                       + ((size_t)(s->y + y) * (size_t)sh->w
                          + (size_t)s->x) * 4u,
                       art.p
                       + ((size_t)(d->y + y) * (size_t)w
                          + (size_t)d->x) * 4u,
                       (size_t)s->w * 4u);
        }
        free(art.p);
    }
    t->ready = 1;
    return 1;

fail:
    view_ui_theme_free(t);
    return 0;
}
