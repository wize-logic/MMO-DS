/* What a device is, and what the app should default to on it. */
#ifndef MMO_DEVICE_RULES_H
#define MMO_DEVICE_RULES_H

struct mmo_device_facts {
    int width_px, height_px;   /* the surface, long side first */
    int density_dpi;           /* 0 when unknown */
    int cpus;                  /* online cores; 0 when unknown */
    int big_cpus;              /* cores at half the top cpu_capacity or more,
                                * or failing that whose ceiling is within 80%
                                * of the fastest one's; 0 when neither the
                                * capacity nor cpufreq is readable */
    int max_khz;               /* the fastest core's ceiling; 0 unknown */
    long ram_mb;               /* 0 unknown */
    int low_ram;               /* ro.config.low_ram, or 1 when ram_mb < 2048 */
    int soft_gl;               /* the GL renderer is a software rasteriser */
    char gpu[64];              /* GL_RENDERER, or "" before the context is up */
    int cal_cap;               /* the calibration's hd3d cap, 0 for none */
};

/*
 * WHAT EARLIER SESSIONS MEASURED. The rules above say what a device SHOULD hold; this is what
 * it did.
 */
struct mmo_calibration {
    int cap;                   /* highest hd3d this device has held, 0 none */
    int good_runs;             /* clean sessions in a row at the cap */
    int level;                 /* the last measured session's hd3d */
    unsigned seconds;          /* ...and how long it ran */
    unsigned paced, late, skipped, drops;   /* its counts */
};

/* The verdict on one session's counts, for the log. */
enum mmo_cal_verdict {
    MMO_CAL_TOO_SHORT,         /* nothing recorded */
    MMO_CAL_BAD,               /* the cap came down */
    MMO_CAL_CLEAN,             /* counted towards stepping back up */
    MMO_CAL_RAISED,            /* the cap went up one */
    MMO_CAL_HELD               /* neither: an ordinary session */
};

/* Fold one session into `c`. `level` is the hd3d it ran at, `rule` the
 * rules' own answer for the device (the ceiling a raise may reach). */
enum mmo_cal_verdict mmo_device_calibrate(struct mmo_calibration *c,
                                          int level, unsigned paced,
                                          unsigned late, unsigned skipped,
                                          unsigned drops, unsigned seconds,
                                          int rule);

struct mmo_device_defaults {
    int threads;               /* PC_THREADS, 1..8 */
    int hd3d;                  /* PC_HD3D, 2..4 (the door's own range) */
    int ui_scale;              /* OPENMMO_UI_SCALE, 1..4 */
    int glass;                 /* the hd3d the panel can show, 2..4, for the log */
    char why[200];             /* one line: which rule set each number */
};

/* The rules. `f` may be partly zero (unknown); every rule has an answer for
 * that which is the app's historical constant. */
void mmo_device_decide(const struct mmo_device_facts *f,
                       struct mmo_device_defaults *d);

/* Whether a GL_RENDERER string names a software rasteriser. */
int mmo_device_gl_is_soft(const char *renderer);

/* The probe's facts, for the calibrator; defined in mmo_device.c. */
const struct mmo_device_facts *mmo_device_facts_for_rules(void);

/* How many of `n` cores are "big": their ceilings within 80% of the top
 * one's. Zero ceilings are unknown and count as small. The fallback. */
int mmo_device_big_cores(const int *max_khz, int n);

/* The same question put to the kernel's own cpu_capacity, which is what
 * tells an A55 clocked like an A76 from one: half the top core's or more is
 * big. Zero when nothing is readable, and the ceilings decide instead. */
int mmo_device_big_cores_by_capacity(const int *capacity, int n);

#endif
