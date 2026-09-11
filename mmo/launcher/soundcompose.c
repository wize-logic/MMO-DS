/*
 * A player's sound packages, composed from their own cartridges, in the
 * launcher.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "soundcompose.h"
#include "soundtables.gen.h"   /* mmo/src, on the launcher include path */
#include "nitrorom.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define REC_N 8
enum { K_SEQ, K_SEQARC, K_BANK, K_WAVEARC, K_PLAYER, K_GROUP, K_PLAYER2,
       K_STRM };
#define BLOCK_HEADER 0x40
#define FILE_ALIGN 32
#define NO_ARCHIVE 0xFFFF
#define SBNK_COUNT 0x38
#define SBNK_ENTRY 0x3C
#define SWAR_COUNT 0x38
#define SWAR_TABLE 0x3C
#define SLOT_PENDING 0xFFFF
#define PT_BANK_LO 700
#define PT_BANK_HI 705

/* The SHARED reader, under this FILE'S own names. */
typedef MmoBlob blob;
typedef MmoMembers members;
#define die         mmo_nitro_die
#define xmalloc     mmo_nitro_alloc
#define xrealloc    mmo_nitro_grow
#define rd16        mmo_nitro_rd16
#define rd32        mmo_nitro_rd32
#define wr16        mmo_nitro_wr16
#define wr32        mmo_nitro_wr32
#define blob_dup    mmo_nitro_dup
#define read_file   mmo_nitro_read_file

/* The engine porter's NitroRom, one question of it: the bytes of one file by
 * its NitroFS path. No hint, a sound archive that is not there is named by
 * the label the caller already chose. */
static blob nds_file(const blob rom, const char *want, const char *label)
{
    return mmo_nitro_file(rom, want, label, NULL);
}


typedef struct {
    u8 *p;
    u32 len, cap;
} buf;

static void buf_need(buf *b, u32 more)
{
    if (b->len + more > b->cap) {
        b->cap = (b->cap ? b->cap * 2 : 4096);
        while (b->cap < b->len + more)
            b->cap *= 2;
        b->p = xrealloc(b->p, b->cap);
    }
}

static void buf_bytes(buf *b, const u8 *p, u32 n)
{
    buf_need(b, n);
    if (n)
        memcpy(b->p + b->len, p, n);
    b->len += n;
}

static void buf_zero(buf *b, u32 n)
{
    buf_need(b, n);
    memset(b->p + b->len, 0, n);
    b->len += n;
}

static void buf_u16(buf *b, u16 v) { u8 t[2]; wr16(t, v); buf_bytes(b, t, 2); }
static void buf_u32(buf *b, u32 v) { u8 t[4]; wr32(t, v); buf_bytes(b, t, 4); }

/* ------------------------------------------------------------------ sdat */
typedef struct {
    blob *rec;                  /* raw records; rec[i].p NULL = null record */
    char **name;                /* SYMB names; NULL entry = unnamed */
    int n;
    int cap;
} reclist;

typedef struct {
    reclist info[REC_N];
    reclist names[REC_N];       /* rec unused; name used */
    int symb_present[REC_N];
    int info_present[REC_N];
    int has_symb;
    blob *fat;
    int nfat, fatcap;
} sdat;

static void list_push(reclist *l, blob b, char *name)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 64;
        l->rec = xrealloc(l->rec, (size_t)l->cap * sizeof *l->rec);
        l->name = xrealloc(l->name, (size_t)l->cap * sizeof *l->name);
    }
    l->rec[l->n] = b;
    l->name[l->n] = name;
    l->n++;
}

static int fat_push(sdat *s, blob b)
{
    if (s->nfat == s->fatcap) {
        s->fatcap = s->fatcap ? s->fatcap * 2 : 256;
        s->fat = xrealloc(s->fat, (size_t)s->fatcap * sizeof *s->fat);
    }
    s->fat[s->nfat] = b;
    return s->nfat++;
}

static u32 rec_len(int kind, const u8 *p)
{
    switch (kind) {
    case K_SEQ: case K_BANK: case K_STRM: return 12;
    case K_SEQARC: case K_WAVEARC: return 4;
    case K_PLAYER: return 8;
    case K_GROUP: return 4 + rd32(p) * 8;
    case K_PLAYER2: return 1 + p[0];
    default: return 0;
    }
}

static void sdat_parse(sdat *s, const blob d)
{
    u32 blk_off[REC_N] = { 0 };  /* SYMB, INFO, FAT, FILE offsets by tag */
    u32 symb_off = 0, symb_sz = 0, info_off = 0, fat_off = 0;
    u16 nblk;
    int i, k;

    (void)blk_off;
    memset(s, 0, sizeof *s);
    if (d.len < 0x40 || memcmp(d.p, "SDAT", 4) != 0)
        die("not an SDAT");
    nblk = rd16(d.p + 14);
    for (i = 0; i < nblk; i++) {
        u32 off = rd32(d.p + 0x10 + i * 8);
        u32 size = rd32(d.p + 0x14 + i * 8);

        if (size == 0)
            continue;
        if (memcmp(d.p + off, "SYMB", 4) == 0) { symb_off = off; symb_sz = size; }
        else if (memcmp(d.p + off, "INFO", 4) == 0) info_off = off;
        else if (memcmp(d.p + off, "FAT ", 4) == 0) fat_off = off;
    }
    (void)symb_sz;
    if (info_off == 0 || fat_off == 0)
        die("SDAT has no INFO or FAT block");

    s->has_symb = symb_off != 0;
    for (k = 0; k < REC_N; k++) {
        u32 ro;
        u32 n, j;

        if (s->has_symb) {
            ro = rd32(d.p + symb_off + 8 + k * 4);
            s->symb_present[k] = ro != 0;
            if (ro != 0) {
                n = rd32(d.p + symb_off + ro);
                for (j = 0; j < n; j++) {
                    u32 o = rd32(d.p + symb_off + ro + 4 + j * 4);
                    char *nm = NULL;

                    if (o != 0) {
                        const char *src = (const char *)d.p + symb_off + o;
                        size_t ln = strlen(src);

                        nm = xmalloc(ln + 1);
                        memcpy(nm, src, ln + 1);
                    }
                    list_push(&s->names[k], (blob){ 0, 0 }, nm);
                }
            }
        }
        ro = rd32(d.p + info_off + 8 + k * 4);
        s->info_present[k] = ro != 0;
        if (ro != 0) {
            n = rd32(d.p + info_off + ro);
            for (j = 0; j < n; j++) {
                u32 o = rd32(d.p + info_off + ro + 4 + j * 4);

                if (o == 0)
                    list_push(&s->info[k], (blob){ 0, 0 }, NULL);
                else
                    list_push(&s->info[k],
                              blob_dup(d.p + info_off + o,
                                       rec_len(k, d.p + info_off + o)), NULL);
            }
        }
    }
    {
        u32 n = rd32(d.p + fat_off + 8);
        u32 j;

        for (j = 0; j < n; j++) {
            u32 off = rd32(d.p + fat_off + 12 + j * 16);
            u32 size = rd32(d.p + fat_off + 16 + j * 16);

            fat_push(s, blob_dup(d.p + off, size));
        }
    }
}

static void build_list_block(buf *out, const char *tag, const sdat *s,
                             int symbols)
{
    const int *present = symbols ? s->symb_present : s->info_present;
    u32 rec_off[REC_N];
    u32 cur = BLOCK_HEADER;
    buf heads = { 0 }, pool = { 0 };
    u32 pool_base;
    int k, i;
    u32 size, start;

    for (k = 0; k < REC_N; k++) {
        const reclist *l = symbols ? &s->names[k] : &s->info[k];

        if (!present[k]) {
            rec_off[k] = 0;
            continue;
        }
        rec_off[k] = cur;
        cur += 4 + 4 * (u32)l->n;
    }
    pool_base = cur;
    for (k = 0; k < REC_N; k++) {
        const reclist *l = symbols ? &s->names[k] : &s->info[k];

        if (!present[k])
            continue;
        buf_u32(&heads, (u32)l->n);
        for (i = 0; i < l->n; i++) {
            int null;

            if (symbols)
                null = l->name[i] == NULL || l->name[i][0] == '\0';
            else
                null = l->rec[i].p == NULL;
            if (null) {
                buf_u32(&heads, 0);
                continue;
            }
            buf_u32(&heads, pool_base + pool.len);
            if (symbols)
                buf_bytes(&pool, (const u8 *)l->name[i],
                          (u32)strlen(l->name[i]) + 1);
            else
                buf_bytes(&pool, l->rec[i].p, l->rec[i].len);
        }
    }
    size = BLOCK_HEADER + heads.len + pool.len;
    size += (4 - size % 4) % 4;
    start = out->len;
    buf_bytes(out, (const u8 *)tag, 4);
    buf_u32(out, size);
    for (k = 0; k < REC_N; k++)
        buf_u32(out, rec_off[k]);
    buf_zero(out, BLOCK_HEADER - 8 - 4 * REC_N);
    buf_bytes(out, heads.p, heads.len);
    buf_bytes(out, pool.p, pool.len);
    buf_zero(out, size - (out->len - start));
    free(heads.p);
    free(pool.p);
}

