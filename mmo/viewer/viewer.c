/* Openmmo-view, the window this project owns. */

#include <SDL.h>

#include <errno.h>
#include <stdarg.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "platform.h"
#include "view_channel.h"
#include "view_font.h"
#include "view_geom.h"
#include "view_hud.h"
#include "view_input.h"
#include "view_status.h"
#include "view_ui.h"
#include "view_ui_bar.h"
#include "view_ui_chat.h"
#include "view_ui_draw.h"
#include "view_ui_game.h"
#include "view_ui_party.h"
#include "view_ui_wins.h"

#define VIEW_NAME    "OpenMMO"
#define VIEW_DEFAULT_CHANNEL "openmmo"

/* ------------------------------------------------------------------ */
/* Options                                                             */
/* ------------------------------------------------------------------ */

struct view_opts {
    const char *name;         /* the shm channel the game's PC_VIEW= names */
    int scale;                /* 0 = auto (display table); else 1..8 x composed */
    int win_w, win_h;         /* --size WxH, 0 = not set */
    int layout;
    int rs;                   /* render scale, 1..4 */
    int filter;
    int integer;              /* scale by whole source pixels only */
    int aspect;
    int fullscreen;
    int no_audio;
    int offline;              /* no server behind the window: no chat, and no
                               * bar button that would have to ask one */
    const char *audio_device;
    const char *shot;         /* write one composed frame here and exit */
    const char *shots;        /* where F12 writes; NULL means here */
    const char *bind;         /* --bind PAD=KEY[,...], applied once SDL is up */
    int wait_ms;              /* how long to wait for the game before giving up */
    int hud_dump;             /* print each hud snapshot to stderr */
    int ui_screen;            /* VIEW_UI_CANVAS_*: where the UI layer starts */
    const char *theme;        /* --theme DIR: the official client's art for the UI layer */
};

/* ------------------------------------------------------------------ */
/* The channel                                                         */
/* ------------------------------------------------------------------ */

enum attach_result {
    ATTACH_OK,
    ATTACH_NO_CHANNEL,    /* nothing is published under that name yet */
    ATTACH_NO_MAGIC,      /* a page is, but it is not stamped as one of ours, 
                           * not filled in yet, or not ours at all */
    ATTACH_BAD_VERSION    /* it is, and it is not the one we can read */
};

struct viewer {
    struct openmmo_view_shm *shm;
    mmo_shm page;                /* kept mapped: the name going away is the
                                  * session ending (platform.h) */
    uint32_t their_version;      /* what a rejected page said it was */
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex[2];         /* [0] top screen, [1] touch screen */
    SDL_Texture *msg;            /* the message screen, when there is one */
    struct view_status status;   /* where the game says its session stands */
    struct view_hud hud;         /* the game's hud page: chat, plates, state */
    SDL_Texture *plate_tex;      /* scratch for one nameplate at a time */
    int plate_tw, plate_th;
    struct view_ui_gpu ui_gpu;   /* the one glyph cache every host UI draws with */
    struct view_ui_layer ui;     /* the whole-window UI layer (view_ui.h) */
    struct view_ui_chat ui_chat; /* its first element: the chat box */
    /* The official client's in-game UI: the party strip on the right edge, the frames a
     * HUD entry raises, and the bar itself, which is registered last so its
     * popup is over everything. The design notes. */
    struct view_ui_party ui_party;
    struct view_ui_wins  ui_wins;
    struct view_ui_bar   ui_bar;
    uint64_t status_changed_ms;  /* when it last said something different */
    int vsync;
    int src_w, src_h;            /* the last frame's published size */
    int sec;                     /* the smart layout's touch screen, per cent */
    int sized;                   /* the window has been sized to a real frame */
    uint64_t torn, presented;
    uint32_t *scaled[2];
    uint32_t *mid;               /* scale2x rs==4 scratch */
};

static void view_sleep_ms(int ms)
{
    mmo_plat_sleep_us((unsigned)ms * 1000u);
}

/*
 * One attempt, no waiting: the caller is a frame loop with a window already on
 * screen, so waiting belongs to it and not to this.
 */
static enum attach_result viewer_attach(struct viewer *vw, const char *name)
{
    struct openmmo_view_shm *shm;

    if (vw->shm != NULL) return ATTACH_OK;
    if (mmo_shm_attach(&vw->page, name, sizeof *shm, 1) != 0)
        return ATTACH_NO_CHANNEL;
    shm = (struct openmmo_view_shm *)vw->page.addr;

    /*
     * The page exists before it is filled in, the game creates it and then writes the magic, 
     * so "no magic yet" is a wait and not a refusal.
     */
    /* Every failure below lets the mapping go before it returns. This is a
     * frame loop: a refusal that kept its mapping would take a fresh one every
     * frame until the game published, and on Windows a section handle each
     * time with it. */
    if (shm->magic != OPENMMO_VIEW_MAGIC) {
        mmo_shm_close(&vw->page);
        vw->shm = NULL;
        return ATTACH_NO_MAGIC;
    }
    if (shm->version != OPENMMO_VIEW_VERSION) {
        vw->their_version = shm->version;
        mmo_shm_close(&vw->page);
        vw->shm = NULL;
        return ATTACH_BAD_VERSION;
    }
    /* Version 7 was not bumped when the pixel arrays grew, so a page with our
     * magic can still have keys and audio sitting past the picture we mapped.
     * The object's own size is that layout. */
    if (!mmo_shm_size_matches(&vw->page, sizeof *shm)) {
        fprintf(stderr,
                "openmmo-view: page is %llu bytes, this window is %zu; "
                "keys and sound sit past the picture. Rebuild the game "
                "and this window together.\n",
                (unsigned long long)mmo_shm_object_size(&vw->page),
                sizeof *shm);
        vw->their_version = shm->version;
        mmo_shm_close(&vw->page);
        vw->shm = NULL;
        return ATTACH_BAD_VERSION;
    }

    vw->shm = shm;
    /* Who IS watching. The game has no window; this process holds it, so the
     * game ends its run when this one goes away. Published at attach rather
     * than at the first frame, so a viewer that dies while still opening its
     * window is noticed too. */
    shm->in_viewer_pid = (uint32_t)mmo_plat_pid();
    shm->in_quit = 0;
    return ATTACH_OK;
}

/* ------------------------------------------------------------------ */
/* Sound                                                               */
/* ------------------------------------------------------------------ */

/*
 * The sound path's own log, because this window's stderr goes nowhere a player can read on
 * Windows.
 */
static FILE *audio_log;

static void view_audio_log_open(void)
{
    const char *dir = getenv("OPENMMO_VIEW_LOG_DIR");
    char resolved[1024];
    char path[1200];

    if (dir == NULL || dir[0] == '\0') {
        if (mmo_plat_log_dir(resolved, sizeof resolved) != 0) return;
        dir = resolved;
    }
    snprintf(path, sizeof path, "%s%sopenmmo-view.log", dir, mmo_plat_sep());
    audio_log = fopen(path, "w");
    /*
     * Line buffering is a POSIX promise and mingw's runtime does not keep it: _IOLBF there
     * means fully buffered, so the whole ledger sat in a 4 KB buffer and died with the process
     * every time the front door stopped the window.
     */
    if (audio_log != NULL) setvbuf(audio_log, NULL, _IONBF, 0);
}

static void view_audio_logf(const char *fmt, ...)
{
    va_list ap;

    if (audio_log == NULL) return;
    fprintf(audio_log, "%8llu ", (unsigned long long)SDL_GetTicks64());
    va_start(ap, fmt);
    vfprintf(audio_log, fmt, ap);
    va_end(ap);
    fputc('\n', audio_log);
    fflush(audio_log);
}

/*
 * The game mixes into the shared ring; this end hands it to SDL. The callback must always fill
 * its buffer: a run momentarily behind gets silence, not a fragment of the last second.
 */
struct view_audio {
    const struct openmmo_view_shm *v;
    uint32_t tail;             /* the ring frame the read head sits on */
    uint32_t frac;             /* how far past it, in 1/65536ths of a frame */
    uint32_t step;             /* ring frames per output frame, 16.16 */
    /*
     * ONE ring FRAME per output FRAME IS an assumption, and when it is wrong every sound in
     * the game is at the wrong pitch.
     */
    uint32_t base;             /* ring frames per output frame, 16.16 */
    uint32_t dev_rate;         /* what the device actually opened at */
    const int16_t *sinc;       /* (VIEW_SINC_PHASES + 1) x VIEW_SINC_TAPS, Q14 */
    long integral;             /* the governor's learned rate difference,
                                * 16.16 either side of base */
    long backlog_avg;          /* the backlog the governor steers by: a
                                * four-second average, 0 until started */
    uint64_t dropped;
    uint64_t starved;
    uint64_t trimmed;
};

/* One ring frame per output frame in the step's fixed point, and how far
 * either side of it the governor below may go. Half a per cent is eight cents
 * of pitch at the very ends of the range, and the mismatches it has to absorb
 * are a third of that: wider would buy nothing and could be heard. */
#define VIEW_STEP_ONE   65536u
#define VIEW_STEP_SWING   328u          /* 0.5 % */
/*
 * How far the step may move per pass of the governor, which runs once a second, and it has
 * to actually run once a second for this to mean what it says.
 */
#define VIEW_STEP_SLEW     32u
#define VIEW_GOVERNOR_MS 1000u

static void view_ring_frame(const struct openmmo_view_shm *v, uint32_t i,
                            int *l, int *r)
{
    uint32_t s = v->audio[i % OPENMMO_VIEW_AUDIO_FRAMES];

    *l = (int16_t)(uint16_t)(s & 0xFFFFu);
    *r = (int16_t)(uint16_t)(s >> 16);
}

/*
 * THE ring IS read at A RATE, NOT A FRAME at a time, and that is half of the governor further
 * down.
 */
/* The step the callback last read the ring at and the ring frame it will
 * read next, for a tap in the same process (the handheld's shim records
 * them beside every burst) so a session's delivered stream can be
 * reproduced from the mixer's dump and compared, sample for sample. */
volatile uint32_t openmmo_view_audio_step;
volatile uint32_t openmmo_view_audio_tail;
volatile uint32_t openmmo_view_audio_frac;

#define VIEW_SINC_TAPS   16
#define VIEW_SINC_PHASES 128
#define VIEW_SINC_BEFORE 7              /* frames behind the read head */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Bessel I0 by its series, enough for a Kaiser window. */
static double view_bessel_i0(double x)
{
    double sum = 1.0, term = 1.0, hx = x / 2.0;
    int k;

    for (k = 1; k < 40; k++) {
        term *= (hx / k) * (hx / k);
        sum += term;
        if (term < sum * 1e-12) break;
    }
    return sum;
}

/*
 * The table: sinc at the phase, under a Kaiser window (beta 6), each phase scaled to sum to
 * one so a constant passes as itself. Cutoff at the Nyquist rate exactly, which is what makes
 * phase zero the identity.
 */
