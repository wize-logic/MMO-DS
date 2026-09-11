/* The defaults each known device earns, pinned. */
#include <stdio.h>
#include <string.h>

#include "mmo_device_rules.h"

static int failures;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) {                                                             \
            printf("  ok   %s\n", msg);                                         \
        } else {                                                                \
            printf("  FAIL %s\n", msg);                                         \
            failures++;                                                         \
        }                                                                       \
    } while (0)

struct known {
    const char *name;
    struct mmo_device_facts f;
    int threads, hd3d, ui_scale, glass;
};

static void expect(const struct known *k)
{
    struct mmo_device_defaults d;
    char msg[256];

    mmo_device_decide(&k->f, &d);
    snprintf(msg, sizeof msg, "%s: threads %d hd3d %d ui %d glass %d, %s",
             k->name, d.threads, d.hd3d, d.ui_scale, d.glass, d.why);
    CHECK(d.threads == k->threads && d.hd3d == k->hd3d
          && d.ui_scale == k->ui_scale && d.glass == k->glass, msg);
    if (d.threads != k->threads || d.hd3d != k->hd3d
        || d.ui_scale != k->ui_scale || d.glass != k->glass) {
        printf("       wanted threads %d hd3d %d ui %d glass %d\n",
               k->threads, k->hd3d, k->ui_scale, k->glass);
    }
}

