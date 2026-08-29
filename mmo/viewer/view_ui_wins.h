/* The official client's in-game frames, on the window's own UI layer. */

#ifndef OPENMMO_VIEW_UI_WINS_H
#define OPENMMO_VIEW_UI_WINS_H

#include "view_hud.h"
#include "view_ui_draw.h"
#include "view_ui_game.h"

#ifdef __cplusplus
extern "C" {
#endif

struct view_ui_win_state {
    int open;
    int drag_x, drag_y; /* where the player has moved it from centre */
    int scroll;         /* rows down from the top of the list */
    int tab;            /* the social window's; the others hold 0 */
    int sel;            /* the selected list row, or -1 */
};

struct view_ui_wins {
    struct view_hud *hud;
    struct view_ui_win_state w[VIEW_UI_WIN_N];
    /* Front to back. order[0] is the topmost frame: it draws last and is
     * offered every event first. */
    int order[VIEW_UI_WIN_N];
    int dragging;       /* the frame being moved, or -1 */
    int grab_x, grab_y;
    int alpha;
    int faded;
    /* The official client's player menu, raised by a click on a row that names one.
     * One at a time, over every frame. `laid` is the layout as it was
     * drawn (the font's real advances), which is what a click answers
     * against. */
    struct {
        int open;
        int at_x, at_y;
        int is_friend;
        int laid_ok;
        char who[OPENMMO_HUD_NAME];
        struct view_ui_menu_layout laid;
    } ctx;
    /* The action row's editfield: one, owned by (win, tab). */
    struct {
        int win;        /* -1: no field focused */
        int tab;
        int len;
        char text[OPENMMO_HUD_NAME];
    } field;
    /* The official client's confirm (string 1654) before an act that removes. -1: none.
     * `line`, when set, is shown verbatim instead of the remove string, 
     * the trade offer and the GTL borrow the popup that way. `gtl` set
     * means Yes pushes CMD_GTL with `gtl_arg` instead of a player act. */
    struct {
        int act;
        int gtl;
        uint32_t gtl_arg;
        char who[OPENMMO_HUD_NAME];
        char line[OPENMMO_HUD_NAME * 2];
    } confirm;
};

void view_ui_wins_init(struct view_ui_wins *w, struct view_hud *hud);

/* Raise it if it is closed (and bring it to the front), close it if it is
 * open, which is what every one of the official client's own HUD entries does. */
void view_ui_wins_toggle(struct view_ui_wins *w, int id);

/* Nonzero while a frame's editfield owns the keyboard; the viewer folds it
 * into frame->typing so no element treats a typed letter as a chord. */
int view_ui_wins_typing(const struct view_ui_wins *w);

struct view_ui_element view_ui_wins_element(struct view_ui_wins *w);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_VIEW_UI_WINS_H */
