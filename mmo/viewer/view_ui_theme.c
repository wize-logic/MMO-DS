/* See view_ui_theme.h. The region table is the official client's default
 * theme (gfx.xml / gfx_ui.xml); the PNG reader is deflate.h under a filter
 * pass. SDL-free; tests/view_ui_test.c holds the table and the reader. */

#include "view_ui_theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "deflate.h"
#include "view_ui_game.h"

/* ------------------------------------------------------------------ */
/* The sheets                                                          */
/* ------------------------------------------------------------------ */

struct sheet_def {
    const char *file;
    int min_w, min_h;   /* the size the regions were measured against */
};

static const struct sheet_def sheets[VIEW_UI_SHEET_N] = {
    { "res/pokemmo_ui.png",     471, 421 },
    { "res/main-hud.png",       560, 560 },
    { "res/user-interface.png", 512, 512 },
    { "res/monster-info.png",   202,  96 }
};

const char *view_ui_sheet_file(int sheet)
{
    if (sheet < 0 || sheet >= VIEW_UI_SHEET_N)
        return NULL;
    return sheets[sheet].file;
}

void view_ui_sheet_min(int sheet, int *w, int *h)
{
    int ww = 0, hh = 0;

    if (sheet >= 0 && sheet < VIEW_UI_SHEET_N) {
        ww = sheets[sheet].min_w;
        hh = sheets[sheet].min_h;
    }
    if (w != NULL) *w = ww;
    if (h != NULL) *h = hh;
}

/* ------------------------------------------------------------------ */
/* The regions                                                         */
/* ------------------------------------------------------------------ */

#define TH_COLS 3
#define TH_ROWS 4

/* One surface: a grid of up to 3x4 cells. Column widths and row heights are
 * source texels; the stretching column and row take whatever of the target
 * the fixed ones leave. Cells name their own source origin because the
 * frame-draggable grid's rows are not a cross product of one area. */
struct th_def {
    unsigned char sheet, ncols, nrows;
    unsigned char stretch_col, stretch_row;
    short cw[TH_COLS], rh[TH_ROWS];
    short sx[TH_ROWS][TH_COLS], sy[TH_ROWS][TH_COLS];
    uint32_t tint;   /* 0xAARRGGBB */
};

