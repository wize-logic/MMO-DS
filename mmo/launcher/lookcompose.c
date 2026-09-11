/*
 * Portlooks.py in C: the four player looks, out of the player's own three
 * cartridges, at Play.
 */
#include <dirent.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "blackcompose.h"
#include "lookcompose.h"
#include "nitrorom.h"
#include "platform.h"
#include "followcompose.h"
#include "soundcompose.h"
#include "../include/appearance.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef MmoBlob blob;
typedef MmoMembers members;
#define die           mmo_nitro_die
#define xmalloc       mmo_nitro_alloc
#define rd16          mmo_nitro_rd16
#define rd32          mmo_nitro_rd32
#define wr16          mmo_nitro_wr16
#define wr32          mmo_nitro_wr32
#define read_file     mmo_nitro_read_file
#define narc_members  mmo_nitro_narc

/* ---------------------------------------------------------------- tables */
/* portlooks.py's constants, under the same names. */
#define LOOKS 4
static const int kLookGender[LOOKS] = { 0, 1, 0, 1 };
static const int kLookIsBlack[LOOKS] = { 0, 0, 1, 1 };

enum { WALK, BIKE, SURF, FIELD_MOVE, FISHING, SAVE, HEAL, POKETCH, SPRAYDUCK, STATES };

static const int kLike[STATES][2] = {
    { 0, 97 }, { 21, 98 }, { 178, 179 }, { 176, 177 }, { 188, 189 },
    { 198, 199 }, { 200, 201 }, { 196, 197 }, { 180, 181 },
};
static const int kPtMmodel[STATES][2] = {
    { 90, 91 }, { 92, 93 }, { 159, 160 }, { 155, 156 }, { 166, 167 },
    { 365, 366 }, { 367, 368 }, { 363, 364 }, { 157, 158 },
};
static const struct { int member[2]; const char *name[2]; } kHgMmodel[STATES] = {
    { { 69, 70 }, { "hero", "heroine" } },
    { { 71, 72 }, { "cyclehero", "cycleheroine" } },
    { { 73, 74 }, { "swimhero", "swimheroine" } },
    { { 75, 76 }, { "sphero", "spheroine" } },
    { { 79, 80 }, { "fishinghero", "fish_heroine" } },
    { { 87, 88 }, { "savehero", "saveheroine" } },
    { { 89, 90 }, { "banzaihero", "banzaiheroine" } },
    { { 85, 86 }, { "pokehero", "pokeheroine" } },
    { { 77, 78 }, { "waterhero", "waterheroine" } },
};
#define HG_MMODEL_ARC "a/0/8/1"
#define HG_TRFGRA     "a/0/5/8"
#define HG_TRBGRA     "a/0/0/6"
#define HG_CARD       "a/0/4/9"
#define HG_CARD_TILES   44
#define HG_CARD_TX 23
#define HG_CARD_TY 6
#define HG_CARD_TW 5
#define HG_CARD_TH 9
#define HG_CARD_INSET 3
static const int kHgCardScreen[2] = { 54, 55 };

#define BW_OW_ARC  "a/0/4/9"
#define BW_TRFGRA  "a/0/7/2"
#define BW_TRBGRA  "a/0/6/4"
#define BW_ENTRY   8
#define BW_MAX_ENTRIES 95
#define BW_BACK_FRAMES 8
static const u32 kBwBackTicks[BW_BACK_FRAMES] = { 0, 8, 24, 38, 40, 44, 48, 60 };
#define BW_BACK_SCALE 0.75
#define BW_BACK_TOP   (-118)

static const int kBwWalk[32] = { 0, 1, 0, 2, 3, 4, 3, 5, 6, 7, 6, 8, 9, 10, 9, 11,
                                 12, 13, 12, 14, 15, 16, 15, 17, 18, 19, 18, 20, 21, 22, 21, 23 };
static const int kBwBike[24] = { 0, 1, 0, 2, 3, 4, 3, 5, 6, 7, 6, 8, 10, 11, 10, 14,
                                 12, 12, 13, 13, 12, 13, 9, 15 };