static blob sdat_build(const sdat *s)
{
    buf symb = { 0 }, info = { 0 }, body = { 0 }, out = { 0 };
    u32 fat_size = 12 + (u32)s->nfat * 16;
    u32 symb_off, info_off, fat_off, file_off, cursor, total;
    u32 *rows;
    int i;

    fat_size += (4 - fat_size % 4) % 4;
    if (s->has_symb)
        build_list_block(&symb, "SYMB", s, 1);
    build_list_block(&info, "INFO", s, 0);
    symb_off = BLOCK_HEADER;
    info_off = symb_off + symb.len;
    fat_off = info_off + info.len;
    file_off = fat_off + fat_size;

    rows = xmalloc((size_t)s->nfat * 2 * sizeof *rows);
    cursor = file_off + 16;
    cursor += (FILE_ALIGN - cursor % FILE_ALIGN) % FILE_ALIGN;
    buf_zero(&body, cursor - (file_off + 16));
    for (i = 0; i < s->nfat; i++) {
        rows[i * 2] = file_off + 16 + body.len;
        rows[i * 2 + 1] = s->fat[i].len;
        buf_bytes(&body, s->fat[i].p, s->fat[i].len);
        buf_zero(&body, (FILE_ALIGN - s->fat[i].len % FILE_ALIGN) % FILE_ALIGN);
    }

    total = BLOCK_HEADER + symb.len + info.len + fat_size + 16 + body.len;
    buf_bytes(&out, (const u8 *)"SDAT", 4);
    buf_u32(&out, 0x0100FEFF);
    buf_u32(&out, total);
    buf_u16(&out, BLOCK_HEADER);
    buf_u16(&out, 4);
    buf_u32(&out, symb_off);
    buf_u32(&out, symb.len);
    buf_u32(&out, info_off);
    buf_u32(&out, info.len);
    buf_u32(&out, fat_off);
    buf_u32(&out, fat_size);
    buf_u32(&out, file_off);
    buf_u32(&out, 16 + body.len);
    buf_zero(&out, BLOCK_HEADER - out.len);
    buf_bytes(&out, symb.p, symb.len);
    buf_bytes(&out, info.p, info.len);
    {
        u32 start = out.len;

        buf_bytes(&out, (const u8 *)"FAT ", 4);
        buf_u32(&out, fat_size);
        buf_u32(&out, (u32)s->nfat);
        for (i = 0; i < s->nfat; i++) {
            buf_u32(&out, rows[i * 2]);
            buf_u32(&out, rows[i * 2 + 1]);
            buf_u32(&out, 0);
            buf_u32(&out, 0);
        }
        buf_zero(&out, fat_size - (out.len - start));
    }
    buf_bytes(&out, (const u8 *)"FILE", 4);
    buf_u32(&out, 16 + body.len);
    buf_u32(&out, (u32)s->nfat);
    buf_u32(&out, 0);
    buf_bytes(&out, body.p, body.len);
    free(rows);
    free(symb.p);
    free(info.p);
    free(body.p);
    return (blob){ out.p, out.len };
}

/* graph helpers */
static u16 seq_file(const sdat *s, int i) { return rd16(s->info[K_SEQ].rec[i].p); }
static u16 seq_bank(const sdat *s, int i) { return rd16(s->info[K_SEQ].rec[i].p + 4); }
static u16 bank_file(const sdat *s, int i) { return rd16(s->info[K_BANK].rec[i].p); }
static u16 wavearc_file(const sdat *s, int i) { return rd16(s->info[K_WAVEARC].rec[i].p); }
static const char *seq_name(const sdat *s, int i)
{
    return (i < s->names[K_SEQ].n && s->names[K_SEQ].name[i])
           ? s->names[K_SEQ].name[i] : "";
}
static int seq_by_name(const sdat *s, const char *nm)
{
    int i;

    for (i = 0; i < s->names[K_SEQ].n; i++)
        if (s->names[K_SEQ].name[i] && strcmp(s->names[K_SEQ].name[i], nm) == 0)
            return i;
    return -1;
}
static int wavearc_by_name(const sdat *s, const char *nm)
{
    int i;

    for (i = 0; i < s->names[K_WAVEARC].n; i++)
        if (s->names[K_WAVEARC].name[i]
            && strcmp(s->names[K_WAVEARC].name[i], nm) == 0)
            return i;
    return -1;
}

/* ------------------------------------------------------------------ swar */
typedef struct {
    const u8 *p;
    u32 len;
} slice;

static int swar_split(const blob arc, slice **out)
{
    u32 n = rd32(arc.p + SWAR_COUNT);
    slice *sl = xmalloc((size_t)n * sizeof *sl);
    u32 i;

    for (i = 0; i < n; i++) {
        u32 o = rd32(arc.p + SWAR_TABLE + 4 * i);
        u32 e = (i + 1 < n) ? rd32(arc.p + SWAR_TABLE + 4 * (i + 1)) : arc.len;

        sl[i].p = arc.p + o;
        sl[i].len = e - o;
    }
    *out = sl;
    return (int)n;
}

static blob swar_build(const slice *samples, int n)
{
    u32 head = SWAR_TABLE + 4 * (u32)n;
    buf body = { 0 }, out = { 0 };
    u32 *offs = xmalloc((size_t)(n ? n : 1) * sizeof *offs);
    int i;
    u32 size;

    for (i = 0; i < n; i++) {
        buf_zero(&body, (4 - body.len % 4) % 4);
        offs[i] = head + body.len;
        buf_bytes(&body, samples[i].p, samples[i].len);
    }
    size = head + body.len;
    buf_bytes(&out, (const u8 *)"SWAR", 4);
    buf_u32(&out, 0x0100FEFF);
    buf_u32(&out, size);
    buf_u16(&out, 0x10);
    buf_u16(&out, 1);
    buf_bytes(&out, (const u8 *)"DATA", 4);
    buf_u32(&out, size - 0x10);
    buf_zero(&out, 32);
    buf_u32(&out, (u32)n);
    for (i = 0; i < n; i++)
        buf_u32(&out, offs[i]);
    buf_bytes(&out, body.p, body.len);
    free(offs);
    free(body.p);
    return (blob){ out.p, out.len };
}

/* ------------------------------------------------------------------ sseq */
/* portmusic.sseq_scan: programs selected, (program, note) pairs, and
 * whether every track ended on an opcode this walker knows. */
typedef struct {
    u8 progs[128 / 8];
    u8 pairs[128 * 128 / 8];
    int complete;
} scan;

static int bit(const u8 *v, int i) { return (v[i >> 3] >> (i & 7)) & 1; }
static void setbit(u8 *v, int i) { v[i >> 3] |= (u8)(1 << (i & 7)); }

static u32 varlen(const u8 *b, u32 len, u32 *i)
{
    u32 v = 0;

    while (*i < len) {
        u8 c = b[(*i)++];

        v = (v << 7) | (c & 0x7F);
        if (!(c & 0x80))
            break;
    }
    return v;
}

typedef struct { u32 i; u8 prog; int8_t transpose; } start;

static void sseq_scan(const blob seq, scan *out)
{
    u32 base = rd32(seq.p + 0x18);
    start *starts = xmalloc(64 * sizeof *starts);
    int nstart = 1, capstart = 64, k = 0;
    u64 *seen = NULL;
    u32 seen_cap = 1u << 16, seen_n = 0;
    u32 si;

    memset(out, 0, sizeof *out);
    out->complete = 1;
    seen = xmalloc(seen_cap * sizeof *seen);
    memset(seen, 0xFF, seen_cap * sizeof *seen);
    starts[0].i = base;
    starts[0].prog = 0;
    starts[0].transpose = 0;

#define SEEN_KEY(pos, pg, tr) \
    (((u32)(pos) & 0x3FFFFu) | ((u32)(pg) << 18) | (((u32)(u8)(tr)) << 25) ^ 0)

    while (k < nstart) {
        u32 i = starts[k].i;
        u8 prog = starts[k].prog;
        int8_t transpose = starts[k].transpose;
        u32 guard = 0;

        k++;
        while (i < seq.len) {
            u8 c;
            u64 key;
            u32 h;

            if (++guard > 0x20000) {
                out->complete = 0;
                break;
            }
            /* the (i, prog, transpose) seen set, open addressing */
            /* full keys, so a table hit is a true revisit: a false
             * positive here would abandon a live branch and lose notes */
            key = ((u64)i << 16) | ((u64)prog << 8) | (u8)transpose;
            h = (u32)((key * 0x9E3779B97F4A7C15ULL) >> 40) & (seen_cap - 1);
            {
                int dup = 0;
                u32 probe = h;

                for (;;) {
                    u64 slotv = seen[probe];

                    if (slotv == UINT64_MAX)
                        break;
                    if (slotv == key) {
                        dup = 1;
                        break;
                    }
                    probe = (probe + 1) & (seen_cap - 1);
                }
                if (dup)
                    break;
                seen[probe] = key;
                if (++seen_n * 2 > seen_cap) {
                    u32 newcap = seen_cap * 2, j;
                    u64 *ns = xmalloc(newcap * sizeof *ns);

                    memset(ns, 0xFF, newcap * sizeof *ns);
                    for (j = 0; j < seen_cap; j++)
                        if (seen[j] != UINT64_MAX) {
                            u32 pb = (u32)((seen[j] * 0x9E3779B97F4A7C15ULL)
                                           >> 40) & (newcap - 1);

                            while (ns[pb] != UINT64_MAX)
                                pb = (pb + 1) & (newcap - 1);
                            ns[pb] = seen[j];
                        }
                    free(seen);
                    seen = ns;
                    seen_cap = newcap;
                }
            }
            c = seq.p[i++];
            if (c < 0x80) {
                int note = c + transpose;

                if (note < 0) note = 0;
                if (note > 127) note = 127;
                setbit(out->pairs, prog * 128 + note);
                i += 1;
                varlen(seq.p, seq.len, &i);
            } else if (c == 0x80) {
                varlen(seq.p, seq.len, &i);
            } else if (c == 0x81) {
                u32 v = varlen(seq.p, seq.len, &i);

                prog = (u8)(v & 0x7F);
                setbit(out->progs, prog);
            } else if (c == 0x93) {
                u32 off;

                i += 1;
                if (i + 3 > seq.len) { out->complete = 0; break; }
                off = seq.p[i] | ((u32)seq.p[i + 1] << 8)
                    | ((u32)seq.p[i + 2] << 16);
                i += 3;
                if (nstart == capstart) {
                    capstart *= 2;
                    starts = xrealloc(starts, (size_t)capstart * sizeof *starts);
                }
                starts[nstart].i = base + off;
                starts[nstart].prog = 0;
                starts[nstart].transpose = 0;
                nstart++;
            } else if (c == 0x94 || c == 0x95) {
                u32 off;

                if (i + 3 > seq.len) { out->complete = 0; break; }
                off = seq.p[i] | ((u32)seq.p[i + 1] << 8)
                    | ((u32)seq.p[i + 2] << 16);
                i += 3;
                if (nstart == capstart) {
                    capstart *= 2;
                    starts = xrealloc(starts, (size_t)capstart * sizeof *starts);
                }
                starts[nstart].i = base + off;
                starts[nstart].prog = prog;
                starts[nstart].transpose = transpose;
                nstart++;
                if (c == 0x94)
                    break;
            } else if (c == 0xA0 || c == 0xA1 || c == 0xA2) {
                /* prefix, no operand of its own */
            } else if (c >= 0xB0 && c <= 0xBD) {
                i += 3;
            } else if (c == 0xC3) {
                if (i >= seq.len) { out->complete = 0; break; }
                transpose = (int8_t)seq.p[i];
                i += 1;
            } else if (c >= 0xC0 && c <= 0xD6) {
                i += 1;
            } else if (c == 0xE0 || c == 0xE1 || c == 0xE3) {
                i += 2;
            } else if (c == 0xFC) {
                /* loop end */
            } else if (c == 0xFD || c == 0xFF) {
                break;
            } else if (c == 0xFE) {
                i += 2;
            } else {
                out->complete = 0;
                break;
            }
        }
    }
    for (si = 0; si < 128 * 128; si++)
        if (bit(out->pairs, si))
            setbit(out->progs, si / 128);
    free(starts);
    free(seen);
#undef SEEN_KEY
}

