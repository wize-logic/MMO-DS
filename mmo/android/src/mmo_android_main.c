/* The app, from C. */

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/input.h>
#include <dlfcn.h>
#include <android/keycodes.h>
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <jni.h>
#include <sys/stat.h>
#include <android/native_window.h>
#include <android/window.h>   /* AWINDOW_FLAG_FULLSCREEN */
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <android_native_app_glue.h>

#include "view_channel.h"   /* our own copy of the frame page */
#include "sdlshim_android.h" /* the desktop viewer, when it is linked in */

#define TAG "openmmo"
/* Every line goes to A FILE too, and the FILE IS where a player can reach it. */
static void log_line(int prio, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#define LOGI(...) log_line(ANDROID_LOG_INFO, __VA_ARGS__)
#define LOGW(...) log_line(ANDROID_LOG_WARN, __VA_ARGS__)
#define LOGE(...) log_line(ANDROID_LOG_ERROR, __VA_ARGS__)

static FILE *g_logfile;
static char g_logpath[512];

static void log_file_write(const char *tag, const char *line)
{
    if (g_logfile == NULL) {
        return;
    }
    fprintf(g_logfile, "%s: %s\n", tag, line);
    fflush(g_logfile);
}

static void log_line(int prio, const char *fmt, ...)
{
    char buf[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    __android_log_write(prio, TAG, buf);
    log_file_write(TAG, buf);
}

/* Opened before the engine starts, so the guest map and the cartridge line
 * are in the file and not just in logcat. `dir` is the external directory
 * when there is one, that is the whole point, and the internal one only
 * as a fallback, where at least a bug report over adb still finds it. */
static void log_open_file(const char *dir)
{
    char prev[520];

    if (dir == NULL || dir[0] == '\0' || g_logfile != NULL) {
        return;
    }
    snprintf(g_logpath, sizeof g_logpath, "%s/openmmo.log", dir);
    snprintf(prev, sizeof prev, "%s/openmmo.log.1", dir);
    rename(g_logpath, prev);
    g_logfile = fopen(g_logpath, "w");
    if (g_logfile == NULL) {
        LOGE("log: cannot write %s (%s)", g_logpath, strerror(errno));
        return;
    }
    LOGI("log: writing %s, previous run kept as openmmo.log.1", g_logpath);
}

/* The port's memory model, when this library has the port in it. */
extern int armrec_mem_init(void) __attribute__((weak));
extern const char *armrec_mem_strerror(void) __attribute__((weak));
extern int armrec_region_count(void) __attribute__((weak));
extern int armrec_region_at(int i, uint32_t *base, uint32_t *size,
                            const char **name) __attribute__((weak));

struct mmo_egl {
    EGLDisplay dpy;
    EGLSurface surf;
    EGLContext ctx;
    int32_t w, h;
};

static struct mmo_egl g_egl = { EGL_NO_DISPLAY, EGL_NO_SURFACE,
                                EGL_NO_CONTEXT, 0, 0 };
static int g_probe_done;

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* ANSWER 4.2's LAST QUESTION, INSIDE A REAL APP PROCESS. */
static void probe_guest_map(void)
{
    int i, n, bad = 0;

    if (g_probe_done) {
        return;
    }
    g_probe_done = 1;
    if (armrec_mem_init == NULL) {
        LOGI("guest map: no engine in this library, nothing to map");
        return;
    }
    if (armrec_mem_init() != 0) {
        LOGE("guest map: FAILED, %s",
             armrec_mem_strerror ? armrec_mem_strerror() : "?");
        LOGE("guest map: an app process needs the slab after all");
        return;
    }
    n = armrec_region_count ? armrec_region_count() : 0;
    for (i = 0; i < n; i++) {
        uint32_t base = 0, size = 0;
        const char *name = NULL;
        volatile uint8_t *p;
        int ok;

        if (!armrec_region_at(i, &base, &size, &name)) {
            continue;
        }
        /* Non-destructive, and that is not fussiness: this runs before the
         * engine boots, over regions the engine is about to use, I/O
         * registers among them, so what it writes it puts back. */
        p = (volatile uint8_t *)(uintptr_t)base;
        {
            uint8_t was0 = p[0], wasN = p[size - 1];

            p[0] = 0x5A;
            p[size - 1] = 0xA5;
            ok = (p[0] == 0x5A && p[size - 1] == 0xA5);
            p[0] = was0;
            p[size - 1] = wasN;
        }
        if (!ok) {
            LOGE("guest map: %08X + %06X %-20s MAPPED BUT NOT WRITABLE",
                 base, size, name ? name : "?");
            bad = 1;
        } else {
            LOGI("guest map: %08X + %06X %-20s ok", base, size,
                 name ? name : "?");
        }
    }
    LOGI("guest map: %d regions, %s", n,
         bad ? "A REGION IS NOT USABLE"
             : "all identity-mapped and writable INSIDE AN APP PROCESS");
}

static void egl_down(void)
{
    if (g_egl.dpy == EGL_NO_DISPLAY) {
        return;
    }
    eglMakeCurrent(g_egl.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (g_egl.ctx != EGL_NO_CONTEXT) {
        eglDestroyContext(g_egl.dpy, g_egl.ctx);
    }
    if (g_egl.surf != EGL_NO_SURFACE) {
        eglDestroySurface(g_egl.dpy, g_egl.surf);
    }
    eglTerminate(g_egl.dpy);
    g_egl.dpy = EGL_NO_DISPLAY;
    g_egl.surf = EGL_NO_SURFACE;
    g_egl.ctx = EGL_NO_CONTEXT;
    g_egl.w = g_egl.h = 0;
}

static int egl_up(ANativeWindow *win)
{
    /* ES2 is the floor this asks for: the Mali-G57 in the RG556 is an ES3.2
     * part, and the layer this will eventually draw needs nothing ES2 does
     * not have. A device that cannot give ES2 cannot run this at all. */
    const EGLint want[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,   8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE,  8,
        EGL_DEPTH_SIZE, 0,
        EGL_NONE
    };
    const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLConfig cfg;
    EGLint n = 0, fmt = 0;

    egl_down();
    g_egl.dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_egl.dpy == EGL_NO_DISPLAY || !eglInitialize(g_egl.dpy, NULL, NULL)) {
        LOGE("egl: no display");
        return -1;
    }
    if (!eglChooseConfig(g_egl.dpy, want, &cfg, 1, &n) || n < 1) {
        LOGE("egl: no ES2 config with an 8888 window");
        return -1;
    }
    /*
     * The window's buffers have to be in the format the config picked, and the framework will
     * not do it for us: ANativeWindow_setBuffersGeometry with the config's
     * EGL_NATIVE_VISUAL_ID is the handshake.
     */
    eglGetConfigAttrib(g_egl.dpy, cfg, EGL_NATIVE_VISUAL_ID, &fmt);
    ANativeWindow_setBuffersGeometry(win, 0, 0, fmt);

    g_egl.surf = eglCreateWindowSurface(g_egl.dpy, cfg, win, NULL);
    if (g_egl.surf == EGL_NO_SURFACE) {
        LOGE("egl: no window surface (0x%x)", eglGetError());
        return -1;
    }
    g_egl.ctx = eglCreateContext(g_egl.dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (g_egl.ctx == EGL_NO_CONTEXT) {
        LOGE("egl: no context (0x%x)", eglGetError());
        return -1;
    }
    if (!eglMakeCurrent(g_egl.dpy, g_egl.surf, g_egl.surf, g_egl.ctx)) {
        LOGE("egl: cannot make current (0x%x)", eglGetError());
        return -1;
    }
    eglQuerySurface(g_egl.dpy, g_egl.surf, EGL_WIDTH, &g_egl.w);
    eglQuerySurface(g_egl.dpy, g_egl.surf, EGL_HEIGHT, &g_egl.h);
    LOGI("egl: surface %dx%d, %s", g_egl.w, g_egl.h,
         (const char *)glGetString(GL_RENDERER));
    return 0;
}

/* ===================================================================== game */

/* Everything the engine prints, where somebody can read it. */
static void *log_pump(void *arg)
{
    int fd = (int)(intptr_t)arg;
    char line[512];
    size_t used = 0;

    for (;;) {
        char c;
        ssize_t n = read(fd, &c, 1);

        if (n <= 0) {
            if (n < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
        if (c == '\n' || used == sizeof line - 1) {
            line[used] = '\0';
            if (used > 0) {
                __android_log_write(ANDROID_LOG_INFO, "openmmo.engine", line);
                log_file_write("engine", line);
            }
            used = 0;
            if (c != '\n') {
                line[used++] = c;
            }
        } else if (c != '\r') {
            line[used++] = c;
        }
    }
    return NULL;
}

static void log_capture_stdio(void)
{
    static int done;
    int fds[2];
    pthread_t t;

    if (done) {
        return;
    }
    done = 1;
    if (pipe(fds) != 0) {
        return;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    dup2(fds[1], STDOUT_FILENO);
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    if (pthread_create(&t, NULL, log_pump, (void *)(intptr_t)fds[0]) == 0) {
        pthread_detach(t);
    }
}

/* The engine, when this library has one. Weak so an APK without it still
 * runs the surface. */
extern int main(int argc, char **argv) __attribute__((weak));
extern void *pc_view_local_page(const char *name, size_t *size)
    __attribute__((weak));

/*
 * The desktop VIEWER, when it is linked in, viewer.c compiled for this target with its main
 * renamed, over the SDL shim.
 */
extern int openmmo_viewer_main(int argc, char **argv) __attribute__((weak));

/* The front door (mmo_frontdoor.c), when the viewer objects are linked in:
 * the launcher's login face, run on the viewer's thread before the game. */
extern int mmo_frontdoor_run(const char *art_dir, char *user, size_t ucap,
                             char *pass, size_t pcap) __attribute__((weak));
extern void mmo_frontdoor_apply(char *view_args, size_t cap)
    __attribute__((weak));

static void engine_go(struct android_app *app);
static void engine_env(struct android_app *app);

static int g_viewer_started;

static int viewer_on(void)
{
    static int v = -1;

    if (v < 0) {
        const char *e = getenv("OPENMMO_NO_VIEWER");

        v = (openmmo_viewer_main != NULL
             && !(e != NULL && e[0] != '\0' && e[0] != '0')) ? 1 : 0;
    }
    return v;
}

#define VIEW_CHAN "openmmo"

static struct openmmo_view_shm *g_page;
static pthread_t g_engine;
static int g_engine_started;
static char g_rom[512];
static char g_savedir[512];

/*
 * Where the cartridge IS, and this is the first half of 4.6's answer rather than a development
 * shortcut.
 */
static int find_rom(struct android_app *app)
{
    static const char *kNames[] = {
        "pokeplatinum.us.nds", "platinum.nds", "rom.nds", NULL
    };
    const char *dirs[2];
    int d, i;

    dirs[0] = app->activity->externalDataPath;
    dirs[1] = app->activity->internalDataPath;
    /* Where the APP'S own files are, said once so the rest of the client can
     * stop being handed it. The ROM, the theme, the fonts and now the sound
     * packages all live here, and the front door is a separate translation
     * unit with no ANativeActivity of its own to ask. */
    if (dirs[0] != NULL) {
        setenv("OPENMMO_EXTERNAL_DIR", dirs[0], 1);
    }
    /*
     * The chosen cartridge first. The front door saves a path the player picked
     * (launcher.cfg's `rom`, the desktop's own key) and hands it down as OPENMMO_ROM;
     * openmmo.env may also set it for a bench run.
     */
    {
        const char *pick = getenv("OPENMMO_ROM");

        if (pick != NULL && pick[0] != '\0') {
            int fd = -1;
            struct stat st;
            int ok;

            /*
             * A picked document arrives as /proc/self/fd/N, and access() is the wrong question
             * to ask about one: it checks path permissions on the fuse target, which is
             * exactly what a SAF grant does not give. The fd itself is the permission, so the
             * fd is what gets checked.
             */
            if (sscanf(pick, "/proc/self/fd/%d", &fd) == 1) {
                ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode);
                if (ok) {
                    /*
                     * The engine will fopen this path, which re-walks the magic link. Prove
                     * the reopen works here, where a failure can still be said in one line
                     * naming the leg that dropped, not discovered as a read error deep in
                     * the loader.
                     */
                    FILE *probe = fopen(pick, "rb");

                    if (probe != NULL) {
                        fclose(probe);
                    } else {
                        LOGE("rom: fd %d is live but reopening %s failed",
                             fd, pick);
                        ok = 0;
                    }
                }
            } else {
                ok = access(pick, R_OK) == 0;
            }
            if (ok) {
                snprintf(g_rom, sizeof g_rom, "%s", pick);
                LOGI("rom: %s (chosen)", g_rom);
                return 0;
            }
            LOGE("rom: OPENMMO_ROM=%s is set but not usable", pick);
        }
    }
    for (d = 0; d < 2; d++) {
        if (dirs[d] == NULL) {
            continue;
        }
        for (i = 0; kNames[i] != NULL; i++) {
            snprintf(g_rom, sizeof g_rom, "%s/%s", dirs[d], kNames[i]);
            if (access(g_rom, R_OK) == 0) {
                LOGI("rom: %s", g_rom);
                return 0;
            }
        }
        LOGI("rom: nothing named pokeplatinum.us.nds in %s", dirs[d]);
    }
    g_rom[0] = '\0';
    return -1;
}

/* Knobs, from the device, without a rebuild. */
static void engine_env_file(const char *dir)
{
    char path[512];
    char line[512];
    FILE *f;

    if (dir == NULL) {
        return;
    }
    snprintf(path, sizeof path, "%s/openmmo.env", dir);
    f = fopen(path, "r");
    if (f == NULL) {
        return;
    }
    LOGI("env: reading %s", path);
    while (fgets(line, sizeof line, f) != NULL) {
        char *eq, *nl;

        nl = strchr(line, '\n');
        if (nl != NULL) {
            *nl = '\0';
        }
        if (line[0] == '#' || line[0] == '\0') {
            continue;
        }
        eq = strchr(line, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        setenv(line, eq + 1, 1);
        LOGI("env: %s=%s", line, eq + 1);
    }
    fclose(f);
}

static void *engine_thread(void *arg)
{
    char arg0[] = "pokeplatinum";
    char *argv[2];

    (void)arg;
    argv[0] = arg0;
    argv[1] = NULL;
    LOGI("engine: entering main()");
    main(1, argv);
    LOGE("engine: main() returned, the game has stopped");
    return NULL;
}

/* Where IS it stuck?, and the answer has to come from the engine's thread. */
static void *stall_watch(void *arg)
{
    int secs = (int)(intptr_t)arg;
    struct openmmo_view_shm *p;
    struct timespec ts;

    ts.tv_sec = secs;
    ts.tv_nsec = 0;
    nanosleep(&ts, NULL);
    p = pc_view_local_page != NULL ? pc_view_local_page(VIEW_CHAN, NULL) : NULL;
    if (p != NULL && p->seq != 0) {
        LOGI("stall: %u frames published by %ds, nothing to report", p->seq,
             secs);
        return NULL;
    }
    LOGE("stall: no frame published in %ds, asking the engine thread where"
         " it is", secs);
    pthread_kill(g_engine, SIGALRM);
    return NULL;
}

/* The VIEWER thread IS the desktop program, argv and all. */
static void *viewer_thread(void *arg)
{
    static char theme[560], shots[520];
    static char extra[512], setargs[128];
    static char *argv[32];
    struct android_app *app = arg;
    const char *ext = app->activity->externalDataPath;
    int argc = 0, rc;

    /*
     * The front door, first. The desktop shows the login face before any game exists, and so
     * does the app: the door runs on this thread, over the same renderer the viewer takes over
     * afterwards, and only when it has an answer does the engine boot.
     */
    {
        const char *have = getenv("OPENMMO_USER");
        const char *sess = getenv("OPENMMO_SESSION");
        int session_on = sess == NULL || sess[0] == '\0' || sess[0] != '0';

        if (mmo_frontdoor_run != NULL && session_on
            && (have == NULL || have[0] == '\0')) {
            static char fd_user[64], fd_pass[64];
            char art[560];

            snprintf(art, sizeof art, "%s/launcher",
                     ext != NULL ? ext : "");
            if (mmo_frontdoor_run(art, fd_user, sizeof fd_user, fd_pass,
                                  sizeof fd_pass) != 0) {
                LOGI("front door: exit");
                exit(0);
            }
            LOGI("front door: playing as %s%s", fd_user,
                 fd_pass[0] == '\0' ? " (saved sign-in)" : "");
            setenv("OPENMMO_USER", fd_user, 1);
            setenv("OPENMMO_PASS", fd_pass, 1);
        }
    }
    /* The settings become the game's environment and the window's rows, the
     * way the desktop plan pushes them, after the door, so what was just
     * saved is what applies, and on scripted runs too. openmmo.env still
     * wins where it names the same knob. */
    setargs[0] = '\0';
    if (mmo_frontdoor_apply != NULL) {
        mmo_frontdoor_apply(setargs, sizeof setargs);
    }
    engine_go(app);

    argv[argc++] = "openmmo-view";
    argv[argc++] = VIEW_CHAN;
    argv[argc++] = "--fullscreen";
    argv[argc++] = "--wait";
    argv[argc++] = "600000";
    if (ext != NULL) {
        snprintf(theme, sizeof theme, "%s/theme", ext);
        snprintf(shots, sizeof shots, "%s", ext);
        argv[argc++] = "--theme";
        argv[argc++] = theme;
        argv[argc++] = "--shots";
        argv[argc++] = shots;
    }
    if (setargs[0] != '\0') {
        char *p;

        for (p = strtok(setargs, " "); p != NULL && argc < 27;
             p = strtok(NULL, " ")) {
            argv[argc++] = p;
        }
    }
    /* Last, so a hand-written row overrides the settings' spelling of it. */
    {
        const char *e = getenv("OPENMMO_VIEW_ARGS");

        if (e != NULL && e[0] != '\0') {
            char *p;

            snprintf(extra, sizeof extra, "%s", e);
            for (p = strtok(extra, " "); p != NULL && argc < 31;
                 p = strtok(NULL, " ")) {
                argv[argc++] = p;
            }
        }
    }
    argv[argc] = NULL;

    /* The door is behind us (or was never shown), so the glass is the
     * game's now and the drawn pad may come up on a device that needs it.
     * sdlshim_android.h says why it was down until this line. */
    mmo_sdlshim_pad_enable(1);
    LOGI("viewer: starting the desktop window (%d args)", argc);
    rc = openmmo_viewer_main(argc, argv);
    /* The game exiting parks or closes this thread first: the shim's exit
     * hook runs ahead of the GL driver's own destructors (see
     * SDL_CreateRenderer), which is what makes a dead session an exit and
     * not a segfault in libGLES_mali. */
    LOGE("viewer: the window closed (rc %d), the session ends with it", rc);
    return NULL;
}

static void viewer_start(struct android_app *app)
{
    pthread_t t;

    if (g_viewer_started || !viewer_on()) {
        return;
    }
    if (app->activity->externalDataPath != NULL) {
        setenv("OPENMMO_VIEW_LOG_DIR", app->activity->externalDataPath, 0);
        /* The no-theme fallback face: DejaVu pushed beside the theme. An
         * Android app has no distribution fonts, and FreeType with no face
         * is a window with no words. */
        {
            char face[560];

            snprintf(face, sizeof face, "%s/fonts/DejaVuSans.ttf",
                     app->activity->externalDataPath);
            setenv("OPENMMO_UI_FONT", face, 0);
        }
    }
    g_viewer_started = 1;
    if (pthread_create(&t, NULL, viewer_thread, app) == 0) {
        pthread_detach(t);
    } else {
        LOGE("viewer: pthread_create failed");
        g_viewer_started = 0;
    }
}

static void engine_start(struct android_app *app)
{
    static int booted;

    if (booted || main == NULL) {
        if (main == NULL) {
            LOGI("engine: no game in this library");
        }
        return;
    }
    booted = 1;
    log_open_file(app->activity->externalDataPath != NULL
                      ? app->activity->externalDataPath
                      : app->activity->internalDataPath);

    engine_env_file(app->activity->externalDataPath);
    engine_env_file(app->activity->internalDataPath);

    /* Both threads print; catch it all from here on, not from the engine's
     * own thread, so the viewer's first lines are not lost to /dev/null. */
    log_capture_stdio();

    /* The memory map, beside the log: a fault report's eip is unreadable
     * without it, and nothing outside the process may read /proc on a
     * release Android. Written once; the library load addresses do not move
     * after this. */
    if (app->activity->externalDataPath != NULL) {
        char path[560];
        FILE *in, *out;

        snprintf(path, sizeof path, "%s/maps.txt",
                 app->activity->externalDataPath);
        in = fopen("/proc/self/maps", "r");
        out = in != NULL ? fopen(path, "w") : NULL;
        if (in != NULL && out != NULL) {
            char line[512];

            while (fgets(line, sizeof line, in) != NULL) {
                fputs(line, out);
            }
        }
        if (in != NULL) {
            fclose(in);
        }
        if (out != NULL) {
            fclose(out);
        }
    }

    /* Scanned before the door, and again after it. The door now carries a
     * cartridge picker (mmo_frontdoor.c), so a player with no ROM can supply
     * one without leaving the app, and the file that was not there when
     * this ran is there by the time the door closes. engine_go asks again. */
    if (find_rom(app) != 0) {
        LOGE("engine: no cartridge. Put one at");
        LOGE("engine:   %s/pokeplatinum.us.nds",
             app->activity->externalDataPath
                 ? app->activity->externalDataPath : "(no external dir)");
        LOGE("engine: the surface stays up so this is readable rather than a"
             " crash.");
        /* The window still opens: its message screen is the readable form
         * of this failure, where logcat is not a thing a player has. */
        viewer_start(app);
        return;
    }
    /* Paced by the game's own clock, not by this thread's swap: the two are
     * separate on purpose, so a dropped picture never slows the game down. */
    engine_env(app);
    /* Where the token and launcher.cfg live: the desktop keeps them under
     * the config home, so point that inside the sandbox and both sides of
     * the client (token.c here, the mod's mint on a password login) agree
     * on the same file. */
    if (app->activity->internalDataPath != NULL) {
        /*
         * Created here, NOT assumed. The framework makes this directory lazily, for apps that
         * ask through Context.getFilesDir(), which a C app never does.
         */
        if (mkdir(app->activity->internalDataPath, 0700) != 0
            && errno != EEXIST) {
            LOGE("config: cannot create %s: %s",
                 app->activity->internalDataPath, strerror(errno));
        }
        setenv("XDG_CONFIG_HOME", app->activity->internalDataPath, 0);
    }

    viewer_start(app);
    if (!viewer_on()) {
        /* No viewer, no front door: the old path boots the engine now. */
        engine_go(app);
    }
}

/*
 * What the engine'S PROCESS environment must say, in one place, because it used to sit inline
 * on the boot path, after the no-cartridge early return.
 */
static void engine_env(struct android_app *app)
{
    snprintf(g_savedir, sizeof g_savedir, "%s/save.bin",
             app->activity->internalDataPath);
    setenv("PC_ROM", g_rom, 0);
    setenv("PC_SAVE", g_savedir, 0);
    /* Paced by the game's own clock, not by this thread's swap: the two are
     * separate on purpose, so a dropped picture never slows the game down. */
    setenv("PC_PACE", "1", 0);
    /* The channel IS part of this or the fix was half a fix. */
    setenv("PC_VIEW", VIEW_CHAN, 1);
    /*
     * The desktop launcher sets this when a player presses Play; this app IS the play button,
     * so a session is the default and openmmo.env can still say 0 to boot the bare port.
     */
    setenv("OPENMMO_SESSION", "1", 0);
    /* Four worker threads, not the engine's cpus-minus-two six: measured on
     * this device at hd3d 2, six and four hold the same frame rate, but six
     * lands two workers on the little cores and the worst frame goes from
     * 22 ms to 50, the tail is what a player feels. openmmo.env overrides. */
    setenv("PC_THREADS", "4", 0);
}

/* The game's own threads, started when the account question is settled: by
 * the front door on the viewer's thread, or immediately when the viewer (and
 * so the door) is off. */
static void engine_go(struct android_app *app)
{
    if (g_engine_started || main == NULL) {
        return;
    }
    /* The door may have installed one since the first scan. */
    if (g_rom[0] == '\0' && find_rom(app) != 0) {
        LOGE("engine: still no cartridge; the window says so");
        return;
    }
    engine_env(app);
    g_engine_started = 1;
    if (pthread_create(&g_engine, NULL, engine_thread, app) != 0) {
        LOGE("engine: pthread_create failed");
        g_engine_started = 0;
        return;
    }
    if (getenv("PC_ANDROID_STALLDUMP") != NULL) {
        pthread_t t;
        intptr_t secs = atoi(getenv("PC_ANDROID_STALLDUMP"));

        if (secs > 0 && pthread_create(&t, NULL, stall_watch, (void *)secs) == 0) {
            pthread_detach(t);
        }
    }
}

/* The page appears when the engine has published once; until then, NULL. */
static struct openmmo_view_shm *page(void)
{
    if (g_page == NULL && pc_view_local_page != NULL) {
        struct openmmo_view_shm *p = pc_view_local_page(VIEW_CHAN, NULL);

        if (p != NULL && p->magic == OPENMMO_VIEW_MAGIC) {
            if (p->version != OPENMMO_VIEW_VERSION) {
                LOGE("page: version %u, this viewer speaks %u", p->version,
                     OPENMMO_VIEW_VERSION);
                return NULL;
            }
            g_page = p;
            /* Say a viewer is here, or the game treats every frame as a
             * headless one and a scripted run stays scripted. */
            p->in_viewer_pid = (uint32_t)getpid();
            p->in_quit = 0;
            LOGI("page: attached, %ux%u", p->width, p->height);
        }
    }
    return g_page;
}

/* ==================================================================== pixels */
static GLuint g_tex[2];
static GLuint g_prog;
static GLint g_uPos, g_uTex;
static uint32_t g_seen_seq;
static uint32_t g_tex_w, g_tex_h;

static const char kVert[] =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "uniform vec4 u_rect;\n"   /* x, y, w, h in clip space */
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  v_uv = a_uv;\n"
    "  gl_Position = vec4(u_rect.xy + a_pos * u_rect.zw, 0.0, 1.0);\n"
    "}\n";

static const char kFrag[] =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "void main() {\n"
    "  gl_FragColor = vec4(texture2D(u_tex, v_uv).bgr, 1.0);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    GLint ok = 0;

    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        LOGE("gl: shader: %s", log);
    }
    return s;
}

static void gl_up(void)
{
    int i;

    g_prog = glCreateProgram();
    glAttachShader(g_prog, compile(GL_VERTEX_SHADER, kVert));
    glAttachShader(g_prog, compile(GL_FRAGMENT_SHADER, kFrag));
    glBindAttribLocation(g_prog, 0, "a_pos");
    glBindAttribLocation(g_prog, 1, "a_uv");
    glLinkProgram(g_prog);
    g_uPos = glGetUniformLocation(g_prog, "u_rect");
    g_uTex = glGetUniformLocation(g_prog, "u_tex");

    glGenTextures(2, g_tex);
    for (i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, g_tex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    g_tex_w = g_tex_h = 0;
    g_seen_seq = 0;
}

/* Where the two screens go. 4.7. */
enum { LAYOUT_BIG = 0, LAYOUT_SIDE, LAYOUT_STACK };

struct layout {
    int x[2], y[2], w[2], h[2];    /* [0] the upper screen, [1] the lower */
};

static int layout_mode(void)
{
    static int mode = -1;
    const char *s;

    if (mode >= 0) {
        return mode;
    }
    mode = LAYOUT_BIG;
    s = getenv("OPENMMO_LAYOUT");
    if (s != NULL) {
        if (strcmp(s, "side") == 0) {
            mode = LAYOUT_SIDE;
        } else if (strcmp(s, "stack") == 0) {
            mode = LAYOUT_STACK;
        } else if (strcmp(s, "big") != 0) {
            LOGI("layout: OPENMMO_LAYOUT=%s is not big|side|stack, using big",
                 s);
        }
    }
    LOGI("layout: %s", mode == LAYOUT_BIG ? "big" :
                       mode == LAYOUT_SIDE ? "side" : "stack");
    return mode;
}

/* Both screens in surface pixels, y measured DOWN from the top edge, which
 * is the touch events' own convention, so the pen can use these rects as they
 * are. */
static void layout_rects(int sw, int sh, uint32_t fw, uint32_t fh,
                         struct layout *out)
{
    int w = (int)fw, h = (int)fh;
    int st = 1, sb = 1;

    memset(out, 0, sizeof(*out));
    if (w <= 0 || h <= 0 || sw <= 0 || sh <= 0) {
        return;
    }

    switch (layout_mode()) {
    case LAYOUT_STACK:
        while ((st + 1) * w <= sw && (st + 1) * h * 2 <= sh) {
            st++;
        }
        sb = st;
        out->w[0] = out->w[1] = st * w;
        out->h[0] = out->h[1] = st * h;
        out->x[0] = out->x[1] = (sw - st * w) / 2;
        out->y[0] = (sh - st * h * 2) / 2;
        out->y[1] = out->y[0] + st * h;
        return;

    case LAYOUT_SIDE:
        while ((st + 1) * w * 2 <= sw && (st + 1) * h <= sh) {
            st++;
        }
        sb = st;
        break;

    default:
        /* The largest upper that still leaves room for a lower beside it,
         * then the largest lower that fits in what is left. */
        while ((st + 1) * w + w <= sw && (st + 1) * h <= sh) {
            st++;
        }
        while ((sb + 1) <= st && st * w + (sb + 1) * w <= sw
               && (sb + 1) * h <= sh) {
            sb++;
        }
        break;
    }

    /* Side by side: the upper on the left, the lower, the touch screen,
     * on the right, under the hand that reaches for it. Each is centred
     * vertically in its own right, which keeps a small lower screen level
     * with the middle of a large upper one rather than hung off its top. */
    out->w[0] = st * w;
    out->h[0] = st * h;
    out->w[1] = sb * w;
    out->h[1] = sb * h;
    out->x[0] = (sw - (st + sb) * w) / 2;
    out->x[1] = out->x[0] + st * w;
    out->y[0] = (sh - out->h[0]) / 2;
    out->y[1] = (sh - out->h[1]) / 2;
}

static void draw_screen(int which, float cx, float cy, float cw, float ch)
{
    static const GLfloat pos[] = { 0,0, 1,0, 0,1, 1,1 };
    static const GLfloat uv[]  = { 0,1, 1,1, 0,0, 1,0 };
    GLfloat rect[4];

    /* a_pos is 0..1; u_rect maps it to a clip-space box. */
    rect[0] = cx; rect[1] = cy; rect[2] = cw; rect[3] = ch;
    glUniform4fv(g_uPos, 1, rect);
    glBindTexture(GL_TEXTURE_2D, g_tex[which]);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, pos);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uv);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

/* Is there a frame to draw at all? Asked before the clear, so a run with no
 * cartridge shows the moving colour rather than a black rectangle that looks
 * exactly like a hang. */
static int present_game_ready(void)
{
    struct openmmo_view_shm *p = page();

    return p != NULL && p->seq != 0 && p->width != 0 && p->height != 0;
}

/* Returns 1 if it drew the game, 0 if there was nothing to draw. */
static int present_game(void)
{
    struct openmmo_view_shm *p = page();
    uint32_t seq, w, h;
    struct layout lay;
    int i;

    if (p == NULL) {
        return 0;
    }
    seq = __atomic_load_n(&p->seq, __ATOMIC_ACQUIRE);
    w = p->width;
    h = p->height;
    if (w == 0 || h == 0 || w > OPENMMO_VIEW_WIDE_MAX * OPENMMO_VIEW_HD_MAX) {
        return 0;
    }

    glUseProgram(g_prog);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(g_uTex, 0);

    if (w != g_tex_w || h != g_tex_h) {
        for (i = 0; i < 2; i++) {
            glBindTexture(GL_TEXTURE_2D, g_tex[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        }
        g_tex_w = w;
        g_tex_h = h;
        g_seen_seq = 0;
        LOGI("page: frame is now %ux%u", w, h);
    }
    if (seq != g_seen_seq) {
        for (i = 0; i < 2; i++) {
            glBindTexture(GL_TEXTURE_2D, g_tex[i]);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)w, (GLsizei)h,
                            GL_RGBA, GL_UNSIGNED_BYTE, (const void *)p->pix[i]);
        }
        g_seen_seq = seq;
    }

    layout_rects(g_egl.w, g_egl.h, w, h, &lay);
    /* upper_engine says which of the two the console is showing on top, so it
     * decides which TEXTURE goes in the upper POSITION, the geometry above
     * does not care which is which. */
    for (i = 0; i < 2; i++) {
        float cx = (float)lay.x[i] / (float)g_egl.w * 2.0f - 1.0f;
        float cy = 1.0f
                   - (float)(lay.y[i] + lay.h[i]) / (float)g_egl.h * 2.0f;
        float cw = (float)lay.w[i] / (float)g_egl.w * 2.0f;
        float ch = (float)lay.h[i] / (float)g_egl.h * 2.0f;

        if (lay.w[i] <= 0 || lay.h[i] <= 0) {
            continue;
        }
        draw_screen(p->upper_engine ? 1 - i : i, cx, cy, cw, ch);
    }
    return 1;
}

/* ===================================================================== input */
static uint32_t g_keys;          /* the DS mask, as the buttons hold it */
static uint32_t g_hat;           /* the DS mask, as the hat holds it */
/* PC_ANDROID_KEYLOG=1: name every code and every pen position. */
static int key_log(void)
{
    static int v = -1;

    if (v < 0) {
        const char *e = getenv("PC_ANDROID_KEYLOG");

        v = (e != NULL && *e != '\0' && *e != '0') ? 1 : 0;
    }
    return v;
}

/*
 * Measured on the owner'S RG556, 2026-08-26, by pressing each face button in a known order
 * with the codes logged. The answer was neither reading this code used to guess between:
 */
static int face_xbox(void)
{
    static int v = -1;

    if (v < 0) {
        const char *e = getenv("PC_ANDROID_FACE");

        v = (e != NULL && e[0] == 'x') ? 1 : 0;
    }
    return v;
}

static uint32_t ds_key_for(int32_t code)
{
    switch (code) {
    /* By position. On this device the letters already agree with a DS's, so
     * the default is the identity; on an Xbox-labelled pad they do not. */
    case AKEYCODE_BUTTON_A:      return face_xbox() ? OPENMMO_VIEW_KEY_B
                                                    : OPENMMO_VIEW_KEY_A;
    case AKEYCODE_BUTTON_B:      return face_xbox() ? OPENMMO_VIEW_KEY_A
                                                    : OPENMMO_VIEW_KEY_B;
    case AKEYCODE_BUTTON_X:      return face_xbox() ? OPENMMO_VIEW_KEY_Y
                                                    : OPENMMO_VIEW_KEY_X;
    case AKEYCODE_BUTTON_Y:      return face_xbox() ? OPENMMO_VIEW_KEY_X
                                                    : OPENMMO_VIEW_KEY_Y;
    case AKEYCODE_BUTTON_L1:     return OPENMMO_VIEW_KEY_L;
    case AKEYCODE_BUTTON_R1:     return OPENMMO_VIEW_KEY_R;
    case AKEYCODE_BUTTON_START:  return OPENMMO_VIEW_KEY_START;
    case AKEYCODE_BUTTON_SELECT: return OPENMMO_VIEW_KEY_SELECT;
    /*
     * L2, R2 and the RIGHT stick are LEFT free, and that is a decision rather than an
     * omission: a DS has neither, and inventing a use for them here is a control this port
     * would have to keep explaining.
     */
    case AKEYCODE_DPAD_UP:       return OPENMMO_VIEW_KEY_UP;
    case AKEYCODE_DPAD_DOWN:     return OPENMMO_VIEW_KEY_DOWN;
    case AKEYCODE_DPAD_LEFT:     return OPENMMO_VIEW_KEY_LEFT;
    case AKEYCODE_DPAD_RIGHT:    return OPENMMO_VIEW_KEY_RIGHT;
    /* A keyboard, because a device with one is a device somebody is
     * developing on and these cost nothing. */
    case AKEYCODE_Z:             return OPENMMO_VIEW_KEY_B;
    case AKEYCODE_X:             return OPENMMO_VIEW_KEY_A;
    case AKEYCODE_A:             return OPENMMO_VIEW_KEY_Y;
    case AKEYCODE_S:             return OPENMMO_VIEW_KEY_X;
    case AKEYCODE_Q:             return OPENMMO_VIEW_KEY_L;
    case AKEYCODE_W:             return OPENMMO_VIEW_KEY_R;
    case AKEYCODE_ENTER:         return OPENMMO_VIEW_KEY_START;
    case AKEYCODE_SHIFT_RIGHT:   return OPENMMO_VIEW_KEY_SELECT;
    default:                     return 0;
    }
}

static const char *ds_key_name(uint32_t m)
{
    switch (m) {
    case OPENMMO_VIEW_KEY_A: return "A";
    case OPENMMO_VIEW_KEY_B: return "B";
    case OPENMMO_VIEW_KEY_X: return "X";
    case OPENMMO_VIEW_KEY_Y: return "Y";
    case OPENMMO_VIEW_KEY_L: return "L";
    case OPENMMO_VIEW_KEY_R: return "R";
    case OPENMMO_VIEW_KEY_START: return "START";
    case OPENMMO_VIEW_KEY_SELECT: return "SELECT";
    case OPENMMO_VIEW_KEY_UP: return "UP";
    case OPENMMO_VIEW_KEY_DOWN: return "DOWN";
    case OPENMMO_VIEW_KEY_LEFT: return "LEFT";
    case OPENMMO_VIEW_KEY_RIGHT: return "RIGHT";
    default: return "-";
    }
}

/* The page is the only place input lives; in_seq is what tells the game a
 * viewer has ever spoken. */
static void push_input(void)
{
    struct openmmo_view_shm *p = page();

    if (p == NULL) {
        return;
    }
    p->in_keys = g_keys | g_hat;
    __atomic_add_fetch(&p->in_seq, 1, __ATOMIC_RELEASE);
}

static void push_touch(int down, int x, int y)
{
    struct openmmo_view_shm *p = page();

    if (p == NULL) {
        return;
    }
    if (down) {
        p->in_touch_x = (uint32_t)x;
        p->in_touch_y = (uint32_t)y;
    }
    p->in_touch = down ? 1u : 0u;
    __atomic_add_fetch(&p->in_seq, 1, __ATOMIC_RELEASE);
}

/*
 * The pen. The lower screen's quad is where the glass is the DS's touch panel, so a finger
 * anywhere else is not a pen-down at all, which is what makes the room around the game
 * usable by 4.7 for a HUD without every tap on it walking the player into a wall.
 */
static int touch_to_ds(float sx, float sy, int *dx, int *dy)
{
    struct openmmo_view_shm *p = page();
    uint32_t w, h;
    struct layout lay;
    int qw, qh, x0, y0;

    if (p == NULL) {
        return 0;
    }
    w = p->width;
    h = p->height;
    if (w == 0 || h == 0) {
        return 0;
    }
    /* The lower screen's rect IS the touch panel, wherever the layout put it
     * and whatever scale it got, which is why this asks the layout rather
     * than repeating its arithmetic. */
    layout_rects(g_egl.w, g_egl.h, w, h, &lay);
    x0 = lay.x[1];
    y0 = lay.y[1];
    qw = lay.w[1];
    qh = lay.h[1];

    if (qw <= 0 || qh <= 0
        || sx < x0 || sx >= x0 + qw || sy < y0 || sy >= y0 + qh) {
        return 0;
    }
    /* The pen's coordinates are the DS's own 256x192, whatever the frame's
     * width is this frame, a wide frame is a wider FRUSTUM, not a wider
     * touch panel. */
    *dx = (int)(((sx - x0) / (float)qw) * (float)OPENMMO_VIEW_W);
    *dy = (int)(((sy - y0) / (float)qh) * (float)OPENMMO_VIEW_H);
    if (*dx < 0) *dx = 0;
    if (*dx > OPENMMO_VIEW_W - 1) *dx = OPENMMO_VIEW_W - 1;
    if (*dy < 0) *dy = 0;
    if (*dy > OPENMMO_VIEW_H - 1) *dy = OPENMMO_VIEW_H - 1;
    return 1;
}

/* Every axis that moved, when the log is on. */
static void log_axes(AInputEvent *ev)
{
    static const struct { int32_t axis; const char *name; } kAxes[] = {
        { AMOTION_EVENT_AXIS_HAT_X,    "HAT_X (d-pad left/right)" },
        { AMOTION_EVENT_AXIS_HAT_Y,    "HAT_Y (d-pad up/down)" },
        { AMOTION_EVENT_AXIS_X,        "X (left stick)" },
        { AMOTION_EVENT_AXIS_Y,        "Y (left stick)" },
        { AMOTION_EVENT_AXIS_Z,        "Z (right stick or a trigger)" },
        { AMOTION_EVENT_AXIS_RZ,       "RZ (right stick or a trigger)" },
        { AMOTION_EVENT_AXIS_RX,       "RX" },
        { AMOTION_EVENT_AXIS_RY,       "RY" },
        { AMOTION_EVENT_AXIS_LTRIGGER, "LTRIGGER" },
        { AMOTION_EVENT_AXIS_RTRIGGER, "RTRIGGER" },
        { AMOTION_EVENT_AXIS_BRAKE,    "BRAKE (often L2)" },
        { AMOTION_EVENT_AXIS_GAS,      "GAS (often R2)" },
    };
    static float last[sizeof kAxes / sizeof kAxes[0]];
    unsigned i;

    for (i = 0; i < sizeof kAxes / sizeof kAxes[0]; i++) {
        float v = AMotionEvent_getAxisValue(ev, kAxes[i].axis, 0);

        /* A quarter deflection, so stick noise never speaks and a real push
         * always does. */
        if (v > -0.25f && v < 0.25f) {
            v = 0.0f;
        }
        if (v != last[i]) {
            LOGI("axis: %-28s %+.2f", kAxes[i].name, (double)v);
            last[i] = v;
        }
    }
}

/* The pad as the viewer's game controller. */
static int sdl_button_for(int32_t code)
{
    switch (code) {
    case AKEYCODE_BUTTON_A:      return face_xbox() ? 0 /* A */ : 1 /* B */;
    case AKEYCODE_BUTTON_B:      return face_xbox() ? 1 : 0;
    case AKEYCODE_BUTTON_X:      return face_xbox() ? 2 /* X */ : 3 /* Y */;
    case AKEYCODE_BUTTON_Y:      return face_xbox() ? 3 : 2;
    case AKEYCODE_BUTTON_L1:     return 9;   /* LEFTSHOULDER */
    case AKEYCODE_BUTTON_R1:     return 10;  /* RIGHTSHOULDER */
    case AKEYCODE_BUTTON_START:  return 6;   /* START */
    case AKEYCODE_BUTTON_SELECT: return 4;   /* BACK */
    case AKEYCODE_DPAD_UP:       return 11;
    case AKEYCODE_DPAD_DOWN:     return 12;
    case AKEYCODE_DPAD_LEFT:     return 13;
    case AKEYCODE_DPAD_RIGHT:    return 14;
    default:                     return -1;
    }
}

/* A developer's keyboard (adb keyevent), as SDL scancodes: the desktop's own
 * keymap then applies, letters drive text fields, F12 takes a screenshot. */
static int sdl_scancode_for(int32_t code)
{
    if (code >= AKEYCODE_A && code <= AKEYCODE_Z)
        return 4 + (code - AKEYCODE_A);           /* SDL_SCANCODE_A.. */
    if (code >= AKEYCODE_0 && code <= AKEYCODE_9)
        return 0;                                 /* digits: unused keys */
    switch (code) {
    case AKEYCODE_ENTER:        return 40;        /* RETURN */
    case AKEYCODE_ESCAPE:       return 41;
    case AKEYCODE_DEL:          return 42;        /* backspace */
    case AKEYCODE_SPACE:        return 44;
    case AKEYCODE_F10:          return 67;
    case AKEYCODE_F11:          return 68;
    case AKEYCODE_F12:          return 69;
    case AKEYCODE_MOVE_HOME:    return 74;
    case AKEYCODE_MOVE_END:     return 77;
    case AKEYCODE_FORWARD_DEL:  return 76;        /* delete */
    default:                    return 0;
    }
}

static int32_t viewer_input(AInputEvent *ev)
{
    int32_t type = AInputEvent_getType(ev);
    int32_t src = AInputEvent_getSource(ev);

    if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t code = AKeyEvent_getKeyCode(ev);
        int32_t act = AKeyEvent_getAction(ev);
        int down = act == AKEY_EVENT_ACTION_DOWN;
        int b;

        if (code == AKEYCODE_BACK) {
            return 0;           /* the framework closes the activity */
        }
        if (key_log()) {
            LOGI("key: code %d %s (viewer)", code, down ? "down" : "up");
        }
        b = sdl_button_for(code);
        if (b >= 0) {
            /* A real button puts the drawn one away. This device has both
             * thumbs on hardware already; a phone never reaches this line at
             * all, which is what makes the pad's default answer automatic
             * rather than a variable somebody has to be told about. */
            if ((src & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD
                || (src & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK) {
                mmo_sdlshim_pad_saw_gamepad();
            }
            mmo_sdlshim_button(b, down);
            return 1;
        }
        b = sdl_scancode_for(code);
        if (b > 0) {
            mmo_sdlshim_scancode(b, down);
            return 1;
        }
        return 0;
    }
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        if ((src & AINPUT_SOURCE_CLASS_JOYSTICK) != 0) {
            float hx = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_HAT_X, 0);
            float hy = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_HAT_Y, 0);
            float lx = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_X, 0);
            float ly = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_Y, 0);

            if (key_log()) {
                log_axes(ev);
            }
            mmo_sdlshim_pad_saw_gamepad();
            /* The hat wins where it is deflected; the stick where it is. */
            if (hx < -0.5f || hx > 0.5f) lx = hx;
            if (hy < -0.5f || hy > 0.5f) ly = hy;
            mmo_sdlshim_axis(lx, ly);
            return 1;
        }
        if ((src & AINPUT_SOURCE_CLASS_POINTER) != 0) {
            int32_t raw = AMotionEvent_getAction(ev);
            int32_t act = raw & AMOTION_EVENT_ACTION_MASK;
            size_t np = AMotionEvent_getPointerCount(ev);
            /*
             * Every POINTER, NOT just the first. The drawn pad needs two fingers to be two
             * fingers, a direction held while A is pressed is the ordinary way anyone walks
             * up to an npc, and the shim's pen is a mouse, which can only ever be one.
             */
            struct mmo_sdlshim_pt pts[MMO_SDLSHIM_PT_MAX];
            int leaver = -1, n = 0, action, act_index = -1;
            size_t i;

            switch (act) {
            case AMOTION_EVENT_ACTION_DOWN:
                action = 0;
                break;
            case AMOTION_EVENT_ACTION_POINTER_DOWN:
                action = 0;
                break;
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_CANCEL:
                action = 2;
                np = 0;
                break;
            case AMOTION_EVENT_ACTION_POINTER_UP:
                action = 1;
                leaver = (int)((raw & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                               >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
                break;
            default:
                action = 1;
                break;
            }
            if (act == AMOTION_EVENT_ACTION_POINTER_DOWN) {
                act_index = (int)((raw
                                   & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                                  >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
            } else if (act == AMOTION_EVENT_ACTION_DOWN) {
                act_index = 0;
            }
            for (i = 0; i < np && n < MMO_SDLSHIM_PT_MAX; i++) {
                if ((int)i == leaver)
                    continue;
                if ((int)i == act_index)
                    act_index = n;
                pts[n].x = AMotionEvent_getX(ev, i);
                pts[n].y = AMotionEvent_getY(ev, i);
                n++;
            }
            if (act_index >= n)
                act_index = -1;
            if (key_log() && action == 0 && n > 0) {
                LOGI("pen: screen %.0f,%.0f -> viewer (fingers %d)",
                     pts[0].x, pts[0].y, n);
            }
            mmo_sdlshim_pointers(action, pts, n, act_index);
            return 1;
        }
    }
    return 0;
}

static int32_t on_input(struct android_app *app, AInputEvent *ev)
{
    int32_t type = AInputEvent_getType(ev);
    int32_t src = AInputEvent_getSource(ev);

    (void)app;
    if (viewer_on()) {
        return viewer_input(ev);
    }
    if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t code = AKeyEvent_getKeyCode(ev);
        int32_t act = AKeyEvent_getAction(ev);
        uint32_t m = ds_key_for(code);

        if (key_log()) {
            /* Named as well as numbered, because the question a press answers
             * is "which button did I just hold", and a bare keycode does not
             * say. Hold each face button once with this on. */
            LOGI("key: code %d %s -> DS %s", code,
                 act == AKEY_EVENT_ACTION_DOWN ? "down" : "up", ds_key_name(m));
        }
        if (code == AKEYCODE_BACK) {
            return 0;               /* let the framework close the activity */
        }
        if (m == 0) {
            return 0;
        }
        if (act == AKEY_EVENT_ACTION_DOWN) {
            g_keys |= m;
        } else if (act == AKEY_EVENT_ACTION_UP) {
            g_keys &= ~m;
        }
        push_input();
        return 1;
    }

    if (type == AINPUT_EVENT_TYPE_MOTION) {
        if ((src & AINPUT_SOURCE_CLASS_JOYSTICK) != 0) {
            float hx = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_HAT_X, 0);
            float hy = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_HAT_Y, 0);
            float lx = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_X, 0);
            float ly = AMotionEvent_getAxisValue(ev, AMOTION_EVENT_AXIS_Y, 0);
            uint32_t was = g_hat;

            if (key_log()) {
                log_axes(ev);
            }

            /* The stick answers the d-pad, past half deflection. A DS has no
             * analogue anything, so there is nothing finer to report. */
            if (hx < -0.5f || lx < -0.5f) g_hat = (g_hat & ~OPENMMO_VIEW_KEY_RIGHT) | OPENMMO_VIEW_KEY_LEFT;
            else if (hx > 0.5f || lx > 0.5f) g_hat = (g_hat & ~OPENMMO_VIEW_KEY_LEFT) | OPENMMO_VIEW_KEY_RIGHT;
            else g_hat &= ~(OPENMMO_VIEW_KEY_LEFT | OPENMMO_VIEW_KEY_RIGHT);

            if (hy < -0.5f || ly < -0.5f) g_hat = (g_hat & ~OPENMMO_VIEW_KEY_DOWN) | OPENMMO_VIEW_KEY_UP;
            else if (hy > 0.5f || ly > 0.5f) g_hat = (g_hat & ~OPENMMO_VIEW_KEY_UP) | OPENMMO_VIEW_KEY_DOWN;
            else g_hat &= ~(OPENMMO_VIEW_KEY_UP | OPENMMO_VIEW_KEY_DOWN);

            if (g_hat != was) {
                push_input();
            }
            return 1;
        }
        if ((src & AINPUT_SOURCE_CLASS_POINTER) != 0) {
            int32_t act = AMotionEvent_getAction(ev) & AMOTION_EVENT_ACTION_MASK;
            float sx = AMotionEvent_getX(ev, 0);
            float sy = AMotionEvent_getY(ev, 0);
            int dx, dy;

            if (act == AMOTION_EVENT_ACTION_UP
                || act == AMOTION_EVENT_ACTION_CANCEL) {
                push_touch(0, 0, 0);
                if (key_log()) {
                    LOGI("pen: up");
                }
                return 1;
            }
            if (touch_to_ds(sx, sy, &dx, &dy)) {
                push_touch(1, dx, dy);
                if (key_log()) {
                    LOGI("pen: screen %.0f,%.0f -> DS %d,%d", sx, sy, dx, dy);
                }
            } else if (act == AMOTION_EVENT_ACTION_DOWN) {
                /* Outside the lower screen: not a pen press at all. The room
                 * around the game has to stay usable without walking the
                 * player into a wall. */
                push_touch(0, 0, 0);
                if (key_log()) {
                    LOGI("pen: screen %.0f,%.0f is outside the lower screen,"
                         " ignored", sx, sy);
                }
            }
            return 1;
        }
    }
    return 0;
}


