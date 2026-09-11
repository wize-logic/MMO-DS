/*
 * The app finds out it is behind, fetches the new build, and hands it
 * to the installer.
 */

#include "mmo_update_notice.h"

#include <android/log.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "feed.h"
#include "feed_pin.h"
#include "update.h"
#include "view_ui.h"
#include "view_ui_draw.h"

#define TAG "openmmo.update"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

/*
 * This build'S own REVISION, compiled in by Makefile.android from the same commit count the
 * APK's versionCode and the release archive are named by.
 */
#ifndef OPENMMO_APK_REVISION
#define OPENMMO_APK_REVISION 0
#endif

/* Where the download waits between being proved and being streamed to the
 * installer: the app's private directory, which no other app can write, so
 * the file that was hashed is the file that is installed. */
#define APK_NAME "update.apk"

int mmo_update_notice_revision(void)
{
    return (int)OPENMMO_APK_REVISION;
}

int mmo_update_notice_available(void)
{
    return OPENMMO_PIN_FEED_URL[0] != '\0' && OPENMMO_PIN_FEED_KEY[0] != '\0'
           && (int)OPENMMO_APK_REVISION > 0;
}

/* ------------------------------------------------------------------ */
/* The state                                                           */
/* ------------------------------------------------------------------ */

enum state {
    ST_IDLE,        /* nothing asked, or an automatic ask that got no answer */
    ST_CHECKING,    /* the ask is out on its thread */
    ST_NOANSWER,    /* a hand check got no answer; msg says why */
    ST_CURRENT,     /* this build is the one the operator publishes */
    ST_BEHIND,      /* `latest` is out and newer than this build */
    ST_DOWNLOADING, /* the fetch is out on its thread */
    ST_INSTALLING,  /* handed to the installer; waiting on its verdict */
    ST_DENIED,      /* the permission page is open; waiting on the player */
    ST_FAILED       /* the download or the install failed; msg says why */
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static enum state g_state;
static int g_latest;            /* what the channel publishes */
static int g_panel;             /* the panel is on the screen */
static int g_manual;            /* the ask in flight was the button's */
static int g_have_apk;          /* the download proved itself; on disk */
static long g_got, g_total;     /* the download's progress, bytes */
static char g_msg[240];         /* the line for the state, when it has one */
static int g_asked;             /* the door's own thread only */

static void set_state(enum state st, const char *msg)
{
    pthread_mutex_lock(&g_lock);
    g_state = st;
    if (msg != NULL)
        snprintf(g_msg, sizeof g_msg, "%s", msg);
    else
        g_msg[0] = '\0';
    pthread_mutex_unlock(&g_lock);
}

static enum state get_state(void)
{
    enum state st;

