#ifndef OPENMMO_WIDGET_H
#define OPENMMO_WIDGET_H
/* One description four engine widgets can draw. */

#include "client.h"
#include "game.h"
#include "mmo.h"

/* A row's label is what a person reads. Several of these packets carry
 * no text at all, so a compiler writes the wire's own numbers there
 * rather than inventing a name for them. */
#define MMO_WIDGET_LABEL_MAX 40
#define MMO_WIDGET_TEXT_MAX  120
#define MMO_WIDGET_NAME_MAX  24
#define MMO_WIDGET_ROWS_MAX  MMO_UI_ROW_MAX
/* A screen is a line of text and one thing to choose from, the pairing
 * the engine's own field script already draws. */
#define MMO_SCREEN_PARTS_MAX 2

/* Above this many rows a fixed grid stops fitting the screen and the
 * list with its scroll model is the widget. The engine draws its own
 * short choices the same way. */
#define MMO_WIDGET_GRID_MAX 8

/* The tile rows a list shows at once before it scrolls. The engine's
 * own join list is 20x10 tiles at two tiles a row. */
#define MMO_WIDGET_LIST_VISIBLE 5

typedef enum {
    MMO_WIDGET_NONE = 0,
    MMO_WIDGET_MESSAGE, /* the field message box */
    MMO_WIDGET_YESNO,   /* the two-choice menu */
    MMO_WIDGET_GRID,    /* a fixed grid of choices, cursor, no scroll */
    MMO_WIDGET_LIST,    /* a list that scrolls when count exceeds visible */
    MMO_WIDGET_FIELD    /* a text field; described here, typed elsewhere */
} mmo_widget_kind;

/* Tiles on the layer the renderer draws to, the units every engine
 * window template is in. */
typedef struct {
    int left;
    int top;
    int width;
    int height;
} mmo_widget_rect;

typedef struct {
    char label[MMO_WIDGET_LABEL_MAX];
    /* The number the wire identifies this row by where the packet names
     * one, and the row's own position where it does not. Which of the
     * two it is is the compiler's business; a pick reports it verbatim. */
    s64 value;
    int selectable;
} mmo_widget_row;

typedef struct {
    mmo_widget_kind kind;
    mmo_widget_rect rect;
    int focus;        /* 1 if this part is the one taking input */
    int framed;       /* 1 to draw the standard frame around it */
    int cancelable;   /* 1 if B closes the screen */
    int loop;         /* 1 if the cursor wraps at the ends */
    int cols;         /* grid columns; a list is always 1 */
    int visible_rows; /* rows on screen before scrolling; 0 means all */
    char text[MMO_WIDGET_TEXT_MAX];
    int row_count;
    mmo_widget_row row[MMO_WIDGET_ROWS_MAX];
} mmo_widget;

typedef struct {
    int valid;
    int opcode; /* the s2c opcode the description was compiled from */
    char name[MMO_WIDGET_NAME_MAX];
    /* The c2s opcode a pick is answered on, and zero where nothing has
     * been measured as the answer. A renderer reports a pick either
     * way; it never invents a reply. */
    int reply_op;
    int timeout_s; /* 0 where the screen does not expire */
    int part_count;
    mmo_widget part[MMO_SCREEN_PARTS_MAX];
} mmo_screen;

void mmo_screen_clear(mmo_screen *s);

/* Compile the packet the UI store last held. Returns 1 when there is
 * something to draw, 0 when the store is empty or the last packet
 * describes no screen (the join's empty pages and its scale byte both
 * land here). */
int mmo_screen_from_ui(const openmmo_ui *ui, mmo_screen *out);

/* The five shapes the store holds, each on its own so a caller can draw
 * one without waiting for it to be the newest. Each clears the screen
 * before it writes, so a compiler that returns 0 leaves an empty one
 * rather than the caller's last. */
int mmo_screen_from_name_choices(const mmo_name_choices *n, mmo_screen *out);
int mmo_screen_from_option_list(const mmo_option_list *o, mmo_screen *out);
int mmo_screen_from_list_window(const mmo_list_window *l, mmo_screen *out);
int mmo_screen_from_confirm(const mmo_confirm_prompt *c, mmo_screen *out);
int mmo_screen_from_menu_prompt(const mmo_menu_prompt *p, mmo_screen *out);

/* The index of the part that takes input, or -1 when the screen is
 * text only. */
int mmo_screen_focus(const mmo_screen *s);

/* The rows a list shows at once, and the first row on screen for a
 * cursor at `cursor`. Both clamp; a grid never scrolls. */
int mmo_widget_visible(const mmo_widget *w);
int mmo_widget_first_visible(const mmo_widget *w, int cursor);

/* The value a cursor position reports. Returns 0 and leaves *out alone
 * when the position is not a row of this widget. */
int mmo_widget_pick(const mmo_widget *w, int cursor, s64 *out);

const char *mmo_widget_kind_name(mmo_widget_kind k);

#endif /* OPENMMO_WIDGET_H */
