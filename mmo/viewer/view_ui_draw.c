/* See view_ui_draw.h. The font cache and the primitives
 * are the host panel's, moved out when the panel stopped being the only
 * host UI. */

#include "view_ui_draw.h"
#include "view_ui_skin.h"

/*
 * The window's own face, frozen into the binary from res/fonts by mmo/tools/bin2c.py. FreeType
 * keeps the pointer for the life of the face, which is exactly why this is a static array and
 * not something read into a buffer somebody has to remember to keep.
 */
extern const unsigned char view_ui_noto[];
extern const size_t view_ui_noto_len;

#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdio.h>
#include <string.h>

#include "platform.h"

void view_ui_gpu_init(struct view_ui_gpu *g)
{
    memset(g, 0, sizeof *g);
}

static void glyph_free(struct view_ui_gpu *g)
{
    int i;

    for (i = 0; i < VIEW_UI_GLYPHS; i++) {
        if (g->gl[i].tex != NULL)
            SDL_DestroyTexture(g->gl[i].tex);
    }
    memset(g->gl, 0, sizeof g->gl);
}

/*
 * The slot a code point lives in, claiming a free one on the way. ASCII is its own index;
 * everything else is open-addressed on the code point, which needs no ordering and no
 * allocation.
 */
static struct view_ui_glyph *glyph_slot(struct view_ui_gpu *g, uint32_t cp)
{
    unsigned start, i;

    if (cp >= 32u && cp < 128u)
        return &g->gl[cp - 32u];
    start = (unsigned)(cp % (uint32_t)VIEW_UI_GLYPH_EXTRA);
    for (i = 0; i < (unsigned)VIEW_UI_GLYPH_EXTRA; i++) {
        struct view_ui_glyph *sl =
            &g->gl[VIEW_UI_GLYPH_ASCII + (start + i) % VIEW_UI_GLYPH_EXTRA];

        if (sl->cp == cp)
            return sl;
        if (sl->cp == 0) {
            sl->cp = cp;
            return sl;
        }
    }
    return NULL;
}

/* Fill a slot's metrics, and its texture when a renderer is at hand. Called
 * from the measure with `ren` NULL: FreeType alone answers a width, and the
 * texture is made on the first draw instead. */
static void glyph_load(struct view_ui_gpu *g, SDL_Renderer *ren,
                       struct view_ui_glyph *sl)
{
    FT_Face face = (FT_Face)g->ft_face;
    FT_GlyphSlot got;
    SDL_Surface *surf;
    unsigned char *src, *dst;
    int x, y, pitch;

    if (sl == NULL || face == NULL)
        return;
    if (sl->metrics && (sl->drawn || ren == NULL))
        return;
    if (FT_Load_Char(face, (FT_ULong)sl->cp,
                     ren != NULL ? FT_LOAD_RENDER : FT_LOAD_DEFAULT) != 0) {
        /* A face without this glyph still has to answer a width, or the
         * measure and the draw disagree about where the next character is. */
        sl->metrics = 1;
        sl->drawn = 1;
        return;
    }
    got = face->glyph;
    sl->left = (short)got->bitmap_left;
    sl->top = (short)got->bitmap_top;
    sl->adv = (short)(got->advance.x >> 6);
    sl->metrics = 1;
    if (ren == NULL)
        return;
    sl->drawn = 1;
    sl->w = (short)got->bitmap.width;
    sl->h = (short)got->bitmap.rows;
    if (sl->w < 1 || sl->h < 1)
        return;
    surf = SDL_CreateRGBSurfaceWithFormat(0, sl->w, sl->h, 32,
                                          SDL_PIXELFORMAT_ARGB8888);
    if (surf == NULL)
        return;
    SDL_LockSurface(surf);
    src = got->bitmap.buffer;
    dst = (unsigned char *)surf->pixels;
    pitch = surf->pitch;
    for (y = 0; y < sl->h; y++) {
        for (x = 0; x < sl->w; x++) {
            unsigned char a = src[y * (int)got->bitmap.pitch + x];
            unsigned char *p = dst + y * pitch + x * 4;

            p[0] = 255;
            p[1] = 255;
            p[2] = 255;
            p[3] = a;
        }
    }
    SDL_UnlockSurface(surf);
    sl->tex = SDL_CreateTextureFromSurface(ren, surf);
    SDL_FreeSurface(surf);
    if (sl->tex != NULL)
        SDL_SetTextureBlendMode(sl->tex, SDL_BLENDMODE_BLEND);
}

