/* Typed characters, on a page of our own beside the frame one. */

#ifndef OPENMMO_TEXT_CHANNEL_H
#define OPENMMO_TEXT_CHANNEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OPENMMO_TEXT_MAGIC   0x4F4D5458u /* 'OMTX' */

/*
 * Ours to bump, unlike the frame channel's. A reader that finds another number refuses the
 * page rather than reading the ring at the wrong offset, the same rule, for the same reason,
 * on a page with no second implementation to check against.
 */
#define OPENMMO_TEXT_VERSION 1u

/* The page's name, given the frame channel's. One window, one game, one pair. */
#define OPENMMO_TEXT_SUFFIX ".text"

/*
 * Ring capacity, in events. A power of two so the modulo is a mask, and far more than a person
 * can type between two frames of a client that drains it every frame: 256 events is over four
 * seconds of a fast typist's hands.
 */
#define OPENMMO_TEXT_SLOTS 256u

/*
 * What an event is. The kind is the high byte and the payload the low half, so one word is one
 * event and the ring needs no second array.
 */
#define OPENMMO_TEXT_UNIT      1u /* insert one UTF-16 code unit (low half) */
#define OPENMMO_TEXT_BACKSPACE 2u /* delete before the caret */
#define OPENMMO_TEXT_DELETE    3u /* delete after it */
#define OPENMMO_TEXT_LEFT      4u
#define OPENMMO_TEXT_RIGHT     5u
#define OPENMMO_TEXT_HOME      6u
#define OPENMMO_TEXT_END       7u
#define OPENMMO_TEXT_COMMIT    8u /* Enter: the line is finished */
#define OPENMMO_TEXT_CANCEL    9u /* Escape: the field is abandoned */

struct openmmo_text_shm {
    uint32_t magic;
    uint32_t version;
    /* The window holding this page, so a client can tell a live typist from a
     * page left behind by one that crashed. */
    volatile uint32_t writer;
    /* The client's word: nonzero while a text field is open. Zero, including
     * the whole of a session with no chat box on screen, means the window
     * sends buttons and nothing arrives here at all. */
    volatile uint32_t want;
    /* Events written since the page was created, ever; a reader's tail is its
     * own. Unsigned, so the arithmetic stays right across the 2^32 wrap. */
    volatile uint32_t head;
    uint32_t reserved;
    uint32_t ev[OPENMMO_TEXT_SLOTS];
};

static inline uint32_t openmmo_text_kind(uint32_t e) { return e >> 24; }
static inline uint32_t openmmo_text_unit(uint32_t e) { return e & 0xFFFFu; }
static inline uint32_t openmmo_text_ev(uint32_t kind, uint32_t unit)
{
    return (kind << 24) | (unit & 0xFFFFu);
}

/*
 * Append one event. The slot is written before the head that publishes it, so a reader can
 * never be handed an index whose contents have not landed yet; the fence is what makes that
 * ordering true on a machine that reorders stores.
 */
static inline void openmmo_text_push(struct openmmo_text_shm *t, uint32_t kind,
                                     uint32_t unit)
{
    uint32_t head;

    if (t == 0 || t->magic != OPENMMO_TEXT_MAGIC ||
        t->version != OPENMMO_TEXT_VERSION) return;
    head = t->head;
    t->ev[head % OPENMMO_TEXT_SLOTS] = openmmo_text_ev(kind, unit);
    __atomic_store_n(&t->head, head + 1u, __ATOMIC_RELEASE);
}

/* Take up to `max` events out of the ring, returning how many were read. */
static inline unsigned openmmo_text_read(const struct openmmo_text_shm *t,
                                         uint32_t *tailp, uint32_t *dst,
                                         unsigned max, uint32_t *dropped)
{
    uint32_t head, tail, avail;
    unsigned n = 0;

    if (dropped) *dropped = 0;
    if (t == 0 || dst == 0 || tailp == 0) return 0;
    if (t->magic != OPENMMO_TEXT_MAGIC || t->version != OPENMMO_TEXT_VERSION)
        return 0;

    head = __atomic_load_n(&t->head, __ATOMIC_ACQUIRE);
    tail = *tailp;
    avail = head - tail;
    if (avail > OPENMMO_TEXT_SLOTS) {
        uint32_t lost = avail - OPENMMO_TEXT_SLOTS;

        if (dropped) *dropped = lost;
        tail += lost;
        avail = OPENMMO_TEXT_SLOTS;
    }
    while (n < max && avail > 0) {
        dst[n++] = t->ev[tail++ % OPENMMO_TEXT_SLOTS];
        avail--;
    }
    *tailp = tail;
    return n;
}

