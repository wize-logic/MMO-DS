#ifndef OPENMMO_CHATWIN_H
#define OPENMMO_CHATWIN_H
/* The chat window the engine never had. */

#include "entry.h"
#include "game.h"

#include <stddef.h>
#include <stdint.h>

#define MMO_CHATWIN_LINE     80
#define MMO_CHATWIN_LOG_ROWS 8
#define MMO_CHATWIN_COMPOSE_ROWS 3

typedef enum {
    MMO_CHATWIN_HIDDEN = 0,
    MMO_CHATWIN_LOG,
    MMO_CHATWIN_COMPOSE
} mmo_chatwin_mode;

typedef struct {
    char     text[MMO_CHATWIN_LINE];
    int      type; /* MMO_CHAT_* */
} mmo_chatwin_row;

typedef struct {
    int      type;
    const char *sender;
    const char *text;
} mmo_chatwin_line;

typedef struct {
    mmo_chatwin_mode mode;
    int              scroll; /* 0 = newest at the bottom */
} mmo_chatwin;

void mmo_chatwin_reset(mmo_chatwin *w);

/* Open the log. Hidden stays hidden until this is called, the window
 * is a joined-session surface, not a title one. */
void mmo_chatwin_show(mmo_chatwin *w);
void mmo_chatwin_hide(mmo_chatwin *w);

int mmo_chatwin_visible(const mmo_chatwin *w);
int mmo_chatwin_composing(const mmo_chatwin *w);

/* The compose step is the one that types. mmo_entry_path_for(MMO_ENTRY_CHAT)
 * says how: it is the field, so the caller drives osk.c rather than
 * launching the naming-screen overlay. */
int mmo_chatwin_needs_entry(const mmo_chatwin *w);

/* START (and a tap on the input strip) flip LOG <-> COMPOSE. Hidden
 * is left alone. Returns 1 when the mode changed. */
int mmo_chatwin_toggle_compose(mmo_chatwin *w);

/* How many rows the current mode shows. */
int mmo_chatwin_row_budget(const mmo_chatwin *w);

/* Format one delivered line. A system / notice / empty sender is just
 * the text; everything else is "sender: text". */
void mmo_chatwin_format(const mmo_chatwin_line *in, char *dst, size_t cap);

/* A colour per wire type, 0x00RRGGBB. Unknown types share one grey. */
uint32_t mmo_chatwin_color(int type);

/* Visible rows, oldest at index 0, newest at the bottom. `log` is
 * oldest-first, the same order openmmo_client_chat writes. */
int mmo_chatwin_visible_count(const mmo_chatwin *w, int log_count);
int mmo_chatwin_get_row(const mmo_chatwin *w, const mmo_chatwin_line *log,
                        int log_count, int i, mmo_chatwin_row *out);

/* Scroll older (positive) or newer (negative). Clamped so the window
 * never shows past either end of the log. */
void mmo_chatwin_scroll(mmo_chatwin *w, int delta, int log_count);

/* Ten-row layout the walking log paints inside the Poketch: a title,
 * eight body rows (the log budget), a footer. Engine-free so the suite
 * pins the split. type -1 is chrome; a body row carries MMO_CHAT_*. */
#define MMO_CHATWIN_APP_ROWS 10
#define MMO_CHATWIN_APP_COLS 24
#if MMO_CHATWIN_LOG_ROWS + 2 != MMO_CHATWIN_APP_ROWS
#error chat app layout is title + log rows + footer
#endif

typedef struct {
    char line[MMO_CHATWIN_APP_ROWS][MMO_CHATWIN_APP_COLS + 1];
    int  type[MMO_CHATWIN_APP_ROWS];
} mmo_chatwin_app;

void mmo_chatwin_render_app(const mmo_chatwin *w, const mmo_chatwin_line *log,
                            int log_count, mmo_chatwin_app *out,
                            const osk_state *osk);

#endif /* OPENMMO_CHATWIN_H */
