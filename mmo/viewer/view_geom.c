/* The geometry and the scalers. See view_geom.h for what each of
 * these owes; tests/view_geom_test.c is where they are held to it. */

#include <string.h>

#include "view_geom.h"

struct openmmo_rect openmmo_view_fit(int src_w, int src_h, int win_w, int win_h,
                                     int integer, int aspect)
{
    struct openmmo_rect r;
    long sw = src_w, sh = src_h;
    int w, h;

    if (sw < 1) sw = 1;
    if (sh < 1) sh = 1;
    if (win_w < 1) win_w = 1;
    if (win_h < 1) win_h = 1;

    if (aspect == OPENMMO_ASPECT_STRETCH) {
        r.x = 0; r.y = 0; r.w = win_w; r.h = win_h;
        return r;
    }

    if ((long)win_w * sh <= (long)win_h * sw) {     /* width-limited  */
        w = win_w;
        h = (int)(((long)win_w * sh) / sw);
    } else {                                        /* height-limited */
        h = win_h;
        w = (int)(((long)win_h * sw) / sh);
    }
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    if (integer && win_w >= sw && win_h >= sh) {
        int s = (int)(win_w / sw);
        int t = (int)(win_h / sh);

        if (t < s) s = t;
        w = (int)sw * s;
        h = (int)sh * s;
    }

    r.w = w;
    r.h = h;
    r.x = (win_w - w) / 2;
    r.y = (win_h - h) / 2;
    return r;
}

void openmmo_view_composed_wh(int layout, int src_w, int src_h, int sec,
                              int *cw, int *ch)
{
    int hds;

    if (src_h < OPENMMO_VIEW_H) src_h = OPENMMO_VIEW_H;
    hds = src_h / OPENMMO_VIEW_H;
    if (hds < 1) hds = 1;
    if (layout == OPENMMO_LAYOUT_FILL) {
        /* The world is the published top screen, extra columns included. */
        if (src_w < OPENMMO_VIEW_W * hds) src_w = OPENMMO_VIEW_W * hds;
        *cw = src_w;
        *ch = OPENMMO_VIEW_H * hds;
        return;
    }
    (void)src_w;
    /* Extra published columns do not take layout space. The panel is 256
     * columns at native resolution, times the internal-resolution multiple
     * named by the published height. */
    src_w = OPENMMO_VIEW_W * hds;
    src_h = OPENMMO_VIEW_H * hds;
    if (layout == OPENMMO_LAYOUT_WIDE)       { *cw = src_w * 2; *ch = src_h; }
    else if (layout == OPENMMO_LAYOUT_SMART) { *cw = src_w + src_w * sec / 100;
                                               *ch = src_h; }
    else                                     { *cw = src_w; *ch = src_h * 2; }
}

void openmmo_view_auto_wh(int usable_w, int usable_h, int *w, int *h)
{
    static const int table[][2] = {
        { 2560, 1440 },
        { 1920, 1080 },
        { 1600,  900 },
        { 1366,  768 },
        { 1280,  720 },
        { 1024,  576 },
        {  856,  480 },
    };
    int cap_w, cap_h, i;

    if (w == NULL || h == NULL) return;
    if (usable_w < 1) usable_w = 1;
    if (usable_h < 1) usable_h = 1;
    cap_w = usable_w * 90 / 100;
    cap_h = usable_h * 90 / 100;
    for (i = 0; i < (int)(sizeof table / sizeof table[0]); i++) {
        if (table[i][0] <= cap_w && table[i][1] <= cap_h) {
            *w = table[i][0];
            *h = table[i][1];
            return;
        }
    }
    *w = 856;
    *h = 480;
}

static int view_isqrt(unsigned n)
{
    unsigned x, y;

    if (n <= 1u) return (int)n;
    x = n;
    y = (x + 1u) / 2u;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2u;
    }
    return (int)x;
}

