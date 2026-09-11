/* See the header. */

#include "nitrorom.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* ------------------------------------------------------------------ err */
static jmp_buf *sJmp;
static char *sErr;
static size_t sErrCap;

void mmo_nitro_catch(jmp_buf *jb, char *err, size_t cap)
{
    sJmp = jb;
    sErr = err;
    sErrCap = cap;
}

void mmo_nitro_die(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    if (sErr != NULL && sErrCap > 0)
        vsnprintf(sErr, sErrCap, fmt, ap);
    va_end(ap);
    /* Nobody armed a landing place: a refusal here is a programming error
     * rather than a bad cartridge, and exiting says so where a return would
     * hand back a half-read image. */
    if (sJmp == NULL)
        abort();
    longjmp(*sJmp, 1);
}

void *mmo_nitro_alloc(size_t n)
{
    void *p = malloc(n ? n : 1);

    if (p == NULL)
        mmo_nitro_die("out of memory (%zu bytes)", n);
    return p;
}

void *mmo_nitro_grow(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);

    if (q == NULL)
        mmo_nitro_die("out of memory (%zu bytes)", n);
    return q;
}

/* ------------------------------------------------------------------ blob */
u16 mmo_nitro_rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }

u32 mmo_nitro_rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

void mmo_nitro_wr16(u8 *p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }

void mmo_nitro_wr32(u8 *p, u32 v)
{
    p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

MmoBlob mmo_nitro_dup(const u8 *p, u32 len)
{
    MmoBlob b;

    b.p = mmo_nitro_alloc(len);
    if (len)
        memcpy(b.p, p, len);
    b.len = len;
    return b;
}

MmoBlob mmo_nitro_read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    MmoBlob b;

    if (f == NULL)
        mmo_nitro_die("cannot open %s", path);
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) {
        fclose(f);
        mmo_nitro_die("cannot size %s", path);
    }
    b.p = mmo_nitro_alloc((size_t)n);
    b.len = (u32)n;
    if (fread(b.p, 1, b.len, f) != b.len) {
        fclose(f);
        mmo_nitro_die("cannot read %s", path);
    }
    fclose(f);
    return b;
}

/* ------------------------------------------------------------------- nds */
MmoBlob mmo_nitro_file(const MmoBlob rom, const char *want, const char *label,
                       const char *hint)
{
    u32 fnt_off, fnt_sz, fat_off;
    const u8 *fnt, *fat;
    struct walk { u32 dir; char prefix[192]; } stack[64];
    int top = 0;

    if (rom.len < 0x160)
        mmo_nitro_die("%s is too small to be a cartridge image", label);
    fnt_off = mmo_nitro_rd32(rom.p + 0x40);
    fnt_sz = mmo_nitro_rd32(rom.p + 0x44);
    fat_off = mmo_nitro_rd32(rom.p + 0x48);
    if (fnt_off + fnt_sz > rom.len || fnt_sz < 8 || fat_off > rom.len)
        mmo_nitro_die("%s has a broken NitroFS table", label);
    fnt = rom.p + fnt_off;
    fat = rom.p + fat_off;
    stack[top].dir = 0xF000;
    stack[top].prefix[0] = '\0';
    top++;
    while (top > 0) {
        u32 off, p;
        u16 first, fid;
        char prefix[192];

        top--;
        off = mmo_nitro_rd32(fnt + (stack[top].dir & 0xFFF) * 8);
        first = mmo_nitro_rd16(fnt + (stack[top].dir & 0xFFF) * 8 + 4);
        snprintf(prefix, sizeof prefix, "%s", stack[top].prefix);
        p = off;
        fid = first;
        while (p < fnt_sz && fnt[p] != 0) {
            u8 flag = fnt[p++];
            u8 namelen = flag & 0x7F;
            char name[224];

            if (p + namelen > fnt_sz)
                mmo_nitro_die("%s has a broken name table", label);
            snprintf(name, sizeof name, "%s%.*s", prefix, namelen, fnt + p);
            p += namelen;
            if (flag & 0x80) {
                u16 sub = mmo_nitro_rd16(fnt + p);

                p += 2;
                if (top < 64) {
                    stack[top].dir = sub;
                    snprintf(stack[top].prefix, sizeof stack[top].prefix,
                             "%s/", name);
                    top++;
                }
            } else {
                if (strcmp(name, want) == 0) {
                    u32 a = mmo_nitro_rd32(fat + fid * 8);
                    u32 b = mmo_nitro_rd32(fat + fid * 8 + 4);

                    if (b > rom.len || a > b)
                        mmo_nitro_die("%s: %s is off the end of the image",
                                      label, want);
                    return mmo_nitro_dup(rom.p + a, b - a);
                }
                fid++;
            }
        }
    }
    if (hint != NULL)
        mmo_nitro_die("%s holds no %s, %s", label, want, hint);
    mmo_nitro_die("%s holds no %s", label, want);
    return rom;                 /* unreached; die longjmps */
}

MmoMembers mmo_nitro_narc(MmoBlob arc, const char *what)
{
    u32 off = 16, i;
    const u8 *fat = NULL;
    u32 nfat = 0, gmif = 0;
    MmoMembers out;

    if (arc.len < 16 || memcmp(arc.p, "NARC", 4) != 0)
        mmo_nitro_die("%s is not a NARC on that cartridge", what);
    while (off + 8 <= arc.len) {
        u32 size = mmo_nitro_rd32(arc.p + off + 4);

        if (size < 8)
            break;
        if (memcmp(arc.p + off, "BTAF", 4) == 0) {
            nfat = mmo_nitro_rd32(arc.p + off + 8);
            fat = arc.p + off + 12;
            if (off + 12 + (u64)nfat * 8 > arc.len)
                mmo_nitro_die("%s has a file table longer than the archive",
                              what);
        } else if (memcmp(arc.p + off, "GMIF", 4) == 0) {
            gmif = off + 8;
        }
        off += size;
    }
    if (fat == NULL || gmif == 0)
        mmo_nitro_die("%s has no BTAF/GMIF, it is not a NARC", what);
    out.n = nfat;
    out.m = mmo_nitro_alloc((size_t)nfat * sizeof *out.m);
    for (i = 0; i < nfat; i++) {
        u32 a = mmo_nitro_rd32(fat + i * 8);
        u32 b = mmo_nitro_rd32(fat + i * 8 + 4);

        if (a > b || gmif + b > arc.len)
            mmo_nitro_die("%s member %u runs off the end of the archive",
                          what, i);
        out.m[i].p = arc.p + gmif + a;
        out.m[i].len = b - a;
    }
    return out;
}