/* The slot to draw `cp` with, loaded. A code point this face has no room for
 * falls back to '?' rather than to nothing, so the text still lines up. */
static struct view_ui_glyph *glyph_get(struct view_ui_gpu *g,
                                       SDL_Renderer *ren, uint32_t cp)
{
    struct view_ui_glyph *sl;

    if (cp < 32u || cp == 127u)
        cp = '?';
    sl = glyph_slot(g, cp);
    if (sl == NULL)
        sl = glyph_slot(g, '?');
    glyph_load(g, ren, sl);
    return sl;
}

void view_ui_gpu_free(struct view_ui_gpu *g)
{
    int i;

    if (g == NULL)
        return;
    glyph_free(g);
    for (i = 0; i < VIEW_UI_SHEET_N; i++)
        if (g->sheet[i] != NULL)
            SDL_DestroyTexture(g->sheet[i]);
    if (g->ft_face != NULL)
        FT_Done_Face((FT_Face)g->ft_face);
    if (g->ft_lib != NULL)
        FT_Done_FreeType((FT_Library)g->ft_lib);
    memset(g, 0, sizeof *g);
}

int view_ui_font_ready(struct view_ui_gpu *g, SDL_Renderer *ren, int px)
{
    FT_Library lib;
    FT_Face face;
    int i;

    if (px < 8)
        px = 8;
    if (g->ready && g->px == px)
        return 1;
    glyph_free(g);
    if (g->ft_lib == NULL) {
        char path[512];

        if (FT_Init_FreeType(&lib) != 0)
            return 0;
        g->ft_lib = lib;
        /* The face, in the order a window should want them. */
        face = NULL;
        if (g->theme_font[0] != '\0' &&
            FT_New_Face(lib, g->theme_font, 0, &face) != 0)
            face = NULL;
        if (face == NULL &&
            FT_New_Memory_Face(lib, view_ui_noto, (FT_Long)view_ui_noto_len,
                               0, &face) != 0)
            face = NULL;
        if (face == NULL) {
            if (mmo_plat_ui_font(0, path, sizeof path) != 0)
                return 0;
            if (FT_New_Face(lib, path, 0, &face) != 0)
                return 0;
        }
        g->ft_face = face;
    }
    face = (FT_Face)g->ft_face;
    if (FT_Set_Pixel_Sizes(face, 0, (unsigned)px) != 0)
        return 0;
    g->ascent = (int)(face->size->metrics.ascender >> 6);
    /* ASCII up front, because every window draws all of it and the slots are
     * already reserved. Anything above it waits until the window meets it. */
    for (i = 0; i < VIEW_UI_GLYPH_ASCII; i++) {
        g->gl[i].cp = (uint32_t)(i + 32);
        glyph_load(g, ren, &g->gl[i]);
    }
    g->px = px;
    g->ready = 1;
    return 1;
}

static unsigned fade_a = 255;

void view_ui_alpha(unsigned a)
{
    fade_a = a > 255u ? 255u : a;
}

static unsigned fade(unsigned a)
{
    return a * fade_a / 255u;
}

SDL_Color view_ui_rgb(unsigned r, unsigned g, unsigned b)
{
    SDL_Color c;

    c.r = (Uint8)r;
    c.g = (Uint8)g;
    c.b = (Uint8)b;
    c.a = 255;
    return c;
}

SDL_Color view_ui_col(uint32_t rgb)
{
    return view_ui_rgb((rgb >> 16) & 255u, (rgb >> 8) & 255u, rgb & 255u);
}

