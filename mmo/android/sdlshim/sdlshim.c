/* SDL.h's implementation: the desktop viewer on the app's GLES2. */

#include "SDL.h"
#include "sdlshim_android.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <aaudio/AAudio.h>
#include <android/log.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "view_font.h"   /* the 5x7: OSK key labels with no FreeType */

/* mmo_device.c, the app's own; declared here rather than through its header
 * because the shim's include path is the viewer's, not the app's. */
void mmo_device_note_gl(const char *renderer);

#define TAG "openmmo.sdl"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

static char g_err[256] = "no error";

static void set_err(const char *msg)
{
    snprintf(g_err, sizeof g_err, "%s", msg);
}

const char *SDL_GetError(void)
{
    return g_err;
}

int SDL_Init(Uint32 flags) { (void)flags; return 0; }
int SDL_InitSubSystem(Uint32 flags) { (void)flags; return 0; }
void SDL_Quit(void) {}

/* The one hint the viewer sets: the scale filter, read at texture creation. */
static int g_hint_linear = 1;

int SDL_SetHint(const char *name, const char *value)
{
    if (name != NULL && strcmp(name, SDL_HINT_RENDER_SCALE_QUALITY) == 0)
        g_hint_linear = value != NULL && strcmp(value, "nearest") != 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Time                                                                */
/* ------------------------------------------------------------------ */

static uint64_t now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t ticks_base(void)
{
    static uint64_t base;

    if (base == 0)
        base = now_ns();
    return base;
}

Uint64 SDL_GetTicks64(void)
{
    return (now_ns() - ticks_base()) / 1000000ull;
}

Uint32 SDL_GetTicks(void)
{
    return (Uint32)SDL_GetTicks64();
}

void SDL_Delay(Uint32 ms)
{
    struct timespec ts;

    ts.tv_sec = ms / 1000u;
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
}

Uint64 SDL_GetPerformanceCounter(void) { return now_ns(); }
Uint64 SDL_GetPerformanceFrequency(void) { return 1000000000ull; }

/* ------------------------------------------------------------------ */
/* Shared state: the glue thread's half                                */
/* ------------------------------------------------------------------ */

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;

static ANativeWindow *g_window;         /* the framework's, under g_lock */
static int g_window_gen;                /* bumped every set_window() */
static int g_have_surface;              /* render thread holds an EGL surface */
static int g_release_asked;            /* set_window(NULL) is waiting */

static int g_active;                    /* SDL_CreateRenderer succeeded */

#define EVQ_MAX 256
static SDL_Event g_evq[EVQ_MAX];
static unsigned g_ev_head, g_ev_tail;

static Uint8 g_keystate[SDL_NUM_SCANCODES];
static Uint8 g_buttons[SDL_CONTROLLER_BUTTON_MAX];
/*
 * A level that nobody sampled is a press that never happened. The viewer reads buttons and
 * keys as levels once per frame; `adb shell input keyevent` delivers down and up microseconds
 * apart, entirely inside one frame, and a real button can bounce the same way.
 */
static Uint8 g_button_frames[SDL_CONTROLLER_BUTTON_MAX];
static Uint8 g_button_pending_up[SDL_CONTROLLER_BUTTON_MAX];
static Uint8 g_key_frames[SDL_NUM_SCANCODES];
static Uint8 g_key_pending_up[SDL_NUM_SCANCODES];
static Sint16 g_axis_x, g_axis_y;
static int g_mouse_x, g_mouse_y;        /* logical */
static Uint32 g_mouse_mask;

static int g_textinput;                 /* SDL_StartTextInput level */
static int g_background;                /* app is paused */

static void push_event(const SDL_Event *ev)
{
    unsigned next = (g_ev_head + 1u) % EVQ_MAX;

    if (next == g_ev_tail) {
        g_ev_tail = (g_ev_tail + 1u) % EVQ_MAX;   /* drop the oldest */
    }
    g_evq[g_ev_head] = *ev;
    g_ev_head = next;
}

int SDL_PollEvent(SDL_Event *ev)
{
    int got = 0;

    pthread_mutex_lock(&g_lock);
    if (g_ev_tail != g_ev_head) {
        *ev = g_evq[g_ev_tail];
        g_ev_tail = (g_ev_tail + 1u) % EVQ_MAX;
        got = 1;
    }
    pthread_mutex_unlock(&g_lock);
    return got;
}

const Uint8 *SDL_GetKeyboardState(int *numkeys)
{
    if (numkeys != NULL)
        *numkeys = SDL_NUM_SCANCODES;
    return g_keystate;
}

SDL_Keymod SDL_GetModState(void) { return KMOD_NONE; }

Uint32 SDL_GetMouseState(int *x, int *y)
{
    Uint32 m;

    pthread_mutex_lock(&g_lock);
    if (x != NULL) *x = g_mouse_x;
    if (y != NULL) *y = g_mouse_y;
    m = g_mouse_mask;
    pthread_mutex_unlock(&g_lock);
    return m;
}

SDL_Scancode SDL_GetScancodeFromName(const char *name)
{
    static const struct { const char *n; SDL_Scancode sc; } kNames[] = {
        { "Return", SDL_SCANCODE_RETURN }, { "Space", SDL_SCANCODE_SPACE },
        { "Backspace", SDL_SCANCODE_BACKSPACE }, { "Tab", SDL_SCANCODE_TAB },
        { "Up", SDL_SCANCODE_UP }, { "Down", SDL_SCANCODE_DOWN },
        { "Left", SDL_SCANCODE_LEFT }, { "Right", SDL_SCANCODE_RIGHT },
    };
    unsigned i;

    if (name == NULL || name[0] == '\0')
        return SDL_SCANCODE_UNKNOWN;
    if (name[1] == '\0') {
        char c = name[0];

        if (c >= 'a' && c <= 'z') return (SDL_Scancode)(SDL_SCANCODE_A + c - 'a');
        if (c >= 'A' && c <= 'Z') return (SDL_Scancode)(SDL_SCANCODE_A + c - 'A');
    }
    for (i = 0; i < sizeof kNames / sizeof kNames[0]; i++)
        if (strcasecmp(kNames[i].n, name) == 0) return kNames[i].sc;
    return SDL_SCANCODE_UNKNOWN;
}

static int g_clipboard_only; /* nothing reads it back; keep the last set */

int SDL_SetClipboardText(const char *text)
{
    (void)text;
    g_clipboard_only = 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Game controller: one pad, levels the glue thread writes             */
/* ------------------------------------------------------------------ */

static SDL_GameController *const GC = (SDL_GameController *)&g_buttons;

int SDL_NumJoysticks(void) { return 1; }
int SDL_IsGameController(int index) { return index == 0; }

SDL_GameController *SDL_GameControllerOpen(int index)
{
    return index == 0 ? GC : NULL;
}

void SDL_GameControllerClose(SDL_GameController *gc) { (void)gc; }

const char *SDL_GameControllerName(SDL_GameController *gc)
{
    (void)gc;
    return "RG556";
}

Uint8 SDL_GameControllerGetButton(SDL_GameController *gc,
                                  SDL_GameControllerButton button)
{
    (void)gc;
    if ((int)button < 0 || (int)button >= SDL_CONTROLLER_BUTTON_MAX)
        return 0;
    return g_buttons[button];
}

Sint16 SDL_GameControllerGetAxis(SDL_GameController *gc,
                                 SDL_GameControllerAxis axis)
{
    (void)gc;
    if (axis == SDL_CONTROLLER_AXIS_LEFTX) return g_axis_x;
    if (axis == SDL_CONTROLLER_AXIS_LEFTY) return g_axis_y;
    return 0;
}

SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController *gc)
{
    (void)gc;
    return (SDL_Joystick *)GC;
}

SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *js)
{
    (void)js;
    return 0;
}

/* ------------------------------------------------------------------ */
/* EGL + the one program                                               */
/* ------------------------------------------------------------------ */

static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLConfig g_cfg;
static EGLContext g_ctx = EGL_NO_CONTEXT;
static EGLSurface g_surf = EGL_NO_SURFACE;
static int g_surf_gen = -1;             /* window generation the surface is on */
static int g_dev_w, g_dev_h;            /* device pixels */
static int g_scale = 2;                 /* logical -> device */

static GLuint g_prog;
static GLint g_a_pos, g_a_uv, g_u_screen, g_u_mode, g_u_color, g_u_tex;

/* Draw state (viewer thread only). */
static Uint8 g_draw_r = 0, g_draw_g = 0, g_draw_b = 0, g_draw_a = 255;
static SDL_BlendMode g_draw_blend = SDL_BLENDMODE_NONE;
static SDL_Rect g_clip;                 /* logical; w<=0 means none */
static int g_blend_on = -1;             /* GL blend cache */

static const char kVert[] =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "uniform vec2 u_screen;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "  vec2 c = a_pos / u_screen * 2.0 - 1.0;\n"
    "  gl_Position = vec4(c.x, -c.y, 0.0, 1.0);\n"
    "  v_uv = a_uv;\n"
    "}\n";

static const char kFrag[] =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "uniform int u_mode;\n"
    "uniform vec4 u_color;\n"
    "void main() {\n"
    "  vec4 t;\n"
    "  if (u_mode == 0) t = texture2D(u_tex, v_uv);\n"
    "  else if (u_mode == 1) t = texture2D(u_tex, v_uv).bgra;\n"
    "  else if (u_mode == 2) t = vec4(texture2D(u_tex, v_uv).bgr, 1.0);\n"
    "  else t = vec4(1.0);\n"
    "  gl_FragColor = t * u_color;\n"
    "}\n";

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    GLint ok = 0;

    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[256];

        glGetShaderInfoLog(s, sizeof log, NULL, log);
        LOGE("shader: %s", log);
    }
    return s;
}

static int gl_ready(void)
{
    return g_surf != EGL_NO_SURFACE;
}

/* Take (or retake) the window surface. Called on the viewer thread with the
 * lock held; returns holding it too. */
static int surface_up_locked(void)
{
    ANativeWindow *win = g_window;
    EGLint fmt = 0;

    if (win == NULL)
        return -1;
    eglGetConfigAttrib(g_dpy, g_cfg, EGL_NATIVE_VISUAL_ID, &fmt);
    ANativeWindow_setBuffersGeometry(win, 0, 0, fmt);
    g_surf = eglCreateWindowSurface(g_dpy, g_cfg, win, NULL);
    if (g_surf == EGL_NO_SURFACE) {
        LOGE("egl: no window surface (0x%x)", eglGetError());
        return -1;
    }
    if (!eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx)) {
        LOGE("egl: cannot make current (0x%x)", eglGetError());
        eglDestroySurface(g_dpy, g_surf);
        g_surf = EGL_NO_SURFACE;
        return -1;
    }
    eglSwapInterval(g_dpy, 1);
    {
        EGLint w = 0, h = 0;

        eglQuerySurface(g_dpy, g_surf, EGL_WIDTH, &w);
        eglQuerySurface(g_dpy, g_surf, EGL_HEIGHT, &h);
        g_dev_w = w;
        g_dev_h = h;
    }
    g_surf_gen = g_window_gen;
    g_have_surface = 1;
    g_blend_on = -1;
    LOGI("egl: surface %dx%d (ui scale %d)", g_dev_w, g_dev_h, g_scale);
    return 0;
}

static void surface_down_locked(void)
{
    if (g_surf == EGL_NO_SURFACE)
        return;
    eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(g_dpy, g_surf);
    g_surf = EGL_NO_SURFACE;
    g_have_surface = 0;
    pthread_cond_broadcast(&g_cond);
}

void mmo_sdlshim_set_window(ANativeWindow *win)
{
    LOGI("window: %s (gen %d)", win != NULL ? "granted" : "revoked",
         g_window_gen + 1);
    pthread_mutex_lock(&g_lock);
    g_window = win;
    g_window_gen++;
    if (win == NULL && g_have_surface) {
        /* Block until the render thread lets the old surface go: the window
         * is invalid the moment the caller's TERM_WINDOW returns. Keyed on
         * the surface and not on g_active, because CreateRenderer takes a
         * surface before it declares the renderer up. */
        g_release_asked = 1;
        pthread_cond_broadcast(&g_cond);
        while (g_have_surface)
            pthread_cond_wait(&g_cond, &g_lock);
        g_release_asked = 0;
    }
    pthread_cond_broadcast(&g_cond);
    pthread_mutex_unlock(&g_lock);
}

