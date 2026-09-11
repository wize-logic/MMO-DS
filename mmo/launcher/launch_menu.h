/* What a key does to the front door. */

#ifndef OPENMMO_LAUNCH_MENU_H
#define OPENMMO_LAUNCH_MENU_H

#include "launch_plan.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MMO_LAUNCH_ROW_TEXT = 0,
    MMO_LAUNCH_ROW_SECRET,
    MMO_LAUNCH_ROW_CHOICE,
    MMO_LAUNCH_ROW_ACTION
};

enum {
    MMO_LAUNCH_R_USER = 0,
    MMO_LAUNCH_R_PASS,
    MMO_LAUNCH_R_ROM,
    MMO_LAUNCH_R_PACE,
    MMO_LAUNCH_R_SCALE,
    MMO_LAUNCH_R_VIEWPORT,
    MMO_LAUNCH_R_LAYOUT,
    MMO_LAUNCH_R_RS,
    MMO_LAUNCH_R_HD3D,
    MMO_LAUNCH_R_FILTER,
    MMO_LAUNCH_R_FIT,
    MMO_LAUNCH_R_FULLSCREEN,
    MMO_LAUNCH_R_AUDIO,
    MMO_LAUNCH_R_BUTTONS,
    MMO_LAUNCH_R_DISCORD,
    MMO_LAUNCH_R_PLAY,
    MMO_LAUNCH_R_PLAY_OFFLINE,
    MMO_LAUNCH_R_QUIT,
    MMO_LAUNCH_R_COUNT
};

enum {
    MMO_LAUNCH_KEY_UP = 0,
    MMO_LAUNCH_KEY_DOWN,
    MMO_LAUNCH_KEY_LEFT,
    MMO_LAUNCH_KEY_RIGHT,
    MMO_LAUNCH_KEY_ENTER,
    MMO_LAUNCH_KEY_ESC,
    MMO_LAUNCH_KEY_BACKSPACE
};

enum {
    MMO_LAUNCH_MENU_NONE = 0,
    MMO_LAUNCH_MENU_PLAY,
    MMO_LAUNCH_MENU_PLAY_OFFLINE,
    MMO_LAUNCH_MENU_QUIT,
    MMO_LAUNCH_MENU_PICK_ROM
};

typedef struct mmo_launch_menu {
    mmo_launch_settings *set;
    int   sel;
    int   editing;
    char  edit[MMO_LAUNCH_PATH];
    char  status[192];
    int   status_bad;
} mmo_launch_menu;

void mmo_launch_menu_init(mmo_launch_menu *m, mmo_launch_settings *s);

void mmo_launch_menu_say(mmo_launch_menu *m, int bad, const char *fmt, ...);

int mmo_launch_menu_kind(int row);
const char *mmo_launch_menu_label(int row);
const char *mmo_launch_menu_value(const mmo_launch_menu *m, int row,
                                  char *scratch, size_t cap);

/* One key, the same ones the window's usage string names. The two PLAY rows
 * and QUIT are returned so the caller can fork or exit; everything else is
 * applied here. */
int mmo_launch_menu_key(mmo_launch_menu *m, int key);

/* UTF-8 typed into an open field. Ignored when the menu is not editing. */
void mmo_launch_menu_text(mmo_launch_menu *m, const char *utf8);

/*
 * One script line: up/down/left/right/enter/esc/backspace, or `text ...` for the rest of the
 * line, or `#` / blank which do nothing. Returns the same codes as mmo_launch_menu_key, or -1
 * for a line this does not know.
 */
int mmo_launch_menu_line(mmo_launch_menu *m, const char *line);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_LAUNCH_MENU_H */
