/* Known-answer checks for the vendored P-256 operations. */
#include <stdio.h>
#include <string.h>

#include "p256.h"

static int failures;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) { printf("  ok   %s\n", msg); }                               \
        else { printf("  FAIL %s\n", msg); failures++; }                        \
    } while (0)

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

/* --- OpenSSL-generated prime256v1 vectors ------------------------------ */
/* a private scalar */
static const char *A_PRIV =
    "21206934226a54438cb4b2b7e7173c6288aaf931479eb5ec40dcdf94ef24245d";
/* a's public point (0x04||X||Y) = A_PRIV * G */
static const char *A_PUB =
    "0451beb6d6949c378ed53824faefe0b2f5cdc60d32dada6d665bc69839ff1e0062"
    "530cb27bdb5d4ab3a453d4ca0f7f31c6f9a93094c9346eb6287a0c70f310e6e7";
/* b's public point (the ECDH peer) */
static const char *B_PUB =
    "0409d2f48d9cb56deff399d953498e69f9fbf04e99dbd31ee76ac458b6a732236f"
    "1c58186086a1e860682ca354c052491d392c9981903a86210f92c6043f9defca";
/* ECDH(A_PRIV, B_PUB) shared secret X coordinate */
static const char *ECDH_SHARED =
    "e2678ba661f6b918a266600fb9073d5a838b6ac96bf2fe009120872b64aa5158";
/* SHA256withECDSA over MSG_FOX, signed by a, DER-encoded */
static const char *MSG_FOX = "The quick brown fox jumps over the lazy dog";
static const char *SIG_FOX =
    "3045022100c3d89c41a1eb22a86d2111fa7fc4b5b004666ef1d6a902923552e5641ca49845"
    "02207d117ee1965d8259a24403a153c35f854d41a95044bcc9b50a6f0eb873b9c2d8";

/* --- RFC 6979 A.2.5 published P-256 / SHA-256 "sample" vector ---------- */
static const char *RFC_PUB =
    "04"
    "60FED4BA255A9D31C961EB74C6356D68C049B8923B61FA6CE669622E60F29FB6"
    "7903FE1008B8BC99A41AE9E95628BC64F2F1B20C2D7E9F5177A3C294D4462299";
static const char *RFC_MSG = "sample";
/* DER of r,s (both high-bit-set, so each carries a 0x00 sign byte) */
static const char *RFC_SIG =
    "3046"
    "022100EFD48B2AACB6A8FD1140DD9CD45E81D69D2C877B56AAF991C34D0EA84EAF3716"
    "022100F7CB1C942D657C41D436C7A1B6E29F65F3E900DBB9AFF4064DC4AB2F843ACDA8";

static void test_derive_pub(void)
{
    printf("p256 derive-pub (OpenSSL vector):\n");
    u8 priv[32], want[65], got[65];
    unhex(A_PRIV, priv, sizeof priv);
    unhex(A_PUB, want, sizeof want);
    int rc = mmo_p256_derive_pub(priv, got);
    CHECK(rc == 0, "derive succeeds");
    CHECK(rc == 0 && memcmp(got, want, 65) == 0, "priv*G == published public point");
}

static void test_ecdh(void)
{
    printf("p256 ecdh (OpenSSL vector):\n");
    u8 priv[32], peer[65], want[32], got[32];
    unhex(A_PRIV, priv, sizeof priv);
    unhex(B_PUB, peer, sizeof peer);
    unhex(ECDH_SHARED, want, sizeof want);
    int rc = mmo_p256_ecdh(priv, peer, got);
    CHECK(rc == 0, "ecdh succeeds");
    CHECK(rc == 0 && memcmp(got, want, 32) == 0, "shared X matches OpenSSL");

    /* An off-curve peer point (flip one X byte) must be rejected. */
    u8 bad[65];
    memcpy(bad, peer, 65);
    bad[1] ^= 0x01;
    CHECK(mmo_p256_ecdh(priv, bad, got) == -1, "off-curve peer rejected");

    /* A zero scalar is not a valid private key. */
    u8 zero[32] = {0};
    CHECK(mmo_p256_ecdh(zero, peer, got) == -1, "zero scalar rejected");
}

static void test_ecdsa(void)
{
    printf("p256 ecdsa-verify:\n");
    u8 pub[65], sig[80];
    size_t slen;

    unhex(A_PUB, pub, sizeof pub);
    slen = unhex(SIG_FOX, sig, sizeof sig);
    CHECK(mmo_p256_ecdsa_verify(pub, MSG_FOX, strlen(MSG_FOX), sig, slen) == 1,
          "OpenSSL signature verifies");

    /* Tamper: flip a signature byte -> must fail. */
    u8 badsig[80];
    memcpy(badsig, sig, slen);
    badsig[slen - 1] ^= 0x01;
    CHECK(mmo_p256_ecdsa_verify(pub, MSG_FOX, strlen(MSG_FOX), badsig, slen) == 0,
          "tampered signature rejected");

    /* Tamper: change the message -> must fail. */
    CHECK(mmo_p256_ecdsa_verify(pub, "the quick brown fox jumps over the lazy dog",
                                strlen(MSG_FOX), sig, slen) == 0,
          "wrong message rejected");

    /* RFC 6979 A.2.5 published vector. */
    u8 rpub[65], rsig[80];
    size_t rlen;
    unhex(RFC_PUB, rpub, sizeof rpub);
    rlen = unhex(RFC_SIG, rsig, sizeof rsig);
    CHECK(mmo_p256_ecdsa_verify(rpub, RFC_MSG, strlen(RFC_MSG), rsig, rlen) == 1,
          "RFC 6979 A.2.5 signature verifies");

    /* Cross: the RFC signature must not verify under the OpenSSL key. */
    CHECK(mmo_p256_ecdsa_verify(pub, RFC_MSG, strlen(RFC_MSG), rsig, rlen) == 0,
          "signature rejected under wrong key");
}

int p256_tests_run(void)
{
    failures = 0;
    test_derive_pub();
    test_ecdh();
    test_ecdsa();
    if (failures) printf("p256: %d check(s) FAILED\n", failures);
    else printf("p256: all checks passed\n");
    return failures;
}