static int16_t *view_audio_sinc_build(void)
{
    /* One row past the last phase: phase 128 is phase 0 one frame on, so
     * the lean between phases 127 and 128 has a row to lean towards. */
    int16_t *tab = malloc(sizeof(int16_t) * (VIEW_SINC_PHASES + 1)
                          * VIEW_SINC_TAPS);
    const double beta = 6.0, half = VIEW_SINC_TAPS / 2.0;
    double i0b;
    int p, k;

    if (tab == NULL) return NULL;
    i0b = view_bessel_i0(beta);
    for (p = 0; p <= VIEW_SINC_PHASES; p++) {
        double h[VIEW_SINC_TAPS], sum = 0.0;
        double a = (double)p / VIEW_SINC_PHASES;
        long acc = 0;

        for (k = 0; k < VIEW_SINC_TAPS; k++) {
            double x = (double)(k - VIEW_SINC_BEFORE) - a;
            double r = x / half, w, sn;

            w = r * r < 1.0 ? view_bessel_i0(beta * sqrt(1.0 - r * r)) / i0b
                            : 0.0;
            sn = x == 0.0 ? 1.0 : sin(M_PI * x) / (M_PI * x);
            h[k] = sn * w;
            sum += h[k];
        }
        for (k = 0; k < VIEW_SINC_TAPS; k++) {
            long q = (long)floor(h[k] / sum * 16384.0 + 0.5);

            tab[p * VIEW_SINC_TAPS + k] = (int16_t)q;
            acc += q;
        }
        /* Rounding left the phase a unit or two off unity: the centre tap
         * takes the difference, so every phase sums to exactly 16384 and a
         * constant input is a constant output at every phase. */
        tab[p * VIEW_SINC_TAPS + VIEW_SINC_BEFORE] += (int16_t)(16384 - acc);
    }
    /* The one property everything above rests on, checked rather than
     * trusted: phase zero is one and fifteen zeros. */
    for (k = 0; k < VIEW_SINC_TAPS; k++) {
        int16_t want = k == VIEW_SINC_BEFORE ? 16384 : 0;

        if (tab[k] != want) {
            view_audio_logf("sinc: phase 0 tap %d is %d, not %d, the table"
                            " is wrong and the plain lean is used", k,
                            (int)tab[k], (int)want);
            free(tab);
            return NULL;
        }
    }
    return tab;
}

static void view_audio_cb(void *arg, Uint8 *stream, int len)
{
    struct view_audio *a = (struct view_audio *)arg;
    unsigned want = (unsigned)len / (2 * sizeof(int16_t));
    int16_t *out = (int16_t *)stream;
    const struct openmmo_view_shm *v = a->v;
    uint32_t head, frac, step;
    unsigned n = 0;

    if (v == NULL || v->magic != OPENMMO_VIEW_MAGIC
        || v->version != OPENMMO_VIEW_VERSION) {
        memset(stream, 0, (size_t)len);
        return;
    }
    head = __atomic_load_n(&v->audio_head, __ATOMIC_ACQUIRE);
    /* A reader a whole ring behind is being handed frames the writer has
     * already written over. Unsigned throughout, so this stays right across
     * the 2^32 wrap. */
    if (head - a->tail > OPENMMO_VIEW_AUDIO_FRAMES) {
        uint32_t lost = (head - a->tail) - OPENMMO_VIEW_AUDIO_FRAMES;

        a->dropped += lost;
        a->tail += lost;
        a->frac = 0;
    }
    frac = a->frac;
    step = a->step != 0 ? a->step
                        : (a->base != 0 ? a->base : VIEW_STEP_ONE);
    openmmo_view_audio_step = step;
    openmmo_view_audio_tail = a->tail;
    openmmo_view_audio_frac = frac;
    /* Sixteen ring frames make one of these, seven behind the head and
     * eight from it on, so the loop stops eight short of the writer rather
     * than leaning on a frame that is not there yet. The seven behind were
     * played already and the writer is a cushion away from reaching them. */
    while (n < want && head - a->tail >= (uint32_t)(VIEW_SINC_TAPS - VIEW_SINC_BEFORE) + 1u) {
        if (a->sinc != NULL) {
            const int16_t *h0 = a->sinc
                + (frac >> (16 - 7)) * VIEW_SINC_TAPS;   /* 128 phases */
            const int16_t *h1 = h0 + VIEW_SINC_TAPS;     /* the next one */
            int32_t rem = (int32_t)(frac & 0x1FFu);      /* the last 9 bits */
            int32_t sl = 0, sr = 0;
            int l, r, k;

            for (k = 0; k < VIEW_SINC_TAPS; k++) {
                int32_t h = h0[k] + ((((int32_t)h1[k] - h0[k]) * rem) >> 9);

                view_ring_frame(v, a->tail + (uint32_t)k - VIEW_SINC_BEFORE,
                                &l, &r);
                sl += h * l;
                sr += h * r;
            }
            sl = (sl + 8192) >> 14;
            sr = (sr + 8192) >> 14;
            out[n * 2 + 0] = (int16_t)(sl > 32767 ? 32767 : sl < -32768 ? -32768 : sl);
            out[n * 2 + 1] = (int16_t)(sr > 32767 ? 32767 : sr < -32768 ? -32768 : sr);
        } else {
            /* No table (the malloc failed): the plain lean, as before. */
            int l0, r0, l1, r1;
            int w = (int)(frac >> 8);

            view_ring_frame(v, a->tail, &l0, &r0);
            view_ring_frame(v, a->tail + 1u, &l1, &r1);
            out[n * 2 + 0] = (int16_t)(l0 + (((l1 - l0) * w) >> 8));
            out[n * 2 + 1] = (int16_t)(r0 + (((r1 - r0) * w) >> 8));
        }
        n++;
        frac += step;
        a->tail += frac >> 16;
        frac &= 0xFFFFu;
    }
    a->frac = frac;
    if (n < want) {
        a->starved += want - n;
        memset(out + n * 2, 0, (size_t)(want - n) * 2 * sizeof(int16_t));
    }
}

/*
 * The ring's rate is whatever the page says: the console's 32,728 Hz, or the finer rate the
 * game's mixer was asked for (PC_AUDIO_RATE, 48,000 through the launcher).
 */
static uint32_t view_audio_rate(const struct openmmo_view_shm *v)
{
    return v != NULL && v->audio_rate ? v->audio_rate : OPENMMO_VIEW_AUDIO_RATE;
}

static uint32_t view_audio_scaled(const struct openmmo_view_shm *v,
                                  uint32_t frames_at_console)
{
    return (uint32_t)((uint64_t)frames_at_console * view_audio_rate(v)
                      / OPENMMO_VIEW_AUDIO_RATE);
}

/*
 * The ratio, from whatever the page says now over whatever the device took. Answers 1.0 while
 * either is unknown, which is the old assumption and the only safe guess before there is
 * anything better.
 */
static void view_audio_rebase(struct view_audio *a)
{
    uint32_t page = (a->v != NULL) ? a->v->audio_rate : 0;
    uint32_t was = a->base;

    if (page == 0 || a->dev_rate == 0) {
        a->base = VIEW_STEP_ONE;
    } else {
        a->base = (uint32_t)(((uint64_t)page << 16) / a->dev_rate);
    }
    if (a->base != was && was != 0) {
        view_audio_logf("rate: the game mixes at %u Hz into a %u Hz device, so "
                        "%u ring frames per output frame (was %u)",
                        (unsigned)page, (unsigned)a->dev_rate,
                        (unsigned)a->base, (unsigned)was);
    }
}

/*
 * Opened at the game's own rate with no SDL resampling allowed; a device that cannot take it,
 * 32,728 Hz is the console's, and few devices take that as it is, fails and we retry
 * letting SDL pick, the one case where it resamples.
 */
static SDL_AudioDeviceID view_audio_open(struct view_audio *a,
                                         const char *device)
{
    SDL_AudioSpec want, have;
    SDL_AudioDeviceID dev;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "openmmo-view: no audio here (%s); the picture still "
                        "works.\n", SDL_GetError());
        return 0;
    }

    SDL_memset(&want, 0, sizeof want);
    want.freq     = (int)(a->v->audio_rate ? a->v->audio_rate
                                           : OPENMMO_VIEW_AUDIO_RATE);
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    /* 1024 frames is 31 ms at the console's rate and 21 ms at 48,000: short
     * enough to stay in step with the picture, long enough to ride a
     * scheduling hiccup. */
    want.samples  = 1024;
    want.callback = view_audio_cb;
    want.userdata = a;

    dev = SDL_OpenAudioDevice(device, 0, &want, &have, 0);
    if (dev == 0)
        dev = SDL_OpenAudioDevice(device, 0, &want, &have,
                                  SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (dev == 0) {
        fprintf(stderr, "openmmo-view: cannot open an audio device (%s); the "
                        "picture still works.\n", SDL_GetError());
        return 0;
    }
    a->dev_rate = (uint32_t)(have.freq > 0 ? have.freq : want.freq);
    if (a->sinc == NULL)
        a->sinc = view_audio_sinc_build();
    view_audio_rebase(a);
    fprintf(stderr, "openmmo-view: audio at %d Hz%s%s\n", have.freq,
            device ? " on " : "", device ? device : "");
    /* The ratio at the top, because it is the one number that decides pitch:
     * 65536 is one ring frame per output frame and anything else is the
     * window correcting for a device that did not take the game's rate. A
     * pitch report is answered by this line before anything else is read. */
    view_audio_logf("open: want %d Hz have %d Hz, %u-frame device buffer, "
                    "ratio %u/65536%s%s",
                    want.freq, have.freq, (unsigned)have.samples,
                    (unsigned)a->base,
                    device ? " on " : "", device ? device : "");
    return dev;
}

static void view_list_audio_devices(void)
{
    int i, n;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "openmmo-view: SDL audio: %s\n", SDL_GetError());
        return;
    }
    n = SDL_GetNumAudioDevices(0);
    for (i = 0; i < n; i++) printf("%s\n", SDL_GetAudioDeviceName(i, 0));
}

/* ------------------------------------------------------------------ */
/* Screenshots                                                         */
/* ------------------------------------------------------------------ */

/*
 * The presented frame at the renderer's real output size, not the logical one: a screenshot at
 * the window's resolution is the picture the player is looking at, upscaler and all.
 */
