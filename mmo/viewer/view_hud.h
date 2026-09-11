/* The window's end of the game's hud page. */

#ifndef OPENMMO_VIEW_HUD_H
#define OPENMMO_VIEW_HUD_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "hud_channel.h"
#include "platform.h"

#ifdef __cplusplus
extern "C" {
#endif

struct view_hud {
    struct openmmo_hud_shm *page;
    mmo_shm  page_mem;
    int      any;
    int      have_font;
    int      compose;  /* the window's text field is open this frame */
    int      dump;
    struct openmmo_hud_snap snap;
    struct openmmo_hud_font font;
};

void view_hud_init(struct view_hud *h);
void view_hud_attach(struct view_hud *h, const char *channel);
void view_hud_close(struct view_hud *h);

/* Re-read the snapshot. Returns nonzero when it moved. */
int view_hud_poll(struct view_hud *h);

void view_hud_push(struct view_hud *h, uint32_t kind, int32_t arg);
/* A verb naming one listing, letter or item by its id rather than by where it
 * sat on the page the window drew. */
void view_hud_push_id(struct view_hud *h, uint32_t kind, int32_t arg,
                      uint32_t id_lo, uint32_t id_hi);
/* CMD_PLAYER with its name: the official client's player menu acting through the guest. */
void view_hud_push_player(struct view_hud *h, int32_t verb, const char *name);
void view_hud_dump(const struct view_hud *h, FILE *out);

/* A nameplate's box, in the pixels it will be drawn with: the ROM font's width
 * for the name plus the border the guest paint used. VIEW_HUD_PLATE_H is that
 * paint's own box height (openmmo_label.c), one native row per scale step. */
#define VIEW_HUD_PLATE_H 14

int view_hud_plate_size(const struct openmmo_hud_font *font, const char *name,
                        int scale, int *w, int *h);

/* Raster one plate into an ARGB buffer of cap_w x cap_h. Returns 0 when the
 * name is empty or the box does not fit, in which case nothing is written. */
int view_hud_plate_raster(uint32_t *px, int cap_w, int cap_h,
                          const struct openmmo_hud_font *font,
                          const char *name, int scale, int *out_w, int *out_h);

/* One chat row as the window prints it. No brackets: the ROM font has none,
 * and "[NOTICE]" draws as "?NOTICE?". */
void view_hud_format_chat(const struct openmmo_hud_chat *c, char *dst, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_VIEW_HUD_H */
