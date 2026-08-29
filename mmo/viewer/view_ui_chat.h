/* The chat box, the UI layer's first element. */

#ifndef OPENMMO_VIEW_UI_CHAT_H
#define OPENMMO_VIEW_UI_CHAT_H

#include "view_hud.h"
#include "view_ui_draw.h"

#ifdef __cplusplus
extern "C" {
#endif

struct view_ui_chat {
    struct view_hud *hud; /* the page: snapshot in, commands out */
    int scroll;           /* wrapped rows up from the tail */
    int tab;              /* the shown channel tab (view_ui_chat_tab_*);
                           * 0 = Local at boot, the official client's own default */
    int alpha;
    int faded;            /* the ramp has run at least once */
};

void view_ui_chat_init(struct view_ui_chat *c, struct view_hud *hud);

/* Nonzero while (mx, my) is over the chat panel. The window's event loop
 * asks so a click anywhere else can defocus an open compose line, focus
 * follows the click, the way every MMO's chat behaves. */
int view_ui_chat_pointer_on(struct view_ui_chat *c,
                            const struct view_ui_frame *f, int mx, int my);

/* The element, ready for view_ui_layer_add. */
struct view_ui_element view_ui_chat_element(struct view_ui_chat *c);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_VIEW_UI_CHAT_H */
