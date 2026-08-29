/*
 * Known-answer vectors for the wire primitives, compiled into the binary that
 * ships them.
 */
#include <stdio.h>
#include <string.h>

#include "mmo.h"
#include "codec.h"
#include "checksum.h"
#include "crypto.h"
#include "p256.h"
#include "selftest.h"

static int fails;
static FILE *rout;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) {                                                             \
            fprintf(rout, "  ok   %s\n", msg);                                  \
        } else {                                                                \
            fprintf(rout, "  FAIL %s\n", msg);                                  \
            fails++;                                                            \
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

/* frame, the u16-le self-including length envelope; the server's own bytes. */
static void kat_frame(void)
{
    fprintf(rout, "frame (server bytecodec vector):\n");
    const u8 payload[] = { 0xAA, 0xBB, 0xCC };
    u8 want[8];
    size_t wn = unhex("0500aabbcc", want, sizeof want);

    mmo_wbuf w;
    mmo_wbuf_init(&w);
    mmo_frame_put(&w, payload, sizeof payload);
    CHECK(!w.err && w.len == wn && memcmp(w.data, want, wn) == 0,
          "frame(AA BB CC) == 05 00 AA BB CC");

    mmo_rbuf r;
    mmo_rbuf_init(&r, w.data, w.len);
    const u8 *body = NULL;
    size_t blen = 0;
    mmo_frame_result fr = mmo_frame_get(&r, &body, &blen);
    CHECK(fr == MMO_FRAME_OK && blen == 3 && memcmp(body, payload, 3) == 0,
          "frame reader recovers the payload");
    mmo_wbuf_free(&w);
}

/* CRC-16/ARC, the game-stream checksum; the server's committed vector. */
static void kat_crc16(void)
{
    fprintf(rout, "crc-16/arc (server Crc16Checksum vector):\n");
    static const u8 data[] = { 0x10, 0x20, 0x30, 0x40, 0x50 };
    CHECK(mmo_crc16(data, sizeof data) == 0xF0FB,
          "CRC16(10 20 30 40 50) == 0xF0FB");
}

/* HMAC-SHA256, the login-stream checksum primitive; RFC 4231 test case 1. */
static void kat_hmac(void)
{
    fprintf(rout, "hmac-sha256 (RFC 4231 test case 1):\n");
    u8 key[20], got[MMO_SHA256_DIGEST], want[MMO_SHA256_DIGEST];
    memset(key, 0x0b, sizeof key);
    mmo_hmac_sha256(key, sizeof key, "Hi There", 8, got);
    unhex("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
          want, sizeof want);
    CHECK(memcmp(got, want, sizeof want) == 0, "HMAC-SHA256(0x0b*20, \"Hi There\")");
}

/* AES-128-CTR, the session cipher; NIST SP 800-38A F.5.1, first block. */
static void kat_aes(void)
{
    fprintf(rout, "aes-128-ctr (NIST SP 800-38A F.5.1):\n");
    u8 key[16], iv[16], pt[16], want[16], got[16], back[16];
    unhex("2b7e151628aed2a6abf7158809cf4f3c", key, sizeof key);
    unhex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff", iv, sizeof iv);
    unhex("6bc1bee22e409f96e93d7e117393172a", pt, sizeof pt);
    unhex("874d6191b620e3261bef6864990db6ce", want, sizeof want);

    mmo_aes128ctr c;
    mmo_aes128ctr_init(&c, key, iv);
    mmo_aes128ctr_xor(&c, pt, got, sizeof pt);
    CHECK(memcmp(got, want, sizeof want) == 0, "F.5.1 block 1 encrypt");

    mmo_aes128ctr_init(&c, key, iv);
    mmo_aes128ctr_xor(&c, got, back, sizeof got);
    CHECK(memcmp(back, pt, sizeof pt) == 0, "decrypt restores plaintext");
}

/* ECDH over P-256, the handshake key agreement; an OpenSSL-generated vector. */
static void kat_ecdh(void)
{
    fprintf(rout, "p256 ecdh (OpenSSL vector):\n");
    u8 priv[MMO_P256_SCALAR], peer[MMO_P256_POINT];
    u8 want[MMO_P256_SCALAR], got[MMO_P256_SCALAR];
    unhex("21206934226a54438cb4b2b7e7173c6288aaf931479eb5ec40dcdf94ef24245d",
          priv, sizeof priv);
    unhex("0409d2f48d9cb56deff399d953498e69f9fbf04e99dbd31ee76ac458b6a732236f"
          "1c58186086a1e860682ca354c052491d392c9981903a86210f92c6043f9defca",
          peer, sizeof peer);
    unhex("e2678ba661f6b918a266600fb9073d5a838b6ac96bf2fe009120872b64aa5158",
          want, sizeof want);
    int rc = mmo_p256_ecdh(priv, peer, got);
    CHECK(rc == 0 && memcmp(got, want, sizeof want) == 0,
          "ECDH shared X matches OpenSSL");
}

int openmmo_selftest_run(FILE *out)
{
    rout = out ? out : stderr;
    fails = 0;

    kat_frame();
    kat_crc16();
    kat_hmac();
    kat_aes();
    kat_ecdh();

    if (fails)
        fprintf(rout, "selftest: %d check(s) FAILED\n", fails);
    else
        fprintf(rout, "selftest: all checks passed\n");
    return fails;
}