static int view_write_shot(SDL_Renderer *ren, const char *path)
{
    int ow = 0, oh = 0, y, x;
    SDL_Surface *sf;
    FILE *f;

    SDL_GetRendererOutputSize(ren, &ow, &oh);
    sf = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_XRGB8888);
    if (sf == NULL ||
        SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_XRGB8888,
                             sf->pixels, sf->pitch) != 0) {
        fprintf(stderr, "openmmo-view: readback failed: %s\n", SDL_GetError());
        if (sf != NULL) SDL_FreeSurface(sf);
        return -1;
    }
    f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "openmmo-view: %s: %s\n", path, strerror(errno));
        SDL_FreeSurface(sf);
        return -1;
    }
    fprintf(f, "P6\n%d %d\n255\n", ow, oh);
    for (y = 0; y < oh; y++) {
        const uint32_t *row = (const uint32_t *)((char *)sf->pixels
                                                 + (size_t)y * sf->pitch);

        for (x = 0; x < ow; x++) {
            unsigned char rgb[3] = { (unsigned char)((row[x] >> 16) & 0xFF),
                                     (unsigned char)((row[x] >> 8) & 0xFF),
                                     (unsigned char)(row[x] & 0xFF) };

            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    SDL_FreeSurface(sf);
    fprintf(stderr, "openmmo-view: wrote %s\n", path);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The window                                                          */
/* ------------------------------------------------------------------ */

static struct openmmo_view_geom view_geom_of(const struct view_opts *o)
{
    struct openmmo_view_geom g;

    g.layout = o->layout;
    g.aspect = o->aspect;
    g.integer = o->integer;
    g.overlay = 0;
    g.swapped = 0;
    g.hide_second = 0;
    return g;
}

/*
 * Whether this window draws a second screen at all. Where the guest says its lower screen is
 * the Poketch, the default is that it does not, the world and the UI layer are the whole
 * picture, and the bar's Poketch button (K) asks for it back.
 */
static int viewer_hides_second(const struct viewer *vw)
{
    return vw->hud.any &&
           vw->hud.snap.lower == OPENMMO_HUD_LOWER_POKETCH &&
           !view_ui_bar_poketch(&vw->ui_bar);
}

static int viewer_poketch(const struct viewer *vw, const struct view_opts *o)
{
    return o->layout == OPENMMO_LAYOUT_FILL && viewer_hides_second(vw);
}

static struct openmmo_view_geom viewer_geom(const struct viewer *vw,
                                           const struct view_opts *o)
{
    struct openmmo_view_geom g = view_geom_of(o);

    g.overlay = viewer_poketch(vw, o);
    g.hide_second = viewer_hides_second(vw);
    /* The game says when it has handed the player the two screens the other
     * way round; the Underground is the one place it does. */
    g.swapped = (vw->status.flags & OPENMMO_STATUS_F_UNDERGROUND) != 0;
    return g;
}

static void view_sdl_rect(SDL_Rect *d, const struct openmmo_rect *s)
{
    d->x = s->x; d->y = s->y; d->w = s->w; d->h = s->h;
}

/* One frame of the window as the UI layer sees it: the canvas, the world
 * rect from the same geometry the present uses, and whether a text field
 * owns the keyboard. Elements follow every layout because this is
 * recomputed rather than remembered. */
static struct view_ui_frame viewer_ui_frame(struct viewer *vw,
                                            const struct view_opts *o,
                                            int typing)
{
    struct view_ui_frame f;
    struct openmmo_view_geom g = viewer_geom(vw, o);
    struct openmmo_rect rects[2];
    int ww = 0, hh = 0;

    memset(&f, 0, sizeof f);
    memset(rects, 0, sizeof rects);
    SDL_GetRendererOutputSize(vw->ren, &ww, &hh);
    f.win_w = ww;
    f.win_h = hh;
    /* A frame's editfield owns the keyboard the same way the typed-line
     * page does: while either is set no element treats a letter as a
     * chord. */
    f.typing = typing || view_ui_wins_typing(&vw->ui_wins);
    openmmo_view_screen_rects(&g, vw->src_w, vw->src_h, vw->sec, ww, hh,
                              rects);
    f.world = rects[0];
    f.touch = rects[1];
    f.canvas = view_ui_canvas(vw->ui.canvas_mode, ww, hh, &f.world, &f.touch);
    return f;
}

/* The layer, last: over the world, the plates, the panel and the status
 * caption, because it is the window's own UI and the window is on top of
 * everything it presents. */
static void viewer_draw_ui(struct viewer *vw, int win_w, int win_h,
                           const struct openmmo_rect rects[2])
{
    struct view_ui_frame f;

    memset(&f, 0, sizeof f);
    f.win_w = win_w;
    f.win_h = win_h;
    f.typing = vw->hud.compose || view_ui_wins_typing(&vw->ui_wins);
    f.world = rects[0];
    f.touch = rects[1];
    f.canvas = view_ui_canvas(vw->ui.canvas_mode, win_w, win_h,
                              &f.world, &f.touch);
    view_ui_layer_draw(&vw->ui, vw->ren, &vw->ui_gpu, &f);
}

/*
 * Texture source rectangles. fill presents the full published top screen and only crops the
 * lower one to the 256-column panel (inset in the 3:2 dest).
 */
static void view_src_rects(const struct view_opts *o, int sw, int sh,
                           SDL_Rect *src0, SDL_Rect *src1)
{
    struct openmmo_rect panel;

    openmmo_view_panel_src(sw, sh, o->rs, &panel);
    if (o->layout == OPENMMO_LAYOUT_FILL) {
        src0->x = 0;
        src0->y = 0;
        src0->w = sw * o->rs;
        src0->h = sh * o->rs;
        view_sdl_rect(src1, &panel);
    } else {
        view_sdl_rect(src0, &panel);
        *src1 = *src0;
    }
}

static void viewer_claim_input(struct viewer *vw)
{
    if (vw->win == NULL)
        return;
    SDL_RaiseWindow(vw->win);
    SDL_SetWindowInputFocus(vw->win);
}

static void viewer_title_for_focus(struct viewer *vw, int stalled)
{
    Uint32 fl;

    if (vw->win == NULL)
        return;
    if (stalled) {
        SDL_SetWindowTitle(vw->win, VIEW_NAME " (stalled)");
        return;
    }
    fl = SDL_GetWindowFlags(vw->win);
    if (fl & SDL_WINDOW_INPUT_FOCUS)
        SDL_SetWindowTitle(vw->win, VIEW_NAME);
    else
        SDL_SetWindowTitle(vw->win, VIEW_NAME " (click this window)");
}

static int viewer_open_video(struct viewer *vw, const struct view_opts *o)
{
    int cw, ch, i;

    /* The window opens before anything has been published: this program's
     * first job is to be a window saying what is happening, and only then a
     * picture. --scale N is still N x the composed size; --scale auto (the
     * default) picks from the display table; --size names the pixels. */
    vw->sec = OPENMMO_VIEW_SEC_MIN;
    vw->src_w = OPENMMO_VIEW_W;
    vw->src_h = OPENMMO_VIEW_H;
    openmmo_view_composed_wh(o->layout, vw->src_w, vw->src_h, vw->sec, &cw, &ch);
    {
        int ww, wh;

        if (o->win_w > 0 && o->win_h > 0) {
            ww = o->win_w;
            wh = o->win_h;
        } else if (o->scale == OPENMMO_VIEW_SCALE_AUTO) {
            SDL_Rect usable;

            if (SDL_GetDisplayUsableBounds(0, &usable) != 0) {
                usable.w = 1920;
                usable.h = 1080;
            }
            openmmo_view_auto_wh(usable.w, usable.h, &ww, &wh);
        } else {
            ww = cw * o->scale;
            wh = ch * o->scale;
        }
        vw->win = SDL_CreateWindow(VIEW_NAME,
                                   SDL_WINDOWPOS_UNDEFINED,
                                   SDL_WINDOWPOS_UNDEFINED,
                                   ww, wh,
                                   SDL_WINDOW_RESIZABLE | SDL_WINDOW_INPUT_FOCUS);
    }
    if (vw->win == NULL) {
        fprintf(stderr, "openmmo-view: cannot open a window: %s\n",
                SDL_GetError());
        return -1;
    }
    viewer_claim_input(vw);
    if (o->fullscreen)
        SDL_SetWindowFullscreen(vw->win, SDL_WINDOW_FULLSCREEN_DESKTOP);

    /*
     * Ask for accelerated+vsync, fall back to software, then check what was granted: naming
     * PRESENTVSYNC names a property and not a driver, and a backend is free to hand back a
     * renderer that ignores it. A loop that believes an unpaced renderer runs flat out.
     */
    vw->ren = SDL_CreateRenderer(vw->win, -1, SDL_RENDERER_ACCELERATED |
                                              SDL_RENDERER_PRESENTVSYNC);
    if (vw->ren == NULL)
        vw->ren = SDL_CreateRenderer(vw->win, -1, SDL_RENDERER_SOFTWARE);
    if (vw->ren == NULL) {
        fprintf(stderr, "openmmo-view: cannot create a renderer: %s\n",
                SDL_GetError());
        return -1;
    }
    {
        SDL_RendererInfo ri;

        if (SDL_GetRendererInfo(vw->ren, &ri) == 0) {
            vw->vsync = (ri.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
            /* The software renderer reports the flag and blocks on nothing. A
             * claim of vsync from a renderer with no display link behind it is
             * not pacing, so the clock paces there whatever the flag says. */
            if (strcmp(ri.name, "software") == 0) vw->vsync = 0;
            fprintf(stderr, "openmmo-view: video %s, renderer %s%s\n",
                    SDL_GetCurrentVideoDriver(), ri.name,
                    vw->vsync ? ", vsync" : ", paced by the clock");
        }
    }

    /* The hint is read at texture creation. Nearest keeps a source pixel a
     * square; linear over a replicated texture is sharp-bilinear. */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,
                o->filter == OPENMMO_FILTER_LINEAR ? "linear" : "nearest");
    /* Sized for the widest frame the protocol allows and used a frame at a
     * time: the width can change between frames when the game follows the
     * window, and reallocating a texture mid-drag would make a resize flicker. */
    for (i = 0; i < 2; i++) {
        vw->tex[i] = SDL_CreateTexture(vw->ren, SDL_PIXELFORMAT_XRGB8888,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       (int)OPENMMO_VIEW_WIDE_MAX
                                         * (int)OPENMMO_VIEW_HD_MAX * o->rs,
                                       OPENMMO_VIEW_H * (int)OPENMMO_VIEW_HD_MAX
                                         * o->rs);
        /* Only a real scale needs a buffer of its own. At 1 the upload reads
         * the seqlock's frame straight out (viewer_present), so this would be
         * 8.4 MB a screen, the widest page at the highest scale, that
         * nothing can ever read, on a handheld, twice. */
        vw->scaled[i] = o->rs > 1
                        ? malloc((size_t)OPENMMO_VIEW_FRAME_WORDS
                                 * o->rs * o->rs * 4)
                        : NULL;
        if (vw->tex[i] == NULL || (o->rs > 1 && vw->scaled[i] == NULL)) {
            fprintf(stderr, "openmmo-view: SDL setup: %s\n", SDL_GetError());
            return -1;
        }
    }
    vw->mid = NULL;
    if (o->rs == 4 && o->filter == OPENMMO_FILTER_SCALE2X) {
        vw->mid = malloc((size_t)OPENMMO_VIEW_FRAME_WORDS * 4 * 4);
        if (vw->mid == NULL) return -1;
    }
    return 0;
}

static void viewer_close_video(struct viewer *vw)
{
    int i;

    for (i = 0; i < 2; i++) {
        if (vw->tex[i] != NULL) SDL_DestroyTexture(vw->tex[i]);
        free(vw->scaled[i]);
        vw->tex[i] = NULL;
        vw->scaled[i] = NULL;
    }
    free(vw->mid);
    vw->mid = NULL;
    if (vw->msg != NULL) SDL_DestroyTexture(vw->msg);
    vw->msg = NULL;
    view_status_close(&vw->status);
    if (vw->plate_tex != NULL) {
        SDL_DestroyTexture(vw->plate_tex);
        vw->plate_tex = NULL;
    }
    view_ui_gpu_free(&vw->ui_gpu);
    view_hud_close(&vw->hud);
    if (vw->ren != NULL) SDL_DestroyRenderer(vw->ren);
    if (vw->win != NULL) SDL_DestroyWindow(vw->win);
    vw->ren = NULL;
    vw->win = NULL;
}

/* ------------------------------------------------------------------ */
/* The message screen                                                  */
/* ------------------------------------------------------------------ */

/*
 * What the window shows when there is no picture to show. The channel names one failure by
 * name, a page of the wrong version, which must be refused rather than drawn, and this is
 * where that refusal is stated.
 */
#define MSG_W 320
#define MSG_H 180

struct view_msg {
    const char *line[5];
    int n;
    uint32_t accent;
};

static void view_msg_lines(struct view_msg *m, enum attach_result st,
                           const struct viewer *vw, const struct view_opts *o,
                           char scratch[3][96])
{
    memset(m, 0, sizeof *m);
    switch (st) {
    case ATTACH_BAD_VERSION:
        snprintf(scratch[0], 96, "THE GAME PUBLISHES VERSION %u",
                 (unsigned)vw->their_version);
        snprintf(scratch[1], 96, "THIS WINDOW READS VERSION %u",
                 (unsigned)OPENMMO_VIEW_VERSION);
        m->line[0] = "FRAME CHANNEL VERSION MISMATCH";
        m->line[1] = scratch[0];
        m->line[2] = scratch[1];
        m->line[3] = "REBUILD WHICHEVER SIDE IS STALE";
        m->line[4] = "NOTHING IS DRAWN: THE PAGE MOVED";
        m->n = 5;
        m->accent = 0xE05050;
        break;
    /* Said as a wait, because that is what it usually is: the page exists and
     * the game has not stamped it yet. The third line is the other reading,
     * which is the one left standing if it never does. */
    case ATTACH_NO_MAGIC:
        snprintf(scratch[0], 96, "'%s' IS NOT FILLED IN", o->name);
        m->line[0] = "WAITING FOR THE GAME";
        m->line[1] = scratch[0];
        m->line[2] = "IF IT STAYS, THE NAME IS TAKEN";
        m->n = 3;
        m->accent = 0xE0A030;
        break;
    case ATTACH_NO_CHANNEL:
        snprintf(scratch[0], 96, "NO CHANNEL NAMED '%s'", o->name);
        snprintf(scratch[1], 96, "START THE GAME WITH PC_VIEW=%s", o->name);
        m->line[0] = "THE GAME IS NOT RUNNING";
        m->line[1] = scratch[0];
        m->line[2] = scratch[1];
        m->n = 3;
        m->accent = 0xE0A030;
        break;
    case ATTACH_OK:
        snprintf(scratch[0], 96, "CHANNEL '%s'", o->name);
        m->line[0] = "WAITING FOR THE FIRST FRAME";
        m->line[1] = scratch[0];
        m->n = 2;
        m->accent = 0x50A0E0;
        break;
    }
}

/*
 * A session that ended, on the screen the note below gives the job to. Only once it is over:
 * view_status_banner answers for live states too, and those belong to the UI layer over the
 * picture rather than to a screen that replaces it.
 */
static int view_msg_over(struct view_msg *m, const struct viewer *vw,
                         char scratch[3][96])
{
    int rejoining;

    if (!vw->status.any)
        return 0;
    /* A rejoin campaign takes the screen too: the world behind it is the dead
     * session's, frozen by the game, and a picture of it under a live caption
     * would say the opposite of the truth. IN_GAME means the campaign has
     * already succeeded and the flag is a frame from clearing. */
    rejoining = (vw->status.flags & OPENMMO_STATUS_F_REJOIN) != 0 &&
                vw->status.state != OPENMMO_ST_IN_GAME;
    if (!rejoining &&
        !openmmo_status_over(vw->status.state, vw->status.flags))
        return 0;
    /* And only while the game that published it is still there to be told about. */
    if (vw->status.page == NULL ||
        !mmo_plat_pid_alive((unsigned)vw->status.page->writer))
        return 0;
    if (!view_status_banner(&vw->status, 0, scratch[0], 96, scratch[1], 96,
                            NULL))
        return 0;
    memset(m, 0, sizeof *m);
    m->line[m->n++] = scratch[0];
    if (scratch[1][0] != '\0')
        m->line[m->n++] = scratch[1];
    /*
     * Only for a link that broke and stayed broken. The character lives on the server, so
     * after a drop the way back in is the front door and not a lost save.
     */
    if (!rejoining && (vw->status.flags & OPENMMO_STATUS_F_WAS_LIVE))
        m->line[m->n++] = "SIGN IN AGAIN TO CARRY ON";
    /* Amber while it is being fixed, red once it could not be. */
    m->accent = rejoining ? 0xE0A030 : 0xE05050;
    return 1;
}

static void viewer_present_message(struct viewer *vw, const struct view_opts *o,
                                   const struct view_msg *m)
{
    static uint32_t px[MSG_W * MSG_H];
    struct openmmo_rect r;
    SDL_Rect dst;
    int win_w = 0, win_h = 0, i, y;

    for (i = 0; i < MSG_W * MSG_H; i++) px[i] = 0x101418;
    for (i = 0; i < MSG_W; i++) {
        px[(size_t)44 * MSG_W + i] = m->accent;
        px[(size_t)45 * MSG_W + i] = m->accent;
    }
    openmmo_font_draw_centred(px, MSG_W, MSG_H, MSG_W / 2, 24,
                              VIEW_NAME, 3, 0xF0F0F0);
    y = 62;
    for (i = 0; i < m->n; i++) {
        openmmo_font_draw_centred(px, MSG_W, MSG_H, MSG_W / 2, y, m->line[i],
                                  1, i == 0 ? m->accent : 0xB8C0C8);
        y += i == 0 ? 16 : 12;
    }

    if (vw->msg == NULL)
        vw->msg = SDL_CreateTexture(vw->ren, SDL_PIXELFORMAT_XRGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, MSG_W, MSG_H);
    if (vw->msg == NULL) return;
    SDL_UpdateTexture(vw->msg, NULL, px, MSG_W * 4);

    SDL_GetRendererOutputSize(vw->ren, &win_w, &win_h);
    /* Fitted, never stretched: the message keeps its proportions whatever the
     * player has done to the window, including --stretch, which is about the
     * game's picture and not about this. */
    r = openmmo_view_fit(MSG_W, MSG_H, win_w, win_h, o->integer,
                         OPENMMO_ASPECT_NATIVE);
    view_sdl_rect(&dst, &r);
    SDL_SetRenderDrawColor(vw->ren, 0, 0, 0, 255);
    SDL_RenderClear(vw->ren);
    SDL_RenderCopy(vw->ren, vw->msg, NULL, &dst);
    SDL_RenderPresent(vw->ren);
}

/* The session-state strip is gone from the picture entirely. */

/* ------------------------------------------------------------------ */
/* Presenting a frame                                                  */
/* ------------------------------------------------------------------ */

/*
 * The window's shape, reported back so a game asked for an adaptive aspect can render into it.
 */
static void viewer_report_aspect(struct viewer *vw, const struct view_opts *o,
                                 int win_w, int win_h)
{
    unsigned n, d;

    if (o->layout == OPENMMO_LAYOUT_WIDE) { n = (unsigned)win_w / 2u;
                                            d = (unsigned)win_h; }
    else if (o->layout == OPENMMO_LAYOUT_SMART) {
        /* The main screen's own area, which is what the game renders into; the
         * touch screen beside it is a passenger. */
        n = (unsigned)((long)win_w * 100 / (100 + vw->sec));
        d = (unsigned)win_h;
    } else if (o->layout == OPENMMO_LAYOUT_FILL) {
        if (viewer_poketch(vw, o)) {
            n = (unsigned)win_w;
            d = (unsigned)win_h;
        } else {
            struct openmmo_rect panel;

            openmmo_view_fill_split(win_w, win_h, vw->sec, &panel);
            n = (unsigned)(panel.x > 0 ? panel.x : 1);
            d = (unsigned)win_h;
        }
    } else                                { n = (unsigned)win_w;
                                            d = (unsigned)win_h / 2u; }
    if (n == 0 || d == 0) return;
    if (vw->shm->in_aspect_n == n && vw->shm->in_aspect_d == d) return;
    vw->shm->in_aspect_n = n;
    vw->shm->in_aspect_d = d;
}

/*
 * Sizing the window to the picture, once. The window opens before the channel exists, so the
 * first frame is the first time this program knows what shape the game is: a higher internal
 * resolution gets a window shaped for the enlarged panel.
 */
static void viewer_size_to_frame(struct viewer *vw, const struct view_opts *o,
                                 int sw, int sh)
{
    Uint32 fl = SDL_GetWindowFlags(vw->win);
    SDL_Rect usable;
    int cw, ch, w, h;

    if (fl & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP
              | SDL_WINDOW_MAXIMIZED))
        return;
    /* fill never resizes itself: the world is the window and the panel only
     * covers more of it. --scale auto and --size already named the pixels. */
    if (o->layout == OPENMMO_LAYOUT_FILL ||
        o->scale == OPENMMO_VIEW_SCALE_AUTO ||
        (o->win_w > 0 && o->win_h > 0))
        return;
    openmmo_view_composed_wh(o->layout, sw, sh, vw->sec, &cw, &ch);
    w = cw * o->scale;
    h = ch * o->scale;
    if (SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(vw->win),
                                   &usable) == 0) {
        if (w > usable.w && w > 0) { h = (int)((long)h * usable.w / w);
                                     w = usable.w; }
        if (h > usable.h && h > 0) { w = (int)((long)w * usable.h / h);
                                     h = usable.h; }
    }
    if (w > 0 && h > 0) SDL_SetWindowSize(vw->win, w, h);
}

