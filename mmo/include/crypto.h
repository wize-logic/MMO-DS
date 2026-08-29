/* The symmetric crypto the MonMMO transport envelope needs. */
#ifndef MMO_CRYPTO_H
#define MMO_CRYPTO_H

#include <stddef.h>

#include "mmo.h"

/* --- SHA-256 ----------------------------------------------------------- */

#define MMO_SHA256_DIGEST 32
#define MMO_SHA256_BLOCK  64

typedef struct {
    u32 state[8];
    u64 total;              /* message length in bytes, for the padding */
    u8  buf[MMO_SHA256_BLOCK];
    size_t used;            /* bytes currently buffered in `buf` */
} mmo_sha256_ctx;

void mmo_sha256_init(mmo_sha256_ctx *c);
void mmo_sha256_update(mmo_sha256_ctx *c, const void *data, size_t n);
void mmo_sha256_final(mmo_sha256_ctx *c, u8 out[MMO_SHA256_DIGEST]);

/* One-shot convenience: SHA-256(data[0..n]) into `out`. */
void mmo_sha256(const void *data, size_t n, u8 out[MMO_SHA256_DIGEST]);

/* --- SHA-1 ------------------------------------------------------------- */

/* SHA-1 is not part of the transport envelope; it exists only to hash the
 * account password. The login server stores and compares sha1Hex(password), 
 * the lowercase 40-char hex digest of the UTF-8 password bytes, so the client
 * hashes the plaintext password before putting it on the wire. */

#define MMO_SHA1_DIGEST 20

/* One-shot SHA-1(data[0..n]) into the 20-byte `out`. */
void mmo_sha1(const void *data, size_t n, u8 out[MMO_SHA1_DIGEST]);

/* SHA-1(data[0..n]) as a lowercase hex string: 40 hex chars plus a NUL, so
 * `out` must hold at least 41 bytes. */
void mmo_sha1_hex(const void *data, size_t n, char out[41]);

/* --- HMAC-SHA256 ------------------------------------------------------- */

/* HMAC-SHA256(key, data) into the full 32-byte `out`. The session layer feeds
 * the ciphertext followed by its big-endian round counter and truncates the
 * result itself; this primitive does neither. */
void mmo_hmac_sha256(const void *key, size_t keylen,
                     const void *data, size_t datalen,
                     u8 out[MMO_SHA256_DIGEST]);

/* --- AES-128-CTR ------------------------------------------------------- */

#define MMO_AES128_KEY   16
#define MMO_AES_BLOCK    16

/* A continuous CTR keystream. */
typedef struct {
    u32 rk[44];             /* 11 round keys, 4 words each (AES-128) */
    u8  counter[MMO_AES_BLOCK];
    u8  keystream[MMO_AES_BLOCK];
    size_t offset;          /* bytes of `keystream` already consumed (0..16) */
} mmo_aes128ctr;

void mmo_aes128ctr_init(mmo_aes128ctr *c,
                        const u8 key[MMO_AES128_KEY],
                        const u8 iv[MMO_AES_BLOCK]);
void mmo_aes128ctr_xor(mmo_aes128ctr *c,
                       const void *in, void *out, size_t n);

#endif /* MMO_CRYPTO_H */
