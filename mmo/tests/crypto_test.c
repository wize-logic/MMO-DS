/* Known-answer checks for the vendored symmetric crypto. */
#include <stdio.h>
#include <string.h>

#include "crypto.h"

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

static int digest_eq(const u8 *got, const char *hex, size_t n)
{
    u8 want[64];
    unhex(hex, want, sizeof want);
    return memcmp(got, want, n) == 0;
}

static void test_sha256(void)
{
    printf("sha-256 (FIPS 180-4):\n");
    u8 d[MMO_SHA256_DIGEST];

    mmo_sha256("abc", 3, d);
    CHECK(digest_eq(d,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 32),
        "SHA256(\"abc\")");

    mmo_sha256("", 0, d);
    CHECK(digest_eq(d,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", 32),
        "SHA256(\"\")");

    /* 56-byte message: crosses into a second padding block. */
    const char *m2 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    mmo_sha256(m2, strlen(m2), d);
    CHECK(digest_eq(d,
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", 32),
        "SHA256(two-block message)");

    /* Incremental update must equal the one-shot: feed "abc" one byte at a time. */
    mmo_sha256_ctx c;
    mmo_sha256_init(&c);
    mmo_sha256_update(&c, "a", 1);
    mmo_sha256_update(&c, "b", 1);
    mmo_sha256_update(&c, "c", 1);
    mmo_sha256_final(&c, d);
    CHECK(digest_eq(d,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 32),
        "streamed SHA256 equals one-shot");
}

static void test_sha1(void)
{
    printf("sha-1 (FIPS 180-4, password digest):\n");
    u8 d[MMO_SHA1_DIGEST];

    mmo_sha1("abc", 3, d);
    CHECK(digest_eq(d, "a9993e364706816aba3e25717850c26c9cd0d89d", 20),
          "SHA1(\"abc\")");

    mmo_sha1("", 0, d);
    CHECK(digest_eq(d, "da39a3ee5e6b4b0d3255bfef95601890afd80709", 20),
          "SHA1(\"\")");

    /* 56-byte message: the 0x80 marker leaves no room for the length word, so
     * padding spills into a second block. */
    mmo_sha1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, d);
    CHECK(digest_eq(d, "84983e441c3bd26ebaae4aa1f95129e5e54670f1", 20),
          "SHA1(56-byte message, two-block padding)");

    /* The exact digest the login flow puts on the wire for password "test". */
    char hex[41];
    mmo_sha1_hex("test", 4, hex);
    CHECK(strcmp(hex, "a94a8fe5ccb19ba61c4c0873d391e987982fbbd3") == 0,
          "sha1Hex(\"test\") matches the server's stored hash");
}

static void test_hmac_sha256(void)
{
    printf("hmac-sha256 (RFC 4231):\n");
    u8 d[MMO_SHA256_DIGEST];
    u8 key[256];
    size_t klen;

    /* TC1: 20-byte 0x0b key, "Hi There". */
    memset(key, 0x0b, 20);
    mmo_hmac_sha256(key, 20, "Hi There", 8, d);
    CHECK(digest_eq(d,
        "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", 32),
        "RFC 4231 test case 1");

    /* TC2: "Jefe" key, "what do ya want for nothing?". */
    mmo_hmac_sha256("Jefe", 4, "what do ya want for nothing?", 28, d);
    CHECK(digest_eq(d,
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", 32),
        "RFC 4231 test case 2");

    /* TC6: 131-byte key (exceeds the 64-byte block, so it is hashed first). */
    klen = 131;
    memset(key, 0xaa, klen);
    mmo_hmac_sha256(key, klen,
        "Test Using Larger Than Block-Size Key - Hash Key First", 54, d);
    CHECK(digest_eq(d,
        "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54", 32),
        "RFC 4231 test case 6 (oversize key)");
}

static void test_aes128ctr(void)
{
    printf("aes-128-ctr (NIST SP 800-38A F.5.1):\n");
    u8 key[16], iv[16], pt[64], want[64], got[64];
    unhex("2b7e151628aed2a6abf7158809cf4f3c", key, sizeof key);
    unhex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", iv, sizeof iv);
    unhex("6bc1bee22e409f96e93d7e117393172a"
          "ae2d8a571e03ac9c9eb76fac45af8e51"
          "30c81c46a35ce411e5fbc1191a0a52ef"
          "f69f2445df4f9b17ad2b417be66c3710", pt, sizeof pt);
    size_t wlen = unhex(
          "874d6191b620e3261bef6864990db6ce"
          "9806f66b7970fdff8617187bb9fffdff"
          "5ae4df3edbd5d35e5b4f09020db03eab"
          "1e031dda2fbe03d1792170a0f3009cee", want, sizeof want);

    /* One-shot encrypt of all four blocks. */
    mmo_aes128ctr c;
    mmo_aes128ctr_init(&c, key, iv);
    mmo_aes128ctr_xor(&c, pt, got, 64);
    CHECK(wlen == 64 && memcmp(got, want, 64) == 0, "F.5.1 four-block encrypt");

    /* CTR is symmetric: re-running over the ciphertext restores the plaintext. */
    u8 back[64];
    mmo_aes128ctr_init(&c, key, iv);
    mmo_aes128ctr_xor(&c, got, back, 64);
    CHECK(memcmp(back, pt, 64) == 0, "decrypt restores plaintext");

    /* Continuous keystream survives ragged boundaries: 10 + 27 + 27 bytes must
     * equal the one-shot ciphertext, proving the per-call offset carries. */
    u8 chunked[64];
    mmo_aes128ctr_init(&c, key, iv);
    mmo_aes128ctr_xor(&c, pt, chunked, 10);
    mmo_aes128ctr_xor(&c, pt + 10, chunked + 10, 27);
    mmo_aes128ctr_xor(&c, pt + 37, chunked + 37, 27);
    CHECK(memcmp(chunked, want, 64) == 0, "ragged-boundary stream matches one-shot");
}

/* Run the crypto suite; returns the number of failed checks. */
int crypto_tests_run(void)
{
    failures = 0;
    test_sha256();
    test_sha1();
    test_hmac_sha256();
    test_aes128ctr();

    if (failures)
        printf("crypto: %d check(s) FAILED\n", failures);
    else
        printf("crypto: all checks passed\n");
    return failures;
}
