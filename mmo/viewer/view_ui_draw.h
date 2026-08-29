/*
 * The SDL half of the UI layer: the type, the primitives and the element
 * registry.
 */

#ifndef OPENMMO_VIEW_UI_DRAW_H
#define OPENMMO_VIEW_UI_DRAW_H

#include <SDL.h>

#include "view_ui.h"
#include "view_ui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One cached glyph: its metrics, and the texture the draw tints. The texture
 * is made the first time the glyph is actually drawn, because measuring
 * happens without a renderer in hand (view_ui_measure_gpu) and a width is all
 * FreeType is needed for. */
struct view_ui_glyph {
    SDL_Texture *tex;
    short        w, h, left, top, adv;
    uint32_t     cp;      /* the code point this slot holds */
    unsigned char metrics; /* FreeType has been asked for its size */
    unsigned char drawn;   /* the texture has been attempted */
};

/* ASCII 32..127, one slot each and always in the same place, so the common
 * case is an index rather than a search. */
#define VIEW_UI_GLYPH_ASCII 96

/* Everything above ASCII, keyed by code point. */
#define VIEW_UI_GLYPH_EXTRA 256

#define VIEW_UI_GLYPHS (VIEW_UI_GLYPH_ASCII + VIEW_UI_GLYPH_EXTRA)

/*
 * The window's one glyph cache: one face through FreeType at one pixel size, keyed by Unicode
 * code point, each glyph an SDL texture tinted at draw time. The face is the theme's own
 * (official renders with it) when a theme loaded, DejaVu otherwise.
 */
struct view_ui_gpu {
    void        *ft_lib;
    void        *ft_face;
    struct view_ui_glyph gl[VIEW_UI_GLYPHS];
    int          px;
    int          ascent;
    int          ready;
    SDL_Texture *sheet[VIEW_UI_SHEET_N];
    int          themed;
    char         theme_font[1024];
};

/* Load the theme under `dir` into textures (and note its face for the glyph
 * cache). Nonzero when the window is themed; a miss says why on stderr and
 * leaves every element on its primitive look. */
int view_ui_theme_up(struct view_ui_gpu *g, SDL_Renderer *ren,
                     const char *dir);

/* Blit surface `id` (VIEW_UI_TH_*) over `r` with the theme's own tint and
 * the layer alpha. Returns 0, draw the primitive look instead, when no
 * theme is up. `view_ui_th_mod` multiplies one more colour in (the fainted
 * slot's purple wash). */
int view_ui_th(SDL_Renderer *ren, struct view_ui_gpu *g, int id,
               const struct openmmo_rect *r);
int view_ui_th_mod(SDL_Renderer *ren, struct view_ui_gpu *g, int id,
                   const struct openmmo_rect *r, uint32_t rgb, unsigned a);

void view_ui_gpu_init(struct view_ui_gpu *g);
void view_ui_gpu_free(struct view_ui_gpu *g);

/* Load (or re-load, when px moved) the cache. Nonzero when it can draw. */
int view_ui_font_ready(struct view_ui_gpu *g, SDL_Renderer *ren, int px);

/* The layer-wide alpha multiplier: every primitive below scales its alpha by
 * this. Set it around an element's paint, put it back to 255 after. */
void view_ui_alpha(unsigned a);

SDL_Color view_ui_rgb(unsigned r, unsigned g, unsigned b);

/* An SDL_Color from a palette word (VIEW_UI_COL_*, view_ui_chat_colour). */
SDL_Color view_ui_col(uint32_t rgb);

void view_ui_fill(SDL_Renderer *ren, int x, int y, int w, int h,
                  unsigned r, unsigned g, unsigned b, unsigned a);
void view_ui_border(SDL_Renderer *ren, int x, int y, int w, int h,
                    unsigned r, unsigned g, unsigned b);
/* Not const: a measure fills the cache with any code point it has not
 * seen. The texture waits for a draw; the metrics do not. */
int view_ui_text_width(struct view_ui_gpu *g, const char *s);
void view_ui_text(SDL_Renderer *ren, struct view_ui_gpu *g,
                  int x, int y, const char *s, SDL_Color col);

/* The three every element repeats: a ground with a border in one call, a
 * clip that can be put back, and one line of text laid inside a rect. They
 * moved here when the fourth element wrote them for the fourth time. */
void view_ui_panel(SDL_Renderer *ren, const struct openmmo_rect *r,
                   uint32_t ground, uint32_t line);
void view_ui_clip_push(SDL_Renderer *ren, const struct openmmo_rect *r,
                       SDL_Rect *saved);
void view_ui_clip_pop(SDL_Renderer *ren, const SDL_Rect *saved);
/* Vertically centred in `r`, inset by `pad`; `align` 0 is left, 1 right. */
void view_ui_text_in(SDL_Renderer *ren, struct view_ui_gpu *g,
                     const struct openmmo_rect *r, int pad, int align,
                     const char *s, uint32_t rgb);

/* view_ui_wrap's measure over this cache: pass the gpu as ctx. */
int view_ui_measure_gpu(void *ctx, const char *s, int len);

/* ------------------------------------------------------------------ */
/* The layer                                                           */
/* ------------------------------------------------------------------ */

/* One element: state plus the three verbs. Draw runs back to front in the
 * order elements were added; events and the pointer walk front to back, and
 * the first element to answer keeps the event, a click an element claims is
 * never also a pen tap. */
struct view_ui_element {
    const char *name;
    void *state;
    void (*draw)(void *state, SDL_Renderer *ren, struct view_ui_gpu *g,
                 const struct view_ui_frame *f);
    /* Nonzero: consumed. */
    int (*event)(void *state, const SDL_Event *ev,
                 const struct view_ui_frame *f);
    /* Nonzero: the pointer is on this element; no pen under it. */
    int (*owns_pointer)(void *state, const struct view_ui_frame *f,
                        int mx, int my);
};

#define VIEW_UI_ELEMENTS 8

struct view_ui_layer {
    struct view_ui_element el[VIEW_UI_ELEMENTS];
    int n;
    int canvas_mode; /* VIEW_UI_CANVAS_*; F10 cycles it at runtime */
};

void view_ui_layer_init(struct view_ui_layer *ui);

/* Returns 0 when the layer is full (and drops the element, loudly). */
int view_ui_layer_add(struct view_ui_layer *ui,
                      const struct view_ui_element *el);

void view_ui_layer_draw(struct view_ui_layer *ui, SDL_Renderer *ren,
                        struct view_ui_gpu *g, const struct view_ui_frame *f);
int view_ui_layer_event(struct view_ui_layer *ui, const SDL_Event *ev,
                        const struct view_ui_frame *f);
int view_ui_layer_owns_pointer(struct view_ui_layer *ui,
                               const struct view_ui_frame *f, int mx, int my);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_VIEW_UI_DRAW_H */
