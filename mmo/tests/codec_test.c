/* Known-answer and round-trip checks for the wire codec. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codec.h"

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

/* Decode a hex string ("AABB...") into out; returns the byte count. */
static size_t unhex(const char *hex, u8 *out, size_t cap)
{
    size_t n = 0;
    for (const char *p = hex; p[0] && p[1] && n < cap; p += 2) {
        unsigned byte;
        sscanf(p, "%2x", &byte);
        out[n++] = (u8)byte;
    }
    return n;
}

/* The server's LoginRequest body: username "test", manualLogin=true, empty
 * hwid, PasswordLogin(sha1Hex("test"), stayLoggedIn=false), language EN,
 * clientRevision=0, installationRevision=0, os=0, empty hardwareInfoCache. */
static const char *LOGIN_REQ_HEX =
    "740065007300740000000100006100390034006100380066006500350063006300"
    "620031003900620061003600310063003400630030003800370033006400330039"
    "00310065003900380037003900380032006600620062006400330000000065006e"
    "00000000000000000000000000";

static void test_login_request_body(void)
{
    printf("login request body:\n");
    u8 expect[256];
    size_t elen = unhex(LOGIN_REQ_HEX, expect, sizeof expect);
    CHECK(elen == 112, "fixture is 112 bytes");

    mmo_wbuf w;
    mmo_wbuf_init(&w);
    mmo_put_utf16_nt(&w, "test");                                    /* username */
    mmo_put_bool(&w, 1);                                             /* manualLogin */
    mmo_put_bytes_u8(&w, NULL, 0);                                   /* hwid */
    mmo_put_u8(&w, 0);                                               /* methodTag */
    mmo_put_utf16_nt(&w, "a94a8fe5ccb19ba61c4c0873d391e987982fbbd3"); /* password */
    mmo_put_bool(&w, 0);                                            /* stayLoggedIn */
    mmo_put_utf16_nt(&w, "en");                                      /* language */
    mmo_put_s32le(&w, 0);                                            /* clientRevision */
    mmo_put_s32le(&w, 0);                                            /* installationRevision */
    mmo_put_u8(&w, 0);                                               /* os */
    mmo_put_bytes_u8(&w, NULL, 0);                                   /* hardwareInfoCache */

    CHECK(!w.err, "writer reported no error");
    CHECK(w.len == elen, "encoded length matches fixture");
    CHECK(w.len == elen && memcmp(w.data, expect, elen) == 0,
          "encoded bytes match server fixture");
    mmo_wbuf_free(&w);
}

static void test_framing(void)
{
    printf("framing:\n");
    /* Frame a 3-byte payload; the length field counts itself, so 3 -> 0x0005. */
    const u8 payload[] = {0xAA, 0xBB, 0xCC};
    u8 expect[8];
    size_t elen = unhex("0500aabbcc", expect, sizeof expect);

    mmo_wbuf w;
    mmo_wbuf_init(&w);
    mmo_frame_put(&w, payload, sizeof payload);
    CHECK(!w.err && w.len == elen && memcmp(w.data, expect, elen) == 0,
          "frame(AA BB CC) == 05 00 AA BB CC");

    /* Read it back out. */
    mmo_rbuf r;
    mmo_rbuf_init(&r, w.data, w.len);
    const u8 *body = NULL;
    size_t blen = 0;
    mmo_frame_result fr = mmo_frame_get(&r, &body, &blen);
    CHECK(fr == MMO_FRAME_OK && blen == 3 && memcmp(body, payload, 3) == 0,
          "frame reader recovers the payload");
    CHECK(mmo_rbuf_remaining(&r) == 0, "frame reader consumes exactly the frame");
    mmo_wbuf_free(&w);

    /* A frame split across reads reports SHORT and leaves the cursor put. */
    u8 partial[] = {0x05, 0x00, 0xAA};
    mmo_rbuf_init(&r, partial, sizeof partial);
    fr = mmo_frame_get(&r, &body, &blen);
    CHECK(fr == MMO_FRAME_SHORT && r.pos == 0, "partial frame is SHORT, non-consuming");

    /* A length field below 2 cannot even cover itself: malformed. */
    u8 bad[] = {0x01, 0x00};
    mmo_rbuf_init(&r, bad, sizeof bad);
    fr = mmo_frame_get(&r, &body, &blen);
    CHECK(fr == MMO_FRAME_BAD, "length < 2 is BAD");
}