static void viewer_blit_guest(struct viewer *vw, const struct view_opts *o,
                              const SDL_Rect *src1, const SDL_Rect *dst1)
{
    (void)o;
    /* The Poketch is never shown: its rect stays the window's ground. The
     * bag, a battle, the lobby, anything the guest actually puts on its
     * lower screen, still gets the band. */
    if (viewer_hides_second(vw))
        return;
    SDL_RenderCopy(vw->ren, vw->tex[1], src1, dst1);
}

/* THE names over THE players, drawn here rather than by the game. */
static void viewer_draw_plates(struct viewer *vw, const SDL_Rect *src,
                               const SDL_Rect *dst, int sw, int sh, int rs)
{
    const struct openmmo_hud_snap *s;
    int hd, margin, scale, i;

    if (vw == NULL || !vw->hud.any || src == NULL || dst == NULL)
        return;
    /* The guest's own window is in front of the world while it is up, and a
     * name painted after it lands on top of the menu it belongs to. The plates
     * are held rather than cleared while this is set (openmmo_label.c), so they
     * come back over the right heads the moment the box closes. */
    if (vw->hud.snap.guest_busy)
        return;
    if (src->w <= 0 || src->h <= 0)
        return;
    s = &vw->hud.snap;
    if (s->plate_n == 0)
        return;

    hd = sh / OPENMMO_VIEW_H;
    if (hd < 1)
        hd = 1;
    margin = (sw - OPENMMO_VIEW_W * hd) / 2;   /* published columns, one side */
    scale = dst->h / OPENMMO_VIEW_H;           /* window pixels per DS row */
    if (scale < 1)
        scale = 1;
    if (scale > 8)
        scale = 8;

    if (vw->plate_tex == NULL) {
        vw->plate_tw = 1024;
        vw->plate_th = VIEW_HUD_PLATE_H * 8;
        vw->plate_tex = SDL_CreateTexture(vw->ren, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STREAMING,
                                          vw->plate_tw, vw->plate_th);
        if (vw->plate_tex == NULL)
            return;
        SDL_SetTextureBlendMode(vw->plate_tex, SDL_BLENDMODE_BLEND);
    }

    for (i = 0; i < (int)s->plate_n && i < OPENMMO_HUD_PLATE_N; i++) {
        const struct openmmo_hud_plate *p = &s->plate[i];
        uint32_t *px = NULL;
        SDL_Rect box, to;
        int pitch = 0, bw = 0, bh = 0, sc = scale, tx, ty;

        /* Shrink rather than clip: a long name on a huge window is still a
         * name, and a plate that fits nothing is not drawn at all. */
        while (sc > 1 && (!view_hud_plate_size(vw->hud.have_font ? &vw->hud.font : NULL,
                                               p->name, sc, &bw, &bh)
                          || bw > vw->plate_tw || bh > vw->plate_th))
            sc--;
        if (!view_hud_plate_size(vw->hud.have_font ? &vw->hud.font : NULL,
                                 p->name, sc, &bw, &bh))
            continue;
        if (bw > vw->plate_tw || bh > vw->plate_th)
            continue;

        box.x = 0; box.y = 0; box.w = bw; box.h = bh;
        if (SDL_LockTexture(vw->plate_tex, &box, (void **)&px, &pitch) != 0)
            continue;
        memset(px, 0, (size_t)pitch * (size_t)bh);
        if (!view_hud_plate_raster(px, pitch / 4, bh,
                                   vw->hud.have_font ? &vw->hud.font : NULL,
                                   p->name, sc, NULL, NULL)) {
            SDL_UnlockTexture(vw->plate_tex);
            continue;
        }
        SDL_UnlockTexture(vw->plate_tex);

        /* The anchor, from the DS's own 256x192 space into the window. */
        tx = (margin + p->x * hd) * rs;
        ty = (p->y * hd) * rs;
        to.x = dst->x + (int)(((long)(tx - src->x) * dst->w) / src->w) - bw / 2;
        to.y = dst->y + (int)(((long)(ty - src->y) * dst->h) / src->h)
               - (VIEW_HUD_PLATE_H + 1) * sc;
        to.w = bw;
        to.h = bh;
        SDL_RenderCopy(vw->ren, vw->plate_tex, &box, &to);
    }
}

