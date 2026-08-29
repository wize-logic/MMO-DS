/* Known-answer checks for the two frame-checksum profiles. */
#include <stdio.h>
#include <string.h>

#include "checksum.h"
#include "session.h"

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

static int tag_eq(const u8 *got, size_t n, const char *hex)
{
    u8 want[MMO_CHECKSUM_MAX];
    size_t wn = unhex(hex, want, sizeof want);
    return wn == n && memcmp(got, want, n) == 0;
}

static void test_crc16(void)
{
    printf("crc-16/arc (the algorithm's check value, then the server's vector):\n");

    /* CRC-16/ARC's published check value: the algorithm is named, so the
     * strongest oracle for it is its own, not a byte the server happened to
     * emit. */
    static const u8 check[] = "123456789";
    CHECK(mmo_crc16(check, sizeof check - 1) == 0xBB3D,
          "CRC16(\"123456789\") == 0xBB3D, the CRC-16/ARC check value");

    static const u8 data[] = { 0x10, 0x20, 0x30, 0x40, 0x50 };

    CHECK(mmo_crc16(data, sizeof data) == 0xF0FB, "CRC16(10 20 30 40 50) == 0xF0FB");

    u8 tag[MMO_CHECKSUM_MAX];
    size_t n = mmo_checksum_calc(2, NULL, NULL, data, sizeof data, tag);
    CHECK(n == 2, "size-2 profile emits a 2-byte tag");
    /* Wire order is little-endian: low byte first. */
    CHECK(tag_eq(tag, n, "FBF0"), "CRC16 tag is FB F0 (little-endian)");

    u32 unused = 0;
    CHECK(mmo_checksum_verify(2, NULL, &unused, data, sizeof data, tag, 2) == 0,
          "CRC16 verify accepts its own tag");
    u8 bad[2] = { 0, 0 };
    CHECK(mmo_checksum_verify(2, NULL, &unused, data, sizeof data, bad, 2) != 0,
          "CRC16 verify rejects a wrong tag");
    CHECK(unused == 0, "CRC16 does not touch the round counter");
}

static void test_hmac_profile(void)
{
    printf("hmac-sha256 profile (independent python oracle):\n");
    u8 key[MMO_CHECKSUM_KEY];
    for (int i = 0; i < MMO_CHECKSUM_KEY; i++)
        key[i] = (u8)i;                     /* key = 00 01 .. 0f */
    static const u8 data[] = { 0xAA, 0xBB, 0xCC };
    u8 tag[MMO_CHECKSUM_MAX];

    /* Size-8 width (the server's HmacSha256Checksum test width), rounds 0 then 1. */
    u32 round = 0;
    size_t n = mmo_checksum_calc(8, key, &round, data, sizeof data, tag);
    CHECK(n == 8 && tag_eq(tag, n, "d88c825e27a8cc8f"),
          "HMAC-8 round 0 == d88c825e27a8cc8f");
    CHECK(round == 1, "round counter advanced to 1");
    n = mmo_checksum_calc(8, key, &round, data, sizeof data, tag);
    CHECK(n == 8 && tag_eq(tag, n, "3eedd5ca44099934"),
          "HMAC-8 round 1 == 3eedd5ca44099934 (differs from round 0)");

    /* Size-16 width, the deployed login profile, rounds 0 then 1. */
    round = 0;
    n = mmo_checksum_calc(16, key, &round, data, sizeof data, tag);
    CHECK(n == 16 && tag_eq(tag, n, "d88c825e27a8cc8f433bdc2727f2ff26"),
          "HMAC-16 round 0 == d88c825e27a8cc8f433bdc2727f2ff26");
    n = mmo_checksum_calc(16, key, &round, data, sizeof data, tag);
    CHECK(n == 16 && tag_eq(tag, n, "3eedd5ca440999347b29cb560e490146"),
          "HMAC-16 round 1 == 3eedd5ca440999347b29cb560e490146");

    /* A verifier tracking its own counter accepts each frame in order. */
    u32 cround = 0, vround = 0;
    u8 t0[MMO_CHECKSUM_MAX], t1[MMO_CHECKSUM_MAX];
    mmo_checksum_calc(16, key, &cround, data, sizeof data, t0);
    mmo_checksum_calc(16, key, &cround, data, sizeof data, t1);
    CHECK(mmo_checksum_verify(16, key, &vround, data, sizeof data, t0, 16) == 0 &&
          mmo_checksum_verify(16, key, &vround, data, sizeof data, t1, 16) == 0,
          "verify tracks the round counter across two frames");

    /* A stale tag (right key, wrong round) is rejected. */
    vround = 0;
    CHECK(mmo_checksum_verify(16, key, &vround, data, sizeof data, t1, 16) != 0,
          "HMAC verify rejects a tag from the wrong round");
    /* A wrong length is rejected before any round is spent. */
    u32 lenr = 0;
    CHECK(mmo_checksum_verify(16, key, &lenr, data, sizeof data, t0, 8) != 0 &&
          lenr == 0,
          "HMAC verify rejects a short tag without spending a round");
}

static void test_session_selection(void)
{
    printf("session selects the profile by negotiated size:\n");
    static const u8 data[] = { 0x01, 0x02, 0x03, 0x04 };
    u8 tag[MMO_CHECKSUM_MAX];

    /* A zeroed crypto with the login profile: HMAC keyed by the (zero) seed. */
    mmo_session_crypto c;
    memset(&c, 0, sizeof c);
    c.checksum_size = 16;
    size_t n = mmo_session_checksum_out(&c, data, sizeof data, tag);
    CHECK(n == 16, "login session emits a 16-byte tag");
    CHECK(c.out_round == 1, "outbound round counter advanced");
    CHECK(mmo_session_checksum_in(&c, data, sizeof data, tag, 16) == 0,
          "the S->C counter starts in step, so it verifies the first frame");

    /* The game profile: CRC-16, no key, no counter. */
    memset(&c, 0, sizeof c);
    c.checksum_size = 2;
    n = mmo_session_checksum_out(&c, data, sizeof data, tag);
    CHECK(n == 2 && c.out_round == 0, "game session emits a keyless 2-byte tag");
    CHECK(mmo_session_checksum_in(&c, data, sizeof data, tag, 2) == 0,
          "game session verifies its own CRC tag");
}

/* Run the checksum suite; returns the number of failed checks. */
int checksum_tests_run(void)
{
    failures = 0;
    test_crc16();
    test_hmac_profile();
    test_session_selection();

    if (failures)
        printf("checksum: %d check(s) FAILED\n", failures);
    else
        printf("checksum: all checks passed\n");
    return failures;
}