static const int kBwSurf[4] = { 0, 1, 2, 3 };
static const int kBwField[4] = { 0, 1, 2, 3 };
static const int kBwFishing[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
static const int kBwSave[2] = { 0, 1 };
static const struct {
    int state;
    int member[2];
    const char *name;
    const int *order;
    int count;
} kBwOw[] = {
    { WALK, { 6, 9 }, "t4x4hero", kBwWalk, 32 },
    { BIKE, { 7, 10 }, "t4x4cycle", kBwBike, 24 },
    { SURF, { 8, 11 }, "t4x4swim", kBwSurf, 4 },
    { FIELD_MOVE, { 152, 153 }, "t4x4cutinhero", kBwField, 4 },
    { FISHING, { 154, 155 }, "t4x4hurahero", kBwFishing, 16 },
    { SAVE, { 138, 139 }, "t4x4reporthero", kBwSave, 2 },
};
#define BW_OW_STATES ((int)(sizeof kBwOw / sizeof kBwOw[0]))

#define PT_TRFGRA "poketool/trgra/trfgra.narc"
#define PT_TRBGRA "poketool/trgra/trbgra.narc"
#define PT_MMODEL_ARC "data/mmodel/mmodel.narc"
#define PT_MSG    "msgdata/pl_msg.narc"
#define PT_CLASS_BANK 619
#define PT_CLASS_COUNT 105
#define PT_BACK_COUNT 11
#define PT_MMODEL_COUNT 470
#define CLASS_FILES 5
#define FRONT_TILES 100
#define SCAN_W 20
#define SCAN_H 10
#define CARD_W 80
#define CARD_H 88
#define NO_BALL 0x7FFF
#define BALL_FRAMES 6
#define COOK_FNV_OFFSET 0xCBF29CE484222325ULL
#define LOOKCOMPOSE_STAMP "v1"

/* ------------------------------------------------------------ resources */

static blob nds_file(const blob rom, const char *want, const char *label)
{
    return mmo_nitro_file(rom, want, label, NULL);
}

static const char *cart_code(blob rom)
{
    static char code[5];

    if (rom.len < 0x10)
        die("that file is too small to be a cartridge image");
    memcpy(code, rom.p + 0x0C, 4);
    code[4] = '\0';
    return code;
}

/* Offset of a Nitro resource section's name, or 0. */
static u32 section(blob b, const char *magic)
{
    u32 off = 16;

    while (off + 8 <= b.len) {
        u32 size = rd32(b.p + off + 4);

        if (memcmp(b.p + off, magic, 4) == 0)
            return off;
        if (size < 8)
            break;
        off += size;
    }
    return 0;
}

static void ncgr_data(blob b, u32 *off, u32 *size)
{
    u32 o = section(b, "RAHC"), doff;

    if (o == 0 || o + 32 > b.len)
        die("a character member with no RAHC");
    *size = rd32(b.p + o + 24);
    doff = rd32(b.p + o + 28);
    *off = o + 8 + doff;
    if (*off + *size > b.len)
        die("a character member shorter than its own data");
}

static u32 ncgr_depth(blob b)
{
    u32 o = section(b, "RAHC");

    return o ? rd32(b.p + o + 12) : 0;
}

static void nclr_data(blob b, u32 *off, u32 *size)
{
    u32 o = section(b, "TTLP"), doff;

    if (o == 0 || o + 24 > b.len)
        die("a palette member with no TTLP");
    *size = rd32(b.p + o + 16);
    doff = rd32(b.p + o + 20);
    *off = o + 8 + doff;
    if (*off + *size > b.len)
        die("a palette member shorter than its own colours");
}

static void nclr_colours(blob b, u16 *out, int n)
{
    u32 off, size;
    int i;

    nclr_data(b, &off, &size);
    for (i = 0; i < n; i++)
        out[i] = (u32)(i * 2 + 2) <= size ? rd16(b.p + off + i * 2) : 0;
}

/* One OAM of a Gen 4 cell bank: x, y signed from the picture's centre, tiles
 * wide and tall, and its first tile through the mapping's multiplier. */
typedef struct { int x, y, tw, th, first; } oam_box;
typedef struct { oam_box o[16]; int n; } cell_boxes;

static int ncer_cells(blob b, cell_boxes *cells, int cap)
{
    static const u8 sizes[3][4][2] = {
        { { 1, 1 }, { 2, 2 }, { 4, 4 }, { 8, 8 } },
        { { 2, 1 }, { 4, 1 }, { 4, 2 }, { 8, 4 } },
        { { 1, 2 }, { 1, 4 }, { 2, 4 }, { 4, 8 } },
    };
    u32 o = section(b, "KBEC"), ncells, cattr, celldata, mapmode, csize, cellbase;
    u32 c;

    if (o == 0 || o + 32 > b.len)
        die("a cell bank with no KBEC");
    ncells = rd16(b.p + o + 8);
    cattr = rd16(b.p + o + 10);
    celldata = rd32(b.p + o + 12);
    mapmode = rd32(b.p + o + 16);
    csize = (cattr & 1) ? 16 : 8;
    cellbase = o + 8 + celldata;
    if ((int)ncells > cap)
        die("a cell bank of %u cells, more than %d", ncells, cap);
    for (c = 0; c < ncells; c++) {
        u32 n = rd16(b.p + cellbase + c * csize);
        u32 oamoff = rd32(b.p + cellbase + c * csize + 4);
        u32 ob = cellbase + ncells * csize + oamoff, i;

        cells[c].n = 0;
        for (i = 0; i < n; i++) {
            u16 a0 = rd16(b.p + ob + i * 6), a1 = rd16(b.p + ob + i * 6 + 2), a2 = rd16(b.p + ob + i * 6 + 4);
            int shape = a0 >> 14, size = a1 >> 14, y = a0 & 0xFF, x = a1 & 0x1FF;
            oam_box *box;

            if (shape == 3)
                continue;
            if (y >= 128)
                y -= 256;
            if (x >= 256)
                x -= 512;
            if (cells[c].n >= 16)
                die("a cell of more than sixteen OAMs");
            box = &cells[c].o[cells[c].n++];
            box->x = x;
            box->y = y;
            box->tw = sizes[shape][size][0];
            box->th = sizes[shape][size][1];
            box->first = (a2 & 0x3FF) << mapmode;
        }
    }
    return (int)ncells;
}

/* An 80x80 (or 80x88) canvas of palette indices. */
typedef struct { u8 px[CARD_H][CARD_W]; } canvas;

static u8 canvas_at(const canvas *c, int x, int y, int w, int h)
{
    if (x < 0 || y < 0 || x >= w || y >= h)
        return 0;
    return c->px[y][x];
}

/* portlooks.tiles_from_canvas: `count` tiles laid out one OAM at a time. */
static void tiles_from_canvas(const canvas *c, const cell_boxes *cell, int count, u8 *out)
{
    int i;

    memset(out, 0, (size_t)count * 32);
    for (i = 0; i < cell->n; i++) {
        const oam_box *b = &cell->o[i];
        int ty, tx, r, k;

        for (ty = 0; ty < b->th; ty++) {
            for (tx = 0; tx < b->tw; tx++) {
                int t = b->first + ty * b->tw + tx;

                if (t >= count)
                    continue;
                for (r = 0; r < 8; r++) {
                    for (k = 0; k < 8; k += 2) {
                        int px = b->x + 40 + tx * 8 + k, py = b->y + 40 + ty * 8 + r;
                        u8 lo = canvas_at(c, px, py, 80, 80), hi = canvas_at(c, px + 1, py, 80, 80);

                        out[t * 32 + r * 4 + k / 2] = (u8)((lo & 0xF) | ((hi & 0xF) << 4));
                    }
                }
            }
        }
    }
}

/* portlooks.raster_tiles. */
static void raster_tiles(const canvas *c, int cw, int ch, int tw, int th, u8 *out)
{
    int ty, tx, r, k;

    memset(out, 0, (size_t)tw * th * 32);
    for (ty = 0; ty < th; ty++) {
        for (tx = 0; tx < tw; tx++) {
            int t = ty * tw + tx;

            for (r = 0; r < 8; r++) {
                for (k = 0; k < 8; k += 2) {
                    int px = tx * 8 + k, py = ty * 8 + r;
                    u8 lo = canvas_at(c, px, py, cw, ch), hi = canvas_at(c, px + 1, py, cw, ch);

                    out[t * 32 + r * 4 + k / 2] = (u8)((lo & 0xF) | ((hi & 0xF) << 4));
                }
            }
        }
    }
}

static blob with_data(blob template, u32 off, const u8 *data, u32 n)
{
    blob out = mmo_nitro_dup(template.p, template.len);

    if (off + n > out.len)
        die("data past the end of a template member");
    memcpy(out.p + off, data, n);
    return out;
}

/* ---------------------------------------------------------- the scramble */
/* PokemonSprite_DecryptPt undone in reverse: the first word is the seed and
 * the plaintext's first word must be 0 for that to hold (a sheet's corner
 * is background). rescramble.encode mode 2, with its seed. */
#define LCRNG_A 1103515245u
#define LCRNG_C 24691u
#define SCRAMBLE_SEED 0x1234u

static void scramble_pt(u8 *data, u32 n)
{
    u32 words = n / 2, i, s;

    if (words == 0)
        return;
    if (rd16(data) != 0)
        die("a sheet whose first word is not background cannot be scrambled");
    s = SCRAMBLE_SEED;
    wr16(data, (u16)s);
    for (i = 1; i < words; i++) {
        s = s * LCRNG_A + LCRNG_C;
        wr16(data + i * 2, (u16)(rd16(data + i * 2) ^ (s & 0xFFFF)));
    }
}

/* rescramble.decode mode 1: HeartGold's dp-way scan, seeded from the last word. */
static void unscramble_dp(u8 *data, u32 n)
{
    u32 words = n / 2, s;
    u32 i;

    if (words == 0)
        return;
    s = rd16(data + (words - 1) * 2);
    for (i = words; i-- > 0;) {
        wr16(data + i * 2, (u16)(rd16(data + i * 2) ^ (s & 0xFFFF)));
        s = s * LCRNG_A + LCRNG_C;
    }
}

static blob rescan(blob hg)
{
    u32 off, size;
    blob out = mmo_nitro_dup(hg.p, hg.len);

    ncgr_data(out, &off, &size);
    unscramble_dp(out.p + off, size);
    scramble_pt(out.p + off, size);
    return out;
}

static blob scan_of(blob template, const canvas *c)
{
    u32 off, size;
    u8 *plain = xmalloc(SCAN_W * SCAN_H * 32);
    blob out;

    ncgr_data(template, &off, &size);
    if (size != SCAN_W * SCAN_H * 32)
        die("your Platinum's scan sheet is not %dx%d tiles", SCAN_W, SCAN_H);
    raster_tiles(c, 80, 80, SCAN_W, SCAN_H, plain);
    scramble_pt(plain, size);
    out = with_data(template, off, plain, size);
    free(plain);
    return out;
}

static blob with_colours(blob template, const u16 *colours)
{
    u32 off, size;
    u8 data[32];
    int i;

    nclr_data(template, &off, &size);
    if (size < 32)
        die("a palette member too small for sixteen colours");
    for (i = 0; i < 16; i++)
        wr16(data + i * 2, colours[i]);
    return with_data(template, off, data, 32);
}

/* -------------------------------------------------------------- NSBTX */
typedef struct { u32 off, len; } span;
typedef struct {
    span tex[64];
    int ntex;
    span pal;
    char palname[17];
} nsbtx_layout;

static void nsbtx_dict(blob b, u32 base, int *num, u32 *at, u32 *unit, u32 *names)
{
    u32 data;

    if (base + 8 > b.len)
        die("a texture set with a dictionary past its end");
    *num = b.p[base + 1];
    data = base + rd16(b.p + base + 0x06);
    if (data + 4 > b.len)
        die("a texture set with a dictionary past its end");
    *unit = rd16(b.p + data);
    *names = data + rd16(b.p + data + 2);
    *at = data + 4;
}

/* portlooks.nsbtx_layout: the 32x32 4bpp textures in dictionary order and
 * the first palette. */
static void nsbtx_read(blob b, nsbtx_layout *out)
{
    u32 t = 0, n, i;
    u32 tex_dict, tex_data, pal_size, pal_dict, pal_data, at, unit, names;
    int num, pnum;
    u32 starts[65], nstarts = 0, poff, best;

    if (b.len >= 0x10 && memcmp(b.p, "BTX0", 4) == 0) {
        n = rd16(b.p + 0x0E);
        for (i = 0; i < n; i++) {
            u32 off = rd32(b.p + 0x10 + 4 * i);

            if (off + 4 <= b.len && memcmp(b.p + off, "TEX0", 4) == 0) {
                t = off;
                break;
            }
        }
    }
    if (t == 0)
        die("a member that is not a texture set");
    tex_dict = t + rd16(b.p + t + 0x0E);
    tex_data = t + rd32(b.p + t + 0x14);
    pal_size = (u32)rd16(b.p + t + 0x30) << 3;
    pal_dict = t + rd32(b.p + t + 0x34);
    pal_data = t + rd32(b.p + t + 0x38);
    nsbtx_dict(b, tex_dict, &num, &at, &unit, &names);
    if (num > 64)
        die("a texture set of %d textures, more than a sheet", num);
    out->ntex = num;
    for (i = 0; (int)i < num; i++) {
        u32 param = rd32(b.p + at + i * unit);
        u32 off = (param & 0xFFFF) << 3;
        int w = 8 << ((param >> 20) & 7), h = 8 << ((param >> 23) & 7), fmt = (param >> 26) & 7;

        if (fmt != 3 || w != 32 || h != 32)
            die("a texture of %dx%d format %d; a player sheet is 32x32 4bpp", w, h, fmt);
        out->tex[i].off = tex_data + off;
        out->tex[i].len = 512;
        if (out->tex[i].off + 512 > b.len)
            die("a texture past the end of its set");
    }
    nsbtx_dict(b, pal_dict, &pnum, &at, &unit, &names);
    if (pnum < 1)
        die("a texture set with no palette");
    poff = (u32)rd16(b.p + at) << 3;
    for (i = 0; (int)i < pnum && nstarts < 64; i++) {
        u32 s = (u32)rd16(b.p + at + i * unit) << 3, k;
        int seen = 0;

        for (k = 0; k < nstarts; k++)
            if (starts[k] == s)
                seen = 1;
        if (!seen)
            starts[nstarts++] = s;
    }
    starts[nstarts++] = pal_size;
    best = pal_size;
    for (i = 0; i < nstarts; i++)
        if (starts[i] > poff && starts[i] < best)
            best = starts[i];
    out->pal.off = pal_data + poff;
    out->pal.len = best - poff;
    memset(out->palname, 0, sizeof out->palname);
    memcpy(out->palname, b.p + names, 16);
}

/* ---------------------------------------------------------- the message bank */
#define MSG_TABLE_KEY   0x2FD
#define MSG_STRING_KEY  0x91BD3
#define MSG_STRING_STEP 0x493D

typedef struct { u16 *c; u32 n; } msg;
typedef struct { msg *e; u32 n; u16 seed; } bank;

static bank msg_read(blob b, const char *what)
{
    bank out;
    u32 i;

    if (b.len < 4)
        die("%s is too small to be a message file", what);
    out.n = rd16(b.p);
    out.seed = rd16(b.p + 2);
    if (out.n == 0 || (u64)4 + (u64)out.n * 8 > b.len)
        die("%s says it holds %u entries and is %u bytes", what, out.n, b.len);
    out.e = xmalloc((size_t)out.n * sizeof *out.e);
    for (i = 0; i < out.n; i++) {
        u16 key = (u16)(MSG_TABLE_KEY * out.seed);
        u16 pair = (u16)(key * (i + 1));
        u32 mask = (u32)pair | ((u32)pair << 16);
        u32 off = rd32(b.p + 4 + i * 8) ^ mask;
        u32 len = rd32(b.p + 4 + i * 8 + 4) ^ mask;
        u16 ck = (u16)(MSG_STRING_KEY * (i + 1));
        u32 j;

        if (len == 0 || (u64)off + (u64)len * 2 > b.len)
            die("message %u of %s runs past its end", i, what);
        out.e[i].n = len;
        out.e[i].c = xmalloc((size_t)len * sizeof *out.e[i].c);
        for (j = 0; j < len; j++) {
            out.e[i].c[j] = (u16)(rd16(b.p + off + j * 2) ^ ck);
            ck = (u16)(ck + MSG_STRING_STEP);
        }
    }
    return out;
}

static blob msg_write(const bank *bk)
{
    blob out;
    u32 total = 4 + bk->n * 8, i, at;

    for (i = 0; i < bk->n; i++)
        total += bk->e[i].n * 2;
    out.p = xmalloc(total);
    out.len = total;
    wr16(out.p, (u16)bk->n);
    wr16(out.p + 2, bk->seed);
    at = 4 + bk->n * 8;
    for (i = 0; i < bk->n; i++) {
        u16 key = (u16)(MSG_TABLE_KEY * bk->seed);
        u16 pair = (u16)(key * (i + 1));
        u32 mask = (u32)pair | ((u32)pair << 16);
        u16 ck = (u16)(MSG_STRING_KEY * (i + 1));
        u32 j;

        wr32(out.p + 4 + i * 8, at ^ mask);
        wr32(out.p + 4 + i * 8 + 4, bk->e[i].n ^ mask);
        for (j = 0; j < bk->e[i].n; j++) {
            wr16(out.p + at, (u16)(bk->e[i].c[j] ^ ck));
            ck = (u16)(ck + MSG_STRING_STEP);
            at += 2;
        }
    }
    return out;
}

/* ------------------------------------------------------------- Black */

/* speciescompose's LZ11, the extended LZ the engine port calls
 * MI_UncompressLZ8. A member that does not start 0x11 is returned as is. */
static blob lz11(blob in)
{
    blob out;
    u32 size, pos = 4, at = 0;

    if (in.len == 0 || in.p[0] != 0x11)
        return mmo_nitro_dup(in.p, in.len);
    if (in.len < 4)
        die("a compressed member too short to have a header");
    size = rd32(in.p) >> 8;
    out.p = xmalloc(size ? size : 1);
    out.len = size;
    while (at < size) {
        u8 flags;
        int i;

        if (pos >= in.len)
            die("a compressed member ends mid-run");
        flags = in.p[pos++];
        for (i = 0; i < 8 && at < size; i++) {
            u32 length, back, k;

            if (!(flags & (0x80 >> i))) {
                if (pos >= in.len)
                    die("a compressed member ends mid-literal");
                out.p[at++] = in.p[pos++];
                continue;
            }
            if (pos + 1 >= in.len)
                die("a compressed member ends mid-reference");
            {
                u8 top = (u8)(in.p[pos] >> 4);

                if (top == 1) {
                    if (pos + 3 >= in.len)
                        die("a long run runs past the member");
                    length = ((u32)(in.p[pos] & 0xF) << 12 | (u32)in.p[pos + 1] << 4
                              | (u32)(in.p[pos + 2] >> 4)) + 0x111;
                    back = ((u32)(in.p[pos + 2] & 0xF) << 8 | in.p[pos + 3]) + 1;
                    pos += 4;
                } else if (top == 0) {
                    if (pos + 2 >= in.len)
                        die("a run runs past the member");
                    length = ((u32)(in.p[pos] & 0xF) << 4 | (u32)(in.p[pos + 1] >> 4)) + 0x11;
                    back = ((u32)(in.p[pos + 1] & 0xF) << 8 | in.p[pos + 2]) + 1;
                    pos += 3;
                } else {
                    length = (u32)top + 1;
                    back = ((u32)(in.p[pos] & 0xF) << 8 | in.p[pos + 1]) + 1;
                    pos += 2;
                }
            }
            if (back > at)
                die("a compressed member reaches before its own start");
            for (k = 0; k < length && at < size; k++, at++)
                out.p[at] = out.p[at - back];
        }
    }
    return out;
}

/* portlooks.bw_front_canvas: the still front, six OAMs over the 256-wide
 * char map through 2D mapping with the char name doubled. */
static void bw_front_canvas(members arc, int entry, canvas *out, u16 *colours)
{
    int b = entry * BW_ENTRY;
    struct face f;
    blob map;
    cell_boxes cells[2];
    int ncells, i;
    u32 o;

    memset(&f, 0, sizeof f);
    map = lz11(arc.m[b + 1]);
    if (!mmo_black_read_charmap(&f, map.p, map.len))
        die("Black front %d has a char map this does not read", entry);
    free(map.p);
    o = section(arc.m[b + 2], "KBEC");
    if (o == 0 || rd32(arc.m[b + 2].p + o + 16) != 1)
        die("Black front %d maps its cells a way this does not read", entry);
    ncells = ncer_cells(arc.m[b + 2], cells, 2);
    if (ncells < 1)
        die("Black front %d has no cell", entry);
    memset(out, 0, sizeof *out);
    for (i = cells[0].n; i-- > 0;) {
        const oam_box *bx = &cells[0].o[i];
        int ty, tx, r, c;

        for (ty = 0; ty < bx->th; ty++) {
            for (tx = 0; tx < bx->tw; tx++) {
                u32 row = (u32)(bx->first / 32) + (u32)ty, col = (u32)(bx->first % 32) + (u32)tx;

                if (row >= f.rows || col >= f.cols)
                    continue;
                for (r = 0; r < 8; r++) {
                    for (c = 0; c < 8; c++) {
                        u32 idx = (row * 8 + (u32)r) * (f.cols * 8) + col * 8 + (u32)c;
                        u8 v = (u8)((f.map[idx / 2] >> (4 * (idx & 1))) & 0xF);
                        int px = bx->x + 40 + tx * 8 + c, py = bx->y + 40 + ty * 8 + r;

                        if (v && px >= 0 && px < 80 && py >= 0 && py < 80)
                            out->px[py][px] = v;
                    }
                }
            }
        }
    }
    free(f.map);
    nclr_colours(arc.m[b + 7], colours, 16);
}

/* portlooks.bw_back_block: the readers of blackcompose.c over the entry's
 * eight members. */
static void bw_back_block(members arc, int entry, struct face *f, u16 *colours)
{
    int b = entry * BW_ENTRY;
    blob map = lz11(arc.m[b + 1]);
    blob anim = lz11(arc.m[b + 3]);

    memset(f, 0, sizeof *f);
    if (!mmo_black_read_charmap(f, map.p, map.len))
        die("Black back %d has a char map this does not read", entry);
    if (!mmo_black_read_cells(f, arc.m[b + 2].p, arc.m[b + 2].len))
        die("Black back %d has a cell bank this does not read", entry);
    if (!mmo_black_read_anims(anim.p, anim.len, &f->cseqs, &f->ncseqs, &f->cframes, &f->ncframes))
        die("Black back %d has an animation bank this does not read", entry);
    if (!mmo_black_read_multicells(f, arc.m[b + 4].p, arc.m[b + 4].len))
        die("Black back %d has a multi-cell bank this does not read", entry);
    if (!mmo_black_read_anims(arc.m[b + 5].p, arc.m[b + 5].len, &f->mseqs, &f->nmseqs, &f->mframes, &f->nmframes))
        die("Black back %d has a multi-cell animation this does not read", entry);
    if (f->nmseqs < 2)
        die("Black's back has no throw sequence");
    free(map.p);
    free(anim.p);
    nclr_colours(arc.m[b + 7], colours, 16);
}

/* portlooks.bw_back_frame: the throw at one tick, at three quarters, the
 * tallest keyframe's top on row 0, the frame's centre column the source's
 * x = 0. compose() draws the first multi-cell sequence, so the throw is the
 * face with its sequence table advanced by one. */
static void bw_back_frame(const struct face *f, u32 tick, canvas *out)
{
    struct face throw = *f;
    int px, py;

    throw.mseqs = f->mseqs + 1;
    throw.nmseqs = 1;
    mmo_black_compose(&throw, tick);
    memset(out, 0, sizeof *out);
    for (py = 0; py < 80; py++) {
        int sy = BW_BACK_TOP + (int)(py / BW_BACK_SCALE);

        for (px = 0; px < 80; px++) {
            int sx = (int)((px - 40) / BW_BACK_SCALE);
            int cx = sx + ORIGIN, cy = sy + ORIGIN;
            u8 v;

            if (cx < 0 || cy < 0 || cx >= CANVAS || cy >= CANVAS)
                continue;
            v = mmo_black_canvas[cy * CANVAS + cx];
            if (v)
                out->px[py][px] = v;
        }
    }
}

static void canvas_release_point(const canvas *c, int *rx, int *ry)
{
    int px, py, bx = -1, by = 0;

    for (py = 0; py < 40; py++)
        for (px = 0; px < 80; px++)
            if (c->px[py][px] && px > bx) {
                bx = px;
                by = py;
            }
    if (bx < 0) {
        *rx = *ry = 0;
        return;
    }
    *rx = bx - 40;
    *ry = by - 40;
}

/* ------------------------------------------------------------ the card */

static blob card_of_canvas(const canvas *c, const u16 *colours)
{
    blob out;
    int py, px, i;

    out.len = 32 + (CARD_W / 8) * (CARD_H / 8) * 64;
    out.p = xmalloc(out.len);
    memset(out.p, 0, out.len);
    for (i = 0; i < 16; i++)
        wr16(out.p + i * 2, colours[i]);
    for (py = 0; py < CARD_H; py++) {
        for (px = 0; px < CARD_W; px++) {
            u8 v = c->px[py][px];

            if (v) {
                int t = (py / 8) * (CARD_W / 8) + px / 8;

                out.p[32 + t * 64 + (py % 8) * 8 + (px % 8)] = (u8)(v & 0xF);
            }
        }
    }
    return out;
}

/* The face in the class's own front palette: the card loads that into the
 * face's block at runtime, and the card archive's own palette members draw
 * the picture in a stranger's clothes. */
static blob hg_card_face(members arc, members front, int gender)
{
    blob tiles = arc.m[HG_CARD_TILES], scr = arc.m[kHgCardScreen[gender]];
    u32 off, size, o, sw, ssize, pitch;
    canvas c;
    int block = -1, ty, tx, r, k;
    u16 colours[16];

    if (ncgr_depth(tiles) != 4)
        die("HeartGold's card tiles are not 8bpp");
    ncgr_data(tiles, &off, &size);
    o = section(scr, "NRCS");
    if (o == 0 || o + 20 > scr.len)
        die("HeartGold's card screen has no NRCS");
    sw = rd16(scr.p + o + 8);
    ssize = rd32(scr.p + o + 16);
    pitch = sw / 8;
    memset(&c, 0, sizeof c);
    for (ty = 0; ty < HG_CARD_TH; ty++) {
        for (tx = 0; tx < HG_CARD_TW; tx++) {
            u32 ei = ((u32)(HG_CARD_TY + ty) * pitch + (u32)(HG_CARD_TX + tx)) * 2;
            u16 e;
            u32 t;
            int hf, vf;

            if (ei + 2 > ssize)
                continue;
            e = rd16(scr.p + o + 20 + ei);
            t = e & 0x3FF;
            hf = (e >> 10) & 1;
            vf = (e >> 11) & 1;
            if ((t + 1) * 64 > size)
                continue;
            for (r = 0; r < 8; r++) {
                for (k = 0; k < 8; k++) {
                    u8 v = tiles.p[off + t * 64 + (vf ? 7 - r : r) * 8 + (hf ? 7 - k : k)];

                    if (!v)
                        continue;
                    if (block < 0)
                        block = v / 16;
                    else if (block != v / 16)
                        die("HeartGold's card face spans two palette blocks");
                    c.px[ty * 8 + r][(HG_CARD_INSET + tx) * 8 + k] = (u8)(v % 16);
                }
            }
        }
    }
    if (block < 0)
        die("HeartGold's card face is empty");
    nclr_colours(front.m[gender * CLASS_FILES + 1], colours, 16);
    return card_of_canvas(&c, colours);
}

/* ------------------------------------------------------------- output */

typedef struct {
    char narc[64];
    u32 index;
    blob data;
} out_member;

typedef struct {
    out_member *m;
    u32 n, cap;
    char text_gfx[4096];
    char text_classes[256];
    char text_looks[1024];
    blob card[LOOKS];
} package;

static void pkg_member(package *p, const char *narc, u32 index, blob data)
{
    if (p->n == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 64;
        p->m = mmo_nitro_grow(p->m, p->cap * sizeof *p->m);
    }
    snprintf(p->m[p->n].narc, sizeof p->m[p->n].narc, "%s", narc);
    p->m[p->n].index = index;
    p->m[p->n].data = data;
    p->n++;
}

static void path_join(char *out, size_t cap, const char *a, const char *b)
{
    snprintf(out, cap, "%s%s%s", a, mmo_plat_sep(), b);
}

static void mkdir_p(const char *path)
{
    char tmp[1024];
    size_t i;
    char sep = mmo_plat_sep()[0];

    snprintf(tmp, sizeof tmp, "%s", path);
    for (i = 1; tmp[i]; i++) {
        if (tmp[i] == sep || tmp[i] == '/') {
            char keep = tmp[i];

            tmp[i] = '\0';
            mmo_plat_mkdir(tmp);
            tmp[i] = keep;
        }
    }
    mmo_plat_mkdir(tmp);
}

static void mkdir_narc(char *out, size_t cap, const char *root, const char *rel)
{
    size_t i;
    char sep = mmo_plat_sep()[0];

    snprintf(out, cap, "%s%s%s", root, mmo_plat_sep(), rel);
    for (i = strlen(root); out[i]; i++) {
        if (out[i] == '/')
            out[i] = sep;
    }
    mkdir_p(out);
}

static void rm_dir_r(const char *path)
{
    DIR *d = opendir(path);
    struct dirent *e;

    if (d == NULL)
        return;
    while ((e = readdir(d)) != NULL) {
        char sub[1024];
        struct stat st;

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        path_join(sub, sizeof sub, path, e->d_name);
        if (stat(sub, &st) == 0 && S_ISDIR(st.st_mode))
            rm_dir_r(sub);
        else
            remove(sub);
    }
    closedir(d);
    rmdir(path);
}

/* Only ever a .cooked tree of ours. */
static void rm_tree(const char *path)
{
    const char *base = strrchr(path, mmo_plat_sep()[0]);

    base = base != NULL ? base + 1 : path;
    if (strncmp(base, ".cooked", 7) != 0)
        return;
    rm_dir_r(path);
}

static void write_bytes(const char *dir, const char *name, const u8 *p, u32 n)
{
    char path[1024];
    FILE *f;

    path_join(path, sizeof path, dir, name);
    f = fopen(path, "wb");
    if (f == NULL)
        die("cannot write %s", path);
    if (n && fwrite(p, 1, n, f) != n) {
        fclose(f);
        die("cannot write %s", path);
    }
    if (fclose(f) != 0)
        die("cannot write %s", path);
}

static void write_text(const char *dir, const char *name, const char *s)
{
    write_bytes(dir, name, (const u8 *)s, (u32)strlen(s));
}

static void text_add(char *buf, size_t cap, const char *line)
{
    size_t at = strlen(buf);

    if (at + strlen(line) + 1 > cap)
        die("a generated table grew past its buffer");
    memcpy(buf + at, line, strlen(line) + 1);
}

/* portlooks.others_bank: the last other package's claim on bank 619. */
static blob others_bank(const char *others, blob base)
{
    const char *colon, *at;

    if (others == NULL || (colon = strchr(others, ':')) == NULL)
        return base;
    at = colon + 1;
    while (*at != '\0') {
        const char *end = strchr(at, ',');
        size_t len = end != NULL ? (size_t)(end - at) : strlen(at);
        char path[1024];
        FILE *f;

        /* never its own last fill: that bank already holds the four */
        if (len > 0 && !(len == 5 && strncmp(at, "looks", 5) == 0)) {
            snprintf(path, sizeof path, "%.*s%s%.*s%s.cooked%snarc%s" PT_MSG "%s%d",
                     (int)(colon - others), others, mmo_plat_sep(), (int)len, at,
                     mmo_plat_sep(), mmo_plat_sep(), mmo_plat_sep(), mmo_plat_sep(),
                     PT_CLASS_BANK);
            {
                size_t i;
                char sep = mmo_plat_sep()[0];

                for (i = 0; path[i]; i++)
                    if (path[i] == '/')
                        path[i] = sep;
            }
            f = fopen(path, "rb");
            if (f != NULL) {
                fclose(f);
                base = read_file(path);
            }
        }
        if (end == NULL)
            break;
        at = end + 1;
    }
    return base;
}

/* ------------------------------------------------------------- the fill */

int mmo_lookcompose(const char *hg_path, const char *bw_path, const char *pt_path,
                    const char *pkg, int mmodel_first, int class_first,
                    int back_first, const char *others,
                    char *err, size_t errcap)
{
    blob hg, bw, pt;
    blob hg_mmodel_arc, hg_front_arc, hg_back_arc, hg_card_arc;
    blob bw_ow_arc, bw_front_arc, bw_back_arc;
    blob pt_mmodel_arc, pt_front_arc, pt_back_arc, pt_msg_arc;
    members hg_mmodel, hg_front, hg_back, hg_card, bw_ow, bw_front, bw_back;
    members pt_mmodel, pt_front, pt_back, pt_msg;
    package p;
    char hg_code[5], bw_code[5], pt_code[5];
    char cooked[1024], gen[1024], narc_mmodel[1024], narc_front[1024], narc_back[1024], narc_msg[1024];
    char line[256];
    u32 at;
    int look;
    jmp_buf jb;

    if (err != NULL && errcap > 0)
        err[0] = '\0';
    memset(&p, 0, sizeof p);
    /* Floored before the jump is armed: a value written after setjmp and
     * read after a longjmp is the compiler's to lose. */
    if (mmodel_first < PT_MMODEL_COUNT)
        mmodel_first = PT_MMODEL_COUNT;
    if (class_first < PT_CLASS_COUNT)
        class_first = PT_CLASS_COUNT;
    if (back_first < PT_BACK_COUNT)
        back_first = PT_BACK_COUNT;
    if (setjmp(jb) != 0)
        return -1;
    mmo_nitro_catch(&jb, err, errcap);

    hg = read_file(hg_path);
    bw = read_file(bw_path);
    pt = read_file(pt_path);
    snprintf(hg_code, sizeof hg_code, "%s", cart_code(hg));
    snprintf(bw_code, sizeof bw_code, "%s", cart_code(bw));
    snprintf(pt_code, sizeof pt_code, "%s", cart_code(pt));
    if (strncmp(hg_code, "IPK", 3) != 0 && strncmp(hg_code, "IPG", 3) != 0)
        die("%s is not a Heart Gold or Soul Silver cartridge", hg_code);
    if (strncmp(bw_code, "IRB", 3) != 0 && strncmp(bw_code, "IRA", 3) != 0)
        die("%s is not a Black or White cartridge", bw_code);
    if (strncmp(pt_code, "CPU", 3) != 0)
        die("%s is not a Platinum cartridge", pt_code);

    /* Everything is READ and CHECKED before a byte is written. */
    hg_mmodel_arc = nds_file(hg, HG_MMODEL_ARC, "the Heart Gold cartridge");
    hg_front_arc = nds_file(hg, HG_TRFGRA, "the Heart Gold cartridge");
    hg_back_arc = nds_file(hg, HG_TRBGRA, "the Heart Gold cartridge");
    hg_card_arc = nds_file(hg, HG_CARD, "the Heart Gold cartridge");
    bw_ow_arc = nds_file(bw, BW_OW_ARC, "the Black cartridge");
    bw_front_arc = nds_file(bw, BW_TRFGRA, "the Black cartridge");
    bw_back_arc = nds_file(bw, BW_TRBGRA, "the Black cartridge");
    pt_mmodel_arc = nds_file(pt, PT_MMODEL_ARC, "your Platinum");
    pt_front_arc = nds_file(pt, PT_TRFGRA, "your Platinum");
    pt_back_arc = nds_file(pt, PT_TRBGRA, "your Platinum");
    pt_msg_arc = nds_file(pt, PT_MSG, "your Platinum");
    hg_mmodel = narc_members(hg_mmodel_arc, HG_MMODEL_ARC);
    hg_front = narc_members(hg_front_arc, HG_TRFGRA);
    hg_back = narc_members(hg_back_arc, HG_TRBGRA);
    hg_card = narc_members(hg_card_arc, HG_CARD);
    bw_ow = narc_members(bw_ow_arc, BW_OW_ARC);
    bw_front = narc_members(bw_front_arc, BW_TRFGRA);
    bw_back = narc_members(bw_back_arc, BW_TRBGRA);
    pt_mmodel = narc_members(pt_mmodel_arc, PT_MMODEL_ARC);
    pt_front = narc_members(pt_front_arc, PT_TRFGRA);
    pt_back = narc_members(pt_back_arc, PT_TRBGRA);
    pt_msg = narc_members(pt_msg_arc, PT_MSG);
    if (bw_front.n != BW_MAX_ENTRIES * BW_ENTRY)
        die("Black's trainer archive holds %u members, not %d entries of %d",
            bw_front.n, BW_MAX_ENTRIES, BW_ENTRY);
    if (pt_front.n != PT_CLASS_COUNT * CLASS_FILES || pt_back.n != PT_BACK_COUNT * CLASS_FILES)
        die("your Platinum's trainer archives are not the sizes this engine builds");
    if (hg_mmodel.n < 100 || hg_front.n < 10 || hg_back.n < 10 || hg_card.n <= (u32)kHgCardScreen[1])
        die("that Heart Gold cartridge is missing archives this reads");
    if (bw_ow.n < 156 || bw_back.n < 2 * BW_ENTRY)
        die("that Black cartridge is missing archives this reads");
    if (pt_mmodel.n != PT_MMODEL_COUNT || pt_msg.n <= PT_CLASS_BANK)
        die("your Platinum's archives are not the sizes this engine builds");

    /* --- overworld sheets */
    at = (u32)mmodel_first;
    for (look = 0; look < LOOKS; look++) {
        int gender = kLookGender[look];

        if (!kLookIsBlack[look]) {
            int state;

            for (state = 0; state < STATES; state++) {
                int member = kHgMmodel[state].member[gender];
                nsbtx_layout lay;

                if ((u32)member >= hg_mmodel.n)
                    die("HeartGold member %d is past its archive", member);
                nsbtx_read(hg_mmodel.m[member], &lay);
                if (strcmp(lay.palname, kHgMmodel[state].name[gender]) != 0)
                    die("HeartGold member %d is '%s', not '%s': not the sheet this expects",
                        member, lay.palname, kHgMmodel[state].name[gender]);
                pkg_member(&p, PT_MMODEL_ARC, at, mmo_nitro_dup(hg_mmodel.m[member].p, hg_mmodel.m[member].len));
                snprintf(line, sizeof line, "%d %u -1 -1 %d\n",
                         MMO_APPEAR_LOOK_GFX_BASE + look * MMO_APPEAR_LOOK_STRIDE + state, at,
                         kLike[state][gender]);
                text_add(p.text_gfx, sizeof p.text_gfx, line);
                at++;
            }
        } else {
            int i;

            for (i = 0; i < BW_OW_STATES; i++) {
                int state = kBwOw[i].state, member = kBwOw[i].member[gender], k;
                blob src, template, out;
                nsbtx_layout slay, tlay;
                int rows[64];
                u32 take;

                if ((u32)member >= bw_ow.n)
                    die("Black member %d is past its archive", member);
                src = bw_ow.m[member];
                nsbtx_read(src, &slay);
                if (strcmp(slay.palname, kBwOw[i].name) != 0)
                    die("Black member %d is '%s', not '%s'", member, slay.palname, kBwOw[i].name);
                template = pt_mmodel.m[kPtMmodel[state][gender]];
                nsbtx_read(template, &tlay);
                if (tlay.ntex != kBwOw[i].count)
                    die("Platinum's sheet %d has %d frames; the layout names %d",
                        kPtMmodel[state][gender], tlay.ntex, kBwOw[i].count);
                for (k = 0; k < kBwOw[i].count; k++)
                    if (kBwOw[i].order[k] >= slay.ntex)
                        die("Black's %s sheet has %d frames; %d asked", kBwOw[i].name, slay.ntex, kBwOw[i].order[k]);
                /* the template's k-th texture is the one named ".n" that
                 * sorts k-th: sheet row n-1. Numbers sort as text. */
                {
                    int n = kBwOw[i].count, a;
                    char names[64][8];
                    int order[64];

                    for (a = 0; a < n; a++) {
                        snprintf(names[a], sizeof names[a], "%d", a + 1);
                        order[a] = a;
                    }
                    /* insertion sort by name, stable */
                    for (a = 1; a < n; a++) {
                        int v = order[a], j = a - 1;

                        while (j >= 0 && strcmp(names[order[j]], names[v]) > 0) {
                            order[j + 1] = order[j];
                            j--;
                        }
                        order[j + 1] = v;
                    }
                    for (a = 0; a < n; a++)
                        rows[a] = order[a];
                }
                out = mmo_nitro_dup(template.p, template.len);
                for (k = 0; k < tlay.ntex; k++) {
                    const span *s = &slay.tex[kBwOw[i].order[rows[k]]];

                    memcpy(out.p + tlay.tex[k].off, src.p + s->off, tlay.tex[k].len);
                }
                take = tlay.pal.len < slay.pal.len ? tlay.pal.len : slay.pal.len;
                if (take > 32)
                    take = 32;
                memcpy(out.p + tlay.pal.off, src.p + slay.pal.off, take);
                pkg_member(&p, PT_MMODEL_ARC, at, out);
                snprintf(line, sizeof line, "%d %u -1 -1 %d\n",
                         MMO_APPEAR_LOOK_GFX_BASE + look * MMO_APPEAR_LOOK_STRIDE + state, at,
                         kLike[state][gender]);
                text_add(p.text_gfx, sizeof p.text_gfx, line);
                at++;
            }
        }
    }

    /* --- battle fronts and backs, the card faces, the ball rows */
    for (look = 0; look < LOOKS; look++) {
        int gender = kLookGender[look];
        int cls = class_first + look, back = back_first + look, k;

        if (!kLookIsBlack[look]) {
            for (k = 0; k < CLASS_FILES; k++) {
                blob f = hg_front.m[gender * CLASS_FILES + k], b = hg_back.m[gender * CLASS_FILES + k];

                pkg_member(&p, PT_TRFGRA, (u32)(cls * CLASS_FILES + k), k == 4 ? rescan(f) : mmo_nitro_dup(f.p, f.len));
                pkg_member(&p, PT_TRBGRA, (u32)(back * CLASS_FILES + k), k == 4 ? rescan(b) : mmo_nitro_dup(b.p, b.len));
            }
            p.card[look] = hg_card_face(hg_card, hg_front, gender);
            snprintf(line, sizeof line, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
                     look, cls, back, gender, NO_BALL, NO_BALL, NO_BALL, NO_BALL, NO_BALL, NO_BALL,
                     NO_BALL, NO_BALL, NO_BALL, NO_BALL, NO_BALL, NO_BALL);
            text_add(p.text_looks, sizeof p.text_looks, line);
        } else {
            canvas front, frames[BW_BACK_FRAMES];
            u16 colours[16], bcolours[16];
            cell_boxes cells[8];
            int ncells, tmpl = gender * CLASS_FILES, btmpl = gender * CLASS_FILES;
            u32 off, size;
            u8 *tiles;
            struct face f;
            int rx, ry;

            bw_front_canvas(bw_front, gender, &front, colours);
            ncells = ncer_cells(pt_front.m[tmpl + 2], cells, 8);
            if (ncells < 1)
                die("Platinum's front has no cell");
            ncgr_data(pt_front.m[tmpl], &off, &size);
            if (size != FRONT_TILES * 32)
                die("Platinum's front is not %d tiles", FRONT_TILES);
            tiles = xmalloc(FRONT_TILES * 32);
            tiles_from_canvas(&front, &cells[0], FRONT_TILES, tiles);
            pkg_member(&p, PT_TRFGRA, (u32)(cls * CLASS_FILES + 0), with_data(pt_front.m[tmpl], off, tiles, FRONT_TILES * 32));
            free(tiles);
            pkg_member(&p, PT_TRFGRA, (u32)(cls * CLASS_FILES + 1), with_colours(pt_front.m[tmpl + 1], colours));
            pkg_member(&p, PT_TRFGRA, (u32)(cls * CLASS_FILES + 2), mmo_nitro_dup(pt_front.m[tmpl + 2].p, pt_front.m[tmpl + 2].len));
            pkg_member(&p, PT_TRFGRA, (u32)(cls * CLASS_FILES + 3), mmo_nitro_dup(pt_front.m[tmpl + 3].p, pt_front.m[tmpl + 3].len));
            pkg_member(&p, PT_TRFGRA, (u32)(cls * CLASS_FILES + 4), scan_of(pt_front.m[tmpl + 4], &front));
            p.card[look] = card_of_canvas(&front, colours);

            bw_back_block(bw_back, gender, &f, bcolours);
            ncells = ncer_cells(pt_back.m[btmpl + 2], cells, 8);
            if (ncells != BW_BACK_FRAMES)
                die("Platinum's back has %d cells, not %d", ncells, BW_BACK_FRAMES);
            ncgr_data(pt_back.m[btmpl], &off, &size);
            if (size != BW_BACK_FRAMES * FRONT_TILES * 32)
                die("Platinum's back is not %d frames of %d tiles", BW_BACK_FRAMES, FRONT_TILES);
            tiles = xmalloc(size);
            for (k = 0; k < BW_BACK_FRAMES; k++) {
                cell_boxes laid = cells[k];
                int minx = 512, miny = 512, i;

                bw_back_frame(&f, kBwBackTicks[k], &frames[k]);
                for (i = 0; i < laid.n; i++) {
                    if (laid.o[i].x < minx)
                        minx = laid.o[i].x;
                    if (laid.o[i].y < miny)
                        miny = laid.o[i].y;
                    if (laid.o[i].first + laid.o[i].tw * laid.o[i].th > FRONT_TILES)
                        die("Platinum's back cell %d names tiles past its frame", k);
                }
                for (i = 0; i < laid.n; i++) {
                    laid.o[i].x -= minx + 40;
                    laid.o[i].y -= miny + 40;
                }
                tiles_from_canvas(&frames[k], &laid, FRONT_TILES, tiles + k * FRONT_TILES * 32);
            }
            pkg_member(&p, PT_TRBGRA, (u32)(back * CLASS_FILES + 0), with_data(pt_back.m[btmpl], off, tiles, size));
            free(tiles);
            pkg_member(&p, PT_TRBGRA, (u32)(back * CLASS_FILES + 1), with_colours(pt_back.m[btmpl + 1], bcolours));
            pkg_member(&p, PT_TRBGRA, (u32)(back * CLASS_FILES + 2), mmo_nitro_dup(pt_back.m[btmpl + 2].p, pt_back.m[btmpl + 2].len));
            pkg_member(&p, PT_TRBGRA, (u32)(back * CLASS_FILES + 3), mmo_nitro_dup(pt_back.m[btmpl + 3].p, pt_back.m[btmpl + 3].len));
            pkg_member(&p, PT_TRBGRA, (u32)(back * CLASS_FILES + 4), scan_of(pt_back.m[btmpl + 4], &frames[0]));
            canvas_release_point(&frames[3], &rx, &ry);
            {
                int minx = 512, miny = 512, i;

                for (i = 0; i < cells[3].n; i++) {
                    if (cells[3].o[i].x < minx)
                        minx = cells[3].o[i].x;
                    if (cells[3].o[i].y < miny)
                        miny = cells[3].o[i].y;
                }
                rx += minx + 40;
                ry += miny + 40;
            }
            snprintf(line, sizeof line, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
                     look, cls, back, -1, NO_BALL, NO_BALL, NO_BALL, NO_BALL, NO_BALL, NO_BALL,
                     rx, ry, NO_BALL, NO_BALL, NO_BALL, NO_BALL);
            text_add(p.text_looks, sizeof p.text_looks, line);
            free(f.map);
            free(f.oams);
            free(f.cells);
            free(f.cframes);
            free(f.cseqs);
            free(f.nodes);
            free(f.mcs);
            free(f.mframes);
            free(f.mseqs);
        }
        snprintf(line, sizeof line, "%d %d 0 0 0\n", cls, gender);
        text_add(p.text_classes, sizeof p.text_classes, line);
    }

    /* --- the class names */
    {
        blob base = others_bank(others, pt_msg.m[PT_CLASS_BANK]);
        bank bk = msg_read(base, "the class name bank");
        msg *grown;
        u32 i;

        if ((int)bk.n != class_first)
            die("bank %d holds %u names and the first look's class is %d; the "
                "packages loaded beside this one do not line up", PT_CLASS_BANK, bk.n, class_first);
        grown = xmalloc((bk.n + LOOKS) * sizeof *grown);
        memcpy(grown, bk.e, bk.n * sizeof *grown);
        for (i = 0; i < LOOKS; i++) {
            const msg *from = &bk.e[kLookGender[i]];

            grown[bk.n + i].n = from->n;
            grown[bk.n + i].c = xmalloc(from->n * sizeof(u16));
            memcpy(grown[bk.n + i].c, from->c, from->n * sizeof(u16));
        }
        bk.e = grown;
        bk.n += LOOKS;
        pkg_member(&p, PT_MSG, PT_CLASS_BANK, msg_write(&bk));
    }

    /* --- write, staged, then swapped in */
    path_join(cooked, sizeof cooked, pkg, ".cooked.new");
    mkdir_p(pkg);
    rm_tree(cooked);
    mkdir_p(cooked);
    mkdir_narc(narc_mmodel, sizeof narc_mmodel, cooked, "narc/" PT_MMODEL_ARC);
    mkdir_narc(narc_front, sizeof narc_front, cooked, "narc/" PT_TRFGRA);
    mkdir_narc(narc_back, sizeof narc_back, cooked, "narc/" PT_TRBGRA);
    mkdir_narc(narc_msg, sizeof narc_msg, cooked, "narc/" PT_MSG);
    mkdir_narc(gen, sizeof gen, cooked, "generated");
    {
        u32 i;
        char name[24];

        for (i = 0; i < p.n; i++) {
            const char *dir = strcmp(p.m[i].narc, PT_MMODEL_ARC) == 0 ? narc_mmodel
                            : strcmp(p.m[i].narc, PT_TRFGRA) == 0 ? narc_front
                            : strcmp(p.m[i].narc, PT_TRBGRA) == 0 ? narc_back : narc_msg;

            snprintf(name, sizeof name, "%u", p.m[i].index);
            write_bytes(dir, name, p.m[i].data.p, p.m[i].data.len);
        }
        for (look = 0; look < LOOKS; look++) {
            snprintf(name, sizeof name, "look_card_%d.bin", look);
            write_bytes(gen, name, p.card[look].p, p.card[look].len);
        }
        write_text(gen, "billboard_gfx.txt", p.text_gfx);
        write_text(gen, "trainer_classes.txt", p.text_classes);
        write_text(gen, "player_looks.txt", p.text_looks);
        snprintf(line, sizeof line, "v1 %016llx\n", (unsigned long long)COOK_FNV_OFFSET);
        write_text(cooked, "digest", line);
    }
    {
        char final[1024];

        path_join(final, sizeof final, pkg, ".cooked");
        rm_tree(final);
        if (rename(cooked, final) != 0)
            die("cannot move %s into place", cooked);
        write_text(pkg, "mod.toml",
                   "id = \"looks\"\nname = \"Player Looks\"\nversion = \"1.0.0\"\n"
                   "authors = [\"openmmo\"]\nrequires = []\nload_after = []\n");
        snprintf(line, sizeof line, "%s %d %d %d\nfill %s\nfill %s\nfill %s\n",
                 LOOKCOMPOSE_STAMP, mmodel_first, class_first, back_first, hg_code, bw_code, pt_code);
        write_text(pkg, "composed.txt", line);
    }
    return 0;
}

/* --------------------------------------------------------------- Play */

static int lc_dir_exists(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* One past the highest trainer class or back picture any other package
 * claims: a class is five members, so the member allocator's answer is
 * rounded up to the next whole class. */
static int class_base_of(const char *root, const char *others, const char *archive, int floor_count)
{
    int member = mmo_followcompose_base(root, others, archive, floor_count * CLASS_FILES);

    return (member + CLASS_FILES - 1) / CLASS_FILES;
}

int mmo_lookcompose_ensure(const mmo_launch_settings *s, const char *port_exe,
                           void (*note)(void *ud, const char *line),
                           void *ud, char *err, size_t errcap)
{
    char root[MMO_LAUNCH_PATH];
    char pkg[MMO_LAUNCH_PATH + 32];
    char stamp[MMO_LAUNCH_PATH + 64];
    char hg_file[MMO_LAUNCH_PATH], bw_file[MMO_LAUNCH_PATH];
    char why[160];
    char have[256] = "";
    char want[256];
    char others[MMO_LAUNCH_TEXT + 256];
    char others_arg[MMO_LAUNCH_TEXT + MMO_LAUNCH_PATH + 512];
    int mmodel_first, class_first, back_first;
    FILE *f;

    if (err != NULL && errcap > 0)
        err[0] = '\0';
    mmo_launch_mods_root(s, port_exe, root, sizeof root);
    path_join(pkg, sizeof pkg, root, "looks");
    path_join(stamp, sizeof stamp, pkg, "composed.txt");

    /* Allocated against the packages this session loads, after the follower
     * fill has allocated its own (the launcher asks in that order, and
     * followcompose skips a `looks` row so the two never chase each other). */
    mmo_launch_mods_list(s, root, others, sizeof others);
    mmodel_first = mmo_followcompose_base(root, others, "data/mmodel/mmodel.narc", PT_MMODEL_COUNT);
    class_first = class_base_of(root, others, "poketool/trgra/trfgra.narc", PT_CLASS_COUNT);
    back_first = class_base_of(root, others, "poketool/trgra/trbgra.narc", PT_BACK_COUNT);

    snprintf(want, sizeof want, "%s %d %d %d\n", LOOKCOMPOSE_STAMP, mmodel_first, class_first, back_first);
    f = fopen(stamp, "rb");
    if (f != NULL) {
        size_t n = fread(have, 1, sizeof have - 1, f);

        have[n] = '\0';
        fclose(f);
        if (strncmp(have, want, strlen(want)) == 0 && lc_dir_exists(pkg))
            return 1;           /* filled by this build, at these bases */
    }

    if (!mmo_sound_slot_status(s, MMO_LAUNCH_CART_HEARTGOLD, hg_file, sizeof hg_file, why, sizeof why)) {
        snprintf(err, errcap, "the HeartGold and Black trainers are drawn from your own "
                 "cartridges (%s): choose them on the settings face and they fill "
                 "themselves next time", why);
        return 0;
    }
    if (!mmo_sound_slot_status(s, MMO_LAUNCH_CART_BLACK, bw_file, sizeof bw_file, why, sizeof why)) {
        snprintf(err, errcap, "the HeartGold and Black trainers are drawn from your own "
                 "cartridges (%s): choose them on the settings face and they fill "
                 "themselves next time", why);
        return 0;
    }
    if (s->rom[0] == '\0') {
        snprintf(err, errcap, "the other games' trainers are laid into your own Platinum's "
                 "sheets, and no Platinum is chosen");
        return 0;
    }
    if (note != NULL)
        note(ud, "composing the HeartGold and Black trainers out of your own cartridges; "
                 "this happens once...");
    snprintf(others_arg, sizeof others_arg, "%s:%s", root, others);
    if (mmo_lookcompose(hg_file, bw_file, s->rom, pkg, mmodel_first, class_first, back_first,
                        others_arg, err, errcap) != 0)
        return -1;
    if (note != NULL)
        note(ud, "the HeartGold and Black trainers are ready; they are kept for next time");
    return 1;
}
