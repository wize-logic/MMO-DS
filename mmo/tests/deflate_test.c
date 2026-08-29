/* Known-answer checks for the game stream's raw-DEFLATE codec. */
#include <stdio.h>
#include <string.h>

#include "deflate.h"

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

/* --- vectors from python3 zlib (independent oracle) ----------------------- */

static const u8 kat_p1[400] = {116,104,101,32,113,117,105,99,107,32,98,114,111,119,110,32,102,111,120,32,106,117,109,112,115,32,111,118,101,114,32,116,104,101,32,108,97,122,121,32,100,111,103,46,32,116,104,101,32,113,117,105,99,107,32,98,114,111,119,110,32,102,111,120,32,106,117,109,112,115,32,111,118,101,114,32,116,104,101,32,108,97,122,121,32,100,111,103,46,32,116,104,101,32,113,117,105,99,107,32,98,114,111,119,110,32,102,111,120,32,106,117,109,112,115,32,111,118,101,114,32,116,104,101,32,108,97,122,121,32,100,111,103,46,32,116,104,101,32,113,117,105,99,107,32,98,114,111,119,110,32,102,111,120,32,106,117,109,112,115,32,111,118,101,114,32,116,104,101,32,108,97,122,121,32,100,111,103,46,32,116,104,101,32,113,117,105,99,107,32,98,114,111,119,110,32,102,111,120,32,106,117,109,112,115,32,111,118,101,114,32,116,104,101,32,108,97,122,121,32,100,111,103,46,32,116,104,101,32,113,117,105,99,107,32,98,114,111,119,110,32,102,111,120,32,106,117,109,112,115,32,111,118,101,114,32,116,104,101,32,108,97,122,121,32,100,111,103,46,32,116,104,101,32,113,117,105,99,107,32,98,114,111,119,110,32,102,111,120,32,106,117,109,112,115,32,111,118,101,114,32,116,104,101,32,108,97,122,121,32,100,111,103,46,32,116,104,101,32,113,117,105,99,107,32,98,114,111,119,110,32,102,111,120,32,106,117,109,112,115,32,111,118,101,114,32,116,104,101,32,108,97,122,121,32,100,111,103,46,32,116,104,101,32,113,117,105,99,107,32,98,114,111,119,110,32,102,111,120,32,106,117,109,112,115,32,111,118,101,114,32,116,104,101,32,108,97,122,121,32};
#define KAT_P1_LEN 400
static const u8 kat_seg1[51] = {42,201,72,85,40,44,205,76,206,86,72,42,202,47,207,83,72,203,175,80,200,42,205,45,40,86,200,47,75,45,82,40,1,74,231,36,86,85,42,164,228,167,235,129,121,163,138,105,170,24,0};
static const u8 kat_pa[312] = {65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,48,49,50,51,52,53,54,55,56,57,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,48,49,50,51,52,53,54,55,56,57,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,48,49,50,51,52,53,54,55,56,57,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,48,49,50,51,52,53,54,55,56,57,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,48,49,50,51,52,53,54,55,56,57,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,48,49,50,51,52,53,54,55,56,57,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112};
#define KAT_PA_LEN 312
static const u8 kat_sega[58] = {114,116,114,118,113,117,115,247,240,244,242,246,241,245,243,15,8,12,10,14,9,13,11,143,136,140,50,48,52,50,54,49,53,51,183,176,76,76,74,78,73,77,75,207,200,204,202,206,201,205,203,47,112,28,213,83,0,0};
static const u8 kat_pb[312] = {65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,48,49,50,51,52,53,54,55,56,57,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,48,49,50,51,52,53,54,55,56,57,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,48,49,50,51,52,53,54,55,56,57,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,48,49,50,51,52,53,54,55,56,57,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,48,49,50,51,52,53,54,55,56,57,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,48,49,50,51,52,53,54,55,56,57,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112};
#define KAT_PB_LEN 312
static const u8 kat_segb[7] = {26,213,67,158,30,0,0};

static void test_inflate_huffman(void)
{
    printf("inflate: dynamic-huffman segment vs python zlib:\n");
    mmo_inflate s;
    mmo_inflate_init(&s);
    u8 out[1024];
    size_t n = mmo_inflate_segment(&s, kat_seg1, sizeof kat_seg1, out, sizeof out);
    CHECK(n == KAT_P1_LEN, "decoded length matches the payload");
    CHECK(n == KAT_P1_LEN && memcmp(out, kat_p1, KAT_P1_LEN) == 0,
          "decoded bytes match the payload");
}

