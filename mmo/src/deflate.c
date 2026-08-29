/* Persistent raw-DEFLATE (RFC 1951) for the game stream. See
 * deflate.h. The inflater is a compact puff-style canonical-Huffman decoder with
 * a 32 KiB sliding window that persists across packets; the deflater emits stored
 * blocks. */
#include "deflate.h"

#include <stdlib.h>
#include <string.h>

/* --- sliding-window output -------------------------------------------------
 * Every decoded byte is written to the caller's out buffer and mirrored into the
 * persistent ring window so a later packet's match can copy from it. */

typedef struct {
    mmo_inflate *w;      /* persistent window */
    u8          *out;    /* caller output */
    size_t       cap;
    size_t       len;    /* bytes written so far this call */
    int          overflow;
} sink;

static void emit(sink *s, u8 b)
{
    if (s->len >= s->cap) { s->overflow = 1; return; }
    s->out[s->len++] = b;
    s->w->window[s->w->wpos] = b;
    s->w->wpos = (s->w->wpos + 1) & (MMO_DEFLATE_WINDOW - 1);
    if (s->w->wpos == 0)
        s->w->full = 1;
}

/* Copy a back-reference of `len` bytes at `dist` behind the current window
 * position, one byte at a time so overlapping copies (dist < len) work. */
static void copy_match(sink *s, unsigned dist, unsigned len)
{
    for (unsigned i = 0; i < len; i++) {
        u32 src = (s->w->wpos - dist) & (MMO_DEFLATE_WINDOW - 1);
        emit(s, s->w->window[src]);
    }
}

/* --- bit reader (LSB-first, as DEFLATE specifies) -------------------------- */

typedef struct {
    const u8 *data;
    size_t    len;
    size_t    byte;   /* next byte to consume */
    u32       bitbuf; /* buffered bits, LSB first */
    int       bitcnt; /* valid bits in bitbuf */
    int       err;    /* ran past the end */
} bitreader;

static int getbit(bitreader *b)
{
    if (b->bitcnt == 0) {
        if (b->byte >= b->len) { b->err = 1; return 0; }
        b->bitbuf = b->data[b->byte++];
        b->bitcnt = 8;
    }
    int bit = b->bitbuf & 1;
    b->bitbuf >>= 1;
    b->bitcnt--;
    return bit;
}

static u32 getbits(bitreader *b, int n)
{
    u32 v = 0;
    for (int i = 0; i < n; i++)
        v |= (u32)getbit(b) << i;
    return v;
}

static void align_byte(bitreader *b)
{
    b->bitbuf = 0;
    b->bitcnt = 0;
}

/* --- canonical Huffman (puff's decode) ------------------------------------ */

#define MAXBITS 15
#define MAXLCODES 286
#define MAXDCODES 30
#define MAXCODES (MAXLCODES + MAXDCODES)

typedef struct {
    s16 count[MAXBITS + 1];
    s16 symbol[MAXCODES];
} huff;

static int huff_build(huff *h, const u8 *lengths, int n)
{
    for (int len = 0; len <= MAXBITS; len++)
        h->count[len] = 0;
    for (int i = 0; i < n; i++)
        h->count[lengths[i]]++;
    if (h->count[0] == n)
        return 0; /* no codes, valid (e.g. an unused distance tree) */

    /* Check the code set is complete or under-subscribed but not over. */
    int left = 1;
    for (int len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0)
            return -1; /* over-subscribed */
    }

    s16 offs[MAXBITS + 1];
    offs[1] = 0;
    for (int len = 1; len < MAXBITS; len++)
        offs[len + 1] = offs[len] + h->count[len];
    for (int i = 0; i < n; i++)
        if (lengths[i] != 0)
            h->symbol[offs[lengths[i]]++] = (s16)i;
    return left; /* 0 = complete, >0 = incomplete (allowed for a single-symbol tree) */
}

static int huff_decode(bitreader *b, const huff *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAXBITS; len++) {
        code |= getbit(b);
        int count = h->count[len];
        if (code - first < count)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

/* --- length / distance base tables (RFC 1951 §3.2.5) ---------------------- */

static const u16 len_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const u8 len_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const u16 dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};
static const u8 dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

