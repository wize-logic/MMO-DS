/* The two frame-checksum profiles the MonMMO transport negotiates. */
#ifndef MMO_CHECKSUM_H
#define MMO_CHECKSUM_H

#include <stddef.h>

#include "mmo.h"

/* The widest tag any profile emits (full HMAC-SHA256); a caller's tag buffer. */
#define MMO_CHECKSUM_MAX 32

/* The directional session seed doubles as the HMAC key; it is 16 bytes. */
#define MMO_CHECKSUM_KEY 16

/* Whether `size` names one of the profiles above: 0, 2, or 4 through 32.
 * Nothing else is a profile, 1, 3 and anything past 32 fall between the
 * keyless and the keyed tag and there is no way to tag a frame under them. */
int mmo_checksum_supported(u8 size);

/* CRC-16/ARC (reflected poly 0xA001, init 0x0000, no final XOR) over data[0..n].
 * Returned as the raw 16-bit sum; the wire tag is its two bytes little-endian. */
u16 mmo_crc16(const u8 *data, size_t n);

/*
 * Compute the frame checksum tag for data[0..n] under the profile selected by `size`, writing
 * it to `tag` and returning its length: size 0 writes nothing, returns 0 size 2 CRC-16/ARC
 * little-endian; `key` and `round` are ignored size 4..32 HMAC-SHA256(key, data ||
 * be32(*round))[0:size]; *round is then incremented, so the next call over the same data
 * differs `key` is the 16-byte directional seed (may be NULL for sizes 0 and 2).
 */
size_t mmo_checksum_calc(u8 size, const u8 *key, u32 *round,
                         const u8 *data, size_t n, u8 tag[MMO_CHECKSUM_MAX]);

/*
 * Verify a received `tag` (`taglen` bytes) over data[0..n] under the same profile, advancing
 * *round for the HMAC profile exactly as calc does (the server increments its verify counter
 * unconditionally, so we must too, even on a mismatch).
 */
int mmo_checksum_verify(u8 size, const u8 *key, u32 *round,
                        const u8 *data, size_t n,
                        const u8 *tag, size_t taglen);

#endif /* MMO_CHECKSUM_H */