int mmo_sdlshim_active(void)
{
    return g_active;
}

/* ------------------------------------------------------------------ */
/* Window & renderer objects                                           */
/* ------------------------------------------------------------------ */

static struct SDL_Window { int unused; } g_win_obj;
static struct SDL_Renderer { int unused; } g_ren_obj;

void mmo_sdlshim_push_quit(void);

static pthread_t g_render_thread;

/* The viewer loop answers SDL_QUIT by closing its renderer, and g_active
 * going down is that, seen from here. An exit() raised on the render thread
 * itself needs none of it: that thread is in exit, not in the driver. */
static void shim_exit_sync(void)
{
    int i;

    if (!g_active)
        return;
    if (pthread_equal(pthread_self(), g_render_thread)) {
        LOGI("exit: from the render thread itself");
        return;
    }
    LOGI("exit: parking the viewer");
    mmo_sdlshim_push_quit();
    for (i = 0; i < 40 && g_active; i++)
        usleep(50 * 1000);
    LOGI("exit: viewer %s", g_active ? "is parked; leaving it" : "closed");
}

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h,
                             Uint32 flags)
{
    (void)title; (void)x; (void)y; (void)w; (void)h; (void)flags;
    return &g_win_obj;
}

void SDL_DestroyWindow(SDL_Window *win) { (void)win; }

void SDL_SetWindowTitle(SDL_Window *win, const char *title)
{
    (void)win;
    /* The desktop title bar is where "stalled" is said; here it is a line. */
    if (title != NULL)
        LOGI("title: %s", title);
}

void SDL_SetWindowSize(SDL_Window *win, int w, int h)
{
    (void)win; (void)w; (void)h;
}

void SDL_GetWindowSize(SDL_Window *win, int *w, int *h)
{
    (void)win;
    if (w != NULL) *w = g_dev_w / g_scale;
    if (h != NULL) *h = g_dev_h / g_scale;
}

int SDL_SetWindowFullscreen(SDL_Window *win, Uint32 flags)
{
    (void)win; (void)flags;
    return 0;
}

Uint32 SDL_GetWindowFlags(SDL_Window *win)
{
    (void)win;
    return SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_INPUT_FOCUS;
}

int SDL_GetWindowDisplayIndex(SDL_Window *win) { (void)win; return 0; }

int SDL_GetDisplayUsableBounds(int display, SDL_Rect *rect)
{
    (void)display;
    if (rect == NULL)
        return -1;
    rect->x = 0;
    rect->y = 0;
    rect->w = g_dev_w > 0 ? g_dev_w / g_scale : 960;
    rect->h = g_dev_h > 0 ? g_dev_h / g_scale : 540;
    return 0;
}

void SDL_RaiseWindow(SDL_Window *win) { (void)win; }
int SDL_SetWindowInputFocus(SDL_Window *win) { (void)win; return 0; }
const char *SDL_GetCurrentVideoDriver(void) { return "android-gles2"; }

SDL_Renderer *SDL_CreateRenderer(SDL_Window *win, int index, Uint32 flags)
{
    const EGLint want[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_NONE
    };
    const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLint n = 0;

    (void)win; (void)index; (void)flags;
    /* The front door and the viewer both ask; the screen is one. The second
     * caller gets the renderer the first one brought up, GL state and all. */
    if (g_active)
        return &g_ren_obj;
    {
        const char *e = getenv("OPENMMO_UI_SCALE");

        if (e != NULL && *e != '\0') {
            g_scale = atoi(e);
            if (g_scale < 1) g_scale = 1;
            if (g_scale > 4) g_scale = 4;
        }
    }
    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_dpy == EGL_NO_DISPLAY || !eglInitialize(g_dpy, NULL, NULL)) {
        set_err("no EGL display");
        return NULL;
    }
    if (!eglChooseConfig(g_dpy, want, &g_cfg, 1, &n) || n < 1) {
        set_err("no ES2 8888 window config");
        return NULL;
    }
    g_ctx = eglCreateContext(g_dpy, g_cfg, EGL_NO_CONTEXT, ctx_attr);
    if (g_ctx == EGL_NO_CONTEXT) {
        set_err("no ES2 context");
        return NULL;
    }
    LOGI("renderer: EGL up, waiting for the window");

    /* Wait for the framework's first window, then take it. */
    pthread_mutex_lock(&g_lock);
    while (g_window == NULL)
        pthread_cond_wait(&g_cond, &g_lock);
    if (surface_up_locked() != 0) {
        pthread_mutex_unlock(&g_lock);
        set_err("no EGL surface");
        return NULL;
    }
    pthread_mutex_unlock(&g_lock);

    g_prog = glCreateProgram();
    glAttachShader(g_prog, compile(GL_VERTEX_SHADER, kVert));
    glAttachShader(g_prog, compile(GL_FRAGMENT_SHADER, kFrag));
    glLinkProgram(g_prog);
    {
        GLint ok = 0;

        glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
        if (!ok) {
            set_err("GL program did not link");
            return NULL;
        }
    }
    g_a_pos = glGetAttribLocation(g_prog, "a_pos");
    g_a_uv = glGetAttribLocation(g_prog, "a_uv");
    g_u_screen = glGetUniformLocation(g_prog, "u_screen");
    g_u_mode = glGetUniformLocation(g_prog, "u_mode");
    g_u_color = glGetUniformLocation(g_prog, "u_color");
    g_u_tex = glGetUniformLocation(g_prog, "u_tex");
    glUseProgram(g_prog);
    glUniform1i(g_u_tex, 0);
    glActiveTexture(GL_TEXTURE0);
    glEnableVertexAttribArray((GLuint)g_a_pos);
    glEnableVertexAttribArray((GLuint)g_a_uv);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    g_clip.w = 0;
    g_render_thread = pthread_self();
    g_active = 1;
    /*
     * Registered here, after eglInitialize, and the order is the point: the Mali driver put
     * its own exit-time destructors on the list when EGL loaded it a moment ago, and exit()
     * runs the list backwards.
     */
    atexit(shim_exit_sync);
    LOGI("renderer: up, %s", (const char *)glGetString(GL_RENDERER));
    /* The driver's name is a fact about the device the defaults read
     * (mmo_device.c): a software rasteriser caps the 3D scale. */
    mmo_device_note_gl((const char *)glGetString(GL_RENDERER));
    return &g_ren_obj;
}

void SDL_DestroyRenderer(SDL_Renderer *ren)
{
    (void)ren;
    pthread_mutex_lock(&g_lock);
    surface_down_locked();
    pthread_mutex_unlock(&g_lock);
    g_active = 0;
}

int SDL_GetRendererInfo(SDL_Renderer *ren, SDL_RendererInfo *info)
{
    (void)ren;
    if (info == NULL)
        return -1;
    info->name = "gles2-shim";
    info->flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;
    return 0;
}

int SDL_GetRendererOutputSize(SDL_Renderer *ren, int *w, int *h)
{
    (void)ren;
    if (w != NULL) *w = g_dev_w / g_scale;
    if (h != NULL) *h = g_dev_h / g_scale;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Textures                                                            */
/* ------------------------------------------------------------------ */

struct SDL_Texture {
    GLuint id;
    int w, h;
    Uint32 format;
    int access;
    SDL_BlendMode blend;
    Uint8 cr, cg, cb, ca;
    void *shadow;                /* streaming lock buffer, w*4 pitch */
    SDL_Rect locked;
};

static int tex_mode(const SDL_Texture *t)
{
    switch (t->format) {
    case SDL_PIXELFORMAT_ABGR8888: return 0;   /* memory R,G,B,A */
    case SDL_PIXELFORMAT_ARGB8888: return 1;   /* memory B,G,R,A */
    default:                       return 2;   /* XRGB: B,G,R,x, opaque */
    }
}

SDL_Texture *SDL_CreateTexture(SDL_Renderer *ren, Uint32 format, int access,
                               int w, int h)
{
    SDL_Texture *t;
    GLint filter = g_hint_linear ? GL_LINEAR : GL_NEAREST;

    (void)ren;
    if (!gl_ready() || w < 1 || h < 1) {
        set_err("no surface to create a texture on");
        return NULL;
    }
    t = calloc(1, sizeof *t);
    if (t == NULL)
        return NULL;
    t->w = w;
    t->h = h;
    t->format = format;
    t->access = access;
    t->blend = SDL_BLENDMODE_NONE;
    t->cr = t->cg = t->cb = t->ca = 255;
    glGenTextures(1, &t->id);
    glBindTexture(GL_TEXTURE_2D, t->id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    if (access == SDL_TEXTUREACCESS_STREAMING) {
        t->shadow = calloc((size_t)w * (size_t)h, 4);
        if (t->shadow == NULL) {
            glDeleteTextures(1, &t->id);
            free(t);
            return NULL;
        }
    }
    return t;
}

void SDL_DestroyTexture(SDL_Texture *tex)
{
    if (tex == NULL)
        return;
    if (gl_ready())
        glDeleteTextures(1, &tex->id);
    free(tex->shadow);
    free(tex);
}

static void tex_upload(SDL_Texture *t, int x, int y, int w, int h,
                       const void *pixels, int pitch)
{
    if (!gl_ready())
        return;
    glBindTexture(GL_TEXTURE_2D, t->id);
    if (pitch == w * 4) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA,
                        GL_UNSIGNED_BYTE, pixels);
    } else {
        /* ES2 has no UNPACK_ROW_LENGTH; feed it row by row. */
        const unsigned char *p = pixels;
        int r;

        for (r = 0; r < h; r++)
            glTexSubImage2D(GL_TEXTURE_2D, 0, x, y + r, w, 1, GL_RGBA,
                            GL_UNSIGNED_BYTE, p + (size_t)r * (size_t)pitch);
    }
}

int SDL_UpdateTexture(SDL_Texture *tex, const SDL_Rect *rect,
                      const void *pixels, int pitch)
{
    int x = 0, y = 0, w, h;

    if (tex == NULL || pixels == NULL)
        return -1;
    w = tex->w;
    h = tex->h;
    if (rect != NULL) {
        x = rect->x;
        y = rect->y;
        w = rect->w;
        h = rect->h;
    }
    tex_upload(tex, x, y, w, h, pixels, pitch);
    return 0;
}

int SDL_LockTexture(SDL_Texture *tex, const SDL_Rect *rect, void **pixels,
                    int *pitch)
{
    if (tex == NULL || tex->shadow == NULL || pixels == NULL || pitch == NULL)
        return -1;
    if (rect != NULL)
        tex->locked = *rect;
    else {
        tex->locked.x = 0;
        tex->locked.y = 0;
        tex->locked.w = tex->w;
        tex->locked.h = tex->h;
    }
    *pitch = tex->w * 4;
    *pixels = (unsigned char *)tex->shadow
              + ((size_t)tex->locked.y * (size_t)tex->w
                 + (size_t)tex->locked.x) * 4;
    return 0;
}

void SDL_UnlockTexture(SDL_Texture *tex)
{
    if (tex == NULL || tex->shadow == NULL)
        return;
    tex_upload(tex, tex->locked.x, tex->locked.y, tex->locked.w,
               tex->locked.h,
               (unsigned char *)tex->shadow
                   + ((size_t)tex->locked.y * (size_t)tex->w
                      + (size_t)tex->locked.x) * 4,
               tex->w * 4);
}

int SDL_SetTextureColorMod(SDL_Texture *tex, Uint8 r, Uint8 g, Uint8 b)
{
    if (tex == NULL)
        return -1;
    tex->cr = r;
    tex->cg = g;
    tex->cb = b;
    return 0;
}

int SDL_SetTextureAlphaMod(SDL_Texture *tex, Uint8 a)
{
    if (tex == NULL)
        return -1;
    tex->ca = a;
    return 0;
}

int SDL_SetTextureBlendMode(SDL_Texture *tex, SDL_BlendMode mode)
{
    if (tex == NULL)
        return -1;
    tex->blend = mode;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Surfaces                                                            */
/* ------------------------------------------------------------------ */

SDL_Surface *SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int w, int h,
                                            int depth, Uint32 format)
{
    SDL_Surface *sf;

    (void)flags; (void)depth;
    if (w < 1 || h < 1)
        return NULL;
    sf = calloc(1, sizeof *sf);
    if (sf == NULL)
        return NULL;
    sf->format = format;
    sf->w = w;
    sf->h = h;
    sf->pitch = w * 4;
    sf->pixels = calloc((size_t)w * (size_t)h, 4);
    if (sf->pixels == NULL) {
        free(sf);
        return NULL;
    }
    return sf;
}

