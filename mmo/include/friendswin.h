#ifndef OPENMMO_FRIENDSWIN_H
#define OPENMMO_FRIENDSWIN_H
/* Eight rows of name and online state. */

#include <stddef.h>

#define MMO_FRIENDSWIN_ROWS     8
#define MMO_FRIENDSWIN_APP_ROWS 10
#define MMO_FRIENDSWIN_APP_COLS 24
#if MMO_FRIENDSWIN_ROWS + 2 != MMO_FRIENDSWIN_APP_ROWS
#error friends app layout is title + eight rows + footer
#endif

typedef struct {
    const char *name;
    int         online;
} mmo_friendswin_line;

typedef struct {
    int scroll; /* 0 = first visible is entry 0 */
} mmo_friendswin;

typedef struct {
    char line[MMO_FRIENDSWIN_APP_ROWS][MMO_FRIENDSWIN_APP_COLS + 1];
} mmo_friendswin_app;

void mmo_friendswin_reset(mmo_friendswin *w);

/* Format one row into at most cap-1 characters. A name with the
 * online bit is left-aligned with "on" at the right; offline is the
 * name alone. An empty name is "?". */
void mmo_friendswin_format(const mmo_friendswin_line *in, char *dst,
                           size_t cap);

/* Scroll toward the start of the list (negative) or the end
 * (positive). Clamped so the window never shows past either end. */
void mmo_friendswin_scroll(mmo_friendswin *w, int delta, int count);

/* Ten-row layout: title friends, eight body rows, footer none or the
 * count. Engine-free so the suite pins the split. */
void mmo_friendswin_render(const mmo_friendswin *w,
                           const mmo_friendswin_line *list, int count,
                           mmo_friendswin_app *out);

#endif /* OPENMMO_FRIENDSWIN_H */
