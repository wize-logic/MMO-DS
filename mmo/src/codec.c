/* Wire buffer primitives and framing. See codec.h for the contract. */
#include "codec.h"

#include <stdlib.h>
#include <string.h>

/* --- write side -------------------------------------------------------- */

void mmo_wbuf_init(mmo_wbuf *w)
{
    w->data = NULL;
    w->len = w->cap = 0;
    w->err = 0;
}

void mmo_wbuf_free(mmo_wbuf *w)
{
    free(w->data);
    w->data = NULL;
    w->len = w->cap = 0;
    w->err = 0;
}

/* Ensure room for `extra` more bytes; set the sticky err on allocation failure. */
static int wbuf_reserve(mmo_wbuf *w, size_t extra)
{
    if (w->err)
        return -1;
    if (w->len + extra <= w->cap)
        return 0;
    size_t want = w->cap ? w->cap : 64;
    while (want < w->len + extra)
        want *= 2;
    u8 *p = realloc(w->data, want);
    if (!p) {
        w->err = 1;
        return -1;
    }
    w->data = p;
    w->cap = want;
    return 0;
}

void mmo_put_bytes(mmo_wbuf *w, const void *src, size_t n)
{
    if (n == 0 || wbuf_reserve(w, n) != 0)
        return;
    memcpy(w->data + w->len, src, n);
    w->len += n;
}

void mmo_put_u8(mmo_wbuf *w, u8 v)
{
    if (wbuf_reserve(w, 1) != 0)
        return;
    w->data[w->len++] = v;
}

void mmo_put_bool(mmo_wbuf *w, int v)
{
    mmo_put_u8(w, v ? 1 : 0);
}

void mmo_put_u16le(mmo_wbuf *w, u16 v)
{
    if (wbuf_reserve(w, 2) != 0)
        return;
    w->data[w->len++] = (u8)(v & 0xFF);
    w->data[w->len++] = (u8)((v >> 8) & 0xFF);
}

void mmo_put_s16le(mmo_wbuf *w, s16 v)
{
    mmo_put_u16le(w, (u16)v);
}

void mmo_put_u32le(mmo_wbuf *w, u32 v)
{
    if (wbuf_reserve(w, 4) != 0)
        return;
    w->data[w->len++] = (u8)(v & 0xFF);
    w->data[w->len++] = (u8)((v >> 8) & 0xFF);
    w->data[w->len++] = (u8)((v >> 16) & 0xFF);
    w->data[w->len++] = (u8)((v >> 24) & 0xFF);
}

void mmo_put_s32le(mmo_wbuf *w, s32 v)
{
    mmo_put_u32le(w, (u32)v);
}

void mmo_put_s64le(mmo_wbuf *w, s64 v)
{
    u64 u = (u64)v;
    if (wbuf_reserve(w, 8) != 0)
        return;
    for (int i = 0; i < 8; i++)
        w->data[w->len++] = (u8)((u >> (8 * i)) & 0xFF);
}

void mmo_put_bytes_u8(mmo_wbuf *w, const void *src, size_t n)
{
    if (n > 0xFF) {
        w->err = 1;
        return;
    }
    mmo_put_u8(w, (u8)n);
    mmo_put_bytes(w, src, n);
}

void mmo_put_utf16_nt(mmo_wbuf *w, const char *s)
{
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        mmo_put_u8(w, (u8)*p);
        mmo_put_u8(w, 0);
    }
    mmo_put_u8(w, 0);
    mmo_put_u8(w, 0);
}

/* --- read side --------------------------------------------------------- */

void mmo_rbuf_init(mmo_rbuf *r, const void *data, size_t len)
{
    r->data = (const u8 *)data;
    r->len = len;
    r->pos = 0;
    r->err = 0;
}

size_t mmo_rbuf_remaining(const mmo_rbuf *r)
{
    return r->len - r->pos;
}

/* True and sets err if fewer than n bytes remain to read. */
static int rbuf_underflow(mmo_rbuf *r, size_t n)
{
    if (r->err)
        return 1;
    if (r->len - r->pos < n) {
        r->err = 1;
        return 1;
    }
    return 0;
}

