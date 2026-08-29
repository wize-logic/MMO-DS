/* The party strip, on the window's own UI layer. */

#ifndef OPENMMO_VIEW_UI_PARTY_H
#define OPENMMO_VIEW_UI_PARTY_H

#include "view_hud.h"
#include "view_ui_draw.h"
#include "view_ui_game.h"

#ifdef __cplusplus
extern "C" {
#endif

struct view_ui_party {
    struct view_hud *hud;
    int alpha;
    int faded;
    int hidden;     /* the player folded it away */
    /* A slot being dragged onto another, the official client's reorder gesture. Armed on
     * the press, a drag once the pointer has moved a few pixels; a release
     * that never became one is the click that opens the summary. */
    int press_slot; /* -1: nothing armed */
    int press_x, press_y;
    int press_off_x, press_off_y; /* the grab point inside the slot */
    int dragging;
};

void view_ui_party_init(struct view_ui_party *p, struct view_hud *hud);

struct view_ui_element view_ui_party_element(struct view_ui_party *p);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_VIEW_UI_PARTY_H */
