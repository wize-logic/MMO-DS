/* The MonMMO transport session handshake. See session.h. */
#include "session.h"

#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

/*
 * The pinned server root public key (uncompressed P-256 point). Every ServerHello's signature
 * is checked against this before its ephemeral point is trusted, and there is no way to point
 * a session at another one: a client belongs to the world it was built for.
 */
#include "endpoint_pin.h"

const u8 mmo_root_pubkey[MMO_P256_POINT] = { OPENMMO_PIN_ROOT_KEY };

/* ClientHello obfuscation keys (HandshakePackets.kt). */
#define XOR_KEY_RANDOM    ((s64)0x2c9ca13689db65c8LL)
#define XOR_KEY_TIMESTAMP ((s64)0xc5828cd837901279LL)

/* --- packet codecs ----------------------------------------------------- */

void mmo_hs_write_client_hello(mmo_wbuf *pkt, s64 timestamp, s64 random)
{
    mmo_put_u8(pkt, MMO_HS_CLIENT_HELLO);
    mmo_put_s64le(pkt, random ^ XOR_KEY_RANDOM);
    mmo_put_s64le(pkt, timestamp ^ XOR_KEY_TIMESTAMP ^ random);
}

void mmo_hs_write_client_ready(mmo_wbuf *pkt, const u8 pub[MMO_P256_POINT])
{
    mmo_put_u8(pkt, MMO_HS_CLIENT_READY);
    mmo_put_u16le(pkt, MMO_P256_POINT);
    mmo_put_bytes(pkt, pub, MMO_P256_POINT);
}

int mmo_hs_read_server_hello(mmo_rbuf *r, mmo_server_hello *out)
{
    if (mmo_get_u8(r) != MMO_HS_SERVER_HELLO)
        return -1;

    u16 publen = mmo_get_u16le(r);
    if (publen != MMO_P256_POINT)
        return -1;
    mmo_get_bytes(r, out->ephemeral_pub, MMO_P256_POINT);

    u16 siglen = mmo_get_u16le(r);
    if (siglen > MMO_HS_MAX_SIG)
        return -1;
    out->siglen = siglen;
    mmo_get_bytes(r, out->signature, siglen);

    out->checksum_size = mmo_get_u8(r);

    return r->err ? -1 : 0;
}

/* --- key derivation ---------------------------------------------------- */

/* tripleHash(secret, salt) = SHA-256(salt || secret || salt)[0:16]. */
static void triple_hash(const u8 *secret, size_t secret_len,
                        const u8 *salt, size_t salt_len,
                        u8 out[MMO_HS_SEED])
{
    u8 digest[MMO_SHA256_DIGEST];
    mmo_sha256_ctx c;
    mmo_sha256_init(&c);
    mmo_sha256_update(&c, salt, salt_len);
    mmo_sha256_update(&c, secret, secret_len);
    mmo_sha256_update(&c, salt, salt_len);
    mmo_sha256_final(&c, digest);
    memcpy(out, digest, MMO_HS_SEED);
}

/* Shared derivation for both roles. The client and server agree the same two
 * seeds; they differ only in which one enciphers their outbound stream. `server`
 * selects the role: outbound uses the server seed for a server peer, the client
 * seed for a client peer, and inbound is the mirror. */
static void derive_role(mmo_session_crypto *c,
                        const u8 secret[MMO_P256_SCALAR],
                        u8 checksum_size, int server)
{
    static const u8 client_salt[8] = {'K', 'e', 'y', 'S', 'a', 'l', 't', 0x01};
    static const u8 server_salt[8] = {'K', 'e', 'y', 'S', 'a', 'l', 't', 0x02};
    static const u8 iv_salt[7]     = {'I', 'V', 'D', 'E', 'R', 'I', 'V'};

    u8 client_seed[MMO_HS_SEED], server_seed[MMO_HS_SEED];
    triple_hash(secret, MMO_P256_SCALAR, client_salt, sizeof client_salt, client_seed);
    triple_hash(secret, MMO_P256_SCALAR, server_salt, sizeof server_salt, server_seed);

    /* The outbound seed is this peer's own; the inbound seed is the other's. The
     * AES key is the seed; the iv is tripleHash(seed, "IVDERIV"). */
    const u8 *out_seed = server ? server_seed : client_seed;
    const u8 *in_seed  = server ? client_seed : server_seed;

    u8 out_iv[MMO_AES_BLOCK], in_iv[MMO_AES_BLOCK];
    triple_hash(out_seed, MMO_HS_SEED, iv_salt, sizeof iv_salt, out_iv);
    triple_hash(in_seed, MMO_HS_SEED, iv_salt, sizeof iv_salt, in_iv);

    mmo_aes128ctr_init(&c->enc, out_seed, out_iv);
    mmo_aes128ctr_init(&c->dec, in_seed, in_iv);
    memcpy(c->out_seed, out_seed, MMO_HS_SEED);
    memcpy(c->in_seed, in_seed, MMO_HS_SEED);
    c->out_round = 0;
    c->in_round = 0;
    c->checksum_size = checksum_size;
}

