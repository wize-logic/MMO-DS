/* Where the session stands, on a page the shell can read. */

#ifndef OPENMMO_STATUS_CHANNEL_H
#define OPENMMO_STATUS_CHANNEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OPENMMO_STATUS_MAGIC   0x4F4D5354u /* 'OMST' */

/* Ours to bump, like the text page's and unlike the frame page's. A reader that
 * finds another number says so and draws nothing, rather than reading a
 * sentence out of what is now the middle of a number. */
#define OPENMMO_STATUS_VERSION 1u

/* The page's name, given the frame channel's. */
#define OPENMMO_STATUS_SUFFIX ".status"

/* Long enough for the client's own failure messages, which are the longest
 * thing that lands here (`openmmo_event.message` is 128). */
#define OPENMMO_STATUS_REASON 128

/*
 * The values of openmmo_status (client.h), repeated here so the window and the launcher need
 * not compile against the client's -m32 headers to read a page. Held to the original by
 * tests/status_channel_test.c.
 */
#define OPENMMO_ST_DISCONNECTED    0u
#define OPENMMO_ST_CONNECTING      1u
#define OPENMMO_ST_HANDSHAKING     2u
#define OPENMMO_ST_AUTHENTICATING  3u
#define OPENMMO_ST_AUTHED          4u
#define OPENMMO_ST_REQUESTING_GAME 5u
#define OPENMMO_ST_JOINING_GAME    6u
#define OPENMMO_ST_IN_GAME         7u
#define OPENMMO_ST_FAILED          8u
#define OPENMMO_ST_N               9u

/*
 * A session was asked for at all. Without it the game is the single-player port it has always
 * been and DISCONNECTED means "there is no server in this run", which is not a fault and must
 * not be reported as one.
 */
#define OPENMMO_STATUS_F_SESSION 0x1u
/* The session got as far as being authenticated at least once. It is what
 * separates "the server refused you" from "the server hung up on you", which
 * are the same state and very different sentences. */
#define OPENMMO_STATUS_F_WAS_LIVE 0x2u
/*
 * The player is in the Underground, which is the one place this game hands the two screens to
 * the player the other way round: the console's own display select is swapped, so the map
 * takes the upper LCD and the WORLD is on the touch screen, because down there the world is
 * what you tap.
 */
#define OPENMMO_STATUS_F_UNDERGROUND 0x4u

/*
 * A broken link is being redialed: the game is between sessions on purpose, and every state
 * the campaign flaps through, DISCONNECTED while it waits out a backoff, CONNECTING while an
 * attempt runs, is one activity as far as the player is concerned.
 */
#define OPENMMO_STATUS_F_REJOIN      0x8u

struct openmmo_status_shm {
    uint32_t magic;
    uint32_t version;
    /* The game holding this page, so a reader can tell a live session from a
     * page left behind by a process that died. */
    volatile uint32_t writer;
    /* Even and unchanged across a read means what was copied is one state. */
    volatile uint32_t seq;
    volatile uint32_t state;   /* OPENMMO_ST_* */
    volatile uint32_t flags;   /* OPENMMO_STATUS_F_* */
    /* Bumped once per state change, ever. A reader compares it to its own copy
     * to notice a transition it slept through, two failures in a row carry the
     * same state and the same words and are still two events. */
    volatile uint32_t gen;
    uint32_t reserved;
    volatile char reason[OPENMMO_STATUS_REASON];
};

/* What a player is told. Not the enum's own spelling: AUTHENTICATING and
 * REQUESTING_GAME are one thing as far as anyone waiting is concerned, and
 * FAILED is not a word anybody says out loud. */
static inline const char *openmmo_status_caption(uint32_t state, uint32_t flags)
{
    /* One word for the whole campaign, whatever state an attempt is in this
     * frame, the flap between CONNECTING and DISCONNECTED is mechanism, not
     * news. IN_GAME is the campaign having already succeeded; the flag clears
     * a beat later and must not caption the world. */
    if ((flags & OPENMMO_STATUS_F_REJOIN) && state != OPENMMO_ST_IN_GAME)
        return "RECONNECTING";
    switch (state) {
    case OPENMMO_ST_DISCONNECTED:
        if (!(flags & OPENMMO_STATUS_F_SESSION)) return "NOT CONNECTED";
        /* The distinction FAILED makes below, for the other way a live
         * session ends: a link that broke under one is not the flat word
         * for a session that is merely not up. */
        return (flags & OPENMMO_STATUS_F_WAS_LIVE) ? "CONNECTION LOST"
                                                   : "DISCONNECTED";
    case OPENMMO_ST_CONNECTING:      return "CONNECTING";
    case OPENMMO_ST_HANDSHAKING:     return "CONNECTING";
    case OPENMMO_ST_AUTHENTICATING:  return "SIGNING IN";
    case OPENMMO_ST_AUTHED:          return "SIGNED IN";
    case OPENMMO_ST_REQUESTING_GAME: return "JOINING";
    case OPENMMO_ST_JOINING_GAME:    return "JOINING";
    case OPENMMO_ST_IN_GAME:         return "ONLINE";
    case OPENMMO_ST_FAILED:
        /* Never authenticated: the server declined, and the reason beneath is
         * the server's own words. Once live it is a link that broke, which is
         * not the same accusation. */
        return (flags & OPENMMO_STATUS_F_WAS_LIVE) ? "CONNECTION LOST"
                                                   : "SERVER SAID NO";
    default:                         return "UNKNOWN";
    }
}