/* which note-def byte offsets in an SBNK the wanted programs reach;
 * portmusic.sbnk_note_defs. `pairs` NULL widens to every def. */
static int note_defs(const blob bank, const u8 *want, const u8 *pairs,
                     u32 **out)
{
    u32 n = rd32(bank.p + SBNK_COUNT);
    u32 i;
    u32 *defs = NULL;
    int ndef = 0, cap = 0;

#define PUSH(o) do { \
        if ((o) + 10 > bank.len) \
            die("an instrument has a note def past the end of the bank"); \
        if (ndef == cap) { cap = cap ? cap * 2 : 64; \
            defs = xrealloc(defs, (size_t)cap * sizeof *defs); } \
        defs[ndef++] = (o); \
    } while (0)

    for (i = 0; i < n && i < 128; i++) {
        u8 kind = bank.p[SBNK_ENTRY + i * 4];
        u16 off = rd16(bank.p + SBNK_ENTRY + i * 4 + 1);

        if (kind == 0 || off == 0 || !bit(want, (int)i))
            continue;
        if (kind >= 1 && kind <= 5) {
            PUSH((u32)off);
        } else if (kind == 16) {
            u8 lo = bank.p[off], hi = bank.p[off + 1];
            int kk;

            for (kk = 0; kk <= hi - lo; kk++) {
                if (pairs == NULL || bit(pairs, (int)i * 128 + lo + kk))
                    PUSH((u32)off + 2 + (u32)kk * 12 + 2);
            }
        } else if (kind == 17) {
            const u8 *bounds = bank.p + off;
            int nr = 0, kk;

            for (kk = 0; kk < 8; kk++)
                if (bounds[kk])
                    nr++;
            for (kk = 0; kk < nr; kk++) {
                int low = kk == 0 ? 0 : bounds[kk - 1] + 1;
                int hit = pairs == NULL, nt;

                for (nt = low; !hit && nt <= bounds[kk]; nt++)
                    hit = bit(pairs, (int)i * 128 + nt);
                if (hit)
                    PUSH((u32)off + 8 + (u32)kk * 12 + 2);
            }
        } else {
            die("instrument %u is record kind %u, which this does not read",
                i, kind);
        }
    }
#undef PUSH
    *out = defs;
    return ndef;
}

/* ------------------------------------------------------- typed programs */
typedef struct {
    u8 kind;                     /* record kind of the region */
    u8 def[10];
} region;

typedef struct {
    int prog;
    u8 kind;
    u8 meta[8];                  /* drums: lo,hi; split: 8 bounds */
    int nreg;
    region reg[130];
} program;

static int typed_instruments(const sdat *s, int b, program **out)
{
    blob bank;
    u32 n;
    u32 i;
    program *ps = NULL;
    int np = 0, cap = 0;

    if (b >= s->info[K_BANK].n || s->info[K_BANK].rec[b].p == NULL) {
        *out = NULL;
        return 0;
    }
    bank = s->fat[bank_file(s, b)];
    n = rd32(bank.p + SBNK_COUNT);
    for (i = 0; i < n; i++) {
        u8 kind = bank.p[SBNK_ENTRY + i * 4];
        u16 off = rd16(bank.p + SBNK_ENTRY + i * 4 + 1);
        program p;

        if (kind == 0 || off == 0)
            continue;
        memset(&p, 0, sizeof p);
        p.prog = (int)i;
        p.kind = kind;
        if (kind >= 1 && kind <= 5) {
            p.nreg = 1;
            p.reg[0].kind = kind;
            memcpy(p.reg[0].def, bank.p + off, 10);
        } else if (kind == 16) {
            u8 lo = bank.p[off], hi = bank.p[off + 1];
            int kk;

            p.meta[0] = lo;
            p.meta[1] = hi;
            p.nreg = hi - lo + 1;
            if (p.nreg > 128)
                die("a drum kit spans %d notes", p.nreg);
            for (kk = 0; kk < p.nreg; kk++) {
                u32 at = off + 2 + (u32)kk * 12;

                p.reg[kk].kind = (u8)rd16(bank.p + at);
                memcpy(p.reg[kk].def, bank.p + at + 2, 10);
            }
        } else if (kind == 17) {
            int nr = 0, kk;

            memcpy(p.meta, bank.p + off, 8);
            for (kk = 0; kk < 8; kk++)
                if (p.meta[kk])
                    nr++;
            p.nreg = nr;
            for (kk = 0; kk < nr; kk++) {
                u32 at = off + 8 + (u32)kk * 12;

                p.reg[kk].kind = (u8)rd16(bank.p + at);
                memcpy(p.reg[kk].def, bank.p + at + 2, 10);
            }
        } else {
            continue;
        }
        if (np == cap) {
            cap = cap ? cap * 2 : 64;
            ps = xrealloc(ps, (size_t)cap * sizeof *ps);
        }
        ps[np++] = p;
    }
    *out = ps;
    return np;
}

static blob build_sbnk(const program *ps, int np)
{
    int maxp = 0, i, j;
    u32 n, base;
    buf entries = { 0 }, body = { 0 }, out = { 0 };
    u32 size;

    for (i = 0; i < np; i++)
        if (ps[i].prog + 1 > maxp)
            maxp = ps[i].prog + 1;
    n = (u32)maxp;
    base = SBNK_ENTRY + 4 * n;
    buf_zero(&entries, 4 * n);
    for (i = 0; i < np; i++) {
        u32 off = base + body.len;
        const program *p = &ps[i];

        if (off > 0xFFFF)
            die("bank record offset past 64k");
        if (p->kind >= 1 && p->kind <= 5) {
            buf_bytes(&body, p->reg[0].def, 10);
        } else if (p->kind == 16) {
            buf_bytes(&body, p->meta, 2);
            for (j = 0; j < p->nreg; j++) {
                buf_u16(&body, p->reg[j].kind);
                buf_bytes(&body, p->reg[j].def, 10);
            }
        } else if (p->kind == 17) {
            buf_bytes(&body, p->meta, 8);
            for (j = 0; j < p->nreg; j++) {
                buf_u16(&body, p->reg[j].kind);
                buf_bytes(&body, p->reg[j].def, 10);
            }
        } else {
            die("program %d has record kind %u, which this does not write",
                p->prog, p->kind);
        }
        entries.p[p->prog * 4] = p->kind;
        wr16(entries.p + p->prog * 4 + 1, (u16)off);
    }
    size = SBNK_ENTRY + 4 * n + body.len;
    buf_bytes(&out, (const u8 *)"SBNK", 4);
    buf_u32(&out, 0x0100FEFF);
    buf_u32(&out, size);
    buf_u16(&out, 0x10);
    buf_u16(&out, 1);
    buf_bytes(&out, (const u8 *)"DATA", 4);
    buf_u32(&out, size - 0x10);
    buf_zero(&out, 32);
    buf_u32(&out, n);
    buf_bytes(&out, entries.p, entries.len);
    buf_bytes(&out, body.p, body.len);
    free(entries.p);
    free(body.p);
    return (blob){ out.p, out.len };
}

/* ------------------------------------------------------- wave registry */
/* insertion-ordered byte-keyed wave list: first index wins, the way the
 * python dicts behave. */
typedef struct {
    slice *w;
    u32 *hash;
    int n, cap;
} waves;

static u32 wave_hash(const u8 *p, u32 len)
{
    u64 h = 0xCBF29CE484222325ULL;
    u32 i;

    for (i = 0; i < len; i++)
        h = (h ^ p[i]) * 0x100000001B3ULL;
    return (u32)(h ^ (h >> 32));
}

static int waves_find(const waves *ws, const u8 *p, u32 len, u32 h)
{
    int i;

    for (i = 0; i < ws->n; i++)
        if (ws->hash[i] == h && ws->w[i].len == len
            && memcmp(ws->w[i].p, p, len) == 0)
            return i;
    return -1;
}

static int waves_add(waves *ws, const u8 *p, u32 len)
{
    u32 h = wave_hash(p, len);
    int i = waves_find(ws, p, len, h);

    if (i >= 0)
        return i;
    if (ws->n == ws->cap) {
        ws->cap = ws->cap ? ws->cap * 2 : 64;
        ws->w = xrealloc(ws->w, (size_t)ws->cap * sizeof *ws->w);
        ws->hash = xrealloc(ws->hash, (size_t)ws->cap * sizeof *ws->hash);
    }
    ws->w[ws->n].p = p;
    ws->w[ws->n].len = len;
    ws->hash[ws->n] = h;
    return ws->n++;
}

/* one wave of a program region, out of the bank's archives */
static slice region_wave(const sdat *s, int bank, const u8 *def)
{
    u16 idx = rd16(def), slot = rd16(def + 2);
    const u8 *brec = s->info[K_BANK].rec[bank].p;
    u16 arc;
    slice *sl;
    int n;
    slice w = { NULL, 0 };

    if (slot >= 4)
        return w;
    arc = rd16(brec + 4 + slot * 2);
    if (arc == NO_ARCHIVE || arc >= s->info[K_WAVEARC].n)
        return w;
    n = swar_split(s->fat[wavearc_file(s, arc)], &sl);
    if (idx < n)
        w = sl[idx];
    free(sl);
    return w;
}

/* =================================================================== */
/* the three passes                                                     */
/* =================================================================== */

/* master instrument list of the destination's music banks: program ->
 * first defining bank's program object */
static int dst_master(const sdat *s, program **out, int *bank_of)
{
    int have[128] = { 0 };
    program *master = xmalloc(128 * sizeof *master);
    int b, i, n = 0;

    for (b = PT_BANK_LO; b <= PT_BANK_HI; b++) {
        program *ps;
        int np = typed_instruments(s, b, &ps);

        for (i = 0; i < np; i++) {
            int p = ps[i].prog;

            if (p < 128 && !have[p]) {
                have[p] = 1;
                master[p] = ps[i];
                bank_of[p] = b;
                n++;
            }
        }
        free(ps);
    }
    *out = master;
    return n;
}