static const struct th_def defs[VIEW_UI_TH_N] = {
    /* Panel, gfx_ui.xml ui-popup.background, 317,247,42,42 split 5 */
    { VIEW_UI_SHEET_UI, 3, 3, 1, 1, { 5, 32, 5 }, { 5, 32, 5, 0 },
      { { 317, 322, 354 }, { 317, 322, 354 }, { 317, 322, 354 }, { 0 } },
      { { 247, 247, 247 }, { 252, 252, 252 }, { 284, 284, 284 }, { 0 } },
      0xFFFFFFFFu },
    /* BUTTON, ui-button.default, 322,116,29,29 split 4 */
    { VIEW_UI_SHEET_UI, 3, 3, 1, 1, { 4, 21, 4 }, { 4, 21, 4, 0 },
      { { 322, 326, 347 }, { 322, 326, 347 }, { 322, 326, 347 }, { 0 } },
      { { 116, 116, 116 }, { 120, 120, 120 }, { 141, 141, 141 }, { 0 } },
      0xFFFFFFFFu },
    /* BUTTON_HOVER, ui-button.hover.background, 353,116,29,29, tinted
     * main-color (theme.xml #5db1ff) at the hover animation's 0x99. */
    { VIEW_UI_SHEET_UI, 3, 3, 1, 1, { 4, 21, 4 }, { 4, 21, 4, 0 },
      { { 353, 357, 378 }, { 353, 357, 378 }, { 353, 357, 378 }, { 0 } },
      { { 116, 116, 116 }, { 120, 120, 120 }, { 141, 141, 141 }, { 0 } },
      0x995DB1FFu },
    /* BUTTON_DOWN, ui-button.pressed.background, 290,116,29,29 */
    { VIEW_UI_SHEET_UI, 3, 3, 1, 1, { 4, 21, 4 }, { 4, 21, 4, 0 },
      { { 290, 294, 315 }, { 290, 294, 315 }, { 290, 294, 315 }, { 0 } },
      { { 116, 116, 116 }, { 120, 120, 120 }, { 141, 141, 141 }, { 0 } },
      0xFFFFFFFFu },
    /* BUTTON_DIS, ui-button.disabled, 322,147,29,29 */
    { VIEW_UI_SHEET_UI, 3, 3, 1, 1, { 4, 21, 4 }, { 4, 21, 4, 0 },
      { { 322, 326, 347 }, { 322, 326, 347 }, { 322, 326, 347 }, { 0 } },
      { { 147, 147, 147 }, { 151, 151, 151 }, { 172, 172, 172 }, { 0 } },
      0xFFFFFFFFu },
    /* ROW_HOVER, ui-popup-button.selected, 364,254,35,29 split 4 */
    { VIEW_UI_SHEET_UI, 3, 3, 1, 1, { 4, 27, 4 }, { 4, 21, 4, 0 },
      { { 364, 368, 395 }, { 364, 368, 395 }, { 364, 368, 395 }, { 0 } },
      { { 254, 254, 254 }, { 258, 258, 258 }, { 279, 279, 279 }, { 0 } },
      0xFFFFFFFFu },
    /* Input, ui-inputbox.default, 326,8,16,16 split 3 */
    { VIEW_UI_SHEET_UI, 3, 3, 1, 1, { 3, 10, 3 }, { 3, 10, 3, 0 },
      { { 326, 329, 339 }, { 326, 329, 339 }, { 326, 329, 339 }, { 0 } },
      { { 8, 8, 8 }, { 11, 11, 11 }, { 21, 21, 21 }, { 0 } },
      0xFFFFFFFFu },
    /* FRAME, gfx.xml frame-draggable.background: the corner row, the title
     * band, the stretching body, the floor. */
    { VIEW_UI_SHEET_HUD, 3, 4, 1, 2, { 7, 8, 8 }, { 7, 19, 8, 6 },
      { { 99, 108, 185 }, { 99, 108, 185 }, { 99, 111, 185 },
        { 99, 104, 185 } },
      { { 179, 179, 179 }, { 184, 184, 184 }, { 210, 213, 208 },
        { 223, 223, 223 } },
      0xFFFFFFFFu },
    /* Close, gfx.xml close-handle, 0,500,12,12 */
    { VIEW_UI_SHEET_WIN, 1, 1, 0, 0, { 12, 0, 0 }, { 12, 0, 0, 0 },
      { { 0 }, { 0 }, { 0 }, { 0 } },
      { { 500 }, { 0 }, { 0 }, { 0 } },
      0xFFFFFFFFu },
    /* HPBAR, gfx.xml mi-hpbar.background: two one-texel caps around a
     * one-texel middle, full height. */
    { VIEW_UI_SHEET_MON, 3, 1, 1, 0, { 1, 1, 1 }, { 8, 0, 0, 0 },
      { { 7, 90, 201 }, { 0 }, { 0 }, { 0 } },
      { { 88, 88, 88 }, { 0 }, { 0 }, { 0 } },
      0xFFFFFFFFu },
    /* TAB, gfx_ui.xml ui-tab.inactive, 72,216,40,22 split L12/R12, at its
     * resting tint. */
    { VIEW_UI_SHEET_UI, 3, 1, 1, 0, { 12, 16, 12 }, { 22, 0, 0, 0 },
      { { 72, 84, 100 }, { 0 }, { 0 }, { 0 } },
      { { 216, 216, 216 }, { 0 }, { 0 }, { 0 } },
      0xFFC5C5C5u },
    /* TAB_ACTIVE, ui-tab.active, 134,216,40,26 split L12/R12. */
    { VIEW_UI_SHEET_UI, 3, 1, 1, 0, { 12, 16, 12 }, { 26, 0, 0, 0 },
      { { 134, 146, 162 }, { 0 }, { 0 }, { 0 } },
      { { 216, 216, 216 }, { 0 }, { 0 }, { 0 } },
      0xFFFFFFFFu },
    /* Header, ui-misc-btn.background, 326,292,23,20 split 5: what
     * ui-header.background composes a column header from. */
    { VIEW_UI_SHEET_UI, 3, 3, 1, 1, { 5, 13, 5 }, { 5, 10, 5, 0 },
      { { 326, 331, 344 }, { 326, 331, 344 }, { 326, 331, 344 }, { 0 } },
      { { 292, 292, 292 }, { 297, 297, 297 }, { 307, 307, 307 }, { 0 } },
      0xFFFFFFFFu },
    /* ROW, ui-table-row.background: ui-inputbox.default (326,8,16,16
     * split 3), which the theme insets by one at draw time. */
    { VIEW_UI_SHEET_UI, 3, 3, 1, 1, { 3, 10, 3 }, { 3, 10, 3, 0 },
      { { 326, 329, 339 }, { 326, 329, 339 }, { 326, 329, 339 }, { 0 } },
      { { 8, 8, 8 }, { 11, 11, 11 }, { 21, 21, 21 }, { 0 } },
      0xFFFFFFFFu },
    /* Warn, ui-popup-warning.background, 405,245,41,41 split 10. */
    { VIEW_UI_SHEET_UI, 3, 3, 1, 1, { 10, 21, 10 }, { 10, 21, 10, 0 },
      { { 405, 415, 436 }, { 405, 415, 436 }, { 405, 415, 436 }, { 0 } },
      { { 245, 245, 245 }, { 255, 255, 255 }, { 276, 276, 276 }, { 0 } },
      0xFFFFFFFFu }
};

