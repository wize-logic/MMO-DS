/* The page typed characters travel on. */

#include <stdio.h>
#include <string.h>

#include "text_channel.h"

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

/* A page as the window creates one. */
static void page_init(struct openmmo_text_shm *t)
{
    memset(t, 0, sizeof *t);
    t->magic = OPENMMO_TEXT_MAGIC;
    t->version = OPENMMO_TEXT_VERSION;
}

static void test_layout(void)
{
    struct openmmo_text_shm t;

    CHECK(sizeof t == 6u * 4u + OPENMMO_TEXT_SLOTS * 4u,
          "the page is words all the way down");
    CHECK((OPENMMO_TEXT_SLOTS & (OPENMMO_TEXT_SLOTS - 1u)) == 0,
          "the ring is a power of two");
    CHECK(sizeof t.ev[0] == 4 && sizeof t.head == 4 && sizeof t.want == 4,
          "every field is 32 bits, so both ABIs see one layout");

    page_init(&t);
    CHECK(openmmo_text_kind(openmmo_text_ev(OPENMMO_TEXT_UNIT, 0xFFFFu))
              == OPENMMO_TEXT_UNIT &&
          openmmo_text_unit(openmmo_text_ev(OPENMMO_TEXT_UNIT, 0xFFFFu))
              == 0xFFFFu,
          "an event carries a kind and a code unit in one word");
}

static void test_ring(void)
{
    struct openmmo_text_shm t;
    uint32_t out[OPENMMO_TEXT_SLOTS + 8], tail = 0, dropped = 0;
    unsigned i, n;

    page_init(&t);
    n = openmmo_text_read(&t, &tail, out, 8, &dropped);
    CHECK(n == 0 && dropped == 0, "an untouched page reads as nothing typed");

    openmmo_text_push(&t, OPENMMO_TEXT_UNIT, 'h');
    openmmo_text_push(&t, OPENMMO_TEXT_UNIT, 'i');
    openmmo_text_push(&t, OPENMMO_TEXT_COMMIT, 0);
    n = openmmo_text_read(&t, &tail, out, 8, &dropped);
    CHECK(n == 3 && openmmo_text_unit(out[0]) == 'h' &&
          openmmo_text_unit(out[1]) == 'i' &&
          openmmo_text_kind(out[2]) == OPENMMO_TEXT_COMMIT,
          "what was typed comes back in the order it was typed");
    n = openmmo_text_read(&t, &tail, out, 8, &dropped);
    CHECK(n == 0, "a drained ring stays drained");

    /* A reader that stops draining loses the oldest events and is told how
     * many: a chat line silently missing its first half is worse than one that
     * says so. */
    page_init(&t);
    tail = 0;
    for (i = 0; i < OPENMMO_TEXT_SLOTS + 5u; i++)
        openmmo_text_push(&t, OPENMMO_TEXT_UNIT, (uint32_t)('a' + i % 26));
    n = openmmo_text_read(&t, &tail, out, OPENMMO_TEXT_SLOTS + 8u, &dropped);
    CHECK(dropped == 5u && n == OPENMMO_TEXT_SLOTS,
          "an overrun ring drops the oldest five and says so");
    CHECK(openmmo_text_unit(out[0]) == (uint32_t)('a' + 5 % 26),
          "and what survives starts where the loss ended");

    /* The counters are unsigned and wrap; a session long enough to reach 2^32
     * events must not start reading the ring backwards. */
    page_init(&t);
    t.head = 0xFFFFFFFEu;
    tail = 0xFFFFFFFEu;
    openmmo_text_push(&t, OPENMMO_TEXT_UNIT, 'A');
    openmmo_text_push(&t, OPENMMO_TEXT_UNIT, 'B');
    openmmo_text_push(&t, OPENMMO_TEXT_UNIT, 'C');
    n = openmmo_text_read(&t, &tail, out, 8, &dropped);
    CHECK(n == 3 && dropped == 0 && openmmo_text_unit(out[0]) == 'A' &&
          openmmo_text_unit(out[2]) == 'C',
          "the head wrapping past 2^32 loses nothing");
}

