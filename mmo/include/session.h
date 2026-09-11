/* The OpenMMO transport session handshake and its crypto state. */
#ifndef MMO_SESSION_H
#define MMO_SESSION_H

#include <stddef.h>

#include "mmo.h"
#include "codec.h"
#include "crypto.h"
#include "checksum.h"
#include "p256.h"

/* Handshake opcodes (SessionHandshakeProtocol). */
#define MMO_HS_CLIENT_HELLO 0x00
#define MMO_HS_SERVER_HELLO 0x01
#define MMO_HS_CLIENT_READY 0x02

/* Fixed sizes on the wire. */
#define MMO_HS_HELLO_BODY   16   /* two S64LE fields: field1, field2 */
#define MMO_HS_SEED         16   /* a directional session seed / AES-128 key */
#define MMO_HS_MAX_SIG      72   /* max DER ECDSA-P256 signature (2x 33B INTs) */

/*
 * What the ServerHello's ECDSA signature covers, at fixed widths so the concatenation cannot
 * be re-split (HandshakeSignature.kt is the same layout):
 */
#define MMO_HS_SIGNED      (MMO_P256_POINT + 1 + 8)

/* The shortest keyed checksum a session may negotiate. Below it a frame's tag is
 * absent (0) or the keyless CRC-16 (2); both are real checksum profiles and
 * neither proves who wrote the frame, so a client refuses to open a session on
 * one however correctly the ServerHello was signed. */
#define MMO_HS_MIN_CHECKSUM 4

/* The pinned OpenMMO server root public key: the uncompressed P-256 point of the
 * key in the server's game.private.pem, used to authenticate every ServerHello.
 * A ServerHello whose signature does not verify under this key is rejected. */
extern const u8 mmo_root_pubkey[MMO_P256_POINT];

/* --- handshake packet codecs (opcode + body, unframed) ----------------- */

/* Write a ClientHello packet (opcode 0x00 + 16-byte body) into pkt. */
void mmo_hs_write_client_hello(mmo_wbuf *pkt, s64 timestamp, s64 random);

/* Write a ClientReady packet (opcode 0x02 + u16-le length 65 + the uncompressed
 * point) into pkt. */
void mmo_hs_write_client_ready(mmo_wbuf *pkt, const u8 pub[MMO_P256_POINT]);

/* A parsed ServerHello. */
typedef struct {
    u8     ephemeral_pub[MMO_P256_POINT]; /* server ephemeral point (signed) */
    u8     signature[MMO_HS_MAX_SIG];     /* DER ECDSA over MMO_HS_SIGNED bytes */
    size_t siglen;
    u8     checksum_size;                 /* selects the post-handshake checksum */
} mmo_server_hello;

/* Lay out the MMO_HS_SIGNED bytes a ServerHello's signature is taken over, from
 * the point and size it carries and the timestamp the client's own ClientHello
 * sent. Exposed so a test can build the same bytes an oracle signed. */
void mmo_hs_signed_bytes(const u8 pub[MMO_P256_POINT], u8 checksum_size,
                         s64 hello_timestamp, u8 out[MMO_HS_SIGNED]);

/* Parse a ServerHello from the frame body at r (opcode included). Returns 0 on
 * success, -1 if the opcode is wrong, a field runs past the span, or the
 * signature is longer than MMO_HS_MAX_SIG. */
int mmo_hs_read_server_hello(mmo_rbuf *r, mmo_server_hello *out);

/* --- session crypto (derived on ServerHello) --------------------------- */

/* The per-direction crypto the session hot-swaps in after ClientReady. Each
 * direction has one continuous AES-128-CTR stream (never reset) and the 16-byte
 * seed that also keys its checksum; the round counters belong to the HMAC
 * checksum (2.7) and start at zero. */
typedef struct {
    mmo_aes128ctr enc;                 /* outgoing C->S keystream */
    mmo_aes128ctr dec;                 /* incoming S->C keystream */
    u8            out_seed[MMO_HS_SEED]; /* C->S checksum key (== enc AES key) */
    u8            in_seed[MMO_HS_SEED];  /* S->C checksum key (== dec AES key) */
    u32           out_round;           /* C->S HMAC round counter */
    u32           in_round;            /* S->C HMAC round counter */
    u8            checksum_size;
} mmo_session_crypto;

/* Derive the CLIENT-role session crypto from the 32-byte ECDH shared secret (the
 * big-endian X coordinate, exactly what mmo_p256_ecdh yields). Uses tripleHash
 * with the "KeySalt"+0x01 / "KeySalt"+0x02 and "IVDERIV" salts. */
