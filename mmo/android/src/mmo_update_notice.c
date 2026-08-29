/* The app finds out it is behind, and says so once. */

#include "mmo_update_notice.h"

#include <android/log.h>
#include <pthread.h>
#include <stdio.h>
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
 * Where a new build actually comes from. The channel says what is current; this says where to
 * get it, and they are deliberately two different places.
 */
#ifndef OPENMMO_RELEASES_URL
#define OPENMMO_RELEASES_URL \
    "https://github.com/wize-logic/OpenMMO-DS/releases/tag/release"
#endif

/*
 * This build'S own REVISION, compiled in by Makefile.android from the same commit count the
 * APK's versionCode and the release archive are named by.
 */
#ifndef OPENMMO_APK_REVISION
#define OPENMMO_APK_REVISION 0
#endif

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_up;                /* the answer came back, and it was newer */
static int g_latest;            /* what the channel publishes */
static int g_asked;             /* the door's own thread only */

/* ------------------------------------------------------------------ */
/* The ask                                                             */
/* ------------------------------------------------------------------ */

static void *ask_thread(void *unused)
{
    mmo_rsa_pubkey key;
    char err[MMO_FEED_MESSAGE];
    int latest = 0;

    (void)unused;
    if (mmo_rsa_pubkey_text(OPENMMO_PIN_FEED_KEY,
                            strlen(OPENMMO_PIN_FEED_KEY), &key,
                            err, sizeof err) != 0) {
        LOGE("the compiled-in feed key is unusable: %s", err);
        return NULL;
    }
    if (mmo_update_latest_revision(OPENMMO_PIN_FEED_URL, &key, &latest,
                                   err, sizeof err) != 0) {
        /* Offline is the usual reason and it is not a fault: the player is
         * at a login screen, about to find out whether the network works. */
        LOGI("no answer from the channel: %s", err);
        return NULL;
    }
    LOGI("this build is r%d and the channel publishes r%d",
         (int)OPENMMO_APK_REVISION, latest);
    if (latest <= (int)OPENMMO_APK_REVISION)
        return NULL;
    pthread_mutex_lock(&g_lock);
    g_latest = latest;
    g_up = 1;
    pthread_mutex_unlock(&g_lock);
    return NULL;
}

void mmo_update_notice_ask(void)
{
    pthread_t thread;

    if (g_asked)
        return;
    g_asked = 1;
    if (OPENMMO_PIN_FEED_URL[0] == '\0' || OPENMMO_PIN_FEED_KEY[0] == '\0') {
        LOGI("no channel is compiled into this build, so nothing to ask");
        return;
    }
    if ((int)OPENMMO_APK_REVISION <= 0) {
        LOGI("this build has no revision of its own, so nothing to compare");
        return;
    }
    if (pthread_create(&thread, NULL, ask_thread, NULL) != 0) {
        LOGE("no thread to ask the channel on");
        return;
    }
    pthread_detach(thread);
}

int mmo_update_notice_up(void)
{
    int up;

    pthread_mutex_lock(&g_lock);
    up = g_up;
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

void mmo_update_notice_draw(SDL_Renderer *ren, struct view_ui_gpu *gpu,
                            int sw, int sh)
{
    struct notice_rects R;
    char line[96];
    int latest;

    if (!mmo_update_notice_up())
        return;
    pthread_mutex_lock(&g_lock);
    latest = g_latest;
    pthread_mutex_unlock(&g_lock);
    notice_layout(sw, sh, &R);

    /* The door goes dim behind it, which is what says the panel has to be
     * answered before anything else on the screen will do anything. */
    view_ui_fill(ren, 0, 0, sw, sh, 0, 0, 0, 150);
    view_ui_fill(ren, R.panel.x, R.panel.y, R.panel.w, R.panel.h,
                 28, 32, 38, 245);
    view_ui_border(ren, R.panel.x, R.panel.y, R.panel.w, R.panel.h,
                   74, 85, 96);

    {
        const char *title = fits(gpu, R.panel.w - R.pad * 2,
                                 "UPDATE AVAILABLE", "UPDATE");

        if (title != NULL)
            notice_text(ren, gpu,
                        R.panel.x + (R.panel.w
                                     - view_ui_text_width(gpu, title)) / 2,
                        R.panel.y + 12, title, 0xFFFFFFu);
    }
    /*
     * Three lines in the order they would be missed: which revisions, where the new one is,
     * and why it matters. A short panel keeps as many as fit above the buttons and drops the
     * rest from the bottom, so the one that survives everywhere is the pair of numbers.
     */
    {
        int room = R.panel.w - R.pad * 2, y = R.text_y, i;
        char shorter[32];

        snprintf(line, sizeof line, "This app is r%d and r%d is out.",
                 (int)OPENMMO_APK_REVISION, latest);
        snprintf(shorter, sizeof shorter, "r%d -> r%d",
                 (int)OPENMMO_APK_REVISION, latest);
        for (i = 0; i < 3; i++) {
            const char *s = NULL;

            /* Nothing may reach the button row: text over a tap target is a
             * button a player cannot read and presses anyway. */
            if (y + gpu->px > R.get_b.y - 6)
                break;
            if (i == 0)
                s = fits(gpu, room, line, shorter);
            else if (i == 1)
                s = fits(gpu, room, "The releases page has the new build.",
                         "Get it from the releases page.");
            else
                s = fits(gpu, room, "Playing on an old build may not work.",
                         NULL);
            if (s != NULL)
                notice_text(ren, gpu, R.panel.x + R.pad, y, s,
                            i == 2 ? 0x929AA2u : 0xF2F2F2u);
            y += 24;
        }
    }

    notice_button(ren, gpu, &R.get_b,
                  fits(gpu, R.get_b.w - 8, "GET THE UPDATE", "UPDATE"));
    notice_button(ren, gpu, &R.later_b,
                  fits(gpu, R.later_b.w - 8, "REMIND ME LATER", "LATER"));
}

int mmo_update_notice_tap(int x, int y, int sw, int sh)
{
    struct notice_rects R;

    if (!mmo_update_notice_up())
        return 0;
    notice_layout(sw, sh, &R);
    if (view_ui_hit(&R.get_b, x, y)) {
        LOGI("opening %s", OPENMMO_RELEASES_URL);
        if (mmo_android_open_url(OPENMMO_RELEASES_URL) != 0)
            LOGE("nothing on this device would open %s",
                 OPENMMO_RELEASES_URL);
        /* Down either way. The browser is a different app and coming back to
         * a panel still demanding an answer reads as a button that did
         * nothing; the reminder returns on the next start regardless. */
        pthread_mutex_lock(&g_lock);
        g_up = 0;
        pthread_mutex_unlock(&g_lock);
    } else if (view_ui_hit(&R.later_b, x, y)) {
        pthread_mutex_lock(&g_lock);
        g_up = 0;
        pthread_mutex_unlock(&g_lock);
    }
    /* Every tap, not just the two that land on a button: see the header. */
    return 1;
}

/*
 * The fallback, for a link with no frontend to ask, a test binary, or an ordering accident.
 */
__attribute__((weak)) int mmo_android_open_url(const char *url)
{
    LOGE("this build has no way to open %s", url);
    return -1;
}