int device_tests_run(void)
{
    static const struct known table[] = {
        /* The device the constants came from: 1920x1080 at 403 dpi, four
         * A76s over four A55s. Threads 4 (worker-pool-must-not-take-every-
         * core), ui 2 ("the 403 dpi answer"), and HD: the overworld holds
         * 60 there with 7.7 ms of slack (android-hd3d-envelope). */
        { "rg556", { 1920, 1080, 403, 8, 4, 2700000, 7600, 0, 0, "Mali-G57", 0 },
          4, 3, 2, 4 },
        /* A 720p phone with two big cores: two workers, SD, scale 2 leaves
         * 360 rows. */
        { "phone 720p 2+6", { 1600, 720, 320, 8, 2, 2400000, 3800, 0, 0,
                              "Adreno (TM) 610", 0 },
          2, 2, 2, 3 },
        /* A cheap 1280x800 tablet, four equal cores at 160 dpi: the pool is
         * cpus-minus-two, and two workers keep SD; scale 1. */
        { "tablet 800p 4x", { 1280, 800, 160, 4, 4, 2000000, 2900, 0, 0,
                              "PowerVR GE8300", 0 },
          2, 2, 1, 4 },
        /* A 4:3 tablet at 320 dpi with eight equal cores: six workers, HD
         * (the glass could show ULTRA; the default never does). */
        { "tablet 4:3 8x", { 2048, 1536, 320, 8, 8, 2000000, 5800, 0, 0,
                             "Mali-G52", 0 },
          6, 3, 2, 4 },
        /* A flagship: one prime, three big, four little, 1440 rows at 560
         * dpi. Four workers, HD, scale 3 leaves 480 rows. */
        { "flagship 1440p", { 3120, 1440, 560, 8, 4, 3000000, 11800, 0, 0,
                              "Adreno (TM) 740", 0 },
          4, 3, 3, 4 },
        /* The emulator's software GL on a strong host: however many cores,
         * SD. */
        { "emulator swiftshader 8x", { 2400, 1080, 420, 8, 8, 0, 3800, 0, 1,
              "Android Emulator OpenGL ES Translator (Google SwiftShader)", 0 },
          6, 2, 2, 4 },
        /* One core and two cores: one worker either way, and SD. */
        { "one core", { 1280, 720, 240, 1, 1, 1500000, 1900, 1, 0, "", 0 },
          1, 2, 1, 3 },
        { "two cores", { 1280, 720, 240, 2, 2, 1500000, 1900, 1, 0, "", 0 },
          1, 2, 1, 3 },
        /* Topology unreadable on an eight-core: cpus-minus-two capped at the
         * proven four, and four workers earn HD. */
        { "8 cores, cpufreq hidden", { 1920, 1080, 403, 8, 0, 0, 7600, 0, 0,
                                       "", 0 },
          4, 3, 2, 4 },
        /* A device that says nothing at all gets the old constants. */
        { "unknown", { 0, 0, 0, 0, 0, 0, 0, 0, 0, "", 0 }, 4, 2, 2, 2 },
        /* A 640 dpi 1440p phone: round(640/180) is 4 and 1440/4 = 360 rows
         * stays above the floor. */
        { "phone 640 dpi", { 3200, 1440, 640, 8, 4, 2800000, 7600, 0, 0,
                             "Mali-G78", 0 },
          4, 3, 4, 4 },
        /* The same density on a 1080-row panel: 4 would leave 270 rows, so
         * it steps back to 3. */
        { "phone 640 dpi 1080p", { 2400, 1080, 640, 8, 4, 2800000, 7600, 0, 0,
                                   "Mali-G78", 0 },
          4, 3, 3, 4 },
    };
    static const int rg556_khz[8] = { 1800000, 1800000, 1800000, 1800000,
                                      2300000, 2300000, 2300000, 2700000 };
    static const int flat_khz[4] = { 2000000, 2000000, 2000000, 2000000 };
    static const int none_khz[4] = { 0, 0, 0, 0 };
    static const int two_big[8] = { 1800000, 1800000, 1800000, 1800000,
                                    1800000, 1800000, 2400000, 2400000 };
    static const int emu_khz[6] = { 2, 2, 2, 2, 2, 2 };
    /* What the RG556 really reports (read 2026-09-10): its A55s' ceiling is
     * 2,184,000 kHz, 80.8% of the prime's, so the ceiling rule alone counts
     * all eight big and the app ran six workers on the device measured to
     * want four. Its cpu_capacity reads 336, 869 and 1024. */
    static const int rg556_real_khz[8] = { 2184000, 2184000, 2184000, 2184000,
                                           2301000, 2301000, 2301000, 2704000 };
    static const int rg556_cap[8] = { 336, 336, 336, 336, 869, 869, 869, 1024 };
    static const int prime_cap[8] = { 200, 200, 200, 200, 750, 750, 750, 1024 };
    static const int flat_cap[4] = { 1024, 1024, 1024, 1024 };
    static const int none_cap[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    size_t i;

    printf("device defaults:\n");
    for (i = 0; i < sizeof table / sizeof table[0]; i++) {
        expect(&table[i]);
    }

    printf("big cores:\n");
    CHECK(mmo_device_big_cores(rg556_khz, 8) == 4,
          "RG556's 2.3 GHz A76s are big beside the 2.7, its A55s are not");
    CHECK(mmo_device_big_cores(flat_khz, 4) == 4, "four equal cores are four big");
    CHECK(mmo_device_big_cores(none_khz, 4) == 0, "unreadable ceilings count none");
    CHECK(mmo_device_big_cores(two_big, 8) == 2, "two A76s over six A55s are two");
    CHECK(mmo_device_big_cores(emu_khz, 6) == 0,
          "the emulator's 2 kHz ceilings are unknown, not six big cores");
    CHECK(mmo_device_big_cores(rg556_real_khz, 8) == 8,
          "the RG556's real ceilings fool the ceiling rule into eight");
    CHECK(mmo_device_big_cores_by_capacity(rg556_cap, 8) == 4,
          "...and its cpu_capacity answers four, which is why it is asked first");
    CHECK(mmo_device_big_cores_by_capacity(prime_cap, 8) == 4,
          "a prime over three mids over four littles is four");
    CHECK(mmo_device_big_cores_by_capacity(flat_cap, 4) == 4,
          "four equal capacities are four big");
    CHECK(mmo_device_big_cores_by_capacity(none_cap, 8) == 0,
          "no capacity readable answers none, and the ceilings decide");

    printf("calibration:\n");
    {
        struct mmo_calibration c;
        struct mmo_device_facts f = { 1920, 1080, 403, 8, 4, 2700000, 7600,
                                      0, 0, "Mali-G57", 0 };
        struct mmo_device_defaults d;
        enum mmo_cal_verdict v;

        memset(&c, 0, sizeof c);
        /* A short session teaches nothing. */
        v = mmo_device_calibrate(&c, 3, 900, 200, 0, 0, 15, 3);
        CHECK(v == MMO_CAL_TOO_SHORT && c.cap == 0,
              "fifteen seconds is not evidence");
        /* HD at 14% late, the title demo before the step-down: cap to SD. */
        v = mmo_device_calibrate(&c, 3, 4500, 648, 0, 0, 75, 3);
        CHECK(v == MMO_CAL_BAD && c.cap == 2, "14% late at HD caps to SD");
        /* Applied through the rules: the RG556 now answers SD. */
        f.cal_cap = c.cap;
        mmo_device_decide(&f, &d);
        CHECK(d.hd3d == 2 && strstr(d.why, "measured cap") != NULL,
              "the cap takes the RG556's HD default to SD");
        /* Two clean sessions at SD earn HD back; one does not. */
        v = mmo_device_calibrate(&c, 2, 18000, 10, 0, 0, 300, 3);
        CHECK(v == MMO_CAL_CLEAN && c.cap == 2 && c.good_runs == 1,
              "one clean session at the cap counts, and holds");
        v = mmo_device_calibrate(&c, 2, 18000, 12, 0, 0, 300, 3);
        CHECK(v == MMO_CAL_RAISED && c.cap == 3 && c.good_runs == 0,
              "the second clean session raises the cap to HD");
        /* Never above the rule. */
        v = mmo_device_calibrate(&c, 3, 18000, 0, 0, 0, 300, 3);
        v = mmo_device_calibrate(&c, 3, 18000, 0, 0, 0, 300, 3);
        CHECK(c.cap == 3, "clean sessions at the rule's level do not raise past it");
        /* An ordinary session in between resets the streak. */
        c.cap = 2; c.good_runs = 1;
        v = mmo_device_calibrate(&c, 2, 18000, 200, 0, 0, 300, 3);
        CHECK(v == MMO_CAL_HELD && c.good_runs == 0 && c.cap == 2,
              "1.1% late is neither bad nor clean, and resets the streak");
        /* Step-downs alone make a session bad. */
        c.cap = 0;
        v = mmo_device_calibrate(&c, 3, 18000, 20, 0, 8, 300, 3);
        CHECK(v == MMO_CAL_BAD && c.cap == 2,
              "eight step-downs in five minutes cap HD to SD");
        /* A forced ULTRA that skipped pictures measures ULTRA for AUTO. */
        c.cap = 0;
        v = mmo_device_calibrate(&c, 4, 18000, 100, 900, 0, 300, 3);
        CHECK(v == MMO_CAL_BAD && c.cap == 3,
              "5% skipped at a forced ULTRA caps AUTO at HD");
        /* A bad session never takes the cap below SD. */
        v = mmo_device_calibrate(&c, 2, 18000, 3000, 0, 0, 300, 3);
        CHECK(c.cap == 2, "SD is the floor");
    }

    printf("software gl:\n");
    CHECK(mmo_device_gl_is_soft(
              "Android Emulator OpenGL ES Translator (Google SwiftShader)"),
          "the emulator's SwiftShader is software");
    CHECK(mmo_device_gl_is_soft("llvmpipe (LLVM 15.0.7, 256 bits)"),
          "Mesa's llvmpipe is software");
    CHECK(!mmo_device_gl_is_soft("Mali-G57 MC2"), "a Mali is not");
    CHECK(!mmo_device_gl_is_soft("Adreno (TM) 640"), "an Adreno is not");
    CHECK(!mmo_device_gl_is_soft(NULL), "no name is not");

    return failures;
}
