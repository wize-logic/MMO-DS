/* Counting the engine's verdicts and keeping the answer. */
#include <android/log.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mmo_calibrate.h"
#include "platform.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "openmmo", __VA_ARGS__)

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct mmo_calibration g_base;      /* the file as loaded */
static int g_loaded;
static int g_level;                        /* this session's hd3d */
static int g_rule;                         /* the rules' answer, for a raise */
static unsigned g_blocks;                  /* pc-pace blocks seen, boot included */
static unsigned g_paced, g_late, g_skipped, g_drops;
static double g_seconds;
static unsigned g_last_flush_block;

static void cal_path(char *out, size_t cap)
{
    char base[512];

    out[0] = '\0';
    if (mmo_plat_config_home(base, sizeof base) != 0) {
        return;
    }
    snprintf(out, cap, "%s%sopenmmo", base, mmo_plat_sep());
    mmo_plat_mkdir(out);
    snprintf(out, cap, "%s%sopenmmo%scalibration", base, mmo_plat_sep(),
             mmo_plat_sep());
}

void mmo_calibrate_load(void)
{
    char path[560], line[128];
    FILE *f;

    pthread_mutex_lock(&g_lock);
    if (g_loaded) {
        pthread_mutex_unlock(&g_lock);
        return;
    }
    g_loaded = 1;
    memset(&g_base, 0, sizeof g_base);
    cal_path(path, sizeof path);
    f = path[0] != '\0' ? fopen(path, "r") : NULL;
    if (f != NULL) {
        while (fgets(line, sizeof line, f) != NULL) {
            char key[32];
            long v;

            if (sscanf(line, "%31s %ld", key, &v) != 2) {
                continue;
            }
            if (strcmp(key, "cap") == 0) g_base.cap = (int)v;
            else if (strcmp(key, "good") == 0) g_base.good_runs = (int)v;
            else if (strcmp(key, "level") == 0) g_base.level = (int)v;
            else if (strcmp(key, "seconds") == 0) g_base.seconds = (unsigned)v;
            else if (strcmp(key, "paced") == 0) g_base.paced = (unsigned)v;
            else if (strcmp(key, "late") == 0) g_base.late = (unsigned)v;
            else if (strcmp(key, "skipped") == 0) g_base.skipped = (unsigned)v;
            else if (strcmp(key, "drops") == 0) g_base.drops = (unsigned)v;
        }
        fclose(f);
        if (g_base.cap < 2 || g_base.cap > 4) {
            g_base.cap = 0;
        }
        LOGI("calibration: %s, cap %d, last session hd3d %d for %u s"
             " (%u of %u late, %u skipped, %u step-downs)",
             path, g_base.cap, g_base.level, g_base.seconds, g_base.late,
             g_base.paced, g_base.skipped, g_base.drops);
    } else {
        LOGI("calibration: none yet%s%s", path[0] ? " at " : "", path);
    }
    pthread_mutex_unlock(&g_lock);
}

int mmo_calibrate_cap(void)
{
    int cap;

    mmo_calibrate_load();
    pthread_mutex_lock(&g_lock);
    cap = g_base.cap;
    pthread_mutex_unlock(&g_lock);
    return cap;
}

void mmo_calibrate_session_level(int hd3d)
{
    struct mmo_device_defaults d;
    struct mmo_device_facts f;

    /* The rules' answer with no cap applied is the ceiling a raise may
     * reach; the facts are the device's, the cap left out on purpose. */
    f = *mmo_device_facts_for_rules();
    f.cal_cap = 0;
    mmo_device_decide(&f, &d);
    pthread_mutex_lock(&g_lock);
    g_level = hd3d;
    g_rule = d.hd3d;
    pthread_mutex_unlock(&g_lock);
}

void mmo_calibrate_note_line(const char *line)
{
    unsigned late, paced, skipped;
    double fps, secs;
    int flush = 0;

    if (strncmp(line, "pc-pace: ", 9) != 0) {
        return;
    }
    pthread_mutex_lock(&g_lock);
    if (sscanf(line, "pc-pace: %lf fps over %lfs (%u of %u frames late,"
                     " %u pictures skipped)",
               &fps, &secs, &late, &paced, &skipped) == 5) {
        g_blocks++;
        if (g_blocks > 2) {
            g_paced += paced;
            g_late += late;
            g_skipped += skipped;
            g_seconds += secs;
        }
        /* Once a minute, because a killed app never reaches its end. */
        if (g_blocks - g_last_flush_block >= 12) {
            g_last_flush_block = g_blocks;
            flush = 1;
        }
    } else if (strncmp(line, "pc-pace: the 3D layer cannot hold", 33) == 0) {
        if (g_blocks > 2) {
            g_drops++;
        }
    }
    pthread_mutex_unlock(&g_lock);
    if (flush) {
        mmo_calibrate_flush();
    }
}

void mmo_calibrate_flush(void)
{
    static const char *const words[] = {
        "too short to count", "the cap came down", "clean",
        "the cap went up", "held"
    };
    struct mmo_calibration c;
    enum mmo_cal_verdict v;
    char path[560];
    FILE *f;
    int level, rule;
    unsigned paced, late, skipped, drops, seconds;

    mmo_calibrate_load();
    pthread_mutex_lock(&g_lock);
    c = g_base;
    level = g_level; rule = g_rule;
    paced = g_paced; late = g_late; skipped = g_skipped; drops = g_drops;
    seconds = (unsigned)g_seconds;
    pthread_mutex_unlock(&g_lock);

    if (level < 2 || paced == 0) {
        return;
    }
    v = mmo_device_calibrate(&c, level, paced, late, skipped, drops, seconds,
                             rule);
    if (v == MMO_CAL_TOO_SHORT) {
        return;
    }
    cal_path(path, sizeof path);
    if (path[0] == '\0' || (f = fopen(path, "w")) == NULL) {
        return;
    }
    fprintf(f, "# what this device held; read by mmo_calibrate.c\n");
    fprintf(f, "cap %d\ngood %d\nlevel %d\nseconds %u\npaced %u\nlate %u\n"
               "skipped %u\ndrops %u\n",
            c.cap, c.good_runs, c.level, c.seconds, c.paced, c.late,
            c.skipped, c.drops);
    fclose(f);
    LOGI("calibration: hd3d %d for %u s, %u of %u late, %u skipped,"
         " %u step-downs, %s (cap %d, rule %d)",
         level, seconds, late, paced, skipped, drops, words[v], c.cap, rule);
}