int view_ui_theme_sheet(int id)
{
    if (id < 0 || id >= VIEW_UI_TH_N)
        return -1;
    return defs[id].sheet;
}

uint32_t view_ui_theme_tint(int id)
{
    if (id < 0 || id >= VIEW_UI_TH_N)
        return 0xFFFFFFFFu;
    return defs[id].tint;
}

/* Split `span` target pixels across the axis: fixed cells scale with the
 * type size, the stretch cell takes the rest, and a target too small for
 * the fixed ones alone shrinks them proportionally instead of overflowing
 * it. `at[i]`/`sz[i]` come back in target pixels. */
static void axis(const short *src, int n, int stretch, int span, int text_px,
                 int *at, int *sz)
{
    int i, fixed = 0, x = 0;

    for (i = 0; i < n; i++) {
        sz[i] = view_ui_scale(src[i], text_px);
        if (sz[i] < 1 && src[i] > 0)
            sz[i] = 1;
        if (i != stretch)
            fixed += sz[i];
    }
    if (n == 1) {
        sz[0] = span;
        fixed = 0;
    } else if (fixed > span) {
        int left = span;

        for (i = 0; i < n; i++) {
            if (i == stretch) {
                sz[i] = 0;
                continue;
            }
            sz[i] = fixed > 0 ? sz[i] * span / fixed : 0;
            left -= sz[i];
        }
        /* The rounding slack goes to the last fixed cell so the row still
         * covers the span exactly. */
        for (i = n - 1; i >= 0; i--)
            if (i != stretch) {
                sz[i] += left;
                if (sz[i] < 0)
                    sz[i] = 0;
                break;
            }
    } else {
        sz[stretch] = span - fixed;
    }
    for (i = 0; i < n; i++) {
        at[i] = x;
        x += sz[i];
    }
}

int view_ui_theme_cells(int id, const struct openmmo_rect *dst, int text_px,
                        struct view_ui_th_pair out[VIEW_UI_TH_CELLS])
{
    const struct th_def *d;
    int cx[TH_COLS], cw[TH_COLS], ry[TH_ROWS], rh[TH_ROWS];
    int r, c, n = 0;

    if (id < 0 || id >= VIEW_UI_TH_N || dst == NULL || out == NULL ||
        dst->w < 1 || dst->h < 1)
        return 0;
    d = &defs[id];
    axis(d->cw, d->ncols, d->stretch_col, dst->w, text_px, cx, cw);
    axis(d->rh, d->nrows, d->stretch_row, dst->h, text_px, ry, rh);
    for (r = 0; r < d->nrows; r++) {
        for (c = 0; c < d->ncols; c++) {
            struct view_ui_th_pair *p = &out[n];

            if (cw[c] < 1 || rh[r] < 1)
                continue;
            p->src.x = d->sx[r][c];
            p->src.y = d->sy[r][c];
            /* frame-draggable's bottom row keeps a narrower middle texel;
             * every other grid's cell is as wide as its column. */
            p->src.w = d->cw[c];
            p->src.h = d->rh[r];
            if (id == VIEW_UI_TH_FRAME && r == 3 && c == 1)
                p->src.w = 3;
            p->dst.x = dst->x + cx[c];
            p->dst.y = dst->y + ry[r];
            p->dst.w = cw[c];
            p->dst.h = rh[r];
            n++;
        }
    }
    return n;
}

/*
 * The same grid read the other way: where each cell sits on the sheet, and how big the surface
 * is at 1:1.
 */
