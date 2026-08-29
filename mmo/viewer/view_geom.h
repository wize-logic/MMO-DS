/*
 * Where the two screens land in the window, and how their pixels are enlarged.
 * No SDL, no shared page: integers in, rectangles out.
 */

#ifndef OPENMMO_VIEW_GEOM_H
#define OPENMMO_VIEW_GEOM_H

#include <stdint.h>

#include "view_channel.h"

#ifdef __cplusplus
extern "C" {
#endif

enum openmmo_view_layout {
    OPENMMO_LAYOUT_SMART,   /* the game large, the touch screen beside it and
                             * growing to match when the game asks for the pen */
    OPENMMO_LAYOUT_STACKED, /* the console's own: top screen above the touch one */
    OPENMMO_LAYOUT_WIDE,    /* side by side, top screen on the left */
    OPENMMO_LAYOUT_FILL     /* poketch: a 3:2 overlay on the world; anything
                             * else: the two screens sit apart, world left */
};

enum openmmo_view_aspect {
    OPENMMO_ASPECT_NATIVE,  /* the DS's own proportions, letterboxed */
    OPENMMO_ASPECT_STRETCH  /* fill the window, proportions be damned */
};

enum openmmo_view_filter {
    OPENMMO_FILTER_NEAREST,
    OPENMMO_FILTER_LINEAR,
    OPENMMO_FILTER_SCALE2X
};

/*
 * The smart layout's two sizes and the ramp between them, as a percentage of the main screen.
 */
#define OPENMMO_VIEW_SEC_MIN  50
#define OPENMMO_VIEW_SEC_MAX 100
#define OPENMMO_VIEW_SEC_STEP  4

/*
 * Fill, two placements. The poketch overlay is 3:2 in the bottom-right, height the geometric
 * mean of the window height and OVER_REF.
 */
#define OPENMMO_VIEW_SEC_REF    120
#define OPENMMO_VIEW_OVER_REF    64
#define OPENMMO_VIEW_SEC_MIN_PX 192

#define OPENMMO_VIEW_SCALE_AUTO 0

struct openmmo_rect { int x, y, w, h; };

/* Everything the geometry needs to know about how the player set the window up.
 * A struct rather than three arguments because all three travel together. */
struct openmmo_view_geom {
    int layout;   /* enum openmmo_view_layout */
    int aspect;   /* enum openmmo_view_aspect */
    int integer;  /* scale by whole source pixels only */
    int overlay;  /* fill: 1 poketch over the world, 0 screens apart */
    /*
     * The game has handed the player the two screens the other way round: the world is on the
     * touch screen and the other screen is a map of it.
     */
    int swapped;
    /*
     * The window is drawing no second screen at all: the guest's lower screen is the Poketch
     * and this window does not draw one.
     */
    int hide_second;
};

/* Whether this geometry hands the large rect to the touch screen. */
static inline int openmmo_view_swaps_screens(const struct openmmo_view_geom *g)
{
    return g != 0 && g->swapped
        && (g->layout == OPENMMO_LAYOUT_FILL
            || g->layout == OPENMMO_LAYOUT_SMART);
}

/*
 * Fit src into win preserving proportions, centred; integer mode snaps to whole
 * multiples once the window is at least source-sized.
 */
struct openmmo_rect openmmo_view_fit(int src_w, int src_h, int win_w, int win_h,
                                     int integer, int aspect);

/*
 * The composed picture's size in source pixels. The window is sized for the 256x192 panel
 * (multiplied when the game is at a higher internal resolution), not for extra published
 * columns: those are 3D-or-black and do not take layout space.
 */
void openmmo_view_composed_wh(int layout, int src_w, int src_h, int sec,
                              int *cw, int *ch);

/*
 * The largest 16:9 size from the default-window table that fits in 90 % of the display's
 * usable bounds. Floor 856x480.
 */
void openmmo_view_auto_wh(int usable_w, int usable_h, int *w, int *h);

/*
 * The fill overlay (poketch): 3:2, bottom-right. `sec` 50 is rest, 100 is
 * the pen-wanted size.
 */
void openmmo_view_fill_panel(int win_w, int win_h, int sec,
                             struct openmmo_rect *panel);

/*
 * The fill split (bag, battle, lobby): a full-height band on the right.
 * Same `sec` ramp as the overlay.
 */
void openmmo_view_fill_split(int win_w, int win_h, int sec,
                             struct openmmo_rect *panel);

/*
 * The guest's 256x192 picture, inset 4:3 and centred inside the fill panel.
 */
void openmmo_view_fill_guest(const struct openmmo_rect *panel,
                             struct openmmo_rect *guest);

/*
 * The rectangle inside a published frame that is the 256-column panel, in the upscaled
 * texture's pixels (`rs` is the render scale).
 */
void openmmo_view_panel_src(int src_w, int src_h, int rs,
                            struct openmmo_rect *src);

/*
 * Both screens' rectangles for a window of win_w x win_h. dst[0] is the top screen, dst[1] the
 * touch screen.
 */
struct openmmo_rect openmmo_view_screen_rects(const struct openmmo_view_geom *g,
                                              int src_w, int src_h, int sec,
                                              int win_w, int win_h,
                                              struct openmmo_rect dst[2]);

/*
 * The pointer, inverted through the same layout: window coordinates to a touch-screen pixel,
 * or -1 when the pointer is not on the panel.
 */
int openmmo_view_window_to_touch(const struct openmmo_view_geom *g,
                                 int src_w, int src_h, int sec,
                                 int win_w, int win_h,
                                 int mx, int my, int *tx, int *ty);

/* The enhancement scalers. Each writes w*f x h*f pixels; none of them ever
 * writes a colour the source does not already hold. */
void openmmo_view_replicate(const uint32_t *s, int w, int h, uint32_t *d, int f);
void openmmo_view_scale2x(const uint32_t *s, int w, int h, uint32_t *d);
void openmmo_view_scale3x(const uint32_t *s, int w, int h, uint32_t *d);

/* src (w x h) into dst (w*rs x h*rs); `mid` is scratch for rs==4 with scale2x
 * and may be NULL otherwise. rs==1 is a copy, so every caller can texture from
 * dst without a second path. */
void openmmo_view_upscale(const uint32_t *src, int w, int h,
                          uint32_t *dst, uint32_t *mid, int rs, int filter);

/* Clock-pace for one present when the renderer has no vsync, in whole milliseconds. */
static inline unsigned openmmo_view_pace_ms(unsigned tick)
{
    return (tick % 3u == 0u) ? 16u : 17u;
}

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_VIEW_GEOM_H */