static int view_clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void fill_panel_rest(int win_w, int win_h, struct openmmo_rect *panel)
{
    int cap_h, h, w;

    if (win_w < 1) win_w = 1;
    if (win_h < 1) win_h = 1;
    cap_h = win_h / 2;
    h = view_clampi(view_isqrt((unsigned)win_h * (unsigned)OPENMMO_VIEW_OVER_REF),
                    OPENMMO_VIEW_SEC_MIN_PX, cap_h);
    w = h * 3 / 2;
    if (w > win_w * 36 / 100) w = win_w * 36 / 100;
    if (h > w * 2 / 3) h = w * 2 / 3;
    if (w < OPENMMO_VIEW_W) w = OPENMMO_VIEW_W > win_w ? win_w : OPENMMO_VIEW_W;
    if (h < OPENMMO_VIEW_SEC_MIN_PX)
        h = OPENMMO_VIEW_SEC_MIN_PX > win_h ? win_h : OPENMMO_VIEW_SEC_MIN_PX;
    if (w > win_w) w = win_w;
    if (h > win_h) h = win_h;
    panel->w = w;
    panel->h = h;
    panel->x = win_w - w;
    panel->y = win_h - h;
}

void openmmo_view_fill_panel(int win_w, int win_h, int sec,
                             struct openmmo_rect *panel)
{
    struct openmmo_rect rest;
    int want_w, want_h, t, den;

    if (panel == NULL) return;
    if (win_w < 1) win_w = 1;
    if (win_h < 1) win_h = 1;
    if (sec < OPENMMO_VIEW_SEC_MIN) sec = OPENMMO_VIEW_SEC_MIN;
    if (sec > OPENMMO_VIEW_SEC_MAX) sec = OPENMMO_VIEW_SEC_MAX;
    fill_panel_rest(win_w, win_h, &rest);
    want_h = rest.h * 8 / 5;
    want_w = want_h * 3 / 2;
    if (want_w > win_w * 46 / 100) want_w = win_w * 46 / 100;
    if (want_h > want_w * 2 / 3) want_h = want_w * 2 / 3;
    t = sec - OPENMMO_VIEW_SEC_MIN;
    den = OPENMMO_VIEW_SEC_MAX - OPENMMO_VIEW_SEC_MIN;
    panel->w = rest.w + (want_w - rest.w) * t / den;
    panel->h = rest.h + (want_h - rest.h) * t / den;
    panel->x = win_w - panel->w;
    panel->y = win_h - panel->h;
}

static void fill_split_rest(int win_w, int win_h, struct openmmo_rect *panel)
{
    int cap_w, w;

    if (win_w < 1) win_w = 1;
    if (win_h < 1) win_h = 1;
    cap_w = win_w / 2;
    w = view_clampi(view_isqrt((unsigned)win_w * (unsigned)OPENMMO_VIEW_SEC_REF),
                    OPENMMO_VIEW_W, cap_w);
    if (w > win_w) w = win_w;
    panel->w = w;
    panel->h = win_h;
    panel->x = win_w - w;
    panel->y = 0;
}

void openmmo_view_fill_split(int win_w, int win_h, int sec,
                             struct openmmo_rect *panel)
{
    struct openmmo_rect rest;
    int want_w, t, den;

    if (panel == NULL) return;
    if (win_w < 1) win_w = 1;
    if (win_h < 1) win_h = 1;
    if (sec < OPENMMO_VIEW_SEC_MIN) sec = OPENMMO_VIEW_SEC_MIN;
    if (sec > OPENMMO_VIEW_SEC_MAX) sec = OPENMMO_VIEW_SEC_MAX;
    fill_split_rest(win_w, win_h, &rest);
    want_w = rest.w * 8 / 5;
    if (want_w > win_w) want_w = win_w;
    t = sec - OPENMMO_VIEW_SEC_MIN;
    den = OPENMMO_VIEW_SEC_MAX - OPENMMO_VIEW_SEC_MIN;
    panel->h = win_h;
    panel->w = rest.w + (want_w - rest.w) * t / den;
    panel->x = win_w - panel->w;
    panel->y = 0;
}