void mmo_session_derive(mmo_session_crypto *c,
                        const u8 secret[MMO_P256_SCALAR],
                        u8 checksum_size);

/*
 * Derive the server-role session crypto from the same shared secret. Identical to
 * mmo_session_derive but with the two directions swapped: outgoing (S->C) uses the server
 * seed, incoming (C->S) the client seed.
 */
void mmo_session_derive_server(mmo_session_crypto *c,
                               const u8 secret[MMO_P256_SCALAR],
                               u8 checksum_size);

/* --- frame checksums, selected by the negotiated session profile ------- */

/*
 * Compute the outbound (C->S) frame checksum tag for the ciphertext `data` under the session's
 * negotiated profile, advancing the C->S round counter for the HMAC profile. Writes the tag to
 * `tag` and returns its length (0, 2 or the negotiated HMAC size).
 */
size_t mmo_session_checksum_out(mmo_session_crypto *c,
                                const u8 *data, size_t n,
                                u8 tag[MMO_CHECKSUM_MAX]);

/* Verify an inbound (S->C) frame checksum `tag` (`taglen` bytes) over the
 * ciphertext `data`, advancing the S->C round counter to stay in lockstep with
 * the server's outbound counter. Returns 0 on a match, -1 otherwise. */
int mmo_session_checksum_in(mmo_session_crypto *c,
                            const u8 *data, size_t n,
                            const u8 *tag, size_t taglen);

/* --- post-handshake application frames (the encrypted envelope) -------- */

/* Frame one outbound application packet and append it to `out`. */
void mmo_session_send_app(mmo_session_crypto *c, u8 opcode,
                          const u8 *body, size_t bodylen, mmo_wbuf *out);

/*
 * Open one inbound application frame. `payload` / `n` is the frame body a successful
 * mmo_frame_get yields, the ciphertext followed by its checksum tag.
 */
size_t mmo_session_recv_app(mmo_session_crypto *c,
                            const u8 *payload, size_t n,
                            u8 *plain, size_t cap);

/* --- the handshake driver (plaintext -> encrypted hot-swap) ------------ */

typedef enum {
    MMO_SESS_INIT = 0,   /* nothing sent yet */
    MMO_SESS_HELLO_SENT, /* ClientHello framed and handed out; awaiting ServerHello */
    MMO_SESS_ESTABLISHED,/* ClientReady sent, crypto installed */
    MMO_SESS_FAILED      /* handshake rejected; see errmsg */
} mmo_session_phase;

typedef struct {
    mmo_session_phase  phase;
    u8                 eph_priv[MMO_P256_SCALAR];
    u8                 eph_pub[MMO_P256_POINT];
    /* The timestamp this session's ClientHello carried. The ServerHello's
     * signature is taken over it, so a session that does not remember what it
     * said cannot check that the answer is its own. */
    s64                hello_timestamp;
    mmo_session_crypto crypto;             /* valid once ESTABLISHED */
    char               errmsg[128];
    /*
     * Optional root key the ServerHello signature is verified against. NULL (the default after
     * mmo_session_start) means the pinned mmo_root_pubkey, the only value production ever
     * uses.
     */
    const u8          *root_pub;
} mmo_session;

/*
 * Begin a handshake: generate the ephemeral keypair and a fresh ClientHello, and append the
 * framed (plaintext) ClientHello to `out`. Reads OS entropy for the ephemeral scalar and the
 * hello random.
 */
int mmo_session_start(mmo_session *s, mmo_wbuf *out);

/*
 * As mmo_session_start, but the ClientHello says `timestamp` (Unix ms) instead of the clock; 0
 * means the clock, so this is mmo_session_start at 0.
 */
int mmo_session_start_at(mmo_session *s, mmo_wbuf *out, s64 timestamp);

/*
 * Begin a handshake with a CALLER-SUPPLIED ephemeral scalar and hello nonce, instead of OS
 * entropy, the seam that makes a whole session byte-reproducible for record/replay (trace.h).
 */
int mmo_session_start_seeded(mmo_session *s, mmo_wbuf *out,
                             const u8 eph_priv[MMO_P256_SCALAR],
                             s64 random, s64 timestamp);

/*
 * Feed the ServerHello frame body (opcode included). Verifies the signature against the pinned
 * root key over MMO_HS_SIGNED, runs ECDH, derives the session crypto, and appends the framed
 * (still plaintext) ClientReady to `out`.
 */
int mmo_session_on_server_hello(mmo_session *s,
                                const void *payload, size_t n,
                                mmo_wbuf *out);

#endif /* MMO_SESSION_H */
