/* The defaults a device earns, as arithmetic. */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "mmo_device_rules.h"

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* strcasestr, spelled out: glibc hides it behind _GNU_SOURCE and the suite
 * compiles without that. */
static int has_word(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    const char *p;

    for (p = hay; *p != '\0'; p++) {
        size_t i;

        for (i = 0; i < n; i++) {
            if (p[i] == '\0'
                || tolower((unsigned char)p[i])
                       != tolower((unsigned char)needle[i])) {
                break;
            }
        }
        if (i == n) {
            return 1;
        }
    }
    return 0;
}

int mmo_device_gl_is_soft(const char *renderer)
{
    /*
     * The names software rasterisers give themselves: SwiftShader is the emulator's default
     * and Android's own fallback, llvmpipe and softpipe are Mesa's, and "Software" is what a
     * few vendors' fallback strings say.
     */
    static const char *const soft[] = {
        "SwiftShader", "llvmpipe", "softpipe", "Software", NULL
    };
    int i;

    if (renderer == NULL) {
        return 0;
    }
    for (i = 0; soft[i] != NULL; i++) {
        if (has_word(renderer, soft[i])) {
            return 1;
        }
    }
    return 0;
}

int mmo_device_big_cores(const int *max_khz, int n)
{
    int top = 0, big = 0, i;

    for (i = 0; i < n; i++) {
        if (max_khz[i] > top) {
            top = max_khz[i];
        }
    }
    /* Below 100 MHz is not a ceiling anybody's core has: the emulator's
     * kernel answers "2" for every one of its vCPUs, which would have made
     * them all big of one another. Unknown, the same as unreadable. */
    if (top < 100000) {
        return 0;
    }
    /*
     * Within 80% of the fastest. The RG556's 2.3 GHz A76s sit at 85% of its one 2.7 GHz core
     * and are big.
     */
    for (i = 0; i < n; i++) {
        if (max_khz[i] * 5 >= top * 4) {
            big++;
        }
    }
    return big;
}

int mmo_device_big_cores_by_capacity(const int *capacity, int n)
{
    int top = 0, big = 0, i;

    for (i = 0; i < n; i++) {
        if (capacity[i] > top) {
            top = capacity[i];
        }
    }
    if (top <= 0) {
        return 0;
    }
    /* Half the top core's capacity or more. */
    for (i = 0; i < n; i++) {
        if (capacity[i] > 0 && capacity[i] * 2 >= top) {
            big++;
        }
    }
    return big;
}

void mmo_device_decide(const struct mmo_device_facts *f,
                       struct mmo_device_defaults *d)
{
    int short_px, cpu_cap, n = 0;
    char part[3][64];

    memset(d, 0, sizeof *d);

    /* ---------------------------------------------------- render threads */
    if (f->cpus <= 0) {
        d->threads = 4;
        snprintf(part[0], sizeof part[0], "threads 4 (cores unknown)");
    } else if (f->big_cpus > 0) {
        d->threads = clampi(f->big_cpus, 1, 8);
        if (d->threads > f->cpus - 2) {
            d->threads = clampi(f->cpus - 2, 1, 8);
        }
        snprintf(part[0], sizeof part[0], "threads %d (%d big of %d cores)",
                 d->threads, f->big_cpus, f->cpus);
    } else {
        d->threads = clampi(f->cpus - 2, 1, 4);
        snprintf(part[0], sizeof part[0],
                 "threads %d (%d cores, topology unknown)", d->threads,
                 f->cpus);
    }

    /* ---------------------------------------------------------------- hd3d */
    short_px = f->width_px < f->height_px ? f->width_px : f->height_px;
    d->glass = short_px > 0 ? clampi(short_px / 192, 2, 4) : 2;
    if (f->soft_gl) {
        cpu_cap = 2;
    } else if (f->cpus <= 0) {
        cpu_cap = 2;
    } else {
        cpu_cap = d->threads >= 4 ? 3 : 2;
    }
    d->hd3d = d->glass < cpu_cap ? d->glass : cpu_cap;
    /* And what the device has actually held, when a session has said. The
     * cap never raises the rule's answer; it only takes back a level the
     * evidence says this device could not keep. */
    if (f->cal_cap >= 2 && f->cal_cap < d->hd3d) {
        d->hd3d = f->cal_cap;
    }
    snprintf(part[1], sizeof part[1], "hd3d %d (glass %d, workers %d%s%s)",
             d->hd3d, d->glass, cpu_cap, f->soft_gl ? ", software gl" : "",
             f->cal_cap >= 2 && f->cal_cap < cpu_cap && f->cal_cap < d->glass
                 ? ", measured cap" : "");

    /* ------------------------------------------------------------ ui scale */
    if (f->density_dpi <= 0) {
        d->ui_scale = 2;
        snprintf(part[2], sizeof part[2], "ui 2 (density unknown)");
    } else {
        d->ui_scale = clampi((f->density_dpi + 90) / 180, 1, 4);
        while (d->ui_scale > 1 && short_px > 0
               && short_px / d->ui_scale < 320) {
            d->ui_scale--;
        }
        snprintf(part[2], sizeof part[2], "ui %d (%d dpi, %d rows)",
                 d->ui_scale, f->density_dpi,
                 short_px > 0 ? short_px / d->ui_scale : 0);
    }

    n = snprintf(d->why, sizeof d->why, "%s, %s, %s", part[0], part[1],
                 part[2]);
    (void)n;
}

/* ---------------------------------------------------------- calibration */
enum mmo_cal_verdict mmo_device_calibrate(struct mmo_calibration *c,
                                          int level, unsigned paced,
                                          unsigned late, unsigned skipped,
                                          unsigned drops, unsigned seconds,
                                          int rule)
{
    unsigned long late_ppt, skip_ppt;
    int bad, clean;

    if (seconds < 60 || paced < 1800 || level < 2) {
        return MMO_CAL_TOO_SHORT;
    }
    late_ppt = (unsigned long)late * 1000 / paced;
    skip_ppt = (unsigned long)skipped * 1000 / paced;
    bad = late_ppt >= 30 || skip_ppt >= 20 || drops * 60 > seconds;
    clean = late_ppt < 5 && skip_ppt < 2 && drops == 0;

    c->level = level;
    c->seconds = seconds;
    c->paced = paced;
    c->late = late;
    c->skipped = skipped;
    c->drops = drops;

    if (bad) {
        int down = level - 1 < 2 ? 2 : level - 1;

        if (c->cap == 0 || down < c->cap) {
            c->cap = down;
        }
        c->good_runs = 0;
        return MMO_CAL_BAD;
    }
    if (clean && c->cap >= 2 && level >= c->cap && c->cap < rule) {
        c->good_runs++;
        if (c->good_runs >= 2) {
            c->cap++;
            c->good_runs = 0;
            return MMO_CAL_RAISED;
        }
        return MMO_CAL_CLEAN;
    }
    c->good_runs = 0;
    return clean ? MMO_CAL_CLEAN : MMO_CAL_HELD;
}
