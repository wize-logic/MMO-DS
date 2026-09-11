/* The shared-memory frame channel, as this repo holds it. */

#ifndef OPENMMO_VIEW_CHANNEL_H
#define OPENMMO_VIEW_CHANNEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OPENMMO_VIEW_MAGIC   0x50504C56u /* 'PPLV' */

/*
 * The pinned VERSION. A page whose version is not this one is not a page we can read: the
 * field offsets below moved, so every pixel would land somewhere else.
 */
#define OPENMMO_VIEW_VERSION 1007u

/* One DS screen, natively. */
#define OPENMMO_VIEW_W 256
#define OPENMMO_VIEW_H 192

/*
 * Wide rendering: the most columns a screen may carry when the publisher widens the 3D field
 * of view (684 is 32:9 against 192 rows, wide enough for an adaptive aspect to follow any real
 * window; the engine's own 342 stops at 16:9).
 */
#define OPENMMO_VIEW_WIDE_MAX 684u

/*
 * Internal resolution: the 3D layer may be rasterized at this many times the DS's pixel count
 * on each axis, and `width`/`height` say what is in the page.
 */
#define OPENMMO_VIEW_HD_MAX 4u

/* One screen's pixel array, in words: the widest frame at the highest internal
 * resolution. Readers size their scratch from this. */
#define OPENMMO_VIEW_FRAME_WORDS                                               \
    (OPENMMO_VIEW_WIDE_MAX * OPENMMO_VIEW_HD_MAX * OPENMMO_VIEW_H              \
     * OPENMMO_VIEW_HD_MAX)

/*
 * The audio ring. The publisher writes stereo frames at the head and advances it; a reader
 * owns its tail in its own memory, so a viewer paused in a debugger costs the publisher
 * nothing and one that falls a full ring behind skips itself forward.
 */
#define OPENMMO_VIEW_AUDIO_FRAMES 32768u
/* The guest's own rate, 33,513,982 / 1024. The rounder 32,768 plays 0.1% fast. */
#define OPENMMO_VIEW_AUDIO_RATE 32728u

/* Button bits, positive, the console SDK's own values. */
#define OPENMMO_VIEW_KEY_A      0x0001u
#define OPENMMO_VIEW_KEY_B      0x0002u
#define OPENMMO_VIEW_KEY_SELECT 0x0004u
#define OPENMMO_VIEW_KEY_START  0x0008u
#define OPENMMO_VIEW_KEY_RIGHT  0x0010u
#define OPENMMO_VIEW_KEY_LEFT   0x0020u
#define OPENMMO_VIEW_KEY_UP     0x0040u
#define OPENMMO_VIEW_KEY_DOWN   0x0080u
#define OPENMMO_VIEW_KEY_R      0x0100u
#define OPENMMO_VIEW_KEY_L      0x0200u
#define OPENMMO_VIEW_KEY_X      0x0400u
#define OPENMMO_VIEW_KEY_Y      0x0800u

struct openmmo_view_shm {
    uint32_t magic;
    uint32_t version;
    /* The process publishing here, so a second game process that finds this
     * name taken can say whose it is. One page has exactly one writer. */
    volatile uint32_t publisher;

    /* Frame channel: the game writes, the viewer reads, seqlock as above. */
    volatile uint32_t seq;
    volatile uint32_t frame_lo;
    volatile uint32_t frame_hi;
    volatile uint32_t upper_engine; /* 0 = engine A on top, 1 = engine B */
    volatile uint32_t width;        /* columns per screen this frame */
    volatile uint32_t height;       /* rows per screen this frame */
    /* 1 while the game has touch auto-sampling running, the game saying it
     * wants the pen, from inside the process, which nothing outside one can
     * see. Advisory: only the layout reads it, so ignoring it just means a
     * viewer with a fixed layout. */
    volatile uint32_t touch_wanted;
    uint32_t pix[2][OPENMMO_VIEW_FRAME_WORDS]; /* 0x00RRGGBB, rows packed at
                                                * `width`, [0]=A, [1]=B */