void openmmo_view_fill_guest(const struct openmmo_rect *panel,
                             struct openmmo_rect *guest)
{
    int w, h;

    if (panel == NULL || guest == NULL) return;
    w = panel->h * 4 / 3;
    h = panel->h;
    if (w > panel->w) {
        w = panel->w;
        h = panel->w * 3 / 4;
    }
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    guest->w = w;
    guest->h = h;
    guest->x = panel->x + (panel->w - w) / 2;
    guest->y = panel->y + (panel->h - h) / 2;
}

void openmmo_view_panel_src(int src_w, int src_h, int rs,
                            struct openmmo_rect *src)
{
    int hds, panel_w, margin;

    if (src == NULL) return;
    if (src_h < OPENMMO_VIEW_H) src_h = OPENMMO_VIEW_H;
    if (src_w < OPENMMO_VIEW_W) src_w = OPENMMO_VIEW_W;
    if (rs < 1) rs = 1;
    hds = src_h / OPENMMO_VIEW_H;
    if (hds < 1) hds = 1;
    panel_w = OPENMMO_VIEW_W * hds;
    if (src_w < panel_w) src_w = panel_w;
    margin = (src_w - panel_w) / 2;
    src->x = margin * rs;
    src->y = 0;
    src->w = panel_w * rs;
    src->h = src_h * rs;
}

struct openmmo_rect openmmo_view_screen_rects(const struct openmmo_view_geom *g,
                                              int src_w, int src_h, int sec,
                                              int win_w, int win_h,
                                              struct openmmo_rect dst[2])
{
    struct openmmo_rect r;
    int cw, ch;

    if (g->layout == OPENMMO_LAYOUT_FILL) {
        struct openmmo_rect panel;
        int sw = src_w, sh = src_h;
        int swap = openmmo_view_swaps_screens(g);

        if (sw < 1) sw = 1;
        if (sh < 1) sh = 1;
        if (g->overlay) {
            r = openmmo_view_fit(sw, sh, win_w, win_h, g->integer, g->aspect);
            dst[0] = r;
            openmmo_view_fill_panel(win_w, win_h, sec, &panel);
            if (swap) panel.x = win_w - panel.w - panel.x;
            openmmo_view_fill_guest(&panel, &dst[1]);
            return r;
        }
        openmmo_view_fill_split(win_w, win_h, sec, &panel);
        if (swap) panel.x = win_w - panel.w - panel.x;
        openmmo_view_fill_guest(&panel, &dst[1]);
        {
            /* The big rect takes what the panel left, on whichever side of it
             * that is. */
            int world_w = swap ? win_w - (panel.x + panel.w) : panel.x;

            if (world_w < 1) world_w = 1;
            r = openmmo_view_fit(sw, sh, world_w, win_h, g->integer, g->aspect);
            if (swap) r.x += panel.x + panel.w;
        }
        dst[0] = r;
        return r;
    }

    openmmo_view_composed_wh(g->layout, src_w, src_h, sec, &cw, &ch);
    r = openmmo_view_fit(cw, ch, win_w, win_h, g->integer, g->aspect);

    if (g->layout == OPENMMO_LAYOUT_SMART) {
        /* The main screen takes the height; the touch screen keeps the same
         * proportions at `sec` per cent of it and sits centred beside it, so
         * neither is ever stretched. */
        int w0 = (int)(((long)r.w * 100) / (100 + sec));
        int w1 = r.w - w0;
        /* The height comes from the width the touch screen actually got, not
         * from `sec` again: the two roundings disagree by a pixel, and the
         * visible cost of that is a touch screen slightly out of proportion
         * where the cost of taking the remainder is nothing. */
        int h1 = w0 > 0 ? (int)(((long)w1 * r.h) / w0) : 0;

        dst[0].x = r.x;      dst[0].y = r.y;                 dst[0].w = w0; dst[0].h = r.h;
        dst[1].x = r.x + w0; dst[1].y = r.y + (r.h - h1) / 2; dst[1].w = w1; dst[1].h = h1;

        /* Swapped, the small one goes to the far side too, so it is not where
         * the touch screen was a moment ago. */
        if (openmmo_view_swaps_screens(g)) {
            dst[0].x = r.x + w1;
            dst[1].x = r.x;
        }
    } else if (g->layout == OPENMMO_LAYOUT_WIDE) {
        int w0 = r.w / 2;

        dst[0].x = r.x;      dst[0].y = r.y; dst[0].w = w0;       dst[0].h = r.h;
        dst[1].x = r.x + w0; dst[1].y = r.y; dst[1].w = r.w - w0; dst[1].h = r.h;
    } else {
        int h0 = r.h / 2;

        dst[0].x = r.x; dst[0].y = r.y;      dst[0].w = r.w; dst[0].h = h0;
        dst[1].x = r.x; dst[1].y = r.y + h0; dst[1].w = r.w; dst[1].h = r.h - h0;
    }
    return r;
}