static void test_guards(void)
{
    struct openmmo_text_shm t;
    uint32_t out[4], tail = 0, dropped = 0;

    /* A page of another version is refused rather than read at the wrong
     * offsets, the same rule the frame channel enforces, and the reason this
     * page carries a version of its own at all. */
    page_init(&t);
    openmmo_text_push(&t, OPENMMO_TEXT_UNIT, 'x');
    t.version = OPENMMO_TEXT_VERSION + 1u;
    CHECK(openmmo_text_read(&t, &tail, out, 4, &dropped) == 0,
          "a page of the wrong version reads as nothing");
    t.version = OPENMMO_TEXT_VERSION;
    t.magic = 0;
    CHECK(openmmo_text_read(&t, &tail, out, 4, &dropped) == 0,
          "and so does one that is not this page at all");

    /* Pushing into a page that is not ready is a no-op, not a write: the window
     * creates the page before it stamps it, and a keystroke that lands in that
     * window must not be written into somebody else's memory. */
    memset(&t, 0, sizeof t);
    openmmo_text_push(&t, OPENMMO_TEXT_UNIT, 'x');
    CHECK(t.head == 0, "a page with no magic takes no keystrokes");
}

/* The standard's own boundaries, and the malformed forms a decoder must refuse.
 * Each row is bytes in, the UTF-16 units expected out; zero units means "this
 * is not text and must be rejected". */
static void test_utf8(void)
{
    static const struct {
        const char *name;
        unsigned char in[6];
        unsigned len;
        unsigned units;
        uint16_t out[2];
    } V[] = {
        { "U+0041 'A'",        { 0x41 },                   1, 1, { 0x0041, 0 } },
        { "U+007F, one byte",  { 0x7F },                   1, 1, { 0x007F, 0 } },
        { "U+0080, two",       { 0xC2, 0x80 },             2, 1, { 0x0080, 0 } },
        { "U+07FF, two",       { 0xDF, 0xBF },             2, 1, { 0x07FF, 0 } },
        { "U+0800, three",     { 0xE0, 0xA0, 0x80 },       3, 1, { 0x0800, 0 } },
        { "U+FFFF, three",     { 0xEF, 0xBF, 0xBF },       3, 1, { 0xFFFF, 0 } },
        { "U+00E9 e-acute",    { 0xC3, 0xA9 },             2, 1, { 0x00E9, 0 } },
        { "U+20AC euro",       { 0xE2, 0x82, 0xAC },       3, 1, { 0x20AC, 0 } },
        { "U+10000, a pair",   { 0xF0, 0x90, 0x80, 0x80 }, 4, 2,
                                                          { 0xD800, 0xDC00 } },
        { "U+10FFFF, the last",{ 0xF4, 0x8F, 0xBF, 0xBF }, 4, 2,
                                                          { 0xDBFF, 0xDFFF } },
        { "U+1F600, an emoji", { 0xF0, 0x9F, 0x98, 0x80 }, 4, 2,
                                                          { 0xD83D, 0xDE00 } },
        /* Refused. */
        { "overlong NUL",      { 0xC0, 0x80 },             2, 0, { 0, 0 } },
        { "overlong slash",    { 0xE0, 0x80, 0xAF },       3, 0, { 0, 0 } },
        { "a lone surrogate",  { 0xED, 0xA0, 0x80 },       3, 0, { 0, 0 } },
        { "past U+10FFFF",     { 0xF4, 0x90, 0x80, 0x80 }, 4, 0, { 0, 0 } },
        { "five-byte nonsense",{ 0xF8, 0x88, 0x80, 0x80 }, 4, 0, { 0, 0 } },
        { "a stray continuation", { 0x80 },                1, 0, { 0, 0 } },
        { "a truncated pair",  { 0xE2, 0x82 },             2, 0, { 0, 0 } },
    };
    unsigned i, bad = 0, badadv = 0;

    for (i = 0; i < sizeof V / sizeof V[0]; i++) {
        uint16_t out[2] = { 0, 0 };
        unsigned used = 0, n;

        n = openmmo_text_utf8_to_utf16(V[i].in, V[i].len, out, &used);
        if (n != V[i].units || (n >= 1 && out[0] != V[i].out[0]) ||
            (n == 2 && out[1] != V[i].out[1])) {
            printf("  FAIL %s: %u unit(s) %04X %04X\n", V[i].name, n,
                   out[0], out[1]);
            bad++;
        }
        /* However it went, the caller must be able to move on: a decoder that
         * reports zero bytes consumed on a bad byte is an infinite loop. */
        if (used < 1 || used > V[i].len) badadv++;
    }
    CHECK(bad == 0, "the standard's boundary values decode as the standard says");
    CHECK(badadv == 0, "every sequence, good or bad, advances the caller");

    {
        /* A whole string, the way the window hands one over. */
        const unsigned char s[] = { 'h', 0xC3, 0xA9, 0xF0, 0x9F, 0x98, 0x80, '!' };
        struct openmmo_text_shm t;
        uint32_t out[8], tail = 0;
        unsigned i2 = 0, n;

        page_init(&t);
        while (i2 < sizeof s) {
            uint16_t u[2];
            unsigned used = 1, k, got;

            got = openmmo_text_utf8_to_utf16(s + i2, (unsigned)sizeof s - i2, u,
                                             &used);
            for (k = 0; k < got; k++)
                openmmo_text_push(&t, OPENMMO_TEXT_UNIT, u[k]);
            i2 += used;
        }
        n = openmmo_text_read(&t, &tail, out, 8, NULL);
        CHECK(n == 5 && openmmo_text_unit(out[0]) == 'h' &&
              openmmo_text_unit(out[1]) == 0x00E9 &&
              openmmo_text_unit(out[2]) == 0xD83D &&
              openmmo_text_unit(out[3]) == 0xDE00 &&
              openmmo_text_unit(out[4]) == '!',
              "a typed line arrives as UTF-16 code units, surrogates in order");
    }
}

