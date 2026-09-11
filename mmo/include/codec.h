/* Wire buffer primitives and framing for the OpenMMO protocol. */
#ifndef MMO_CODEC_H
#define MMO_CODEC_H

#include <stddef.h>

#include "mmo.h"

/* --- write side -------------------------------------------------------- */

/* A growable little-endian output buffer. All writers become no-ops once `err`
 * is set (an allocation failed); check it once after a packet is built. */
typedef struct {
    u8    *data;
    size_t len;
    size_t cap;
    int    err;
} mmo_wbuf;

void mmo_wbuf_init(mmo_wbuf *w);
void mmo_wbuf_free(mmo_wbuf *w);

void mmo_put_u8(mmo_wbuf *w, u8 v);
void mmo_put_bool(mmo_wbuf *w, int v);
void mmo_put_u16le(mmo_wbuf *w, u16 v);
void mmo_put_s16le(mmo_wbuf *w, s16 v);
void mmo_put_u32le(mmo_wbuf *w, u32 v);
void mmo_put_s32le(mmo_wbuf *w, s32 v);
void mmo_put_s64le(mmo_wbuf *w, s64 v);

/* Raw bytes, no length prefix. */
void mmo_put_bytes(mmo_wbuf *w, const void *src, size_t n);
/* A U8 length prefix (0..255) followed by the raw bytes; sets err if n > 255. */
void mmo_put_bytes_u8(mmo_wbuf *w, const void *src, size_t n);
/* A UTF-16LE string terminated by a NUL-NUL code unit. The input is UTF-8, the
 * client's own encoding everywhere else, and it is transcoded: a code point
 * above the BMP goes out as a surrogate pair, and a malformed byte becomes one
 * U+FFFD, the same substitution the far end's decoder would have made. */
void mmo_put_utf16_nt(mmo_wbuf *w, const char *s);

/*
 * How many UTF-16 code units mmo_put_utf16_nt would write for this string, the NUL-NUL
 * terminator apart.
 */
size_t mmo_utf16_units(const char *s);

/* --- read side --------------------------------------------------------- */

/* A read cursor over a fixed byte span. Readers set `err` on underflow and then
 * yield zero/empty; check it once after a packet is parsed. */
typedef struct {
    const u8 *data;
    size_t    len;
    size_t    pos;
    int       err;
} mmo_rbuf;

void   mmo_rbuf_init(mmo_rbuf *r, const void *data, size_t len);
size_t mmo_rbuf_remaining(const mmo_rbuf *r);

u8  mmo_get_u8(mmo_rbuf *r);
int mmo_get_bool(mmo_rbuf *r);
u16 mmo_get_u16le(mmo_rbuf *r);
s16 mmo_get_s16le(mmo_rbuf *r);
u32 mmo_get_u32le(mmo_rbuf *r);
s32 mmo_get_s32le(mmo_rbuf *r);
s64 mmo_get_s64le(mmo_rbuf *r);

/* Copy n raw bytes into dst; sets err (and leaves dst untouched) if fewer than
 * n remain. */
void mmo_get_bytes(mmo_rbuf *r, void *dst, size_t n);
/* A U8-length-prefixed blob: read the length, copy up to cap bytes into dst,
 * and advance past the whole blob. Returns the on-wire length; sets err if the
 * blob runs past the span or does not fit in cap (dst then holds the prefix). */
size_t mmo_get_bytes_u8(mmo_rbuf *r, void *dst, size_t cap);
/*
 * A UTF-16LE NUL-NUL string transcoded to NUL-terminated UTF-8 in dst, joining surrogate pairs
 * and turning a lone half into U+FFFD.
 */
size_t mmo_get_utf16_nt(mmo_rbuf *r, char *dst, size_t cap);

/* --- framing ----------------------------------------------------------- */

/* The wire frame is a u16-LE length prefix that INCLUDES its own two bytes
 * (Netty LengthFieldPrepender, lengthIncludesLengthFieldLength=true), so the
 * value on the wire is payload length + 2. */

/* Append payload framed (its length prefix, then the bytes) to out. Sets err if
 * the payload is too large to frame (n + 2 > 0xFFFF). */
void mmo_frame_put(mmo_wbuf *out, const void *payload, size_t n);

typedef enum {
    MMO_FRAME_OK = 0, /* a full frame was consumed; payload and n name its body */
    MMO_FRAME_SHORT,  /* not enough bytes buffered yet for a whole frame */
    MMO_FRAME_BAD     /* length field < 2: malformed */
} mmo_frame_result;

/* Try to read one frame at the cursor. On OK, advance past it and point
 * payload and n at the body (inside r's own span). On SHORT or BAD the cursor is
 * left where it was, so a streaming caller can wait for more or bail. */
mmo_frame_result mmo_frame_get(mmo_rbuf *r, const u8 **payload, size_t *n);

#endif /* MMO_CODEC_H */
