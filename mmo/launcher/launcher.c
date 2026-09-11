/* The front door. */

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "platform.h"

#include "feed.h"
#include "feed_pin.h"
#include "launch_gui.h"
#include "launch_menu.h"
#include "soundcompose.h"
#include "followcompose.h"
#include "lookcompose.h"
#include "speciescompose.h"
#include "launch_plan.h"
#include "update.h"
#include "status_channel.h"
#include "text_channel.h"
#include "view_font.h"

#define LAUNCH_NAME "OpenMMO"
#define UI_W 384
#define UI_H 400

/* ------------------------------------------------------------------ */
/* The menu, plus the last session's sentence                          */
/* ------------------------------------------------------------------ */

struct ui {
    mmo_launch_settings set;
    mmo_launch_menu menu;
    /* How the last session's connection ended, kept apart from `status` so it
     * survives the next "server set" and is still on screen when the player
     * comes back to try again. Empty before any session has run. */
    char     conn[192];
    uint32_t conn_colour;
    /* The one thing the front door asks about a save, and the answer is only
     * ever this session's: it is not written to the settings file, so the box
     * comes up clear every time and a save is never carried online twice by a
     * preference nobody remembers setting. */
    int      take_save_online;
};

/*
 * Everything this window has ever told a player is one line at the bottom of it, replaced by
 * the next one and gone when the window closes, which is exactly the shape of "it said
 * something about the server and then it shut".
 */
static const char *launch_log_path(void);

static void say_logged(const char *line, int bad)
{
    static FILE *f;
    static int tried;

    if (!tried) {
        const char *path = launch_log_path();

        tried = 1;
        if (path != NULL) {
            char when[32];

            f = fopen(path, "ab");
            if (f != NULL) {
                setvbuf(f, NULL, _IOLBF, 0);
                mmo_plat_stamp(when, sizeof when);
                fprintf(f, "\n==== launcher %s ====\n", when);
            }
        }
    }
    if (f != NULL)
        fprintf(f, "%s%s\n", bad ? "! " : "  ", line);
}

static void say(struct ui *u, int bad, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(u->menu.status, sizeof u->menu.status, fmt, ap);
    va_end(ap);
    u->menu.status_bad = bad;
    say_logged(u->menu.status, bad);
}

static int feed_gate(const mmo_launch_settings *s, const char *argv0,
                     const char *port_exe, char *msg, size_t cap);
static int update_gate(const mmo_launch_settings *s, const char *argv0,
                       void (*note)(void *ud, const char *line),
                       void (*tick)(void *ud, const char *line), void *ud,
                       char *msg, size_t cap);

/*
 * One download's progress line, said where the caller lives: the status line of the menu, or
 * stdout for the paths a person is not watching.
 */
static void note_say(void *ud, const char *line)
{
    say(ud, 0, "%s", line);
    launch_gui_progress(line);
}

/* The other half of note_say: a line the player is meant to notice rather
 * than watch go by. */
static void note_warn(void *ud, const char *line)
{
    say(ud, 1, "%s", line);
}

/* And the same in the ordinary colour, for a caller that reports what it did
 * rather than how far it has got: no frame is pumped, because nothing behind
 * this one is blocking the window. */
static void note_good(void *ud, const char *line)
{
    say(ud, 0, "%s", line);
}

