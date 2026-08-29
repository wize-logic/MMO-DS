/* Known-answer checks for the transport session handshake. */
#include <stdio.h>
#include <string.h>

#include "session.h"

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

/* --- ClientHello: a pinned 19-byte frame ------------------------------- */
/* random=0, timestamp=1700000000000 ms, framed (len prefix includes itself). */
static const char *CLIENT_HELLO_FRAME =
    "130000c865db8936a19c2c797a75f8538d82c5";

static void test_client_hello(void)
{
    printf("client hello:\n");
    u8 expect[32];
    size_t elen = unhex(CLIENT_HELLO_FRAME, expect, sizeof expect);

    mmo_wbuf pkt, frame;
    mmo_wbuf_init(&pkt);
    mmo_wbuf_init(&frame);
    mmo_hs_write_client_hello(&pkt, (s64)1700000000000LL, 0);
    CHECK(!pkt.err && pkt.len == 1 + MMO_HS_HELLO_BODY, "packet is opcode + 16 body");
    mmo_frame_put(&frame, pkt.data, pkt.len);
    CHECK(!frame.err && frame.len == elen && memcmp(frame.data, expect, elen) == 0,
          "framed ClientHello matches the 19-byte vector");
    mmo_wbuf_free(&pkt);
    mmo_wbuf_free(&frame);
}

/* --- ClientReady: opcode 02, u16 len 65, the point --------------------- */
static void test_client_ready(void)
{
    printf("client ready:\n");
    u8 pub[MMO_P256_POINT];
    for (int i = 0; i < MMO_P256_POINT; i++)
        pub[i] = (u8)i;              /* an arbitrary 65-byte point placeholder */
    pub[0] = 0x04;

    mmo_wbuf pkt, frame;
    mmo_wbuf_init(&pkt);
    mmo_wbuf_init(&frame);
    mmo_hs_write_client_ready(&pkt, pub);
    CHECK(!pkt.err && pkt.len == 1 + 2 + MMO_P256_POINT, "packet is opcode + u16 + 65");
    CHECK(pkt.data[0] == MMO_HS_CLIENT_READY && pkt.data[1] == 65 && pkt.data[2] == 0
              && memcmp(pkt.data + 3, pub, MMO_P256_POINT) == 0,
          "ClientReady body is 02 | 41 00 | point");
    mmo_frame_put(&frame, pkt.data, pkt.len);
    /* frame length includes its own 2 bytes: 1+2+65 body + 2 = 70 = 0x0046 */
    CHECK(!frame.err && frame.len == 70 && frame.data[0] == 0x46 && frame.data[1] == 0x00,
          "framed ClientReady is 70 bytes, prefix 46 00");
    mmo_wbuf_free(&pkt);
    mmo_wbuf_free(&frame);
}

/* --- tripleHash KDF and AES-CTR derivation ----------------------------- */
/* Fixed ECDH shared secret (reused from the P-256 OpenSSL vector). */
static const char *SECRET =
    "e2678ba661f6b918a266600fb9073d5a838b6ac96bf2fe009120872b64aa5158";
/* SHA-256(salt||secret||salt)[0:16] for the two key salts (Python oracle). */
static const char *CLIENT_SEED = "19ba558088831359998268d46868e8f7";
static const char *SERVER_SEED = "b27e27f532fab584b4fc57c4a3329d8d";
/* AES-128-CTR(key=clientSeed, iv=tripleHash(clientSeed,"IVDERIV")) over the
 * plaintext below, the C->S stream (OpenSSL oracle). */
static const char *CTR_PLAINTEXT =
    "the quick brown fox jumps over the lazy dog";
static const char *CTR_CIPHERTEXT =
    "b9e5e2ad0b62e1779c625d3306dbf066b70a5c4319e2b3f895ad21077fe91b31"
    "d9e2c21bf90e35e903f974";

static void test_derive(void)
{
    printf("session derive (CLIENT role):\n");
    u8 secret[MMO_P256_SCALAR];
    unhex(SECRET, secret, sizeof secret);
    u8 cseed[MMO_HS_SEED], sseed[MMO_HS_SEED];
    unhex(CLIENT_SEED, cseed, sizeof cseed);
    unhex(SERVER_SEED, sseed, sizeof sseed);

    mmo_session_crypto c;
    mmo_session_derive(&c, secret, 16);
    CHECK(memcmp(c.out_seed, cseed, MMO_HS_SEED) == 0, "outgoing seed == clientSeed");
    CHECK(memcmp(c.in_seed, sseed, MMO_HS_SEED) == 0, "incoming seed == serverSeed");
    CHECK(c.checksum_size == 16 && c.out_round == 0 && c.in_round == 0,
          "checksum size carried, round counters start at 0");

    /* The outgoing keystream must match the server's C->S cipher. */
    u8 ct[64], expect[64];
    size_t n = strlen(CTR_PLAINTEXT);
    size_t elen = unhex(CTR_CIPHERTEXT, expect, sizeof expect);
    mmo_aes128ctr_xor(&c.enc, CTR_PLAINTEXT, ct, n);
    CHECK(n == elen && memcmp(ct, expect, n) == 0,
          "outgoing AES-CTR stream matches the OpenSSL C->S ciphertext");
}

/* --- a real ServerHello, decoded and driven through the handshake ------ */
/* Captured live from the login server; its signature verifies under the pinned
 * root (confirmed with openssl before freezing). Body only (frame prefix removed). */
