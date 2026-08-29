/* The official client's Global Trade Link window (the "broker"). */

#ifndef OPENMMO_VIEW_UI_GTL_H
#define OPENMMO_VIEW_UI_GTL_H

#include "view_hud.h"
#include "view_ui_draw.h"
#include "view_ui_game.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The window was just raised: reset to the first tab and ask for its page. */
void view_ui_gtl_open(struct view_hud *hud);

/* Draw the body (everything under the title band) into frame layout L. */
void view_ui_gtl_draw(SDL_Renderer *ren, struct view_ui_gpu *g,
                      const struct view_ui_frame_layout *L,
                      struct view_hud *hud, int mx, int my);

/* Offer the body an event. Nonzero: consumed. */
int view_ui_gtl_event(const SDL_Event *ev,
                      const struct view_ui_frame_layout *L,
                      struct view_hud *hud);

/* Nonzero while one of the broker's text fields owns the keyboard. */
int view_ui_gtl_typing(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_VIEW_UI_GTL_H */