/*
 * OPENMMO_VIEW_TRACE=1: where a presented frame's own time goes, printed every 300 presents,
 * the seqlock copy out of the page, the CPU prescale, the texture upload, and the present with
 * its vsync wait folded in.
 */
static struct {
    int on;                             /* -1 until the environment is read */
    unsigned n;
    double copy, scale, upload, present, worst;
} view_trace = { -1, 0, 0.0, 0.0, 0.0, 0.0, 0.0 };

static double view_trace_ms(Uint64 from, Uint64 to)
{
    return (double)(to - from) * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

/*
 * What reached THE panel, which is the half no engine trace can see. pc-pace answers "did the
 * game make its frame in time"; a player saying the game stutters is answering a different
 * question, "did a new picture arrive on every refresh".
 */
static struct {
    int on;                             /* -1 until the environment is read */
    int seen;                           /* a previous frame is on record */
    uint32_t last;                      /* seq of the last frame presented */
    Uint64 t0;                          /* when this window's tally started */
    unsigned presents, fresh, repeats, skips, lost;
    unsigned run, worst_run;            /* repeats in a row, and the worst */
} view_frames = { -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/*
 * One present's verdict. `seq` is the frame that went up, or 0 for a present that could not
 * read one whole and left the previous picture there, which is a repeat by any name a player
 * would use for it.
 */
static void view_frames_note(uint32_t seq)
{
    unsigned advanced;

    if (view_frames.on < 0) {
        const char *e = getenv("OPENMMO_VIEW_FRAMES");

        view_frames.on = e != NULL && e[0] != '\0' && e[0] != '0';
        view_frames.t0 = SDL_GetTicks64();
    }
    if (!view_frames.on) return;
    view_frames.presents++;
    if (!view_frames.seen) {
        /* The first present has nothing to compare against and is neither. */
        if (seq != 0) {
            view_frames.seen = 1;
            view_frames.last = seq;
        }
        return;
    }
    /* The page's seq bumps twice a frame, odd while the writer is inside
     * it, even when it is stable, so the halving is what makes this count
     * frames rather than twice as many of them. */
    advanced = (seq == 0) ? 0u : (unsigned)((seq - view_frames.last) / 2u);
    if (seq != 0) view_frames.last = seq;
    if (advanced == 1u) {
        view_frames.fresh++;
        view_frames.run = 0;
    } else if (advanced == 0u) {
        view_frames.repeats++;
        if (++view_frames.run > view_frames.worst_run)
            view_frames.worst_run = view_frames.run;
    } else {
        view_frames.skips++;
        view_frames.lost += advanced - 1u;
        view_frames.run = 0;
    }
}

/* The tally, five-secondly, on the same cadence pc-pace prints its own so the
 * two lines in a log can be read against each other. */
static void view_frames_report(void)
{
    Uint64 now;
    double secs;

    if (view_frames.on <= 0) return;
    now = SDL_GetTicks64();
    if (now - view_frames.t0 < 5000u) return;
    secs = (double)(now - view_frames.t0) / 1000.0;
    fprintf(stderr, "view-frames: %u presents in %.2f s (%.1f/s), %u fresh,"
                    " %u repeats, %u skips (%u frames never shown),"
                    " worst run %u\n",
            view_frames.presents, secs, (double)view_frames.presents / secs,
            view_frames.fresh, view_frames.repeats, view_frames.skips,
            view_frames.lost, view_frames.worst_run);
    fflush(stderr);
    view_frames.t0 = now;
    view_frames.presents = view_frames.fresh = view_frames.repeats = 0;
    view_frames.skips = view_frames.lost = view_frames.worst_run = 0;
}

/* One frame: seqlock-copy, upscale, place, present. `frame` is the caller's
 * scratch so every path through this program shares the exact code. */
static void viewer_present(struct viewer *vw, const struct view_opts *o,
                           uint32_t frame[2][OPENMMO_VIEW_FRAME_WORDS])
{
    Uint64 tt0 = 0, tt1 = 0, tt2 = 0, tt3 = 0, tt4 = 0;
    struct openmmo_view_geom g = viewer_geom(vw, o);
    struct openmmo_rect rects[2];
    /* The two screens as the upload wants them: the scaled buffers when there
     * is a scale, and the seqlock's own frames when there is not. */
    const uint32_t *up[2] = { NULL, NULL };
    SDL_Rect dst[2], src0, src1;
    int win_w = 0, win_h = 0, upper = 0, tries, got = 0;
    int sw = OPENMMO_VIEW_W, sh = OPENMMO_VIEW_H;
    uint32_t s0, s1;

    /*
     * Retries WITH A pause, and none of them is worth anything without it: sixteen attempts
     * with no delay all land inside the same write, because the odd-seq check costs
     * nanoseconds and burns a try.
     */
    if (view_trace.on < 0) {
        const char *e = getenv("OPENMMO_VIEW_TRACE");

        view_trace.on = e != NULL && e[0] != '\0' && e[0] != '0';
    }
    if (view_trace.on) tt0 = SDL_GetPerformanceCounter();

    for (tries = 0; tries < 16; tries++) {
        s0 = vw->shm->seq;
        if (s0 & 1) { if (tries >= 2) view_sleep_ms(1); continue; }
        __sync_synchronize();
        sw = (int)vw->shm->width;
        sh = (int)vw->shm->height;
        if (sw < OPENMMO_VIEW_W) sw = OPENMMO_VIEW_W;
        if (sw > (int)(OPENMMO_VIEW_WIDE_MAX * OPENMMO_VIEW_HD_MAX))
            sw = (int)(OPENMMO_VIEW_WIDE_MAX * OPENMMO_VIEW_HD_MAX);
        if (sh < OPENMMO_VIEW_H) sh = OPENMMO_VIEW_H;
        if (sh > (int)(OPENMMO_VIEW_H * OPENMMO_VIEW_HD_MAX))
            sh = (int)(OPENMMO_VIEW_H * OPENMMO_VIEW_HD_MAX);
        memcpy(frame[0], (const void *)vw->shm->pix[0], (size_t)sw * sh * 4);
        memcpy(frame[1], (const void *)vw->shm->pix[1], (size_t)sw * sh * 4);
        upper = (int)vw->shm->upper_engine;
        __sync_synchronize();
        s1 = vw->shm->seq;
        if (s0 == s1) { got = 1; break; }
        if (tries >= 2) view_sleep_ms(1);
    }

    /*
     * A FRAME that could NOT be read IS NOT DRAWN. What is in the buffer after sixteen
     * failures is half of one frame and half of another, and putting that on screen is a flash
     * a person sees; keeping the last good picture for one more frame is invisible.
     */
    if (!got) {
        vw->torn++;
        SDL_GetRendererOutputSize(vw->ren, &win_w, &win_h);
        viewer_report_aspect(vw, o, win_w, win_h);
        view_src_rects(o, vw->src_w, vw->src_h, &src0, &src1);
        openmmo_view_screen_rects(&g, vw->src_w, vw->src_h, vw->sec,
                                  win_w, win_h, rects);
        view_sdl_rect(&dst[0], &rects[0]);
        view_sdl_rect(&dst[1], &rects[1]);
        SDL_SetRenderDrawColor(vw->ren, 0, 0, 0, 255);
        SDL_RenderClear(vw->ren);
        SDL_RenderCopy(vw->ren, vw->tex[0], &src0, &dst[0]);
        viewer_draw_plates(vw, &src0, &dst[0], vw->src_w, vw->src_h, o->rs);
        viewer_blit_guest(vw, o, &src1, &dst[1]);
        viewer_draw_ui(vw, win_w, win_h, rects);
        view_frames_note(0);
        SDL_RenderPresent(vw->ren);
        return;
    }
    vw->presented++;
    if (view_trace.on) tt1 = SDL_GetPerformanceCounter();

    /*
     * Which screen the big rect gets. Normally the console's upper one, which is where the
     * game draws the world; swapped, the touch screen, because that is where the world is then
     * (see openmmo_view_geom.swapped).
     */
    {
        int big = openmmo_view_swaps_screens(&g) ? (upper ? 0 : 1)
                                                 : (upper ? 1 : 0);

        /*
         * At scale 1 there IS NOTHING TO scale, and the buffer the seqlock already filled is
         * the one to upload.
         */
        if (o->rs > 1) {
            openmmo_view_upscale(frame[big], sw, sh,
                                 vw->scaled[0], vw->mid, o->rs, o->filter);
            openmmo_view_upscale(frame[big ^ 1], sw, sh,
                                 vw->scaled[1], vw->mid, o->rs, o->filter);
            up[0] = vw->scaled[0];
            up[1] = vw->scaled[1];
        } else {
            up[0] = frame[big];
            up[1] = frame[big ^ 1];
        }
    }
    if (view_trace.on) tt2 = SDL_GetPerformanceCounter();
    /* Into the corner of a texture sized for the widest frame; the rest of it
     * is stale and never drawn, because every copy names this rect. */
    {
        SDL_Rect full = { 0, 0, sw * o->rs, sh * o->rs };

        SDL_UpdateTexture(vw->tex[0], &full, up[0], sw * o->rs * 4);
        SDL_UpdateTexture(vw->tex[1], &full, up[1], sw * o->rs * 4);
        view_src_rects(o, sw, sh, &src0, &src1);
    }
    if (view_trace.on) tt3 = SDL_GetPerformanceCounter();

    vw->src_w = sw;
    vw->src_h = sh;
    if (!vw->sized) {
        vw->sized = 1;
        viewer_size_to_frame(vw, o, sw, sh);
    }

    /* THE SMART layout makes room rather than taking IT. */
    if (o->layout == OPENMMO_LAYOUT_SMART ||
        o->layout == OPENMMO_LAYOUT_FILL) {
        /* A hidden second screen never asks for room: the vanilla watch
         * samples the pen, and growing a band nobody can see for it would
         * shove the world around for nothing. */
        int want = (vw->shm->touch_wanted && !viewer_hides_second(vw))
                       ? OPENMMO_VIEW_SEC_MAX
                       : OPENMMO_VIEW_SEC_MIN;
        int was = vw->sec;

        if (vw->sec < want) vw->sec += OPENMMO_VIEW_SEC_STEP;
        else if (vw->sec > want) vw->sec -= OPENMMO_VIEW_SEC_STEP;
        if (vw->sec < OPENMMO_VIEW_SEC_MIN) vw->sec = OPENMMO_VIEW_SEC_MIN;
        if (vw->sec > OPENMMO_VIEW_SEC_MAX) vw->sec = OPENMMO_VIEW_SEC_MAX;

        /* fill grows the panel in place; the window stays put. */
        if (o->layout == OPENMMO_LAYOUT_SMART && vw->sec != was) {
            Uint32 fl = SDL_GetWindowFlags(vw->win);

            if (!(fl & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP
                        | SDL_WINDOW_MAXIMIZED))) {
                SDL_Rect usable;
                int ww = 0, wh = 0, cw = 0, ch = 0, target;

                SDL_GetWindowSize(vw->win, &ww, &wh);
                openmmo_view_composed_wh(o->layout, vw->src_w, vw->src_h,
                                         vw->sec, &cw, &ch);
                target = ch > 0 ? (int)(((long)wh * cw) / ch) : ww;
                if (SDL_GetDisplayUsableBounds(
                        SDL_GetWindowDisplayIndex(vw->win), &usable) != 0) {
                    usable.w = target;      /* unknown: do not second-guess */
                }
                if (target > usable.w) target = usable.w;
                if (target > 0 && target != ww)
                    SDL_SetWindowSize(vw->win, target, wh);
            }
        }
    }

    SDL_GetRendererOutputSize(vw->ren, &win_w, &win_h);
    viewer_report_aspect(vw, o, win_w, win_h);
    openmmo_view_screen_rects(&g, sw, sh, vw->sec, win_w, win_h, rects);
    view_sdl_rect(&dst[0], &rects[0]);
    view_sdl_rect(&dst[1], &rects[1]);
    SDL_SetRenderDrawColor(vw->ren, 0, 0, 0, 255);
    SDL_RenderClear(vw->ren);
    SDL_RenderCopy(vw->ren, vw->tex[0], &src0, &dst[0]);
    viewer_draw_plates(vw, &src0, &dst[0], sw, sh, o->rs);
    viewer_blit_guest(vw, o, &src1, &dst[1]);
    viewer_draw_ui(vw, win_w, win_h, rects);
    view_frames_note(s0);
    SDL_RenderPresent(vw->ren);

    if (view_trace.on) {
        double total;

        tt4 = SDL_GetPerformanceCounter();
        view_trace.copy    += view_trace_ms(tt0, tt1);
        view_trace.scale   += view_trace_ms(tt1, tt2);
        view_trace.upload  += view_trace_ms(tt2, tt3);
        view_trace.present += view_trace_ms(tt3, tt4);
        total = view_trace_ms(tt0, tt4);
        if (total > view_trace.worst) view_trace.worst = total;
        if (++view_trace.n >= 300u) {
            double n = (double)view_trace.n;

            fprintf(stderr, "openmmo-view: %ux%u rs %d over %u presents:"
                            " copy %.2f  prescale %.2f  upload %.2f"
                            "  present %.2f ms (worst frame %.2f)\n",
                    (unsigned)sw, (unsigned)sh, o->rs, view_trace.n,
                    view_trace.copy / n, view_trace.scale / n,
                    view_trace.upload / n, view_trace.present / n,
                    view_trace.worst);
            fflush(stderr);
            view_trace.n = 0;
            view_trace.copy = view_trace.scale = 0.0;
            view_trace.upload = view_trace.present = 0.0;
            view_trace.worst = 0.0;
        }
    }
}

/* ------------------------------------------------------------------ */
/* The loop                                                            */
/* ------------------------------------------------------------------ */

/*
 * One loop for both halves of this program's life. A viewer that had to be started after the
 * game could not say "the game is not running", the thing a player most needs told, so the
 * window comes up first and the channel is attached from inside the frame loop.
 */
enum view_state { VIEW_WAITING, VIEW_LIVE, VIEW_FAILED };

static int viewer_run(struct viewer *vw, const struct view_opts *o,
                      struct view_input *in)
{
    static uint32_t frame[2][OPENMMO_VIEW_FRAME_WORDS];
    struct view_audio audio = { 0 };
    struct view_msg msg;
    char scratch[3][96];
    SDL_AudioDeviceID adev = 0;
    enum attach_result st = ATTACH_NO_CHANNEL;
    enum view_state state = VIEW_WAITING;
    int audio_started = 0, running = 1, stalled = 0, rc = 0, focus_shown = -1;
    uint64_t starved_seen = 0;
    Uint64 governor_next = 0;       /* the governor's next pass, in ticks */
    uint32_t last_seq = 0;
    unsigned tick = 0, waited_ms = 0, shot_frames = 0;
    Uint64 next_tick = 0, t0 = SDL_GetTicks64();

    view_audio_log_open();
    view_msg_lines(&msg, ATTACH_NO_CHANNEL, vw, o, scratch);

    while (running) {
        SDL_Event ev;

        while (SDL_PollEvent(&ev)) {
            /* Input first, and it says whether it took the event: while a text
             * field is open Escape cancels the field rather than ending the
             * session, and a typed line is not a chord. */
            if (view_input_event(in, &ev)) continue;
            /* Then the UI layer, in every layout: a click or a wheel an
             * element claims is the element's, never also a pen tap or a
             * window chord, that claim is what makes the top screen
             * clickable. */
            {
                struct view_ui_frame f =
                    viewer_ui_frame(vw, o, view_input_typing(in));

                /* Focus follows the click: a left press anywhere off the
                 * chat panel while a line is composing puts the field away
                 * (the guest keeps the draft), and the press still lands
                 * where it fell, a button both defocuses and acts. */
                if (ev.type == SDL_MOUSEBUTTONDOWN &&
                    ev.button.button == SDL_BUTTON_LEFT &&
                    vw->hud.any && vw->hud.snap.composing &&
                    !view_ui_chat_pointer_on(&vw->ui_chat, &f, ev.button.x,
                                             ev.button.y))
                    view_hud_push(&vw->hud, OPENMMO_HUD_CMD_COMPOSE, 0);

                if (view_ui_layer_event(&vw->ui, &ev, &f)) continue;
            }
            switch (ev.type) {
            case SDL_QUIT:
                running = 0;
                break;
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_SHOWN
                    || ev.window.event == SDL_WINDOWEVENT_EXPOSED
                    || ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
                    viewer_claim_input(vw);
                viewer_title_for_focus(vw, stalled);
                break;
            case SDL_KEYDOWN:
                if (ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
                if (ev.key.keysym.sym == SDLK_F11 ||
                    (ev.key.keysym.sym == SDLK_RETURN &&
                     (ev.key.keysym.mod & KMOD_ALT) != 0)) {
                    Uint32 fs = SDL_GetWindowFlags(vw->win) &
                                SDL_WINDOW_FULLSCREEN_DESKTOP;

                    SDL_SetWindowFullscreen(vw->win, fs ? 0 :
                                            SDL_WINDOW_FULLSCREEN_DESKTOP);
                }
                if (ev.key.keysym.sym == SDLK_F12) {
                    char stamp[64], path[1024];
                    time_t t = time(NULL);
                    struct tm *tm = localtime(&t);

                    strftime(stamp, sizeof stamp, "openmmo-%Y%m%d-%H%M%S.ppm",
                             tm);
                    snprintf(path, sizeof path, "%s%s%s",
                             o->shots ? o->shots : "", o->shots ? "/" : "",
                             stamp);
                    view_write_shot(vw->ren, path);
                }
                break;
            }
        }

        /* Attaching, from inside the loop: ten times a second is far more
         * often than a game takes to start and far too rare to cost anything. */
        if (state == VIEW_WAITING && tick % 6 == 0) {
            st = viewer_attach(vw, o->name);
            if (st == ATTACH_OK) {
                fprintf(stderr, "openmmo-view: attached to '%s'\n", o->name);
                if (!o->no_audio)
                    audio.v = vw->shm;
                /* The typing page is named after this one and exists for as
                 * long as the window does, so a client that opens a text field
                 * at any point in the session finds somewhere to be typed to. */
                view_input_open_text(in, o->name);
                /*
                 * And the page the game paces against, for the same span and named the same
                 * way. Only where this renderer really presents on the panel: a window paced
                 * by SDL_Delay would be publishing its own clock, and a game pacing to that is
                 * a loop chasing itself.
                 */
                if (vw->vsync) mmo_plat_vsync_publish(o->name);
                state = VIEW_LIVE;
                vw->status_changed_ms = SDL_GetTicks64();
            } else if (st == ATTACH_BAD_VERSION) {
                /* Not a wait: the page is there and we cannot read it. */
                view_msg_lines(&msg, st, vw, o, scratch);
                fprintf(stderr, "openmmo-view: %s\n", msg.line[0]);
                state = VIEW_FAILED;
                rc = 3;
            } else if (o->wait_ms > 0 && waited_ms >= (unsigned)o->wait_ms) {
                view_msg_lines(&msg, st, vw, o, scratch);
                fprintf(stderr, "openmmo-view: %s\n", msg.line[0]);
                state = VIEW_FAILED;
                /* Told apart on the way out: nothing there at all is a game
                 * that never started, a page without our magic is a name
                 * somebody else is holding. */
                rc = st == ATTACH_NO_MAGIC ? 3 : 2;
            } else if (st == ATTACH_NO_MAGIC) {
                /* A PAGE without THE MAGIC IS A wait, NOT A refusal. */
                view_msg_lines(&msg, st, vw, o, scratch);
            }
        }

        if (state == VIEW_LIVE) {
            /* THE device opens at THE GAME'S RATE, and NOT BEFORE IT knows IT. */
            if (adev == 0 && !o->no_audio && vw->shm != NULL
                && vw->shm->audio_rate != 0) {
                /* Play from now, not from a ring of stale samples published
                 * before this window existed. */
                audio.tail = vw->shm->audio_head;
                audio.frac = 0;
                /* One ring frame per output frame until the governor has a
                 * cushion to steer by. */
                audio.step = audio.base != 0 ? audio.base : VIEW_STEP_ONE;
                adev = view_audio_open(&audio, o->audio_device);
            }
            /*
             * Start the sound only once there is a cushion, and re-establish it after a
             * starve.
             */
            if (adev != 0 && audio_started) {
                uint64_t starved_now;

                SDL_LockAudioDevice(adev);
                starved_now = audio.starved;
                SDL_UnlockAudioDevice(adev);
                if (starved_now != starved_seen) {
                    starved_seen = starved_now;
                    SDL_PauseAudioDevice(adev, 1);
                    audio_started = 0;
                    audio.backlog_avg = 0;
                    view_audio_logf("starve: paused, %llu frames padded so far",
                                    (unsigned long long)starved_now);
                }
            }
            if (adev != 0 && !audio_started) {
                uint32_t head = __atomic_load_n(&vw->shm->audio_head,
                                                __ATOMIC_ACQUIRE);

                if (head - audio.tail >= view_audio_scaled(vw->shm, 6144u)) {
                    SDL_PauseAudioDevice(adev, 0);
                    audio_started = 1;
                    view_audio_logf("start: backlog %u frames (%llu ms)",
                                    head - audio.tail,
                                    (unsigned long long)(head - audio.tail)
                                        * 1000u / view_audio_rate(vw->shm));
                }
            }
            /* The governor, and it steers a RATE now rather than cutting the waveform. */
            if (adev != 0 && audio_started
                && SDL_GetTicks64() >= governor_next) {
                uint32_t head = __atomic_load_n(&vw->shm->audio_head,
                                                __ATOMIC_ACQUIRE);
                /* The cushion to hold, and it is the one playback starts on
                 * and the starve repair re-lands on: steering towards any
                 * other number would quietly move the latency between a
                 * sound existing and a player hearing it. */
                uint32_t target = view_audio_scaled(vw->shm, 6144u);
                uint32_t backlog, skip = 0;
                long err, want_step, step, base;
                long long refresh = 0, refresh_period = 0;
                int steering;

                /*
                 * The same question the pacer asks, asked of the same marks: this window's
                 * own, because this window is the one that makes them.
                 */
                steering = vw->vsync
                           && mmo_plat_vsync_next(&refresh, &refresh_period) > 0;
                governor_next = SDL_GetTicks64() + VIEW_GOVERNOR_MS;

                SDL_LockAudioDevice(adev);
                backlog = head - audio.tail;
                if (backlog > view_audio_scaled(vw->shm, 16384u)) {
                    skip = backlog - view_audio_scaled(vw->shm, 6144u);
                    audio.tail += skip;
                    audio.trimmed += skip;
                    backlog -= skip;
                }
                /*
                 * Read faster than the game writes while the cushion is deep, slower while it
                 * is thin. Two terms, because the two things this corrects have different
                 * shapes.
                 */
                /* The pitch first, the cushion second. */
                view_audio_rebase(&audio);
                base = (long)(audio.base != 0 ? audio.base : VIEW_STEP_ONE);
                if (!steering) {
                    want_step = base;
                } else {
                    long tgt = (long)(target != 0 ? target : 1u);

                    /* THE backlog IS steered by its average, not by the sample. */
                    if (audio.backlog_avg <= 0)
                        audio.backlog_avg = (long)backlog;
                    else
                        audio.backlog_avg += ((long)backlog
                                              - audio.backlog_avg) / 4;
                    err = audio.backlog_avg - (long)target;
                    audio.integral += (err * base) / (tgt * 2000L);
                    if (audio.integral > (long)VIEW_STEP_SWING)
                        audio.integral = (long)VIEW_STEP_SWING;
                    if (audio.integral < -(long)VIEW_STEP_SWING)
                        audio.integral = -(long)VIEW_STEP_SWING;
                    want_step = base + audio.integral
                                + (err * base) / (tgt * 100L);
                    if (want_step < base - (long)VIEW_STEP_SWING)
                        want_step = base - (long)VIEW_STEP_SWING;
                    if (want_step > base + (long)VIEW_STEP_SWING)
                        want_step = base + (long)VIEW_STEP_SWING;
                }
                step = (long)(audio.step != 0 ? audio.step : (uint32_t)base);
                if (want_step > step + (long)VIEW_STEP_SLEW)
                    step += (long)VIEW_STEP_SLEW;
                else if (want_step < step - (long)VIEW_STEP_SLEW)
                    step -= (long)VIEW_STEP_SLEW;
                else
                    step = want_step;
                audio.step = (uint32_t)step;
                SDL_UnlockAudioDevice(adev);
                if (skip > view_audio_scaled(vw->shm, 6144u))
                    view_audio_logf("reskip: dropped %u frames to reland on "
                                    "the cushion", skip);
            }
            /*
             * The second-by-second ledger, and the drop alarm. `tail` and the counters belong
             * to the callback thread, so they are read under the device lock; `backlog / rate`
             * is the whole latency between a sound existing and a player hearing it, minus one
             * device buffer.
             */
            if (adev != 0) {
                static Uint64 audio_log_next;
                static uint64_t dropped_seen;
                Uint64 now_ms = SDL_GetTicks64();
                uint32_t head = __atomic_load_n(&vw->shm->audio_head,
                                                __ATOMIC_ACQUIRE);
                uint32_t tail_now, step_now;
                uint64_t dropped_now, starved_now, trimmed_now;

                SDL_LockAudioDevice(adev);
                tail_now = audio.tail;
                step_now = audio.step != 0 ? audio.step : VIEW_STEP_ONE;
                dropped_now = audio.dropped;
                starved_now = audio.starved;
                trimmed_now = audio.trimmed;
                SDL_UnlockAudioDevice(adev);
                if (dropped_now != dropped_seen) {
                    view_audio_logf("OVERRUN: dropped %llu frames; the ring "
                                    "now plays a full second late",
                                    (unsigned long long)(dropped_now
                                                         - dropped_seen));
                    dropped_seen = dropped_now;
                }
                if (now_ms >= audio_log_next) {
                    audio_log_next = now_ms + 1000;
                    view_audio_logf("ring: backlog %u frames (%llu ms) "
                                    "dropped %llu starved %llu trimmed %llu "
                                    "rate %+.3f%% started %d",
                                    head - tail_now,
                                    (unsigned long long)(head - tail_now)
                                        * 1000u / view_audio_rate(vw->shm),
                                    (unsigned long long)dropped_now,
                                    (unsigned long long)starved_now,
                                    (unsigned long long)trimmed_now,
                                    ((double)step_now - (double)VIEW_STEP_ONE)
                                        * 100.0 / (double)VIEW_STEP_ONE,
                                    audio_started);
                }
            }

            /* What the player is doing, every frame and published only when it
             * changed. Before the picture, so a button pressed during this
             * frame's wait reaches the game's next sample rather than the one
             * after it. */
            {
                struct openmmo_view_geom g = viewer_geom(vw, o);
                int in_w = 0, in_h = 0;

                SDL_GetRendererOutputSize(vw->ren, &in_w, &in_h);
                view_input_publish(in, vw->shm, &g, vw->src_w, vw->src_h,
                                   vw->sec, in_w, in_h);
                /* A pointer on a UI element is the element's in every
                 * layout: no pen under the chat box, even where the world
                 * rect is the touch screen (the Underground's swap). */
                {
                    struct view_ui_frame f =
                        viewer_ui_frame(vw, o, view_input_typing(in));
                    int mx = 0, my = 0;

                    SDL_GetMouseState(&mx, &my);
                    if (view_ui_layer_owns_pointer(&vw->ui, &f, mx, my)) {
                        vw->shm->in_touch = 0;
                        in->sent_touch = 0;
                    }
                }
                vw->hud.compose = view_input_typing(in);
            }

            /*
             * Where the session stands. Attached from inside the loop like the frame page and
             * for the same reason: a game may start its session a minute after the window
             * opened, or never, a run with no server publishes no page and simply goes
             * uncaptioned.
             */
            if (vw->status.page == NULL && tick % 6 == 0)
                view_status_attach(&vw->status, o->name);
            if (vw->hud.page == NULL)
                view_hud_attach(&vw->hud, o->name);
            view_hud_poll(&vw->hud);
            if (view_status_poll(&vw->status)) {
                vw->status_changed_ms = SDL_GetTicks64();
                fprintf(stderr, "openmmo-view: session %s%s%s\n",
                        openmmo_status_caption(vw->status.state,
                                               vw->status.flags),
                        vw->status.reason[0] ? ": " : "", vw->status.reason);
            }

            /* Ahead of the picture: the game holds the session open for a
             * few seconds after the link dies precisely so this can be read,
             * and a stale last frame under it says nothing. */
            if (view_msg_over(&msg, vw, scratch))
                viewer_present_message(vw, o, &msg);
            else if (vw->shm->seq >= 2) viewer_present(vw, o, frame);
            else {
                view_msg_lines(&msg, ATTACH_OK, vw, o, scratch);
                viewer_present_message(vw, o, &msg);
            }
        } else {
            viewer_present_message(vw, o, &msg);
        }

        /*
         * THE refresh just happened. A vsynced present returns when the panel has taken the
         * buffer, so this line is the display's own clock read at the one place in the program
         * that can see it.
         */
        if (vw->vsync)
            mmo_plat_vsync_mark();

        /*
         * --shot: one picture and out, whichever picture this is. A shot of the refusal screen
         * is as much a thing worth checking as a shot of a frame, and it is how the version
         * guard is asserted rather than described.
         */
        if (o->shot != NULL) {
            int settled = state != VIEW_LIVE ||
                          o->layout != OPENMMO_LAYOUT_SMART ||
                          vw->sec == (vw->shm->touch_wanted
                                        ? OPENMMO_VIEW_SEC_MAX
                                        : OPENMMO_VIEW_SEC_MIN);

            if (state != VIEW_WAITING && ++shot_frames >= 3 && settled) {
                if (view_write_shot(vw->ren, o->shot) != 0) rc = 1;
                break;
            }
        }

        /* Menu > Exit is the same door Esc opens: the session ends with
         * the window, and the launcher is where the player lands. */
        if (view_ui_bar_quit(&vw->ui_bar))
            running = 0;

        /*
         * The window closes on its own when the game ends. Nothing here takes the channel
         * down, so the name going away is "the session is over"; a seq that stops moving with
         * the name still there is a game that died hard, said in the title rather than guessed
         * at.
         */
        if (state == VIEW_LIVE && ++tick % 120 == 0) {
            if (mmo_shm_orphaned(&vw->page)) {
                fprintf(stderr, "openmmo-view: the game ended; closing\n");
                running = 0;
            }
            if (vw->shm->seq == last_seq && !stalled) {
                stalled = 1;
                viewer_title_for_focus(vw, 1);
            } else if (vw->shm->seq != last_seq && stalled) {
                stalled = 0;
                viewer_title_for_focus(vw, 0);
            }
            last_seq = vw->shm->seq;
        } else {
            tick++;
        }
        if (vw->win != NULL) {
            int has = (SDL_GetWindowFlags(vw->win) & SDL_WINDOW_INPUT_FOCUS) ? 1 : 0;

            if (!stalled && !has && SDL_GetTicks64() - t0 < 4000 && tick % 8 == 0)
                viewer_claim_input(vw);
            if (!stalled && has != focus_shown) {
                focus_shown = has;
                viewer_title_for_focus(vw, 0);
            }
        }

        /*
         * Nothing blocks when the renderer is not vsynced, and a fixed SDL_Delay(1) is not
         * pacing either, it is a 400 KB copy and a full present several hundred times a
         * second.
         */
        if (!vw->vsync) {
            Uint64 now = SDL_GetTicks64();

            if (next_tick == 0 || now > next_tick + 100) next_tick = now;
            if (next_tick > now) SDL_Delay((Uint32)(next_tick - now));
            next_tick += openmmo_view_pace_ms(tick);
        }
        view_frames_report();
        waited_ms += openmmo_view_pace_ms(tick);
    }

    if (adev != 0) {
        SDL_CloseAudioDevice(adev);
        if (audio.dropped || audio.starved)
            fprintf(stderr, "openmmo-view: audio dropped %llu, starved %llu "
                            "frames over the session\n",
                    (unsigned long long)audio.dropped,
                    (unsigned long long)audio.starved);
    }
    if (vw->torn != 0)
        fprintf(stderr, "openmmo-view: %llu of %llu frames could not be read "
                        "whole and were skipped\n",
                (unsigned long long)vw->torn,
                (unsigned long long)(vw->torn + vw->presented));
    if (in->units != 0)
        fprintf(stderr, "openmmo-view: %lu character(s) typed this session\n",
                in->units);
    if (getenv("OPENMMO_VIEW_PACE_REPORT") != NULL) {
        Uint64 dt = SDL_GetTicks64() - t0;

        fprintf(stderr,
                "openmmo-view: presented %llu in %llu ms%s\n",
                (unsigned long long)vw->presented,
                (unsigned long long)dt,
                vw->vsync ? " (vsync)" : " (clock)");
    }
    /* Closing the window is a person ending the session, and the game cannot
     * see the window. Saying so lets it finish tidily rather than be noticed
     * missing a second later by the pid check. */
    if (vw->shm != NULL) vw->shm->in_quit = 1;
    view_input_close_text(in);
    mmo_plat_vsync_unpublish();
    return rc;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

static void usage(const char *argv0)
{
    printf("usage: %s [CHANNEL] [options]\n"
           "\n"
           "  CHANNEL        the page the game's PC_VIEW=NAME names\n"
           "                 (default: " VIEW_DEFAULT_CHANNEL ")\n"
           "  --scale N      initial window scale, 1..8, or auto (the default:\n"
           "                 the largest 16:9 size that fits 90 percent of the display)\n"
           "  --size WxH     initial window in pixels; overrides --scale\n"
           "  --layout MODE  fill (the default: the world is the window, other\n"
           "                 screens a band of their own), smart (the game\n"
           "                 large, the touch screen beside it), stacked (the\n"
           "                 console's own) or wide (side by side); in every\n"
           "                 layout the second screen draws as nothing while\n"
           "                 the game would show the Poketch on it, until the\n"
           "                 bar's Poketch button or K asks for it\n"
           "  --render-scale N  draw from an N-times-larger texture, 1..4\n"
           "                 (default 2)\n"
           "  --filter NAME  nearest, linear (the default; with render-scale\n"
           "                 2+ it is sharp-bilinear), or scale2x (EPX edge\n"
           "                 smoothing, render scale 2-4)\n"
           "  --integer      scale by whole source pixels only\n"
           "  --stretch      fill the window, ignoring proportions\n"
           "  --fullscreen   start fullscreen (F11 / Alt+Enter toggles)\n"
           "  --audio-device NAME  play the sound on this device\n"
           "  --list-audio-devices  print this machine's devices, exit\n"
           "  --no-audio     open no audio device (also OPENMMO_VIEW_NO_AUDIO=1)\n"
           "  --offline      no server behind this window: no chat box, and\n"
           "                 no Community, PvP, Trade or Mail on the bar\n"
           "  --shots DIR    where F12 screenshots go (default: here)\n"
           "  --hud-dump     print each hud-page snapshot as it arrives\n"
           "  --shot PATH    write one composed frame as a binary PPM and\n"
           "                 exit; works under SDL_VIDEODRIVER=dummy, which\n"
           "                 is how a test reads this program's output\n"
           "  --wait MS      how long to wait for the game before the window\n"
           "                 says it is not running; 0 waits forever\n"
           "                 (default 5000)\n"
           "  --ui-screen S  where the window's own UI (the chat box, the\n"
           "                 HUD bar, the party strip) draws:\n"
           "                 both (the default: the whole window), top or\n"
           "                 bottom; F10 cycles them at runtime\n"
           " --theme DIR    draw the in-game UI with the official client's own\n"
           "                 art and face: DIR is an extracted client's\n"
           "                 data/themes/default (also OPENMMO_THEME, or a\n"
           "                 theme/ folder beside this program); without one\n"
           "                 the same UI draws from primitives\n"
           "  --bind PAD=KEY[,...]  rebind the keyboard; PAD is a, b, x, y,\n"
           "                 l, r, start, select, up, down, left or right and\n"
           "                 KEY is one of SDL's key names\n"
           "  --help         this message\n"
           "\n"
           "The game takes the arrow keys, X and Z (A and B), S and A (X and\n"
           "Y), Q and W (L and R), Enter (Start) and Backspace (Select); the\n"
           "left mouse button is the stylus on the touch screen. A gamepad\n"
           "works if one is plugged in, positionally, so confirm is where a\n"
           "thumb expects it rather than where the letters match.\n"
           "\n"
           "Esc closes the window, F11 or Alt+Enter toggles fullscreen, F12\n"
           "writes a screenshot. While the game has a text field open the\n"
           "keyboard types into it instead, real characters, from your own\n"
           "layout, and Esc leaves the field rather than closing the window.\n"
           "The chat box sits over the picture in every layout: Enter opens\n"
           "the line (Enter sends, Esc closes), the wheel scrolls it, and a\n"
           "click on it is the box's, not the pen's. F10 moves the window's\n"
           "UI between both screens, the top one and the bottom one.\n"
           "\n"
           "The in-game UI is the official client's. The HUD bar is bottom\n"
           "right, Bag, Trainer, Community, PvP, Pokedex, Egg Incubators,\n"
           "Trade, Mail, Gift Shop, Poketch, Menu, on the official client's own keys: B\n"
           "bag, C trainer, N pokedex, D the Menu popup, V friends, G team, I\n"
           "incubators, P trade, H the FAQ. K opens and shuts the Poketch,\n"
           "the game's own lower screen, which this window otherwise leaves\n"
           "out; it stays as you left it through a bag, a battle and a map\n"
           "change. The party strip is on the right edge; O folds it away. A\n"
           "frame is dragged by its title bar and closed with its button or\n"
           "with Esc.\n"
           "\n"
           "The window opens whether or not the game is running, and says why\n"
           "when there is no picture. A page whose version\n"
           "is not this build's is refused on screen rather than drawn, because\n"
           "every field in it has moved and the picture would be skewed.\n",
           argv0);
}

int main(int argc, char **argv)
{
    struct view_opts o = { 0 };
    struct view_input in;
    struct viewer vw;
    int i, want_list = 0, rc;

    mmo_plat_crash_install("view");

    o.name = VIEW_DEFAULT_CHANNEL;
    o.scale = OPENMMO_VIEW_SCALE_AUTO;
    o.layout = OPENMMO_LAYOUT_FILL;
    o.rs = 2;
    o.filter = OPENMMO_FILTER_LINEAR;
    o.wait_ms = 5000;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (a[0] != '-') { o.name = a; continue; }
        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(a, "--list-audio-devices") == 0) {
            want_list = 1;
        } else if (strcmp(a, "--no-audio") == 0) {
            o.no_audio = 1;
        } else if (strcmp(a, "--offline") == 0) {
            o.offline = 1;
        } else if (strcmp(a, "--integer") == 0) {
            o.integer = 1;
        } else if (strcmp(a, "--stretch") == 0) {
            o.aspect = OPENMMO_ASPECT_STRETCH;
        } else if (strcmp(a, "--fullscreen") == 0) {
            o.fullscreen = 1;
        } else if (strcmp(a, "--scale") == 0 && i + 1 < argc) {
            const char *v = argv[++i];

            if (strcmp(v, "auto") == 0) {
                o.scale = OPENMMO_VIEW_SCALE_AUTO;
            } else {
                o.scale = atoi(v);
                if (o.scale < 1 || o.scale > 8) {
                    fprintf(stderr, "openmmo-view: --scale wants 1..8 or auto\n");
                    return 2;
                }
            }
        } else if (strcmp(a, "--size") == 0 && i + 1 < argc) {
            int w = 0, h = 0;

            if (sscanf(argv[++i], "%dx%d", &w, &h) != 2 || w < 1 || h < 1) {
                fprintf(stderr, "openmmo-view: --size wants WxH\n");
                return 2;
            }
            o.win_w = w;
            o.win_h = h;
        } else if (strcmp(a, "--render-scale") == 0 && i + 1 < argc) {
            o.rs = atoi(argv[++i]);
            if (o.rs < 1 || o.rs > 4) {
                fprintf(stderr, "openmmo-view: --render-scale wants 1..4\n");
                return 2;
            }
        } else if (strcmp(a, "--layout") == 0 && i + 1 < argc) {
            const char *m = argv[++i];

            if (strcmp(m, "smart") == 0)        o.layout = OPENMMO_LAYOUT_SMART;
            else if (strcmp(m, "stacked") == 0) o.layout = OPENMMO_LAYOUT_STACKED;
            else if (strcmp(m, "wide") == 0)    o.layout = OPENMMO_LAYOUT_WIDE;
            else if (strcmp(m, "fill") == 0)    o.layout = OPENMMO_LAYOUT_FILL;
            else {
                fprintf(stderr,
                        "openmmo-view: --layout wants fill, smart, stacked or wide\n");
                return 2;
            }
        } else if (strcmp(a, "--filter") == 0 && i + 1 < argc) {
            const char *m = argv[++i];

            if (strcmp(m, "nearest") == 0)      o.filter = OPENMMO_FILTER_NEAREST;
            else if (strcmp(m, "linear") == 0)  o.filter = OPENMMO_FILTER_LINEAR;
            else if (strcmp(m, "scale2x") == 0) o.filter = OPENMMO_FILTER_SCALE2X;
            else {
                fprintf(stderr, "openmmo-view: --filter wants nearest, linear "
                                "or scale2x\n");
                return 2;
            }
        } else if (strcmp(a, "--audio-device") == 0 && i + 1 < argc) {
            o.audio_device = argv[++i];
        } else if (strcmp(a, "--bind") == 0 && i + 1 < argc) {
            o.bind = argv[++i];
        } else if (strcmp(a, "--shots") == 0 && i + 1 < argc) {
            o.shots = argv[++i];
        } else if (strcmp(a, "--shot") == 0 && i + 1 < argc) {
            o.shot = argv[++i];
        } else if (strcmp(a, "--wait") == 0 && i + 1 < argc) {
            o.wait_ms = atoi(argv[++i]);
        } else if (strcmp(a, "--ui-screen") == 0 && i + 1 < argc) {
            const char *m = argv[++i];

            if (strcmp(m, "both") == 0)        o.ui_screen = VIEW_UI_CANVAS_BOTH;
            else if (strcmp(m, "top") == 0)    o.ui_screen = VIEW_UI_CANVAS_TOP;
            else if (strcmp(m, "bottom") == 0) o.ui_screen = VIEW_UI_CANVAS_BOTTOM;
            else {
                fprintf(stderr,
                        "openmmo-view: --ui-screen wants both, top or bottom\n");
                return 2;
            }
        } else if (strcmp(a, "--hud-dump") == 0) {
            o.hud_dump = 1;
        } else if (strcmp(a, "--theme") == 0 && i + 1 < argc) {
            o.theme = argv[++i];
        } else {
            fprintf(stderr, "openmmo-view: unknown option %s (try --help)\n", a);
            return 2;
        }
    }
    if (o.filter == OPENMMO_FILTER_SCALE2X && o.rs < 2) {
        fprintf(stderr, "openmmo-view: scale2x wants --render-scale 2, 3 or 4\n");
        return 2;
    }
    if (getenv("OPENMMO_VIEW_NO_AUDIO") != NULL) o.no_audio = 1;

    /* The gamepad subsystem is asked for here rather than opened on demand,
     * because a pad plugged in before the window opened arrives as a device
     * event only if something is listening. Not fatal, a machine with no
     * joystick support still has a keyboard. */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "openmmo-view: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
        fprintf(stderr, "openmmo-view: no gamepad support here (%s)\n",
                SDL_GetError());
    view_input_init(&in);
    view_input_attach_present(&in);
    /* After SDL is up: --bind's key names are SDL's own. */
    if (o.bind != NULL && view_input_bind(&in, o.bind) != 0) {
        SDL_Quit();
        return 2;
    }
    if (want_list) {
        view_list_audio_devices();
        SDL_Quit();
        return 0;
    }

    memset(&vw, 0, sizeof vw);
    vw.page.fd = -1;
    view_status_init(&vw.status);
    view_hud_init(&vw.hud);
    view_ui_gpu_init(&vw.ui_gpu);
    vw.hud.dump = o.hud_dump;
    /* The UI layer and its elements. Order is draw order, back to front;
     * events walk it the other way. The design notes is the guide to adding one. */
    view_ui_layer_init(&vw.ui);
    vw.ui.canvas_mode = o.ui_screen;
    view_ui_offline_set(o.offline);
    view_ui_chat_init(&vw.ui_chat, &vw.hud);
    view_ui_party_init(&vw.ui_party, &vw.hud);
    view_ui_wins_init(&vw.ui_wins, &vw.hud);
    view_ui_bar_init(&vw.ui_bar, &vw.hud, &vw.ui_wins);
    {
        struct view_ui_element party = view_ui_party_element(&vw.ui_party);
        struct view_ui_element chat = view_ui_chat_element(&vw.ui_chat);
        struct view_ui_element wins = view_ui_wins_element(&vw.ui_wins);
        struct view_ui_element bar = view_ui_bar_element(&vw.ui_bar);

        /* Back to front, and so events front to back: a frame dragged over
         * the chat box is above it, and the bar and its popup are above
         * everything, which is where a control strip belongs. */
        view_ui_layer_add(&vw.ui, &party);
        /* Offline the box is never built into the layer at all rather than
         * drawn empty: Enter is then the game's, and the bar below gets the
         * whole floor instead of leaving room for a panel nobody can type
         * into. */
        if (!o.offline)
            view_ui_layer_add(&vw.ui, &chat);
        view_ui_layer_add(&vw.ui, &wins);
        view_ui_layer_add(&vw.ui, &bar);
    }
    if (viewer_open_video(&vw, &o) != 0) {
        viewer_close_video(&vw);
        SDL_Quit();
        return 1;
    }
    /* The art for the UI layer. */
    {
        char fallback[1024];
        const char *dir = o.theme;

        if (dir == NULL || dir[0] == '\0')
            dir = getenv("OPENMMO_THEME");
        if (dir == NULL || dir[0] == '\0') {
            char exe[512];

            fallback[0] = '\0';
            if (mmo_plat_exe_path(exe, sizeof exe) == 0) {
                char *slash = (char *)mmo_plat_last_sep(exe);
                char probe[1024];
                FILE *pf;

                if (slash != NULL) {
                    *slash = '\0';
                    snprintf(fallback, sizeof fallback, "%s%stheme", exe,
                             mmo_plat_sep());
                    snprintf(probe, sizeof probe, "%s%s%s", fallback,
                             mmo_plat_sep(), view_ui_sheet_file(0));
                    pf = fopen(probe, "rb");
                    if (pf == NULL)
                        fallback[0] = '\0';
                    else
                        fclose(pf);
                }
            }
            dir = fallback;
        }
        view_ui_theme_up(&vw.ui_gpu, vw.ren, dir);
    }
    rc = viewer_run(&vw, &o, &in);
    viewer_close_video(&vw);
    SDL_Quit();
    return rc;
}
