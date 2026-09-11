/* The front door's keys, without a window. */

#include "launch_menu.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

struct launch_row {
    const char   *label;
    int           kind;
};

static const struct launch_row rows[MMO_LAUNCH_R_COUNT] = {
    { "ACCOUNT",      MMO_LAUNCH_ROW_TEXT   },
    { "PASSWORD",     MMO_LAUNCH_ROW_SECRET },
    { "CARTRIDGES",   MMO_LAUNCH_ROW_TEXT   },
    { "PACING",       MMO_LAUNCH_ROW_CHOICE },
    { "SCALE",        MMO_LAUNCH_ROW_CHOICE },
    { "VIEWPORT",     MMO_LAUNCH_ROW_CHOICE },
    { "LAYOUT",       MMO_LAUNCH_ROW_CHOICE },
    { "RENDER SCALE", MMO_LAUNCH_ROW_CHOICE },
    { "3D DETAIL",    MMO_LAUNCH_ROW_CHOICE },
    { "FILTER",       MMO_LAUNCH_ROW_CHOICE },
    { "FIT",          MMO_LAUNCH_ROW_CHOICE },
    { "FULLSCREEN",   MMO_LAUNCH_ROW_CHOICE },
    { "SOUND",        MMO_LAUNCH_ROW_CHOICE },
    { "BUTTONS",      MMO_LAUNCH_ROW_CHOICE },
    { "DISCORD",      MMO_LAUNCH_ROW_CHOICE },
    { "PLAY",         MMO_LAUNCH_ROW_ACTION },
    { "PLAY OFFLINE", MMO_LAUNCH_ROW_ACTION },
    { "QUIT",         MMO_LAUNCH_ROW_ACTION },
};

static int wrap(int v, int n, int dir)
{
    v += dir;
    if (v < 0) v = n - 1;
    if (v >= n) v = 0;
    return v;
}

/* The named viewports, in the plan's own spelling. A hand-typed width from the
 * config is shown as itself and steps onto this ring the first time the row is
 * cycled. */
static const char *const viewport_ring[] = {
    "auto", "native", "16:9", "16:10", "4:3"
};
#define VIEWPORT_RING_N ((int)(sizeof viewport_ring / sizeof viewport_ring[0]))

static void viewport_cycle(mmo_launch_settings *s, int dir)
{
    int i, at = -1;

    for (i = 0; i < VIEWPORT_RING_N; i++)
        if (strcmp(s->viewport, viewport_ring[i]) == 0)
            at = i;
    if (s->viewport[0] == '\0')
        at = 0;
    at = (at < 0) ? 0 : wrap(at, VIEWPORT_RING_N, dir);
    snprintf(s->viewport, sizeof s->viewport, "%s", viewport_ring[at]);
}

static char *row_text(mmo_launch_menu *m, int r, size_t *cap)
{
    mmo_launch_settings *s = m->set;

    switch (r) {
    case MMO_LAUNCH_R_USER:      *cap = sizeof s->user;      return s->user;
    case MMO_LAUNCH_R_PASS:      *cap = sizeof s->pass;      return s->pass;
    case MMO_LAUNCH_R_ROM:       *cap = sizeof s->rom;       return s->rom;
    default:                     *cap = 0;                   return NULL;
    }
}

static void row_cycle(mmo_launch_menu *m, int r, int dir)
{
    mmo_launch_settings *s = m->set;

    switch (r) {
    case MMO_LAUNCH_R_PACE:
        s->pace = wrap(s->pace, MMO_PACE_N, dir);
        break;
    case MMO_LAUNCH_R_SCALE:
        s->scale = wrap(s->scale, MMO_LAUNCH_SCALE_MAX + 1, dir);
        break;
    case MMO_LAUNCH_R_VIEWPORT:
        viewport_cycle(s, dir);
        break;
    case MMO_LAUNCH_R_LAYOUT:
        s->layout = wrap(s->layout, MMO_LAYOUT_N, dir);
        break;
    case MMO_LAUNCH_R_RS:
        s->render_scale = 1 + wrap(s->render_scale - 1, MMO_LAUNCH_RS_MAX, dir);
        break;
    case MMO_LAUNCH_R_HD3D:
        s->hd3d = MMO_LAUNCH_HD3D_MIN +
                  wrap(s->hd3d - MMO_LAUNCH_HD3D_MIN, MMO_LAUNCH_HD3D_N, dir);
        break;
    case MMO_LAUNCH_R_FILTER:
        s->filter = wrap(s->filter, MMO_FILTER_N, dir);
        break;
    case MMO_LAUNCH_R_FIT:
        s->fit = wrap(s->fit, MMO_FIT_N, dir);
        break;
    case MMO_LAUNCH_R_BUTTONS:
        s->button_mode = wrap(s->button_mode, MMO_BUTTON_N, dir);
        break;
    case MMO_LAUNCH_R_FULLSCREEN: s->fullscreen = !s->fullscreen; break;
    case MMO_LAUNCH_R_AUDIO:      s->audio = !s->audio; break;
    case MMO_LAUNCH_R_DISCORD:    s->discord = !s->discord; break;
    default: break;
    }
}

