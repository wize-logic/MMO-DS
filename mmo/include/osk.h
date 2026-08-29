#ifndef OPENMMO_OSK_H
#define OPENMMO_OSK_H
/* An on-screen keyboard: the client's free-text entry path. */

#include <stddef.h>
#include <stdint.h>

/* Capacity of the text buffer, in UTF-16 code units. Comfortably over the
 * server's 32-code-point VARCHAR name/nickname fields and a chat line. */
#define OSK_MAX_TEXT 64

/* The device window the grid is tiled into: pixel (16, 16) 192x160, the
 * same rectangle POKETCH_SCREEN_MIN/MAX hit-tests. A typed-line strip
 * takes the top 16 px; the 6x13 keys fill the rest. */
#define OSK_ORIGIN_X 16
#define OSK_ORIGIN_Y 16
#define OSK_WIDTH    192
#define OSK_HEIGHT   160

/* Navigation events, mapped from the D-pad by the caller. */
typedef enum {
    OSK_UP,
    OSK_DOWN,
    OSK_LEFT,
    OSK_RIGHT
} osk_dir;

/* What a key does when activated. */
typedef enum {
    OSK_KEY_CHAR,       /* insert unit (a Unicode code point) */
    OSK_KEY_PAGE,       /* switch to unit (0..4) */
    OSK_KEY_BACKSPACE,  /* delete the last code unit */
    OSK_KEY_SPACE,      /* insert U+0020 */
    OSK_KEY_ENTER       /* commit the line */
} osk_keykind;

/* A key as the caller needs it to draw one: its screen rectangle (lower-screen
 * pixels, matching the touch coordinate space), a short label, whether the
 * cursor is on it, and the Unicode / page id it carries. */
typedef struct {
    int         x, y, w, h;
    osk_keykind kind;
    char        label[6];
    int         selected;
    uint16_t    unit;
} osk_key_view;

/* The keyboard. Opaque layout, small enough to live in a caller's BSS. */
typedef struct {
    uint16_t text[OSK_MAX_TEXT];
    size_t   len;          /* code units held */
    size_t   caret;        /* insertion point, 0..len */
    size_t   max;          /* cap; 0 means OSK_MAX_TEXT */
    int      cursor;       /* index into the current page's key table */
    int      page;         /* 0..4: upper, lower, others, JP, numpad */
    int      committed;    /* set by OSK_KEY_ENTER, cleared by osk_reset */
    int      cancelled;    /* set by a host Escape, cleared by osk_reset */
} osk_state;

/* Reset to an empty line, cursor on the first letter, page 0 (upper).
 * Keeps the cap set by osk_set_max so a surface that reopens the field
 * does not have to recap it. A zeroed struct (max == 0) becomes a
 * full-size field. Also the initializer. */
void osk_reset(osk_state *k);

/* Cap the field at `n` code units (clamped to 1..OSK_MAX_TEXT). A surface
 * whose wire is shorter than the widget (a 32-unit name) calls this so the
 * grid and the host keyboard stop at the same place. */
void osk_set_max(osk_state *k, size_t n);

/* Insert `u` at the caret, shifting the tail right. A full field ignores it. */
void osk_insert(osk_state *k, uint16_t u);

/* Apply one host-keyboard event. `kind` is an OPENMMO_TEXT_* value from
 * UNIT / BACKSPACE / DELETE / LEFT / RIGHT / HOME / END /
 * COMMIT / CANCEL, so the page and the pad write the same buffer. An
 * unknown kind is ignored. */
void osk_feed(osk_state *k, unsigned kind, unsigned unit);

/* Move the cursor one cell. Rows wrap left/right and the grid wraps
 * top/bottom; empty rows are skipped so the cursor never lands off a key. */
void osk_move(osk_state *k, osk_dir dir);

/* Activate the key under the cursor: insert a character, switch page,
 * delete, insert a space, or commit, per the key's kind. A full buffer
 * silently ignores an inserting key (the caller can see len == max). */
void osk_activate(osk_state *k);

/* Delete the unit before the caret, if any. The pad's B button maps here so
 * backspace needs no cursor trip to the delete key. */
void osk_backspace(osk_state *k);

/* The caret, 0..len. A host Left/Right/Home/End moves it; a grid insert
 * writes here rather than always appending. */
size_t osk_caret(const osk_state *k);

/* The current page, 0..4. */
int osk_page(const osk_state *k);

/* Nonzero once a host Escape cancelled the field, until the next osk_reset. */
int osk_cancelled(const osk_state *k);

/* A pen tap at a lower-screen pixel. If it lands on a key, the cursor moves
 * there and the key activates; returns 1. A tap on no key returns 0 and does
 * nothing. */
int osk_pen(osk_state *k, int px, int py);

/* Nonzero once ENTER has been activated, until the next osk_reset. */
int osk_committed(const osk_state *k);

/* The current text length, in UTF-16 code units. */
size_t osk_text_len(const osk_state *k);

/* Write the current text as UTF-16LE bytes (two per code unit, no terminator)
 * into dst, at most dst_cap bytes. Returns the number of bytes that a full copy
 * would need (2 * len), so a caller can detect truncation by comparing against
 * dst_cap. This is the exact byte format mmo_utf16le_to_charcode() reads. */
size_t osk_text_utf16le(const osk_state *k, uint8_t *dst, size_t dst_cap);

/* How many keys the current page has, for a draw loop. */
int osk_key_count(const osk_state *k);

/* Fill *out with the i-th key's rectangle, kind, label and selected flag.
 * Returns 1 for a valid index, 0 otherwise. */
int osk_get_key(const osk_state *k, int i, osk_key_view *out);

#endif /* OPENMMO_OSK_H */