    /* Input channel: the viewer writes, the game samples at each frame
     * boundary. `in_seq` bumps on every change, and 0 means no viewer has ever
     * spoken, which is what keeps a scripted run scripted with no window up. */
    volatile uint32_t in_seq;
    volatile uint32_t in_keys;    /* OPENMMO_VIEW_KEY_* mask, positive */
    volatile uint32_t in_touch;   /* 1 = pen down */
    volatile uint32_t in_touch_x; /* 0..255, lower-screen pixels */
    volatile uint32_t in_touch_y; /* 0..191 */

    /* The window's shape, as the ratio of the area one screen is drawn into.
     * A ratio and not a width because the policy, what is too wide, how it
     * rounds, belongs to the side that renders. Zero in either word means no
     * viewer has said, which keeps a windowless run native. */
    volatile uint32_t in_aspect_n;
    volatile uint32_t in_aspect_d;

    /*
     * Who is watching, and whether they are still there. The game has no window of its own, so
     * closing ours ends the session; a game process that kept running would be one nobody can
     * see and nobody thinks is there.
     */
    volatile uint32_t in_viewer_pid;
    volatile uint32_t in_quit;

    /*
     * Fast-forward, held. Nothing in this client writes it any more: the world is the server's
     * and a client that ran its own frames faster only drew a session going at the usual
     * speed.
     */
    volatile uint32_t in_turbo;

    /* The ring above. Both fields are the publisher's; a reader's tail is its own. */
    volatile uint32_t audio_rate;
    volatile uint32_t audio_head;                  /* stereo frames written */
    uint32_t audio[OPENMMO_VIEW_AUDIO_FRAMES];     /* left in the low half  */
};

/*
 * Take up to `frames` stereo frames out of the ring as interleaved 16-bit left/right,
 * returning how many it got; the caller pads with silence, because an audio callback must
 * always fill its buffer.
 */
static inline unsigned openmmo_view_audio_read(const struct openmmo_view_shm *v,
    uint32_t *tailp, int16_t *dst, unsigned frames, uint32_t *dropped)
{
    uint32_t head, tail, avail;
    unsigned n = 0;

    if (dropped) *dropped = 0;
    if (v == 0 || dst == 0 || tailp == 0) return 0;
    if (v->magic != OPENMMO_VIEW_MAGIC || v->version != OPENMMO_VIEW_VERSION)
        return 0;

    head = __atomic_load_n(&v->audio_head, __ATOMIC_ACQUIRE);
    tail = *tailp;

    /* Unsigned subtraction, so this stays right across the 2^32 wrap. */
    avail = head - tail;
    if (avail > OPENMMO_VIEW_AUDIO_FRAMES) {
        uint32_t lost = avail - OPENMMO_VIEW_AUDIO_FRAMES;
        if (dropped) *dropped = lost;
        tail += lost;
        avail = OPENMMO_VIEW_AUDIO_FRAMES;
    }

    while (n < frames && avail > 0) {
        uint32_t s = v->audio[tail % OPENMMO_VIEW_AUDIO_FRAMES];
        dst[n * 2 + 0] = (int16_t)(uint16_t)(s & 0xFFFFu);
        dst[n * 2 + 1] = (int16_t)(uint16_t)(s >> 16);
        tail++;
        avail--;
        n++;
    }

    *tailp = tail;
    return n;
}

/*
 * The width a window of n x d pixels asks for: the columns that fill that shape at 192 rows,
 * never narrower than the DS and never wider than the cap, and always even so the two margins
 * come out the same size.
 */
static inline int openmmo_view_aspect_width(unsigned n, unsigned d)
{
    unsigned w;

    if (n == 0 || d == 0) return OPENMMO_VIEW_W;
    w = (OPENMMO_VIEW_H * n + d / 2) / d;
    if (w < OPENMMO_VIEW_W) w = OPENMMO_VIEW_W;
    if (w > OPENMMO_VIEW_WIDE_MAX) w = OPENMMO_VIEW_WIDE_MAX;
    return (int)(w & ~1u);
}

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_VIEW_CHANNEL_H */