static void test_primitive_roundtrip(void)
{
    printf("primitive round-trips:\n");
    mmo_wbuf w;
    mmo_wbuf_init(&w);
    mmo_put_u8(&w, 0xA5);
    mmo_put_u16le(&w, 0x1234);
    mmo_put_s16le(&w, -2);
    mmo_put_u32le(&w, 0xDEADBEEFu);
    mmo_put_s32le(&w, -123456);
    mmo_put_s64le(&w, -1);
    mmo_put_bool(&w, 1);
    mmo_put_bytes_u8(&w, "hi", 2);
    mmo_put_utf16_nt(&w, "OpenMMO");
    CHECK(!w.err, "writer reported no error");

    mmo_rbuf r;
    mmo_rbuf_init(&r, w.data, w.len);
    CHECK(mmo_get_u8(&r) == 0xA5, "u8");
    CHECK(mmo_get_u16le(&r) == 0x1234, "u16le");
    CHECK(mmo_get_s16le(&r) == -2, "s16le");
    CHECK(mmo_get_u32le(&r) == 0xDEADBEEFu, "u32le");
    CHECK(mmo_get_s32le(&r) == -123456, "s32le");
    CHECK(mmo_get_s64le(&r) == -1, "s64le");
    CHECK(mmo_get_bool(&r) == 1, "bool");

    char blob[4];
    size_t bn = mmo_get_bytes_u8(&r, blob, sizeof blob);
    CHECK(bn == 2 && memcmp(blob, "hi", 2) == 0, "bytes_u8");

    char str[16];
    size_t sn = mmo_get_utf16_nt(&r, str, sizeof str);
    CHECK(sn == 7 && strcmp(str, "OpenMMO") == 0, "utf16 nul-nul string");

    CHECK(!r.err && mmo_rbuf_remaining(&r) == 0, "reader consumed exactly");
    mmo_wbuf_free(&w);
}

static void test_underflow_is_sticky(void)
{
    printf("underflow:\n");
    u8 one = 0x42;
    mmo_rbuf r;
    mmo_rbuf_init(&r, &one, 1);
    (void)mmo_get_u8(&r);
    CHECK(!r.err, "reading to the end is not an error");
    (void)mmo_get_u8(&r);
    CHECK(r.err, "reading past the end sets err");
    (void)mmo_get_u32le(&r);
    CHECK(r.err, "err stays sticky");
}

