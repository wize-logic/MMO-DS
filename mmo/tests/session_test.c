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
/* Captured live from the login server, back when the signature covered the
 * ephemeral point alone. Body only (frame prefix removed). */
static const char *SERVER_HELLO_BODY =
    "014100040c38ac53e0f2f0a801f2303e40975ea1dc2a86527fd4e3593fc80d999"
    "e22e5a60557ae5afedb1d820fef8d2e06ec6db3dd1050b7a77057dee7b9829dd4"
    "fe85f746003044022024a95e17ec8692f0b36c5dbbdc2d595abd39b86bf109828"
    "56e2388366c344e0a02201d7f80f9511b057c37ca0ef3442993ccdd3e188115ea"
    "39afd20286082b19c8c010";

/* The hello timestamp the two signed vectors below were minted for; it is the
 * one the pinned ClientHello frame above carries. */
#define HELLO_TS ((s64)1700000000000LL)

/* The same captured point, signed by `openssl dgst -sha256 -sign` under the
 * server's game.private.pem, the key mmo_root_pubkey is the public half of, 
 * over (point || checksum size || big-endian HELLO_TS). */
static const char *SERVER_HELLO_BOUND16 =
    "014100040c38ac53e0f2f0a801f2303e40975ea1dc2a86527fd4e3593fc80d999"
    "e22e5a60557ae5afedb1d820fef8d2e06ec6db3dd1050b7a77057dee7b9829dd4"
    "fe85f747003045022100d84392457183782c259463a1ed49ef3c6a1ac30adbe89"
    "21f6067b63305d5c2a902205ec135670fd02bb9ddb7f3bf0d55e44643abae18d5"
    "d6f02d3eeb80105589e76910";

/* The same, minted for checksum size 2: a ServerHello whose signature is
 * perfectly good and which still has to be refused, because a keyless CRC-16
 * tag does not say who wrote the frame. */
static const char *SERVER_HELLO_BOUND2 =
    "014100040c38ac53e0f2f0a801f2303e40975ea1dc2a86527fd4e3593fc80d999"
    "e22e5a60557ae5afedb1d820fef8d2e06ec6db3dd1050b7a77057dee7b9829dd4"
    "fe85f748003046022100d85ed100ed7e7b9fa9c44cef0afaf111281c3d459d13a"
    "00648bb310e20024aa4022100b68786e5617ebe21b6c5f83f047b3ad9821f3db3"
    "d4be7a27c4b9b55c74235c5502";

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
    /* The captured signature still verifies over the point on its own, the
     * server did sign that once. What has changed is that the point on its own
     * is no longer what a ServerHello is signed over. */
    CHECK(mmo_p256_ecdsa_verify(mmo_root_pubkey, sh.ephemeral_pub, MMO_P256_POINT,
                                sh.signature, sh.siglen) == 1,
          "captured signature verifies under the pinned root over the point alone");
}

/* --- what the signature covers ----------------------------------------- */

