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

/* Run the codec suite; returns the number of failed checks. */
int codec_tests_run(void)
{
    failures = 0;
    test_login_request_body();
    test_framing();
    test_primitive_roundtrip();
    test_underflow_is_sticky();

    if (failures)
        printf("codec: %d check(s) FAILED\n", failures);
    else
        printf("codec: all checks passed\n");
    return failures;
}