void mmo_launch_menu_init(mmo_launch_menu *m, mmo_launch_settings *s)
{
    memset(m, 0, sizeof *m);
    m->set = s;
}

void mmo_launch_menu_say(mmo_launch_menu *m, int bad, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(m->status, sizeof m->status, fmt, ap);
    va_end(ap);
    m->status_bad = bad;
}

int mmo_launch_menu_kind(int row)
{
    if (row < 0 || row >= MMO_LAUNCH_R_COUNT) return -1;
    return rows[row].kind;
}

const char *mmo_launch_menu_label(int row)
{
    if (row < 0 || row >= MMO_LAUNCH_R_COUNT) return NULL;
    return rows[row].label;
}

const char *mmo_launch_menu_value(const mmo_launch_menu *m, int r,
                                  char *scratch, size_t cap)
{
    const mmo_launch_settings *s = m->set;

    switch (r) {
    case MMO_LAUNCH_R_USER:
        return s->user[0] != '\0' ? s->user : "(none)";
    case MMO_LAUNCH_R_PASS:
        if (s->pass[0] == '\0') return "(none)";
        memset(scratch, '*', sizeof "********" - 1);
        scratch[sizeof "********" - 1] = '\0';
        return scratch;
    case MMO_LAUNCH_R_ROM:
        return s->rom[0] != '\0' ? s->rom
                                 : "(none: Platinum, Heart Gold and Black)";
    case MMO_LAUNCH_R_PACE:
        return s->pace == MMO_PACE_UNLIMITED ? "UNLIMITED"
                                             : "CONSOLE RATE (60 FPS)";
    case MMO_LAUNCH_R_SCALE:
        if (s->scale == MMO_LAUNCH_SCALE_AUTO) return "AUTO";
        snprintf(scratch, cap, "%dX", s->scale);
        return scratch;
    case MMO_LAUNCH_R_VIEWPORT:
        return s->viewport[0] != '\0' ? s->viewport : "auto";
    case MMO_LAUNCH_R_LAYOUT:
        return mmo_launch_layout_name(s->layout);
    case MMO_LAUNCH_R_RS:
        snprintf(scratch, cap, "%dX", s->render_scale);
        return scratch;
    case MMO_LAUNCH_R_HD3D:
        /* SD, HD or ULTRA; which multiplier that is stays inside the plan. */
        snprintf(scratch, cap, "%s",
                 s->hd3d >= MMO_LAUNCH_HD3D_MAX ? "ULTRA"
                 : (s->hd3d > MMO_LAUNCH_HD3D_MIN ? "HD" : "SD"));
        return scratch;
    case MMO_LAUNCH_R_FILTER:
        return mmo_launch_filter_name(s->filter);
    case MMO_LAUNCH_R_FIT:
        return mmo_launch_fit_name(s->fit);
    case MMO_LAUNCH_R_BUTTONS:
        return mmo_launch_button_mode_name(s->button_mode);
    case MMO_LAUNCH_R_FULLSCREEN:
        return s->fullscreen ? "ON" : "OFF";
    case MMO_LAUNCH_R_AUDIO:
        return s->audio ? "ON" : "OFF";
    case MMO_LAUNCH_R_DISCORD:
        /* What it says, rather than what it is called: the row is one line to
         * a player and "ON" alone does not say what is being shown. */
        return s->discord ? "WHERE I AM" : "OFF";
    default:
        return "";
    }
}