static void test_inflate_window_persists(void)
{
    printf("inflate: persistent window across two segments:\n");
    mmo_inflate s;
    mmo_inflate_init(&s);
    u8 a[1024], b[1024];

    size_t na = mmo_inflate_segment(&s, kat_sega, sizeof kat_sega, a, sizeof a);
    CHECK(na == KAT_PA_LEN && memcmp(a, kat_pa, KAT_PA_LEN) == 0,
          "first segment decodes");

    /* kat_segb is only 7 bytes because it is almost entirely a back-reference
     * into segment A. It decodes correctly only if the window carried over. */
    size_t nb = mmo_inflate_segment(&s, kat_segb, sizeof kat_segb, b, sizeof b);
    CHECK(nb == KAT_PB_LEN && memcmp(b, kat_pb, KAT_PB_LEN) == 0,
          "second segment back-references the first (window persisted)");

    /* A fresh inflater has no history, so the same short segment must not
     * silently produce the same bytes, proving the persistence was real. */
    mmo_inflate fresh;
    mmo_inflate_init(&fresh);
    u8 c[1024];
    size_t nc = mmo_inflate_segment(&fresh, kat_segb, sizeof kat_segb, c, sizeof c);
    CHECK(!(nc == KAT_PB_LEN && memcmp(c, kat_pb, KAT_PB_LEN) == 0),
          "the same segment on a fresh window does not reproduce the payload");
}

static void test_inflate_overflow(void)
{
    printf("inflate: guards a too-small output buffer:\n");
    mmo_inflate s;
    mmo_inflate_init(&s);
    u8 tiny[8];
    size_t n = mmo_inflate_segment(&s, kat_seg1, sizeof kat_seg1, tiny, sizeof tiny);
    CHECK(n == (size_t)-1, "overflow is reported, not truncated silently");
}

static void test_deflate_roundtrip(void)
{
    printf("deflate: stored block round-trips through our inflater:\n");
    /* An incompressible-ish payload over the threshold; the stored deflater
     * emits it verbatim in a raw-DEFLATE stored block. */
    u8 payload[600];
    for (int i = 0; i < (int)sizeof payload; i++)
        payload[i] = (u8)(i * 7 + 3);

    mmo_deflate d;
    mmo_deflate_init(&d);
    u8 seg[700];
    size_t sn = mmo_deflate_segment(&d, payload, sizeof payload, seg, sizeof seg);
    CHECK(sn != (size_t)-1, "deflate produced a segment");

    mmo_inflate s;
    mmo_inflate_init(&s);
    u8 back[1024];
    size_t bn = mmo_inflate_segment(&s, seg, sn, back, sizeof back);
    CHECK(bn == sizeof payload && memcmp(back, payload, sizeof payload) == 0,
          "inflate(deflate(p)) == p");

    /* The stored ceiling traps rather than truncating. */
    u8 big[2];
    size_t huge = mmo_deflate_segment(&d, payload, 0x10000, big, sizeof big);
    CHECK(huge == (size_t)-1, "over-0xFFFF payload traps loudly");
    size_t tight = mmo_deflate_segment(&d, payload, sizeof payload, big, sizeof big);
    CHECK(tight == (size_t)-1, "a too-small output buffer traps loudly");
}

static void test_inflate_gzip(void)
{
    /* python3 gzip.compress(b'sync-ok', mtime=0) */
    static const u8 gz[] = {
        0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff,
        0x2b, 0xae, 0xcc, 0x4b, 0xd6, 0xcd, 0xcf, 0x06, 0x00,
        0x0b, 0xc2, 0xc0, 0x6b, 0x07, 0x00, 0x00, 0x00,
    };
    u8 bad[sizeof gz];
    u8 out[16];
    size_t n;

    printf("inflate: gzip member vs python gzip:\n");
    n = mmo_inflate_gzip(gz, sizeof gz, out, sizeof out);
    CHECK(n == 7 && memcmp(out, "sync-ok", 7) == 0,
          "gzip.compress('sync-ok') inflates");
    CHECK(mmo_inflate_gzip(gz, sizeof gz, out, 3) == (size_t)-1,
          "a too-small gzip output traps");
    memcpy(bad, gz, sizeof gz);
    bad[0] = 0x00;
    CHECK(mmo_inflate_gzip(bad, sizeof bad, out, sizeof out) == (size_t)-1,
          "a bad gzip magic traps");
}

int deflate_tests_run(void)
{
    failures = 0;
    test_inflate_huffman();
    test_inflate_window_persists();
    test_inflate_overflow();
    test_deflate_roundtrip();
    test_inflate_gzip();

    if (failures)
        printf("deflate: %d check(s) FAILED\n", failures);
    else
        printf("deflate: all checks passed\n");
    return failures;
}
