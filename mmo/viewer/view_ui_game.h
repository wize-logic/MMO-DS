/* The official client's in-game UI, laid out. */

#ifndef OPENMMO_VIEW_UI_GAME_H
#define OPENMMO_VIEW_UI_GAME_H

#include <stddef.h>
#include <stdint.h>

#include "hud_channel.h"
#include "view_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The official client's own font size. Every constant below is in its pixels. */
#define VIEW_UI_OFFICIAL_PX 12

/* A official pixel measurement at this canvas's type size. */
int view_ui_scale(int v, int text_px);

/* Clamp a row scroll to a list of `total` rows showing `rows` at a time. */
int view_ui_scroll_clamp(int scroll, int total, int rows);

/* ------------------------------------------------------------------ */
/* What a button or a menu row does                                    */
/* ------------------------------------------------------------------ */

enum view_ui_act {
    VIEW_UI_ACT_NONE = 0,
    VIEW_UI_ACT_MENU,   /* arg: a view_ui_menu_id, open that popup */
    VIEW_UI_ACT_SCREEN, /* arg: OPENMMO_HUD_SCREEN_*, ask the guest for it */
    VIEW_UI_ACT_WINDOW, /* arg: a view_ui_win_id, raise that frame */
    VIEW_UI_ACT_NOTICE, /* say why not, the way the official client does */
    VIEW_UI_ACT_LOGOUT, /* back to character select, after the official client's confirm */
    VIEW_UI_ACT_QUIT    /* end the session */
};

/* One button or row: what it says, what it does, the key that does it
 * without the pointer, and, for a surface this client has no server for, 
 * what official says instead. A NULL notice is the official client's own sentence with the
 * label as its subject. */
struct view_ui_item {
    const char *label;
    /*
     * What the bar prints when ten full labels will not fit the strip it is allowed. The official client
     * drops the label entirely at that point and keeps its icon reel; the reel is official art
     * and is not shipped, so these are ours.
     */
    const char *shortl;
    char        key;   /* lowercase ASCII, 0 for none */
    int         act;
    int         arg;
    const char *notice;
};

/* The sentence a NOTICE row prints. */
void view_ui_notice_text(const struct view_ui_item *it, char *dst, size_t cap);

/* ------------------------------------------------------------------ */
/* The bar                                                             */
/* ------------------------------------------------------------------ */

enum view_ui_bar_id {
    VIEW_UI_BAR_BAG = 0,
    VIEW_UI_BAR_TRAINER,
    VIEW_UI_BAR_COMMUNITY,
    VIEW_UI_BAR_PVP,
    VIEW_UI_BAR_DEX,
    VIEW_UI_BAR_INCUBATOR,
    VIEW_UI_BAR_TRADE,
    VIEW_UI_BAR_MAIL,
    VIEW_UI_BAR_SHOP,
    VIEW_UI_BAR_MENU,
    VIEW_UI_BAR_N
};

const struct view_ui_item *view_ui_bar_item(int i);

/* The label to print for button `i`: its short form when the layout says the
 * bar had to shorten, its own otherwise. */
const char *view_ui_bar_label(int i, int shortened);

struct view_ui_bar_layout {
    struct openmmo_rect bar;
    struct openmmo_rect btn[VIEW_UI_BAR_N];
    int text_px;
    int pad;    /* the panel's own border */
    int gap;    /* between buttons */
    int btn_h;
    int shortened; /* the labels had to give way; view_ui_bar_label */
};

/* Lay the bar out against the bottom-right of `canvas`. `measure` is the
 * caller's font (view_ui_measure, as view_ui_wrap takes); passing NULL sizes
 * every button from its label length instead, which is what the suite does. */
void view_ui_bar_place(const struct openmmo_rect *canvas,
                       view_ui_measure measure, void *ctx,
                       struct view_ui_bar_layout *out);

/* Which button (view_ui_bar_id), or -1. */
int view_ui_bar_hit(const struct view_ui_bar_layout *L, int x, int y);

/* ------------------------------------------------------------------ */
/* The popup menus                                                     */
/* ------------------------------------------------------------------ */

enum view_ui_menu_id {
    VIEW_UI_MENU_NONE = 0,
    VIEW_UI_MENU_COMMUNITY,
    VIEW_UI_MENU_PVP,
    VIEW_UI_MENU_GAME,
    VIEW_UI_MENU_N
};