/* --------------------------------------------------------- font_apply */
/* soundfont.compose. `font` is 1 heartgold, 2 blackwhite. */

static int is_hg_music_bank(const sdat *s, int b)
{
    const char *n = (b < s->names[K_BANK].n && s->names[K_BANK].name[b])
                    ? s->names[K_BANK].name[b] : "";

    return b >= 700 && b < 750 && strncmp(n, "BANK_", 5) == 0
        && strstr(n, "_SE") == NULL && strstr(n, "GAMEBOY") == NULL;
}


/* program signature for the variant vote: kind, meta, and per region the
 * kind, the wave bytes (hashed) and the articulation. */
static u64 prog_sig(const sdat *s, int bank, const program *p)
{
    u64 h = 0xCBF29CE484222325ULL;
    int r;

#define MIX(byte) (h = (h ^ (u8)(byte)) * 0x100000001B3ULL)
    MIX(p->kind);
    for (r = 0; r < 8; r++)
        MIX(p->meta[r]);
    for (r = 0; r < p->nreg; r++) {
        int j;

        MIX(p->reg[r].kind);
        if (p->reg[r].kind >= 1 && p->reg[r].kind <= 5) {
            slice w = region_wave(s, bank, p->reg[r].def);

            MIX(w.len); MIX(w.len >> 8); MIX(w.len >> 16);
            if (w.p != NULL) {
                u32 wh = wave_hash(w.p, w.len);

                MIX(wh); MIX(wh >> 8); MIX(wh >> 16); MIX(wh >> 24);
            }
        } else {
            MIX(p->reg[r].def[0]); MIX(p->reg[r].def[1]);
        }
        for (j = 4; j < 10; j++)
            MIX(p->reg[r].def[j]);
    }
#undef MIX
    return h;
}

/* the most bank-supported variant of foreign program q (ties to the first
 * seen, the way python's most_common breaks them) */
static int hg_canonical(const sdat *fo, int q, program *out, int *out_bank)
{
    struct variant { u64 sig; int count; program rep; int bank; } vars[64];
    int nvar = 0, b, i, best = -1;

    for (b = 0; b < fo->info[K_BANK].n; b++) {
        program *ps;
        int np;

        if (!is_hg_music_bank(fo, b))
            continue;
        np = typed_instruments(fo, b, &ps);
        for (i = 0; i < np; i++) {
            if (ps[i].prog != q)
                continue;
            {
                u64 sig = prog_sig(fo, b, &ps[i]);
                int v;

                for (v = 0; v < nvar; v++)
                    if (vars[v].sig == sig)
                        break;
                if (v == nvar && nvar < 64) {
                    vars[nvar].sig = sig;
                    vars[nvar].count = 0;
                    vars[nvar].rep = ps[i];
                    vars[nvar].bank = b;
                    nvar++;
                }
                if (v < nvar)
                    vars[v].count++;
            }
        }
        free(ps);
    }
    for (i = 0; i < nvar; i++)
        if (best < 0 || vars[i].count > vars[best].count)
            best = i;
    if (best < 0)
        return 0;
    *out = vars[best].rep;
    *out_bank = vars[best].bank;
    return 1;
}

/* the exact (program, bank) donor a table cell pins, either game */
static int bw_variant(const sdat *fo, int q, int bank, program *out)
{
    program *ps;
    int np = typed_instruments(fo, bank, &ps);
    int i, hit = 0;

    for (i = 0; i < np; i++)
        if (ps[i].prog == q) {
            *out = ps[i];
            hit = 1;
            break;
        }
    free(ps);
    return hit;
}

/* per-program notes the base's music actually plays, for kit coverage */
static void base_played(const sdat *base, u8 *pairs /* 128*128 bits */)
{
    int i;

    memset(pairs, 0, 128 * 128 / 8);
    for (i = 0; i < base->info[K_SEQ].n; i++) {
        u16 bank;
        const char *nm = seq_name(base, i);
        scan sc;
        int b;

        if (base->info[K_SEQ].rec[i].p == NULL)
            continue;
        bank = seq_bank(base, i);
        if (bank < PT_BANK_LO || bank > PT_BANK_HI)
            continue;
        if (nm[0] == '\0' || strstr(nm, "DUMMY") != NULL)
            continue;
        sseq_scan(base->fat[seq_file(base, i)], &sc);
        for (b = 0; b < 128 * 128; b++)
            if (bit(sc.pairs, b))
                setbit(pairs, b);
    }
}

#define SHARED_ARC_BUDGET 1450000
#define CLOSURE_CAP 240000

static void font_apply(sdat *base, const sdat *fo, int font)
{
    program donor[128];
    int donor_bank[128];
    int donor_pct[128];
    int has_donor[128] = { 0 };
    u8 played[128 * 128 / 8];
    int j, b, i, r;
    /* the rebuilt per-bank program sets, waves as foreign/base slices */
    program *newprog[PT_BANK_HI + 1] = { 0 };
    slice (*newwave[PT_BANK_HI + 1])[130] = { 0 };
    int newn[PT_BANK_HI + 1] = { 0 };
    waves shared = { 0 };
    slice *orig;
    int norig;
    waves ownw[PT_BANK_HI + 1];

    memset(ownw, 0, sizeof ownw);
    for (j = 0; j < 128; j++)
        donor_pct[j] = 100;
    base_played(base, played);
    for (j = 0; j < MMO_SOUNDFONT_ROWS; j++) {
        const mmo_soundfont_row *row = &MMO_SOUNDFONTS[j];
        int p = row->pt;

        if (p < 0 || p >= 128)
            continue;
        if (font == 1 && row->hg >= 0) {
            if (row->hg_bank >= 0) {
                if (bw_variant(fo, row->hg, row->hg_bank, &donor[p])) {
                    donor_bank[p] = row->hg_bank;
                    has_donor[p] = 1;
                }
            } else if (hg_canonical(fo, row->hg, &donor[p], &donor_bank[p])) {
                has_donor[p] = 1;
            }
            donor_pct[p] = row->hg_pct;
        } else if (font == 2 && row->bw >= 0) {
            if (bw_variant(fo, row->bw, row->bw_bank, &donor[p]))  {
                donor_bank[p] = row->bw_bank;
                has_donor[p] = 1;
            }
            donor_pct[p] = row->bw_pct;
        }

    }

    /* a drum kit replacing a drum kit must cover what the music plays;
     * the python compose checks exactly that case and no other, and parity
     * matters more than a stricter rule of this pass's own */
    {
        program *bm;
        int bm_bank[128] = { 0 };

        dst_master(base, &bm, bm_bank);
        for (j = 0; j < 128; j++) {
            int base_is_kit = 0;
            int p2;

            if (!has_donor[j] || donor[j].kind != 16)
                continue;
            for (p2 = 0; p2 < 128; p2++)
                ;
            base_is_kit = bm[j].kind == 16 && bm[j].nreg > 0;
            if (!base_is_kit)
                continue;
            for (r = 0; r < 128; r++)
                if (bit(played, j * 128 + r)
                    && !(r >= donor[j].meta[0] && r <= donor[j].meta[1])) {
                    has_donor[j] = 0;
                    break;
                }
        }
        free(bm);
    }

    /* The voice is the donor'S, the performance is platinum'S. */
    for (j = 0; j < 128; j++) {
        program *ps2;
        int np2, ii, pr;
        const program *ptp = NULL;

        if (!has_donor[j])
            continue;
        for (b = PT_BANK_LO; b <= PT_BANK_HI && ptp == NULL; b++) {
            np2 = typed_instruments(base, b, &ps2);
            for (ii = 0; ii < np2; ii++)
                if (ps2[ii].prog == j) {
                    static program keep;

                    keep = ps2[ii];
                    ptp = &keep;
                    break;
                }
            free(ps2);
        }
        if (ptp == NULL || ptp->nreg == 0)
            continue;
        for (r = 0; r < donor[j].nreg; r++) {
            if (donor[j].reg[r].kind < 1 || donor[j].reg[r].kind > 5)
                continue;
            pr = r < ptp->nreg ? r : ptp->nreg - 1;
            memcpy(donor[j].reg[r].def + 5, ptp->reg[pr].def + 5, 5);
            if (donor_pct[j] != 100 && donor[j].reg[r].def[7] > 0) {
                int lv = donor[j].reg[r].def[7] * donor_pct[j];

                lv = (lv + 50) / 100;
                if (lv < 1)
                    lv = 1;
                donor[j].reg[r].def[7] = (u8)(lv > 127 ? 127 : lv);
            }
        }
    }

    /* the new program sets, waves resolved to slices */
    orig = NULL;
    norig = swar_split(base->fat[wavearc_file(base, 700)], &orig);
    for (i = 0; i < norig; i++)
        waves_add(&shared, orig[i].p, orig[i].len);

    for (b = PT_BANK_LO; b <= PT_BANK_HI; b++) {
        program *ps;
        int np = typed_instruments(base, b, &ps);

        if (np == 0) {
            free(ps);
            continue;
        }
        newprog[b] = xmalloc((size_t)np * sizeof(program));
        newwave[b] = xmalloc((size_t)np * sizeof *newwave[b]);
        newn[b] = np;
        for (i = 0; i < np; i++) {
            int p = ps[i].prog;
            const program *srcp;
            const sdat *srcs;
            int srcbank;

            if (p < 128 && has_donor[p]) {
                srcp = &donor[p];
                srcs = fo;
                srcbank = donor_bank[p];
            } else {
                srcp = &ps[i];
                srcs = base;
                srcbank = b;
            }
            newprog[b][i] = *srcp;
            newprog[b][i].prog = p;
            for (r = 0; r < srcp->nreg; r++) {
                slice w = { NULL, 0 };

                if (srcp->reg[r].kind >= 1 && srcp->reg[r].kind <= 5) {
                    w = region_wave(srcs, srcbank, srcp->reg[r].def);
                    if (w.p == NULL)
                        die("bank %d program %d region lost its wave", b, p);
                }
                newwave[b][i][r] = w;
            }
        }
        free(ps);
    }

    /* wave placement: shared keeps its original indices and only grows; a
     * wave one bank reaches (and 700 reaches nothing alone) goes to that
     * bank's own archive. Count users first. */
    {
        waves cand = { 0 };
        u8 *users = NULL;
        int ncand = 0;

        for (b = PT_BANK_LO; b <= PT_BANK_HI; b++)
            for (i = 0; i < newn[b]; i++)
                for (r = 0; r < newprog[b][i].nreg; r++) {
                    slice w = newwave[b][i][r];
                    int at;

                    if (w.p == NULL)
                        continue;
                    at = waves_add(&cand, w.p, w.len);
                    if (at >= ncand) {
                        users = xrealloc(users, (size_t)cand.cap);
                        memset(users + ncand, 0, (size_t)(cand.cap - ncand));
                        ncand = cand.cap;
                    }
                    users[at] |= (u8)(1 << (b - PT_BANK_LO));
                }
        /* sorted(wave_users.items()) in python iterates by wave bytes; the
         * outcome it decides, which waves join shared vs own, does not
         * depend on that order, so insertion order serves here */
        for (i = 0; i < cand.n; i++) {
            slice w = cand.w[i];
            u32 h = cand.hash[i];
            int nb = 0, bb, only = -1;

            if (waves_find(&shared, w.p, w.len, h) >= 0)
                continue;
            for (bb = 0; bb < 6; bb++)
                if (users[i] & (1 << bb)) {
                    nb++;
                    only = bb + PT_BANK_LO;
                }
            if (nb > 1 || (users[i] & 1))
                waves_add(&shared, w.p, w.len);
            else if (nb == 1)
                waves_add(&ownw[only], w.p, w.len);
        }
        free(users);
        free(cand.w);
        free(cand.hash);
    }

    /* rewire defs and rebuild the six banks and their archives */
    for (b = PT_BANK_LO; b <= PT_BANK_HI; b++) {
        const u8 *brec;
        int slot_shared = -1, slot_own = -1, k2;

        if (newn[b] == 0)
            continue;
        brec = base->info[K_BANK].rec[b].p;
        for (k2 = 0; k2 < 4; k2++) {
            u16 a = rd16(brec + 4 + k2 * 2);

            if (a == 700)
                slot_shared = k2;
            else if (a == (u16)b)
                slot_own = k2;
        }
        for (i = 0; i < newn[b]; i++)
            for (r = 0; r < newprog[b][i].nreg; r++) {
                slice w = newwave[b][i][r];
                u32 h;
                int at;

                if (w.p == NULL)
                    continue;
                h = wave_hash(w.p, w.len);
                at = waves_find(&shared, w.p, w.len, h);
                if (at >= 0) {
                    if (slot_shared < 0)
                        die("bank %d has no shared-archive slot", b);
                    wr16(newprog[b][i].reg[r].def, (u16)at);
                    wr16(newprog[b][i].reg[r].def + 2, (u16)slot_shared);
                } else {
                    at = waves_find(&ownw[b], w.p, w.len, h);
                    if (at < 0 || slot_own < 0)
                        die("bank %d has no own-archive slot", b);
                    wr16(newprog[b][i].reg[r].def, (u16)at);
                    wr16(newprog[b][i].reg[r].def + 2, (u16)slot_own);
                }
            }
        base->fat[bank_file(base, b)] = build_sbnk(newprog[b], newn[b]);
        if (b != 700 && slot_own >= 0)
            base->fat[wavearc_file(base, b)] = swar_build(ownw[b].w, ownw[b].n);
    }
    {
        blob sb = swar_build(shared.w, shared.n);

        if (sb.len > SHARED_ARC_BUDGET)
            die("the rebuilt shared archive is %u bytes against a %d budget",
                sb.len, SHARED_ARC_BUDGET);
        base->fat[wavearc_file(base, 700)] = sb;
    }
    free(orig);
    free(shared.w);
    free(shared.hash);
}

