/* The P-256 (secp256r1) public-key operations the handshake needs. */
#ifndef MMO_P256_H
#define MMO_P256_H

#include <stddef.h>

#include "mmo.h"

#define MMO_P256_SCALAR 32   /* a private scalar / X coordinate, big-endian */
#define MMO_P256_POINT  65   /* an uncompressed public point: 0x04 || X || Y */

/* Derive the public point pub = priv*G from a private scalar. `priv` is 32
 * big-endian bytes; `pub` receives the 65-byte uncompressed encoding. Returns
 * 0 on success, -1 if `priv` is not a valid scalar (zero, or >= the group
 * order). */
int mmo_p256_derive_pub(const u8 priv[MMO_P256_SCALAR],
                        u8 pub[MMO_P256_POINT]);

/* ECDH: shared = the big-endian X coordinate of priv*peer_pub. `peer_pub` is a
 * 65-byte uncompressed point. Returns 0 on success, -1 if `priv` is invalid or
 * `peer_pub` is not a valid point on the curve. */
int mmo_p256_ecdh(const u8 priv[MMO_P256_SCALAR],
                  const u8 peer_pub[MMO_P256_POINT],
                  u8 shared[MMO_P256_SCALAR]);

/* Verify a SHA256withECDSA signature (as the JDK produces) over msg[0..msglen]
 * against the pinned public key `pub` (65-byte uncompressed). `sig` is the
 * DER-encoded ECDSA signature (SEQUENCE of two INTEGERs r, s). Returns 1 if the
 * signature is valid, 0 otherwise, including any malformed input. */
int mmo_p256_ecdsa_verify(const u8 pub[MMO_P256_POINT],
                          const void *msg, size_t msglen,
                          const void *sig, size_t siglen);

#endif /* MMO_P256_H */