#define VIEW_UI_MENU_ROWS 8

struct view_ui_menu_def {
    const char *title;
    int n;
    struct view_ui_item item[VIEW_UI_MENU_ROWS];
};

/* NULL for VIEW_UI_MENU_NONE or an id past the table. */
const struct view_ui_menu_def *view_ui_menu_def(int id);

/* Rows only: the official client's menupopup is its entries on the popup ground, with no
 * title band (init.xml `menupopup`; the def's title names the menu for the
 * tests and the log, not for a caption row). */
struct view_ui_menu_layout {
    struct openmmo_rect box;
    struct openmmo_rect row[VIEW_UI_MENU_ROWS];
    int text_px;
    int pad;
    int row_h;
    int n;
};

/* Hang the popup off `anchor` (the button that opened it), opening upward
 * because the bar is on the floor of the window, and clamped into `canvas`. */
void view_ui_menu_place(const struct openmmo_rect *canvas,
                        const struct openmmo_rect *anchor, int id,
                        view_ui_measure measure, void *ctx,
                        struct view_ui_menu_layout *out);

/* Which row, or -1. */
int view_ui_menu_hit(const struct view_ui_menu_layout *L, int x, int y);

/* ------------------------------------------------------------------ */
/* The frames                                                          */
/* ------------------------------------------------------------------ */

enum view_ui_win_id {
    VIEW_UI_WIN_FRIENDS = 0,
    VIEW_UI_WIN_TEAM,
    VIEW_UI_WIN_NEARBY,
    VIEW_UI_WIN_INSTANCE,
    VIEW_UI_WIN_GTL,
    VIEW_UI_WIN_MAIL,
    VIEW_UI_WIN_N
};

struct view_ui_win_def {
    const char *title;
    int w, h;   /* the official client's own size, in its 12px pixels */
};

const struct view_ui_win_def *view_ui_win_def(int id);

struct view_ui_frame_layout {
    struct openmmo_rect box;
    struct openmmo_rect title;  /* where the caption is printed */
    struct openmmo_rect close;  /* the close button, as it is drawn */
    /* WHAT A TAP ANSWERS AGAINST, which is bigger than what is drawn. */
    struct openmmo_rect close_hit;
    struct openmmo_rect body;   /* whole rows only */
    int text_px;
    int title_h;
    int row_h;
    int rows;                   /* body rows that fit */
    int pad;
};

/* Centre the frame on `canvas`, offset by the drag the player has applied,
 * and clamp it so the title bar can always be grabbed again. */
void view_ui_frame_place(const struct openmmo_rect *canvas, int id,
                         int drag_x, int drag_y,
                         struct view_ui_frame_layout *out);

enum {
    VIEW_UI_FRAME_HIT_NONE = 0,
    VIEW_UI_FRAME_HIT_TITLE,
    VIEW_UI_FRAME_HIT_CLOSE,
    VIEW_UI_FRAME_HIT_BODY
};

int view_ui_frame_hit(const struct view_ui_frame_layout *L, int x, int y);

/* Which body row the pointer is on, or -1. */
int view_ui_frame_row(const struct view_ui_frame_layout *L, int x, int y);

/* ------------------------------------------------------------------ */
/* Inside the frames: tabs, columns, rows that act, an action row      */
/* ------------------------------------------------------------------ */

/* The official client's own furniture heights, in its 12px pixels: the tab strip is
 * ui-tab.active (26), a column header is ui-misc-btn (20), and the action
 * row is an editfield (16) inside its border. */
#define VIEW_UI_TAB_H    26
#define VIEW_UI_HEADER_H 20
#define VIEW_UI_ACTION_H 26

/* The widest tab strip any frame grows (the GTL's three); the social
 * window's two still lay out from its own label count. */
#define VIEW_UI_WIN_TABS 3

/* What a frame's body is furnished with. Zero-size rects for the parts a
 * window does not have (the instance list has no tabs and no action row). */
struct view_ui_win_body {
    struct openmmo_rect tab[VIEW_UI_WIN_TABS];
    int tab_n;
    struct openmmo_rect header;       /* the column-header band */
    struct openmmo_rect list;         /* whole rows only */
    struct openmmo_rect action;       /* the bottom row */
    struct openmmo_rect action_label; /* its caption */
    struct openmmo_rect action_field; /* its editfield */
    int rows, row_h, text_px, pad;
};