/* The way back. Everything downstream of the page is UTF-8, so the pair has to
 * close: the standard's boundary values again, then the same string the test
 * above pushed onto the ring, read out of it and rebuilt byte for byte. */
static void test_utf16_out(void)
{
    static const struct {
        const char *name;
        uint16_t    in[2];
        unsigned    len;
        unsigned    bytes;
        const char *out;
    } V[] = {
        { "NUL",                { 0x0000 },         1, 1, "\x00" },
        { "the last one byte",  { 0x007F },         1, 1, "\x7F" },
        { "the first two",      { 0x0080 },         1, 2, "\xC2\x80" },
        { "the last two",       { 0x07FF },         1, 2, "\xDF\xBF" },
        { "the first three",    { 0x0800 },         1, 3, "\xE0\xA0\x80" },
        { "the last BMP",       { 0xFFFF },         1, 3, "\xEF\xBF\xBF" },
        { "the first pair",     { 0xD800, 0xDC00 }, 2, 4, "\xF0\x90\x80\x80" },
        { "the last pair",      { 0xDBFF, 0xDFFF }, 2, 4, "\xF4\x8F\xBF\xBF" },
        { "a high half alone",  { 0xD800 },         1, 3, "\xEF\xBF\xBD" },
        { "a low half alone",   { 0xDC00 },         1, 3, "\xEF\xBF\xBD" },
        { "a high, then a letter", { 0xD800, 0x0041 }, 2, 3, "\xEF\xBF\xBD" },
    };
    unsigned i, bad = 0, badadv = 0;

    for (i = 0; i < sizeof V / sizeof V[0]; i++) {
        char out[4];
        unsigned used = 0, n;

        n = openmmo_text_utf16_to_utf8(V[i].in, V[i].len, out, &used);
        if (n != V[i].bytes || memcmp(out, V[i].out, n) != 0) {
            printf("  FAIL %s: %u byte(s)\n", V[i].name, n);
            bad++;
        }
        /* Half a pair must cost exactly the one unit it is, or the letter
         * after it disappears with it. */
        if (used < 1 || used > V[i].len) badadv++;
    }
    CHECK(bad == 0, "the standard's boundary values encode as the standard says");
    CHECK(badadv == 0, "every sequence, whole or broken, advances the caller");

    {
        /* Round trip: the same bytes the window handed over come back out. */
        static const char s[] = "h\xC3\xA9\xF0\x9F\x98\x80!";
        uint16_t u[16];
        char back[32];
        unsigned nu = 0, i2 = 0, nb = 0;

        while (s[i2] != '\0') {
            uint16_t one[2];
            unsigned used = 1, k, got;

            got = openmmo_text_utf8_to_utf16((const unsigned char *)s + i2,
                                             (unsigned)(sizeof s - 1 - i2), one,
                                             &used);
            for (k = 0; k < got; k++)
                u[nu++] = one[k];
            i2 += used;
        }
        i2 = 0;
        while (i2 < nu) {
            unsigned used = 1;

            nb += openmmo_text_utf16_to_utf8(u + i2, nu - i2, back + nb, &used);
            i2 += used;
        }
        back[nb] = '\0';
        CHECK(nu == 5 && nb == sizeof s - 1 && strcmp(back, s) == 0,
              "utf-8 out, code units, and the same utf-8 back");
    }
}

int text_channel_tests_run(void)
{
    failures = 0;
    printf("text channel:\n");
    test_layout();
    test_ring();
    test_guards();
    test_utf8();
    test_utf16_out();
    if (failures == 0) printf("text channel: all checks passed\n");
    else printf("text channel: %d check(s) FAILED\n", failures);
    return failures;
}