/* ------------------------------------------------------------------ */
/* The theme                                                           */
/* ------------------------------------------------------------------ */

int view_ui_theme_up(struct view_ui_gpu *g, SDL_Renderer *ren,
                     const char *dir)
{
    struct view_ui_theme t;
    int i;

    if (g == NULL || ren == NULL)
        return 0;
    /*
     * The official client's art where the player pointed at one, ours otherwise, and ours is not a fallback
     * the way the flat primitives were: view_ui_skin_make fills the same four sheets at the
     * same sizes, so nothing below this line can tell the two apart.
     */
    if (!view_ui_theme_read(&t, dir) && !view_ui_skin_make(&t))
        return 0;
    for (i = 0; i < VIEW_UI_SHEET_N; i++) {
        SDL_Texture *tex =
            SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888,
                              SDL_TEXTUREACCESS_STATIC, t.sheet[i].w,
                              t.sheet[i].h);

        if (tex == NULL ||
            SDL_UpdateTexture(tex, NULL, t.sheet[i].rgba,
                              t.sheet[i].w * 4) != 0) {
            fprintf(stderr, "openmmo-view: theme: %s: %s\n",
                    view_ui_sheet_file(i), SDL_GetError());
            if (tex != NULL)
                SDL_DestroyTexture(tex);
            view_ui_theme_free(&t);
            /* Half a theme is worse than none: free what was uploaded. */
            for (i--; i >= 0; i--) {
                SDL_DestroyTexture(g->sheet[i]);
                g->sheet[i] = NULL;
            }
            return 0;
        }
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        g->sheet[i] = tex;
    }
    view_ui_theme_free(&t);
    if (view_ui_theme_font(dir, g->theme_font, sizeof g->theme_font)) {
        /* Reload the glyph cache onto the theme's face, even if a frame
         * already drew with DejaVu. */
        if (g->ft_face != NULL) {
            FT_Done_Face((FT_Face)g->ft_face);
            g->ft_face = NULL;
        }
        if (g->ft_lib != NULL) {
            FT_Done_FreeType((FT_Library)g->ft_lib);
            g->ft_lib = NULL;
        }
        g->ready = 0;
    }
    g->themed = 1;
    return 1;
}

int view_ui_th_mod(SDL_Renderer *ren, struct view_ui_gpu *g, int id,
                   const struct openmmo_rect *r, uint32_t rgb, unsigned a)
{
    struct view_ui_th_pair cells[VIEW_UI_TH_CELLS];
    SDL_Texture *tex;
    uint32_t tint;
    unsigned tr, tg, tb, ta;
    int i, n, sheet;

    if (g == NULL || !g->themed)
        return 0;
    sheet = view_ui_theme_sheet(id);
    if (sheet < 0 || g->sheet[sheet] == NULL)
        return 0;
    n = view_ui_theme_cells(id, r, g->px, cells);
    if (n < 1)
        return 1;   /* themed, nothing to put behind a degenerate rect */
    tex = g->sheet[sheet];
    tint = view_ui_theme_tint(id);
    tr = ((tint >> 16) & 255u) * ((rgb >> 16) & 255u) / 255u;
    tg = ((tint >> 8) & 255u) * ((rgb >> 8) & 255u) / 255u;
    tb = (tint & 255u) * (rgb & 255u) / 255u;
    ta = fade((tint >> 24) & 255u) * (a > 255u ? 255u : a) / 255u;
    SDL_SetTextureColorMod(tex, (Uint8)tr, (Uint8)tg, (Uint8)tb);
    SDL_SetTextureAlphaMod(tex, (Uint8)ta);
    for (i = 0; i < n; i++) {
        SDL_Rect src, dst;

        src.x = cells[i].src.x;
        src.y = cells[i].src.y;
        src.w = cells[i].src.w;
        src.h = cells[i].src.h;
        dst.x = cells[i].dst.x;
        dst.y = cells[i].dst.y;
        dst.w = cells[i].dst.w;
        dst.h = cells[i].dst.h;
        SDL_RenderCopy(ren, tex, &src, &dst);
    }
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
    return 1;
}

