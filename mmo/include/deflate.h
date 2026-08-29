/* The game stream's persistent raw-DEFLATE codec (RFC 1951). */
#ifndef MMO_DEFLATE_H
#define MMO_DEFLATE_H

#include <stddef.h>

#include "mmo.h"

#define MMO_DEFLATE_WINDOW 32768

/* One direction's persistent inflate state: the sliding window plus its cursor.
 * Zero-initialise with mmo_inflate_init before the first segment. */
typedef struct {
    u8  window[MMO_DEFLATE_WINDOW];
    u32 wpos;   /* next write position into the ring, & (WINDOW-1) */
    int full;   /* the ring has wrapped at least once */
} mmo_inflate;

void mmo_inflate_init(mmo_inflate *s);

/*
 * Inflate one compression segment, the flag=1 body exactly as it arrives on the wire, without
 * the stripped `00 00 FF FF` sync marker (this re-appends it, as CompressionDecoder.kt does).
 */
size_t mmo_inflate_segment(mmo_inflate *s, const u8 *seg, size_t seglen,
                           u8 *out, size_t cap);

/* One direction's persistent deflate state. Stored blocks reference no history,
 * so nothing is carried between calls today; the struct exists so the API is
 * per-connection and can gain real compression later without a signature change. */
typedef struct {
    int _reserved;
} mmo_deflate;

void mmo_deflate_init(mmo_deflate *s);

/*
 * Deflate `in` (n bytes) into one raw-DEFLATE stored block followed by an empty Z_SYNC_FLUSH
 * block, with the trailing `00 00 FF FF` marker stripped, matching what CompressionEncoder.kt
 * puts on the wire for a flag=1 packet.
 */
size_t mmo_deflate_segment(mmo_deflate *s, const u8 *in, size_t n,
                           u8 *out, size_t cap);

/*
 * Inflate one complete, self-contained zlib stream (RFC 1950: a 2-byte header, a raw-DEFLATE
 * payload ending in a BFINAL block, then a 4-byte big-endian Adler-32 of the output).
 */
size_t mmo_inflate_zlib(const u8 *in, size_t inlen, u8 *out, size_t cap);

/*
 * Inflate one complete gzip member (RFC 1952: 10-byte header, a raw-DEFLATE payload ending in
 * a BFINAL block, then CRC-32 and ISIZE). Optional extra, name, comment and header-CRC fields
 * are skipped.
 */
size_t mmo_inflate_gzip(const u8 *in, size_t inlen, u8 *out, size_t cap);

#endif /* MMO_DEFLATE_H */
