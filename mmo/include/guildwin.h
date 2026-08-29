#ifndef OPENMMO_GUILDWIN_H
#define OPENMMO_GUILDWIN_H
/* MOTD and who is on, at a glance. */

#include <stddef.h>

#define MMO_GUILDWIN_MOTD_ROWS   2
#define MMO_GUILDWIN_ONLINE_ROWS 6
#define MMO_GUILDWIN_APP_ROWS    10
#define MMO_GUILDWIN_APP_COLS    24
#if 1 + MMO_GUILDWIN_MOTD_ROWS + MMO_GUILDWIN_ONLINE_ROWS + 1 \
    != MMO_GUILDWIN_APP_ROWS
#error guild glance is title + motd + who is on + footer
#endif

typedef struct {
    const char *name;
    int         online;
} mmo_guildwin_line;

typedef struct {
    int in_guild;
    const char *name;
    const char *tag;
    const char *motd;
    int log_valid;
    int log_count;
} mmo_guildwin_view;

typedef struct {
    int scroll; /* 0 = first visible online member is the first online */
} mmo_guildwin;

typedef struct {
    char line[MMO_GUILDWIN_APP_ROWS][MMO_GUILDWIN_APP_COLS + 1];
} mmo_guildwin_app;

void mmo_guildwin_reset(mmo_guildwin *w);

/* Title row: "GUILD" when not in one; the name (or "GUILD" if the
 * name is empty) with the tag right-aligned when it is set. The
 * engine font has no brackets, so the tag is the letters alone. */
void mmo_guildwin_format_title(const mmo_guildwin_view *in, char *dst,
                               size_t cap);

/* Wrap the MOTD into two 24-col rows. Characters past that are
 * dropped. An empty message leaves both rows blank. */
void mmo_guildwin_format_motd(const char *motd, int row, char *dst,
                              size_t cap);

/* Format one online row into at most cap-1 characters. Same split
 * as the friends list: name left, "on" at the right. An empty name
 * is "?". */
void mmo_guildwin_format_member(const mmo_guildwin_line *in, char *dst,
                                size_t cap);

/* How many members in the list have the online bit. */
int mmo_guildwin_online_count(const mmo_guildwin_line *list, int count);

/* Scroll the online list toward the start (negative) or the end
 * (positive). Clamped so the window never shows past either end. */
void mmo_guildwin_scroll(mmo_guildwin *w, int delta, int online_count);

/* Ten-row layout: title, two MOTD rows, six online names, footer
 * None / LOG n / the online count. Offline members are not drawn.
 * Engine-free so the suite pins the split. */
void mmo_guildwin_render(const mmo_guildwin *w,
                         const mmo_guildwin_view *view,
                         const mmo_guildwin_line *list, int count,
                         mmo_guildwin_app *out);

#endif /* OPENMMO_GUILDWIN_H */