/* The strings that are not ASCII. */
static void test_utf16_transcodes(void)
{
    static const u8 W_ASCII[]  = { 0x4F,0x00, 0x70,0x00, 0x65,0x00, 0x6E,0x00,
                                   0x4D,0x00, 0x4D,0x00, 0x4F,0x00, 0x00,0x00 };
    /* "café", U+00E9, one code unit, three UTF-8 bytes it never used to have */
    static const u8 W_ACCENT[] = { 0x63,0x00, 0x61,0x00, 0x66,0x00, 0xE9,0x00,
                                   0x00,0x00 };
    /* "日本語", three CJK code units, whose low bytes alone are nonsense */
    static const u8 W_CJK[]    = { 0xE5,0x65, 0x2C,0x67, 0x9E,0x8A, 0x00,0x00 };
    /* U+1F310, above the BMP, so a surrogate pair on the wire */
    static const u8 W_ASTRAL[] = { 0x3C,0xD8, 0x10,0xDF, 0x00,0x00 };
    /* "Renée 你好 🌐", all three widths and a pair in one string */
    static const u8 W_MIXED[]  = { 0x52,0x00, 0x65,0x00, 0x6E,0x00, 0xE9,0x00,
                                   0x65,0x00, 0x20,0x00, 0x60,0x4F, 0x7D,0x59,
                                   0x20,0x00, 0x3C,0xD8, 0x10,0xDF, 0x00,0x00 };
    static const struct {
        const char *utf8;
        const u8   *wire;
        size_t      wire_n;
        const char *what;
    } V[] = {
        { "OpenMMO",                   W_ASCII,  sizeof W_ASCII,  "ASCII" },
        { "caf\xC3\xA9",               W_ACCENT, sizeof W_ACCENT, "an accent" },
        { "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E",
                                       W_CJK,    sizeof W_CJK,    "CJK" },
        { "\xF0\x9F\x8C\x90",          W_ASTRAL, sizeof W_ASTRAL, "a surrogate pair" },
        { "Ren\xC3\xA9""e \xE4\xBD\xA0\xE5\xA5\xBD \xF0\x9F\x8C\x90",
                                       W_MIXED,  sizeof W_MIXED,  "all of them at once" },
    };

    printf("utf-16 on the wire, utf-8 in the client:\n");
    for (size_t i = 0; i < sizeof V / sizeof V[0]; i++) {
        char msg[96];
        mmo_wbuf w;
        mmo_rbuf r;
        char back[64];
        size_t n;

        mmo_wbuf_init(&w);
        mmo_put_utf16_nt(&w, V[i].utf8);
        snprintf(msg, sizeof msg, "%s: written exactly as the server writes it",
                 V[i].what);
        CHECK(!w.err && w.len == V[i].wire_n &&
                  memcmp(w.data, V[i].wire, V[i].wire_n) == 0, msg);
        snprintf(msg, sizeof msg, "%s: counted in the units the writer wrote",
                 V[i].what);
        /* The wire is the units plus the NUL-NUL, two bytes each, which is the
         * oracle for the counter a cap is checked against. */
        CHECK(mmo_utf16_units(V[i].utf8) * 2 + 2 == w.len, msg);
        mmo_wbuf_free(&w);

        mmo_rbuf_init(&r, V[i].wire, V[i].wire_n);
        n = mmo_get_utf16_nt(&r, back, sizeof back);
        snprintf(msg, sizeof msg, "%s: read back as the utf-8 it started as",
                 V[i].what);
        CHECK(!r.err && mmo_rbuf_remaining(&r) == 0 &&
                  n == strlen(V[i].utf8) && strcmp(back, V[i].utf8) == 0, msg);
    }

    /* Malformed UTF-8 in, one replacement character out. */
    {
        static const u8 want[] = { 0x61,0x00, 0xFD,0xFF, 0x62,0x00, 0x00,0x00 };
        mmo_wbuf w;

        mmo_wbuf_init(&w);
        mmo_put_utf16_nt(&w, "a\xFF" "b");
        CHECK(!w.err && w.len == sizeof want &&
                  memcmp(w.data, want, sizeof want) == 0,
              "a byte that is not utf-8 goes out as one replacement character");
        CHECK(mmo_utf16_units("a\xFF" "b") == 3,
              "and the counter charges one unit for it, the way the writer does");
        mmo_wbuf_free(&w);
    }

    /* Half a surrogate pair in, one replacement character out. */
    {
        static const u8 lone[] = { 0x3C,0xD8, 0x00,0x00 };
        mmo_rbuf r;
        char back[16];
        size_t n;

        mmo_rbuf_init(&r, lone, sizeof lone);
        n = mmo_get_utf16_nt(&r, back, sizeof back);
        CHECK(!r.err && n == 3 && strcmp(back, "\xEF\xBF\xBD") == 0,
              "a surrogate with no partner reads back as one replacement");
    }

    /* A destination too small cuts between code points, never inside one, and
     * still says how much the whole string wanted. */
    {
        char small[6];
        mmo_rbuf r;
        size_t n;

        mmo_rbuf_init(&r, W_CJK, sizeof W_CJK);
        n = mmo_get_utf16_nt(&r, small, sizeof small);
        CHECK(!r.err && mmo_rbuf_remaining(&r) == 0 && n == 9 &&
                  strcmp(small, "\xE6\x97\xA5") == 0,
              "a short buffer keeps whole characters and reports the full length");
    }
}

/* Run the codec suite; returns the number of failed checks. */
int codec_tests_run(void)
{
    failures = 0;
    test_login_request_body();
    test_framing();
    test_primitive_roundtrip();
    test_utf16_transcodes();
    test_underflow_is_sticky();

    if (failures)
        printf("codec: %d check(s) FAILED\n", failures);
    else
        printf("codec: all checks passed\n");
    return failures;
}