int view_ui_th(SDL_Renderer *ren, struct view_ui_gpu *g, int id,
               const struct openmmo_rect *r)
{
    return view_ui_th_mod(ren, g, id, r, 0xFFFFFFu, 255u);
}

void view_ui_fill(SDL_Renderer *ren, int x, int y, int w, int h,
                  unsigned r, unsigned g, unsigned b, unsigned a)
{
    SDL_Rect rc;

    if (w < 1 || h < 1)
        return;
    rc.x = x;
    rc.y = y;
    rc.w = w;
    rc.h = h;
    SDL_SetRenderDrawColor(ren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)fade(a));
    SDL_RenderFillRect(ren, &rc);
}

void view_ui_border(SDL_Renderer *ren, int x, int y, int w, int h,
                    unsigned r, unsigned g, unsigned b)
{
    SDL_Rect rc;

    rc.x = x;
    rc.y = y;
    rc.w = w;
    rc.h = h;
    SDL_SetRenderDrawColor(ren, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)fade(255));
    SDL_RenderDrawRect(ren, &rc);
}

int view_ui_text_width(struct view_ui_gpu *g, const char *s)
{
    int w = 0;

    if (g == NULL || s == NULL)
        return 0;
    while (*s != '\0') {
        struct view_ui_glyph *sl;
        uint32_t cp;
        unsigned step = view_ui_utf8(s, &cp);

        if (step == 0)
            break;
        s += step;
        sl = glyph_get(g, NULL, cp);
        if (sl != NULL)
            w += sl->adv;
    }
    return w;
}

void view_ui_text(SDL_Renderer *ren, struct view_ui_gpu *g,
                  int x, int y, const char *s, SDL_Color col)
{
    if (s == NULL || !g->ready)
        return;
    while (*s != '\0') {
        struct view_ui_glyph *sl;
        uint32_t cp;
        unsigned step = view_ui_utf8(s, &cp);
        SDL_Rect dst;

        if (step == 0)
            break;
        s += step;
        sl = glyph_get(g, ren, cp);
        if (sl == NULL)
            continue;
        if (sl->tex != NULL) {
            dst.x = x + sl->left;
            dst.y = y + g->ascent - sl->top;
            dst.w = sl->w;
            dst.h = sl->h;
            SDL_SetTextureColorMod(sl->tex, col.r, col.g, col.b);
            SDL_SetTextureAlphaMod(sl->tex,
                                   (Uint8)fade(col.a ? col.a : 255));
            SDL_RenderCopy(ren, sl->tex, NULL, &dst);
        }
        x += sl->adv;
    }
}

void view_ui_panel(SDL_Renderer *ren, const struct openmmo_rect *r,
                   uint32_t ground, uint32_t line)
{
    SDL_Color g = view_ui_col(ground), l = view_ui_col(line);

    if (r == NULL || r->w < 1 || r->h < 1)
        return;
    view_ui_fill(ren, r->x, r->y, r->w, r->h, g.r, g.g, g.b, 255);
    view_ui_border(ren, r->x, r->y, r->w, r->h, l.r, l.g, l.b);
}

void view_ui_clip_push(SDL_Renderer *ren, const struct openmmo_rect *r,
                       SDL_Rect *saved)
{
    SDL_Rect c;

    if (saved != NULL)
        SDL_RenderGetClipRect(ren, saved);
    if (r == NULL)
        return;
    c.x = r->x;
    c.y = r->y;
    c.w = r->w > 0 ? r->w : 1;
    c.h = r->h > 0 ? r->h : 1;
    SDL_RenderSetClipRect(ren, &c);
}

void view_ui_clip_pop(SDL_Renderer *ren, const SDL_Rect *saved)
{
    if (saved != NULL && saved->w > 0 && saved->h > 0)
        SDL_RenderSetClipRect(ren, saved);
    else
        SDL_RenderSetClipRect(ren, NULL);
}