int openmmo_view_window_to_touch(const struct openmmo_view_geom *g,
                                 int src_w, int src_h, int sec,
                                 int win_w, int win_h,
                                 int mx, int my, int *tx, int *ty)
{
    struct openmmo_rect dst[2], touch, panel;
    int swap = openmmo_view_swaps_screens(g);
    int x, y, hds;

    if (src_h < OPENMMO_VIEW_H) src_h = OPENMMO_VIEW_H;
    /* Extra published columns are not on screen: the dest is the panel. */
    hds = src_h / OPENMMO_VIEW_H;
    if (hds < 1) hds = 1;
    if (src_w < OPENMMO_VIEW_W * hds) src_w = OPENMMO_VIEW_W * hds;
    /* Swapped, the touch screen is the big rect, and that one is laid out from
     * the whole published frame rather than from the panel's width. */
    openmmo_view_screen_rects(g, swap ? src_w : OPENMMO_VIEW_W * hds, src_h,
                             sec, win_w, win_h, dst);
    touch = swap ? dst[0] : dst[1];
    /* The window drawing no second screen means there is nothing under a pen
     * aimed at it. Only when the pen's own rect is the one left out, though:
     * swapped, the pen goes to the big rect and that one is always drawn. */
    if (g->hide_second && !swap) return -1;
    /*
     * ...and only the middle of that frame is the console's own 256 columns. A wide frame's
     * extra columns are 3D-or-black beside the panel, and a pen mapped through them lands
     * tiles away from where it was put down.
     */
    if (swap && g->layout == OPENMMO_LAYOUT_FILL && touch.w > 0 && src_w > 0) {
        openmmo_view_panel_src(src_w, src_h, 1, &panel);
        touch.x += (int)(((long)panel.x * touch.w) / src_w);
        touch.w = (int)(((long)panel.w * touch.w) / src_w);
    }
    if (touch.w < 1 || touch.h < 1) return -1;
    if (mx < touch.x || mx >= touch.x + touch.w ||
        my < touch.y || my >= touch.y + touch.h)
        return -1;
    x = (int)(((long)(mx - touch.x) * OPENMMO_VIEW_W) / touch.w);
    y = (int)(((long)(my - touch.y) * OPENMMO_VIEW_H) / touch.h);
    if (x < 0) x = 0;
    if (x >= OPENMMO_VIEW_W) x = OPENMMO_VIEW_W - 1;
    if (y < 0) y = 0;
    if (y >= OPENMMO_VIEW_H) y = OPENMMO_VIEW_H - 1;
    *tx = x;
    *ty = y;
    return 0;
}

void openmmo_view_replicate(const uint32_t *s, int w, int h, uint32_t *d, int f)
{
    int x, y, i, j;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            uint32_t p = s[(size_t)y * w + x];

            for (j = 0; j < f; j++) {
                uint32_t *row = d + ((size_t)y * f + j) * ((size_t)w * f);

                for (i = 0; i < f; i++) row[(size_t)x * f + i] = p;
            }
        }
    }
}

/* EPX / Scale2x: a corner leans toward a neighbour only where the two adjacent
 * neighbours agree and the two opposite ones do not. */