/* The names on a window's furniture, the official client's own strings. */
const char *view_ui_win_tab_label(int id, int tab);          /* NULL = none */
const char *view_ui_win_action_label(int id, int tab);       /* NULL = none */
void view_ui_win_columns(int id, int tab, const char **left,
                         const char **right);

/* Furnish frame `id`'s body for the selected tab. */
void view_ui_win_body_place(const struct view_ui_frame_layout *L, int id,
                            int tab, struct view_ui_win_body *out);

/* Which tab, or -1. */
int view_ui_win_tab_hit(const struct view_ui_win_body *B, int x, int y);
/* Which list row, or -1. */
int view_ui_win_row_hit(const struct view_ui_win_body *B, int x, int y);

/* ------------------------------------------------------------------ */
/* The player menu on a row                                            */
/* ------------------------------------------------------------------ */

/* The official client's player menu (strings 2250..2261), the entries this client's
 * guest action layer serves, the same list openmmo_player.c raises on a
 * tap in the world, in the same order. */
#define VIEW_UI_PLAYER_ROWS 7

struct view_ui_player_item {
    const char *label;      /* ACT_FRIEND's is picked by is_friend */
    int act;                /* OPENMMO_HUD_ACT_*, or -1: the window's own */
    int confirm;            /* ask first, with the official client's sentence */
};

const struct view_ui_player_item *view_ui_player_item(int i);
/* The row's label; ACT_FRIEND reads "Add Friend" or "Remove Friend". */
const char *view_ui_player_label(int i, int is_friend);
/* The official client's confirm sentence (1654) with the name in it. */
void view_ui_player_confirm_text(const char *name, char *dst, size_t cap);

/* Hang the player menu off the pointer, opening down-right and flipped or
 * clamped where the canvas ends. Reuses the popup row metrics, so the
 * result is a view_ui_menu_layout whose rows are the player items. */
void view_ui_player_place(const struct openmmo_rect *canvas, int at_x,
                          int at_y, view_ui_measure measure, void *ctx,
                          int is_friend, struct view_ui_menu_layout *out);

/* ------------------------------------------------------------------ */
/* The confirm box                                                     */
/* ------------------------------------------------------------------ */

struct view_ui_confirm_layout {
    struct openmmo_rect box;
    struct openmmo_rect text;   /* the sentence, wrapped by the caller */
    struct openmmo_rect yes;
    struct openmmo_rect no;
    int text_px;
    int pad;
};

/* Centre the confirm on the canvas: the sentence above two buttons. */
void view_ui_confirm_place(const struct openmmo_rect *canvas, int text_w,
                           struct view_ui_confirm_layout *out);

/* ------------------------------------------------------------------ */
/* The party strip                                                     */
/* ------------------------------------------------------------------ */

struct view_ui_partybar_layout {
    struct openmmo_rect box;
    struct openmmo_rect slot[OPENMMO_HUD_PARTY_N];
    struct openmmo_rect name[OPENMMO_HUD_PARTY_N]; /* the name and the level */
    struct openmmo_rect hp[OPENMMO_HUD_PARTY_N];   /* the numbers under them */
    struct openmmo_rect bar[OPENMMO_HUD_PARTY_N];  /* the HP bar under those */
    int text_px;
    int pad;
    int line_h;
    int slot_h;
    int n;
};

void view_ui_partybar_place(const struct openmmo_rect *canvas, int n,
                            struct view_ui_partybar_layout *out);

/* Which slot, or -1. */
int view_ui_partybar_hit(const struct view_ui_partybar_layout *L, int x, int y);

/* The official client's four HP colours: green above half, orange above a fifth, red
 * below it, and the fainted purple at zero. An unknown maximum draws as a
 * full green bar rather than an empty red one. */
uint32_t view_ui_hp_colour(int hp, int max_hp);

/* How much of `w` the bar fills. */
int view_ui_hp_width(int hp, int max_hp, int w);

/* ------------------------------------------------------------------ */
/* The notice                                                          */
/* ------------------------------------------------------------------ */

struct view_ui_notice_layout {
    struct openmmo_rect box;
    int text_px;
    int pad;
};

/* A one-line notice, centred over the bottom of the canvas the way the official client's
 * own popup message sits over the world. `text_w` is the measured line. */
void view_ui_notice_place(const struct openmmo_rect *canvas, int text_w,
                          struct view_ui_notice_layout *out);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_VIEW_UI_GAME_H */
