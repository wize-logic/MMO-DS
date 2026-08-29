/* The window's own UI layer: the whole window is the canvas. */

#ifndef OPENMMO_VIEW_UI_H
#define OPENMMO_VIEW_UI_H

#include <stddef.h>
#include <stdint.h>

#include "view_geom.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Which part of the window the layer treats as its canvas: both screens at once (the whole
 * window), the world screen's rect, or the second screen's.
 */
enum view_ui_canvas_mode {
    VIEW_UI_CANVAS_BOTH = 0,
    VIEW_UI_CANVAS_TOP,
    VIEW_UI_CANVAS_BOTTOM,
    VIEW_UI_CANVAS_MODES
};

/* The canvas rect for a mode. A degenerate screen rect (nothing presented
 * there this frame) falls back to the whole window rather than a sliver. */
struct openmmo_rect view_ui_canvas(int mode, int win_w, int win_h,
                                   const struct openmmo_rect *top,
                                   const struct openmmo_rect *bottom);

/* What one frame of the window looks like to an element: the canvas it
 * places itself on, where the two screens landed, and whether a text field
 * owns the keyboard (an element must not claim Enter while a line is being
 * typed). */
struct view_ui_frame {
    int win_w, win_h;
    int typing;
    struct openmmo_rect canvas; /* place against this */
    struct openmmo_rect world;  /* the big picture (dst[0]) */
    struct openmmo_rect touch;  /* the second screen (dst[1]) */
};

/* The corner of the canvas an element hangs from. */
enum view_ui_corner {
    VIEW_UI_TOP_LEFT,
    VIEW_UI_TOP_RIGHT,
    VIEW_UI_BOTTOM_LEFT,
    VIEW_UI_BOTTOM_RIGHT
};

struct openmmo_rect view_ui_place(const struct openmmo_rect *canvas,
                                  int corner, int pad, int w, int h);

/* Nonzero when (x,y) is inside r. */
int view_ui_hit(const struct openmmo_rect *r, int x, int y);

int view_ui_clampi(int v, int lo, int hi);

/* The layer's type size for a canvas this tall: 16px at 1080, never under
 * 12 or over 20. The panel scales to its own band; elements scale to the
 * canvas they are placed on. */
int view_ui_text_px(int canvas_h);

/* One step of the panel's alpha ramp: snap to `want` on the first call,
 * then walk toward it by `step`. `*started` is the element's own flag. */
int view_ui_fade(int alpha, int want, int step, int *started);

/* The palette, shared with the host panel (the launcher's logingui colours).
 * 0xRRGGBB; the draw side multiplies its own alpha in. */
#define VIEW_UI_COL_GROUND 0x12161Cu /* 18, 22, 28 */
#define VIEW_UI_COL_FIELD  0x161A20u /* 22, 26, 32 */
#define VIEW_UI_COL_LINE   0x4A5560u /* 74, 85, 96 */
#define VIEW_UI_COL_TEXT   0xF2F2F2u
#define VIEW_UI_COL_DIM    0xA0A0A8u
#define VIEW_UI_COL_ACCENT 0x81DDF1u /* 129, 221, 241 */

/* One chat line's colour, by wire type (OPENMMO_HUD_CHAT_*). */
uint32_t view_ui_chat_colour(uint32_t type);

/* ------------------------------------------------------------------ */
/* Wrapping                                                            */
/* ------------------------------------------------------------------ */

#define VIEW_UI_ROW_LEN 144

/* One character of `s`, as a code point and the bytes it took. */
unsigned view_ui_utf8(const char *s, uint32_t *cp);

/* Drop the last character of `s` in place, and return its new length. The
 * backspace key in every field on this layer: a byte-at-a-time delete leaves
 * half a sequence behind, which is not a shorter name but a broken one. */
int view_ui_utf8_trunc(char *s);

/* Measure a prefix of `s` (len bytes) in pixels; ctx is the caller's font. */
typedef int (*view_ui_measure)(void *ctx, const char *s, int len);

/* Break one line into rows no wider than `width` measured pixels, cut at the
 * last space that fits; a word wider than the box is cut at the width,
 * because the alternative is a row that runs off the edge anyway. Rows land
 * in rows[at..], each tagged with `tag`; returns the new count. */
int view_ui_wrap(view_ui_measure measure, void *ctx, int width, uint32_t tag,
                 const char *s, char rows[][VIEW_UI_ROW_LEN], uint32_t *tags,
                 int at, int cap);

/* ------------------------------------------------------------------ */
/* The chat box                                                        */
/* ------------------------------------------------------------------ */

/* The official client's channel tabs across the top of the box: the official client's own shipped set
 * (config/main.properties, client.ui.chat.tab.names). */
#define VIEW_UI_CHAT_TABS 5

/* The layer's first element: the log and the compose row, bottom-left,
 * whatever the guest's second screen is doing. */
struct view_ui_chat_layout {
    struct openmmo_rect box;   /* the whole element */
    struct openmmo_rect log;   /* the wrapped rows */
    struct openmmo_rect input; /* the compose row */
    struct openmmo_rect tab[VIEW_UI_CHAT_TABS]; /* the channel tabs */
    int tabs;                  /* how many tab rects are placed; 0 = a canvas
                                * too short for chrome dropped the strip
                                * before dropping the log */
    int text_px;
    int line_h;
    int rows;                  /* how many log rows fit */
    int pad;                   /* inner padding */
};

void view_ui_chat_place(const struct openmmo_rect *canvas,
                        struct view_ui_chat_layout *out);

enum {
    VIEW_UI_CHAT_HIT_NONE = 0,
    VIEW_UI_CHAT_HIT_LOG,
    VIEW_UI_CHAT_HIT_INPUT
};

int view_ui_chat_hit(const struct view_ui_chat_layout *L, int x, int y);

/* Which channel tab (x,y) lands on, or -1. A tab click also answers
 * VIEW_UI_CHAT_HIT_LOG above, so ask this first. */
int view_ui_chat_tab_hit(const struct view_ui_chat_layout *L, int x, int y);

/* Tab `tab`'s label, NULL past the strip. */
const char *view_ui_chat_tab_label(int tab);

/* The label for send channel `type` (OPENMMO_HUD_CHAT_*), NULL for NORMAL
 * and anything no tab sends on. This is the input-line prefix's key: the
 * prefix names the channel the guest published, not the tab on screen. */
const char *view_ui_chat_type_label(uint32_t type);

/* Nonzero when tab `tab` shows a line of wire type `type`. */
int view_ui_chat_tab_shows(int tab, uint32_t type);

/* The wire chat type a line composed on tab `tab` sends on
 * (OPENMMO_HUD_CHAT_*): the official client's channel picker follows the tab. Normal for
 * an out-of-range tab. The window tells the guest with
 * OPENMMO_HUD_CMD_CHANNEL. */
uint32_t view_ui_chat_tab_send_type(int tab);

/*
 * The compose row's text: the typed line with a caret while composing, prefixed with its
 * whisper target when the guest opened one, else with `channel` (the guest's published send
 * channel's label; NULL on the default channel), the standing hint otherwise.
 */
int view_ui_chat_input_line(int composing, const char *compose,
                            const char *to, const char *channel,
                            char *dst, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_VIEW_UI_H */