void view_ui_text_in(SDL_Renderer *ren, struct view_ui_gpu *g,
                     const struct openmmo_rect *r, int pad, int align,
                     const char *s, uint32_t rgb)
{
    int x, y;

    if (r == NULL || s == NULL || s[0] == '\0' || !g->ready)
        return;
    y = r->y + (r->h - g->px) / 2;
    if (align)
        x = r->x + r->w - pad - view_ui_text_width(g, s);
    else
        x = r->x + pad;
    view_ui_text(ren, g, x, y, s, view_ui_col(rgb));
}

int view_ui_measure_gpu(void *ctx, const char *s, int len)
{
    struct view_ui_gpu *g = ctx;
    int i = 0, w = 0;

    if (g == NULL || s == NULL)
        return 0;
    while (i < len && s[i] != '\0') {
        struct view_ui_glyph *sl;
        uint32_t cp;
        unsigned step = view_ui_utf8(s + i, &cp);

        /* A prefix that ends inside a sequence is what the wrap is asking
         * about; the partial character is not measured, because it is not
         * going to be drawn either. */
        if (step == 0 || i + (int)step > len)
            break;
        i += (int)step;
        sl = glyph_get(g, NULL, cp);
        if (sl != NULL)
            w += sl->adv;
    }
    return w;
}

/* ------------------------------------------------------------------ */
/* The layer                                                           */
/* ------------------------------------------------------------------ */

void view_ui_layer_init(struct view_ui_layer *ui)
{
    memset(ui, 0, sizeof *ui);
}

int view_ui_layer_add(struct view_ui_layer *ui,
                      const struct view_ui_element *el)
{
    if (ui == NULL || el == NULL)
        return 0;
    if (ui->n >= VIEW_UI_ELEMENTS) {
        fprintf(stderr, "openmmo-view: ui layer full; '%s' not added\n",
                el->name != NULL ? el->name : "?");
        return 0;
    }
    ui->el[ui->n++] = *el;
    return 1;
}

void view_ui_layer_draw(struct view_ui_layer *ui, SDL_Renderer *ren,
                        struct view_ui_gpu *g, const struct view_ui_frame *f)
{
    int i;

    if (ui == NULL || ren == NULL || f == NULL)
        return;
    for (i = 0; i < ui->n; i++) {
        if (ui->el[i].draw != NULL)
            ui->el[i].draw(ui->el[i].state, ren, g, f);
    }
    view_ui_alpha(255);
}

static const char *const canvas_name[VIEW_UI_CANVAS_MODES] = {
    "both screens", "the top screen", "the bottom screen"
};

int view_ui_layer_event(struct view_ui_layer *ui, const SDL_Event *ev,
                        const struct view_ui_frame *f)
{
    int i;

    if (ui == NULL || ev == NULL || f == NULL)
        return 0;
    /* F10 walks the layer across the screens: both, the top one, the
     * bottom one, and back. The layer's own chord, ahead of the elements,
     * so it works whichever element the pointer happens to be on. */
    if (ev->type == SDL_KEYDOWN && !ev->key.repeat &&
        ev->key.keysym.sym == SDLK_F10) {
        ui->canvas_mode = (ui->canvas_mode + 1) % VIEW_UI_CANVAS_MODES;
        fprintf(stderr, "openmmo-view: ui layer on %s\n",
                canvas_name[ui->canvas_mode]);
        return 1;
    }
    for (i = ui->n - 1; i >= 0; i--) {
        if (ui->el[i].event != NULL && ui->el[i].event(ui->el[i].state, ev, f))
            return 1;
    }
    return 0;
}

int view_ui_layer_owns_pointer(struct view_ui_layer *ui,
                               const struct view_ui_frame *f, int mx, int my)
{
    int i;

    if (ui == NULL || f == NULL)
        return 0;
    for (i = ui->n - 1; i >= 0; i--) {
        if (ui->el[i].owns_pointer != NULL &&
            ui->el[i].owns_pointer(ui->el[i].state, f, mx, my))
            return 1;
    }
    return 0;
}