/* ====================================================================== sound */
#define AA_IN_RATE  OPENMMO_VIEW_AUDIO_RATE

static AAudioStream *g_aa;
static uint32_t g_aa_tail;
static int32_t  g_aa_rate;
static uint64_t g_aa_starved, g_aa_dropped, g_aa_frames;
static double   g_aa_pos;            /* fractional position between two
                                        input frames, when resampling */
static uint32_t g_aa_cushion;        /* frames to stay behind the writer */
static uint64_t g_aa_rebuffers;
static int16_t  g_aa_prev[2];
static int      g_aa_have_prev;

/* Pull `want` input frames out of the ring, padding with silence. */
static void aa_resync(void)
{
    uint32_t head = __atomic_load_n(&g_page->audio_head, __ATOMIC_ACQUIRE);

    g_aa_tail = head - (head < g_aa_cushion ? head : g_aa_cushion);
}

static unsigned aa_pull(int16_t *dst, unsigned want)
{
    uint32_t lost = 0;
    unsigned got;

    got = openmmo_view_audio_read(g_page, &g_aa_tail, dst, want, &lost);
    g_aa_dropped += lost;
    if (got < want) {
        g_aa_starved += want - got;
        memset(dst + (size_t)got * 2, 0,
               (size_t)(want - got) * 2 * sizeof(int16_t));
        if (got == 0) {
            g_aa_rebuffers++;
            aa_resync();
        }
    }
    return got;
}