static void test_signed_bytes(void)
{
    printf("the bytes a ServerHello signature covers:\n");
    u8 pub[MMO_P256_POINT];
    for (size_t i = 0; i < sizeof pub; i++)
        pub[i] = (u8)i;

    u8 got[MMO_HS_SIGNED];
    mmo_hs_signed_bytes(pub, 16, (s64)0x0102030405060708LL, got);
    CHECK(sizeof got == MMO_P256_POINT + 9, "the span is the point, a size byte and 8 more");
    CHECK(memcmp(got, pub, MMO_P256_POINT) == 0, "the point comes first, verbatim");
    CHECK(got[MMO_P256_POINT] == 16, "the checksum size byte follows it");
    static const u8 be[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    CHECK(memcmp(got + MMO_P256_POINT + 1, be, 8) == 0,
          "the hello timestamp is last, big-endian");
}

/* A session mid-handshake with a fixed valid ephemeral scalar (reuse the P-256
 * vector's private key) so the driver's ECDH has a real input, and having said
 * `ts` in its ClientHello. */
static void hello_sent(mmo_session *s, s64 ts)
{
    static const char *EPH_PRIV =
        "21206934226a54438cb4b2b7e7173c6288aaf931479eb5ec40dcdf94ef24245d";
    memset(s, 0, sizeof *s);
    unhex(EPH_PRIV, s->eph_priv, MMO_P256_SCALAR);
    mmo_p256_derive_pub(s->eph_priv, s->eph_pub);
    s->hello_timestamp = ts;
    s->phase = MMO_SESS_HELLO_SENT;
}

static void test_handshake_driver(void)
{
    printf("handshake driver:\n");
    u8 body[256];
    size_t blen = unhex(SERVER_HELLO_BOUND16, body, sizeof body);

    mmo_session s;
    hello_sent(&s, HELLO_TS);

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

    /* The size byte is inside the signature now. Rewriting it in flight, the
     * downgrade a plaintext handshake frame used to invite, no longer reads as
     * a differently negotiated session, it reads as a bad signature. */
    u8 downgrade[256];
    memcpy(downgrade, body, blen);
    downgrade[blen - 1] = 2;
    mmo_session sd;
    hello_sent(&sd, HELLO_TS);
    mmo_wbuf outd;
    mmo_wbuf_init(&outd);
    rc = mmo_session_on_server_hello(&sd, downgrade, blen, &outd);
    CHECK(rc == -1 && sd.phase == MMO_SESS_FAILED && outd.len == 0,
          "the checksum size cannot be rewritten in flight: signature refused");
    mmo_wbuf_free(&outd);

    /* And a client that said a different millisecond refuses the same frame, so
     * a ServerHello recorded from one session cannot be replayed into another. */
    mmo_session sr;
    hello_sent(&sr, HELLO_TS + 1);
    mmo_wbuf outr;
    mmo_wbuf_init(&outr);
    rc = mmo_session_on_server_hello(&sr, body, blen, &outr);
    CHECK(rc == -1 && sr.phase == MMO_SESS_FAILED && outr.len == 0,
          "a ServerHello for another hello timestamp is refused");
    mmo_wbuf_free(&outr);

    /* The signature the server used to produce covered the point alone, so the
     * live capture no longer opens a session however genuine it is. */
    u8 old[256];
    size_t olen = unhex(SERVER_HELLO_BODY, old, sizeof old);
    mmo_session so;
    hello_sent(&so, HELLO_TS);
    mmo_wbuf outo;
    mmo_wbuf_init(&outo);
    rc = mmo_session_on_server_hello(&so, old, olen, &outo);
    CHECK(rc == -1 && so.phase == MMO_SESS_FAILED && outo.len == 0,
          "a signature over the point alone no longer opens a session");
    mmo_wbuf_free(&outo);

    /* A correctly signed size under the keyed minimum is refused on its own
     * merits: the frame tag would be the keyless CRC-16, which says nothing
     * about who wrote the frame. */
    u8 weak[256];
    size_t wlen = unhex(SERVER_HELLO_BOUND2, weak, sizeof weak);
    mmo_session s2;
    hello_sent(&s2, HELLO_TS);
    mmo_wbuf out2;
    mmo_wbuf_init(&out2);
    rc = mmo_session_on_server_hello(&s2, weak, wlen, &out2);
    CHECK(rc == -1 && s2.phase == MMO_SESS_FAILED && out2.len == 0,
          "a signed checksum size of 2 is refused, no ClientReady sent");
    CHECK(strstr(s2.errmsg, "checksum size 2") != NULL &&
          strstr(s2.errmsg, "keyed minimum") != NULL,
          "the failure says which size it refused and why");
    mmo_wbuf_free(&out2);

    /* A tampered signature must be rejected, not accepted. */
    body[blen - 20] ^= 0xff;         /* flip a byte inside the DER signature */
    mmo_session s3;
    hello_sent(&s3, HELLO_TS);
    mmo_wbuf out3;
    mmo_wbuf_init(&out3);
    rc = mmo_session_on_server_hello(&s3, body, blen, &out3);
    CHECK(rc == -1 && s3.phase == MMO_SESS_FAILED && out3.len == 0,
          "tampered ServerHello signature is rejected, no ClientReady sent");
    mmo_wbuf_free(&out3);
}

int session_tests_run(void)
{
    failures = 0;
    test_client_hello();
    test_client_ready();
    test_derive();
    test_server_hello_decode();
    test_signed_bytes();
    test_handshake_driver();
    return failures;
}