void mmo_session_derive(mmo_session_crypto *c,
                        const u8 secret[MMO_P256_SCALAR],
                        u8 checksum_size)
{
    derive_role(c, secret, checksum_size, 0);
}

void mmo_session_derive_server(mmo_session_crypto *c,
                               const u8 secret[MMO_P256_SCALAR],
                               u8 checksum_size)
{
    derive_role(c, secret, checksum_size, 1);
}

/* --- frame checksums ---------------------------------------------------- */

size_t mmo_session_checksum_out(mmo_session_crypto *c,
                                const u8 *data, size_t n,
                                u8 tag[MMO_CHECKSUM_MAX])
{
    return mmo_checksum_calc(c->checksum_size, c->out_seed, &c->out_round,
                             data, n, tag);
}

int mmo_session_checksum_in(mmo_session_crypto *c,
                            const u8 *data, size_t n,
                            const u8 *tag, size_t taglen)
{
    return mmo_checksum_verify(c->checksum_size, c->in_seed, &c->in_round,
                               data, n, tag, taglen);
}

/* --- post-handshake application frames ---------------------------------- */

void mmo_session_send_app(mmo_session_crypto *c, u8 opcode,
                          const u8 *body, size_t bodylen, mmo_wbuf *out)
{
    /* payload = ciphertext(opcode||body) followed by its checksum tag. */
    size_t ctlen = bodylen + 1;
    u8 *payload = malloc(ctlen + MMO_CHECKSUM_MAX);
    if (!payload) {
        out->err = 1;
        return;
    }
    /* One continuous C->S keystream over opcode then body. */
    mmo_aes128ctr_xor(&c->enc, &opcode, payload, 1);
    if (bodylen)
        mmo_aes128ctr_xor(&c->enc, body, payload + 1, bodylen);

    size_t taglen = mmo_session_checksum_out(c, payload, ctlen, payload + ctlen);
    mmo_frame_put(out, payload, ctlen + taglen);
    free(payload);
}

size_t mmo_session_recv_app(mmo_session_crypto *c,
                            const u8 *payload, size_t n,
                            u8 *plain, size_t cap)
{
    size_t taglen = c->checksum_size;
    if (n < taglen)
        return (size_t)-1;
    size_t ctlen = n - taglen;
    if (ctlen > cap)
        return (size_t)-1;
    /* Verify the tag over the ciphertext before deciphering, matching the
     * server's decode order; a mismatch leaves the keystream untouched. */
    if (mmo_session_checksum_in(c, payload, ctlen, payload + ctlen, taglen) != 0)
        return (size_t)-1;
    mmo_aes128ctr_xor(&c->dec, payload, plain, ctlen);
    return ctlen;
}

/* --- the handshake driver ---------------------------------------------- */

/* Fill buf with OS entropy, whatever this machine calls one (platform.h).
 * Returns 0, or -1 when it will not supply any; the caller then fails loudly
 * rather than proceeding with a predictable key. */
static int os_random(u8 *buf, size_t n)
{
    return mmo_plat_random(buf, n);
}

static void fail(mmo_session *s, const char *msg)
{
    s->phase = MMO_SESS_FAILED;
    snprintf(s->errmsg, sizeof s->errmsg, "%s", msg);
}

/* Shared tail of both start paths: the ephemeral private key is already in
 * s->eph_priv; derive its public point, obfuscate (random, timestamp) into a
 * ClientHello and append the framed plaintext to `out`. Returns 0 / HELLO_SENT,
 * or -1 / FAILED if the scalar yields no public point or the frame won't build. */