void openmmo_view_scale2x(const uint32_t *s, int w, int h, uint32_t *d)
{
    int x, y;

    for (y = 0; y < h; y++) {
        const uint32_t *row = s + (size_t)y * w;
        const uint32_t *up  = y > 0     ? row - w : row;
        const uint32_t *dn  = y + 1 < h ? row + w : row;
        uint32_t *o0 = d + (size_t)(2 * y) * (2 * w);
        uint32_t *o1 = o0 + (size_t)(2 * w);

        for (x = 0; x < w; x++) {
            uint32_t P = row[x];
            uint32_t A = up[x];                      /* above */
            uint32_t B = x + 1 < w ? row[x + 1] : P; /* right */
            uint32_t C = x > 0     ? row[x - 1] : P; /* left  */
            uint32_t D = dn[x];                      /* below */

            o0[2 * x]     = (C == A && C != D && A != B) ? A : P;
            o0[2 * x + 1] = (A == B && A != C && B != D) ? B : P;
            o1[2 * x]     = (D == C && D != B && C != A) ? C : P;
            o1[2 * x + 1] = (B == D && B != A && D != C) ? D : P;
        }
    }
}

/* AdvMAME3x, the 3x member of the same family. */
void openmmo_view_scale3x(const uint32_t *s, int w, int h, uint32_t *d)
{
    int x, y;

    for (y = 0; y < h; y++) {
        const uint32_t *row = s + (size_t)y * w;
        const uint32_t *up  = y > 0     ? row - w : row;
        const uint32_t *dn  = y + 1 < h ? row + w : row;
        uint32_t *o0 = d + (size_t)(3 * y) * (3 * w);
        uint32_t *o1 = o0 + (size_t)(3 * w);
        uint32_t *o2 = o1 + (size_t)(3 * w);

        for (x = 0; x < w; x++) {
            int xl = x > 0 ? x - 1 : x, xr = x + 1 < w ? x + 1 : x;
            uint32_t A = up[xl],  B = up[x],  C = up[xr];
            uint32_t D = row[xl], E = row[x], F = row[xr];
            uint32_t G = dn[xl],  H = dn[x],  I = dn[xr];

            o0[3 * x]     = (D == B && B != F && D != H) ? D : E;
            o0[3 * x + 1] = ((D == B && B != F && D != H && E != C) ||
                             (B == F && B != D && F != H && E != A)) ? B : E;
            o0[3 * x + 2] = (B == F && B != D && F != H) ? F : E;
            o1[3 * x]     = ((D == B && B != F && D != H && E != G) ||
                             (D == H && D != B && H != F && E != A)) ? D : E;
            o1[3 * x + 1] = E;
            o1[3 * x + 2] = ((B == F && B != D && F != H && E != I) ||
                             (H == F && D != H && B != F && E != C)) ? F : E;
            o2[3 * x]     = (D == H && D != B && H != F) ? D : E;
            o2[3 * x + 1] = ((D == H && D != B && H != F && E != I) ||
                             (H == F && D != H && B != F && E != G)) ? H : E;
            o2[3 * x + 2] = (H == F && D != H && B != F) ? F : E;
        }
    }
}

void openmmo_view_upscale(const uint32_t *src, int w, int h,
                          uint32_t *dst, uint32_t *mid, int rs, int filter)
{
    if (rs <= 1) {
        memcpy(dst, src, (size_t)w * h * sizeof *src);
        return;
    }
    if (filter == OPENMMO_FILTER_SCALE2X) {
        if (rs == 2) { openmmo_view_scale2x(src, w, h, dst); return; }
        if (rs == 3) { openmmo_view_scale3x(src, w, h, dst); return; }
        if (rs == 4 && mid != NULL) {
            openmmo_view_scale2x(src, w, h, mid);
            openmmo_view_scale2x(mid, 2 * w, 2 * h, dst);
            return;
        }
    }
    openmmo_view_replicate(src, w, h, dst, rs);
}