static aaudio_data_callback_result_t aa_cb(AAudioStream *stream, void *user,
                                           void *audioData, int32_t numFrames)
{
    int16_t *out = audioData;

    (void)stream;
    (void)user;
    if (g_page == NULL || numFrames <= 0) {
        memset(audioData, 0, (size_t)numFrames * 2 * sizeof(int16_t));
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
    g_aa_frames += (uint64_t)numFrames;

    if (g_aa_rate == (int32_t)AA_IN_RATE) {
        aa_pull(out, (unsigned)numFrames);
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    {
        /* One input frame per step of AA_IN_RATE/out_rate, interpolated. The
         * scratch is sized for the worst case this can be asked for. */
        static int16_t scratch[2048 * 2];
        const double step = (double)AA_IN_RATE / (double)g_aa_rate;
        unsigned need, i;
        double pos = g_aa_pos;

        need = (unsigned)(pos + step * (double)numFrames) + 2;
        if (need > sizeof scratch / (2 * sizeof scratch[0])) {
            need = (unsigned)(sizeof scratch / (2 * sizeof scratch[0]));
        }
        aa_pull(scratch, need);

        for (i = 0; i < (unsigned)numFrames; i++) {
            unsigned k = (unsigned)pos;
            double f = pos - (double)k;
            const int16_t *a, *b;

            if (k + 1 >= need) {
                k = need >= 2 ? need - 2 : 0;
                f = 0.0;
            }
            a = &scratch[(size_t)k * 2];
            b = &scratch[(size_t)(k + 1) * 2];
            out[i * 2 + 0] = (int16_t)(a[0] + (b[0] - a[0]) * f);
            out[i * 2 + 1] = (int16_t)(a[1] + (b[1] - a[1]) * f);
            pos += step;
        }
        /* Carry the fraction, drop the whole frames consumed. */
        g_aa_pos = pos - (double)(unsigned)pos;
        (void)g_aa_prev;
        (void)g_aa_have_prev;
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static void audio_stop(void)
{
    if (g_aa != NULL) {
        AAudioStream_requestStop(g_aa);
        AAudioStream_close(g_aa);
        g_aa = NULL;
    }
}

static void audio_start(void)
{
    AAudioStreamBuilder *b = NULL;
    aaudio_result_t r;

    if (g_aa != NULL || g_page == NULL) {
        return;
    }
    /* audio_rate is 0 until the game has published once, which is how a reader
     * learns whether this game makes sound at all. */
    if (g_page->audio_rate == 0) {
        return;
    }
    r = AAudio_createStreamBuilder(&b);
    if (r != AAUDIO_OK) {
        LOGE("sound: no stream builder (%s)", AAudio_convertResultToText(r));
        return;
    }
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setSampleRate(b, (int32_t)AA_IN_RATE);
    AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    /* setUsage arrived at API 28 and the floor here is 26, which is where
     * AAudio itself arrived. Its default usage is media, which is the right
     * answer for a game anyway, it ducks and routes the same way. */
    AAudioStreamBuilder_setDataCallback(b, aa_cb, NULL);

    r = AAudioStreamBuilder_openStream(b, &g_aa);
    AAudioStreamBuilder_delete(b);
    if (r != AAUDIO_OK) {
        LOGE("sound: cannot open a stream (%s); the picture still works",
             AAudio_convertResultToText(r));
        g_aa = NULL;
        return;
    }
    g_aa_rate = AAudioStream_getSampleRate(g_aa);
    /*
     * How far behind the writer to read, in milliseconds, and it is a real trade rather than a
     * constant to tune away.
     */
    {
        const char *ms = getenv("PC_ANDROID_AUDIO_MS");
        unsigned want_ms = (ms != NULL && *ms != '\0') ? (unsigned)atoi(ms) : 100u;

        if (want_ms < 10u) want_ms = 10u;
        if (want_ms > 500u) want_ms = 500u;
        g_aa_cushion = (uint32_t)((uint64_t)AA_IN_RATE * want_ms / 1000u);
    }
    if (g_aa_cushion == 0 || g_aa_cushion > OPENMMO_VIEW_AUDIO_FRAMES / 4u) {
        g_aa_cushion = 2048u;
    }
    aa_resync();
    g_aa_pos = 0.0;
    LOGI("sound: %d Hz out, %u Hz in%s, %d-frame burst, %u-frame cushion"
         " (%.0f ms)", g_aa_rate, (unsigned)AA_IN_RATE,
         g_aa_rate == (int32_t)AA_IN_RATE ? " (no resampling)" : " (resampled)",
         (int)AAudioStream_getFramesPerBurst(g_aa), g_aa_cushion,
         1000.0 * (double)g_aa_cushion / (double)AA_IN_RATE);
    r = AAudioStream_requestStart(g_aa);
    if (r != AAUDIO_OK) {
        LOGE("sound: cannot start (%s)", AAudio_convertResultToText(r));
        audio_stop();
    }
}

static void audio_report(void)
{
    static uint64_t last_frames;
    int32_t xruns;

    if (g_aa == NULL) {
        return;
    }
    xruns = AAudioStream_getXRunCount(g_aa);
    LOGI("sound: %llu frames out, %llu padded with silence, %llu dropped"
         " behind, %llu rebuffer(s), %d underrun(s) the device counted",
         (unsigned long long)(g_aa_frames - last_frames),
         (unsigned long long)g_aa_starved, (unsigned long long)g_aa_dropped,
         (unsigned long long)g_aa_rebuffers, (int)xruns);
    last_frames = g_aa_frames;
}

static void on_cmd(struct android_app *app, int32_t cmd)
{
    /* Unconditional and NOT behind key_log(): lifecycle commands arrive
     * before openmmo.env is read, and asking would cache the answer early
     * and wrongly for the whole run. */
    LOGI("cmd: %d (window %p)", cmd, (void *)app->window);
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (app->window == NULL) {
            break;
        }
        probe_guest_map();
        engine_start(app);      /* reads openmmo.env, starts both threads */
        if (viewer_on()) {
            mmo_sdlshim_set_window(app->window);
        } else if (egl_up(app->window) == 0) {
            gl_up();
        }
        break;
    case APP_CMD_TERM_WINDOW:
        /* The surface is gone the moment this returns, a home press, or
         * the far side of a rotation the activity did not keep. */
        if (viewer_on()) {
            mmo_sdlshim_set_window(NULL);
        } else {
            egl_down();
        }
        break;
    case APP_CMD_PAUSE:
        /* A backgrounded game that keeps playing is a game somebody hears
         * from another app. The engine keeps running: it is a session on a
         * server and stopping it is a disconnection. */
        if (viewer_on()) {
            mmo_sdlshim_background(1);
        } else {
            audio_stop();
        }
        break;
    case APP_CMD_RESUME:
        if (viewer_on()) {
            mmo_sdlshim_background(0);
        } else {
            audio_start();
        }
        break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONFIG_CHANGED:
        if (g_egl.dpy != EGL_NO_DISPLAY) {
            EGLint w = 0, h = 0;
            eglQuerySurface(g_egl.dpy, g_egl.surf, EGL_WIDTH, &w);
            eglQuerySurface(g_egl.dpy, g_egl.surf, EGL_HEIGHT, &h);
            if (w != g_egl.w || h != g_egl.h) {
                g_egl.w = w;
                g_egl.h = h;
                LOGI("egl: surface now %dx%d", w, h);
            }
        }
        break;
    default:
        break;
    }
}

/* ==================================================================== picker */
static struct android_app *g_app;

static JNIEnv *pick_env(void)
{
    JavaVM *vm;
    JNIEnv *env = NULL;

    if (g_app == NULL) {
        return NULL;
    }
    vm = g_app->activity->vm;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) == JNI_OK) {
        return env;
    }
    if ((*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) {
        return NULL;
    }
    return env;
}

/* One method by name, exceptions cleared rather than propagated: a JNI
 * exception left pending aborts the process at the next call, and every
 * failure here already has a C-side answer (the drawn fallback browser). */
static int pick_call(const char *name, const char *sig, jvalue *args,
                     jvalue *ret, char kind)
{
    JNIEnv *env = pick_env();
    jclass cls;
    jmethodID mid;

    if (env == NULL) {
        return -1;
    }
    cls = (*env)->GetObjectClass(env, g_app->activity->clazz);
    if (cls == NULL) {
        return -1;
    }
    mid = (*env)->GetMethodID(env, cls, name, sig);
    (*env)->DeleteLocalRef(env, cls);
    if (mid == NULL) {
        (*env)->ExceptionClear(env);
        return -1;
    }
    switch (kind) {
    case 'v':
        (*env)->CallVoidMethodA(env, g_app->activity->clazz, mid, args);
        break;
    case 'z':
        ret->z = (*env)->CallBooleanMethodA(env, g_app->activity->clazz, mid,
                                            args);
        break;
    case 'i':
        ret->i = (*env)->CallIntMethodA(env, g_app->activity->clazz, mid,
                                        args);
        break;
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        return -1;
    }
    return 0;
}

/* Fire the picker. 0 when the Intent went out; -1 sends the caller to the
 * drawn fallback browser. */
int mmo_android_rom_pick(void)
{
    char stale[600];
    const char *ext = getenv("OPENMMO_EXTERNAL_DIR");

    if (ext != NULL) {
        /* A leftover answer from a run that died mid-pick must not be read
         * as this pick's result. */
        snprintf(stale, sizeof stale, "%s/picked.uri", ext);
        remove(stale);
    }
    return pick_call("pickRom", "()V", NULL, NULL, 'v');
}

/* Poll for the answer. 1 with the URI (empty string = the player cancelled),
 * 0 while the picker is still up. */
int mmo_android_rom_picked(char *out, size_t cap)
{
    char path[600];
    const char *ext = getenv("OPENMMO_EXTERNAL_DIR");
    FILE *f;
    size_t n;

    if (ext == NULL) {
        return 0;
    }
    snprintf(path, sizeof path, "%s/picked.uri", ext);
    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    n = fread(out, 1, cap - 1, f);
    fclose(f);
    remove(path);
    out[n] = '\0';
    return 1;
}

/* Is the granted file still there and readable? The launcher's footer asks
 * this to stay honest after the file behind it is deleted. */
int mmo_android_rom_uri_ok(const char *uri)
{
    jvalue arg, ret;
    JNIEnv *env = pick_env();

    if (env == NULL) {
        return 0;
    }
    arg.l = (*env)->NewStringUTF(env, uri);
    ret.z = 0;
    if (pick_call("romOk", "(Ljava/lang/String;)Z", &arg, &ret, 'z') != 0) {
        ret.z = 0;
    }
    (*env)->DeleteLocalRef(env, arg.l);
    return ret.z ? 1 : 0;
}

/* The URI as a detached file descriptor, or -1. The caller owns it. */
int mmo_android_rom_uri_fd(const char *uri)
{
    jvalue arg, ret;
    JNIEnv *env = pick_env();

    if (env == NULL) {
        return -1;
    }
    arg.l = (*env)->NewStringUTF(env, uri);
    ret.i = -1;
    if (pick_call("romFd", "(Ljava/lang/String;)I", &arg, &ret, 'i') != 0) {
        ret.i = -1;
    }
    (*env)->DeleteLocalRef(env, arg.l);
    return ret.i;
}

/*
 * Hand a URL to whatever browses on this device, the update notice's "get it" button
 * (android/src/mmo_update_notice.c), which is what an app has instead of the desktop's
 * patcher.
 */
int mmo_android_open_url(const char *url)
{
    jvalue arg;
    JNIEnv *env = pick_env();
    int rc;

    if (env == NULL || url == NULL || url[0] == '\0') {
        return -1;
    }
    arg.l = (*env)->NewStringUTF(env, url);
    rc = pick_call("openUrl", "(Ljava/lang/String;)V", &arg, NULL, 'v');
    (*env)->DeleteLocalRef(env, arg.l);
    return rc;
}

void android_main(struct android_app *app)
{
    g_app = app;
    uint64_t t0 = now_ns(), last = t0;
    unsigned frames = 0, presented = 0, seconds = 0;
    uint32_t last_seq = 0;

    app->onAppCmd = on_cmd;
    app->onInputEvent = on_input;
    /*
     * The navigation bar overlays the bottom of the SURFACE. 4.3 measured mAppBounds at
     * 1920x1003 against an EGL surface of 1920x1080, 77 rows the system draws over.
     */
    ANativeActivity_setWindowFlags(app->activity, AWINDOW_FLAG_FULLSCREEN, 0);
    {
        /* The load base, because every address in a stall report or a crash is
         * relative to it and an app's /proc/self/maps is readable by nobody
         * else. addr2line against build/android/fused/libpokeplatinum.so takes
         * (address - base) straight to a line. */
        Dl_info info;

        if (dladdr((void *)(uintptr_t)android_main, &info) != 0) {
            LOGI("android_main: up, pid %d, lib base %p", getpid(),
                 info.dli_fbase);
        } else {
            LOGI("android_main: up, pid %d", getpid());
        }
    }

    for (;;) {
        int events = 0;
        struct android_poll_source *src = NULL;
        /*
         * Block only when there is nothing to draw. With a surface up this polls with no
         * timeout and the pacing comes from eglSwapBuffers, which is what makes "the device's
         * rate" the device's and not a number written here.
         */
        int timeout = viewer_on() ? 10000
                      : (g_egl.dpy == EGL_NO_DISPLAY) ? -1 : 0;

        while (ALooper_pollOnce(timeout, NULL, &events, (void **)&src) >= 0) {
            if (src != NULL) {
                src->process(app, src);
            }
            if (app->destroyRequested != 0) {
                LOGI("android_main: %u frames, %u presented", frames,
                     presented);
                audio_stop();
                egl_down();
                return;
            }
            timeout = 0;
        }

        if (viewer_on()) {
            /* The heartbeat: the game's publish rate, ten-secondly, so a
             * frozen viewer and a frozen game can be told apart in the log. */
            uint64_t now = now_ns();

            if (now - last >= 10000000000ull) {
                struct openmmo_view_shm *p =
                    pc_view_local_page != NULL
                        ? pc_view_local_page(VIEW_CHAN, NULL)
                        : NULL;
                uint32_t seq = p != NULL ? p->seq : 0;
                double secs = (double)(now - last) / 1e9;

                LOGI("game: published %.1f fps over %.0f s (viewer %s)",
                     (double)(seq - last_seq) / secs, secs,
                     mmo_sdlshim_active() ? "up" : "starting");
                last_seq = seq;
                last = now;
            }
            continue;
        }

        if (g_egl.dpy == EGL_NO_DISPLAY) {
            continue;
        }

        /*
         * A clear that moves. A still colour cannot tell a surface that is presenting from one
         * that froze three seconds ago, and that is the exact failure this task is about, so
         * the clear cycles slowly and a person can see it is alive from across the room.
         */
        glViewport(0, 0, g_egl.w, g_egl.h);
        if (present_game_ready()) {
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            present_game();
        } else {
            /* No game yet, no cartridge, or the engine has not published a
             * first frame. A still colour cannot tell a surface that is
             * presenting from one that froze, so this one moves. */
            double t = (double)(now_ns() - t0) / 1e9;
            float r = (float)(0.5 + 0.5 * sin(t * 1.1));
            float g = (float)(0.5 + 0.5 * sin(t * 1.7 + 2.0));
            float b = (float)(0.5 + 0.5 * sin(t * 2.3 + 4.0));

            glClearColor(r * 0.35f, g * 0.35f, b * 0.45f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        frames++;
        if (eglSwapBuffers(g_egl.dpy, g_egl.surf)) {
            presented++;
        } else {
            EGLint e = eglGetError();
            LOGW("egl: swap failed 0x%x", e);
            if (e == EGL_BAD_SURFACE || e == EGL_CONTEXT_LOST) {
                egl_down();
            }
        }

        {
            uint64_t now = now_ns();
            if (now - last >= 1000000000ull) {
                /*
                 * Two rates, and they are NOT the same number. The surface presents at the
                 * panel's rate whether or not the game made a new picture; the game publishes
                 * at its own.
                 */
                uint32_t seq = (g_page != NULL) ? g_page->seq : 0;
                double secs = (double)(now - last) / 1e9;

                /*
                 * touch_wanted is the game saying, from inside its own process, that it has
                 * the SDK's touch auto-sampling running, which nothing outside one can see.
                 * It is what says when the pen is worth trying, and 4.8's player wants to
                 * know.
                 */
                LOGI("surface: %u presented in %.2f s, %.1f fps, %dx%d;"
                     " game published %.1f fps; pen %s",
                     presented, secs, presented / secs, g_egl.w, g_egl.h,
                     (double)(seq - last_seq) / secs,
                     (g_page != NULL && g_page->touch_wanted)
                         ? "WANTED" : "not asked for");
                last_seq = seq;
                presented = 0;
                last = now;
                audio_start();          /* no-op once it is up */
                if (++seconds % 10 == 0) {
                    audio_report();
                }
            }
        }
    }
}