/* --------------------------------------------------------- track_apply */
typedef struct {
    int fr_seq;                  /* foreign seq index */
    int seq_file_id;             /* appended sseq file, -1 = skipped */
    int bank_no;
} portmap;

static int closure_append(sdat *dst, const sdat *src, int s_idx,
                          u32 *carried_bytes)
{
    int bank = seq_bank(src, s_idx);
    blob seqb = src->fat[seq_file(src, s_idx)];
    blob bankb = blob_dup(src->fat[bank_file(src, bank)].p,
                          src->fat[bank_file(src, bank)].len);
    scan sc;
    u32 n_inst;
    u32 *defs;
    int ndef, d;
    /* resident: dst waves of name-shared archives, first index wins */
    waves resident = { 0 };
    int *res_arc = NULL, *res_idx = NULL, rescap = 0;
    int slots[4], nslot = 0;
    slice *carried = NULL;
    u32 (*ckey)[2] = NULL;
    int ncar = 0, carcap = 0;
    int k;
    slice *src_arc_samples[4] = { 0 };
    int src_arc_n[4] = { 0 };
    int new_wave, new_bank;

    sseq_scan(seqb, &sc);
    n_inst = rd32(bankb.p + SBNK_COUNT);
    {
        int any = 0, i;

        for (i = 0; i < 128; i++)
            if (bit(sc.progs, i)) { any = 1; break; }
        if (!any)
            die("%s selects no instrument at all", seq_name(src, s_idx));
    }
    {
        int i;

        for (i = (int)n_inst; i < 128; i++)
            sc.progs[i >> 3] &= (u8)~(1 << (i & 7));
    }

    for (k = 0; k < 4; k++) {
        u16 w = rd16(src->info[K_BANK].rec[bank].p + 4 + k * 2);
        const char *nm;
        int here;

        if (w == NO_ARCHIVE)
            continue;
        src_arc_n[k] = swar_split(src->fat[wavearc_file(src, w)],
                                  &src_arc_samples[k]);
        nm = (w < src->names[K_WAVEARC].n && src->names[K_WAVEARC].name[w])
             ? src->names[K_WAVEARC].name[w] : "";
        if (nm[0] == '\0')
            continue;
        here = wavearc_by_name(dst, nm);
        if (here < 0)
            continue;
        {
            slice *sl;
            int nn = swar_split(dst->fat[wavearc_file(dst, here)], &sl);
            int j;

            for (j = 0; j < nn; j++) {
                u32 h = wave_hash(sl[j].p, sl[j].len);

                if (waves_find(&resident, sl[j].p, sl[j].len, h) < 0) {
                    int at = waves_add(&resident, sl[j].p, sl[j].len);

                    if (at >= rescap) {
                        rescap = resident.cap;
                        res_arc = xrealloc(res_arc, (size_t)rescap * sizeof *res_arc);
                        res_idx = xrealloc(res_idx, (size_t)rescap * sizeof *res_idx);
                    }
                    res_arc[at] = here;
                    res_idx[at] = j;
                }
            }
            free(sl);
        }
    }

    ndef = note_defs(bankb, sc.progs, sc.complete ? sc.pairs : NULL, &defs);
    for (d = 0; d < ndef; d++) {
        u16 idx = rd16(bankb.p + defs[d]);
        u16 slot = rd16(bankb.p + defs[d] + 2);
        slice wave;
        u32 h;
        int at;

        if (slot >= 4 || src_arc_samples[slot] == NULL)
            die("an instrument reads wave archive slot %u and the bank names "
                "none there", slot);
        if (idx >= src_arc_n[slot])
            die("an instrument asks for wave %u of an archive with %d",
                idx, src_arc_n[slot]);
        wave = src_arc_samples[slot][idx];
        h = wave_hash(wave.p, wave.len);
        at = waves_find(&resident, wave.p, wave.len, h);
        if (at >= 0) {
            int sl2, have = -1;

            for (sl2 = 0; sl2 < nslot; sl2++)
                if (slots[sl2] == res_arc[at])
                    have = sl2;
            if (have < 0) {
                if (nslot >= 4)
                    die("this track's waves come from more than four archives");
                slots[nslot] = res_arc[at];
                have = nslot++;
            }
            wr16(bankb.p + defs[d], (u16)res_idx[at]);
            wr16(bankb.p + defs[d] + 2, (u16)have);
            continue;
        }
        {
            int c, found = -1;

            for (c = 0; c < ncar; c++)
                if (ckey[c][0] == slot && ckey[c][1] == idx) {
                    found = c;
                    break;
                }
            if (found < 0) {
                if (ncar == carcap) {
                    carcap = carcap ? carcap * 2 : 32;
                    carried = xrealloc(carried, (size_t)carcap * sizeof *carried);
                    ckey = xrealloc(ckey, (size_t)carcap * sizeof *ckey);
                }
                carried[ncar].p = wave.p;
                carried[ncar].len = wave.len;
                ckey[ncar][0] = slot;
                ckey[ncar][1] = idx;
                found = ncar++;
            }
            wr16(bankb.p + defs[d], (u16)found);
            wr16(bankb.p + defs[d] + 2, SLOT_PENDING);
        }
    }

    {
        u32 total = bankb.len, c;

        for (c = 0; c < (u32)ncar; c++)
            total += carried[c].len;
        *carried_bytes = total;
        if (total > CLOSURE_CAP) {
            free(bankb.p);
            free(defs);
            free(resident.w); free(resident.hash);
            free(res_arc); free(res_idx);
            free(carried); free(ckey);
            for (k = 0; k < 4; k++)
                free(src_arc_samples[k]);
            return -1;
        }
    }

    new_wave = dst->info[K_WAVEARC].n;
    {
        blob arc = swar_build(carried, ncar);
        u8 rec[4];
        char *nm;
        const char *want = seq_name(src, s_idx);
        const char *base_nm = strncmp(want, "SEQ_", 4) == 0 ? want + 4 : want;

        wr16(rec, (u16)fat_push(dst, arc));
        wr16(rec + 2, 0);
        nm = xmalloc(strlen(base_nm) + 32);
        sprintf(nm, "WAVE_ARC_%s_PORTED", base_nm);
        list_push(&dst->info[K_WAVEARC], blob_dup(rec, 4), NULL);
        if (dst->has_symb)
            list_push(&dst->names[K_WAVEARC], (blob){ 0, 0 }, nm);
    }
    if (nslot >= 4)
        die("this track's waves come from more than four archives");
    slots[nslot++] = new_wave;
    for (d = 0; d < ndef; d++)
        if (rd16(bankb.p + defs[d] + 2) == SLOT_PENDING)
            wr16(bankb.p + defs[d] + 2, (u16)(nslot - 1));
    {
        u32 i;

        for (i = 0; i < n_inst && i < 128; i++)
            if (!bit(sc.progs, (int)i))
                bankb.p[SBNK_ENTRY + i * 4] = 0;
    }
    new_bank = dst->info[K_BANK].n;
    {
        u8 rec[12];
        int sl2;
        char *nm;
        const char *bn = (bank < src->names[K_BANK].n
                          && src->names[K_BANK].name[bank])
                         ? src->names[K_BANK].name[bank] : "";

        memcpy(rec, src->info[K_BANK].rec[bank].p, 12);
        wr16(rec, (u16)fat_push(dst, bankb));
        for (sl2 = 0; sl2 < 4; sl2++)
            wr16(rec + 4 + sl2 * 2,
                 sl2 < nslot ? (u16)slots[sl2] : NO_ARCHIVE);
        list_push(&dst->info[K_BANK], blob_dup(rec, 12), NULL);
        nm = xmalloc(strlen(bn) + 16);
        sprintf(nm, "%s_PORTED", bn);
        if (dst->has_symb)
            list_push(&dst->names[K_BANK], (blob){ 0, 0 }, nm);
    }
    free(defs);
    free(resident.w); free(resident.hash);
    free(res_arc); free(res_idx);
    free(carried); free(ckey);
    for (k = 0; k < 4; k++)
        free(src_arc_samples[k]);
    return new_bank;
}