void SDL_FreeSurface(SDL_Surface *sf)
{
    if (sf == NULL)
        return;
    free(sf->pixels);
    free(sf);
}

int SDL_LockSurface(SDL_Surface *sf) { (void)sf; return 0; }
void SDL_UnlockSurface(SDL_Surface *sf) { (void)sf; }

SDL_Texture *SDL_CreateTextureFromSurface(SDL_Renderer *ren, SDL_Surface *sf)
{
    SDL_Texture *t;

    if (sf == NULL)
        return NULL;
    t = SDL_CreateTexture(ren, sf->format, SDL_TEXTUREACCESS_STATIC,
                          sf->w, sf->h);
    if (t == NULL)
        return NULL;
    tex_upload(t, 0, 0, sf->w, sf->h, sf->pixels, sf->pitch);
    return t;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

int SDL_SetRenderDrawColor(SDL_Renderer *ren, Uint8 r, Uint8 g, Uint8 b,
                           Uint8 a)
{
    (void)ren;
    g_draw_r = r;
    g_draw_g = g;
    g_draw_b = b;
    g_draw_a = a;
    return 0;
}

int SDL_SetRenderDrawBlendMode(SDL_Renderer *ren, SDL_BlendMode mode)
{
    (void)ren;
    g_draw_blend = mode;
    return 0;
}

int SDL_GetRenderDrawBlendMode(SDL_Renderer *ren, SDL_BlendMode *mode)
{
    (void)ren;
    if (mode != NULL)
        *mode = g_draw_blend;
    return 0;
}

static void want_blend(int on)
{
    if (g_blend_on == on)
        return;
    g_blend_on = on;
    if (on) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
}

static void apply_clip(void)
{
    if (g_clip.w > 0 && g_clip.h > 0) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(g_clip.x * g_scale,
                  g_dev_h - (g_clip.y + g_clip.h) * g_scale,
                  g_clip.w * g_scale, g_clip.h * g_scale);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
}

/* One quad in device pixels, current program state. */
static void quad(float x, float y, float w, float h, float u0, float v0,
                 float u1, float v1)
{
    GLfloat pos[8];
    GLfloat uv[8];

    pos[0] = x;     pos[1] = y;
    pos[2] = x + w; pos[3] = y;
    pos[4] = x;     pos[5] = y + h;
    pos[6] = x + w; pos[7] = y + h;
    uv[0] = u0; uv[1] = v0;
    uv[2] = u1; uv[3] = v0;
    uv[4] = u0; uv[5] = v1;
    uv[6] = u1; uv[7] = v1;
    glVertexAttribPointer((GLuint)g_a_pos, 2, GL_FLOAT, GL_FALSE, 0, pos);
    glVertexAttribPointer((GLuint)g_a_uv, 2, GL_FLOAT, GL_FALSE, 0, uv);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void solid_dev(float x, float y, float w, float h, Uint8 r, Uint8 g,
                      Uint8 b, Uint8 a, int blend)
{
    if (!gl_ready())
        return;
    glUseProgram(g_prog);
    glUniform2f(g_u_screen, (float)g_dev_w, (float)g_dev_h);
    glUniform1i(g_u_mode, 3);
    glUniform4f(g_u_color, r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
    want_blend(blend);
    apply_clip();
    quad(x, y, w, h, 0, 0, 1, 1);
}

int SDL_RenderClear(SDL_Renderer *ren)
{
    (void)ren;
    if (!gl_ready())
        return 0;
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, g_dev_w, g_dev_h);
    glClearColor(g_draw_r / 255.0f, g_draw_g / 255.0f, g_draw_b / 255.0f,
                 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    return 0;
}

int SDL_RenderFillRect(SDL_Renderer *ren, const SDL_Rect *rect)
{
    SDL_Rect r;

    (void)ren;
    if (rect == NULL) {
        r.x = 0;
        r.y = 0;
        r.w = g_dev_w / g_scale;
        r.h = g_dev_h / g_scale;
    } else {
        r = *rect;
    }
    solid_dev((float)r.x * g_scale, (float)r.y * g_scale,
              (float)r.w * g_scale, (float)r.h * g_scale,
              g_draw_r, g_draw_g, g_draw_b, g_draw_a,
              g_draw_blend == SDL_BLENDMODE_BLEND);
    return 0;
}

int SDL_RenderDrawRect(SDL_Renderer *ren, const SDL_Rect *rect)
{
    SDL_Rect e;

    if (rect == NULL || rect->w < 1 || rect->h < 1)
        return 0;
    e = *rect;
    e.h = 1;
    SDL_RenderFillRect(ren, &e);                    /* top */
    e.y = rect->y + rect->h - 1;
    SDL_RenderFillRect(ren, &e);                    /* bottom */
    e = *rect;
    e.w = 1;
    SDL_RenderFillRect(ren, &e);                    /* left */
    e.x = rect->x + rect->w - 1;
    SDL_RenderFillRect(ren, &e);                    /* right */
    return 0;
}

int SDL_RenderCopy(SDL_Renderer *ren, SDL_Texture *tex, const SDL_Rect *src,
                   const SDL_Rect *dst)
{
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    SDL_Rect d;

    (void)ren;
    if (tex == NULL || !gl_ready())
        return -1;
    if (src != NULL) {
        u0 = (float)src->x / (float)tex->w;
        v0 = (float)src->y / (float)tex->h;
        u1 = (float)(src->x + src->w) / (float)tex->w;
        v1 = (float)(src->y + src->h) / (float)tex->h;
    }
    if (dst != NULL) {
        d = *dst;
    } else {
        d.x = 0;
        d.y = 0;
        d.w = g_dev_w / g_scale;
        d.h = g_dev_h / g_scale;
    }
    glUseProgram(g_prog);
    glUniform2f(g_u_screen, (float)g_dev_w, (float)g_dev_h);
    glUniform1i(g_u_mode, tex_mode(tex));
    glUniform4f(g_u_color, tex->cr / 255.0f, tex->cg / 255.0f,
                tex->cb / 255.0f, tex->ca / 255.0f);
    want_blend(tex->blend == SDL_BLENDMODE_BLEND);
    apply_clip();
    glBindTexture(GL_TEXTURE_2D, tex->id);
    quad((float)d.x * g_scale, (float)d.y * g_scale, (float)d.w * g_scale,
         (float)d.h * g_scale, u0, v0, u1, v1);
    return 0;
}

int SDL_RenderSetClipRect(SDL_Renderer *ren, const SDL_Rect *rect)
{
    (void)ren;
    if (rect == NULL || rect->w < 1 || rect->h < 1) {
        g_clip.x = g_clip.y = g_clip.w = g_clip.h = 0;
    } else {
        g_clip = *rect;
    }
    return 0;
}

void SDL_RenderGetClipRect(SDL_Renderer *ren, SDL_Rect *rect)
{
    (void)ren;
    if (rect != NULL)
        *rect = g_clip;
}

int SDL_RenderReadPixels(SDL_Renderer *ren, const SDL_Rect *rect,
                         Uint32 format, void *pixels, int pitch)
{
    int lw = g_dev_w / g_scale, lh = g_dev_h / g_scale;
    unsigned char *dev;
    int x, y;

    (void)ren; (void)format;
    if (!gl_ready() || pixels == NULL)
        return -1;
    if (rect != NULL) {
        /* Nothing in the viewer passes one; keep the claim honest. */
        set_err("rect readback not implemented");
        return -1;
    }
    dev = malloc((size_t)g_dev_w * (size_t)g_dev_h * 4);
    if (dev == NULL)
        return -1;
    glFinish();
    glReadPixels(0, 0, g_dev_w, g_dev_h, GL_RGBA, GL_UNSIGNED_BYTE, dev);
    /* GL reads bottom-up RGBA; the caller wants top-down XRGB words at the
     * Logical size, so sample every scale-th pixel. */
    for (y = 0; y < lh; y++) {
        Uint32 *row = (Uint32 *)((char *)pixels + (size_t)y * (size_t)pitch);
        int dy = g_dev_h - 1 - y * g_scale;
        const unsigned char *src = dev + (size_t)dy * (size_t)g_dev_w * 4;

        for (x = 0; x < lw; x++) {
            const unsigned char *p = src + (size_t)x * (size_t)g_scale * 4;

            row[x] = ((Uint32)p[0] << 16) | ((Uint32)p[1] << 8) | p[2];
        }
    }
    free(dev);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The OSK                                                             */
/* ------------------------------------------------------------------ */
/*
 * Drawn inside RenderPresent, over everything, while a text field is open. The image is
 * rastered on the CPU with the 5x7 face and uploaded once per shift state; a tap flashes the
 * key with a translucent fill.
 */

#define OSK_ROWS 5
#define OSK_COLS 10

/* One key: what it types lowercase/shifted, or a control code. */
#define OSK_SHIFT  1
#define OSK_BS     2
#define OSK_ENTER  3
#define OSK_CLOSE  4
#define OSK_SPACE  5

static const char *const kRows[OSK_ROWS] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl'",
    "\001zxcvbnm-\002",
    "\005,.!?\003\004"
};
static const char *const kRowsUp[OSK_ROWS] = {
    "1234567890",
    "QWERTYUIOP",
    "ASDFGHJKL\"",
    "\001ZXCVBNM_\002",
    "\005,.!?\003\004"
};

/* The space row spreads: the seven keys cover the ten columns exactly,
 * space two wide, the send and hide pair two each. */
static const signed char kWidth4[OSK_COLS] = { 2, 1, 1, 1, 1, 2, 2, 0, 0, 0 };

struct osk_state {
    int shift;
    int built_shift;             /* what the image was built with; -1 never */
    int built_w, built_h;        /* the surface the image was cut for */
    GLuint tex;
    GLuint tab_tex;              /* the one key that is left when it is away */
    int img_w, img_h;            /* image pixels (= device pixels) */
    int x, y;                    /* on the glass, device px */
    int cell;                    /* one key cell, device px */
    int flash_row, flash_col;    /* last tap, for the flash */
    uint64_t flash_until_ms;
    /*
     * Put away by the player, which is not the same as the field closing. HIDE is the pad
     * toggle's kind of button: the panel goes, the half-typed line stays, and the key stays on
     * the glass reading KEYS so there is a way back.
     */
    int user_hidden;
};

static struct osk_state g_osk = { .built_shift = -1, .flash_row = -1,
                                  .flash_col = -1 };

static const char *osk_row(int r)
{
    return (g_osk.shift ? kRowsUp : kRows)[r];
}

static void osk_geometry(void)
{
    /* Right side, above the bottom edge: the chat box composes bottom-left
     * and must stay visible under a keyboard. */
    g_osk.cell = g_dev_h / 10;                  /* 108 px on this panel */
    g_osk.img_w = g_osk.cell * OSK_COLS;
    g_osk.img_h = g_osk.cell * OSK_ROWS;
    g_osk.x = g_dev_w - g_osk.img_w - g_dev_h / 36;
    g_osk.y = g_dev_h - g_osk.img_h - g_dev_h / 36;
}

/* The corner HIDE sits in, which is where KEYS sits once it has been pressed:
 * the toggle does not move when it is used. */
static void osk_tab_rect(int *x, int *y, int *w, int *h)
{
    *w = g_osk.cell * 2;
    *h = g_osk.cell;
    *x = g_osk.x + g_osk.img_w - *w;
    *y = g_osk.y + g_osk.img_h - *h;
}

static const char *osk_label(char c, char *buf)
{
    switch (c) {
    case OSK_SHIFT: return "SHIFT";
    case OSK_BS:    return "DEL";
    case OSK_ENTER: return "SEND";
    case OSK_CLOSE: return "HIDE";
    case OSK_SPACE: return "SPACE";
    default:
        buf[0] = c;
        buf[1] = '\0';
        return buf;
    }
}

static void osk_build(void)
{
    uint32_t *px;
    int r;

    osk_geometry();
    px = malloc((size_t)g_osk.img_w * (size_t)g_osk.img_h * 4);
    if (px == NULL)
        return;
    for (r = 0; r < g_osk.img_w * g_osk.img_h; r++)
        px[r] = 0x14181E;
    for (r = 0; r < OSK_ROWS; r++) {
        const char *row = osk_row(r);
        int col = 0, i;

        for (i = 0; row[i] != '\0'; i++) {
            int span = r == OSK_ROWS - 1 ? kWidth4[i] : 1;
            int x0 = col * g_osk.cell, y0 = r * g_osk.cell;
            int w = span * g_osk.cell, h = g_osk.cell;
            char one[2];
            const char *label = osk_label(row[i], one);
            int scale = label[1] == '\0' ? 5 : 2;
            int y, x;

            if (span < 1)
                break;
            /* Key face and a one-pixel seam. */
            for (y = y0 + 2; y < y0 + h - 2; y++)
                for (x = x0 + 2; x < x0 + w - 2; x++)
                    px[(size_t)y * g_osk.img_w + x] =
                        row[i] == OSK_SHIFT && g_osk.shift ? 0x35507A
                                                           : 0x232A33;
            openmmo_font_draw_centred(px, g_osk.img_w, g_osk.img_h,
                                      x0 + w / 2,
                                      y0 + (h - 7 * scale) / 2, label, scale,
                                      0xE8ECF0);
            col += span;
        }
    }
    if (g_osk.tex == 0)
        glGenTextures(1, &g_osk.tex);
    glBindTexture(GL_TEXTURE_2D, g_osk.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_osk.img_w, g_osk.img_h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, px);
    free(px);

    /* The KEYS tab, in an image of its own so the panel's ten-by-five grid
     * stays exactly a grid of keys. */
    {
        int w = g_osk.cell * 2, h = g_osk.cell, x, y;

        px = malloc((size_t)w * (size_t)h * 4);
        if (px != NULL) {
            for (y = 0; y < w * h; y++)
                px[y] = 0x14181E;
            for (y = 2; y < h - 2; y++)
                for (x = 2; x < w - 2; x++)
                    px[(size_t)y * w + x] = 0x232A33;
            openmmo_font_draw_centred(px, w, h, w / 2, (h - 7 * 2) / 2,
                                      "KEYS", 2, 0xE8ECF0);
            if (g_osk.tab_tex == 0)
                glGenTextures(1, &g_osk.tab_tex);
            glBindTexture(GL_TEXTURE_2D, g_osk.tab_tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, px);
            free(px);
        }
    }
    g_osk.built_shift = g_osk.shift;
    g_osk.built_w = g_dev_w;
    g_osk.built_h = g_dev_h;
}

/* PC_ANDROID_KEYLOG=1, the frontend's own knob for "name every key and pen
 * event", says here which of the two overlapping controls took a finger.
 * A tap that reaches neither prints nothing and is the pen's. */
static int touch_log(void)
{
    static int v = -1;

    if (v < 0) {
        const char *e = getenv("PC_ANDROID_KEYLOG");

        v = (e != NULL && e[0] != '\0' && e[0] != '0') ? 1 : 0;
    }
    return v;
}

/* OPENMMO_OSK_TEST=1 pins the keyboard up with no text field open: the only
 * way to look at its layout on a screen that has no chat yet. */
static int osk_forced(void)
{
    static int v = -1;

    if (v < 0) {
        const char *e = getenv("OPENMMO_OSK_TEST");

        v = (e != NULL && e[0] != '\0' && e[0] != '0') ? 1 : 0;
    }
    return v;
}

/* Is the keyboard on the glass, panel, or the tab it leaves behind? Asked
 * with the lock held, and it answers no until the geometry has been cut, so a
 * hit test never runs against a rectangle of zeroes. */
static int osk_on_locked(void)
{
    return (g_textinput || osk_forced()) && g_osk.cell > 0;
}

static void osk_draw(void)
{
    int tx, ty, tw, th;

    if ((!g_textinput && !osk_forced()) || !gl_ready())
        return;
    /* A rotation or a resize moves every key: the pad rebuilds on the same
     * test and the keyboard used to rebuild on neither. */
    if (g_osk.built_shift != g_osk.shift || g_osk.tex == 0
        || g_osk.built_w != g_dev_w || g_osk.built_h != g_dev_h)
        osk_build();
    if (g_osk.tex == 0)
        return;
    glUseProgram(g_prog);
    glUniform2f(g_u_screen, (float)g_dev_w, (float)g_dev_h);
    glDisable(GL_SCISSOR_TEST);
    glUniform1i(g_u_mode, 2);
    glUniform4f(g_u_color, 1, 1, 1, 1);
    want_blend(0);
    if (g_osk.user_hidden) {
        osk_tab_rect(&tx, &ty, &tw, &th);
        if (g_osk.tab_tex != 0) {
            glBindTexture(GL_TEXTURE_2D, g_osk.tab_tex);
            quad((float)tx, (float)ty, (float)tw, (float)th, 0, 0, 1, 1);
        }
        return;
    }
    glBindTexture(GL_TEXTURE_2D, g_osk.tex);
    quad((float)g_osk.x, (float)g_osk.y, (float)g_osk.img_w,
         (float)g_osk.img_h, 0, 0, 1, 1);
    if (g_osk.flash_row >= 0 && SDL_GetTicks64() < g_osk.flash_until_ms) {
        glUniform1i(g_u_mode, 3);
        glUniform4f(g_u_color, 1, 1, 1, 0.35f);
        want_blend(1);
        quad((float)(g_osk.x + g_osk.flash_col * g_osk.cell),
             (float)(g_osk.y + g_osk.flash_row * g_osk.cell),
             (float)g_osk.cell, (float)g_osk.cell, 0, 0, 1, 1);
    }
}

/* Push the events one key answers. Called with g_lock held. */
static void osk_key_locked(char c)
{
    SDL_Event ev;

    memset(&ev, 0, sizeof ev);
    switch (c) {
    case OSK_SHIFT:
        g_osk.shift = !g_osk.shift;
        return;
    case OSK_BS:
        ev.type = SDL_KEYDOWN;
        ev.key.keysym.sym = SDLK_BACKSPACE;
        ev.key.keysym.scancode = SDL_SCANCODE_BACKSPACE;
        push_event(&ev);
        return;
    case OSK_ENTER:
        ev.type = SDL_KEYDOWN;
        ev.key.keysym.sym = SDLK_RETURN;
        ev.key.keysym.scancode = SDL_SCANCODE_RETURN;
        push_event(&ev);
        return;
    case OSK_CLOSE:
        /* HIDE is A HIDE, not an ESCAPE. */
        g_osk.user_hidden = 1;
        return;
    case OSK_SPACE:
        c = ' ';
        /* fall through */
    default:
        ev.type = SDL_TEXTINPUT;
        ev.text.text[0] = c;
        push_event(&ev);
        if (g_osk.shift) {
            g_osk.shift = 0;    /* one-shot, the phone convention */
        }
        return;
    }
}

/* The key under a finger that has just landed. Lock held. */
static void osk_press_locked(float fx, float fy)
{
    int col = ((int)fx - g_osk.x) / g_osk.cell;
    int row = ((int)fy - g_osk.y) / g_osk.cell;
    int i, span_col;
    const char *rowstr;

    if (row < 0 || row >= OSK_ROWS || col < 0 || col >= OSK_COLS)
        return;
    rowstr = osk_row(row);
    if (row == OSK_ROWS - 1) {
        span_col = 0;
        for (i = 0; rowstr[i] != '\0'; i++) {
            if (col < span_col + kWidth4[i])
                break;
            span_col += kWidth4[i];
        }
        if (rowstr[i] == '\0')
            return;
    } else {
        i = col;
        if (i >= (int)strlen(rowstr))
            return;
    }
    g_osk.flash_row = row;
    g_osk.flash_col = row == OSK_ROWS - 1 ? span_col : col;
    g_osk.flash_until_ms = SDL_GetTicks64() + 150;
    if (touch_log()) {
        char one[2];

        LOGI("osk: %.0f,%.0f -> row %d col %d key %s", fx, fy, row, col,
             osk_label(rowstr[i], one));
    }
    osk_key_locked(rowstr[i]);
}

/*
 * Which pointers are the keyboard'S. Every one inside the panel, claimed whatever it is doing:
 * a finger resting on the keys is not a pen press either.
 */
static uint32_t osk_take_locked(int action, const struct mmo_sdlshim_pt *pts,
                                int n, int act_index)
{
    uint32_t taken = 0;
    int i, x, y, w, h;

    if (!osk_on_locked())
        return 0;
    if (g_osk.user_hidden) {
        osk_tab_rect(&x, &y, &w, &h);
        for (i = 0; i < n; i++) {
            if (pts[i].x < (float)x || pts[i].x >= (float)(x + w)
                || pts[i].y < (float)y || pts[i].y >= (float)(y + h))
                continue;
            taken |= 1u << i;
            if (action == 0 && i == act_index)
                g_osk.user_hidden = 0;
        }
        return taken;
    }
    for (i = 0; i < n; i++) {
        if (pts[i].x < (float)g_osk.x
            || pts[i].x >= (float)(g_osk.x + g_osk.img_w)
            || pts[i].y < (float)g_osk.y
            || pts[i].y >= (float)(g_osk.y + g_osk.img_h))
            continue;
        taken |= 1u << i;
        if (action == 0 && i == act_index)
            osk_press_locked(pts[i].x, pts[i].y);
    }
    return taken;
}

/* ------------------------------------------------------------------ */
/* Present                                                             */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* The on-screen pad                                                   */
/* ------------------------------------------------------------------ */
/*
 * Why it exists. The RG556 this port was written on has real sticks and real face buttons, so
 * every control question so far has been "which keycode is that thumb".
 */

/* SDL_CONTROLLER_BUTTON_* by number: see the note above about the letters. */
#define PAD_SDL_A       1   /* the button drawn "A" -> SDL B -> DS A */
#define PAD_SDL_B       0
#define PAD_SDL_X       3
#define PAD_SDL_Y       2
#define PAD_SDL_L       9
#define PAD_SDL_R      10
#define PAD_SDL_START   6
#define PAD_SDL_SELECT  4
#define PAD_SDL_UP     11
#define PAD_SDL_DOWN   12
#define PAD_SDL_LEFT   13
#define PAD_SDL_RIGHT  14

/* The buttons that are plain rectangles. The d-pad is not one of them: it is
 * a single square read by direction, which is what makes its whole area live
 * instead of five thin arms with dead seams between them. */
struct pad_btn {
    int x, y, w, h;
    int sdl;
    const char *label;
};

#define PAD_BTN_N 8
/* The label atlas holds the eight buttons plus the toggle's two captions,
 * which are picked between at draw time rather than rebuilt on every press. */
#define PAD_CELL_HIDE (PAD_BTN_N)
#define PAD_CELL_SHOW (PAD_BTN_N + 1)
#define PAD_CELLS     (PAD_BTN_N + 2)

struct pad_state {
    int built_w, built_h;        /* the surface the geometry was cut for */
    int shown;
    GLuint tex;                  /* the labels, one cell per button, in a row */
    int cell_w, cell_h;          /* atlas cell, texels */
    struct pad_btn b[PAD_BTN_N];
    int cell_lw[PAD_CELLS], cell_lh[PAD_CELLS];  /* each label's real extent */
    /* The toggle. Top centre, and the one control that stays on the glass
     * when everything else is put away, a hide button that hides itself is
     * a pad you cannot get back. */
    int hide_x, hide_y, hide_w, hide_h;
    int hidden;
    int hide_finger;             /* a finger is on it NOW, for edge detection:
                                    a press toggles once, not once per motion
                                    event for as long as it is held */
    int dp_x, dp_y, dp_side;     /* the d-pad square */
    uint32_t held;               /* bit per PAD_BTN_N, plus the four below */
    uint32_t held_dir;           /* OPENMMO-independent: bit 0..3 U D L R */
    int saw_gamepad;
    int enabled;                 /* the frontend has handed over the game */
};

static struct pad_state g_pad;

/* mmo_sdlshim_button's body without the lock. The pad already holds it, and
 * the deferred-release counters below are the same ones a real button uses:
 * a drawn tap shorter than a frame has to survive to be sampled, exactly as
 * an `adb shell input keyevent` one does. */
static void pad_set_locked(int button, int down)
{
    if (button < 0 || button >= SDL_CONTROLLER_BUTTON_MAX)
        return;
    if (down) {
        g_buttons[button] = 1;
        g_button_frames[button] = 0;
        g_button_pending_up[button] = 0;
    } else if (g_button_frames[button] < 3) {
        g_button_pending_up[button] = 1;
    } else {
        g_buttons[button] = 0;
    }
}

/* Let go of everything the pad was holding. Called with g_lock held. */
static void pad_release_locked(void)
{
    static const int kAll[] = { PAD_SDL_A, PAD_SDL_B, PAD_SDL_X, PAD_SDL_Y,
                                PAD_SDL_L, PAD_SDL_R, PAD_SDL_START,
                                PAD_SDL_SELECT, PAD_SDL_UP, PAD_SDL_DOWN,
                                PAD_SDL_LEFT, PAD_SDL_RIGHT };
    int i;

    if (g_pad.held == 0 && g_pad.held_dir == 0)
        return;
    for (i = 0; i < (int)(sizeof kAll / sizeof kAll[0]); i++)
        pad_set_locked(kAll[i], 0);
    g_pad.held = 0;
    g_pad.held_dir = 0;
}

/*
 * OPENMMO_TOUCH_PAD=1 always, =0 never, unset (or anything else) is auto: drawn until a real
 * gamepad speaks, then never again this run.
 */
static int pad_mode(void)
{
    const char *e = getenv("OPENMMO_TOUCH_PAD");

    if (e == NULL || *e == '\0') return 2;      /* auto */
    if (e[0] == '0')             return 0;
    if (e[0] == '1')             return 1;
    return 2;
}

static int pad_on(void)
{
    int m = pad_mode();

    if (!g_pad.enabled)
        return 0;
    /*
     * Not while the keyboard is UP. The OSK covers the bottom right of the glass, ten
     * columns of it, which is exactly where the face buttons and Start/Select are, and the
     * pad claims its pointers BEFORE the OSK is offered them.
     */
    if (g_textinput || osk_forced())
        return 0;
    if (m == 0) return 0;
    if (m == 1) return 1;
    return !g_pad.saw_gamepad;
}

void mmo_sdlshim_pad_enable(int on)
{
    pthread_mutex_lock(&g_lock);
    g_pad.enabled = on ? 1 : 0;
    if (!on)
        pad_release_locked();
    pthread_mutex_unlock(&g_lock);
}

void mmo_sdlshim_pad_saw_gamepad(void)
{
    pthread_mutex_lock(&g_lock);
    if (!g_pad.saw_gamepad) {
        g_pad.saw_gamepad = 1;
        /* Whatever it was holding goes with it, or a pad that vanishes
         * mid-press leaves the button down forever. */
        pad_release_locked();
    }
    pthread_mutex_unlock(&g_lock);
}

/*
 * TUNING KNOBS, read once. openmmo.env is where an app with no command line gets them, and
 * they exist because the right answer here is a matter of the hand holding the device and
 * cannot be settled from this side of the glass.
 */
static int pad_pct(const char *name, int dflt)
{
    const char *e = getenv(name);
    int v;

    if (e == NULL || *e == '\0')
        return dflt;
    v = atoi(e);
    return v < 0 ? 0 : v > 400 ? 400 : v;
}

/* "X,Y" or "X%,Y%" into device pixels. Returns 0 if unset or unreadable, so
 * a typo falls back to the built-in place rather than to the corner. */
static int pad_pos(const char *name, int *x, int *y)
{
    const char *e = getenv(name);
    char buf[64], *comma;
    double fx, fy;

    if (e == NULL || *e == '\0')
        return 0;
    snprintf(buf, sizeof buf, "%s", e);
    comma = strchr(buf, ',');
    if (comma == NULL)
        return 0;
    *comma = '\0';
    fx = atof(buf);
    fy = atof(comma + 1);
    if (strchr(buf, '%') != NULL)
        fx = fx * (double)g_dev_w / 100.0;
    if (strchr(comma + 1, '%') != NULL)
        fy = fy * (double)g_dev_h / 100.0;
    *x = (int)fx;
    *y = (int)fy;
    return 1;
}

/* Where the controls go. */
static void pad_geometry(void)
{
    int u = g_dev_h / 9;
    int m, cx, cy, s, d, mid, i, ox, oy;

    u = u * pad_pct("OPENMMO_TOUCH_SIZE", 100) / 100;
    if (u < 8)
        u = 8;
    if (u > g_dev_h / 4)
        u = g_dev_h / 4;
    m = u / 2;

    /* The d-pad, bottom left, under the left thumb. It stays there: the
     * screen it covers is the upper one, which is drawn at and never
     * touched, so it blocks nothing a finger wants. */
    g_pad.dp_side = 3 * u;
    g_pad.dp_x = m;
    g_pad.dp_y = g_dev_h - m - g_pad.dp_side;
    if (pad_pos("OPENMMO_TOUCH_DPAD", &ox, &oy)) {
        g_pad.dp_x = ox;
        g_pad.dp_y = oy;
    }

    /* The four faces as a diamond, bottom right, in the DS's arrangement:
     * A right, B bottom, X top, Y left, lifted clear of the message box
     * that runs along the bottom of the touch screen. */
    s = u * 6 / 5;
    d = u * 23 / 20;
    cx = g_dev_w - m - 3 * u / 2;
    cy = g_dev_h - m - 3 * u / 2 - 6 * u / 5;
    if (pad_pos("OPENMMO_TOUCH_FACES", &ox, &oy)) {
        /* The knob names the block's top left, so the centre follows from
         * it, a player reads a position off a screenshot, not a centre. */
        cx = ox + d + s / 2;
        cy = oy + d + s / 2;
    }
    i = 0;
    g_pad.b[i++] = (struct pad_btn){ cx - s / 2, cy - d - s / 2, s, s,
                                     PAD_SDL_X, "X" };
    g_pad.b[i++] = (struct pad_btn){ cx - s / 2, cy + d - s / 2, s, s,
                                     PAD_SDL_B, "B" };
    g_pad.b[i++] = (struct pad_btn){ cx - d - s / 2, cy - s / 2, s, s,
                                     PAD_SDL_Y, "Y" };
    g_pad.b[i++] = (struct pad_btn){ cx + d - s / 2, cy - s / 2, s, s,
                                     PAD_SDL_A, "A" };

    /* The shoulders at the top corners, where a hand holding the device in
     * landscape already rests its index fingers. */
    g_pad.b[i++] = (struct pad_btn){ m, m, 2 * u, 9 * u / 10, PAD_SDL_L,
                                     "L" };
    g_pad.b[i++] = (struct pad_btn){ g_dev_w - m - 2 * u, m, 2 * u,
                                     9 * u / 10, PAD_SDL_R, "R" };

    /*
     * Start and Select share the bottom centre, small on purpose: they are pressed between
     * things, never during them, and the middle of the glass is where the game is.
     */
    {
        int my_ = g_dev_h - m - 7 * u / 10 - 6 * u / 5;
        int mw = 8 * u / 5, mh = 7 * u / 10;

        mid = g_dev_w / 2;
        if (pad_pos("OPENMMO_TOUCH_MENU", &ox, &oy)) {
            mid = ox + 17 * u / 10;
            my_ = oy;
        }
        g_pad.b[i++] = (struct pad_btn){ mid - 17 * u / 10, my_, mw, mh,
                                         PAD_SDL_SELECT, "SELECT" };
        g_pad.b[i++] = (struct pad_btn){ mid + u / 10, my_, mw, mh,
                                         PAD_SDL_START, "START" };
    }

    /* Nothing may leave the glass, however a knob was written. */
    for (i = 0; i < PAD_BTN_N; i++) {
        struct pad_btn *b = &g_pad.b[i];

        if (b->x + b->w > g_dev_w) b->x = g_dev_w - b->w;
        if (b->y + b->h > g_dev_h) b->y = g_dev_h - b->h;
        if (b->x < 0) b->x = 0;
        if (b->y < 0) b->y = 0;
    }
    if (g_pad.dp_x + g_pad.dp_side > g_dev_w)
        g_pad.dp_x = g_dev_w - g_pad.dp_side;
    if (g_pad.dp_y + g_pad.dp_side > g_dev_h)
        g_pad.dp_y = g_dev_h - g_pad.dp_side;
    if (g_pad.dp_x < 0) g_pad.dp_x = 0;
    if (g_pad.dp_y < 0) g_pad.dp_y = 0;

    /* The toggle, at the very top centre, the one place no control and
     * neither screen wants, and the same place on every layout. */
    g_pad.hide_w = 8 * u / 5;
    g_pad.hide_h = 7 * u / 10;
    g_pad.hide_x = g_dev_w / 2 - g_pad.hide_w / 2;
    g_pad.hide_y = m / 4;
    if (g_pad.hide_x < 0) g_pad.hide_x = 0;
    if (g_pad.hide_y < 0) g_pad.hide_y = 0;

    g_pad.built_w = g_dev_w;
    g_pad.built_h = g_dev_h;
}

/* The labels, one cell per button in a single row. */
static void pad_build(void)
{
    const int cw = 128, ch = 64;
    int i;
    uint32_t *px;

    /*
     * Under the lock, because the rects this cuts are the ones the glue thread is hit-testing
     * fingers against on its own thread.
     */
    pthread_mutex_lock(&g_lock);
    pad_geometry();
    pthread_mutex_unlock(&g_lock);
    g_pad.cell_w = cw;
    g_pad.cell_h = ch;
    px = calloc((size_t)cw * PAD_CELLS * (size_t)ch, 4);
    if (px == NULL)
        return;
    for (i = 0; i < PAD_CELLS; i++) {
        const char *l = i < PAD_BTN_N ? g_pad.b[i].label
                      : (i == PAD_CELL_HIDE ? "HIDE" : "SHOW");
        /* The largest whole scale that still leaves a margin. Whole, because
         * the face is a bitmap and a fractional one is a blurred letter. */
        int scale = 8;

        while (scale > 1 && (openmmo_font_width(l, scale) > cw - 8
                             || 7 * scale > ch - 8)) {
            scale--;
        }
        g_pad.cell_lw[i] = openmmo_font_width(l, scale);
        g_pad.cell_lh[i] = 7 * scale;
        openmmo_font_draw_centred(px, cw * PAD_CELLS, ch,
                                  i * cw + cw / 2, (ch - 7 * scale) / 2,
                                  l, scale, 0xFFFFFFFFu);
    }
    if (g_pad.tex == 0)
        glGenTextures(1, &g_pad.tex);
    glBindTexture(GL_TEXTURE_2D, g_pad.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cw * PAD_CELLS, ch, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, px);
    free(px);
}

/* OPENMMO_TOUCH_ALPHA scales every part of the pad at once, so the knob is
 * one number rather than a palette. Read once, like the rest. */
static float pad_alpha(void)
{
    static float v = -1.0f;

    if (v < 0.0f)
        v = (float)pad_pct("OPENMMO_TOUCH_ALPHA", 100) / 100.0f;
    return v;
}

/* A filled rectangle over everything, ignoring the viewer's clip, which is
 * why this is not solid_dev, whose whole job is to honour it. */
static void pad_fill(int x, int y, int w, int h, uint32_t rgb, float a)
{
    glUseProgram(g_prog);
    glUniform2f(g_u_screen, (float)g_dev_w, (float)g_dev_h);
    glDisable(GL_SCISSOR_TEST);
    glUniform1i(g_u_mode, 3);
    glUniform4f(g_u_color, ((rgb >> 16) & 0xFF) / 255.0f,
                ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f,
                a * pad_alpha());
    want_blend(1);
    quad((float)x, (float)y, (float)w, (float)h, 0, 0, 1, 1);
}

/* One button's face: a translucent slab, brighter while it is held, with a
 * one-pixel-ish rim so it reads as a button against a bright map. */
static void pad_face(int x, int y, int w, int h, int held)
{
    int e = h / 16 + 1;

    /* A dark slab with a light rim, not a light slab: the world behind this
     * is a bright outdoor map most of the time, and a pale fill over it
     * washes out both. The rim is what says "button"; the fill only has to
     * hold the letter. Held is the one state that may shout. */
    pad_fill(x, y, w, h, 0x0E1116, held ? 0.62f : 0.30f);
    pad_fill(x, y, w, e, 0xE8ECF0, held ? 0.85f : 0.42f);
    pad_fill(x, y + h - e, w, e, 0xE8ECF0, held ? 0.85f : 0.42f);
    pad_fill(x, y, e, h, 0xE8ECF0, held ? 0.85f : 0.42f);
    pad_fill(x + w - e, y, e, h, 0xE8ECF0, held ? 0.85f : 0.42f);
}

/* One label, letterboxed into its button. */
static void pad_label(int i, int x, int y, int w, int h)
{
    float u0 = (float)i / (float)PAD_CELLS;
    float u1 = (float)(i + 1) / (float)PAD_CELLS;
    float fit_w = (float)w * 0.62f, fit_h = (float)h * 0.62f;
    float lw = (float)g_pad.cell_lw[i], lh = (float)g_pad.cell_lh[i];
    float sc, dw, dh;

    if (lw <= 0.0f || lh <= 0.0f)
        return;
    /* Fit the glyph, then carry the cell's whole width around it so the uv
     * rect stays the cell, the transparent margin costs nothing and keeps
     * the letter the shape it was rastered at. */
    sc = fit_w / lw;
    if (fit_h / lh < sc)
        sc = fit_h / lh;
    dw = (float)g_pad.cell_w * sc;
    dh = (float)g_pad.cell_h * sc;

    glUseProgram(g_prog);
    glUniform2f(g_u_screen, (float)g_dev_w, (float)g_dev_h);
    glDisable(GL_SCISSOR_TEST);
    glUniform1i(g_u_mode, 1);            /* the raster is 0xAARRGGBB */
    glUniform4f(g_u_color, 1, 1, 1, 0.5f * pad_alpha());
    want_blend(1);
    glBindTexture(GL_TEXTURE_2D, g_pad.tex);
    quad((float)x + ((float)w - dw) / 2.0f, (float)y + ((float)h - dh) / 2.0f,
         dw, dh, u0, 0, u1, 1);
}

static void pad_draw(void)
{
    int i, u, arm;

    if (!pad_on() || !gl_ready()) {
        if (g_pad.shown) {
            pthread_mutex_lock(&g_lock);
            pad_release_locked();
            pthread_mutex_unlock(&g_lock);
        }
        g_pad.shown = 0;
        return;
    }
    if (g_pad.built_w != g_dev_w || g_pad.built_h != g_dev_h || g_pad.tex == 0)
        pad_build();
    if (g_pad.tex == 0)
        return;
    g_pad.shown = 1;

    /* The toggle first and always, hidden or not: it is the way back. */
    {
        int cell = g_pad.hidden ? PAD_CELL_SHOW : PAD_CELL_HIDE;

        pad_face(g_pad.hide_x, g_pad.hide_y, g_pad.hide_w, g_pad.hide_h, 0);
        pad_label(cell, g_pad.hide_x, g_pad.hide_y, g_pad.hide_w,
                  g_pad.hide_h);
    }
    if (g_pad.hidden) {
        return;
    }

    /* The d-pad as a cross of two bars, so the shape says "directions" while
     * the hit test stays the whole square. */
    u = g_pad.dp_side / 3;
    arm = g_pad.dp_side;
    pad_fill(g_pad.dp_x + u, g_pad.dp_y, u, arm, 0x0E1116, 0.30f);
    pad_fill(g_pad.dp_x, g_pad.dp_y + u, arm, u, 0x0E1116, 0.30f);
    if (g_pad.held_dir & 1u)
        pad_fill(g_pad.dp_x + u, g_pad.dp_y, u, u, 0xE8ECF0, 0.42f);
    if (g_pad.held_dir & 2u)
        pad_fill(g_pad.dp_x + u, g_pad.dp_y + 2 * u, u, u, 0xE8ECF0, 0.42f);
    if (g_pad.held_dir & 4u)
        pad_fill(g_pad.dp_x, g_pad.dp_y + u, u, u, 0xE8ECF0, 0.42f);
    if (g_pad.held_dir & 8u)
        pad_fill(g_pad.dp_x + 2 * u, g_pad.dp_y + u, u, u, 0xE8ECF0, 0.42f);

    for (i = 0; i < PAD_BTN_N; i++) {
        const struct pad_btn *b = &g_pad.b[i];

        pad_face(b->x, b->y, b->w, b->h, (g_pad.held & (1u << i)) != 0);
        pad_label(i, b->x, b->y, b->w, b->h);
    }
}

static int pad_in(const struct pad_btn *b, float x, float y)
{
    return x >= (float)b->x && x < (float)(b->x + b->w)
        && y >= (float)b->y && y < (float)(b->y + b->h);
}

/* Re-read every button from every pointer and push the changes. Called with g_lock held. */
static uint32_t pad_take_locked(const struct mmo_sdlshim_pt *pts, int n)
{
    uint32_t taken = 0, held = 0, dir = 0;
    int i, j;

    if (!pad_on() || !g_pad.shown)
        return 0;

    /*
     * The toggle, on the EDGE. pad_take_locked runs for every motion event while a finger is
     * down, so a level test here would flip the pad tens of times per second for as long as it
     * was held.
     */
    {
        int on_hide = 0;

        for (i = 0; i < n; i++) {
            if (pts[i].x >= (float)g_pad.hide_x
                && pts[i].x < (float)(g_pad.hide_x + g_pad.hide_w)
                && pts[i].y >= (float)g_pad.hide_y
                && pts[i].y < (float)(g_pad.hide_y + g_pad.hide_h)) {
                on_hide = 1;
                taken |= 1u << i;
            }
        }
        if (on_hide && !g_pad.hide_finger) {
            g_pad.hidden = !g_pad.hidden;
            if (g_pad.hidden) {
                /* Everything it was holding goes with it, or a direction held
                 * as the pad went away would walk forever. */
                pad_release_locked();
            }
        }
        g_pad.hide_finger = on_hide;
    }
    /* PUT AWAY MEANS PUT AWAY: every pointer that is not on the toggle falls
     * straight through to the pen, which is the whole point of hiding it. */
    if (g_pad.hidden)
        return taken;

    for (i = 0; i < n; i++) {
        int on_pad = 0;

        if ((taken & (1u << i)) != 0)
            continue;              /* the toggle already claimed this one */

        for (j = 0; j < PAD_BTN_N; j++) {
            if (pad_in(&g_pad.b[j], pts[i].x, pts[i].y)) {
                held |= 1u << j;
                on_pad = 1;
                break;
            }
        }
        if (!on_pad && pts[i].x >= (float)g_pad.dp_x
            && pts[i].x < (float)(g_pad.dp_x + g_pad.dp_side)
            && pts[i].y >= (float)g_pad.dp_y
            && pts[i].y < (float)(g_pad.dp_y + g_pad.dp_side)) {
            float half = (float)g_pad.dp_side / 2.0f;
            float dx = pts[i].x - ((float)g_pad.dp_x + half);
            float dy = pts[i].y - ((float)g_pad.dp_y + half);
            float ax = dx < 0 ? -dx : dx;
            float ay = dy < 0 ? -dy : dy;
            float dead = half / 5.0f;

            on_pad = 1;
            if (ax > dead || ay > dead) {
                if (ax >= ay)
                    dir |= dx < 0 ? 4u : 8u;
                else
                    dir |= dy < 0 ? 1u : 2u;
            }
        }
        if (on_pad)
            taken |= 1u << i;
    }

    /* Push only the transitions: mmo_sdlshim_button takes g_lock itself, so
     * the levels are written straight here and the deferred-release counters
     * kept the same way it does. */
    {
        static const int kDir[4] = { PAD_SDL_UP, PAD_SDL_DOWN, PAD_SDL_LEFT,
                                     PAD_SDL_RIGHT };
        uint32_t was = g_pad.held, wasd = g_pad.held_dir;

        for (j = 0; j < PAD_BTN_N; j++)
            if (((held ^ was) & (1u << j)) != 0) {
                if (touch_log())
                    LOGI("pad: %s %s", g_pad.b[j].label,
                         (held & (1u << j)) != 0 ? "down" : "up");
                pad_set_locked(g_pad.b[j].sdl, (held & (1u << j)) != 0);
            }
        for (j = 0; j < 4; j++)
            if (((dir ^ wasd) & (1u << j)) != 0)
                pad_set_locked(kDir[j], (dir & (1u << j)) != 0);
        g_pad.held = held;
        g_pad.held_dir = dir;
    }
    return taken;
}

void SDL_RenderPresent(SDL_Renderer *ren)
{
    (void)ren;
    pthread_mutex_lock(&g_lock);
    /* The framework revoked the window (or wants it back): let go, then
     * wait for the next one and retake it, the viewer's loop stalls
     * rather than draws on a corpse. */
    if (g_release_asked || (g_have_surface && g_surf_gen != g_window_gen))
        surface_down_locked();
    while (!g_have_surface) {
        if (g_window != NULL && !g_release_asked) {
            if (surface_up_locked() == 0)
                break;
            /* A window that will not take a surface: wait for a new one. */
            g_window = NULL;
        }
        pthread_cond_wait(&g_cond, &g_lock);
    }
    pthread_mutex_unlock(&g_lock);

    pad_draw();
    osk_draw();
    if (!eglSwapBuffers(g_dpy, g_surf)) {
        EGLint e = eglGetError();

        LOGE("egl: swap failed 0x%x", e);
        pthread_mutex_lock(&g_lock);
        surface_down_locked();
        pthread_mutex_unlock(&g_lock);
    }

    /* The frame boundary: levels have now been sampled once, so deferred
     * releases may land and held levels age. */
    pthread_mutex_lock(&g_lock);
    {
        int i;

        for (i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) {
            if (g_buttons[i] && g_button_frames[i] < 255)
                g_button_frames[i]++;
            if (g_button_pending_up[i] && g_button_frames[i] >= 3) {
                g_buttons[i] = 0;
                g_button_pending_up[i] = 0;
            }
        }
        for (i = 0; i < SDL_NUM_SCANCODES; i++) {
            if (g_keystate[i] && g_key_frames[i] < 255)
                g_key_frames[i]++;
            if (g_key_pending_up[i] && g_key_frames[i] >= 3) {
                g_keystate[i] = 0;
                g_key_pending_up[i] = 0;
            }
        }
    }
    pthread_mutex_unlock(&g_lock);
}

/* ------------------------------------------------------------------ */
/* Text input                                                          */
/* ------------------------------------------------------------------ */

void SDL_StartTextInput(void)
{
    pthread_mutex_lock(&g_lock);
    g_textinput = 1;
    g_osk.user_hidden = 0;
    /* Cut the geometry NOW, not at the first present. */
    if (g_dev_w > 0 && g_dev_h > 0)
        osk_geometry();
    pthread_mutex_unlock(&g_lock);
}

void SDL_StopTextInput(void)
{
    pthread_mutex_lock(&g_lock);
    g_textinput = 0;
    g_osk.user_hidden = 0;
    pthread_mutex_unlock(&g_lock);
}

/* ------------------------------------------------------------------ */
/* The glue thread's feeders                                           */
/* ------------------------------------------------------------------ */

static int g_wheel_mode;         /* two fingers down: drags are a wheel */
static float g_wheel_y;          /* the gesture's accumulated device y */

/* The pen and the wheel, with g_lock held: everything mmo_sdlshim_touch used
 * to be below its own locking. Split out so the multi-pointer feeder can run
 * the pad first and then hand what is left down here unchanged. */
static void touch_locked(int action, float x, float y, int fingers, float y2)
{
    SDL_Event ev;

    if (fingers >= 2 && !g_wheel_mode) {
        /* The second finger turns the gesture into a wheel; the drag that
         * may already be in flight ends where it stands. */
        g_wheel_mode = 1;
        g_wheel_y = y2;
        if (g_mouse_mask & SDL_BUTTON_LMASK) {
            memset(&ev, 0, sizeof ev);
            ev.type = SDL_MOUSEBUTTONUP;
            ev.button.button = SDL_BUTTON_LEFT;
            ev.button.x = g_mouse_x;
            ev.button.y = g_mouse_y;
            push_event(&ev);
            g_mouse_mask = 0;
        }
    }
    if (g_wheel_mode) {
        if (action == 2 || fingers < 2) {
            g_wheel_mode = 0;
        } else {
            float dy = y2 - g_wheel_y;

            while (dy > 60.0f || dy < -60.0f) {
                memset(&ev, 0, sizeof ev);
                ev.type = SDL_MOUSEWHEEL;
                ev.wheel.y = dy < 0 ? 1 : -1;   /* fingers up: scroll down */
                push_event(&ev);
                dy += dy < 0 ? 60.0f : -60.0f;
                g_wheel_y = y2 - dy;
            }
        }
        /* No unlock here. This body was mmo_sdlshim_touch, which took the
         * lock itself; every caller it has now takes it and drops it, and the
         * one left behind was a second unlock of a mutex this thread no
         * longer held, on bionic, a lock somebody else was inside. */
        return;
    }

    {
        int lx = (int)x / g_scale, ly = (int)y / g_scale;

        memset(&ev, 0, sizeof ev);
        switch (action) {
        case 0:
            g_mouse_x = lx;
            g_mouse_y = ly;
            ev.type = SDL_MOUSEMOTION;
            ev.motion.x = lx;
            ev.motion.y = ly;
            push_event(&ev);
            memset(&ev, 0, sizeof ev);
            ev.type = SDL_MOUSEBUTTONDOWN;
            ev.button.button = SDL_BUTTON_LEFT;
            ev.button.clicks = 1;
            ev.button.x = lx;
            ev.button.y = ly;
            push_event(&ev);
            g_mouse_mask = SDL_BUTTON_LMASK;
            break;
        case 1:
            ev.type = SDL_MOUSEMOTION;
            ev.motion.state = g_mouse_mask;
            ev.motion.x = lx;
            ev.motion.y = ly;
            ev.motion.xrel = lx - g_mouse_x;
            ev.motion.yrel = ly - g_mouse_y;
            g_mouse_x = lx;
            g_mouse_y = ly;
            push_event(&ev);
            break;
        default:
            if (g_mouse_mask & SDL_BUTTON_LMASK) {
                ev.type = SDL_MOUSEBUTTONUP;
                ev.button.button = SDL_BUTTON_LEFT;
                ev.button.x = g_mouse_x;
                ev.button.y = g_mouse_y;
                push_event(&ev);
            }
            g_mouse_mask = 0;
            /* And the pointer leaves the glass with the finger. */
            g_mouse_x = -1;
            g_mouse_y = -1;
            memset(&ev, 0, sizeof ev);
            ev.type = SDL_MOUSEMOTION;
            ev.motion.x = -1;
            ev.motion.y = -1;
            push_event(&ev);
            break;
        }
    }
}

void mmo_sdlshim_touch(int action, float x, float y, int fingers, float y2)
{
    struct mmo_sdlshim_pt p;

    p.x = x;
    p.y = y;
    pthread_mutex_lock(&g_lock);
    /* One pointer, so the acting one is the only one there is. */
    if (osk_take_locked(action, &p, 1, action == 0 ? 0 : -1) == 0)
        touch_locked(action, x, y, fingers, y2);
    pthread_mutex_unlock(&g_lock);
}

/*
 * The order is keyboard, then PAD, then pen, which is the order they are drawn in, back to
 * front, read backwards. A finger belongs to the topmost thing under it and to nothing else.
 */
void mmo_sdlshim_pointers(int action, const struct mmo_sdlshim_pt *pts, int n,
                          int act_index)
{
    struct mmo_sdlshim_pt rest[MMO_SDLSHIM_PT_MAX];
    struct mmo_sdlshim_pt left[MMO_SDLSHIM_PT_MAX];
    uint32_t taken;
    int i, nrest = 0, rest_act = -1, nleft = 0, act_left = 1, lact;

    if (pts == NULL || n < 0)
        return;
    if (n > MMO_SDLSHIM_PT_MAX)
        n = MMO_SDLSHIM_PT_MAX;

    pthread_mutex_lock(&g_lock);
    taken = osk_take_locked(action, pts, n, act_index);
    for (i = 0; i < n; i++) {
        if ((taken & (1u << i)) != 0) {
            if (i == act_index)
                act_left = 0;
            continue;
        }
        if (i == act_index)
            rest_act = nrest;
        rest[nrest++] = pts[i];
    }

    taken = pad_take_locked(rest, nrest);
    for (i = 0; i < nrest; i++) {
        if ((taken & (1u << i)) != 0) {
            if (i == rest_act)
                act_left = 0;
            continue;
        }
        left[nleft++] = rest[i];
    }

    if (nleft == 0) {
        /* Nothing but the keyboard and the pad is on the glass. Release the
         * pen if it was down; a gesture that never reached it is not a tap on
         * the touch screen. */
        if (g_mouse_mask != 0)
            touch_locked(2, 0, 0, 0, 0);
        pthread_mutex_unlock(&g_lock);
        return;
    }

    lact = act_left ? action : 1;
    if (lact == 2)
        lact = 1;              /* somebody is still down; this is a move */
    touch_locked(lact, left[0].x, left[0].y, nleft,
                 nleft > 1 ? left[1].y : 0.0f);
    pthread_mutex_unlock(&g_lock);
}

void mmo_sdlshim_button(int button, int down)
{
    pthread_mutex_lock(&g_lock);
    pad_set_locked(button, down);
    pthread_mutex_unlock(&g_lock);
}

void mmo_sdlshim_axis(float lx, float ly)
{
    pthread_mutex_lock(&g_lock);
    g_axis_x = (Sint16)(lx * 32767.0f);
    g_axis_y = (Sint16)(ly * 32767.0f);
    pthread_mutex_unlock(&g_lock);
}

void mmo_sdlshim_scancode(int scancode, int down)
{
    SDL_Event ev;

    if (scancode <= 0 || scancode >= SDL_NUM_SCANCODES)
        return;
    pthread_mutex_lock(&g_lock);
    if (down) {
        g_keystate[scancode] = 1;
        g_key_frames[scancode] = 0;
        g_key_pending_up[scancode] = 0;
    } else if (g_key_frames[scancode] < 3 && g_keystate[scancode]) {
        g_key_pending_up[scancode] = 1;
    } else {
        g_keystate[scancode] = 0;
    }
    memset(&ev, 0, sizeof ev);
    ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    ev.key.keysym.scancode = (SDL_Scancode)scancode;
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
        ev.key.keysym.sym = 'a' + (scancode - SDL_SCANCODE_A);
    else if (scancode == SDL_SCANCODE_RETURN)
        ev.key.keysym.sym = SDLK_RETURN;
    else if (scancode == SDL_SCANCODE_BACKSPACE)
        ev.key.keysym.sym = SDLK_BACKSPACE;
    else if (scancode == SDL_SCANCODE_ESCAPE)
        ev.key.keysym.sym = SDLK_ESCAPE;
    else
        ev.key.keysym.sym = SDL_SCANCODE_TO_KEYCODE(scancode);
    push_event(&ev);
    pthread_mutex_unlock(&g_lock);
}

/* ------------------------------------------------------------------ */
/* Audio: the SDL callback contract on an AAudio stream                */
/* ------------------------------------------------------------------ */

static pthread_mutex_t g_audio_lock = PTHREAD_MUTEX_INITIALIZER;
static AAudioStream *g_stream;
static SDL_AudioSpec g_spec;
static int g_audio_paused = 1;          /* SDL opens devices paused */
static int g_audio_open;                /* a device is open: reopens allowed */
static int g_audio_reopens;             /* consecutive ones that delivered
                                           nothing; only AAudio's own
                                           callbacks touch it, and AAudio.h
                                           promises never two at once */
static int g_out_rate;
static double g_res_pos;
static int16_t g_res_scratch[4096 * 2];
/* The device's own underrun count, and the buffer we have grown to answer it
 * see audio_tune() below. Touched only from the data callback, which
 * AAudio.h promises is never two at once. */
static int32_t g_audio_xruns;
static int32_t g_audio_bufsize;
static int g_audio_grown;

/* How many reopens in a row may deliver no audio at all before the shim stops trying. */
/*
 * The TAP, for a session under measurement: every byte this stream hands the device, and when
 * each callback ran.
 */
enum { TAP_RING = 8u << 20, TAP_EVENTS = 8192 };
struct tap_event { int64_t ns; int32_t frames; int32_t xruns; int32_t paused;
                   uint32_t step, tail, frac; };
/* The viewer's reader state, for the event line: weak, so a shim linked
 * without the viewer still builds. */
extern volatile uint32_t openmmo_view_audio_step __attribute__((weak));
extern volatile uint32_t openmmo_view_audio_tail __attribute__((weak));
extern volatile uint32_t openmmo_view_audio_frac __attribute__((weak));
static uint8_t *g_tap_ring;
static struct tap_event *g_tap_events;
static uint32_t g_tap_w;                 /* bytes the callback appended */
static uint32_t g_tap_ev_w;              /* events the callback recorded */
static uint32_t g_tap_lost;
static FILE *g_tap_pcm, *g_tap_txt;
static pthread_t g_tap_thread;
static int g_tap_stop;
static int g_tap_on;

static void tap_note(AAudioStream *stream, const void *data, int32_t frames,
                     int paused)
{
    uint32_t w, n, off, first, ew;
    struct timespec ts;
    struct tap_event *ev;

    if (!g_tap_on || frames <= 0)
        return;
    n = (uint32_t)frames * 4u;
    w = g_tap_w;
    off = w % TAP_RING;
    first = TAP_RING - off < n ? TAP_RING - off : n;
    memcpy(g_tap_ring + off, data, first);
    if (first < n)
        memcpy(g_tap_ring, (const uint8_t *)data + first, n - first);
    __atomic_store_n(&g_tap_w, w + n, __ATOMIC_RELEASE);
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ew = g_tap_ev_w;
    ev = &g_tap_events[ew % TAP_EVENTS];
    ev->ns = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    ev->frames = frames;
    ev->xruns = AAudioStream_getXRunCount(stream);
    ev->paused = paused;
    ev->step = &openmmo_view_audio_step != NULL ? openmmo_view_audio_step : 0;
    ev->tail = &openmmo_view_audio_tail != NULL ? openmmo_view_audio_tail : 0;
    ev->frac = &openmmo_view_audio_frac != NULL ? openmmo_view_audio_frac : 0;
    __atomic_store_n(&g_tap_ev_w, ew + 1, __ATOMIC_RELEASE);
}

static void *tap_writer(void *arg)
{
    uint32_t r = 0, er = 0;

    (void)arg;
    for (;;) {
        uint32_t w = __atomic_load_n(&g_tap_w, __ATOMIC_ACQUIRE);
        uint32_t ew = __atomic_load_n(&g_tap_ev_w, __ATOMIC_ACQUIRE);
        int stop = __atomic_load_n(&g_tap_stop, __ATOMIC_ACQUIRE);

        if (w - r > TAP_RING) {
            g_tap_lost += (w - r) - TAP_RING;
            r = w - TAP_RING;
        }
        while (r != w) {
            uint32_t off = r % TAP_RING;
            uint32_t n = w - r;

            if (n > TAP_RING - off)
                n = TAP_RING - off;
            fwrite(g_tap_ring + off, 1, n, g_tap_pcm);
            r += n;
        }
        if (ew - er > TAP_EVENTS)
            er = ew - TAP_EVENTS;
        while (er != ew) {
            const struct tap_event *ev = &g_tap_events[er % TAP_EVENTS];

            fprintf(g_tap_txt, "%lld %d %d %d %u %u %u\n", (long long)ev->ns,
                    (int)ev->frames, (int)ev->xruns, (int)ev->paused,
                    (unsigned)ev->step, (unsigned)ev->tail, (unsigned)ev->frac);
            er++;
        }
        fflush(g_tap_pcm);
        fflush(g_tap_txt);
        if (stop)
            break;
        usleep(100000);
    }
    return NULL;
}

static void tap_open(int rate)
{
    const char *prefix = getenv("OPENMMO_AUDIO_TAP");
    char path[1024];

    if (prefix == NULL || prefix[0] == '\0' || g_tap_on)
        return;
    g_tap_ring = malloc(TAP_RING);
    g_tap_events = calloc(TAP_EVENTS, sizeof *g_tap_events);
    snprintf(path, sizeof path, "%s.pcm", prefix);
    g_tap_pcm = fopen(path, "wb");
    snprintf(path, sizeof path, "%s.txt", prefix);
    g_tap_txt = fopen(path, "w");
    if (g_tap_ring == NULL || g_tap_events == NULL || g_tap_pcm == NULL
        || g_tap_txt == NULL) {
        LOGE("audio: tap %s could not be opened", prefix);
        free(g_tap_ring); free(g_tap_events);
        if (g_tap_pcm) fclose(g_tap_pcm);
        if (g_tap_txt) fclose(g_tap_txt);
        g_tap_ring = NULL; g_tap_events = NULL;
        g_tap_pcm = NULL; g_tap_txt = NULL;
        return;
    }
    fprintf(g_tap_txt, "# rate %d ns frames xruns paused step tail frac\n", rate);
    g_tap_w = 0; g_tap_ev_w = 0; g_tap_lost = 0; g_tap_stop = 0;
    if (pthread_create(&g_tap_thread, NULL, tap_writer, NULL) != 0) {
        LOGE("audio: tap has no writer thread");
        fclose(g_tap_pcm); fclose(g_tap_txt);
        free(g_tap_ring); free(g_tap_events);
        g_tap_ring = NULL; g_tap_events = NULL;
        g_tap_pcm = NULL; g_tap_txt = NULL;
        return;
    }
    __atomic_store_n(&g_tap_on, 1, __ATOMIC_RELEASE);
    LOGI("audio: tap on, %s.pcm at %d Hz", prefix, rate);
}

static void tap_close(void)
{
    if (!g_tap_on)
        return;
    __atomic_store_n(&g_tap_on, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&g_tap_stop, 1, __ATOMIC_RELEASE);
    pthread_join(g_tap_thread, NULL);
    fclose(g_tap_pcm); fclose(g_tap_txt);
    free(g_tap_ring); free(g_tap_events);
    g_tap_ring = NULL; g_tap_events = NULL;
    g_tap_pcm = NULL; g_tap_txt = NULL;
    LOGI("audio: tap off, %u bytes appended, %u lost to the writer",
         (unsigned)g_tap_w, (unsigned)g_tap_lost);
}

enum { AUDIO_REOPEN_MAX = 8 };

/*
 * DYNAMIC BUFFER SIZING, which a LOW_LATENCY stream on a busy device needs and this shim did
 * not have.
 */
static void audio_tune(AAudioStream *stream)
{
    int32_t xruns = AAudioStream_getXRunCount(stream);
    int32_t burst, cap, want;

    if (xruns <= g_audio_xruns)
        return;
    g_audio_xruns = xruns;
    burst = AAudioStream_getFramesPerBurst(stream);
    cap = AAudioStream_getBufferCapacityInFrames(stream);
    if (burst <= 0 || cap <= 0)
        return;
    want = AAudioStream_getBufferSizeInFrames(stream) + burst;
    if (want > cap)
        want = cap;
    if (want == g_audio_bufsize)
        return;
    g_audio_bufsize = AAudioStream_setBufferSizeInFrames(stream, want);
    /* Said once a growth rather than once an underrun: a device that clicks
     * steadily would otherwise fill the log from the audio thread. */
    if (++g_audio_grown <= 16)
        LOGI("audio: %d underrun(s); buffer now %d frames of %d (burst %d)",
             (int)xruns, (int)g_audio_bufsize, (int)cap, (int)burst);
}

static aaudio_data_callback_result_t audio_cb(AAudioStream *stream,
                                              void *user, void *data,
                                              int32_t frames)
{
    int16_t *out = data;

    (void)stream; (void)user;
    if (frames <= 0)
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    audio_tune(stream);
    /* This stream is delivering, so whatever it took to get here worked and
     * the next disconnection starts its own count. */
    g_audio_reopens = 0;
    pthread_mutex_lock(&g_audio_lock);
    if (g_audio_paused || g_background || g_spec.callback == NULL) {
        memset(data, 0, (size_t)frames * 4);
        pthread_mutex_unlock(&g_audio_lock);
        tap_note(stream, data, frames, 1);
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
    if (g_out_rate == g_spec.freq) {
        g_spec.callback(g_spec.userdata, (Uint8 *)out, (int)frames * 4);
    } else {
        /* The device refused the game's rate: pull at the game's, hand out
         * at the device's, linearly, the frontend's own arrangement. */
        const double step = (double)g_spec.freq / (double)g_out_rate;
        double pos = g_res_pos;
        unsigned need, i;

        need = (unsigned)(pos + step * (double)frames) + 2;
        if (need > sizeof g_res_scratch / (2 * sizeof g_res_scratch[0]))
            need = (unsigned)(sizeof g_res_scratch
                              / (2 * sizeof g_res_scratch[0]));
        g_spec.callback(g_spec.userdata, (Uint8 *)g_res_scratch,
                        (int)need * 4);
        for (i = 0; i < (unsigned)frames; i++) {
            unsigned k = (unsigned)pos;
            double f = pos - (double)k;
            const int16_t *a, *b;

            if (k + 1 >= need) {
                k = need >= 2 ? need - 2 : 0;
                f = 0.0;
            }
            a = &g_res_scratch[(size_t)k * 2];
            b = &g_res_scratch[(size_t)(k + 1) * 2];
            out[i * 2 + 0] = (int16_t)(a[0] + (b[0] - a[0]) * f);
            out[i * 2 + 1] = (int16_t)(a[1] + (b[1] - a[1]) * f);
            pos += step;
        }
        g_res_pos = pos - (double)(unsigned)pos;
    }
    pthread_mutex_unlock(&g_audio_lock);
    tap_note(stream, out, frames, 0);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

/* A stream that dies is opened again, on a thread of its own. */
static void audio_err_cb(AAudioStream *stream, void *user,
                         aaudio_result_t err);

/* Build, open and start a stream for the rate the game asked for, and say
 * what the device gave back. Holds no lock: opening blocks, and the data
 * callback wants that lock. */
static AAudioStream *audio_stream_open(int freq, int *rate)
{
    AAudioStreamBuilder *b = NULL;
    AAudioStream *s = NULL;

    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK)
        return NULL;
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setSampleRate(b, freq);
    AAudioStreamBuilder_setPerformanceMode(b,
                                           AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(b, audio_cb, NULL);
    AAudioStreamBuilder_setErrorCallback(b, audio_err_cb, NULL);
    if (AAudioStreamBuilder_openStream(b, &s) != AAUDIO_OK) {
        AAudioStreamBuilder_delete(b);
        return NULL;
    }
    AAudioStreamBuilder_delete(b);
    *rate = AAudioStream_getSampleRate(s);
    /*
     * Four bursts to begin with, one is what a device hands back by default and leaves no
     * room at all for a late callback, and two was measured on the RG556 (2026-09-10, the tap)
     * with callbacks arriving up to 10.6 ms after a 10 ms burst: the whole buffer, nothing
     * spare.
     */
    {
        int32_t burst = AAudioStream_getFramesPerBurst(s);

        g_audio_xruns = 0;
        g_audio_grown = 0;
        g_audio_bufsize = burst > 0
            ? AAudioStream_setBufferSizeInFrames(s, burst * 4)
            : AAudioStream_getBufferSizeInFrames(s);
        LOGI("audio: stream open, buffer %d frames of %d (burst %d)",
             (int)g_audio_bufsize,
             (int)AAudioStream_getBufferCapacityInFrames(s), (int)burst);
    }
    if (AAudioStream_requestStart(s) != AAUDIO_OK) {
        AAudioStream_close(s);
        return NULL;
    }
    return s;
}

/* The recovery. `arg` is the stream the error arrived on, which is this
 * thread's to stop and close and nobody else's. */
static void *audio_restart(void *arg)
{
    AAudioStream *dead = arg, *fresh;
    struct timespec ts;
    int freq, rate = 0;

    pthread_mutex_lock(&g_audio_lock);
    if (g_stream != dead) {        /* closed, or already replaced */
        pthread_mutex_unlock(&g_audio_lock);
        return NULL;
    }
    g_stream = NULL;
    freq = g_spec.freq;
    pthread_mutex_unlock(&g_audio_lock);

    AAudioStream_requestStop(dead);
    AAudioStream_close(dead);

    /* A moment for the route to settle. A device in the middle of being
     * swapped refuses the open, and the wait is also what stops a stream
     * that dies the instant it starts from spinning threads here. */
    ts.tv_sec = 0;
    ts.tv_nsec = 200 * 1000 * 1000;
    nanosleep(&ts, NULL);

    fresh = audio_stream_open(freq, &rate);
    pthread_mutex_lock(&g_audio_lock);
    if (fresh == NULL) {
        pthread_mutex_unlock(&g_audio_lock);
        LOGE("audio: the stream disconnected and would not reopen");
        return NULL;
    }
    if (!g_audio_open || g_stream != NULL) {
        /* The game closed the device while this was opening, or something
         * else got there first: this stream has no owner. */
        pthread_mutex_unlock(&g_audio_lock);
        AAudioStream_requestStop(fresh);
        AAudioStream_close(fresh);
        return NULL;
    }
    g_stream = fresh;
    g_out_rate = rate;
    g_res_pos = 0.0;
    pthread_mutex_unlock(&g_audio_lock);
    LOGI("audio: reopened, device at %d Hz (%d wanted)", rate, freq);
    return NULL;
}

static void audio_err_cb(AAudioStream *stream, void *user,
                         aaudio_result_t err)
{
    pthread_t t;

    (void)user;
    LOGI("audio: stream error %s", AAudio_convertResultToText(err));
    if (++g_audio_reopens > AUDIO_REOPEN_MAX) {
        LOGE("audio: %d reopens delivered nothing; leaving it silent",
             AUDIO_REOPEN_MAX);
        return;
    }
    if (pthread_create(&t, NULL, audio_restart, stream) != 0)
        LOGE("audio: no thread to reopen the stream on");
    else
        pthread_detach(t);
}

SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture,
                                      const SDL_AudioSpec *want,
                                      SDL_AudioSpec *obtained,
                                      int allowed_changes)
{
    AAudioStream *s;
    int rate = 0;

    (void)device; (void)iscapture; (void)allowed_changes;
    if (want == NULL || g_stream != NULL) {
        set_err("audio: already open");
        return 0;
    }
    g_spec = *want;
    g_res_pos = 0.0;
    g_audio_paused = 1;
    g_audio_reopens = 0;
    s = audio_stream_open(want->freq, &rate);
    if (s == NULL) {
        set_err("audio: cannot open a stream");
        return 0;
    }
    pthread_mutex_lock(&g_audio_lock);
    g_stream = s;
    g_out_rate = rate;
    g_audio_open = 1;
    pthread_mutex_unlock(&g_audio_lock);
    tap_open(rate);
    if (obtained != NULL) {
        *obtained = *want;
        /* The resampler above keeps the callback at want->freq whatever the
         * device runs, which is SDL's own no-changes-allowed promise. */
    }
    LOGI("audio: %d Hz wanted, device at %d Hz%s", want->freq, rate,
         rate == want->freq ? "" : " (resampled)");
    return 1;
}

void SDL_CloseAudioDevice(SDL_AudioDeviceID dev)
{
    AAudioStream *s;

    (void)dev;
    /* Cleared under the lock and closed outside it: closing waits for the
     * data callback to return, and that callback holds this lock. */
    pthread_mutex_lock(&g_audio_lock);
    s = g_stream;
    g_stream = NULL;
    g_audio_open = 0;
    pthread_mutex_unlock(&g_audio_lock);
    if (s != NULL) {
        AAudioStream_requestStop(s);
        AAudioStream_close(s);
    }
    tap_close();
}

void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on)
{
    (void)dev;
    pthread_mutex_lock(&g_audio_lock);
    g_audio_paused = pause_on;
    pthread_mutex_unlock(&g_audio_lock);
}

void SDL_LockAudioDevice(SDL_AudioDeviceID dev)
{
    (void)dev;
    pthread_mutex_lock(&g_audio_lock);
}

void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev)
{
    (void)dev;
    pthread_mutex_unlock(&g_audio_lock);
}

int SDL_GetNumAudioDevices(int iscapture)
{
    return iscapture ? 0 : 1;
}

const char *SDL_GetAudioDeviceName(int index, int iscapture)
{
    (void)index; (void)iscapture;
    return "AAudio";
}

void mmo_sdlshim_background(int paused)
{
    pthread_mutex_lock(&g_audio_lock);
    g_background = paused;
    pthread_mutex_unlock(&g_audio_lock);
}

/* A QUIT, from outside the event feeders: the app's exit hook uses it to ask
 * the viewer to leave before the process is torn down under its GL calls. */
void mmo_sdlshim_push_quit(void)
{
    SDL_Event ev;

    memset(&ev, 0, sizeof ev);
    ev.type = SDL_QUIT;
    pthread_mutex_lock(&g_lock);
    push_event(&ev);
    pthread_cond_broadcast(&g_cond);
    pthread_mutex_unlock(&g_lock);
}
