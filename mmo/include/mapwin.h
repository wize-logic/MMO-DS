#ifndef OPENMMO_MAPWIN_H
#define OPENMMO_MAPWIN_H
/* Who is on this map, as names. */

#include <stddef.h>

#define MMO_MAPWIN_ROWS     8
#define MMO_MAPWIN_APP_ROWS 10
#define MMO_MAPWIN_APP_COLS 24
#if MMO_MAPWIN_ROWS + 2 != MMO_MAPWIN_APP_ROWS
#error map app layout is title + eight rows + footer
#endif

typedef struct {
    const char *name;
} mmo_mapwin_line;

typedef struct {
    int scroll; /* 0 = first visible is entry 0 */
} mmo_mapwin;

typedef struct {
    char line[MMO_MAPWIN_APP_ROWS][MMO_MAPWIN_APP_COLS + 1];
} mmo_mapwin_app;

void mmo_mapwin_reset(mmo_mapwin *w);

/* Format one row into at most cap-1 characters. The name alone; an
 * empty name is "?". */
void mmo_mapwin_format(const mmo_mapwin_line *in, char *dst, size_t cap);

/* Scroll toward the start of the list (negative) or the end
 * (positive). Clamped so the window never shows past either end. */
void mmo_mapwin_scroll(mmo_mapwin *w, int delta, int count);

/* Ten-row layout: title here, eight body rows, footer none or the
 * count. Engine-free so the suite pins the split. */
void mmo_mapwin_render(const mmo_mapwin *w, const mmo_mapwin_line *list,
                       int count, mmo_mapwin_app *out);

#endif /* OPENMMO_MAPWIN_H */