static void track_apply(sdat *base, const sdat *fo, int track,
                        portmap **out_map, int *out_n)
{
    portmap *pmap = NULL;
    int npm = 0, pmcap = 0;
    int player_need[8] = { 0 };
    int j;

    for (j = 0; j < MMO_SOUNDTRACK_ROWS; j++) {
        const char *pt_nm = MMO_SOUNDTRACKS[j].pt;
        const char *fr_nm = track == 1 ? MMO_SOUNDTRACKS[j].hg
                                       : MMO_SOUNDTRACKS[j].bw;
        int pt_i, fr_i, m;
        u8 *srec;
        int have = -1;

        if (fr_nm[0] == '\0')
            continue;
        pt_i = seq_by_name(base, pt_nm);
        if (pt_i < 0)
            die("%s is not a sequence the base archive names", pt_nm);
        fr_i = seq_by_name(fo, fr_nm);
        if (fr_i < 0)
            die("%s is not a sequence the donor archive names", fr_nm);
        srec = base->info[K_SEQ].rec[pt_i].p;
        if (srec[9] != 1 && srec[9] != 7)
            die("%s plays on player %u; only field and BGM tracks switch",
                pt_nm, srec[9]);
        for (m = 0; m < npm; m++)
            if (pmap[m].fr_seq == fr_i)
                have = m;
        if (have < 0) {
            u32 carried = 0;
            int nb = closure_append(base, fo, fr_i, &carried);

            if (npm == pmcap) {
                pmcap = pmcap ? pmcap * 2 : 64;
                pmap = xrealloc(pmap, (size_t)pmcap * sizeof *pmap);
            }
            pmap[npm].fr_seq = fr_i;
            if (nb < 0) {
                pmap[npm].seq_file_id = -1;
                pmap[npm].bank_no = -1;
            } else {
                pmap[npm].seq_file_id =
                    fat_push(base, blob_dup(fo->fat[seq_file(fo, fr_i)].p,
                                            fo->fat[seq_file(fo, fr_i)].len));
                pmap[npm].bank_no = nb;
            }
            have = npm++;
        }
        if (pmap[have].seq_file_id < 0)
            continue;
        wr16(srec, (u16)pmap[have].seq_file_id);
        wr16(srec + 4, (u16)pmap[have].bank_no);
        srec[6] = fo->info[K_SEQ].rec[fr_i].p[6];
        {
            u32 need = fo->fat[seq_file(fo, fr_i)].len;
            u8 ply = srec[9];

            if (ply < 8 && (int)need > player_need[ply])
                player_need[ply] = (int)need;
        }
    }
    for (j = 0; j < 8 && j < base->info[K_PLAYER].n; j++) {
        u8 *rec;
        u32 cur;

        if (player_need[j] == 0 || base->info[K_PLAYER].rec[j].p == NULL)
            continue;
        rec = base->info[K_PLAYER].rec[j].p;
        cur = rd32(rec + 4);
        if (cur == 0 || (u32)player_need[j] + 256 <= cur)
            continue;
        if ((u32)player_need[j] + 1024 > 0x10000)
            die("player %d would need a %d-byte sequence buffer", j,
                player_need[j] + 1024);
        wr32(rec + 4, (u32)player_need[j] + 1024);
    }
    *out_map = pmap;
    *out_n = npm;
}

/* --------------------------------------------------------- refont_apply */
static void refont_apply(sdat *base, const sdat *fo, int track,
                         const portmap *pmap, int npm)
{
    int chain[128];
    program master[128];
    int master_bank[128], have_master[128] = { 0 };
    slice *a700;
    int n700, i, m;
    u8 *done = NULL;

    for (i = 0; i < 128; i++)
        chain[i] = -1;
    if (track == 2) {
        /*
         * Black's banks name their instruments per song, not by the ancestral numbering; the
         * identity chain re-voiced carried Black tracks into the wrong instruments. They keep
         * their own voices until an acoustic chain earns its place, the python pass says the
         * same.
         */
        return;
    }
    {
        for (i = 0; i < MMO_SOUNDFONT_ROWS; i++) {
            int q = MMO_SOUNDFONTS[i].hg;

            if (q >= 0 && q < 128 && chain[q] < 0)
                chain[q] = MMO_SOUNDFONTS[i].pt;
        }
    }
    {
        program *mast;
        int bank_of[128] = { 0 };
        int n = dst_master(base, &mast, bank_of);
        int p;

        (void)n;
        for (p = 0; p < 128; p++) {
            int b2;
            program *ps;
            int np, ii;

            for (b2 = PT_BANK_LO; b2 <= PT_BANK_HI && !have_master[p]; b2++) {
                np = typed_instruments(base, b2, &ps);
                for (ii = 0; ii < np; ii++)
                    if (ps[ii].prog == p) {
                        master[p] = ps[ii];
                        master_bank[p] = b2;
                        have_master[p] = 1;
                        break;
                    }
                free(ps);
            }
        }
        free(mast);
    }
    n700 = swar_split(base->fat[wavearc_file(base, 700)], &a700);
    done = xmalloc((size_t)base->info[K_BANK].n);
    memset(done, 0, (size_t)base->info[K_BANK].n);

    for (m = 0; m < npm; m++) {
        int bnk = pmap[m].bank_no;
        program *ps;
        int np, ii, r;
        u8 tracknotes[128 * 128 / 8];
        scan sc;
        int changed = 0;
        u16 arcs[4];
        int own_slot = -1, slot700 = -1, k2;
        int own_arc;
        slice *own_s;
        int own_n, own_cap;
        slice *grown;

        if (bnk < 0 || done[bnk])
            continue;
        done[bnk] = 1;
        memset(tracknotes, 0, sizeof tracknotes);
        {
            int mm;

            for (mm = 0; mm < npm; mm++) {
                if (pmap[mm].bank_no != bnk)
                    continue;
                sseq_scan(fo->fat[seq_file(fo, pmap[mm].fr_seq)], &sc);
                for (r = 0; r < 128 * 128; r++)
                    if (bit(sc.pairs, r))
                        setbit(tracknotes, r);
            }
        }
        for (k2 = 0; k2 < 4; k2++) {
            arcs[k2] = rd16(base->info[K_BANK].rec[bnk].p + 4 + k2 * 2);
            if (arcs[k2] != NO_ARCHIVE)
                own_slot = k2;
            if (arcs[k2] == 700)
                slot700 = k2;
        }
        own_arc = arcs[own_slot];
        if (slot700 < 0) {
            for (k2 = 0; k2 < 4; k2++)
                if (arcs[k2] == NO_ARCHIVE) {
                    u8 *rec = base->info[K_BANK].rec[bnk].p;

                    wr16(rec + 4 + k2 * 2, 700);
                    slot700 = k2;
                    break;
                }
        }
        own_n = swar_split(base->fat[wavearc_file(base, own_arc)], &own_s);
        own_cap = own_n + 64;
        grown = xmalloc((size_t)own_cap * sizeof *grown);
        memcpy(grown, own_s, (size_t)own_n * sizeof *own_s);

        np = typed_instruments(base, bnk, &ps);
        for (ii = 0; ii < np; ii++) {
            int q = ps[ii].prog;
            int p = q < 128 ? chain[q] : -1;
            program np2;
            int ok = 1;

            if (p < 0 || !have_master[p])
                continue;
            np2 = master[p];
            np2.prog = q;
            if (np2.kind == 16) {
                for (r = 0; r < 128 && ok; r++)
                    if (bit(tracknotes, q * 128 + r)
                        && !(r >= np2.meta[0] && r <= np2.meta[1]))
                        ok = 0;
            }
            for (r = 0; r < np2.nreg && ok; r++) {
                if (np2.reg[r].kind >= 1 && np2.reg[r].kind <= 5) {
                    slice w = region_wave(base, master_bank[p],
                                          master[p].reg[r].def);
                    /* the carried track's own performance stays; the font
                     * lends only the voice and its root note */
                    if (ps[ii].nreg > 0) {
                        int pr = r < ps[ii].nreg ? r : ps[ii].nreg - 1;

                        memcpy(np2.reg[r].def + 5, ps[ii].reg[pr].def + 5, 5);
                    }
                    u32 h;
                    int at, jj, found;

                    if (w.p == NULL) {
                        ok = 0;
                        break;
                    }
                    h = wave_hash(w.p, w.len);
                    at = -1;
                    for (jj = 0; jj < n700; jj++)
                        if (a700[jj].len == w.len
                            && wave_hash(a700[jj].p, a700[jj].len) == h
                            && memcmp(a700[jj].p, w.p, w.len) == 0) {
                            at = jj;
                            break;
                        }
                    if (at >= 0 && slot700 >= 0) {
                        wr16(np2.reg[r].def, (u16)at);
                        wr16(np2.reg[r].def + 2, (u16)slot700);
                        continue;
                    }
                    found = -1;
                    for (jj = 0; jj < own_n; jj++)
                        if (grown[jj].len == w.len
                            && memcmp(grown[jj].p, w.p, w.len) == 0) {
                            found = jj;
                            break;
                        }
                    if (found < 0) {
                        if (own_n == own_cap) {
                            own_cap *= 2;
                            grown = xrealloc(grown,
                                             (size_t)own_cap * sizeof *grown);
                        }
                        grown[own_n] = w;
                        found = own_n++;
                    }
                    wr16(np2.reg[r].def, (u16)found);
                    wr16(np2.reg[r].def + 2, (u16)own_slot);
                }
            }
            if (ok) {
                ps[ii] = np2;
                changed = 1;
            }
        }
        if (changed) {
            base->fat[bank_file(base, bnk)] = build_sbnk(ps, np);
            base->fat[wavearc_file(base, own_arc)] = swar_build(grown, own_n);
        }
        free(ps);
        free(own_s);
        free(grown);
    }
    free(a700);
    free(done);
}