/* Decode one compressed (fixed or dynamic Huffman) block body. Returns 0, or -1
 * on a malformed symbol. Terminates on the end-of-block symbol (256). */
static int inflate_block(bitreader *b, sink *s, const huff *lc, const huff *dc)
{
    for (;;) {
        int sym = huff_decode(b, lc);
        if (sym < 0 || b->err)
            return -1;
        if (sym == 256)
            return 0;               /* end of block */
        if (sym < 256) {
            emit(s, (u8)sym);
            if (s->overflow)
                return -1;
            continue;
        }
        sym -= 257;
        if (sym >= 29)
            return -1;
        unsigned length = len_base[sym] + getbits(b, len_extra[sym]);
        int dsym = huff_decode(b, dc);
        if (dsym < 0 || dsym >= 30 || b->err)
            return -1;
        unsigned dist = dist_base[dsym] + getbits(b, dist_extra[dsym]);
        if (dist > MMO_DEFLATE_WINDOW ||
            (!s->w->full && dist > s->w->wpos))
            return -1;              /* reference before the stream began */
        copy_match(s, dist, length);
        if (s->overflow)
            return -1;
    }
}

static const huff *fixed_lit(void)
{
    static huff h;
    static int built = 0;
    if (!built) {
        u8 lengths[288];
        int i = 0;
        for (; i < 144; i++) lengths[i] = 8;
        for (; i < 256; i++) lengths[i] = 9;
        for (; i < 280; i++) lengths[i] = 7;
        for (; i < 288; i++) lengths[i] = 8;
        huff_build(&h, lengths, 288);
        built = 1;
    }
    return &h;
}

static const huff *fixed_dist(void)
{
    static huff h;
    static int built = 0;
    if (!built) {
        u8 lengths[30];
        for (int i = 0; i < 30; i++) lengths[i] = 5;
        huff_build(&h, lengths, 30);
        built = 1;
    }
    return &h;
}