int view_ui_theme_source(int id, struct view_ui_th_pair out[VIEW_UI_TH_CELLS],
                         int *w, int *h)
{
    const struct th_def *d;
    int r, c, n = 0, x, y = 0, tw = 0, th = 0;

    if (id < 0 || id >= VIEW_UI_TH_N || out == NULL)
        return 0;
    d = &defs[id];
    for (c = 0; c < d->ncols; c++)
        tw += d->cw[c];
    for (r = 0; r < d->nrows; r++)
        th += d->rh[r];
    for (r = 0; r < d->nrows; r++) {
        x = 0;
        for (c = 0; c < d->ncols; c++) {
            struct view_ui_th_pair *p = &out[n];
            int cw = d->cw[c];

            if (id == VIEW_UI_TH_FRAME && r == 3 && c == 1)
                cw = 3;
            p->src.x = d->sx[r][c];
            p->src.y = d->sy[r][c];
            p->src.w = cw;
            p->src.h = d->rh[r];
            p->dst.x = x;
            p->dst.y = y;
            p->dst.w = cw;
            p->dst.h = d->rh[r];
            x += d->cw[c];
            n++;
        }
        y += d->rh[r];
    }
    if (w != NULL) *w = tw;
    if (h != NULL) *h = th;
    return n;
}

/* ------------------------------------------------------------------ */
/* PNG                                                                 */
/* ------------------------------------------------------------------ */

static uint32_t be32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 |
           (uint32_t)p[3];
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;

    if (pa <= pb && pa <= pc)
        return a;
    return pb <= pc ? b : c;
}