/* --------------------------------------------------------- the driver */
static const char *rom_sdat_path(const char *code)
{
    if (strncmp(code, "CPU", 3) == 0)
        return "data/sound/pl_sound_data.sdat";
    if (strncmp(code, "IPK", 3) == 0 || strncmp(code, "IPG", 3) == 0)
        return "data/sound/gs_sound_data.sdat";
    if (strncmp(code, "IRB", 3) == 0 || strncmp(code, "IRA", 3) == 0)
        return "wb_sound_data.sdat";
    return NULL;
}

static void load_game(sdat *out, const char *path, const char *label)
{
    blob raw = read_file(path);

    if (raw.len >= 4 && memcmp(raw.p, "SDAT", 4) == 0) {
        sdat_parse(out, raw);
        return;
    }
    if (raw.len >= 0x10) {
        char code[5] = { 0 };
        const char *inner;

        memcpy(code, raw.p + 0x0C, 4);
        inner = rom_sdat_path(code);
        if (inner != NULL) {
            blob sd = nds_file(raw, inner, label);

            sdat_parse(out, sd);
            return;
        }
    }
    die("%s is neither a sound archive nor a cartridge this composes from",
        label);
}

/* ------------------------------------------------------------------ cries */
/* The 156 cries a gen 5 cartridge adds, as host PCM. */
#define CRY_FIRST   494
#define CRY_LAST    649

