/* Enough letters to say what went wrong, in the window. */

#ifndef OPENMMO_VIEW_FONT_H
#define OPENMMO_VIEW_FONT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OPENMMO_FONT_W 5
#define OPENMMO_FONT_H 7

/* The width in pixels `text` will occupy at `scale`, including the one-pixel
 * gap between glyphs but not after the last one. */
int openmmo_font_width(const char *text, int scale);

/*
 * Draw `text` into an RGB buffer of `w` x `h` words at (x, y), scaled by `scale` and in
 * `colour` (0x00RRGGBB). An unknown character is a filled box, so a message is never silently
 * blank.
 */
void openmmo_font_draw(uint32_t *px, int w, int h, int x, int y,
                       const char *text, int scale, uint32_t colour);

/* The same, centred on `cx`, what every message here actually wants. */
void openmmo_font_draw_centred(uint32_t *px, int w, int h, int cx, int y,
                               const char *text, int scale, uint32_t colour);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_VIEW_FONT_H */
