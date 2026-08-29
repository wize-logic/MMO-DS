#ifndef OPENMMO_PARTYWIN_H
#define OPENMMO_PARTYWIN_H
/* The party, as six rows of name and status. */

#include <stddef.h>

#define MMO_PARTYWIN_ROWS     8
#define MMO_PARTYWIN_APP_ROWS 10
#define MMO_PARTYWIN_APP_COLS 24
#if MMO_PARTYWIN_ROWS + 2 != MMO_PARTYWIN_APP_ROWS
#error party app layout is title + eight rows + footer
#endif

typedef struct {
    const char *name;
    int         egg;
    int         level;
    int         hp;
} mmo_partywin_line;

typedef struct {
    char line[MMO_PARTYWIN_APP_ROWS][MMO_PARTYWIN_APP_COLS + 1];
} mmo_partywin_app;

/* Format one row into at most cap-1 characters. A hatched member is
 * the name left, "LvN H" right. An egg is the name alone. An empty
 * name is "?". */
void mmo_partywin_format(const mmo_partywin_line *in, char *dst,
                         size_t cap);

/* Ten-row layout: title party, eight body rows, footer none or the
 * count. Engine-free so the suite pins the split. */
void mmo_partywin_render(const mmo_partywin_line *list, int count,
                         mmo_partywin_app *out);

#endif /* OPENMMO_PARTYWIN_H */
