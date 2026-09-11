/* The official client's own art, loaded at runtime. */

#ifndef OPENMMO_VIEW_UI_THEME_H
#define OPENMMO_VIEW_UI_THEME_H

#include <stddef.h>
#include <stdint.h>

#include "view_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The sheets the used regions are cut from, under the theme root. */
enum view_ui_sheet {
    VIEW_UI_SHEET_UI = 0,   /* res/pokemmo_ui.png      (471x421) */
    VIEW_UI_SHEET_HUD,      /* res/main-hud.png        (560x560) */
    VIEW_UI_SHEET_WIN,      /* res/user-interface.png  (512x512) */
    VIEW_UI_SHEET_MON,      /* res/monster-info.png    (the HP bar) */
    VIEW_UI_SHEET_N
};

/* The sheet's path under the theme root ("res/pokemmo_ui.png"). */
const char *view_ui_sheet_file(int sheet);

/* The size the region table was measured against; a smaller file is a
 * different theme layout and is refused rather than blitted skewed. */
void view_ui_sheet_min(int sheet, int *w, int *h);

/* The surfaces this layer draws with official art. */
enum view_ui_th {
    VIEW_UI_TH_PANEL = 0,   /* ui-popup.background: the bar, popups, notices */
    VIEW_UI_TH_BUTTON,      /* ui-button.default: bar buttons, party slots */
    VIEW_UI_TH_BUTTON_HOVER,/* ui-button.hover.background, main-color tint */
    VIEW_UI_TH_BUTTON_DOWN, /* ui-button.pressed.background */
    VIEW_UI_TH_BUTTON_DIS,  /* ui-button.disabled */
    VIEW_UI_TH_ROW_HOVER,   /* ui-popup-button.selected: a hot menu row */
    VIEW_UI_TH_INPUT,       /* ui-inputbox.default: the compose row */
    VIEW_UI_TH_FRAME,       /* frame-draggable.background: window chrome */
    VIEW_UI_TH_CLOSE,       /* close-handle: the title bar's X */
    VIEW_UI_TH_HPBAR,       /* mi-hpbar.background: the HP bar's trough */
    VIEW_UI_TH_TAB,         /* ui-tab.inactive, its #C5C5C5 rest tint */
    VIEW_UI_TH_TAB_ACTIVE,  /* ui-tab.active */
    VIEW_UI_TH_HEADER,      /* ui-misc-btn: a table's column header */
    VIEW_UI_TH_ROW,         /* ui-table-row: ui-inputbox.default, inset 1 */
    VIEW_UI_TH_WARN,        /* ui-popup-warning: the confirm box */
    VIEW_UI_TH_N
};

/* Which sheet a surface is cut from (VIEW_UI_SHEET_*). */
int view_ui_theme_sheet(int id);

/* The tint the theme applies when it draws this surface, 0xAARRGGBB;
 * 0xFFFFFFFF for art drawn as it is. */
uint32_t view_ui_theme_tint(int id);

/* A grid is at most 3 columns by 4 rows (frame-draggable's shape); a
 * nine-patch is the 3x3 case and a plain blit the 1x1. */
#define VIEW_UI_TH_CELLS 12

struct view_ui_th_pair {
    struct openmmo_rect src, dst;
};

/* Cut surface `id` over `dst`: each cell's source texels and the window rect
 * they land on. Fixed spans scale with the type size the way every official
 * number does (view_ui_scale); the stretching column and row take what is
 * left. Returns the cell count, 0 for an unknown id or an empty dst. */
int view_ui_theme_cells(int id, const struct openmmo_rect *dst, int text_px,
                        struct view_ui_th_pair out[VIEW_UI_TH_CELLS]);

/* The same grid read the other way: `out[i].src` is where the cell sits on
 * the sheet and `out[i].dst` its place in the surface's own footprint, which
 * comes back in `w`/`h`. What a painter filling a sheet of its own needs
 * (view_ui_skin.c). Returns the cell count. */
int view_ui_theme_source(int id, struct view_ui_th_pair out[VIEW_UI_TH_CELLS],
                         int *w, int *h);

/* ------------------------------------------------------------------ */
/* The sheets' pixels                                                  */
/* ------------------------------------------------------------------ */

/* Decode one PNG (8-bit RGB or RGBA, not interlaced, every sheet the
 * default theme ships is) into a malloc'd RGBA buffer, w*h*4 bytes.
 * NULL on anything else; the caller frees. */
unsigned char *view_ui_png(const unsigned char *buf, size_t len,
                           int *w, int *h);

struct view_ui_theme_px {
    unsigned char *rgba;    /* malloc'd, w*h*4 */
    int w, h;
};

struct view_ui_theme {
    struct view_ui_theme_px sheet[VIEW_UI_SHEET_N];
    int ready;
};

/* Read every sheet under `dir` (the theme root holding res/). Nonzero and
 * `ready` when all of them decoded at least their measured size; a miss
 * frees what was read and reports which file, so a wrong path is one line
 * in the log rather than a half-themed window. */
int view_ui_theme_read(struct view_ui_theme *t, const char *dir);

void view_ui_theme_free(struct view_ui_theme *t);

/* The theme's own face (res/fonts/NotoSansCJK-Medium.ttc, the face official
 * renders every one of these surfaces with), if it is there. Nonzero and
 * the path in `out` when it is. */
int view_ui_theme_font(const char *dir, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_VIEW_UI_THEME_H */