/* 0x00RRGGBB, the HUD's own scheme: grey idle, amber in flight, cyan signed in,
 * green in world, red over. Here rather than in each reader so the window and
 * the launcher cannot drift into disagreeing about what red means. */
static inline uint32_t openmmo_status_colour(uint32_t state)
{
    switch (state) {
    case OPENMMO_ST_DISCONNECTED: return 0x808080u;
    case OPENMMO_ST_AUTHED:       return 0x40D0FFu;
    case OPENMMO_ST_IN_GAME:      return 0x40FF60u;
    case OPENMMO_ST_FAILED:       return 0xFF5050u;
    default:                      return 0xFFD040u;
    }
}

/*
 * Whether the session is over, the one question the launcher acts on, so it is answered once,
 * here. FAILED is terminal; so is DISCONNECTED after a session that was live, which is the
 * server hanging up.
 */
static inline int openmmo_status_over(uint32_t state, uint32_t flags)
{
    if (!(flags & OPENMMO_STATUS_F_SESSION)) return 0;
    /*
     * A rejoin campaign flaps through FAILED and DISCONNECTED between attempts, and each is
     * the campaign working, not the session ending.
     */
    if (flags & OPENMMO_STATUS_F_REJOIN) return 0;
    if (state == OPENMMO_ST_FAILED) return 1;
    return state == OPENMMO_ST_DISCONNECTED &&
           (flags & OPENMMO_STATUS_F_WAS_LIVE) != 0;
}

/*
 * Publish one state. The seqlock is closed around the whole record because the reason is a
 * sentence; `gen` counts publishes so a reader can see a repeat.
 */
static inline void openmmo_status_publish(struct openmmo_status_shm *s,
                                          uint32_t state, uint32_t flags,
                                          const char *reason)
{
    unsigned i;

    if (s == 0 || s->magic != OPENMMO_STATUS_MAGIC ||
        s->version != OPENMMO_STATUS_VERSION) return;

    __atomic_store_n(&s->seq, s->seq + 1u, __ATOMIC_RELEASE);  /* odd: writing */
    s->state = state;
    s->flags = flags;
    for (i = 0; i + 1u < OPENMMO_STATUS_REASON; i++) {
        char c = reason != 0 ? reason[i] : '\0';

        s->reason[i] = c;
        if (c == '\0') break;
    }
    s->reason[OPENMMO_STATUS_REASON - 1u] = '\0';
    s->gen = s->gen + 1u;
    __atomic_store_n(&s->seq, s->seq + 1u, __ATOMIC_RELEASE);  /* even: settled */
}

/*
 * Copy one consistent state out, or return 0 having written nothing the caller may act on.
 * `reason` must hold OPENMMO_STATUS_REASON bytes and is always NUL-terminated on success.
 */
static inline int openmmo_status_read(const struct openmmo_status_shm *s,
                                      uint32_t *state, uint32_t *flags,
                                      uint32_t *gen, char *reason)
{
    unsigned t, i;

    if (s == 0) return 0;
    if (s->magic != OPENMMO_STATUS_MAGIC ||
        s->version != OPENMMO_STATUS_VERSION) return 0;

    /* Plain volatile reads with a fence either side, which is the frame page's
     * own reader discipline, and unlike __atomic_load_n it does not object to
     * being handed a const pointer, which is what a reader that never writes
     * should be holding. */
    for (t = 0; t < 16u; t++) {
        uint32_t s0 = s->seq, s1;
        uint32_t st, fl, g;

        if (s0 & 1u) continue;
        __sync_synchronize();
        st = s->state;
        fl = s->flags;
        g  = s->gen;
        if (reason != 0)
            for (i = 0; i < OPENMMO_STATUS_REASON; i++) reason[i] = s->reason[i];
        __sync_synchronize();
        s1 = s->seq;
        if (s0 != s1) continue;

        if (state != 0) *state = st;
        if (flags != 0) *flags = fl;
        if (gen   != 0) *gen   = g;
        if (reason != 0) reason[OPENMMO_STATUS_REASON - 1u] = '\0';
        return 1;
    }
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_STATUS_CHANNEL_H */