/* Read a dynamic block's two Huffman trees from the header (RFC 1951 §3.2.7). */
static const u8 clcidx[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

static int read_dynamic(bitreader *b, huff *lc, huff *dc)
{
    int hlit = getbits(b, 5) + 257;
    int hdist = getbits(b, 5) + 1;
    int hclen = getbits(b, 4) + 4;
    if (hlit > MAXLCODES || hdist > MAXDCODES || b->err)
        return -1;

    u8 cl_lengths[19];
    memset(cl_lengths, 0, sizeof cl_lengths);
    for (int i = 0; i < hclen; i++)
        cl_lengths[clcidx[i]] = (u8)getbits(b, 3);

    huff clh;
    if (huff_build(&clh, cl_lengths, 19) < 0)
        return -1;

    u8 lengths[MAXCODES];
    int n = 0;
    int total = hlit + hdist;
    while (n < total) {
        int sym = huff_decode(b, &clh);
        if (sym < 0 || b->err)
            return -1;
        if (sym < 16) {
            lengths[n++] = (u8)sym;
        } else if (sym == 16) {
            if (n == 0)
                return -1;
            int rep = getbits(b, 2) + 3;
            u8 prev = lengths[n - 1];
            while (rep-- && n < total)
                lengths[n++] = prev;
        } else if (sym == 17) {
            int rep = getbits(b, 3) + 3;
            while (rep-- && n < total)
                lengths[n++] = 0;
        } else { /* sym == 18 */
            int rep = getbits(b, 7) + 11;
            while (rep-- && n < total)
                lengths[n++] = 0;
        }
    }
    if (n != total)
        return -1;

    if (huff_build(lc, lengths, hlit) < 0)
        return -1;
    if (huff_build(dc, lengths + hlit, hdist) < 0)
        return -1;
    return 0;
}

/* Decode DEFLATE blocks from b into sk until the stream ends. With
 * stop_on_empty_stored set (the game stream's Z_SYNC_FLUSH framing) an empty
 * stored block terminates the run; otherwise (a self-contained zlib payload)
 * only the BFINAL bit does. Returns 0, or -1 on a malformed stream. */
static int decode_blocks(bitreader *b, sink *sk, int stop_on_empty_stored)
{
    for (;;) {
        int bfinal = getbit(b);
        int btype = getbits(b, 2);
        if (b->err)
            return -1;

        if (btype == 0) {
            align_byte(b);
            if (b->byte + 4 > b->len)
                return -1;
            u16 len = (u16)(b->data[b->byte] | (b->data[b->byte + 1] << 8));
            u16 nlen = (u16)(b->data[b->byte + 2] | (b->data[b->byte + 3] << 8));
            b->byte += 4;
            if ((u16)~len != nlen)
                return -1;
            if (len == 0) {
                if (stop_on_empty_stored)
                    return 0;       /* the SYNC_FLUSH terminator */
            } else {
                if (b->byte + len > b->len)
                    return -1;
                for (u16 i = 0; i < len; i++) {
                    emit(sk, b->data[b->byte++]);
                    if (sk->overflow)
                        return -1;
                }
            }
        } else if (btype == 1) {
            if (inflate_block(b, sk, fixed_lit(), fixed_dist()) != 0)
                return -1;
        } else if (btype == 2) {
            huff lc, dc;
            if (read_dynamic(b, &lc, &dc) != 0)
                return -1;
            if (inflate_block(b, sk, &lc, &dc) != 0)
                return -1;
        } else {
            return -1;              /* reserved block type */
        }

        if (bfinal)
            return 0;
        if (b->byte >= b->len && b->bitcnt == 0)
            return 0;               /* consumed everything */
    }
}

static u32 adler32(const u8 *d, size_t n)
{
    u32 a = 1, b = 0;
    for (size_t i = 0; i < n; i++) {
        a = (a + d[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

void mmo_inflate_init(mmo_inflate *s)
{
    memset(s, 0, sizeof *s);
}

size_t mmo_inflate_segment(mmo_inflate *s, const u8 *seg, size_t seglen,
                           u8 *out, size_t cap)
{
    /* Re-append the sync marker the sender stripped, exactly as the server's
     * CompressionDecoder does, so the stream ends on a byte-aligned empty
     * stored block. */
    size_t inlen = seglen + 4;
    u8 *in = malloc(inlen);
    if (!in)
        return (size_t)-1;
    if (seglen)
        memcpy(in, seg, seglen);
    in[seglen + 0] = 0x00;
    in[seglen + 1] = 0x00;
    in[seglen + 2] = 0xFF;
    in[seglen + 3] = 0xFF;

    bitreader b = { in, inlen, 0, 0, 0, 0 };
    sink sk = { s, out, cap, 0, 0 };
    int rc = decode_blocks(&b, &sk, 1);

    free(in);
    if (rc != 0 || sk.overflow)
        return (size_t)-1;
    return sk.len;
}

size_t mmo_inflate_zlib(const u8 *in, size_t inlen, u8 *out, size_t cap)
{
    /* 2-byte header + at least a one-byte block + 4-byte Adler-32. */
    if (inlen < 7)
        return (size_t)-1;
    u8 cmf = in[0], flg = in[1];
    if ((cmf & 0x0f) != 8)                       /* CM: 8 = DEFLATE */
        return (size_t)-1;
    if (((cmf << 8) | flg) % 31 != 0)            /* header checksum (RFC 1950) */
        return (size_t)-1;
    if (flg & 0x20)                              /* FDICT: preset dict unsupported */
        return (size_t)-1;

    mmo_inflate w;
    mmo_inflate_init(&w);
    bitreader b = { in + 2, inlen - 2, 0, 0, 0, 0 };
    sink sk = { &w, out, cap, 0, 0 };
    if (decode_blocks(&b, &sk, 0) != 0 || sk.overflow)
        return (size_t)-1;

    u32 want = (u32)in[inlen - 4] << 24 | (u32)in[inlen - 3] << 16 |
               (u32)in[inlen - 2] << 8 | (u32)in[inlen - 1];
    if (adler32(out, sk.len) != want)
        return (size_t)-1;
    return sk.len;
}

static u32 crc32_ieee(const u8 *p, size_t n)
{
    u32 c = 0xffffffffu;
    size_t i;
    int b;

    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (b = 0; b < 8; b++)
            c = (c >> 1) ^ (0xedb88320u & (u32)-(int)(c & 1u));
    }
    return ~c;
}

size_t mmo_inflate_gzip(const u8 *in, size_t inlen, u8 *out, size_t cap)
{
    u8 flags;
    size_t off = 10;
    u32 crc, isize;
    mmo_inflate w;
    bitreader b;
    sink sk;

    /* 10-byte header + at least a one-byte block + 8-byte trailer. */
    if (!in || inlen < 19)
        return (size_t)-1;
    if (in[0] != 0x1f || in[1] != 0x8b || in[2] != 8)
        return (size_t)-1;
    flags = in[3];
    if (flags & 0xe0)                       /* reserved flags */
        return (size_t)-1;
    if (flags & 0x04) {                     /* FEXTRA */
        u16 xlen;

        if (off + 2 > inlen)
            return (size_t)-1;
        xlen = (u16)(in[off] | (in[off + 1] << 8));
        off += 2 + (size_t)xlen;
    }
    if (flags & 0x08) {                     /* FNAME */
        while (off < inlen && in[off] != 0)
            off++;
        off++;
    }
    if (flags & 0x10) {                     /* FCOMMENT */
        while (off < inlen && in[off] != 0)
            off++;
        off++;
    }
    if (flags & 0x02)                       /* FHCRC */
        off += 2;
    if (off + 8 > inlen)
        return (size_t)-1;

    mmo_inflate_init(&w);
    b.data = in + off;
    b.len = inlen - off;
    b.byte = 0;
    b.bitbuf = 0;
    b.bitcnt = 0;
    b.err = 0;
    sk.w = &w;
    sk.out = out;
    sk.cap = cap;
    sk.len = 0;
    sk.overflow = 0;
    if (decode_blocks(&b, &sk, 0) != 0 || sk.overflow)
        return (size_t)-1;

    crc = (u32)in[inlen - 8] | (u32)in[inlen - 7] << 8 |
          (u32)in[inlen - 6] << 16 | (u32)in[inlen - 5] << 24;
    isize = (u32)in[inlen - 4] | (u32)in[inlen - 3] << 8 |
            (u32)in[inlen - 2] << 16 | (u32)in[inlen - 1] << 24;
    if (crc32_ieee(out, sk.len) != crc)
        return (size_t)-1;
    if ((u32)sk.len != isize)
        return (size_t)-1;
    return sk.len;
}

/* --- deflate (stored blocks) ---------------------------------------------- */

void mmo_deflate_init(mmo_deflate *s)
{
    s->_reserved = 0;
}

size_t mmo_deflate_segment(mmo_deflate *s, const u8 *in, size_t n,
                           u8 *out, size_t cap)
{
    (void)s;
    if (n > 0xFFFF)
        return (size_t)-1;          /* one stored block; loud ceiling */
    /* Full stream = [00][LEN][NLEN][data][00 00 00 FF FF]; strip the trailing
     * `00 00 FF FF`, leaving the empty block's lone header byte. */
    size_t need = 1 + 2 + 2 + n + 1;
    if (need > cap)
        return (size_t)-1;
    size_t o = 0;
    out[o++] = 0x00;                        /* BFINAL=0, BTYPE=00, pad */
    out[o++] = (u8)(n & 0xFF);              /* LEN LE */
    out[o++] = (u8)((n >> 8) & 0xFF);
    out[o++] = (u8)(~n & 0xFF);             /* NLEN LE */
    out[o++] = (u8)((~n >> 8) & 0xFF);
    if (n)
        memcpy(out + o, in, n);
    o += n;
    out[o++] = 0x00;                        /* empty SYNC_FLUSH block header */
    return o;
}