int mmo_launch_menu_key(mmo_launch_menu *m, int key)
{
    if (m->editing) {
        size_t cap;
        char *dst = row_text(m, m->sel, &cap);

        if (key == MMO_LAUNCH_KEY_BACKSPACE) {
            size_t n = strlen(m->edit);

            if (n > 0) m->edit[n - 1] = '\0';
            return MMO_LAUNCH_MENU_NONE;
        }
        if (key == MMO_LAUNCH_KEY_ENTER) {
            if (dst != NULL)
                snprintf(dst, cap, "%s", m->edit);
            m->editing = 0;
            mmo_launch_menu_say(m, 0, "%s set", rows[m->sel].label);
            return MMO_LAUNCH_MENU_NONE;
        }
        if (key == MMO_LAUNCH_KEY_ESC) {
            m->editing = 0;
            mmo_launch_menu_say(m, 0, "unchanged");
            return MMO_LAUNCH_MENU_NONE;
        }
        return MMO_LAUNCH_MENU_NONE;
    }

    switch (key) {
    case MMO_LAUNCH_KEY_UP:
        m->sel = wrap(m->sel, MMO_LAUNCH_R_COUNT, -1);
        return MMO_LAUNCH_MENU_NONE;
    case MMO_LAUNCH_KEY_DOWN:
        m->sel = wrap(m->sel, MMO_LAUNCH_R_COUNT, +1);
        return MMO_LAUNCH_MENU_NONE;
    case MMO_LAUNCH_KEY_LEFT:
        row_cycle(m, m->sel, -1);
        return MMO_LAUNCH_MENU_NONE;
    case MMO_LAUNCH_KEY_RIGHT:
        row_cycle(m, m->sel, +1);
        return MMO_LAUNCH_MENU_NONE;
    case MMO_LAUNCH_KEY_ESC:
        return MMO_LAUNCH_MENU_QUIT;
    case MMO_LAUNCH_KEY_ENTER:
        if (m->sel == MMO_LAUNCH_R_QUIT)
            return MMO_LAUNCH_MENU_QUIT;
        if (m->sel == MMO_LAUNCH_R_PLAY)
            return MMO_LAUNCH_MENU_PLAY;
        if (m->sel == MMO_LAUNCH_R_PLAY_OFFLINE)
            return MMO_LAUNCH_MENU_PLAY_OFFLINE;
        if (m->sel == MMO_LAUNCH_R_ROM)
            return MMO_LAUNCH_MENU_PICK_ROM;
        if (rows[m->sel].kind == MMO_LAUNCH_ROW_TEXT ||
            rows[m->sel].kind == MMO_LAUNCH_ROW_SECRET) {
            size_t cap;
            const char *cur = row_text(m, m->sel, &cap);

            snprintf(m->edit, sizeof m->edit, "%s", cur != NULL ? cur : "");
            m->editing = 1;
            mmo_launch_menu_say(m, 0, "editing %s", rows[m->sel].label);
            return MMO_LAUNCH_MENU_NONE;
        }
        row_cycle(m, m->sel, +1);
        return MMO_LAUNCH_MENU_NONE;
    default:
        return MMO_LAUNCH_MENU_NONE;
    }
}

void mmo_launch_menu_text(mmo_launch_menu *m, const char *utf8)
{
    size_t n, add, cap;

    if (!m->editing || utf8 == NULL || utf8[0] == '\0')
        return;
    (void)row_text(m, m->sel, &cap);
    n = strlen(m->edit);
    add = strlen(utf8);
    if (n + add < cap && n + add < sizeof m->edit)
        memcpy(m->edit + n, utf8, add + 1);
}

int mmo_launch_menu_line(mmo_launch_menu *m, const char *line)
{
    char buf[MMO_LAUNCH_PATH + 16];
    size_t n;
    const char *p;

    if (line == NULL)
        return -1;
    snprintf(buf, sizeof buf, "%s", line);
    n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = '\0';
    p = buf;
    while (*p == ' ' || *p == '\t')
        p++;
    if (*p == '\0' || *p == '#')
        return MMO_LAUNCH_MENU_NONE;
    if (strcmp(p, "up") == 0)
        return mmo_launch_menu_key(m, MMO_LAUNCH_KEY_UP);
    if (strcmp(p, "down") == 0)
        return mmo_launch_menu_key(m, MMO_LAUNCH_KEY_DOWN);
    if (strcmp(p, "left") == 0)
        return mmo_launch_menu_key(m, MMO_LAUNCH_KEY_LEFT);
    if (strcmp(p, "right") == 0)
        return mmo_launch_menu_key(m, MMO_LAUNCH_KEY_RIGHT);
    if (strcmp(p, "enter") == 0)
        return mmo_launch_menu_key(m, MMO_LAUNCH_KEY_ENTER);
    if (strcmp(p, "esc") == 0)
        return mmo_launch_menu_key(m, MMO_LAUNCH_KEY_ESC);
    if (strcmp(p, "backspace") == 0)
        return mmo_launch_menu_key(m, MMO_LAUNCH_KEY_BACKSPACE);
    if (strncmp(p, "text ", 5) == 0) {
        mmo_launch_menu_text(m, p + 5);
        return MMO_LAUNCH_MENU_NONE;
    }
    if (strcmp(p, "text") == 0)
        return MMO_LAUNCH_MENU_NONE;
    return -1;
}
