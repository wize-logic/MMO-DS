/* The official client's Mail window. */

#ifndef OPENMMO_VIEW_UI_MAIL_H
#define OPENMMO_VIEW_UI_MAIL_H

#include "view_hud.h"
#include "view_ui_draw.h"
#include "view_ui_game.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The window was just raised: back to the inbox, and ask for its first page. */
void view_ui_mail_open(struct view_hud *hud);

/* Draw the body (everything under the title band) into frame layout L. */
void view_ui_mail_draw(SDL_Renderer *ren, struct view_ui_gpu *g,
                       const struct view_ui_frame_layout *L,
                       struct view_hud *hud, int mx, int my);

/* Offer the body an event. Nonzero: consumed. */
int view_ui_mail_event(const SDL_Event *ev,
                       const struct view_ui_frame_layout *L,
                       struct view_hud *hud);

/* Nonzero while one of the mail form's text fields owns the keyboard. */
int view_ui_mail_typing(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_VIEW_UI_MAIL_H */