static int start_from_priv(mmo_session *s, mmo_wbuf *out,
                           s64 random, s64 timestamp)
{
    if (mmo_p256_derive_pub(s->eph_priv, s->eph_pub) != 0) {
        fail(s, "could not derive an ephemeral keypair");
        return -1;
    }

    mmo_wbuf pkt;
    mmo_wbuf_init(&pkt);
    mmo_hs_write_client_hello(&pkt, timestamp, random);
    mmo_frame_put(out, pkt.data, pkt.len);
    int err = pkt.err || out->err;
    mmo_wbuf_free(&pkt);
    if (err) {
        fail(s, "failed to build ClientHello");
        return -1;
    }

    s->phase = MMO_SESS_HELLO_SENT;
    return 0;
}

int mmo_session_start(mmo_session *s, mmo_wbuf *out)
{
    memset(s, 0, sizeof *s);

    /* Reject-sample a valid ephemeral scalar: derive_pub fails a zero or
     * out-of-range scalar, so retry a few times before giving up. */
    int ok = 0;
    for (int tries = 0; tries < 8; tries++) {
        if (os_random(s->eph_priv, MMO_P256_SCALAR) != 0) {
            fail(s, "no OS entropy for ephemeral key");
            return -1;
        }
        if (mmo_p256_derive_pub(s->eph_priv, s->eph_pub) == 0) {
            ok = 1;
            break;
        }
    }
    if (!ok) {
        fail(s, "could not derive an ephemeral keypair");
        return -1;
    }

    /* A fresh random and the current time (Unix ms) obfuscate into ClientHello. */
    u8 rnd[8];
    if (os_random(rnd, sizeof rnd) != 0) {
        fail(s, "no OS entropy for hello nonce");
        return -1;
    }
    s64 random = 0;
    for (int i = 0; i < 8; i++)
        random |= (s64)rnd[i] << (8 * i);

    struct timeval tv;
    gettimeofday(&tv, NULL);
    s64 timestamp = (s64)tv.tv_sec * 1000 + tv.tv_usec / 1000;

    return start_from_priv(s, out, random, timestamp);
}

int mmo_session_start_seeded(mmo_session *s, mmo_wbuf *out,
                             const u8 eph_priv[MMO_P256_SCALAR],
                             s64 random, s64 timestamp)
{
    memset(s, 0, sizeof *s);
    memcpy(s->eph_priv, eph_priv, MMO_P256_SCALAR);
    return start_from_priv(s, out, random, timestamp);
}

int mmo_session_on_server_hello(mmo_session *s,
                                const void *payload, size_t n,
                                mmo_wbuf *out)
{
    if (s->phase != MMO_SESS_HELLO_SENT) {
        fail(s, "ServerHello arrived out of sequence");
        return -1;
    }

    mmo_rbuf r;
    mmo_rbuf_init(&r, payload, n);
    mmo_server_hello sh;
    if (mmo_hs_read_server_hello(&r, &sh) != 0) {
        fail(s, "malformed ServerHello");
        return -1;
    }

    /*
     * The signature covers the 65-byte ephemeral point; verify it under the pinned root (or
     * the caller's override) before trusting the point.
     */
    const u8 *root = s->root_pub ? s->root_pub : mmo_root_pubkey;
    if (!mmo_p256_ecdsa_verify(root,
                               sh.ephemeral_pub, MMO_P256_POINT,
                               sh.signature, sh.siglen)) {
        fail(s, "ServerHello signature does not verify: this client was built for another server");
        return -1;
    }

    /* A size outside the negotiable profiles cannot be tagged at all, so stop
     * here rather than opening a stream whose every frame is silently untagged. */
    if (!mmo_checksum_supported(sh.checksum_size)) {
        char msg[80];
        snprintf(msg, sizeof msg,
                 "ServerHello asks for checksum size %u, which is no profile",
                 (unsigned)sh.checksum_size);
        fail(s, msg);
        return -1;
    }

    u8 secret[MMO_P256_SCALAR];
    if (mmo_p256_ecdh(s->eph_priv, sh.ephemeral_pub, secret) != 0) {
        fail(s, "ECDH with the server ephemeral point failed");
        return -1;
    }

    mmo_session_derive(&s->crypto, secret, sh.checksum_size);

    /* ClientReady is still sent under the plaintext cipher/checksum; the swap to
     * the derived crypto takes effect for every frame after it. */
    mmo_wbuf pkt;
    mmo_wbuf_init(&pkt);
    mmo_hs_write_client_ready(&pkt, s->eph_pub);
    mmo_frame_put(out, pkt.data, pkt.len);
    int err = pkt.err || out->err;
    mmo_wbuf_free(&pkt);
    if (err) {
        fail(s, "failed to build ClientReady");
        return -1;
    }

    s->phase = MMO_SESS_ESTABLISHED;
    return 0;
}