static void note_print(void *ud, const char *line)
{
    (void)ud;
    printf("openmmo-launch: %s\n", line);
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

#define COL_BG      0x101418
#define COL_LABEL   0x8892A0
#define COL_VALUE   0xE8ECF0
#define COL_ACCENT  0x50A0E0
#define COL_BAD     0xE05050
#define COL_OK      0x60C080

static void fill(uint32_t *px, int x, int y, int w, int h, uint32_t c)
{
    int iy, ix;

    for (iy = y; iy < y + h; iy++) {
        if (iy < 0 || iy >= UI_H) continue;
        for (ix = x; ix < x + w; ix++) {
            if (ix < 0 || ix >= UI_W) continue;
            px[(size_t)iy * UI_W + ix] = c;
        }
    }
}

/*
 * An over-long value shown by its tail, because the end of a path is the part that says which
 * file it is, and marked as cut, so a truncated path is never mistaken for a short one
 * somewhere odd.
 */
static const char *fit_text(char *buf, size_t cap, const char *s, int budget)
{
    int len = (int)strlen(s);

    if (len <= budget)
        return s;
    snprintf(buf, cap, "...%s", s + (len - budget) + 3);
    return buf;
}

#define ROW_TOP  46
#define ROW_STEP 13
/* How many glyphs fit between the value column and the right edge, at scale 1
 * with the font's one-pixel gap: (384 - 132 - 12) / 6. */
#define VALUE_CHARS ((UI_W - 132 - 12) / (OPENMMO_FONT_W + 1))
/* And how many fit across the whole panel, for the lines under the rule that
 * start at the left margin rather than in the value column. */
#define LINE_CHARS  ((UI_W - 24) / (OPENMMO_FONT_W + 1))

static void ui_draw(uint32_t *px, const struct ui *u)
{
    char scratch[MMO_LAUNCH_PATH + 8];   /* the longest field, plus a cursor */
    char shown[VALUE_CHARS + 8];         /* what is left of it after fitting */
    char wide[LINE_CHARS + 8];           /* a line that runs the whole width */
    int i, y;

    for (i = 0; i < UI_W * UI_H; i++)
        px[i] = COL_BG;

    openmmo_font_draw(px, UI_W, UI_H, 12, 12, LAUNCH_NAME, 3, COL_VALUE);
    fill(px, 12, 36, UI_W - 24, 1, COL_ACCENT);

    for (i = 0; i < MMO_LAUNCH_R_COUNT; i++) {
        int selected = (i == u->menu.sel);
        int kind = mmo_launch_menu_kind(i);
        const char *label = mmo_launch_menu_label(i);
        const char *value;
        uint32_t vc = selected ? COL_ACCENT : COL_VALUE;

        y = ROW_TOP + i * ROW_STEP;
        if (selected)
            fill(px, 8, y - 3, UI_W - 16, ROW_STEP - 1, 0x1C2430);

        if (kind == MMO_LAUNCH_ROW_ACTION) {
            openmmo_font_draw(px, UI_W, UI_H, 14, y, label, 1,
                              selected ? COL_ACCENT : COL_LABEL);
            continue;
        }
        openmmo_font_draw(px, UI_W, UI_H, 14, y, label, 1, COL_LABEL);

        if (selected && u->menu.editing) {
            /* The cursor is part of the string so it is drawn by the same
             * glyphs and cannot drift from the text it follows. */
            if (kind == MMO_LAUNCH_ROW_SECRET) {
                size_t n = strlen(u->menu.edit);

                if (n > VALUE_CHARS - 1) n = VALUE_CHARS - 1;
                memset(shown, '*', n);
                shown[n] = '_';
                shown[n + 1] = '\0';
            } else {
                const char *t = fit_text(scratch, sizeof scratch, u->menu.edit,
                                         VALUE_CHARS - 1);
                size_t n = strlen(t);

                if (n > sizeof shown - 2) n = sizeof shown - 2;
                memcpy(shown, t, n);
                shown[n] = '_';
                shown[n + 1] = '\0';
            }
            value = shown;
            vc = COL_OK;
        } else {
            value = mmo_launch_menu_value(&u->menu, i, scratch, sizeof scratch);
            if (value == NULL) value = "?";
            value = fit_text(shown, sizeof shown, value, VALUE_CHARS);
        }
        openmmo_font_draw(px, UI_W, UI_H, 132, y, value, 1, vc);
    }

    y = ROW_TOP + MMO_LAUNCH_R_COUNT * ROW_STEP + 8;
    fill(px, 12, y, UI_W - 24, 1, 0x283038);
    if (u->conn[0] != '\0') {
        openmmo_font_draw(px, UI_W, UI_H, 12, y + 8,
                          fit_text(wide, sizeof wide, u->conn, LINE_CHARS), 1,
                          u->conn_colour);
        y += 10;
    }
    openmmo_font_draw(px, UI_W, UI_H, 12, y + 8,
                      fit_text(wide, sizeof wide, u->menu.status, LINE_CHARS), 1,
                      u->menu.status_bad ? COL_BAD : COL_LABEL);
    openmmo_font_draw(px, UI_W, UI_H, 12, UI_H - 14,
                      u->menu.editing ? "ENTER SAVES  ESC CANCELS"
                                      : "ARROWS MOVE  ENTER OPENS  ESC QUITS", 1,
                      0x606A78);
}

/* ------------------------------------------------------------------ */

/* The menu as a binary PPM, which is how a run with no display reads this
 * program's output. Mirrors the window's --shot. */
static int ui_write_shot(const struct ui *u, const char *path)
{
    static uint32_t px[UI_W * UI_H];
    unsigned char *rgb;
    FILE *f;
    int i, ok;

    ui_draw(px, u);
    rgb = malloc((size_t)UI_W * UI_H * 3);
    if (rgb == NULL) return -1;
    for (i = 0; i < UI_W * UI_H; i++) {
        rgb[i * 3 + 0] = (unsigned char)(px[i] >> 16);
        rgb[i * 3 + 1] = (unsigned char)(px[i] >> 8);
        rgb[i * 3 + 2] = (unsigned char)px[i];
    }
    f = fopen(path, "wb");
    if (f == NULL) { free(rgb); return -1; }
    fprintf(f, "P6\n%d %d\n255\n", UI_W, UI_H);
    ok = fwrite(rgb, 1, (size_t)UI_W * UI_H * 3, f) == (size_t)UI_W * UI_H * 3;
    free(rgb);
    return (fclose(f) == 0 && ok) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Starting the game                                                   */
/* ------------------------------------------------------------------ */

/*
 * The session's own environment with the plan's variables laid over it, rather than a composed
 * one: the port wants the session's HOME for its save directory and the loader's paths, and a
 * plan that replaced the environment wholesale would be a second thing to get right for no
 * gain.
 */
#define GAME_LOG_NAME   "openmmo-client.log"
#define VIEW_LOG_NAME   "openmmo-view.out.log"
#define LAUNCH_LOG_NAME "openmmo-launch.log"

static const char *log_named(char *path, size_t cap, const char *name)
{
    if (path[0] != '\0')
        return path;
    if (mmo_plat_log_path(name, path, cap) != 0)
        return NULL;
    return path;
}

static const char *game_log_path(void)
{
    static char path[MMO_LAUNCH_PATH];

    return log_named(path, sizeof path, GAME_LOG_NAME);
}

/*
 * The window's own prints. It has no terminal either, and on Windows its stderr went to a
 * handle nobody owned, which is where a theme that failed to load, a page this build is too
 * old to read, and every gamepad complaint were going.
 */
static const char *view_log_path(void)
{
    static char path[MMO_LAUNCH_PATH];

    return log_named(path, sizeof path, VIEW_LOG_NAME);
}

/* This program's own, whose first line say_logged writes (above). */
static const char *launch_log_path(void)
{
    static char path[MMO_LAUNCH_PATH];

    return log_named(path, sizeof path, LAUNCH_LOG_NAME);
}

/* A trace that is appended to for the life of an install is a trace nobody can send. */
#define LOG_ROTATE_AT (16L * 1024 * 1024)

static void log_rotate(const char *path)
{
    char old[MMO_LAUNCH_PATH + 4];
    struct stat st;

    if (path == NULL || stat(path, &st) != 0 || (long)st.st_size < LOG_ROTATE_AT)
        return;
    snprintf(old, sizeof old, "%s.1", path);
    remove(old);
    rename(path, old);
}

/*
 * Two pages, not one: the frames ride `chan` and the characters typed into the game ride
 * `chan.text` beside it (text_channel.h). Removing only the first leaves the second behind for
 * every session ever played.
 */
static void channel_remove(const char *chan)
{
    char name[MMO_LAUNCH_NAME + 24];

    mmo_shm_unlink(chan);
    snprintf(name, sizeof name, "%s%s", chan, OPENMMO_TEXT_SUFFIX);
    mmo_shm_unlink(name);
    snprintf(name, sizeof name, "%s%s", chan, OPENMMO_STATUS_SUFFIX);
    mmo_shm_unlink(name);
}

/* ------------------------------------------------------------------ */
/* What the session says about itself                                  */
/* ------------------------------------------------------------------ */

/*
 * The game publishes where its session stands on a page beside the frame one
 * (status_channel.h).
 */
struct sess_watch {
    const struct openmmo_status_shm *page;
    mmo_shm  mem;
    uint32_t gen;          /* the last transition this has already reported */
    int      any;          /* a state has been read at least once */
    uint32_t state, flags;
    char     reason[OPENMMO_STATUS_REASON];
};

static void watch_open(struct sess_watch *w, const char *chan)
{
    char name[MMO_LAUNCH_NAME + 24];

    if (w->page != NULL) return;
    snprintf(name, sizeof name, "%s%s", chan, OPENMMO_STATUS_SUFFIX);
    if (mmo_shm_attach(&w->mem, name, sizeof *w->page, 0) != 0)
        return;                       /* not published yet, or not at all */
    /* The game zeroes the page and writes the magic last, so an unfinished one
     * is left unmapped and tried again next time round the loop. */
    if (((const struct openmmo_status_shm *)w->mem.addr)->magic
        != OPENMMO_STATUS_MAGIC) {
        mmo_shm_close(&w->mem);
        return;
    }
    w->page = (const struct openmmo_status_shm *)w->mem.addr;
}

static void watch_close(struct sess_watch *w)
{
    mmo_shm_close(&w->mem);
    w->page = NULL;
}

/* One poll. Returns nonzero when the state moved, so the caller reports a
 * transition and not a frame. */
static int watch_poll(struct sess_watch *w)
{
    uint32_t state = 0, flags = 0, gen = 0;
    char reason[OPENMMO_STATUS_REASON];

    if (w->page == NULL) return 0;
    if (!openmmo_status_read(w->page, &state, &flags, &gen, reason)) return 0;
    if (w->any && gen == w->gen) return 0;
    w->any = 1;
    w->gen = gen;
    w->state = state;
    w->flags = flags;
    snprintf(w->reason, sizeof w->reason, "%s", reason);
    return 1;
}

/* The one sentence a player is shown, wherever it is shown: the caption, and
 * the server's own words after it when there are any. */
static void watch_sentence(const struct sess_watch *w, char *out, size_t cap)
{
    const char *cap_text = openmmo_status_caption(w->state, w->flags);

    if (w->reason[0] != '\0')
        snprintf(out, cap, "%s: %s", cap_text, w->reason);
    else
        snprintf(out, cap, "%s", cap_text);
}

/* And the way OUT, said where the refusal is. */
static void say_offline_is_there(struct ui *u, const struct sess_watch *w)
{
    if (w->state != OPENMMO_ST_FAILED)
        return;
    if (w->flags & OPENMMO_STATUS_F_WAS_LIVE)
        return;
    say(u, 0, "PLAY OFFLINE needs no server, the save under save/ is the "
              "game, and it is on this machine");
}

/* If the page says the session is over, copy the sentence onto the menu.
 * Used both when we notice the transition ourselves and when a child exits
 * after publishing it, the second is how a drop is not reported as
 * "the game ended". */
static int watch_take_over(struct sess_watch *w, const char *chan,
                           struct ui *u, char *sentence, size_t cap)
{
    watch_open(w, chan);
    (void)watch_poll(w);
    if (!w->any || !openmmo_status_over(w->state, w->flags))
        return 0;
    watch_sentence(w, sentence, cap);
    u->conn_colour = openmmo_status_colour(w->state);
    snprintf(u->conn, sizeof u->conn, "%s", sentence);
    fprintf(stderr, "openmmo-launch: %s\n", sentence);
    say(u, 1, "%s", sentence);
    say_offline_is_there(u, w);
    return 1;
}

/*
 * The session's two children, for the one case that cannot go through play()'s own return: a
 * launcher killed while a game is running. Without this the port is orphaned, holding a page
 * nobody will ever present.
 */
static mmo_proc *volatile live_port, *volatile live_view;
static char live_chan[MMO_LAUNCH_NAME];
static volatile sig_atomic_t stop_sig;

/*
 * Stop the session's children and let the launcher walk out through its own teardown, rather
 * than leaving from here.
 */
static void reap_on_signal(int sig)
{
    if (live_view != NULL) mmo_proc_stop(live_view);
    if (live_port != NULL) mmo_proc_stop(live_port);
    if (stop_sig != 0 || (live_port == NULL && live_view == NULL)) {
        if (live_chan[0] != '\0') channel_remove(live_chan);
        _exit(128 + sig);
    }
    stop_sig = sig;
}

/* Leave with the status the signal asked for, once whatever the caller had
 * left to write down has been written down. Does nothing until one arrives. */
static void leave_if_signalled(void)
{
    if (stop_sig != 0)
        _exit(128 + (int)stop_sig);
}

/*
 * Run the plan: the port first, then wait for it to publish a page, then the window. Returns 0
 * when the session ended normally.
 */
static int play(const mmo_launch_plan *p, struct ui *u)
{
    struct sess_watch watch;
    char sentence[192];
    mmo_proc *port, *view;
    int status = 0, i, rc = 0, dropped = 0;

    memset(&watch, 0, sizeof watch);
    watch.mem.fd = -1;

    port = mmo_proc_spawn_guest(p->port_argv, p->env, game_log_path());
    if (port == NULL) {
        say(u, 1, "could not start the game: %s", mmo_plat_error());
        return -1;
    }
    live_port = port;
    snprintf(live_chan, sizeof live_chan, "%s", p->chan);
    signal(SIGINT, reap_on_signal);
    signal(SIGTERM, reap_on_signal);

    /* Ten seconds: a cold ROM mmap and the port's own boot, measured at well
     * under a second here, with room for a machine that is not this one. */
    for (i = 0; i < 100; i++) {
        if (mmo_shm_exists(p->chan))
            break;
        if (mmo_proc_exited(port, &status)) {
            live_port = NULL;
            say(u, 1, "the game exited before it drew anything (status %d)",
                status);
            mmo_proc_free(port);
            return -1;
        }
        mmo_plat_sleep_us(100000);
    }
    if (!mmo_shm_exists(p->chan)) {
        say(u, 1, "the game never published a picture, giving up");
        mmo_proc_stop(port);
        mmo_proc_wait(port, NULL);
        live_port = NULL;
        mmo_proc_free(port);
        return -1;
    }

    view = mmo_proc_spawn(p->view_argv, NULL, view_log_path());
    if (view == NULL) {
        say(u, 1, "could not open the window: %s", mmo_plat_error());
        mmo_proc_stop(port);
        mmo_proc_wait(port, NULL);
        live_port = NULL;
        mmo_proc_free(port);
        channel_remove(p->chan);
        return -1;
    }
    live_view = view;
    launch_gui_hide_for_session();

    /*
     * Either half ending ends the session, and which one it was is the difference between "you
     * closed the window" and "the game fell over".
     */
    u->conn[0] = '\0';
    for (;;) {
        /*
         * Where the session stands, before either child is asked whether it is still there: a
         * server that hung up is reported as a server that hung up, not as "the game ended",
         * and the player is put back in front of this menu with the reason instead of left
         * holding a window drawing a world nobody is connected to.
         */
        watch_open(&watch, p->chan);
        if (watch_poll(&watch)) {
            watch_sentence(&watch, sentence, sizeof sentence);
            u->conn_colour = openmmo_status_colour(watch.state);
            snprintf(u->conn, sizeof u->conn, "%s", sentence);
            fprintf(stderr, "openmmo-launch: %s\n", sentence);
            if (openmmo_status_over(watch.state, watch.flags)) {
                dropped = 1;
                say(u, 1, "%s", sentence);
                say_offline_is_there(u, &watch);
                break;
            }
        }

        if (mmo_proc_exited(view, &status)) {
            live_view = NULL;
            if (watch_take_over(&watch, p->chan, u, sentence, sizeof sentence))
                dropped = 1;
            else
                say(u, 0, "the session ended");
            break;
        }
        if (mmo_proc_exited(port, &status)) {
            live_port = NULL;
            if (watch_take_over(&watch, p->chan, u, sentence, sizeof sentence)) {
                dropped = 1;
            } else if (status == 0) {
                say(u, 0, "the game ended");
            } else {
                say(u, 1, "the game stopped unexpectedly (status %d)", status);
                rc = -1;
            }
            break;
        }
        mmo_plat_sleep_us(50000);
    }

    /* Asked to stop rather than killed outright where the host has a way to
     * ask, so whichever half is still up runs its own shutdown. Only these two
     * children, and only the page this session named. */
    watch_close(&watch);
    if (live_view != NULL) { mmo_proc_stop(view); mmo_proc_wait(view, NULL); live_view = NULL; }
    if (live_port != NULL) { mmo_proc_stop(port); mmo_proc_wait(port, NULL); live_port = NULL; }
    mmo_proc_free(view);
    mmo_proc_free(port);
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    channel_remove(p->chan);
    live_chan[0] = '\0';
    /* A session the server ended is a failure of the session, whatever exit
     * status the two programs we then took down happened to produce. */
    return dropped ? -1 : rc;
}

/*
 * Save, check the feed, build the plan, run it. The window is hidden for the session when
 * there is one; --script presses Play with no window of our own.
 */
static int start_play(struct ui *u, const char *cfg_path, const char *argv0,
                      const char *port_exe, const char *view_exe)
{
    mmo_launch_plan plan;
    mmo_launch_export exp;
    char root[MMO_LAUNCH_PATH];
    char stamp[MMO_LAUNCH_STAMP];
    char err[192];
    char feed_msg[MMO_FEED_MESSAGE];
    int rc, adopted, aside;

    if (mmo_launch_save(cfg_path, &u->set) != 0)
        say(u, 1, "settings not saved to %s", cfg_path);
    /* Fetch, then gate: a failed fetch is said and played through, because
     * the gate below still holds whatever is on disk to the local feed. */
    if (update_gate(&u->set, argv0, note_say, note_say, u, feed_msg,
                    sizeof feed_msg) != 0)
        say(u, 1, "%s", feed_msg);
    else if (feed_msg[0] != '\0')
        say(u, 0, "%s", feed_msg);
    if (feed_gate(&u->set, argv0, port_exe, feed_msg, sizeof feed_msg) != 0) {
        say(u, 1, "%s", feed_msg);
        return -1;
    }
    /* The sound pair the settings name is composed from the player's own
     * cartridges the first time it is asked for, and kept. A missing
     * cartridge refuses here by slot name and the face stays standing. */
    if (mmo_soundcompose_ensure(&u->set, port_exe, note_say, u,
                                err, sizeof err) != 0) {
        say(u, 1, "%s", err);
        return -1;
    }
    /*
     * And the Pokemon that walks behind the player, out of the same cartridges, kept the same
     * way.
     */
    if (mmo_followcompose_ensure(&u->set, port_exe, note_say, u,
                                 err, sizeof err) < 1 && err[0] != '\0')
        say(u, 1, "%s", err);
    /*
     * And the Pokemon a Black or White cartridge adds, their tables, their battle art, their
     * moves and their abilities, out of the player's own two cartridges, kept the same way.
     */
    if (mmo_speciescompose_ensure(&u->set, port_exe, note_say, u,
                                  err, sizeof err) < 1 && err[0] != '\0')
        say(u, 1, "%s", err);
    /*
     * And the four trainers the creator offers past Platinum's own two, HeartGold's and
     * Black's boy and girl, out of all three cartridges, kept the same way. Last, because
     * its bases are allocated past every other package's claims.
     */
    if (mmo_lookcompose_ensure(&u->set, port_exe, note_say, u,
                               err, sizeof err) < 1 && err[0] != '\0')
        say(u, 1, "%s", err);
    mmo_launch_install_root(port_exe, root, sizeof root);
    /* What an earlier launch could not adopt and kept instead. */
    if (mmo_launch_export_sweep(port_exe, mmo_feed_installed_revision(root),
                                note_warn, u) > 0)
        say(u, 0, "a game from an earlier session is saved for offline play;"
                  " PLAY OFFLINE opens it from the character select, and any"
                  " earlier save is under Restore save");
    rc = mmo_launch_plan_build_session(&u->set, port_exe, view_exe, NULL,
                                       (long)mmo_plat_pid(),
                                       u->take_save_online,
                                       note_good, note_warn, u, &exp, &plan,
                                       err, sizeof err);
    if (rc != 0) {
        say(u, 1, "%s", err);
        mmo_launch_export_close(&exp);
        return -1;
    }
    rc = play(&plan, u);
    /*
     * Was there an offline game still waiting to go online? The adopt below makes the session
     * just played the offline save, and sets that one aside under Restore save, which is the
     * right way round, since the newest thing the player did is the session.
     */
    {
        mmo_launch_import waiting;

        aside = mmo_launch_import_offer(port_exe, u->set.slot, &waiting) == 1;
    }
    adopted = mmo_launch_export_adopt(&exp, NULL,
                                      mmo_feed_installed_revision(root),
                                      stamp, sizeof stamp, err, sizeof err);
    if (adopted < 0)
        say(u, 1, "%s", err);
    if (mmo_launch_export_settle(&exp, adopted))
        say(u, 1, "the game you just played is still at %s, and nothing has"
                  " been thrown away", exp.save);
    else if (adopted > 0) {
        /*
         * This sentence used to say "choose continue, not new GAME", and it was a caption over
         * the wrong screen.
         */
        say(u, 0, "your game is saved for offline play; PLAY OFFLINE opens it"
                  " from the character select, and any earlier save is under"
                  " Restore save");
        if (aside)
            say(u, 0, "the offline game you had waiting to go online is the"
                      " one under Restore save now; put it back and the box"
                      " to carry it up comes back with it");
    }
    /* A save the session took online: the game sets the report aside on the
     * server's word and comes back here, because the character it was drawing
     * is the one the save replaced. The box was answered for that save, so it
     * is cleared rather than left to warn about a report that is gone. */
    if (exp.import[0] != '\0' && mmo_launch_import_landed(exp.import)) {
        u->take_save_online = 0;
        say(u, 0, "your offline save is on the server now; PLAY carries on"
                  " as that character");
    }
    leave_if_signalled();
    return rc;
}

/* The same, with no server behind it. */
static int start_play_offline(struct ui *u, const char *cfg_path,
                              const char *argv0, const char *port_exe,
                              const char *view_exe)
{
    mmo_launch_plan plan;
    mmo_launch_offline off;
    char root[MMO_LAUNCH_PATH];
    char err[192];
    int rc;

    (void)argv0;
    if (mmo_launch_save(cfg_path, &u->set) != 0)
        say(u, 1, "settings not saved to %s", cfg_path);
    if (mmo_soundcompose_ensure(&u->set, port_exe, note_say, u,
                                err, sizeof err) != 0) {
        say(u, 1, "%s", err);
        return -1;
    }
    /*
     * And the Pokemon that walks behind the player, out of the same cartridges, kept the same
     * way.
     */
    if (mmo_followcompose_ensure(&u->set, port_exe, note_say, u,
                                 err, sizeof err) < 1 && err[0] != '\0')
        say(u, 1, "%s", err);
    /*
     * And the Pokemon a Black or White cartridge adds, their tables, their battle art, their
     * moves and their abilities, out of the player's own two cartridges, kept the same way.
     */
    if (mmo_speciescompose_ensure(&u->set, port_exe, note_say, u,
                                  err, sizeof err) < 1 && err[0] != '\0')
        say(u, 1, "%s", err);
    /*
     * And the four trainers the creator offers past Platinum's own two, HeartGold's and
     * Black's boy and girl, out of all three cartridges, kept the same way. Last, because
     * its bases are allocated past every other package's claims.
     */
    if (mmo_lookcompose_ensure(&u->set, port_exe, note_say, u,
                               err, sizeof err) < 1 && err[0] != '\0')
        say(u, 1, "%s", err);
    mmo_launch_install_root(port_exe, root, sizeof root);
    /* Here too, and here it decides which game is played: a handoff an earlier
     * launch kept is the newest session there is, and the row below opens the
     * offline save as it stands. Sweeping after it would play the older one
     * and adopt over the top of it at the next Play. */
    if (mmo_launch_export_sweep(port_exe, mmo_feed_installed_revision(root),
                                note_warn, u) > 0)
        say(u, 0, "a game from an earlier session was picked up, and any"
                  " earlier save is under Restore save");
    if (mmo_launch_offline_open(port_exe, u->set.slot, NULL, &off, err,
                                sizeof err) != 0) {
        say(u, 1, "%s", err);
        return -1;
    }
    /*
     * The save is this session's from the open above until the close below, and a row that
     * never gets as far as starting a game gives it straight back, otherwise the front door
     * would go on holding it, and the next PLAY OFFLINE would be refused by the launcher the
     * player is looking at.
     */
    if (mmo_launch_offline_begin(&off, mmo_feed_installed_revision(root),
                                 err, sizeof err) != 0) {
        say(u, 1, "%s", err);
        mmo_launch_offline_close(&off);
        return -1;
    }
    if (mmo_launch_plan_build_offline(&u->set, port_exe, view_exe, NULL,
                                      (long)mmo_plat_pid(), &off, &plan,
                                      err, sizeof err) != 0) {
        say(u, 1, "%s", err);
        mmo_launch_offline_close(&off);
        return -1;
    }
    rc = play(&plan, u);
    /* Whatever the session did, the record of it is closed and the image it
     * left is kept: a game that ended badly is exactly the one whose backup
     * matters. */
    if (mmo_launch_offline_end(&off, err, sizeof err) != 0)
        say(u, 1, "%s", err);
    /* And how to bring it back, said here because nothing else says it. */
    {
        mmo_launch_import back;

        if (mmo_launch_import_offer(port_exe, u->set.slot, &back) == 1)
            say(u, 0, "to take this game online, tick \"Take your offline"
                      " save online\" on the front door and press PLAY;"
                      " nothing here is thrown away either way");
        else if (back.unsure[0] != '\0')
            say(u, 1, "%s", back.unsure);
        else if (back.report[0] != '\0')
            /*
             * The silent case, which reads as a missing feature. There is a saved game here
             * and it is not newer than the copy the server was handed, so there is nothing to
             * offer and the box stays away, correct, and indistinguishable from the box
             * never existing.
             */
            say(u, 0, "nothing new to take online: this game has not been"
                      " saved since the server handed it over. Save it"
                      " (Menu > Save) and the box to carry it up appears");
    }
    leave_if_signalled();
    return rc;
}

struct gui_play_args {
    struct ui   *u;
    const char  *cfg;
    const char  *argv0;
    const char  *port;
    const char  *view;
};

static int gui_play_cb(void *ctx)
{
    struct gui_play_args *a = ctx;

    return start_play(a->u, a->cfg, a->argv0, a->port, a->view);
}

static int gui_play_offline_cb(void *ctx)
{
    struct gui_play_args *a = ctx;

    return start_play_offline(a->u, a->cfg, a->argv0, a->port, a->view);
}

/* The stamped images the offline row can go back to, newest first. */
static int gui_saves_cb(void *ctx, char out[][MMO_LAUNCH_STAMP], int max)
{
    struct gui_play_args *a = ctx;
    char dir[MMO_LAUNCH_PATH + 8];

    mmo_launch_save_dir(a->port, a->u->set.slot, dir, sizeof dir);
    return mmo_launch_offline_list(dir, out, max);
}

/* Is there an offline save newer than the copy the server was last given?
 * The window asks once a second while the front door is up. */
static int gui_offer_cb(void *ctx, mmo_launch_import *out)
{
    struct gui_play_args *a = ctx;

    return mmo_launch_import_offer(a->port, a->u->set.slot, out);
}

/* The player's answer to it, which lives for this session and is not written
 * to the settings file: a save is carried online because somebody ticked the
 * box this time, never because of a preference nobody remembers setting. */
static void gui_take_cb(void *ctx, int yes)
{
    struct gui_play_args *a = ctx;

    a->u->take_save_online = yes;
}

static int gui_restore_cb(void *ctx, const char *stamp)
{
    struct gui_play_args *a = ctx;
    char dir[MMO_LAUNCH_PATH + 8], err[192];

    mmo_launch_save_dir(a->port, a->u->set.slot, dir, sizeof dir);
    if (mmo_launch_offline_restore(dir, stamp, NULL, err, sizeof err) != 0) {
        say(a->u, 1, "%s", err);
        return -1;
    }
    say(a->u, 0, "restored the saved game from %s", stamp);
    return 0;
}

/*
 * Which of this install's saved games the window is looking at, and the press that changes it.
 */
static int gui_slot_cb(void *ctx)
{
    struct gui_play_args *a = ctx;

    return a->u->set.slot;
}

static void gui_set_slot_cb(void *ctx, int slot)
{
    struct gui_play_args *a = ctx;

    if (slot < 1 || slot > MMO_LAUNCH_SLOTS || slot == a->u->set.slot)
        return;
    a->u->set.slot = slot;
    if (mmo_launch_save(a->cfg, &a->u->set) != 0)
        say(a->u, 1, "settings not saved to %s", a->cfg);
}

/* The name a carried saved game is offered under. The slot is in it because
 * two of them on one stick otherwise differ by nothing a person can see. */
static void bundle_name(int slot, char *out, size_t cap)
{
    char wall[MMO_LAUNCH_TEXT];
    char stamp[MMO_LAUNCH_STAMP];
    size_t at = 0, i;

    mmo_plat_stamp(wall, sizeof wall);
    /* `YYYY-MM-DD HH:MM:SS` with the punctuation taken out, which is the same
     * shape every other name under save/ carries. A clock that will not read
     * leaves the date out rather than putting a colon in a file name. */
    for (i = 0; wall[i] != '\0' && at + 1 < sizeof stamp; i++) {
        if (wall[i] >= '0' && wall[i] <= '9')
            stamp[at++] = wall[i];
        else if (wall[i] == ' ')
            stamp[at++] = '-';
    }
    stamp[at] = '\0';
    if (at > 0)
        snprintf(out, cap, "openmmo-slot%d-%s.omsb", slot, stamp);
    else
        snprintf(out, cap, "openmmo-slot%d.omsb", slot);
}

/* Carrying this slot out to a file, and bringing one in. */
static int gui_save_export_cb(void *ctx)
{
    struct gui_play_args *a = ctx;
    char dir[MMO_LAUNCH_PATH + 8];
    char suggest[MMO_LAUNCH_NAME];
    char chosen[MMO_LAUNCH_PATH];
    char err[192];
    int picked;

    mmo_launch_save_dir(a->port, a->u->set.slot, dir, sizeof dir);
    bundle_name(a->u->set.slot, suggest, sizeof suggest);
    picked = mmo_plat_pick_save_file("Carry this saved game out to a file",
                                     dir, suggest, "OpenMMO saved game",
                                     "*.omsb", chosen, sizeof chosen);
    /* Closed without choosing: the player changed their mind, and a front door
     * that announced something anyway would be answering a question nobody
     * asked. */
    if (picked == 1)
        return 0;
    if (picked != 0)
        snprintf(chosen, sizeof chosen, "%s/%s", dir, suggest);
    if (mmo_launch_bundle_write(a->port, a->u->set.slot, chosen,
                                err, sizeof err) != 0) {
        say(a->u, 1, "%s", err);
        return -1;
    }
    say(a->u, 0, "slot %d is at %s; copy it to the other machine and bring it"
                 " in there", a->u->set.slot, chosen);
    return 0;
}

static int gui_save_import_cb(void *ctx)
{
    struct gui_play_args *a = ctx;
    char dir[MMO_LAUNCH_PATH + 8];
    char chosen[MMO_LAUNCH_PATH];
    char landed[MMO_LAUNCH_TEXT * 2];
    char err[192];
    int picked;

    mmo_launch_save_dir(a->port, a->u->set.slot, dir, sizeof dir);
    picked = mmo_plat_pick_file("Bring a saved game in from a file", dir,
                                "OpenMMO saved game", "*.omsb",
                                chosen, sizeof chosen);
    if (picked == 1)
        return 0;
    if (picked != 0) {
        snprintf(chosen, sizeof chosen, "%s/import.omsb", dir);
        if (access(chosen, R_OK) != 0) {
            say(a->u, 1, "this machine has no window for choosing a file, so"
                         " put the saved game at %s and press this again",
                chosen);
            return -1;
        }
    }
    if (mmo_launch_bundle_read(a->port, a->u->set.slot, chosen, landed,
                               sizeof landed, err, sizeof err) != 0) {
        say(a->u, 1, "%s", err);
        return -1;
    }
    say(a->u, 0, "%s", landed);
    return 0;
}

/* One line per key, the same names the usage string uses. `text ...` types
 * into an open field. PLAY and QUIT stop the file; anything else is applied
 * and the next line is read. */
static int apply_script(mmo_launch_menu *m, const char *path,
                        char *err, size_t errcap)
{
    FILE *f;
    char line[MMO_LAUNCH_PATH + 32];
    int last = MMO_LAUNCH_MENU_NONE, n = 0, rc;

    f = (strcmp(path, "-") == 0) ? stdin : fopen(path, "r");
    if (f == NULL) {
        snprintf(err, errcap, "cannot read %s: %s", path, strerror(errno));
        return -1;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        n++;
        rc = mmo_launch_menu_line(m, line);
        if (rc < 0) {
            snprintf(err, errcap, "%s:%d: unknown line",
                     strcmp(path, "-") == 0 ? "stdin" : path, n);
            if (f != stdin)
                fclose(f);
            return -1;
        }
        last = rc;
        if (last == MMO_LAUNCH_MENU_PLAY || last == MMO_LAUNCH_MENU_QUIT)
            break;
    }
    if (f != stdin)
        fclose(f);
    return last;
}

/* ------------------------------------------------------------------ */

/*
 * The install root, for an unpacked release: this program lives in `bin/` and the files the
 * feed lists are relative to the directory above it. A built tree has no `bin/` and no feed,
 * so the fallback is simply this program's own directory.
 */
static void install_root(const char *argv0, char *out, size_t cap)
{
    const char *e = getenv("OPENMMO_ROOT");
    const char *slash;
    size_t n;

    if (e != NULL && e[0] != '\0') {
        snprintf(out, cap, "%s", e);
        return;
    }
    slash = mmo_plat_last_sep(argv0);
    if (slash == NULL) {
        snprintf(out, cap, ".");
        return;
    }
    snprintf(out, cap, "%.*s", (int)(slash - argv0), argv0);
    n = strlen(out);
    /* The last component, not the last four bytes: a Windows release is
     * unpacked to `...\bin\openmmo-launch.exe` and a `/bin` compare would
     * leave the root pointing at bin/, where the feed then finds none of the
     * files it lists. mmo_plat_last_sep knows both separators. */
    if (n >= 4) {
        const char *last = mmo_plat_last_sep(out);

        if (last != NULL && strcmp(last + 1, "bin") == 0)
            out[last - out] = '\0';
    }
}

/* `path` as the feed would name it: relative to the install root, or empty
 * when it is somewhere else entirely, which the feed cannot vouch for anyway. */
static void relative_to(const char *root, const char *path, char *out, size_t cap)
{
    size_t n = strlen(root);

    out[0] = '\0';
    /* Either separator, for the same reason install_root above takes both:
     * on Windows the whole of this path is spelled with backslashes. */
    if (n > 0 && strncmp(path, root, n) == 0 &&
        (path[n] == '/' || path[n] == '\\'))
        snprintf(out, cap, "%s", path + n + 1);
}

/*
 * Whose signature the feed must carry: the launcher.cfg row when a tester set one, else the
 * key compiled into this build (feed_pin.h, the endpoint pin's arrangement). Returns 0 with
 * `key` filled, or -1 with the line a player is shown.
 */
static int load_feed_key(const mmo_launch_settings *s, mmo_rsa_pubkey *key,
                         char *msg, size_t cap)
{
    char err[192];

    if (s->feed_key[0] != '\0')
        return mmo_update_load_key(s->feed_key, key, msg, cap);
    if (mmo_rsa_pubkey_text(OPENMMO_PIN_FEED_KEY,
                            strlen(OPENMMO_PIN_FEED_KEY), key,
                            err, sizeof err) != 0) {
        snprintf(msg, cap, "the compiled-in feed key is unusable: %.100s",
                 err);
        return -1;
    }
    return 0;
}

/* The feed directory an install is held to: the configured one, or `feed/`
 * under the install root for a build with a compiled-in channel. */
static void feed_dir(const mmo_launch_settings *s, const char *root,
                     char *out, size_t cap)
{
    if (s->feed[0] != '\0')
        snprintf(out, cap, "%s", s->feed);
    else
        snprintf(out, cap, "%s/feed", root);
}

/*
 * The fetch before the gate: when a feed URL is configured, a launcher.cfg row, or the one
 * compiled into a release, bring the install up to what the server publishes.
 */
static int update_gate(const mmo_launch_settings *s, const char *argv0,
                       void (*note)(void *ud, const char *line),
                       void (*tick)(void *ud, const char *line), void *ud,
                       char *msg, size_t cap)
{
    static mmo_rsa_pubkey key;
    char root[MMO_LAUNCH_PATH], dir[MMO_LAUNCH_PATH + 8];
    char self[MMO_FEED_NAME];
    const char *url = s->feed_url[0] != '\0' ? s->feed_url
                                             : OPENMMO_PIN_FEED_URL;
    int self_updated = 0, i;

    msg[0] = '\0';
    if (url[0] == '\0') {
        char probe[MMO_LAUNCH_PATH + 16];

        /* A .forceupdate with nowhere to fetch from deserves a sentence,
         * not silence. The usual way here is a copied working-tree build,
         * whose compiled-in channel is empty. */
        install_root(argv0, root, sizeof root);
        snprintf(probe, sizeof probe, "%s/.forceupdate", root);
        if (access(probe, F_OK) == 0)
            snprintf(msg, cap, ".forceupdate is beside the install, but this "
                               "build has no update URL, none compiled in, "
                               "no feed-url row, so nothing can be fetched");
        return 0;
    }
    if (load_feed_key(s, &key, msg, cap) != 0)
        return -1;
    install_root(argv0, root, sizeof root);
    feed_dir(s, root, dir, sizeof dir);
    relative_to(root, argv0, self, sizeof self);
    /* The feed names files with '/', whatever this path was spelled with. */
    for (i = 0; self[i] != '\0'; i++)
        if (self[i] == '\\')
            self[i] = '/';
    return mmo_update_run(url, s->feed_ca[0] != '\0' ? s->feed_ca : NULL,
                          dir, &key, root, self, note, tick, ud,
                          &self_updated, msg, cap);
}

/*
 * What the channel publishes, beside what is installed here. Nothing is fetched but the one
 * signed document that carries the revision, and nothing on disk is touched whatever the
 * answer is.
 */
static int latest_gate(const mmo_launch_settings *s, const char *argv0,
                       char *msg, size_t cap)
{
    static mmo_rsa_pubkey key;
    char root[MMO_LAUNCH_PATH], line[MMO_FEED_MESSAGE];
    const char *url = s->feed_url[0] != '\0' ? s->feed_url
                                             : OPENMMO_PIN_FEED_URL;
    int latest = 0, have;

    msg[0] = '\0';
    if (url[0] == '\0') {
        snprintf(msg, cap, "no feed-url is configured, so there is no "
                           "channel to ask");
        return 0;
    }
    if (load_feed_key(s, &key, msg, cap) != 0)
        return -1;
    if (mmo_update_latest_revision(url,
                                   s->feed_ca[0] != '\0' ? s->feed_ca : NULL,
                                   &key, &latest, line, sizeof line) != 0) {
        snprintf(msg, cap, "%s", line);
        return -1;
    }
    install_root(argv0, root, sizeof root);
    have = mmo_feed_installed_revision(root);
    if (have <= 0)
        snprintf(msg, cap, "the channel publishes r%d, and this install does "
                           "not say what revision it is", latest);
    else if (latest > have)
        snprintf(msg, cap, "a newer build is published: r%d, and this one is "
                           "r%d", latest, have);
    else
        snprintf(msg, cap, "this build is current at r%d", have);
    return 0;
}

/*
 * The android channel's package, fetched to `dest` and proven, the read the handheld's front
 * door does when it is behind (android/src/mmo_update_notice.c), driven from a terminal so the
 * suite can hold it.
 */
static int fetch_package_gate(const mmo_launch_settings *s, const char *dest,
                              char *msg, size_t cap)
{
    static mmo_rsa_pubkey key;
    char line[MMO_FEED_MESSAGE], name[MMO_FEED_NAME];
    const char *url = s->feed_url[0] != '\0' ? s->feed_url
                                             : OPENMMO_PIN_FEED_URL;
    int latest = 0;

    msg[0] = '\0';
    if (url[0] == '\0') {
        snprintf(msg, cap, "no feed-url is configured, so there is no "
                           "channel to fetch from");
        return -1;
    }
    if (load_feed_key(s, &key, msg, cap) != 0)
        return -1;
    if (mmo_update_fetch_package(url,
                                 s->feed_ca[0] != '\0' ? s->feed_ca : NULL,
                                 &key, ".apk", dest, NULL, NULL, &latest,
                                 name, sizeof name, line, sizeof line) != 0) {
        snprintf(msg, cap, "%s", line);
        return -1;
    }
    snprintf(msg, cap, "%s is r%d and is now at %s", name, latest, dest);
    return 0;
}

/* Verify the feed, hold the install to it, and say whether the game may start. */
static int feed_gate(const mmo_launch_settings *s, const char *argv0,
                     const char *port_exe, char *msg, size_t cap)
{
    /* ~190 KB of inventory: static, never a stack frame. */
    static mmo_feed_update upd;
    static mmo_rsa_pubkey key;
    mmo_feed_main main_feed;
    mmo_feed_report report;
    char root[MMO_LAUNCH_PATH], dir[MMO_LAUNCH_PATH + 8];
    char target[MMO_FEED_NAME];

    msg[0] = '\0';
    /* A feed to hold the install to: the configured directory, or the one a
     * compiled-in channel keeps under the install root. Neither means no
     * check, which is the build tree and any install nobody publishes
     * updates for. */
    if (s->feed[0] == '\0' && OPENMMO_PIN_FEED_URL[0] == '\0')
        return 0;

    if (load_feed_key(s, &key, msg, cap) != 0)
        return -1;

    install_root(argv0, root, sizeof root);
    feed_dir(s, root, dir, sizeof dir);
    if (mmo_feed_load(dir, &key, &main_feed, &upd, &report) != 0) {
        snprintf(msg, cap, "%s", report.message);
        return -1;
    }
    relative_to(root, port_exe, target, sizeof target);
    if (mmo_feed_check(root, &main_feed, &upd, mmo_feed_os(), mmo_feed_arch(),
                       target, &report) != MMO_FEED_OK) {
        snprintf(msg, cap, "%s", report.message);
        return -1;
    }
    snprintf(msg, cap, "%s", report.message);
    return 0;
}

static void usage(const char *argv0)
{
    printf("usage: %s [options]\n"
           "\n"
           "The front door: account, cartridges, how the picture is drawn,\n"
           "and Play. The server is not among them, this client is built\n"
           "for one and dials it, and Play is that session: there is no\n"
           "single-player boot here and no save file to name, because the\n"
           "party and position are the server's. Settings are remembered in\n"
           "a file (--config says which); no cartridge is supplied by this\n"
           "build.\n"
           "CARTRIDGES is the folder holding your own backups of three\n"
           "cartridges, Platinum, Heart Gold and Black, under any file\n"
           "names (SoulSilver and White stand in for the last two), or the\n"
           "Platinum file itself with the other two beside it. Play refuses\n"
           "until all three are there. An account is created on the login\n"
           "server (create-user), not from this window.\n"
           "\n"
           "  --config PATH  the settings file (default:\n"
           "                 $XDG_CONFIG_HOME/openmmo/launcher.cfg)\n"
           "  --print-plan   print the two command lines Play would run, and\n"
           "                 exit; the password is printed as <hidden>\n"
           "  --check-species BW PT\n"
           "                 rebuild every species both games have from BW and\n"
           "                 say how many of them differ from PT at each byte;\n"
           "                 the oracle the species fill proves itself on\n"
           "  --compose-species BW PT OUT\n"
           "                 fill OUT's species tables from BW, growing PT's\n"
           "                 own name bank, and exit\n"
           "  --compose-anim BW OUT\n"
           "                 carry the animation loops out of BW into OUT\n"
           "  --compose-sheets BW PT OUT\n"
           "                 bake BW's battle sprites into OUT's sheets and\n"
           "                 height bytes, proven on PT's own first\n"
           "  --compose-moves BW PT OUT\n"
           "                 write the 92 moves BW adds into OUT\n"
           "  --compose-abilities BW PT OUT\n"
           "                 write the 41 abilities BW adds into OUT\n"
           "  --compose-cries BW OUT.bin\n"
           "                 decode BW's ported cries into one pack\n"
           "  --fill-imports the whole of the above through the seam Play\n"
           "                 uses, into the install's mods folder\n"
           "  --compose-pair T F PT HG BW OUT\n"
           "                 run the sound compose alone (0 platinum, 1\n"
           "                 heartgold, 2 blackwhite; three cartridge paths,\n"
           "                 one output archive) and exit, the door the\n"
           "                 test drives; Play composes on its own\n"
           "  --play         press Play on the saved settings without opening\n"
           "                 the menu, and exit when the session ends\n"
           "  --play-offline the other row: no server, no update and no feed\n"
           "                 check. The save file under save/ is the game, the\n"
           "                 clock is this machine's, and the session is\n"
           "                 recorded beside it\n"
           "  --list-saves   the earlier saved games kept under save/, newest\n"
           "                 first, and exit\n"
           "  --slot N       which of this install's saved games to play,\n"
           "                 list, restore, carry out or bring in. 1 is the\n"
           "                 folder that was always there (save/); the rest\n"
           "                 are save/slotN. Remembered in the settings\n"
           "  --export-save FILE\n"
           "                 write the whole of this slot, the save, the\n"
           "                 report beside it and every session record since\n"
           "                 the last export, to one file, and exit. That\n"
           "                 file is what another machine brings in; the save\n"
           "                 alone travels too, but arrives with no play\n"
           "                 behind it and stays marked online\n"
           "  --import-save FILE\n"
           "                 put one of those into this slot, keeping the\n"
           "                 game it replaces under Restore save, and exit\n"
           "  --take-save-online\n"
           "                 with --play: offer the offline save to the server\n"
           "                 as the character this session picks, if it is\n"
           "                 newer than the copy the server was last given.\n"
           "                 The window asks this with a box; this is the same\n"
           "                 answer, said on the command line\n"
           "  --restore-save STAMP\n"
           "                 put one of those back as the save file, keeping\n"
           "                 the one it replaces, and exit\n"
           "  --script FILE  apply keys to the menu (one per line: up, down,\n"
           "                 left, right, enter, esc, backspace, or\n"
           "                 `text ...`) and then --shot / --play / the\n"
           "                 window; `-' reads stdin. This is what a person\n"
           "                 pressing those keys does, without a display.\n"
           "  --check-feed   verify the configured update feed, hold the\n"
           "                 install to it, print the verdict and exit\n"
           "  --update       fetch the update the configured feed-url\n"
           "                 publishes, prove it against the signed feed,\n"
           "                 install it and exit; Play also does this first\n"
           "  --latest       ask the feed-url what revision it publishes,\n"
           "                 say whether this install is behind it, and exit\n"
           "                 without downloading or changing anything\n"
           "  --fetch-package PATH\n"
           "                 fetch the .apk the feed-url's channel signs into\n"
           "                 PATH, prove it, and exit, the handheld's own\n"
           "                 update read, from a terminal\n"
           "  --shot PATH    write the menu as a binary PPM and exit, so a\n"
           "                 run with no display can read this program\n"
           "  --port PATH    the game binary (also $OPENMMO_PORT)\n"
           "  --viewer PATH  the window binary (also $OPENMMO_VIEWER)\n"
           "  --help         this message\n"
           "\n"
           "The window is a form: click a field to type, Browse to pick a\n"
           "ROM, Play to start. Closing the game comes back here, and so\n"
           "does a session the server ends.\n",
           argv0);
}

/*
 * play.sh already forces X11 under WSLg: Wayland here is a window the keyboard and the audio
 * device do not reach. The launcher is GLFW (raylib) and the game window is SDL, so both
 * backends have to be told.
 */
static void prefer_x11_on_wsl(void)
{
    const char *disp = getenv("DISPLAY");
    const char *wsl = getenv("WSL_DISTRO_NAME");
    const char *sdl, *glfw;

    if (disp == NULL || disp[0] == '\0')
        return;
    if ((wsl == NULL || wsl[0] == '\0') && access("/mnt/wslg", F_OK) != 0)
        return;
    sdl = getenv("SDL_VIDEODRIVER");
    if (sdl == NULL || sdl[0] == '\0')
        mmo_plat_setenv("SDL_VIDEODRIVER", "x11", 0);
    glfw = getenv("GLFW_PLATFORM");
    if (glfw == NULL || glfw[0] == '\0')
        mmo_plat_setenv("GLFW_PLATFORM", "x11", 0);
}

int main(int argc, char **argv)
{
    char cfg_path[MMO_LAUNCH_PATH] = { 0 };
    char port_exe[MMO_LAUNCH_PATH], view_exe[MMO_LAUNCH_PATH];
    char rom_here[MMO_LAUNCH_PATH];
    char err[192] = { 0 };
    const char *shot = NULL;
    const char *script = NULL;
    struct ui u;
    char feed_msg[MMO_FEED_MESSAGE];
    int print_plan = 0, play_now = 0, check_feed = 0, do_update = 0;
    int fill_imports = 0;
    int play_offline = 0, list_saves = 0;
    const char *restore_stamp = NULL;
    const char *bundle_out = NULL, *bundle_in = NULL;
    int slot_arg = 0;
    int ask_latest = 0, i, rc;
    const char *fetch_dest = NULL;
    int script_act = MMO_LAUNCH_MENU_NONE;

    prefer_x11_on_wsl();
    /*
     * Resolved once, here, and handed to both children: the game and the window each resolve
     * their own if they are started by hand, and this is what keeps a session from writing
     * into two different folders when the three programs are not where each other expects.
     */
    {
        char logs[MMO_LAUNCH_PATH];

        if (mmo_plat_log_dir(logs, sizeof logs) == 0)
            mmo_plat_setenv("OPENMMO_LOGS", logs, 1);
    }
    /* Before anything is written to them, so a session never has its own first
     * line rolled out from under it. */
    log_rotate(game_log_path());
    log_rotate(view_log_path());
    log_rotate(launch_log_path());
    mmo_plat_crash_install("launcher");
    memset(&u, 0, sizeof u);
    mmo_launch_defaults(&u.set);
    mmo_launch_menu_init(&u.menu, &u.set);
    mmo_launch_paths(argv[0], port_exe, sizeof port_exe,
                     view_exe, sizeof view_exe, rom_here, sizeof rom_here);
    /* A working tree keeps its cartridges in roms/; a release has no such
     * folder and the lookup finds nothing. */
    {
        char roms[MMO_LAUNCH_PATH];

        mmo_launch_roms_dir(argv[0], roms, sizeof roms);
        mmo_launch_roms_fallback(roms);
    }

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(a, "--fill-imports") == 0) {
            fill_imports = 1;
        } else if (strcmp(a, "--print-plan") == 0) {
            print_plan = 1;
        } else if (strcmp(a, "--play") == 0) {
            play_now = 1;
        } else if (strcmp(a, "--play-offline") == 0) {
            play_offline = 1;
        } else if (strcmp(a, "--take-save-online") == 0) {
            u.take_save_online = 1;
        } else if (strcmp(a, "--list-saves") == 0) {
            list_saves = 1;
        } else if (strcmp(a, "--restore-save") == 0 && i + 1 < argc) {
            restore_stamp = argv[++i];
        } else if (strcmp(a, "--slot") == 0 && i + 1 < argc) {
            /* Held rather than applied: the settings file is read further
             * down and would put the remembered slot back over this one. */
            slot_arg = atoi(argv[++i]);
        } else if (strcmp(a, "--export-save") == 0 && i + 1 < argc) {
            bundle_out = argv[++i];
        } else if (strcmp(a, "--import-save") == 0 && i + 1 < argc) {
            bundle_in = argv[++i];
        } else if (strcmp(a, "--check-feed") == 0) {
            check_feed = 1;
        } else if (strcmp(a, "--update") == 0) {
            do_update = 1;
        } else if (strcmp(a, "--latest") == 0) {
            ask_latest = 1;
        } else if (strcmp(a, "--fetch-package") == 0 && i + 1 < argc) {
            fetch_dest = argv[++i];
        } else if (strcmp(a, "--compose-pair") == 0 && i + 6 < argc) {
            /* the compose engine alone, for the test that holds it and the
             * python pipeline together: track font pt hg bw out */
            char cerr[256] = "";
            int trk = atoi(argv[i + 1]);
            int fnt = atoi(argv[i + 2]);

            if (mmo_soundcompose(trk, fnt, argv[i + 3], argv[i + 4],
                                 argv[i + 5], argv[i + 6],
                                 cerr, sizeof cerr) != 0) {
                fprintf(stderr, "openmmo-launch: compose failed: %s\n", cerr);
                return 1;
            }
            printf("composed %s\n", argv[i + 6]);
            return 0;
        } else if (strcmp(a, "--follower-base") == 0 && i + 2 < argc) {
            /* the allocator alone: mods-root package,list -> the two bases it
             * would fill at. What the test asserts, and what makes the number
             * in composed.txt checkable without a cartridge. */
            printf("%d %d\n",
                   mmo_followcompose_base(argv[i + 1], argv[i + 2],
                                          "data/mmodel/mmodel.narc", 470),
                   mmo_followcompose_base(argv[i + 1], argv[i + 2],
                                          "data/mmodel/fldeff.narc", 201));
            return 0;
        } else if (strcmp(a, "--check-species") == 0 && i + 2 < argc) {
            /* The shared-range oracle alone, for the test that holds this
             * file's field mapping against the two images: rebuild every
             * species both games have and say, per byte, how many differ. */
            char cerr[256] = "";
            int diff[MMO_SPECIESCOMPOSE_ENTRY];
            int n = mmo_speciescompose_check(argv[i + 1], argv[i + 2], diff,
                                             cerr, sizeof cerr);
            int k;

            if (n < 0) {
                fprintf(stderr, "openmmo-launch: species check failed: %s\n",
                        cerr);
                return 1;
            }
            printf("compared %d\n", n);
            for (k = 0; k < MMO_SPECIESCOMPOSE_ENTRY; k++) {
                if (diff[k] != 0)
                    printf("%d %d\n", k, diff[k]);
            }
            return 0;
        } else if (strcmp(a, "--compose-species") == 0 && i + 3 < argc) {
            /* The table half of the species fill alone: bw pt pkg. */
            char cerr[256] = "";

            if (mmo_speciescompose_tables(argv[i + 1], argv[i + 2], argv[i + 3],
                                          NULL, NULL,
                                          cerr, sizeof cerr) != 0) {
                fprintf(stderr, "openmmo-launch: species fill failed: %s\n",
                        cerr);
                return 1;
            }
            printf("filled %s\n", argv[i + 3]);
            return 0;
        } else if (strcmp(a, "--compose-sheets") == 0 && i + 3 < argc) {
            /* The battle art alone: bw pt pkg. */
            char cerr[256] = "";

            if (mmo_speciescompose_sheets(argv[i + 1], argv[i + 2], argv[i + 3],
                                          NULL, NULL,
                                          cerr, sizeof cerr) != 0) {
                fprintf(stderr, "openmmo-launch: sprite fill failed: %s\n",
                        cerr);
                return 1;
            }
            printf("filled %s\n", argv[i + 3]);
            return 0;
        } else if (strcmp(a, "--compose-cries") == 0 && i + 2 < argc) {
            /* The ported cries alone: bw out.bin. */
            char cerr[256] = "";

            if (mmo_soundcompose_cries(argv[i + 1], argv[i + 2],
                                       cerr, sizeof cerr) != 0) {
                fprintf(stderr, "openmmo-launch: cry pack failed: %s\n", cerr);
                return 1;
            }
            printf("wrote %s\n", argv[i + 2]);
            return 0;
        } else if (strcmp(a, "--compose-abilities") == 0 && i + 3 < argc) {
            /* The 41 abilities alone: bw pt pkg. */
            char cerr[256] = "";

            if (mmo_speciescompose_abilities(argv[i + 1], argv[i + 2],
                                             argv[i + 3], NULL, NULL,
                                             cerr, sizeof cerr) != 0) {
                fprintf(stderr, "openmmo-launch: ability fill failed: %s\n",
                        cerr);
                return 1;
            }
            printf("filled %s\n", argv[i + 3]);
            return 0;
        } else if (strcmp(a, "--compose-moves") == 0 && i + 3 < argc) {
            /* The 92 moves alone: bw pt pkg. */
            char cerr[256] = "";

            if (mmo_speciescompose_moves(argv[i + 1], argv[i + 2], argv[i + 3],
                                         NULL, NULL,
                                         cerr, sizeof cerr) != 0) {
                fprintf(stderr, "openmmo-launch: move fill failed: %s\n", cerr);
                return 1;
            }
            printf("filled %s\n", argv[i + 3]);
            return 0;
        } else if (strcmp(a, "--compose-anim") == 0 && i + 2 < argc) {
            /* The animation carry alone: bw pkg. */
            char cerr[256] = "";

            if (mmo_speciescompose_anim(argv[i + 1], argv[i + 2], NULL, NULL,
                                        cerr, sizeof cerr) != 0) {
                fprintf(stderr, "openmmo-launch: animation carry failed: %s\n",
                        cerr);
                return 1;
            }
            printf("filled %s\n", argv[i + 2]);
            return 0;
        } else if (strcmp(a, "--compose-looks") == 0 && i + 4 < argc) {
            /* the look fill alone, for the test that holds it and
             * tools/portlooks.py together:
             * hg bw pt pkg [mmodel-base class-base back-base [DIR:pkg,pkg]] */
            char cerr[256] = "";
            int mfirst = i + 5 < argc ? atoi(argv[i + 5]) : 0;
            int cfirst = i + 6 < argc ? atoi(argv[i + 6]) : 0;
            int bfirst = i + 7 < argc ? atoi(argv[i + 7]) : 0;
            const char *others = i + 8 < argc ? argv[i + 8] : NULL;

            if (mmo_lookcompose(argv[i + 1], argv[i + 2], argv[i + 3], argv[i + 4],
                                mfirst, cfirst, bfirst, others, cerr, sizeof cerr) != 0) {
                fprintf(stderr, "openmmo-launch: look fill failed: %s\n", cerr);
                return 1;
            }
            printf("composed %s\n", argv[i + 4]);
            return 0;
        } else if (strcmp(a, "--compose-followers") == 0 && i + 2 < argc) {
            /* the follower fill alone, for the test that holds it and
             * tools/portfollow.py together: rom pkg [mmodel-base emote-base] */
            char cerr[256] = "";
            int mfirst = i + 3 < argc ? atoi(argv[i + 3]) : 0;
            int efirst = i + 4 < argc ? atoi(argv[i + 4]) : 0;

            if (mmo_followcompose(argv[i + 1], argv[i + 2], mfirst, efirst,
                                  cerr, sizeof cerr) != 0) {
                fprintf(stderr, "openmmo-launch: follower fill failed: %s\n",
                        cerr);
                return 1;
            }
            printf("filled %s\n", argv[i + 2]);
            return 0;
        } else if (strcmp(a, "--config") == 0 && i + 1 < argc) {
            snprintf(cfg_path, sizeof cfg_path, "%s", argv[++i]);
        } else if (strcmp(a, "--shot") == 0 && i + 1 < argc) {
            shot = argv[++i];
        } else if (strcmp(a, "--script") == 0 && i + 1 < argc) {
            script = argv[++i];
        } else if (strcmp(a, "--port") == 0 && i + 1 < argc) {
            snprintf(port_exe, sizeof port_exe, "%s", argv[++i]);
        } else if (strcmp(a, "--viewer") == 0 && i + 1 < argc) {
            snprintf(view_exe, sizeof view_exe, "%s", argv[++i]);
        } else {
            fprintf(stderr, "openmmo-launch: unknown option %s (try --help)\n", a);
            return 2;
        }
    }

    if (cfg_path[0] == '\0' &&
        mmo_launch_config_path(cfg_path, sizeof cfg_path) < 0) {
        fprintf(stderr, "openmmo-launch: neither $XDG_CONFIG_HOME nor $HOME is\n"
                        "               set, so there is nowhere to keep settings.\n"
                        "               Pass --config PATH.\n");
        return 1;
    }

    rc = mmo_launch_load(cfg_path, &u.set, err, sizeof err);
    if (rc < 0) {
        /* Refused rather than partly applied: half a settings file is a game
         * started with settings nobody chose. */
        fprintf(stderr, "openmmo-launch: %s: %s\n", cfg_path, err);
        return 1;
    }
    /* After the file, and refused rather than clamped: a person who typed a
     * slot number meant that saved game, and quietly playing another one is
     * how the wrong file gets overwritten. The config key clamps instead,
     * because a file nobody typed today must not stop the door opening. */
    if (slot_arg != 0) {
        if (slot_arg < 1 || slot_arg > MMO_LAUNCH_SLOTS) {
            fprintf(stderr, "openmmo-launch: there are %d saved game slots,"
                            " numbered from 1\n", MMO_LAUNCH_SLOTS);
            return 1;
        }
        u.set.slot = slot_arg;
    }
    /*
     * An older file with the password still in it, written back out without one before
     * anything else runs. Waiting for the next ordinary save would leave it there for a player
     * who opens the front door and closes it, and this is the run where we know it is there.
     */
    if (rc == 2) {
        if (mmo_launch_save(cfg_path, &u.set) != 0)
            fprintf(stderr, "openmmo-launch: %s still holds a saved password"
                            " and could not be rewritten\n", cfg_path);
        else
            fprintf(stderr, "openmmo-launch: removed the saved password from"
                            " %s; it is not kept on disk any more\n", cfg_path);
    }
    /*
     * An unpacked release keeps the ROM in the directory the player was told to put it in,
     * which is the same place the game itself looks when nothing names one. Adopt it only when
     * the settings name no ROM at all, so a player who chose a different file keeps it.
     */
    if (u.set.rom[0] == '\0' && rom_here[0] != '\0')
        snprintf(u.set.rom, sizeof u.set.rom, "%s", rom_here);
    if (u.set.rom[0] == '\0') {
        const char *pc = getenv("PC_ROM");
        const char *eng = getenv("ENGINE_DIR");
        char file[MMO_LAUNCH_PATH], cand[MMO_LAUNCH_PATH];

        if (pc != NULL && mmo_launch_rom_file(pc, file, sizeof file) == 0)
            snprintf(u.set.rom, sizeof u.set.rom, "%s", pc);
        else if (eng != NULL && eng[0] != '\0') {
            snprintf(cand, sizeof cand, "%s/build/rom", eng);
            if (mmo_launch_rom_file(cand, file, sizeof file) == 0)
                snprintf(u.set.rom, sizeof u.set.rom, "%s", cand);
        }
    }

    /*
     * Same rule as the ROM: adopt a default only when the settings name nothing, so a player
     * who chose a folder keeps it. $PC_MODS_DIR wins when set; otherwise mods/ next to the ROM
     * if it exists, otherwise <exedir>/../mods (mmo/mods in a build tree).
     */
    if (u.set.mods_dir[0] == '\0') {
        const char *pc = getenv("PC_MODS_DIR");
        char cand[MMO_LAUNCH_PATH], romfile[MMO_LAUNCH_PATH];
        const char *slash;
        int n;

        if (pc != NULL && pc[0] != '\0') {
            snprintf(u.set.mods_dir, sizeof u.set.mods_dir, "%s", pc);
        } else {
            if (u.set.rom[0] != '\0'
                && mmo_launch_rom_file(u.set.rom, romfile, sizeof romfile) == 0) {
                slash = mmo_plat_last_sep(romfile);
                if (slash != NULL) {
                    n = snprintf(cand, sizeof cand, "%.*s/mods",
                                 (int)(slash - romfile), romfile);
                    if (n > 0 && (size_t)n < sizeof cand && access(cand, R_OK) == 0)
                        snprintf(u.set.mods_dir, sizeof u.set.mods_dir, "%s", cand);
                }
            }
            if (u.set.mods_dir[0] == '\0') {
                slash = mmo_plat_last_sep(argv[0]);
                if (slash != NULL)
                    n = snprintf(cand, sizeof cand, "%.*s/../mods",
                                 (int)(slash - argv[0]), argv[0]);
                else
                    n = snprintf(cand, sizeof cand, "../mods");
                if (n > 0 && (size_t)n < sizeof cand && access(cand, R_OK) == 0)
                    snprintf(u.set.mods_dir, sizeof u.set.mods_dir, "%s", cand);
            }
        }
    }
    if (u.set.mods[0] == '\0') {
        const char *pc = getenv("PC_MODS");

        if (pc != NULL && pc[0] != '\0')
            snprintf(u.set.mods, sizeof u.set.mods, "%s", pc);
    }

    /* Only when there is one. A missing ROM is the footer's to say: it is live,
     * it carries the button that fixes it, and it stops saying it the moment
     * one is chosen. This line could not, it was written once at startup and
     * went on asking for a ROM the player had already picked. */
    if (u.set.rom[0] != '\0')
        say(&u, 0, "ready");

    if (script != NULL) {
        script_act = apply_script(&u.menu, script, err, sizeof err);
        if (script_act < 0) {
            fprintf(stderr, "openmmo-launch: %s\n", err);
            return 1;
        }
    }

    if (do_update) {
        rc = update_gate(&u.set, argv[0], note_print, NULL, NULL, feed_msg,
                         sizeof feed_msg);
        if (feed_msg[0] == '\0')
            snprintf(feed_msg, sizeof feed_msg,
                     "no feed-url is configured, so there is nothing to fetch");
        fprintf(rc == 0 ? stdout : stderr, "openmmo-launch: %s\n", feed_msg);
        return rc == 0 ? 0 : 1;
    }

    if (ask_latest) {
        rc = latest_gate(&u.set, argv[0], feed_msg, sizeof feed_msg);
        fprintf(rc == 0 ? stdout : stderr, "openmmo-launch: %s\n", feed_msg);
        return rc == 0 ? 0 : 1;
    }

    if (fetch_dest != NULL) {
        rc = fetch_package_gate(&u.set, fetch_dest, feed_msg, sizeof feed_msg);
        fprintf(rc == 0 ? stdout : stderr, "openmmo-launch: %s\n", feed_msg);
        return rc == 0 ? 0 : 1;
    }

    if (check_feed) {
        rc = feed_gate(&u.set, argv[0], port_exe, feed_msg, sizeof feed_msg);

        if (feed_msg[0] == '\0')
            snprintf(feed_msg, sizeof feed_msg,
                     "no update feed is configured, so nothing was checked");
        fprintf(rc == 0 ? stdout : stderr, "openmmo-launch: %s\n", feed_msg);
        return rc == 0 ? 0 : 1;
    }

    if (print_plan) {
        mmo_launch_plan plan;
        mmo_launch_export exp;
        mmo_launch_offline off;
        int built;

        /* Asked for the offline row, print the offline row. */
        if (play_offline) {
            int built;

            if (mmo_launch_offline_open(port_exe, u.set.slot, NULL, &off, err,
                                        sizeof err) != 0) {
                fprintf(stderr, "openmmo-launch: %s\n", err);
                return 1;
            }
            built = mmo_launch_plan_build_offline(&u.set, port_exe, view_exe,
                                                  NULL, (long)mmo_plat_pid(),
                                                  &off, &plan, err,
                                                  sizeof err);
            /* Printing a plan is not playing one, so the save goes back
             * before this returns: a --print-plan that left it claimed would
             * refuse the Play it was run to explain. */
            mmo_launch_offline_close(&off);
            if (built != 0) {
                fprintf(stderr, "openmmo-launch: %s\n", err);
                return 1;
            }
            mmo_launch_plan_print(&plan, stdout);
            return 0;
        }
        /*
         * The plan Play would run, built by the same call Play builds it with: where Continue
         * Offline would write, and the save --take-save-online would carry up. A printed plan
         * that leaves a name out is a plan nobody can reproduce by hand.
         */
        built = mmo_launch_plan_build_session(&u.set, port_exe, view_exe, NULL,
                                              (long)mmo_plat_pid(),
                                              u.take_save_online, NULL, NULL,
                                              NULL, &exp, &plan,
                                              err, sizeof err);
        if (built != 0) {
            fprintf(stderr, "openmmo-launch: %s\n", err);
            return 1;
        }
        mmo_launch_plan_print(&plan, stdout);
        return 0;
    }
    if (list_saves) {
        char stamps[MMO_LAUNCH_SAVE_KEEP][MMO_LAUNCH_STAMP];
        char dir[MMO_LAUNCH_PATH + 8];
        int n, k;

        mmo_launch_save_dir(port_exe, u.set.slot, dir, sizeof dir);
        n = mmo_launch_offline_list(dir, stamps, MMO_LAUNCH_SAVE_KEEP);
        for (k = 0; k < n; k++)
            printf("%s\n", stamps[k]);
        if (n == 0)
            fprintf(stderr, "openmmo-launch: no saved games kept in %s\n", dir);
        return 0;
    }

    if (restore_stamp != NULL) {
        char dir[MMO_LAUNCH_PATH + 8];

        mmo_launch_save_dir(port_exe, u.set.slot, dir, sizeof dir);
        if (mmo_launch_offline_restore(dir, restore_stamp, NULL,
                                       err, sizeof err) != 0) {
            fprintf(stderr, "openmmo-launch: %s\n", err);
            return 1;
        }
        printf("openmmo-launch: restored the saved game from %s\n",
               restore_stamp);
        return 0;
    }

    if (bundle_out != NULL) {
        if (mmo_launch_bundle_write(port_exe, u.set.slot, bundle_out,
                                    err, sizeof err) != 0) {
            fprintf(stderr, "openmmo-launch: %s\n", err);
            return 1;
        }
        printf("openmmo-launch: slot %d is at %s; copy it to the other"
               " machine and bring it in there\n", u.set.slot, bundle_out);
        return 0;
    }

    /* The whole species fill, through the seam Play uses, the stamp, the mods
     * folder and the cartridge slots included, so a test can drive exactly
     * what a player's press does rather than the five doors underneath it. */
    if (fill_imports) {
        char ferr[256] = "";
        int rc2 = mmo_speciescompose_ensure(&u.set, port_exe, note_say, &u,
                                            ferr, sizeof ferr);

        if (rc2 < 0) {
            fprintf(stderr, "openmmo-launch: %s\n", ferr);
            return 1;
        }
        printf("%s\n", rc2 == 1 ? "imports: ready"
                                 : (ferr[0] != '\0' ? ferr : "imports: not filled"));
        return rc2 == 1 ? 0 : 2;
    }

    if (bundle_in != NULL) {
        char landed[MMO_LAUNCH_TEXT * 2];

        if (mmo_launch_bundle_read(port_exe, u.set.slot, bundle_in, landed,
                                   sizeof landed, err, sizeof err) != 0) {
            fprintf(stderr, "openmmo-launch: %s\n", err);
            return 1;
        }
        printf("openmmo-launch: %s\n", landed);
        return 0;
    }

    if (play_offline) {
        rc = start_play_offline(&u, cfg_path, argv[0], port_exe, view_exe);
        fprintf(rc == 0 ? stdout : stderr, "openmmo-launch: %s\n",
                u.menu.status);
        return rc == 0 ? 0 : 1;
    }

    /*
     * Play without the menu: the same settings, the same plan, the same fork, with no window
     * of our own in the way.
     */
    if (play_now) {
        rc = start_play(&u, cfg_path, argv[0], port_exe, view_exe);
        fprintf(rc == 0 ? stdout : stderr, "openmmo-launch: %s\n",
                u.menu.status);
        return rc == 0 ? 0 : 1;
    }

    if (script_act == MMO_LAUNCH_MENU_QUIT) {
        if (mmo_launch_save(cfg_path, &u.set) != 0)
            fprintf(stderr, "openmmo-launch: could not write %s\n", cfg_path);
        return 0;
    }
    if (script_act == MMO_LAUNCH_MENU_PLAY_OFFLINE) {
        rc = start_play_offline(&u, cfg_path, argv[0], port_exe, view_exe);
        fprintf(rc == 0 ? stdout : stderr, "openmmo-launch: %s\n",
                u.menu.status);
        if (shot != NULL && ui_write_shot(&u, shot) != 0)
            fprintf(stderr, "openmmo-launch: cannot write %s: %s\n",
                    shot, strerror(errno));
        return rc == 0 ? 0 : 1;
    }
    if (script_act == MMO_LAUNCH_MENU_PLAY) {
        rc = start_play(&u, cfg_path, argv[0], port_exe, view_exe);
        fprintf(rc == 0 ? stdout : stderr, "openmmo-launch: %s\n", u.menu.status);
        if (shot != NULL && ui_write_shot(&u, shot) != 0)
            fprintf(stderr, "openmmo-launch: cannot write %s: %s\n",
                    shot, strerror(errno));
        return rc == 0 ? 0 : 1;
    }
    /* Keys that only changed a setting are what a person would leave the
     * menu holding, so they are written before --shot or the window opens. */
    if (script != NULL && mmo_launch_save(cfg_path, &u.set) != 0)
        fprintf(stderr, "openmmo-launch: could not write %s\n", cfg_path);

    if (shot != NULL) {
        if (ui_write_shot(&u, shot) != 0) {
            fprintf(stderr, "openmmo-launch: cannot write %s: %s\n",
                    shot, strerror(errno));
            return 1;
        }
        return 0;
    }

    {
        struct gui_play_args gp = { &u, cfg_path, argv[0], port_exe, view_exe };
        struct launch_gui_host host;

        host.set = &u.set;
        host.status = u.menu.status;
        host.status_bad = &u.menu.status_bad;
        host.conn = u.conn;
        host.conn_colour = u.conn_colour;
        host.play = gui_play_cb;
        host.play_offline = gui_play_offline_cb;
        host.saves = gui_saves_cb;
        host.restore = gui_restore_cb;
        host.offer = gui_offer_cb;
        host.take = gui_take_cb;
        host.slot = gui_slot_cb;
        host.set_slot = gui_set_slot_cb;
        host.save_export = gui_save_export_cb;
        host.save_import = gui_save_import_cb;
        host.ctx = &gp;
        launch_gui_run(&host);
    }

    if (mmo_launch_save(cfg_path, &u.set) != 0)
        fprintf(stderr, "openmmo-launch: could not write %s\n", cfg_path);
    return 0;
}