/* The NDS/IMA step walk, the arithmetic the hardware decodes with. */
static const short CRY_STEPS[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41,
    45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190,
    209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724,
    796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
    2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132,
    7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500,
    20350, 22385, 24623, 27086, 29794, 32767
};
static const signed char CRY_INDEX_STEP[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

static u32 cry_adpcm(const u8 *p, u32 len, short *out)
{
    int sample = (short)rd16(p);
    int index = rd16(p + 2);
    u32 at = 0, i;

    if (index > 88)
        index = 88;
    for (i = 4; i < len; i++) {
        int half;

        for (half = 0; half < 2; half++) {
            int nib = half == 0 ? (p[i] & 0xF) : (p[i] >> 4);
            int step = CRY_STEPS[index];
            int diff = step >> 3;

            if (nib & 1)
                diff += step >> 2;
            if (nib & 2)
                diff += step >> 1;
            if (nib & 4)
                diff += step;
            sample = (nib & 8) ? sample - diff : sample + diff;
            if (sample < -32768)
                sample = -32768;
            if (sample > 32767)
                sample = 32767;
            index += CRY_INDEX_STEP[nib & 7];
            if (index < 0)
                index = 0;
            if (index > 88)
                index = 88;
            if (out != NULL)
                out[at] = (short)sample;
            at++;
        }
    }
    return at;
}

/* One SWAV's samples as PCM16. Returns the count; `out` may be NULL to ask
 * only how many there would be. */
static u32 cry_decode(const slice *s, int *rate, short *out, int species)
{
    u32 kind, len;
    const u8 *data;

    if (s->len < 12)
        die("species %d's cry is %u bytes, too short to have a header",
            species, s->len);
    kind = s->p[0];
    *rate = (int)rd16(s->p + 2);
    data = s->p + 12;
    len = s->len - 12;
    switch (kind) {
    case 0: {                   /* PCM8, and on the DS it is signed */
        u32 i;

        if (out != NULL) {
            for (i = 0; i < len; i++)
                out[i] = (short)((signed char)data[i] << 8);
        }
        return len;
    }
    case 1: {                   /* PCM16 already */
        u32 i, n = len / 2;

        if (out != NULL) {
            for (i = 0; i < n; i++)
                out[i] = (short)rd16(data + i * 2);
        }
        return n;
    }
    case 2:
        return cry_adpcm(data, len, out);
    default:
        die("species %d's cry is sample kind %u, which this has no decoder for",
            species, kind);
    }
    return 0;
}

int mmo_soundcompose_cries(const char *bw_rom, const char *out_path,
                           char *err, size_t errcap)
{
    static sdat bw;
    jmp_buf jb;
    struct { int species, rate; u32 n; short *pcm; } *got;
    int count = 0, species, i;
    u32 total = 0, offset;
    blob out;
    FILE *f;

    if (err != NULL && errcap > 0)
        err[0] = '\0';
    if (setjmp(jb))
        return -1;
    mmo_nitro_catch(&jb, err, errcap);
    if (bw_rom == NULL || bw_rom[0] == '\0')
        die("the ported cries need a Black or White cartridge");

    load_game(&bw, bw_rom, "the Black cartridge");
    got = xmalloc((size_t)(CRY_LAST - CRY_FIRST + 1) * sizeof *got);
    for (species = CRY_FIRST; species <= CRY_LAST; species++) {
        slice *sl = NULL;
        int n, rate = 0;
        u16 arc = wavearc_file(&bw, species);

        if (arc >= bw.nfat || bw.fat[arc].p == NULL)
            continue;
        n = swar_split(bw.fat[arc], &sl);
        if (n <= 0) {
            free(sl);
            continue;           /* an empty wave arc; the stand-in answers */
        }
        got[count].species = species;
        got[count].n = cry_decode(&sl[0], &rate, NULL, species);
        got[count].rate = rate;
        got[count].pcm = xmalloc((size_t)got[count].n * sizeof(short));
        cry_decode(&sl[0], &rate, got[count].pcm, species);
        total += got[count].n;
        count++;
        free(sl);
    }
    if (count == 0)
        die("that cartridge holds no cries past species %d", CRY_FIRST - 1);

    out.len = 8 + (u32)count * 16 + total * 2;
    out.p = xmalloc(out.len);
    memcpy(out.p, "OCRY", 4);
    wr16(out.p + 4, (u16)count);
    wr16(out.p + 6, 0);
    offset = 8 + (u32)count * 16;
    for (i = 0; i < count; i++) {
        u8 *row = out.p + 8 + (u32)i * 16;

        wr16(row, (u16)got[i].species);
        wr16(row + 2, (u16)got[i].rate);
        wr32(row + 4, got[i].n);
        wr32(row + 8, offset);
        wr32(row + 12, 0);
        memcpy(out.p + offset, got[i].pcm, (size_t)got[i].n * sizeof(short));
        offset += got[i].n * 2;
        free(got[i].pcm);
    }
    free(got);

    f = fopen(out_path, "wb");
    if (f == NULL)
        die("cannot write %s", out_path);
    if (fwrite(out.p, 1, out.len, f) != out.len) {
        fclose(f);
        die("cannot write %s", out_path);
    }
    fclose(f);
    free(out.p);
    mmo_nitro_catch(NULL, NULL, 0);
    return 0;
}

int mmo_soundcompose(int track, int font, const char *pt_rom,
                     const char *hg_rom, const char *bw_rom,
                     const char *out_path, char *err, size_t errcap)
{
    static sdat base, fo_font, fo_track;
    const char *font_rom, *track_rom;
    jmp_buf jb;

    if (setjmp(jb))
        return -1;
    mmo_nitro_catch(&jb, err, errcap);
    if (track == 0 && font == 0)
        die("platinum with platinum is the stock archive; nothing to compose");
    font_rom = font == 1 ? hg_rom : font == 2 ? bw_rom : NULL;
    track_rom = track == 1 ? hg_rom : track == 2 ? bw_rom : NULL;
    if (font != 0 && (font_rom == NULL || font_rom[0] == '\0'))
        die("that soundfont needs its cartridge");
    if (track != 0 && (track_rom == NULL || track_rom[0] == '\0'))
        die("that soundtrack needs its cartridge");

    load_game(&base, pt_rom, "the Platinum cartridge");
    if (font != 0) {
        load_game(&fo_font, font_rom, "the soundfont cartridge");
        font_apply(&base, &fo_font, font);
    }
    if (track != 0) {
        portmap *pmap = NULL;
        int npm = 0;

        load_game(&fo_track, track_rom, "the soundtrack cartridge");
        track_apply(&base, &fo_track, track, &pmap, &npm);
        if (font != track)
            refont_apply(&base, &fo_track, track, pmap, npm);
        free(pmap);
    }
    {
        blob outb = sdat_build(&base);
        FILE *f = fopen(out_path, "wb");

        if (f == NULL)
            die("cannot write %s", out_path);
        if (fwrite(outb.p, 1, outb.len, f) != outb.len) {
            fclose(f);
            die("cannot write %s", out_path);
        }
        fclose(f);
        free(outb.p);
    }
    return 0;
}

/* =================================================================== */
/* the launcher's side of the door                                      */
/* =================================================================== */
#include <dirent.h>
#include <sys/stat.h>

#include "launch_plan.h"
#include "platform.h"

/* The pair the settings name, font following the soundtrack unless pinned.
 * Returns 0 for platinum-with-platinum: the stock archive, no package. */
int mmo_sound_pair(const mmo_launch_settings *s, int *track, int *font)
{
    *track = s->soundtrack;
    *font = (s->soundfont == MMO_SOUNDFONT_FOLLOW)
            ? s->soundtrack : s->soundfont - 1;
    return *track != 0 || *font != 0;
}

static const char *const SLOT_SLUG[3] = { "pt", "hg", "bw" };
static const char *const SLOT_NAME[3] = { "Platinum", "Heart Gold",
                                          "Black & White" };

/* the install's mods folder: the cfg row when its folder actually exists
 * (an r740-era install wrote its own absolute path here and then got
 * deleted; a dead row reads as unset, launch_plan.c says why), or mods/
 * beside the game's bin/ */
static int sc_dir_exists(const char *path)
{
    struct stat st;

    return path[0] != '\0' && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* the folder the Platinum cartridge sits in */
static int rom_folder(const mmo_launch_settings *s, char *out, size_t cap)
{
    char file[MMO_LAUNCH_PATH];
    const char *slash;

    if (mmo_launch_rom_file(s->rom, file, sizeof file) != 0)
        return -1;
    slash = mmo_plat_last_sep(file);
    if (slash == NULL)
        snprintf(out, cap, ".");
    else
        snprintf(out, cap, "%.*s", (int)(slash - file), file);
    return 0;
}

#include "cartridge.h"

/* what a file's header says it is, through the registry's own names */
static const char *file_cart_name(const char *path, char *code4)
{
    FILE *f = fopen(path, "rb");
    static char code[5];
    const MmoCartridge *c;

    code[0] = '\0';
    if (f == NULL)
        return NULL;
    if (fseek(f, 0x0C, SEEK_SET) != 0 || fread(code, 1, 4, f) != 4) {
        fclose(f);
        return NULL;
    }
    fclose(f);
    code[4] = '\0';
    if (code4 != NULL)
        memcpy(code4, code, 5);
    c = mmo_cartridge_by_code(code);
    if (c == NULL)
        c = mmo_cartridge_slot_of(code);
    return c != NULL ? c->name : code;
}

/* The official arrangement: every cartridge is a labelled slot of its own. */
int mmo_sound_slot_status(const mmo_launch_settings *s, int slot,
                          char *file, size_t filecap,
                          char *why, size_t whycap)
{
    static char last_path[2][MMO_LAUNCH_PATH];
    static char last_dir[2][MMO_LAUNCH_PATH];
    static char last_file[2][MMO_LAUNCH_PATH];
    static char last_why[2][160];
    static int last_rc[2] = { -1, -1 };
    static int age[2];
    int ix = slot == 1 ? 0 : 1;
    const char *path = slot == 1 ? s->rom_hg : s->rom_bw;
    char dir[MMO_LAUNCH_PATH] = "";

    rom_folder(s, dir, sizeof dir);
    if (last_rc[ix] < 0 || strcmp(path, last_path[ix]) != 0
        || strcmp(dir, last_dir[ix]) != 0 || ++age[ix] > 120) {
        const char *slotname = SLOT_NAME[slot];

        age[ix] = 0;
        snprintf(last_path[ix], sizeof last_path[ix], "%s", path);
        snprintf(last_dir[ix], sizeof last_dir[ix], "%s", dir);
        last_file[ix][0] = '\0';
        if (path[0] != '\0') {
            char code[5] = "";
            const char *nm = file_cart_name(path, code);
            /* The same gate the front door uses, off the same registry: the
             * settings face and a refusal must not disagree about what a
             * cartridge is. */
            int right = mmo_launch_cart_is(slot, code);
            const char *base = mmo_plat_last_sep(path);
            FILE *mf = fopen(path, "rb");
            char magic[4] = "";

            /* a bare sound archive is the dev door and carries no header
             * code to check; the compose validates it by use */
            if (mf != NULL) {
                if (fread(magic, 1, 4, mf) == 4
                    && memcmp(magic, "SDAT", 4) == 0) {
                    right = 1;
                    nm = "a sound archive";
                }
                fclose(mf);
            }
            base = base != NULL ? base + 1 : path;
            if (nm == NULL) {
                snprintf(last_why[ix], sizeof last_why[ix],
                         "that cartridge cannot be read");
                last_rc[ix] = 0;
            } else if (!right) {
                snprintf(last_why[ix], sizeof last_why[ix],
                         "that is %s, not %s", nm, slotname);
                last_rc[ix] = 0;
            } else {
                snprintf(last_file[ix], sizeof last_file[ix], "%s", path);
                snprintf(last_why[ix], sizeof last_why[ix],
                         "%s ready: %s", nm, base);
                last_rc[ix] = 1;
            }
        } else if ((dir[0] != '\0'
                    && mmo_launch_cart_scan(dir, slot, last_file[ix],
                                            sizeof last_file[ix]))
                   || mmo_launch_cart_scan(mmo_launch_roms_fallback_dir(),
                                           slot, last_file[ix],
                                           sizeof last_file[ix])) {
            const char *nm = file_cart_name(last_file[ix], NULL);
            const char *base = mmo_plat_last_sep(last_file[ix]);

            base = base != NULL ? base + 1 : last_file[ix];
            snprintf(last_why[ix], sizeof last_why[ix],
                     "%s found beside Platinum: %s",
                     nm != NULL ? nm : slotname, base);
            last_rc[ix] = 1;
        } else {
            snprintf(last_why[ix], sizeof last_why[ix],
                     "none chosen, and none beside Platinum");
            last_rc[ix] = 0;
        }
    }
    if (file != NULL)
        snprintf(file, filecap, "%s", last_file[ix]);
    if (why != NULL)
        snprintf(why, whycap, "%s", last_why[ix]);
    return last_rc[ix];
}

static int write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");

    if (f == NULL)
        return -1;
    if (fwrite(text, 1, strlen(text), f) != strlen(text)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

/* The Play-time half: the named pair's package exists under the install's
 * mods folder with the tables' own stamp, or it gets composed right here
 * from the player's cartridges, and a cartridge that is not there is a
 * refusal that names the slot and leaves the launcher standing. */
int mmo_soundcompose_ensure(const mmo_launch_settings *s, const char *port_exe,
                            void (*note)(void *ud, const char *line), void *ud,
                            char *err, size_t errcap)
{
    int track, font;
    char root[MMO_LAUNCH_PATH];
    char pkg[MMO_LAUNCH_PATH + 32];
    char path[MMO_LAUNCH_PATH + 96];
    char stamp[MMO_LAUNCH_PATH + 96];
    char want_stamp[32];
    char pt_file[MMO_LAUNCH_PATH];
    char hg_file[MMO_LAUNCH_PATH] = "";
    char bw_file[MMO_LAUNCH_PATH] = "";
    char tmp[MMO_LAUNCH_PATH + 100];
    char line[192];

    if (!mmo_sound_pair(s, &track, &font))
        return 0;
    mmo_launch_mods_root(s, port_exe, root, sizeof root);
    snprintf(pkg, sizeof pkg, "%s%ssound_%s_%s", root, mmo_plat_sep(),
             SLOT_SLUG[track], SLOT_SLUG[font]);
    snprintf(path, sizeof path, "%s%sreplace%sdata%ssound%spl_sound_data.sdat",
             pkg, mmo_plat_sep(), mmo_plat_sep(), mmo_plat_sep(),
             mmo_plat_sep());
    snprintf(stamp, sizeof stamp, "%s%scomposed.txt", pkg, mmo_plat_sep());
    snprintf(want_stamp, sizeof want_stamp, "%016llX\n",
             (unsigned long long)MMO_SOUNDTABLES_HASH);
    {
        FILE *f = fopen(path, "rb");
        FILE *g = fopen(stamp, "rb");
        char have[32] = "";

        if (g != NULL) {
            size_t n = fread(have, 1, sizeof have - 1, g);

            have[n] = '\0';
            fclose(g);
        }
        if (f != NULL) {
            fclose(f);
            if (strcmp(have, want_stamp) == 0)
                return 0;       /* composed under these very tables */
        }
    }

    /* the cartridges this pair needs */
    if (mmo_launch_rom_file(s->rom, pt_file, sizeof pt_file) != 0) {
        snprintf(err, errcap, "the ROM cannot be read: %s", s->rom);
        return -1;
    }
    {
        char why[160];
        int need_hg = track == 1 || font == 1;
        int need_bw = track == 2 || font == 2;

        if (need_hg
            && !mmo_sound_slot_status(s, 1, hg_file, sizeof hg_file,
                                      why, sizeof why)) {
            snprintf(err, errcap, "that sound needs a Heart Gold cartridge "
                     "(%s): choose one on the settings face, or put it "
                     "beside %s", why, MMO_LAUNCH_ROM_NAME);
            return -1;
        }
        if (need_bw
            && !mmo_sound_slot_status(s, 2, bw_file, sizeof bw_file,
                                      why, sizeof why)) {
            snprintf(err, errcap, "that sound needs a Black or White "
                     "cartridge (%s): choose one on the settings face, or "
                     "put it beside %s", why, MMO_LAUNCH_ROM_NAME);
            return -1;
        }
    }

    if (note != NULL) {
        if (track == font || font == 0)
            snprintf(line, sizeof line, "composing the %s soundtrack from "
                     "your cartridge...", SLOT_NAME[track]);
        else
            snprintf(line, sizeof line, "composing the %s soundtrack in the "
                     "%s soundfont from your cartridges...",
                     SLOT_NAME[track], SLOT_NAME[font]);
        note(ud, line);
    }

    /* the package tree, then the archive beside it, then the rename over */
    snprintf(tmp, sizeof tmp, "%s", pkg);
    mmo_plat_mkdir(root);
    mmo_plat_mkdir(tmp);
    snprintf(tmp, sizeof tmp, "%s%sreplace", pkg, mmo_plat_sep());
    mmo_plat_mkdir(tmp);
    snprintf(tmp, sizeof tmp, "%s%sreplace%sdata", pkg, mmo_plat_sep(),
             mmo_plat_sep());
    mmo_plat_mkdir(tmp);
    snprintf(tmp, sizeof tmp, "%s%sreplace%sdata%ssound", pkg,
             mmo_plat_sep(), mmo_plat_sep(), mmo_plat_sep());
    mmo_plat_mkdir(tmp);

    snprintf(tmp, sizeof tmp, "%s.new", path);
    if (mmo_soundcompose(track, font, pt_file, hg_file, bw_file, tmp,
                         err, errcap) != 0)
        return -1;
    if (mmo_plat_rename_over(tmp, path) != 0) {
        snprintf(err, errcap, "composed, but cannot move the archive into "
                 "%s", pkg);
        return -1;
    }
    {
        char toml[MMO_LAUNCH_PATH + 96];
        char body[256];

        snprintf(toml, sizeof toml, "%s%smod.toml", pkg, mmo_plat_sep());
        snprintf(body, sizeof body,
                 "id = \"sound_%s_%s\"\n"
                 "name = \"%s Soundtrack, %s Soundfont\"\n"
                 "version = \"1.0.0\"\nauthors = [\"openmmo\"]\n"
                 "requires = []\nload_after = []\n",
                 SLOT_SLUG[track], SLOT_SLUG[font],
                 SLOT_NAME[track], SLOT_NAME[font]);
        if (write_text(toml, body) != 0 || write_text(stamp, want_stamp) != 0) {
            snprintf(err, errcap, "composed, but cannot write the package "
                     "files in %s", pkg);
            return -1;
        }
    }
    if (note != NULL)
        note(ud, "composed; it is kept for next time");
    return 0;
}
