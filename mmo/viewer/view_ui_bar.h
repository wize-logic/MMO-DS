/* The official client's in-game HUD bar, on the window's own UI layer. */

#ifndef OPENMMO_VIEW_UI_BAR_H
#define OPENMMO_VIEW_UI_BAR_H

#include "view_hud.h"
#include "view_ui_draw.h"
#include "view_ui_game.h"
#include "view_ui_wins.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VIEW_UI_NOTICE_LEN 192

struct view_ui_bar {
    struct view_hud     *hud;   /* the page: snapshot in, commands out */
    struct view_ui_wins *wins;  /* where a WINDOW action lands */
    int   menu;                 /* the open popup, or VIEW_UI_MENU_NONE */
    struct openmmo_rect anchor; /* the button it hangs from */
    /* What was drawn last frame. Events answer against these rather than
     * re-placing: the draw side measures with the real glyph advances and
     * the event side has no font, so laying out twice would put the click
     * one button along from the label under the pointer. */
    struct view_ui_bar_layout  laid_bar;
    struct view_ui_menu_layout laid_menu;
    int   laid;
    int   alpha;
    int   faded;
    int   quit;                 /* Menu > Exit was chosen */
    /* Menu > Logout: official confirms first (string 1160), then the guest
     * takes the session back to character select. */
    int   confirm_logout;
    char  notice[VIEW_UI_NOTICE_LEN];
    Uint64 notice_at;           /* when it went up, in SDL ticks */
};

void view_ui_bar_init(struct view_ui_bar *b, struct view_hud *hud,
                      struct view_ui_wins *wins);

/* Nonzero once the player has chosen Exit. The window polls this and ends
 * its loop, which is the same door Esc opens. */
int view_ui_bar_quit(const struct view_ui_bar *b);

struct view_ui_element view_ui_bar_element(struct view_ui_bar *b);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_VIEW_UI_BAR_H */