static const char *SERVER_HELLO_BODY =
    "014100040c38ac53e0f2f0a801f2303e40975ea1dc2a86527fd4e3593fc80d999"
    "e22e5a60557ae5afedb1d820fef8d2e06ec6db3dd1050b7a77057dee7b9829dd4"
    "fe85f746003044022024a95e17ec8692f0b36c5dbbdc2d595abd39b86bf109828"
    "56e2388366c344e0a02201d7f80f9511b057c37ca0ef3442993ccdd3e188115ea"
    "39afd20286082b19c8c010";

static void test_server_hello_decode(void)
{
    printf("server hello decode:\n");
    u8 body[256];
    size_t blen = unhex(SERVER_HELLO_BODY, body, sizeof body);

    mmo_rbuf r;
    mmo_rbuf_init(&r, body, blen);
    mmo_server_hello sh;
    int rc = mmo_hs_read_server_hello(&r, &sh);
    CHECK(rc == 0, "ServerHello parses");
    CHECK(sh.checksum_size == 16, "checksum size is 16 (login profile)");
    CHECK(sh.siglen == 70 && sh.signature[0] == 0x30, "signature is a 70-byte DER SEQUENCE");
    CHECK(sh.ephemeral_pub[0] == 0x04, "ephemeral point is uncompressed");
    /* The captured signature verifies against the pinned root over its point. */
    CHECK(mmo_p256_ecdsa_verify(mmo_root_pubkey, sh.ephemeral_pub, MMO_P256_POINT,
                                sh.signature, sh.siglen) == 1,
          "captured signature verifies under the pinned root key");
}

static void test_handshake_driver(void)
{
    printf("handshake driver:\n");
    u8 body[256];
    size_t blen = unhex(SERVER_HELLO_BODY, body, sizeof body);

    /* A session mid-handshake with a fixed valid ephemeral scalar (reuse the
     * P-256 vector's private key) so the driver's ECDH has a real input. */
    static const char *EPH_PRIV =
        "21206934226a54438cb4b2b7e7173c6288aaf931479eb5ec40dcdf94ef24245d";
    mmo_session s;
    memset(&s, 0, sizeof s);
    unhex(EPH_PRIV, s.eph_priv, MMO_P256_SCALAR);
    mmo_p256_derive_pub(s.eph_priv, s.eph_pub);
    s.phase = MMO_SESS_HELLO_SENT;

    mmo_wbuf out;
    mmo_wbuf_init(&out);
    int rc = mmo_session_on_server_hello(&s, body, blen, &out);
    CHECK(rc == 0 && s.phase == MMO_SESS_ESTABLISHED,
          "valid ServerHello -> ESTABLISHED, ClientReady emitted");
    CHECK(out.len == 70 && out.data[0] == 0x46 && out.data[2] == MMO_HS_CLIENT_READY,
          "emitted a 70-byte framed ClientReady");
    /* The ClientReady must carry our own ephemeral point. */
    CHECK(memcmp(out.data + 5, s.eph_pub, MMO_P256_POINT) == 0,
          "ClientReady carries the client ephemeral point");
    CHECK(s.crypto.checksum_size == 16, "session adopted the server's checksum size");
    mmo_wbuf_free(&out);

    /* A checksum size that names no profile has to stop the handshake. The size
     * is the last byte and sits outside the signed point, so only it changes:
     * 3 falls between the keyless CRC and the shortest keyed tag, and a session
     * that carried on would send every frame untagged. */
    u8 nosize[256];
    memcpy(nosize, body, blen);
    nosize[blen - 1] = 3;
    mmo_session s3;
    memset(&s3, 0, sizeof s3);
    unhex(EPH_PRIV, s3.eph_priv, MMO_P256_SCALAR);
    mmo_p256_derive_pub(s3.eph_priv, s3.eph_pub);
    s3.phase = MMO_SESS_HELLO_SENT;
    mmo_wbuf out3;
    mmo_wbuf_init(&out3);
    rc = mmo_session_on_server_hello(&s3, nosize, blen, &out3);
    CHECK(rc == -1 && s3.phase == MMO_SESS_FAILED && out3.len == 0,
          "a checksum size of 3 is refused, no ClientReady sent");
    CHECK(strstr(s3.errmsg, "checksum size 3") != NULL,
          "the failure says which size it refused");
    mmo_wbuf_free(&out3);

    /* A tampered signature must be rejected, not accepted. */
    body[blen - 20] ^= 0xff;         /* flip a byte inside the DER signature */
    mmo_session s2;
    memset(&s2, 0, sizeof s2);
    unhex(EPH_PRIV, s2.eph_priv, MMO_P256_SCALAR);
    mmo_p256_derive_pub(s2.eph_priv, s2.eph_pub);
    s2.phase = MMO_SESS_HELLO_SENT;
    mmo_wbuf out2;
    mmo_wbuf_init(&out2);
    rc = mmo_session_on_server_hello(&s2, body, blen, &out2);
    CHECK(rc == -1 && s2.phase == MMO_SESS_FAILED && out2.len == 0,
          "tampered ServerHello signature is rejected, no ClientReady sent");
    mmo_wbuf_free(&out2);
}

int session_tests_run(void)
{
    failures = 0;
    test_client_hello();
    test_client_ready();
    test_derive();
    test_server_hello_decode();
    test_handshake_driver();
    return failures;
}