unsigned char *view_ui_png(const unsigned char *buf, size_t len,
                           int *w, int *h)
{
    static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    unsigned char *idat = NULL, *raw = NULL, *px = NULL;
    size_t at, idat_len = 0, idat_cap = 0, raw_len, got;
    uint32_t iw = 0, ih = 0;
    int bpp = 0, y;
    /* A palette image's table, expanded on the way out: the launcher's
     * wallpaper is one (825 KB against 2 MB as truecolour), and the Android
     * door reads it through here rather than through raylib. */
    unsigned char pal[256][4];
    int npal = 0, paletted = 0;

    if (buf == NULL || len < 8 + 25 || memcmp(buf, sig, 8) != 0)
        return NULL;
    for (at = 8; at + 12 <= len;) {
        uint32_t clen = be32(buf + at);
        const unsigned char *type = buf + at + 4;
        const unsigned char *data = buf + at + 8;

        if (clen > len || at + 12 + clen > len)
            goto fail;
        if (memcmp(type, "IHDR", 4) == 0) {
            if (clen < 13)
                goto fail;
            iw = be32(data);
            ih = be32(data + 4);
            /* 8-bit truecolour or 8-bit palette, no interlace: what every
             * sheet the default theme ships is, plus the wallpaper. Anything
             * else is refused, not half-read. */
            if (iw < 1 || ih < 1 || iw > 8192 || ih > 8192 || data[8] != 8 ||
                (data[9] != 2 && data[9] != 6 && data[9] != 3) ||
                data[12] != 0)
                goto fail;
            paletted = data[9] == 3;
            bpp = data[9] == 6 ? 4 : data[9] == 2 ? 3 : 1;
        } else if (memcmp(type, "PLTE", 4) == 0) {
            int i;

            if (clen % 3 != 0 || clen > 256 * 3)
                goto fail;
            npal = (int)(clen / 3);
            for (i = 0; i < npal; i++) {
                pal[i][0] = data[i * 3];
                pal[i][1] = data[i * 3 + 1];
                pal[i][2] = data[i * 3 + 2];
                pal[i][3] = 255;
            }
        } else if (memcmp(type, "tRNS", 4) == 0 && paletted) {
            int i;

            if ((int)clen > npal)
                goto fail;
            for (i = 0; i < (int)clen; i++)
                pal[i][3] = data[i];
        } else if (memcmp(type, "IDAT", 4) == 0) {
            if (idat_len + clen > idat_cap) {
                unsigned char *grow;

                idat_cap = (idat_len + clen) * 2;
                grow = realloc(idat, idat_cap);
                if (grow == NULL)
                    goto fail;
                idat = grow;
            }
            memcpy(idat + idat_len, data, clen);
            idat_len += clen;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        }
        at += 12 + clen;
    }
    if (bpp == 0 || idat_len == 0 || (paletted && npal == 0))
        goto fail;

    raw_len = (size_t)ih * ((size_t)iw * (size_t)bpp + 1);
    raw = malloc(raw_len);
    if (raw == NULL)
        goto fail;
    got = mmo_inflate_zlib(idat, idat_len, raw, raw_len);
    if (got != raw_len)
        goto fail;
    free(idat);
    idat = NULL;

    px = malloc((size_t)iw * (size_t)ih * 4);
    if (px == NULL)
        goto fail;
    for (y = 0; y < (int)ih; y++) {
        unsigned char *row = raw + (size_t)y * (iw * bpp + 1);
        unsigned char *prev = y > 0 ? row - (iw * bpp + 1) + 1 : NULL;
        unsigned char filt = row[0];
        unsigned char *cur = row + 1;
        int i;

        for (i = 0; i < (int)iw * bpp; i++) {
            int a = i >= bpp ? cur[i - bpp] : 0;
            int b = prev != NULL ? prev[i] : 0;
            int c = (prev != NULL && i >= bpp) ? prev[i - bpp] : 0;

            switch (filt) {
            case 1: cur[i] = (unsigned char)(cur[i] + a); break;
            case 2: cur[i] = (unsigned char)(cur[i] + b); break;
            case 3: cur[i] = (unsigned char)(cur[i] + (a + b) / 2); break;
            case 4: cur[i] = (unsigned char)(cur[i] + paeth(a, b, c)); break;
            case 0: break;
            default: goto fail;
            }
        }
        for (i = 0; i < (int)iw; i++) {
            unsigned char *o = px + ((size_t)y * iw + i) * 4;

            if (paletted) {
                if (cur[i] >= npal)
                    goto fail;
                memcpy(o, pal[cur[i]], 4);
                continue;
            }
            o[0] = cur[i * bpp];
            o[1] = cur[i * bpp + 1];
            o[2] = cur[i * bpp + 2];
            o[3] = bpp == 4 ? cur[i * bpp + 3] : 255;
        }
    }
    free(raw);
    if (w != NULL) *w = (int)iw;
    if (h != NULL) *h = (int)ih;
    return px;

fail:
    free(idat);
    free(raw);
    free(px);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* The theme on disk                                                   */
/* ------------------------------------------------------------------ */

static unsigned char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long n;

    if (f == NULL)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || n > 32 * 1024 * 1024 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = malloc((size_t)n > 0 ? (size_t)n : 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

void view_ui_theme_free(struct view_ui_theme *t)
{
    int i;

    if (t == NULL)
        return;
    for (i = 0; i < VIEW_UI_SHEET_N; i++)
        free(t->sheet[i].rgba);
    memset(t, 0, sizeof *t);
}

int view_ui_theme_read(struct view_ui_theme *t, const char *dir)
{
    int i;

    if (t == NULL)
        return 0;
    memset(t, 0, sizeof *t);
    if (dir == NULL || dir[0] == '\0')
        return 0;
    for (i = 0; i < VIEW_UI_SHEET_N; i++) {
        char path[1024];
        unsigned char *buf, *px;
        size_t len = 0;
        int w = 0, h = 0;

        snprintf(path, sizeof path, "%s/%s", dir, sheets[i].file);
        buf = slurp(path, &len);
        /* Nothing at that PATH is not a fault any more, and it is not worth a line. */
        if (buf == NULL && i == 0)
            return 0;
        px = buf != NULL ? view_ui_png(buf, len, &w, &h) : NULL;
        free(buf);
        if (px == NULL || w < sheets[i].min_w || h < sheets[i].min_h) {
            fprintf(stderr,
                    "openmmo-view: theme: %s %s; drawing the window's own art\n",
                    path,
                    px == NULL ? "missing or not an 8-bit PNG"
                               : "is not the layout the table was measured on");
            free(px);
            view_ui_theme_free(t);
            return 0;
        }
        t->sheet[i].rgba = px;
        t->sheet[i].w = w;
        t->sheet[i].h = h;
    }
    t->ready = 1;
    return 1;
}

int view_ui_theme_font(const char *dir, char *out, size_t cap)
{
    char path[1024];
    FILE *f;

    if (dir == NULL || dir[0] == '\0' || out == NULL || cap == 0)
        return 0;
    snprintf(path, sizeof path, "%s/res/fonts/NotoSansCJK-Medium.ttc", dir);
    f = fopen(path, "rb");
    if (f == NULL)
        return 0;
    fclose(f);
    snprintf(out, cap, "%s", path);
    return 1;
}