/*
 * One UTF-8 sequence to UTF-16, which is the conversion between what a windowing system hands
 * over and what this page carries.
 */
static inline unsigned openmmo_text_utf8_to_utf16(const unsigned char *s,
                                                  unsigned len, uint16_t out[2],
                                                  unsigned *used)
{
    uint32_t cp;
    unsigned need, i;

    if (used) *used = 1;
    if (s == 0 || len == 0 || out == 0) return 0;

    if (s[0] < 0x80u)              { cp = s[0];          need = 0; }
    else if ((s[0] & 0xE0u) == 0xC0u) { cp = s[0] & 0x1Fu;  need = 1; }
    else if ((s[0] & 0xF0u) == 0xE0u) { cp = s[0] & 0x0Fu;  need = 2; }
    else if ((s[0] & 0xF8u) == 0xF0u) { cp = s[0] & 0x07u;  need = 3; }
    else return 0;                 /* a continuation byte, or 5-byte nonsense */

    if (need + 1u > len) return 0;
    for (i = 1; i <= need; i++) {
        if ((s[i] & 0xC0u) != 0x80u) return 0;
        cp = (cp << 6) | (s[i] & 0x3Fu);
    }
    if (used) *used = need + 1u;

    /* Overlong forms encode a short code point in a long sequence; they are how
     * a filter that checks the bytes gets past a check the decoder then undoes,
     * so they are refused rather than normalized. */
    if ((need == 1 && cp < 0x80u) || (need == 2 && cp < 0x800u) ||
        (need == 3 && cp < 0x10000u)) return 0;
    if (cp > 0x10FFFFu) return 0;
    if (cp >= 0xD800u && cp <= 0xDFFFu) return 0;

    if (cp < 0x10000u) { out[0] = (uint16_t)cp; return 1; }
    cp -= 0x10000u;
    out[0] = (uint16_t)(0xD800u + (cp >> 10));
    out[1] = (uint16_t)(0xDC00u + (cp & 0x3FFu));
    return 2;
}

/*
 * One code point to UTF-8, the client's own string encoding: what the viewer measures and
 * draws, what the charcode bridge takes, and what the wire codec transcodes to and from.
 * Writes 1..4 bytes, no terminator, and returns how many.
 */
static inline unsigned openmmo_text_cp_to_utf8(uint32_t cp, char out[4])
{
    if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) cp = 0xFFFDu;

    if (cp < 0x80u) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800u) {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp < 0x10000u) {
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        return 3;
    }
    out[0] = (char)(0xF0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (cp & 0x3Fu));
    return 4;
}

/*
 * One UTF-16 sequence to UTF-8, which is the conversion between what this page carries and
 * what the rest of the client holds.
 */
static inline unsigned openmmo_text_utf16_to_utf8(const uint16_t *u, unsigned len,
                                                  char out[4], unsigned *used)
{
    uint32_t cp;

    if (used) *used = 1;
    if (u == 0 || len == 0 || out == 0) return 0;

    cp = u[0];
    if (cp >= 0xD800u && cp <= 0xDBFFu) {
        if (len >= 2 && u[1] >= 0xDC00u && u[1] <= 0xDFFFu) {
            cp = 0x10000u + ((cp - 0xD800u) << 10) + (u[1] - 0xDC00u);
            if (used) *used = 2;
        } else {
            cp = 0xFFFDu;
        }
    } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
        cp = 0xFFFDu;
    }
    return openmmo_text_cp_to_utf8(cp, out);
}

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_TEXT_CHANNEL_H */
