/* Reading the device, on Android. */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/configuration.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <sys/sysinfo.h>
#include <unistd.h>

#include "mmo_calibrate.h"
#include "mmo_device.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "openmmo", __VA_ARGS__)

static struct mmo_device_facts g_facts;
static int g_probed;

static int read_int_file(const char *path)
{
    FILE *f = fopen(path, "r");
    long v = 0;

    if (f == NULL) {
        return 0;
    }
    if (fscanf(f, "%ld", &v) != 1) {
        v = 0;
    }
    fclose(f);
    return v > 0 && v < 0x7fffffffL ? (int)v : 0;
}

static void probe_cores(struct mmo_device_facts *f)
{
    int khz[64], cap[64];
    long n = sysconf(_SC_NPROCESSORS_CONF);
    int i;

    if (n <= 0) {
        n = sysconf(_SC_NPROCESSORS_ONLN);
    }
    if (n <= 0) {
        return;
    }
    if (n > 64) {
        n = 64;
    }
    f->cpus = (int)n;
    /*
     * cpuinfo_max_freq is the hardware ceiling and is what tells an A76 from an A55;
     * scaling_max_freq is a governor's current cap and moves with thermals, which is not what
     * a thread count should follow.
     */
    for (i = 0; i < n; i++) {
        char path[96];

        snprintf(path, sizeof path,
                 "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
        khz[i] = read_int_file(path);
        if (khz[i] > f->max_khz) {
            f->max_khz = khz[i];
        }
    }
    /*
     * The scheduler's own word first: cpu_capacity is the core's throughput at its ceiling,
     * normalised to the fastest, and it is what separates an A55 from an A76 when the vendor
     * has clocked them alike.
     */
    for (i = 0; i < n; i++) {
        char path[96];

        snprintf(path, sizeof path,
                 "/sys/devices/system/cpu/cpu%d/cpu_capacity", i);
        cap[i] = read_int_file(path);
    }
    f->big_cpus = mmo_device_big_cores_by_capacity(cap, (int)n);
    if (f->big_cpus == 0) {
        f->big_cpus = mmo_device_big_cores(khz, (int)n);
    }
    if (f->big_cpus == 0) {
        f->max_khz = 0;         /* the ceilings were not ceilings */
    }
}

static void probe_memory(struct mmo_device_facts *f)
{
    struct sysinfo si;
    char prop[PROP_VALUE_MAX];

    if (sysinfo(&si) == 0) {
        f->ram_mb = (long)((unsigned long long)si.totalram * si.mem_unit
                           / (1024ULL * 1024ULL));
    }
    prop[0] = '\0';
    if (__system_property_get("ro.config.low_ram", prop) > 0
        && strcmp(prop, "true") == 0) {
        f->low_ram = 1;
    } else if (f->ram_mb > 0 && f->ram_mb < 2048) {
        f->low_ram = 1;
    }
}

/* The driver'S name, before there is a window. */
static void probe_gl(struct mmo_device_facts *f)
{
    static const EGLint want[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_NONE
    };
    static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2,
                                       EGL_NONE };
    static const EGLint pb_attr[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
    EGLDisplay dpy;
    EGLConfig cfg;
    EGLContext ctx;
    EGLSurface pb;
    EGLint n = 0;
    const char *name;

    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL)) {
        return;
    }
    if (!eglChooseConfig(dpy, want, &cfg, 1, &n) || n < 1) {
        return;
    }
    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (ctx == EGL_NO_CONTEXT) {
        return;
    }
    pb = eglCreatePbufferSurface(dpy, cfg, pb_attr);
    if (pb != EGL_NO_SURFACE && eglMakeCurrent(dpy, pb, pb, ctx)) {
        name = (const char *)glGetString(GL_RENDERER);
        if (name != NULL) {
            snprintf(f->gpu, sizeof f->gpu, "%s", name);
            f->soft_gl = mmo_device_gl_is_soft(name);
        }
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    if (pb != EGL_NO_SURFACE) {
        eglDestroySurface(dpy, pb);
    }
    eglDestroyContext(dpy, ctx);
}

void mmo_device_probe(struct android_app *app)
{
    struct mmo_device_facts *f = &g_facts;
    struct mmo_device_defaults d;

    if (!g_probed) {
        memset(f, 0, sizeof *f);
        probe_cores(f);
        probe_memory(f);
        probe_gl(f);
        /* And what earlier sessions measured, which caps the rules. */
        if (app != NULL && app->activity != NULL
            && app->activity->internalDataPath != NULL) {
            mkdir(app->activity->internalDataPath, 0700);
            setenv("XDG_CONFIG_HOME", app->activity->internalDataPath, 0);
        }
        f->cal_cap = mmo_calibrate_cap();
    }
    if (app != NULL && app->config != NULL) {
        int dpi = AConfiguration_getDensity(app->config);
        int wdp = AConfiguration_getScreenWidthDp(app->config);
        int hdp = AConfiguration_getScreenHeightDp(app->config);

        /* ACONFIGURATION_DENSITY_DEFAULT is 160 and _NONE is 0; the named
         * buckets and the exact values both come through as the dpi. */
        if (dpi > 0 && dpi < 2000) {
            f->density_dpi = dpi;
        }
        /* dp to pixels, long side first, only until the window says. */
        if (f->width_px == 0 && wdp > 0 && hdp > 0 && f->density_dpi > 0) {
            int w = wdp * f->density_dpi / 160;
            int h = hdp * f->density_dpi / 160;

            f->width_px = w > h ? w : h;
            f->height_px = w > h ? h : w;
        }
    }
    g_probed = 1;
    mmo_device_decide(f, &d);
    {
        char cap[40];

        cap[0] = '\0';
        if (f->cal_cap != 0) {
            snprintf(cap, sizeof cap, ", measured hd3d cap %d", f->cal_cap);
        }
        LOGI("device: %dx%d px, %d dpi, %d cores (%d big, top %d MHz),"
             " %ld MB%s%s%s%s",
             f->width_px, f->height_px, f->density_dpi, f->cpus, f->big_cpus,
             f->max_khz / 1000, f->ram_mb, f->low_ram ? ", low ram" : "",
             f->gpu[0] != '\0' ? ", gl " : "", f->gpu, cap);
    }
    LOGI("device: defaults, %s", d.why);
}

void mmo_device_note_window(int width_px, int height_px)
{
    if (width_px <= 0 || height_px <= 0) {
        return;
    }
    g_facts.width_px = width_px > height_px ? width_px : height_px;
    g_facts.height_px = width_px > height_px ? height_px : width_px;
}

void mmo_device_note_gl(const char *renderer)
{
    struct mmo_device_defaults d;

    if (renderer == NULL) {
        return;
    }
    /* Already known from the pbuffer probe, and the same driver: nothing
     * to say twice. */
    if (strcmp(g_facts.gpu, renderer) == 0) {
        return;
    }
    snprintf(g_facts.gpu, sizeof g_facts.gpu, "%s", renderer);
    g_facts.soft_gl = mmo_device_gl_is_soft(renderer);
    mmo_device_decide(&g_facts, &d);
    LOGI("device: gl '%s'%s, %s", renderer,
         g_facts.soft_gl ? " (software)" : "", d.why);
}

const struct mmo_device_facts *mmo_device_facts(void)
{
    return &g_facts;
}

const struct mmo_device_facts *mmo_device_facts_for_rules(void)
{
    return &g_facts;
}

void mmo_device_defaults(struct mmo_device_defaults *out)
{
    mmo_device_decide(&g_facts, out);
}