u8 mmo_get_u8(mmo_rbuf *r)
{
    if (rbuf_underflow(r, 1))
        return 0;
    return r->data[r->pos++];
}

int mmo_get_bool(mmo_rbuf *r)
{
    return mmo_get_u8(r) != 0;
}

u16 mmo_get_u16le(mmo_rbuf *r)
{
    if (rbuf_underflow(r, 2))
        return 0;
    u16 v = (u16)r->data[r->pos] | ((u16)r->data[r->pos + 1] << 8);
    r->pos += 2;
    return v;
}

s16 mmo_get_s16le(mmo_rbuf *r)
{
    return (s16)mmo_get_u16le(r);
}

u32 mmo_get_u32le(mmo_rbuf *r)
{
    if (rbuf_underflow(r, 4))
        return 0;
    u32 v = (u32)r->data[r->pos] | ((u32)r->data[r->pos + 1] << 8) |
            ((u32)r->data[r->pos + 2] << 16) | ((u32)r->data[r->pos + 3] << 24);
    r->pos += 4;
    return v;
}

s32 mmo_get_s32le(mmo_rbuf *r)
{
    return (s32)mmo_get_u32le(r);
}

s64 mmo_get_s64le(mmo_rbuf *r)
{
    if (rbuf_underflow(r, 8))
        return 0;
    u64 v = 0;
    for (int i = 0; i < 8; i++)
        v |= (u64)r->data[r->pos + i] << (8 * i);
    r->pos += 8;
    return (s64)v;
}

void mmo_get_bytes(mmo_rbuf *r, void *dst, size_t n)
{
    if (rbuf_underflow(r, n))
        return;
    if (n > 0)
        memcpy(dst, r->data + r->pos, n);
    r->pos += n;
}

size_t mmo_get_bytes_u8(mmo_rbuf *r, void *dst, size_t cap)
{
    size_t n = mmo_get_u8(r);
    if (rbuf_underflow(r, n))
        return 0;
    size_t take = n < cap ? n : cap;
    if (take > 0)
        memcpy(dst, r->data + r->pos, take);
    if (n > cap)
        r->err = 1; /* blob did not fit the caller's buffer */
    r->pos += n;
    return n;
}

size_t mmo_get_utf16_nt(mmo_rbuf *r, char *dst, size_t cap)
{
    size_t out = 0;
    for (;;) {
        if (rbuf_underflow(r, 2)) {
            if (cap > 0)
                dst[out < cap ? out : cap - 1] = '\0';
            return out;
        }
        u8 lo = r->data[r->pos];
        u8 hi = r->data[r->pos + 1];
        r->pos += 2;
        if (lo == 0 && hi == 0)
            break;
        if (out + 1 < cap)
            dst[out] = (char)lo; /* Latin-1: low byte of the code unit */
        out++;
    }
    if (cap > 0)
        dst[out < cap ? out : cap - 1] = '\0';
    return out;
}

/* --- framing ----------------------------------------------------------- */

void mmo_frame_put(mmo_wbuf *out, const void *payload, size_t n)
{
    if (n + 2 > 0xFFFF) {
        out->err = 1;
        return;
    }
    mmo_put_u16le(out, (u16)(n + 2));
    mmo_put_bytes(out, payload, n);
}

mmo_frame_result mmo_frame_get(mmo_rbuf *r, const u8 **payload, size_t *n)
{
    size_t avail = r->len - r->pos;
    if (avail < 2)
        return MMO_FRAME_SHORT; /* not even the length field yet */
    size_t total = (size_t)r->data[r->pos] | ((size_t)r->data[r->pos + 1] << 8);
    if (total < 2)
        return MMO_FRAME_BAD;
    if (avail < total)
        return MMO_FRAME_SHORT; /* body not fully buffered */
    *payload = r->data + r->pos + 2;
    *n = total - 2;
    r->pos += total;
    return MMO_FRAME_OK;
}