    pthread_mutex_lock(&g_lock);
    st = g_state;
    pthread_mutex_unlock(&g_lock);
    return st;
}

static int apk_path(char *out, size_t cap)
{
    const char *dir = getenv("OPENMMO_INTERNAL_DIR");

    if (dir == NULL || dir[0] == '\0')
        return -1;
    snprintf(out, cap, "%s/" APK_NAME, dir);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The ask                                                             */
/* ------------------------------------------------------------------ */

static int load_key(mmo_rsa_pubkey *key)
{
    char err[MMO_FEED_MESSAGE];

    if (mmo_rsa_pubkey_text(OPENMMO_PIN_FEED_KEY,
                            strlen(OPENMMO_PIN_FEED_KEY), key,
                            err, sizeof err) != 0) {
        LOGE("the compiled-in feed key is unusable: %s", err);
        return -1;
    }
    return 0;
}

static void *ask_thread(void *unused)
{
    mmo_rsa_pubkey key;
    char err[MMO_FEED_MESSAGE];
    int latest = 0, manual;

    (void)unused;
    pthread_mutex_lock(&g_lock);
    manual = g_manual;
    pthread_mutex_unlock(&g_lock);

    if (load_key(&key) != 0) {
        set_state(manual ? ST_NOANSWER : ST_IDLE,
                  "This build's channel key is unusable.");
        return NULL;
    }
    if (mmo_update_latest_revision(OPENMMO_PIN_FEED_URL, NULL, &key, &latest,
                                   err, sizeof err) != 0) {
        /* Offline is the usual reason and it is not a fault: the player is
         * at a login screen, about to find out whether the network works. */
        LOGI("no answer from the channel: %s", err);
        set_state(manual ? ST_NOANSWER : ST_IDLE, err);
        return NULL;
    }
    LOGI("this build is r%d and the channel publishes r%d",
         (int)OPENMMO_APK_REVISION, latest);
    pthread_mutex_lock(&g_lock);
    g_latest = latest;
    if (latest <= (int)OPENMMO_APK_REVISION) {
        g_state = ST_CURRENT;
    } else {
        g_state = ST_BEHIND;
        g_panel = 1;
    }
    g_msg[0] = '\0';
    pthread_mutex_unlock(&g_lock);
    return NULL;
}

static void start_ask(int manual)
{
    pthread_t thread;

    pthread_mutex_lock(&g_lock);
    g_state = ST_CHECKING;
    g_manual = manual;
    g_msg[0] = '\0';
    pthread_mutex_unlock(&g_lock);
    if (pthread_create(&thread, NULL, ask_thread, NULL) != 0) {
        LOGE("no thread to ask the channel on");
        set_state(manual ? ST_NOANSWER : ST_IDLE, "No thread to ask on.");
        return;
    }
    pthread_detach(thread);
}

void mmo_update_notice_ask(void)
{
    char path[600];

    if (g_asked)
        return;
    g_asked = 1;
    /* A previous run's leftovers: a verdict nobody read, a package that was
     * installed (this is the new build reading it) or never was. Neither
     * may be taken for this run's. */
    if (apk_path(path, sizeof path) == 0) {
        char verdict[64];

        remove(path);
        mmo_android_install_result(verdict, sizeof verdict);
    }
    if (OPENMMO_PIN_FEED_URL[0] == '\0' || OPENMMO_PIN_FEED_KEY[0] == '\0') {
        LOGI("no channel is compiled into this build, so nothing to ask");
        return;
    }
    if ((int)OPENMMO_APK_REVISION <= 0) {
        LOGI("this build has no revision of its own, so nothing to compare");
        return;
    }
    start_ask(0);
}

/* ------------------------------------------------------------------ */
/* The download, and the hand-over                                     */
/* ------------------------------------------------------------------ */

static void fetch_tick(void *ud, long got, long total)
{
    (void)ud;
    pthread_mutex_lock(&g_lock);
    g_got = got;
    g_total = total;
    pthread_mutex_unlock(&g_lock);
}

/* The proven package to the installer. The state is INSTALLING from here
 * until the activity says otherwise (mmo_update_notice_tick). */
static void hand_over(void)
{
    char path[600];

    if (apk_path(path, sizeof path) != 0) {
        set_state(ST_FAILED, "This app has no private directory to install "
                             "from.");
        return;
    }
    set_state(ST_INSTALLING, "Handing the download to the installer.");
    if (mmo_android_install_apk(path) != 0) {
        set_state(ST_FAILED, "This build cannot reach the installer.");
        return;
    }
    LOGI("handed %s to the installer", path);
}

static void *fetch_thread(void *unused)
{
    mmo_rsa_pubkey key;
    char path[600], msg[MMO_FEED_MESSAGE], name[MMO_FEED_NAME];
    int latest = 0;

    (void)unused;
    if (apk_path(path, sizeof path) != 0) {
        set_state(ST_FAILED, "This app has no private directory to download "
                             "into.");
        return NULL;
    }
    if (load_key(&key) != 0) {
        set_state(ST_FAILED, "This build's channel key is unusable.");
        return NULL;
    }
    if (mmo_update_fetch_package(OPENMMO_PIN_FEED_URL, NULL, &key, ".apk",
                                 path, fetch_tick, NULL, &latest,
                                 name, sizeof name, msg, sizeof msg) != 0) {
        LOGE("the download failed: %s", msg);
        set_state(ST_FAILED, msg);
        return NULL;
    }
    LOGI("%s", msg);
    pthread_mutex_lock(&g_lock);
    g_have_apk = 1;
    if (latest > 0)
        g_latest = latest;
    pthread_mutex_unlock(&g_lock);
    hand_over();
    return NULL;
}

static void start_fetch(void)
{
    pthread_t thread;

    pthread_mutex_lock(&g_lock);
    g_state = ST_DOWNLOADING;
    g_have_apk = 0;
    g_got = 0;
    g_total = 0;
    g_msg[0] = '\0';
    pthread_mutex_unlock(&g_lock);
    if (pthread_create(&thread, NULL, fetch_thread, NULL) != 0) {
        LOGE("no thread to download on");
        set_state(ST_FAILED, "No thread to download on.");
        return;
    }
    pthread_detach(thread);
}

void mmo_update_notice_tick(void)
{
    char verdict[256];
    enum state st = get_state();

    if (st != ST_INSTALLING && st != ST_DENIED)
        return;
    if (!mmo_android_install_result(verdict, sizeof verdict))
        return;
    LOGI("the installer says: %s", verdict);
    if (strcmp(verdict, "pending") == 0) {
        set_state(ST_INSTALLING, "Confirm the install in the system dialog.");
    } else if (strcmp(verdict, "denied") == 0) {
        set_state(ST_DENIED, NULL);
        pthread_mutex_lock(&g_lock);
        g_panel = 1;
        pthread_mutex_unlock(&g_lock);
    } else if (strcmp(verdict, "done") == 0) {
        /* Seen only if the process outlives its own replacement, which it
         * does not; said anyway so the line is never blank. */
        set_state(ST_INSTALLING, "Installed. Open OpenMMO again.");
    } else if (strncmp(verdict, "failed", 6) == 0) {
        const char *why = verdict + 6;

        while (*why == ' ')
            why++;
        set_state(ST_FAILED, *why != '\0' ? why : "The installer refused.");
        pthread_mutex_lock(&g_lock);
        g_panel = 1;
        pthread_mutex_unlock(&g_lock);
    }
}

/* ------------------------------------------------------------------ */
/* The rail button                                                     */
/* ------------------------------------------------------------------ */

void mmo_update_notice_label(char *out, size_t cap)
{
    enum state st;
    int latest, pct;

    pthread_mutex_lock(&g_lock);
    st = g_state;
    latest = g_latest;
    pct = g_total > 0 ? (int)(g_got * 100 / g_total) : 0;
    pthread_mutex_unlock(&g_lock);
    switch (st) {
    case ST_IDLE:        snprintf(out, cap, "CHECK FOR UPDATES"); break;
    case ST_CHECKING:    snprintf(out, cap, "CHECKING..."); break;
    case ST_NOANSWER:    snprintf(out, cap, "NO ANSWER: RETRY"); break;
    case ST_CURRENT:     snprintf(out, cap, "UP TO DATE (r%d)",
                                  (int)OPENMMO_APK_REVISION); break;
    case ST_BEHIND:      snprintf(out, cap, "UPDATE TO r%d", latest); break;
    case ST_DOWNLOADING: snprintf(out, cap, "DOWNLOADING %d%%", pct); break;
    case ST_INSTALLING:  snprintf(out, cap, "INSTALLING..."); break;
    case ST_DENIED:      snprintf(out, cap, "ALLOW, THEN INSTALL"); break;
    case ST_FAILED:      snprintf(out, cap, "UPDATE FAILED"); break;
    }
}

void mmo_update_notice_press(void)
{
    if (!mmo_update_notice_available())
        return;
    switch (get_state()) {
    case ST_IDLE:
    case ST_NOANSWER:
    case ST_CURRENT:
        start_ask(1);
        break;
    case ST_CHECKING:
        break;
    default:
        pthread_mutex_lock(&g_lock);
        g_panel = 1;
        pthread_mutex_unlock(&g_lock);
        break;
    }
}

int mmo_update_notice_up(void)
{
    int up;

    pthread_mutex_lock(&g_lock);
    up = g_panel;
    pthread_mutex_unlock(&g_lock);
    return up;
}

/* ------------------------------------------------------------------ */
/* The panel                                                           */
/* ------------------------------------------------------------------ */

struct notice_rects {
    struct openmmo_rect panel, get_b, later_b;
    int pad;                     /* the inset the text shares with the buttons */
    int text_y;                  /* where the lines begin, under the title */
};

/*
 * The door's own proportions, a 440-wide frame, 20 of padding, two buttons sharing the
 * bottom row, and every one of them gives way to the screen.
 */
static void notice_layout(int sw, int sh, struct notice_rects *o)
{
    int ww = 440, wh, pad, gap, bh = 34, bw, wx, wy, stacked;

    if (ww > sw - 40)
        ww = sw - 40;
    /* A floor under the arithmetic, not a layout: below this the width goes
     * negative and starts making rectangles nothing can hit. */
    if (ww < 120)
        ww = 120;
    /* A small panel cannot afford 20 of margin on each side, and a tiny one
     * cannot afford 10: at 160 wide the difference is whether the buttons are
     * 66 across or 74, and "UPDATE" is 65 of them. */
    pad = ww < 200 ? 4 : (ww < 300 ? 10 : 20);
    gap = ww < 200 ? 4 : (ww < 300 ? 8 : 16);
    /*
     * Stacking is the better answer to a narrow screen, two full-width buttons always hold
     * their words, but only where the height can take the extra row.
     */
    stacked = ww < 174 && sh - 40 >= 150;
    wh = stacked ? 230 : 190;
    if (wh > sh - 40)
        wh = sh - 40;
    if (wh < (stacked ? 150 : 110))
        wh = stacked ? 150 : 110;
    wx = (sw - ww) / 2;
    if (wx < 0)
        wx = 0;
    wy = (sh - wh) / 2;
    if (wy < 20)
        wy = 20;
    o->panel = (struct openmmo_rect){ wx, wy, ww, wh };
    o->pad = pad;
    if (stacked) {
        bw = ww - pad * 2;
        o->get_b = (struct openmmo_rect){ wx + pad,
                                          wy + wh - pad - bh * 2 - 8, bw, bh };
        o->later_b = (struct openmmo_rect){ wx + pad, wy + wh - pad - bh,
                                            bw, bh };
    } else {
        bw = (ww - pad * 2 - gap) / 2;
        o->get_b = (struct openmmo_rect){ wx + pad, wy + wh - pad - bh,
                                          bw, bh };
        o->later_b = (struct openmmo_rect){ wx + pad + bw + gap,
                                            wy + wh - pad - bh, bw, bh };
    }
    /* The title keeps its place at the top; the lines start under it, closer
     * on a panel that has no room to breathe. */
    o->text_y = wy + (wh >= 150 ? 52 : 34);
}

/* The longest of these that fits, or nothing at all. */
static const char *fits(struct view_ui_gpu *gpu, int room, const char *first,
                        const char *second)
{
    if (view_ui_text_width(gpu, first) <= room)
        return first;
    if (second != NULL && view_ui_text_width(gpu, second) <= room)
        return second;
    return NULL;
}

/* A sentence that has no short form, the installer's reason, the fetch's
 * broken at spaces into as many lines as `room` takes, at most `max`. The
 * tail past the last line is lost, and the last line says so with a dot. */
static int wrap(struct view_ui_gpu *gpu, int room, const char *s,
                char out[][96], int max)
{
    int n = 0;

    while (*s != '\0' && n < max) {
        size_t take = strlen(s), at;
        char line[96];

        if (take >= sizeof line)
            take = sizeof line - 1;
        memcpy(line, s, take);
        line[take] = '\0';
        /* Shorten at spaces until it fits; a single word wider than the
         * room is cut mid-word, which is the honest thing at 120 wide. */
        while (view_ui_text_width(gpu, line) > room) {
            char *sp = strrchr(line, ' ');

            if (sp == NULL) {
                line[strlen(line) - 1] = '\0';
                continue;
            }
            *sp = '\0';
        }
        at = strlen(line);
        if (at == 0)
            break;
        snprintf(out[n++], 96, "%s", line);
        s += at;
        while (*s == ' ')
            s++;
    }
    if (*s != '\0' && n > 0) {
        size_t l = strlen(out[n - 1]);

        if (l > 3)
            snprintf(out[n - 1] + l - 3, 4, "...");
    }
    return n;
}

static void notice_text(SDL_Renderer *ren, struct view_ui_gpu *gpu, int x,
                        int y, const char *s, uint32_t rgb)
{
    view_ui_text(ren, gpu, x, y, s, view_ui_col(rgb));
}

/* The box always, the word only if one of its forms fits inside. A label
 * hanging over both edges of its own button is worse than a bare button, and
 * the two here are in fixed positions a player can still answer. */
static void notice_button(SDL_Renderer *ren, struct view_ui_gpu *gpu,
                          const struct openmmo_rect *r, const char *label)
{
    view_ui_fill(ren, r->x, r->y, r->w, r->h, 0x3a, 0x44, 0x50, 255);
    view_ui_border(ren, r->x, r->y, r->w, r->h, 0x5a, 0x66, 0x70);
    if (label != NULL)
        view_ui_text(ren, gpu,
                     r->x + (r->w - view_ui_text_width(gpu, label)) / 2,
                     r->y + (r->h - gpu->px) / 2, label,
                     view_ui_col(0xF2F2F2u));
}

/* What the panel says for each state: its title, up to three lines, and the
 * two button labels (long form, short form). A NULL left label is a panel
 * with one button, on the right. */
struct notice_face {
    const char *title, *title_s;
    const char *line[3];
    const char *line_s[3];
    uint32_t colour[3];
    const char *left, *left_s, *right, *right_s;
    int bar;                     /* a progress bar under the first line */
};

static void notice_face(struct notice_face *f, enum state st, int latest,
                        long got, long total, const char *msg,
                        char scratch[][96])
{
    memset(f, 0, sizeof *f);
    f->colour[0] = f->colour[1] = 0xF2F2F2u;
    f->colour[2] = 0x929AA2u;
    switch (st) {
    case ST_BEHIND:
        f->title = "UPDATE AVAILABLE"; f->title_s = "UPDATE";
        snprintf(scratch[0], 96, "This app is r%d and r%d is out.",
                 (int)OPENMMO_APK_REVISION, latest);
        snprintf(scratch[1], 96, "r%d -> r%d",
                 (int)OPENMMO_APK_REVISION, latest);
        f->line[0] = scratch[0]; f->line_s[0] = scratch[1];
        f->line[1] = "Download it and the installer takes over.";
        f->line_s[1] = "Download and install it.";
        f->line[2] = "Playing on an old build may not work.";
        f->left = "UPDATE"; f->left_s = "UPDATE";
        f->right = "REMIND ME LATER"; f->right_s = "LATER";
        break;
    case ST_DOWNLOADING:
        f->title = "UPDATING"; f->title_s = "UPDATE";
        snprintf(scratch[0], 96, "Downloading r%d: %d%%", latest,
                 total > 0 ? (int)(got * 100 / total) : 0);
        snprintf(scratch[1], 96, "%ld%%",
                 total > 0 ? got * 100 / total : 0);
        f->line[0] = scratch[0]; f->line_s[0] = scratch[1];
        f->bar = 1;
        snprintf(scratch[2], 96, "%ld of %ld KB", (got + 1023) / 1024,
                 (total + 1023) / 1024);
        f->line[1] = scratch[2];
        f->line[2] = "The install is asked for when this is done.";
        f->right = "HIDE"; f->right_s = "HIDE";
        break;
    case ST_INSTALLING:
        /*
         * What the installer does on a self-update, measured on the RG556 (2026-09-07): the
         * confirmation, possibly a Play Protect scan offer in front of it the first time, then
         * the app is stopped and the device is left on its home screen.
         */
        f->title = "INSTALLING"; f->title_s = "INSTALL";
        f->line[0] = msg;
        snprintf(scratch[0], 96, "OpenMMO closes when done; open it again "
                                 "as r%d.", latest);
        snprintf(scratch[1], 96, "Then open OpenMMO again.");
        f->line[1] = scratch[0]; f->line_s[1] = scratch[1];
        f->line[2] = "Lost the dialog? Ask for it again.";
        f->left = "INSTALL AGAIN"; f->left_s = "AGAIN";
        f->right = "HIDE"; f->right_s = "HIDE";
        break;
    case ST_DENIED:
        f->title = "PERMISSION NEEDED"; f->title_s = "ALLOW";
        f->line[0] = "Android asks once before an app may install";
        f->line_s[0] = "Android asks once.";
        f->line[1] = "updates. Allow OpenMMO on the page that opened,";
        f->line_s[1] = "Allow OpenMMO there,";
        f->line[2] = "then press INSTALL.";
        f->colour[2] = 0xF2F2F2u;
        f->left = "INSTALL"; f->left_s = "INSTALL";
        f->right = "LATER"; f->right_s = "LATER";
        break;
    case ST_FAILED:
        f->title = "UPDATE FAILED"; f->title_s = "FAILED";
        f->line[0] = msg;            /* wrapped by the drawer */
        f->left = "TRY AGAIN"; f->left_s = "RETRY";
        f->right = "CLOSE"; f->right_s = "CLOSE";
        break;
    default:
        break;
    }
}

void mmo_update_notice_draw(SDL_Renderer *ren, struct view_ui_gpu *gpu,
                            int sw, int sh)
{
    struct notice_rects R;
    struct notice_face F;
    char scratch[3][96], msg[sizeof g_msg], wrapped[3][96];
    enum state st;
    SDL_BlendMode old_blend;
    int latest, room, y, i;
    long got, total;

    pthread_mutex_lock(&g_lock);
    if (!g_panel) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    st = g_state;
    latest = g_latest;
    got = g_got;
    total = g_total;
    memcpy(msg, g_msg, sizeof msg);
    pthread_mutex_unlock(&g_lock);
    notice_layout(sw, sh, &R);
    notice_face(&F, st, latest, got, total, msg, scratch);
    if (F.title == NULL)
        return;

    /* The door goes dim behind it, which is what says the panel has to be
     * answered before anything else on the screen will do anything. The
     * blend mode is asked for, not assumed: the shim draws a fill opaque
     * unless it is, and an opaque black fill is a door that vanished. */
    SDL_GetRenderDrawBlendMode(ren, &old_blend);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    view_ui_fill(ren, 0, 0, sw, sh, 0, 0, 0, 150);
    view_ui_fill(ren, R.panel.x, R.panel.y, R.panel.w, R.panel.h,
                 28, 32, 38, 245);
    SDL_SetRenderDrawBlendMode(ren, old_blend);
    view_ui_border(ren, R.panel.x, R.panel.y, R.panel.w, R.panel.h,
                   74, 85, 96);

    room = R.panel.w - R.pad * 2;
    {
        const char *title = fits(gpu, room, F.title, F.title_s);

        if (title != NULL)
            notice_text(ren, gpu,
                        R.panel.x + (R.panel.w
                                     - view_ui_text_width(gpu, title)) / 2,
                        R.panel.y + 12, title, 0xFFFFFFu);
    }

    /* A failure's reason is one sentence of unknown length, and it is the
     * whole point of the panel: wrapped into the three line slots rather
     * than fitted or dropped. */
    if (st == ST_FAILED) {
        int n = wrap(gpu, room, msg[0] != '\0' ? msg
                                              : "The update did not finish.",
                     wrapped, 3);

        for (i = 0; i < 3; i++) {
            F.line[i] = i < n ? wrapped[i] : NULL;
            F.line_s[i] = NULL;
            F.colour[i] = 0xF2F2F2u;
        }
    }

    /*
     * Three lines in the order they would be missed. A short panel keeps as many as fit above
     * the buttons and drops the rest from the bottom, so the one that survives everywhere is
     * the first.
     */
    y = R.text_y;
    for (i = 0; i < 3; i++) {
        const char *s;

        /* Nothing may reach the button row: text over a tap target is a
         * button a player cannot read and presses anyway. */
        if (y + gpu->px > R.later_b.y - 6)
            break;
        if (F.line[i] == NULL) {
            y += 24;
            continue;
        }
        s = fits(gpu, room, F.line[i], F.line_s[i]);
        if (s != NULL)
            notice_text(ren, gpu, R.panel.x + R.pad, y, s, F.colour[i]);
        y += 24;
        if (i == 0 && F.bar && y + 14 <= R.later_b.y - 6) {
            int w = total > 0 ? (int)((long long)room * got / total) : 0;

            view_ui_fill(ren, R.panel.x + R.pad, y, room, 10,
                         0x16, 0x1a, 0x20, 255);
            if (w > 0)
                view_ui_fill(ren, R.panel.x + R.pad, y, w, 10,
                             0x81, 0xDD, 0xF1, 255);
            view_ui_border(ren, R.panel.x + R.pad, y, room, 10,
                           0x4a, 0x55, 0x60);
            y += 16;
        }
    }

    if (F.left != NULL)
        notice_button(ren, gpu, &R.get_b,
                      fits(gpu, R.get_b.w - 8, F.left, F.left_s));
    notice_button(ren, gpu, &R.later_b,
                  fits(gpu, R.later_b.w - 8, F.right, F.right_s));
}

int mmo_update_notice_tap(int x, int y, int sw, int sh)
{
    struct notice_rects R;
    enum state st;
    int have_apk;

    if (!mmo_update_notice_up())
        return 0;
    notice_layout(sw, sh, &R);
    pthread_mutex_lock(&g_lock);
    st = g_state;
    have_apk = g_have_apk;
    pthread_mutex_unlock(&g_lock);

    if (view_ui_hit(&R.get_b, x, y)) {
        switch (st) {
        case ST_BEHIND:
            start_fetch();
            break;
        case ST_INSTALLING:
        case ST_DENIED:
            hand_over();
            break;
        case ST_FAILED:
            /* The download proved itself and the install is what failed:
             * ask the installer again. Otherwise the download is the retry,
             * and it fetches the channel afresh on its way. */
            if (have_apk)
                hand_over();
            else
                start_fetch();
            break;
        default:
            break;
        }
    } else if (view_ui_hit(&R.later_b, x, y)
               || !view_ui_hit(&R.panel, x, y)) {
        /* Down, whichever state. The rail button keeps saying where things
         * stand and brings the panel back; nothing running is stopped. */
        pthread_mutex_lock(&g_lock);
        g_panel = 0;
        pthread_mutex_unlock(&g_lock);
    }
    /* Every tap, not just the ones that land on a button: see the header. */
    return 1;
}

/*
 * The fallbacks, for a link with no frontend to ask, a test binary, or an ordering accident.
 */
__attribute__((weak)) int mmo_android_open_url(const char *url)
{
    LOGE("this build has no way to open %s", url);
    return -1;
}

__attribute__((weak)) int mmo_android_install_apk(const char *path)
{
    LOGE("this build has no way to install %s", path);
    return -1;
}

__attribute__((weak)) int mmo_android_install_result(char *out, size_t cap)
{
    (void)out;
    (void)cap;
    return 0;
}
