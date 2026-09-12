/*
 * The follower package, filled from the player's own cartridge, in the
 * launcher.
 */
#include <dirent.h>
#include <setjmp.h>
#include <sys/stat.h>
#include <unistd.h>     /* rmdir: remove() does not drop a directory on Windows */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "followcompose.h"
#include "soundcompose.h"  /* mmo_sound_slot_status: one definition of
                              where the player keeps their Heart Gold */
#include "platform.h"
#include "follower_fill.gen.h"   /* mmo/src, on the launcher include path */
#include "follower_index.gen.h"  /* MMO_FOLLOWER_GFX_BASE, and only that */
#include "nitrorom.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* ---------------------------------------------------------------- source */
/* The archives this reads, by the path Heart Gold and Soul Silver both keep
 * them at. The names are stripped in an official image, so these are the ids;
 * portfollow.py's constants of the same names. */
#define SRC_MMODEL      "a/0/8/1"
#define SRC_TP_PARAM    "a/1/4/1"
#define SRC_TALK_COND   "a/2/2/0"
#define SRC_TALK_REACT  "a/2/2/1"
#define SRC_TALK_ANIM   "a/2/2/2"
#define SRC_TALK_SPECIES "a/2/3/1"
#define SRC_TALK_MSG    "a/0/2/7"
#define SRC_EMOTE       "a/1/0/3"

#define HG_TALK_MSG_BANK  265
#define HG_EMOTE_MODEL    130
#define HG_EMOTE_TEX_FIRST  2
#define HG_EMOTE_SEQ      150
#define HG_EMOTE_COUNT     14
#define HG_EMOTE_TEXTURES_PER 2
/* The ball the follower is recalled into (portfollow.HG_BALL_MEMBERS). */
#define HG_BALL_MODEL     129
#define HG_FLASH_MODEL    104
#define HG_FLASH_ANIM     164
#define HG_BALL_COUNT       3

/* ----------------------------------------------------------- destination */
#define DST_MMODEL "data/mmodel/mmodel.narc"
#define DST_EMOTE  "data/mmodel/fldeff.narc"
#define DST_TALK   "openmmo/follow_talk.narc"
#define PL_FLDEFF_COUNT 201

#define SEQ_WALK        64
#define SEQ_WALK_SHINY  65
#define BILLBOARD_MODEL_GENERIC_32x32 0
#define BILLBOARD_MODEL_GENERIC_64x64 5
#define COOK_FNV_OFFSET 0xCBF29CE484222325ULL

#define TALK_MAGIC        0x3154464FU   /* 'OFT1' */
#define TALK_VERSION      5
#define TALK_HEADER_BASE  594
#define TALK_ROW          20
#define TALK_WORLD_ROWS   70
#define TALK_MAP_ROWS     30
#define TALK_MAP_SECTIONS 235
#define TALK_REACT_BYTES  52
#define TALK_REACT_STEPS   5
#define TALK_STEP_BYTES    8
#define TALK_ANIM_BYTES   80
#define TALK_ANIM_FRAMES  10
#define TALK_ANIM_FRAME_BYTES 8
#define TALK_SPECIES_MIN  493
#define TALK_EMOTE_MAX    14
#define HG_SEQ_SE_END     2378
#define TALK_SOUND_NONE   0
#define TALK_SOUND_CRY    (HG_SEQ_SE_END + 1)
#define TALK_SOUND_CRY_ALT (HG_SEQ_SE_END + 2)

/* What a follower member looks like, measured across all 566 of them
 * (gen_followers.py's three oracles; this is the one that needs no checkout). */
#define FOLLOWER_FRAMES  8
#define FOLLOWER_FORMAT  3      /* 4bpp */

/* The stamp. A package filled by an older build of this file is refilled
 * rather than trusted, the way soundcompose stamps its tables' hash: the
 * version moves whenever what is written changes, so a player who updates
 * gets the new package without knowing there was one. */
#define FOLLOWCOMPOSE_STAMP "v2 566"

/* The shared reader, under this FILE'S own names. */
typedef MmoBlob blob;
typedef MmoMembers members;
#define die           mmo_nitro_die
#define xmalloc       mmo_nitro_alloc
#define xrealloc      mmo_nitro_grow
#define rd16          mmo_nitro_rd16
#define rd32          mmo_nitro_rd32
#define wr16          mmo_nitro_wr16
#define wr32          mmo_nitro_wr32
#define blob_dup      mmo_nitro_dup
#define read_file     mmo_nitro_read_file
#define narc_members  mmo_nitro_narc

/* modport.NitroRom.file_bytes. Every path this file asks for is one Heart
 * Gold and Soul Silver both keep, so a miss is not a missing archive, it is
 * the wrong cartridge, and the refusal says so. */
static blob nds_file(const blob rom, const char *want)
{
    return mmo_nitro_file(rom, want, "that file",
                          "it is not a Heart Gold or Soul Silver cartridge");
}


/* ----------------------------------------------------------------- nsbtx */
/*
 * nsbtx.read, reduced to the two questions this file asks of a texture set: how many textures
 * there are and what shape they are, and how many palettes and what they are called.
 */
#define NSBTX_NAME_LEN 16
#define NSBTX_MAX_TEX  64

typedef struct {
    u32 param;
    const u8 *data;
    u32 len;
} tex_entry;

typedef struct {
    tex_entry tex[NSBTX_MAX_TEX];
    int ntex;
    char pal[NSBTX_MAX_TEX][NSBTX_NAME_LEN + 1];
    int npal;
} tex_set;

/* The TEX0 block of an NSBTX or an NSBMD, both of which may carry one. */
static u32 find_tex0(blob b)
{
    u32 n, i;

    if (b.len < 0x10)
        return 0;
    if (memcmp(b.p, "BTX0", 4) != 0 && memcmp(b.p, "BMD0", 4) != 0)
        return 0;
    n = rd16(b.p + 0x0E);
    for (i = 0; i < n; i++) {
        u32 off;

        if (0x10 + 4 * i + 4 > b.len)
            return 0;
        off = rd32(b.p + 0x10 + 4 * i);
        if (off + 4 <= b.len && memcmp(b.p + off, "TEX0", 4) == 0)
            return off;
    }
    return 0;
}

/* One dictionary: how many entries, where their fixed-size records start,
 * how big a record is, and where the name table is. nsbtx.py's `_dict`. */
static int nsbtx_dict(blob b, u32 base, int *num, u32 *at, u32 *unit,
                      u32 *names)
{
    u32 data;

    if (base + 8 > b.len)
        return 0;
    *num = b.p[base + 1];
    data = base + rd16(b.p + base + 0x06);
    if (data + 4 > b.len)
        return 0;
    *unit = rd16(b.p + data);
    *names = data + 4 + rd16(b.p + data + 2) - 4;
    *at = data + 4;
    /* `ofs_name` is measured from the record block's own start, which is
     * `data`, and the records begin four bytes into it. */
    *names = data + rd16(b.p + data + 2);
    return 1;
}

static int tex_read(blob b, tex_set *out)
{
    u32 t, tex_size, tex_dict, tex_data, comp, pal_dict;
    int num, i;
    u32 at, unit, names;

    memset(out, 0, sizeof *out);
    t = find_tex0(b);
    if (t == 0 || t + 0x3C > b.len)
        return 0;
    tex_size = (u32)rd16(b.p + t + 0x0C) << 3;
    tex_dict = t + rd16(b.p + t + 0x0E);
    tex_data = t + rd32(b.p + t + 0x14);
    comp = (u32)rd16(b.p + t + 0x1C) << 3;
    pal_dict = t + rd32(b.p + t + 0x34);
    if (comp)
        return 0;    /* 4x4-compressed: needs a second data block we do not read */

    if (!nsbtx_dict(b, tex_dict, &num, &at, &unit, &names))
        return 0;
    if (num > NSBTX_MAX_TEX)
        return 0;
    out->ntex = num;
    for (i = 0; i < num; i++) {
        u32 param, off, w, h, fmt, bits, ln;

        if (at + (u32)i * unit + 8 > b.len)
            return 0;
        param = rd32(b.p + at + (u32)i * unit);
        off = (param & 0xFFFF) << 3;
        w = 8u << ((param >> 20) & 7);
        h = 8u << ((param >> 23) & 7);
        fmt = (param >> 26) & 7;
        switch (fmt) {
        case 1: case 4: case 6: bits = 8; break;
        case 2: bits = 2; break;
        case 3: bits = 4; break;
        case 7: bits = 16; break;
        default: return 0;      /* compressed, or a format with no known size */
        }
        ln = w * h * bits / 8;
        if (off + ln > tex_size || tex_data + off + ln > b.len)
            return 0;
        out->tex[i].param = param & ~0xFFFFu;
        out->tex[i].data = b.p + tex_data + off;
        out->tex[i].len = ln;
    }

    if (!nsbtx_dict(b, pal_dict, &num, &at, &unit, &names))
        return 0;
    if (num > NSBTX_MAX_TEX)
        return 0;
    out->npal = num;
    for (i = 0; i < num; i++) {
        u32 nm = names + (u32)i * NSBTX_NAME_LEN;
        int k;

        if (nm + NSBTX_NAME_LEN > b.len)
            return 0;
        for (k = 0; k < NSBTX_NAME_LEN && b.p[nm + k]; k++)
            out->pal[i][k] = (char)b.p[nm + k];
        out->pal[i][k] = '\0';
    }
    return 1;
}

/* gen_followers._shape: the member's size in pixels, or 0 with a sentence. */
static int art_shape(blob b, const char **why)
{
    tex_set set;
    int i, size = 0, seen0 = 0, seen1 = 0;

    if (b.len < 4 || memcmp(b.p, "BTX0", 4) != 0) {
        *why = "not an NSBTX";
        return 0;
    }
    if (!tex_read(b, &set)) {
        *why = "unreadable as an NSBTX";
        return 0;
    }
    if (set.ntex != FOLLOWER_FRAMES) {
        *why = "not eight frames";
        return 0;
    }
    for (i = 0; i < set.ntex; i++) {
        u32 param = set.tex[i].param;
        int w = 8 << ((param >> 20) & 7);
        int h = 8 << ((param >> 23) & 7);

        if ((int)((param >> 26) & 7) != FOLLOWER_FORMAT) {
            *why = "a frame is not 4bpp";
            return 0;
        }
        if (w != h) {
            *why = "a frame is not square";
            return 0;
        }
        if (size == 0)
            size = w;
        else if (size != w) {
            *why = "the frames are not all one size";
            return 0;
        }
    }
    if (size != 32 && size != 64) {
        *why = "the frames are neither 32 nor 64";
        return 0;
    }
    for (i = 0; i < set.npal; i++) {
        if (strcmp(set.pal[i], "tsure_poke0") == 0) seen0 = 1;
        if (strcmp(set.pal[i], "tsure_poke1") == 0) seen1 = 1;
    }
    if (set.npal != 2 || !seen0 || !seen1) {
        *why = "not the normal/shiny palette pair";
        return 0;
    }
    return size;
}

/* gen_followers._walk_sequence: the billboard gfx sequence a follower walks on. */
static u32 walk_sequence(members mm)
{
    u32 i, found = 0;
    int n_found = 0;

    for (i = 0; i < mm.n; i++) {
        blob b = mm.m[i];
        u32 n;
        int g, distinct = 0, ok = 1;
        u8 seen[256];

        if (b.len < 8)
            continue;
        if (memcmp(b.p, "BTX0", 4) == 0 || memcmp(b.p, "BMD0", 4) == 0)
            continue;
        n = rd32(b.p);
        if (n != 16 || b.len < 4 + 4 * n)
            continue;
        memset(seen, 0, sizeof seen);
        for (g = 0; g < 16; g++) {
            u8 t = b.p[4 + 2 * n + g];

            if (!seen[t]) {
                seen[t] = 1;
                distinct++;
            }
        }
        if (distinct != FOLLOWER_FRAMES)
            continue;
        for (g = 0; g < 16 && ok; g += 4) {
            const u8 *t = b.p + 4 + 2 * n + g;

            if (!(t[0] == t[2] && t[1] == t[3] && t[0] != t[1]))
                ok = 0;
        }
        if (ok) {
            found = i;
            n_found++;
        }
    }
    if (n_found == 1)
        return found;
    if (n_found == 0)
        die("no member of %s is the follower's walk sequence, that cartridge"
            " does not carry follower art", SRC_MMODEL);
    die("%d members of %s look like the follower's walk sequence and it is"
        " meant to be unique", n_found, SRC_MMODEL);
    return 0;
}

/* portfollow.timeline: (facings, frames per facing) off the step list. */
static void timeline(blob b, int *facings, int *frames)
{
    u32 n = rd32(b.p);
    u32 i;
    int step;

    if (n == 0 || n % 4 || b.len < 4 + 4 * n)
        die("the walk sequence is not four-step facings of a two-frame cycle");
    for (i = 0; i < n; i += 4) {
        const u8 *t = b.p + 4 + 2 * n + i;

        if (!(t[0] == t[2] && t[1] == t[3] && t[0] != t[1]))
            die("the walk sequence is not four-step facings of a two-frame"
                " cycle");
    }
    step = n > 1 ? (int)rd16(b.p + 4 + 2) - (int)rd16(b.p + 4) : 1;
    for (i = 0; i + 1 < n; i++) {
        int d = (int)rd16(b.p + 4 + 2 * (i + 1)) - (int)rd16(b.p + 4 + 2 * i);

        if (d != step)
            die("the walk sequence's steps are not evenly spaced");
    }
    *facings = (int)(n / 4);
    *frames = step * 4;
}

/* portfollow.shiny_sequence: the same walk, read through the second palette. */
static blob shiny_seq(blob b)
{
    u32 n = rd32(b.p);
    u32 head = 4 + 3 * n;
    blob out;

    if (b.len < 4 + 4 * n)
        die("the walk sequence is %u bytes and its own count says it needs %u",
            b.len, 4 + 4 * n);
    out.len = b.len;
    out.p = xmalloc(out.len);
    memcpy(out.p, b.p, head);
    memset(out.p + head, 1, n);
    memcpy(out.p + head + n, b.p + 4 + 4 * n, b.len - (4 + 4 * n));
    return out;
}

/* ------------------------------------------------------------------ talk */
/*
 * portfollow.decode_bank, kept for the one thing the fill needs from it: how many lines the
 * follower's message bank holds, so a reaction that names a line the bank does not have is
 * refused before a player meets it.
 */
static u32 bank_lines(blob b, const char *what)
{
    u32 n, seed, i;

    if (b.len < 4)
        die("%s: the follower's message bank is %u bytes", what, b.len);
    n = rd16(b.p);
    seed = rd16(b.p + 2);
    for (i = 0; i < n; i++) {
        u32 o, ln, k, kk;

        if (4 + (u64)i * 8 + 8 > b.len)
            die("%s: the message bank does not hold its own line table", what);
        o = rd32(b.p + 4 + i * 8);
        ln = rd32(b.p + 4 + i * 8 + 4);
        k = (seed * 765u * (i + 1)) & 0xFFFF;
        kk = k | (k << 16);
        o ^= kk;
        ln ^= kk;
        if ((u64)o + (u64)ln * 2 > b.len)
            die("%s: the message bank does not hold its own line %u", what, i);
    }
    return n;
}

/* portfollow.talk_sources: the five tables, each checked against its siblings. */
static void talk_check(members cond, members react, members anim,
                       members species, u32 lines)
{
    u32 i, named = 0;

    if (cond.n != TALK_MAP_SECTIONS + 1)
        die("%s holds %u members and the talk wants one world table and %d map"
            " sections", SRC_TALK_COND, cond.n, TALK_MAP_SECTIONS);
    if (cond.m[0].len != TALK_WORLD_ROWS * TALK_ROW)
        die("%s member 0 is %u bytes, not %d rows of %d", SRC_TALK_COND,
            cond.m[0].len, TALK_WORLD_ROWS, TALK_ROW);
    for (i = 1; i < cond.n; i++) {
        if (cond.m[i].len != TALK_MAP_ROWS * TALK_ROW)
            die("%s member %u is %u bytes, not %d rows of %d", SRC_TALK_COND,
                i, cond.m[i].len, TALK_MAP_ROWS, TALK_ROW);
    }
    for (i = 0; i < react.n; i++) {
        if (react.m[i].len != TALK_REACT_BYTES)
            die("%s member %u is %u bytes, not %d", SRC_TALK_REACT, i,
                react.m[i].len, TALK_REACT_BYTES);
    }
    for (i = 0; i < anim.n; i++) {
        if (anim.m[i].len != TALK_ANIM_BYTES)
            die("%s member %u is %u bytes, not %d", SRC_TALK_ANIM, i,
                anim.m[i].len, TALK_ANIM_BYTES);
    }
    if (species.n != 1 || species.m[0].len < TALK_SPECIES_MIN)
        die("%s holds %u member(s) of %u bytes and the talk wants one of at"
            " least %d", SRC_TALK_SPECIES, species.n,
            species.n ? species.m[0].len : 0, TALK_SPECIES_MIN);

    for (i = 0; i < cond.n; i++) {
        u32 r, rows = cond.m[i].len / TALK_ROW;

        for (r = 0; r < rows; r++) {
            u32 rid = rd16(cond.m[i].p + r * TALK_ROW + 0x0A) >> 6;

            if (rid == 0)
                continue;
            if (rid > react.n)
                die("%s member %u row %u names reaction %u and the cartridge"
                    " holds %u", SRC_TALK_COND, i, r, rid, react.n);
            named++;
        }
    }
    if (named == 0)
        die("%s names no reaction at all, this is not a follower talk table",
            SRC_TALK_COND);

    for (i = 0; i < react.n; i++) {
        const u8 *m = react.m[i].p;
        int s;
        u32 f;

        for (s = 0; s < TALK_REACT_STEPS; s++) {
            u32 a = rd16(m + s * TALK_STEP_BYTES);
            u32 msg = rd16(m + s * TALK_STEP_BYTES + 2);
            u32 snd = rd16(m + s * TALK_STEP_BYTES + 4);
            u32 emote = m[s * TALK_STEP_BYTES + 6];

            if (a == 0xFFFF)
                break;
            if (a > anim.n)
                die("reaction %u step %d wants animation %u and the cartridge"
                    " holds %u", i + 1, s, a, anim.n);
            if (msg > lines)
                die("reaction %u step %d wants line %u and the bank holds %u",
                    i + 1, s, msg, lines);
            if (snd != TALK_SOUND_NONE && snd != TALK_SOUND_CRY
                && snd != TALK_SOUND_CRY_ALT)
                die("reaction %u step %d plays sound %u, which is neither"
                    " silence nor the cry", i + 1, s, snd);
            if (emote > TALK_EMOTE_MAX)
                die("reaction %u step %d wants emote %u and the interpreter's"
                    " own ceiling is %d", i + 1, s, emote, TALK_EMOTE_MAX);
        }
        for (f = 0; f < 2; f++) {
            u32 follow = rd16(m + 44 + f * 2);

            if (follow > react.n)
                die("reaction %u answers a question with reaction %u and the"
                    " cartridge holds %u", i + 1, follow, react.n);
        }
    }

    /* An animation ends at a 0xFF frame or at the tenth, and both happen: the
     * interpreter checks the index before the terminator. What is worth
     * refusing is a facing byte the object cannot be set to, since that one is
     * written straight onto the map object. */
    for (i = 0; i < anim.n; i++) {
        int f;

        for (f = 0; f < TALK_ANIM_FRAMES; f++) {
            u8 face = anim.m[i].p[f * TALK_ANIM_FRAME_BYTES];

            if (face == 0xFF)
                break;
            if (face > 4)
                die("animation %u frame %d faces %d, and a direction is 0"
                    " (leave it) or 1..4", i, f, face);
        }
    }
}

/* portfollow.emote_sources: the bubble's model, its sequence and its fourteen sheets. */
static void emote_check(members fx)
{
    u32 need = HG_EMOTE_TEX_FIRST + HG_EMOTE_COUNT;
    u32 n, i;
    const u8 *seq;
    u32 seqlen;

    if (need < (u32)HG_EMOTE_SEQ + HG_EMOTE_COUNT)
        need = (u32)HG_EMOTE_SEQ + HG_EMOTE_COUNT;
    if (need < HG_EMOTE_MODEL)
        need = HG_EMOTE_MODEL;
    if (need < HG_FLASH_ANIM + 1)
        need = HG_FLASH_ANIM + 1;
    if (fx.n < need)
        die("%s holds %u members and the emote wants at least %u", SRC_EMOTE,
            fx.n, need);
    if (fx.m[HG_EMOTE_MODEL].len < 4
        || memcmp(fx.m[HG_EMOTE_MODEL].p, "BMD0", 4) != 0)
        die("%s member %d is not the BMD0 the bubble is drawn on", SRC_EMOTE,
            HG_EMOTE_MODEL);

    seq = fx.m[HG_EMOTE_SEQ].p;
    seqlen = fx.m[HG_EMOTE_SEQ].len;
    n = seqlen >= 4 ? rd32(seq) : 0;
    if (n != 4 || seqlen != 4 + 4 * n)
        die("%s member %d is not a four-step frame sequence", SRC_EMOTE,
            HG_EMOTE_SEQ);
    for (i = 0; i < 4; i++) {
        u8 want = (u8)(i & 1);

        if (seq[4 + 2 * n + i] != want)
            die("the bubble's sequence does not flicker between its two"
                " textures");
    }
    for (i = 1; i < HG_EMOTE_COUNT; i++) {
        if (fx.m[HG_EMOTE_SEQ + i].len != seqlen
            || memcmp(fx.m[HG_EMOTE_SEQ + i].p, seq, seqlen) != 0)
            die("%s member %u is not the same sequence as member %d; the fill"
                " carries one because the game's fourteen are identical",
                SRC_EMOTE, HG_EMOTE_SEQ + i, HG_EMOTE_SEQ);
    }
    for (i = 0; i < HG_EMOTE_COUNT; i++) {
        blob b = fx.m[HG_EMOTE_TEX_FIRST + i];
        tex_set set;

        if (b.len < 4 || memcmp(b.p, "BTX0", 4) != 0)
            die("%s member %u is not a texture", SRC_EMOTE,
                HG_EMOTE_TEX_FIRST + i);
        if (!tex_read(b, &set))
            die("%s member %u is unreadable as a texture set", SRC_EMOTE,
                HG_EMOTE_TEX_FIRST + i);
        if (set.ntex != HG_EMOTE_TEXTURES_PER)
            die("emote %u holds %d texture(s) and its sequence asks for %d",
                i, set.ntex, HG_EMOTE_TEXTURES_PER);
        if (set.npal != 1)
            die("emote %u holds %d palettes and the sequence selects one",
                i, set.npal);
    }
    if (fx.m[HG_BALL_MODEL].len < 4
        || memcmp(fx.m[HG_BALL_MODEL].p, "BMD0", 4) != 0)
        die("%s member %d is not the BMD0 the ball is drawn on", SRC_EMOTE,
            HG_BALL_MODEL);
    if (fx.m[HG_FLASH_MODEL].len < 4
        || memcmp(fx.m[HG_FLASH_MODEL].p, "BMD0", 4) != 0)
        die("%s member %d is not the BMD0 the ball's flash is drawn on",
            SRC_EMOTE, HG_FLASH_MODEL);
    if (fx.m[HG_FLASH_ANIM].len < 4
        || memcmp(fx.m[HG_FLASH_ANIM].p, "BTA0", 4) != 0)
        die("%s member %d is not the BTA0 the ball's flash plays", SRC_EMOTE,
            HG_FLASH_ANIM);
}

/* ----------------------------------------------------------------- write */
static void path_join(char *out, size_t cap, const char *a, const char *b)
{
    snprintf(out, cap, "%s%s%s", a, mmo_plat_sep(), b);
}

/* mkdir -p, over a path whose separators are this platform's. */
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

/* A NitroFS-shaped relative path under a root, with this platform's
 * separators, and every directory on the way made. */
static void mkdir_narc(char *out, size_t cap, const char *root,
                       const char *rel)
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

/*
 * Remove a tree, for the one thing that has to happen before a refill: the old members have to
 * Go.
 */
static void rm_dir_r(const char *path)
{
    DIR *d = opendir(path);
    struct dirent *e;

    if (d == NULL) {
        remove(path);
        return;
    }
    while ((e = readdir(d)) != NULL) {
        char child[2048];

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        snprintf(child, sizeof child, "%s%s%s", path, mmo_plat_sep(),
                 e->d_name);
        if (remove(child) != 0)
            rm_dir_r(child);        /* a directory, or a file we cannot drop */
    }
    closedir(d);
    /* remove() does not drop a directory on Windows: the C library sends it to
     * DeleteFile, which refuses one, so this walk emptied the tree and left
     * the skeleton of directories standing. The rename that publishes the
     * package then failed against a destination that still existed, and the
     * player got a package with no files in it and a game that exits before it
     * draws. Only a second fill can hit it; the first has no .cooked to
     * replace. lookcompose's copy of this walk has always ended in rmdir. */
    if (remove(path) != 0)
        rmdir(path);
}

static void rm_tree(const char *path)
{
    const char *base = mmo_plat_last_sep(path);

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

static void write_member(const char *dir, u32 index, blob b)
{
    char name[24];

    snprintf(name, sizeof name, "%u", index);
    write_bytes(dir, name, b.p, b.len);
}

static void write_text(const char *dir, const char *name, const char *s)
{
    write_bytes(dir, name, (const u8 *)s, (u32)strlen(s));
}

/* ---------------------------------------------------------------- header */
/*
 * portfollow.talk_header. Member 0: the magic, and where each section starts and how long it
 * is.
 */
static void talk_header(u8 *out, const u32 *counts, u32 msg_strings,
                        u32 emote_first, u32 emote_count, u32 ball_first)
{
    u32 base = 1;
    int i;

    wr32(out, TALK_MAGIC);
    wr16(out + 4, TALK_VERSION);
    wr16(out + 6, TALK_HEADER_BASE);
    wr16(out + 8, (u16)msg_strings);
    for (i = 0; i < 8; i++) {
        wr16(out + 10 + i * 4, (u16)base);
        wr16(out + 10 + i * 4 + 2, (u16)counts[i]);
        base += counts[i];
    }
    wr16(out + 42, (u16)emote_first);
    wr16(out + 44, (u16)(emote_first + 1));
    wr16(out + 46, (u16)(emote_first + 2));
    wr16(out + 48, (u16)emote_count);
    wr16(out + 50, (u16)ball_first);
    wr16(out + 52, (u16)(ball_first + 1));
    wr16(out + 54, (u16)(ball_first + 2));
}

/* ------------------------------------------------------------- allocate */
/* Where this package'S appended MEMBERS start, and why it is not a constant. */
static int narc_high(const char *pkg, const char *archive)
{
    char dir[1024 + 128];
    DIR *d;
    struct dirent *e;
    int high = -1;
    size_t i;
    char sep = mmo_plat_sep()[0];

    snprintf(dir, sizeof dir, "%s%s.cooked%snarc%s%s", pkg, mmo_plat_sep(),
             mmo_plat_sep(), mmo_plat_sep(), archive);
    for (i = strlen(pkg); dir[i]; i++) {
        if (dir[i] == '/')
            dir[i] = sep;
    }
    d = opendir(dir);
    if (d == NULL)
        return -1;
    while ((e = readdir(d)) != NULL) {
        const char *n = e->d_name;
        int v = 0;

        if (n[0] < '0' || n[0] > '9')
            continue;
        for (; *n != '\0'; n++) {
            if (*n < '0' || *n > '9') {
                v = -1;
                break;
            }
            v = v * 10 + (*n - '0');
        }
        if (v > high)
            high = v;
    }
    closedir(d);
    return high;
}

/* Whether this row of the list is that package, whole row only. */
static int row_is(const char *at, size_t len, const char *name)
{
    return len == strlen(name) && strncmp(at, name, len) == 0;
}

/*
 * The base for one archive, over a comma-separated list of sibling packages. `floor` is what
 * the built image itself holds, which is where an append starts when nothing else has claimed
 * anything. `self` is the package being allocated, and it decides which rows are skipped.
 */
int mmo_followcompose_base(const char *root, const char *others,
                           const char *archive, int floor_count,
                           const char *self)
{
    const char *at = others;
    int base = floor_count;

    if (self == NULL)
        self = "followers";

    while (at != NULL && *at != '\0') {
        const char *end = strchr(at, ',');
        size_t len = end != NULL ? (size_t)(end - at) : strlen(at);
        char pkg[1024];
        int high;
        int skip;

        /* This package's own row always, and `looks` when the follower fill is
         * asking, because that one is allocated after it and against its
         * claims. Skipping `looks` for both callers handed the looks fill the
         * follower fill's own base: the two then claimed the same members, the
         * later load won them, and the field died on the first billboard whose
         * sequence asked the substituted body for a texture it does not carry.
         * The looks fill counts `followers` precisely because it comes second. */
        skip = row_is(at, len, self)
               || (row_is(at, len, "looks") && strcmp(self, "followers") == 0);

        if (len > 0 && !skip) {
            snprintf(pkg, sizeof pkg, "%s%s%.*s", root, mmo_plat_sep(),
                     (int)len, at);
            high = narc_high(pkg, archive);
            if (high >= base)
                base = high + 1;
        }
        if (end == NULL)
            break;
        at = end + 1;
    }
    return base;
}

/* --------------------------------------------------------------- compose */
int mmo_followcompose(const char *rom_path, const char *pkg,
                      int mmodel_first, int emote_at,
                      char *err, size_t errcap)
{
    blob rom;
    blob mmodel_arc, tp_arc, cond_arc, react_arc, anim_arc, species_arc;
    blob msg_arc, fx_arc;
    members mmodel, tp, cond, react, anim, species, msg, fx;
    u32 seq_member, lines, at, emote_first, first_member, i;
    int facings, frames;
    char cooked[1024], gen[1024], narc[1024], fxdir[1024], talkdir[1024];
    char line[128];
    /* volatile because they are written between the setjmp below and any
     * longjmp out of die(), and a jump discards a register copy. Nothing
     * reads them after the jump, the refusal returns straight away, but
     * the compiler cannot know that. */
    u8 * volatile sizes = NULL;
    u8 * volatile tps = NULL;
    char * volatile gfx_txt = NULL;
    volatile size_t gfx_cap = 0, gfx_len = 0;
    jmp_buf jb;

    if (err != NULL && errcap > 0)
        err[0] = '\0';
    if (setjmp(jb) != 0)
        return -1;
    mmo_nitro_catch(&jb, err, errcap);

    rom = read_file(rom_path);

    /* Everything is READ and CHECKED before a byte is written, so a cartridge
     * that cannot answer for itself costs the package nothing. */
    mmodel_arc = nds_file(rom, SRC_MMODEL);
    tp_arc = nds_file(rom, SRC_TP_PARAM);
    mmodel = narc_members(mmodel_arc, SRC_MMODEL);
    tp = narc_members(tp_arc, SRC_TP_PARAM);
    if (tp.n != MMO_FOLLOWFILL_SPRITES)
        die("%s holds %u records and the follower tables want %d",
            SRC_TP_PARAM, tp.n, MMO_FOLLOWFILL_SPRITES);

    seq_member = walk_sequence(mmodel);
    timeline(mmodel.m[seq_member], &facings, &frames);

    cond_arc = nds_file(rom, SRC_TALK_COND);
    react_arc = nds_file(rom, SRC_TALK_REACT);
    anim_arc = nds_file(rom, SRC_TALK_ANIM);
    species_arc = nds_file(rom, SRC_TALK_SPECIES);
    msg_arc = nds_file(rom, SRC_TALK_MSG);
    fx_arc = nds_file(rom, SRC_EMOTE);
    cond = narc_members(cond_arc, SRC_TALK_COND);
    react = narc_members(react_arc, SRC_TALK_REACT);
    anim = narc_members(anim_arc, SRC_TALK_ANIM);
    species = narc_members(species_arc, SRC_TALK_SPECIES);
    msg = narc_members(msg_arc, SRC_TALK_MSG);
    fx = narc_members(fx_arc, SRC_EMOTE);
    if (HG_TALK_MSG_BANK >= (int)msg.n)
        die("%s holds %u message banks and the follower's lines are bank %d",
            SRC_TALK_MSG, msg.n, HG_TALK_MSG_BANK);
    lines = bank_lines(msg.m[HG_TALK_MSG_BANK], SRC_TALK_MSG);
    talk_check(cond, react, anim, species, lines);
    emote_check(fx);

    /* The two bases the caller allocated, floored at what the built image
     * itself holds: a base below that would claim a member the image already
     * has, which is a replace rather than an append and would put a follower
     * over one of this game's own. */
    first_member = mmodel_first > MMO_FOLLOWFILL_DST_FIRST
                   ? (u32)mmodel_first : (u32)MMO_FOLLOWFILL_DST_FIRST;
    emote_first = emote_at > PL_FLDEFF_COUNT
                  ? (u32)emote_at : (u32)PL_FLDEFF_COUNT;

    /* Every member the fill will write, checked before any of it is staged.
     * The size is answered twice, by the art's own shape and by the
     * cartridge's tp_param size byte, and a cartridge where the two disagree
     * is refused rather than guessed at. */
    sizes = xmalloc(MMO_FOLLOWFILL_SPRITES);
    tps = xmalloc(MMO_FOLLOWFILL_SPRITES * 4);
    for (i = 0; i < MMO_FOLLOWFILL_SPRITES; i++) {
        u32 member = kFollowerSrcMember[i];
        const char *why = "";
        int size, k;

        if (member >= mmodel.n)
            die("follower %u wants %s member %u and the image has %u",
                i, SRC_MMODEL, member, mmodel.n);
        size = art_shape(mmodel.m[member], &why);
        if (size == 0)
            die("follower %u (%s member %u) is not follower art: %s",
                i, SRC_MMODEL, member, why);
        if (tp.m[i].len < 2)
            die("%s record %u is %u bytes", SRC_TP_PARAM, i, tp.m[i].len);
        if ((tp.m[i].p[1] != 0) != (size == 64))
            die("follower %u (%s member %u): the art is %dx%d and %s byte 1"
                " says %d", i, SRC_MMODEL, member, size, size, SRC_TP_PARAM,
                tp.m[i].p[1]);
        sizes[i] = (u8)(size == 64 ? BILLBOARD_MODEL_GENERIC_64x64
                                   : BILLBOARD_MODEL_GENERIC_32x32);
        /* The record itself crosses, four bytes as the cartridge keeps it
         * (a/1/4/1); the client reads bytes 1 and 2 the way its own game's
         * FollowMon_SetObjectParams does. */
        for (k = 0; k < 4; k++)
            tps[i * 4 + k] = (u32)k < tp.m[i].len ? tp.m[i].p[k] : 0;
    }

    /* Staged beside the package's own cooked directory and swapped in at the
     * end, so a refusal above leaves the package that is already there intact,
     * and so a refill at a different base cannot leave the last one's
     * members lying in the archive claiming somebody else's band. */
    path_join(cooked, sizeof cooked, pkg, ".cooked.new");
    mkdir_p(pkg);
    rm_tree(cooked);
    mkdir_p(cooked);
    mkdir_narc(narc, sizeof narc, cooked, "narc/" DST_MMODEL);
    mkdir_narc(gen, sizeof gen, cooked, "generated");

    at = first_member;
    for (i = 0; i < MMO_FOLLOWFILL_SPRITES; i++) {
        char row[64];
        size_t need;

        write_member(narc, at, mmodel.m[kFollowerSrcMember[i]]);
        /* Two bands: the normal coats, then the same members again read
         * through the shiny sequence. A second band rather than interleaved,
         * so a client turns a normal id into its shiny one by adding the
         * count and nothing else. */
        snprintf(row, sizeof row, "%u %u %d %d\n",
                 (unsigned)(MMO_FOLLOWER_GFX_BASE + i), at, sizes[i], SEQ_WALK);
        need = gfx_len + strlen(row) + 1;
        if (need > gfx_cap) {
            gfx_cap = need * 2 + 4096;
            gfx_txt = xrealloc(gfx_txt, gfx_cap);
        }
        memcpy(gfx_txt + gfx_len, row, strlen(row));
        gfx_len += strlen(row);
        gfx_txt[gfx_len] = '\0';
        at++;
    }
    for (i = 0; i < MMO_FOLLOWFILL_SPRITES; i++) {
        char row[64];
        size_t need;

        snprintf(row, sizeof row, "%u %u %d %d\n",
                 (unsigned)(MMO_FOLLOWER_GFX_BASE + MMO_FOLLOWFILL_SPRITES + i),
                 first_member + i, sizes[i], SEQ_WALK_SHINY);
        need = gfx_len + strlen(row) + 1;
        if (need > gfx_cap) {
            gfx_cap = need * 2 + 4096;
            gfx_txt = xrealloc(gfx_txt, gfx_cap);
        }
        memcpy(gfx_txt + gfx_len, row, strlen(row));
        gfx_len += strlen(row);
        gfx_txt[gfx_len] = '\0';
    }

    {
        blob shiny;
        char seq_txt[64];
        u32 walk_at = at;

        write_member(narc, walk_at, mmodel.m[seq_member]);
        at++;
        shiny = shiny_seq(mmodel.m[seq_member]);
        write_member(narc, at, shiny);
        free(shiny.p);
        snprintf(seq_txt, sizeof seq_txt, "%d %u %d %d\n%d %u %d %d\n",
                 SEQ_WALK, walk_at, facings, frames,
                 SEQ_WALK_SHINY, at, facings, frames);
        at++;
        write_text(gen, "billboard_seq.txt", seq_txt);
    }

    /* The bubble's own members go in the archive the field effect manager
     * opens, in one contiguous run: the model, the sequence, then the fourteen
     * sheets in the source's own order, so an id is the base plus the emote. */
    mkdir_narc(fxdir, sizeof fxdir, cooked, "narc/" DST_EMOTE);
    write_member(fxdir, emote_first, fx.m[HG_EMOTE_MODEL]);
    write_member(fxdir, emote_first + 1, fx.m[HG_EMOTE_SEQ]);
    for (i = 0; i < HG_EMOTE_COUNT; i++)
        write_member(fxdir, emote_first + 2 + i, fx.m[HG_EMOTE_TEX_FIRST + i]);
    /* And the ball's three behind the sheets, in the header's own order. */
    write_member(fxdir, emote_first + 2 + HG_EMOTE_COUNT, fx.m[HG_BALL_MODEL]);
    write_member(fxdir, emote_first + 3 + HG_EMOTE_COUNT, fx.m[HG_FLASH_MODEL]);
    write_member(fxdir, emote_first + 4 + HG_EMOTE_COUNT, fx.m[HG_FLASH_ANIM]);

    mkdir_narc(talkdir, sizeof talkdir, cooked, "narc/" DST_TALK);
    {
        u8 head[56];
        u32 counts[8];
        u32 talk_at = 1;

        counts[0] = cond.n;
        counts[1] = react.n;
        counts[2] = anim.n;
        counts[3] = 1;                       /* the species category byte */
        counts[4] = 1;                       /* the map-section table */
        counts[5] = 1;                       /* the message bank */
        counts[6] = 1;                       /* four tp_param bytes a sprite */
        counts[7] = 1;                       /* the follow modes */
        talk_header(head, counts, lines, emote_first, HG_EMOTE_COUNT,
                    emote_first + 2 + HG_EMOTE_COUNT);
        write_bytes(talkdir, "0", head, sizeof head);

        for (i = 0; i < cond.n; i++)
            write_member(talkdir, talk_at++, cond.m[i]);
        for (i = 0; i < react.n; i++)
            write_member(talkdir, talk_at++, react.m[i]);
        for (i = 0; i < anim.n; i++)
            write_member(talkdir, talk_at++, anim.m[i]);
        write_member(talkdir, talk_at++, species.m[0]);
        {
            blob b;

            b.p = (u8 *)kFollowerMapSec;
            b.len = MMO_FOLLOWFILL_MAPSECS;
            write_member(talkdir, talk_at++, b);
        }
        write_member(talkdir, talk_at++, msg.m[HG_TALK_MSG_BANK]);
        {
            blob b;

            b.p = tps;
            b.len = MMO_FOLLOWFILL_SPRITES * 4;
            write_member(talkdir, talk_at++, b);
        }
        {
            blob b;

            b.p = (u8 *)kFollowerMode;
            b.len = MMO_FOLLOWFILL_HEADERS;
            write_member(talkdir, talk_at++, b);
        }
    }

    write_bytes(gen, "billboard_gfx.txt", (const u8 *)gfx_txt, (u32)gfx_len);
    snprintf(line, sizeof line, "v1 %016llx\n",
             (unsigned long long)COOK_FNV_OFFSET);
    write_text(cooked, "digest", line);
    write_text(pkg, "mod.toml",
               "id = \"followers\"\n"
               "name = \"Follower Pokemon\"\n"
               "version = \"1.0.0\"\n"
               "authors = [\"openmmo\"]\n"
               "requires = []\n"
               "load_after = []\n");
    /* The stamp names the bases this fill used, so the package says what it
     * was allocated against and `ensure` can tell a load order that has since
     * changed from one that has not. */
    snprintf(line, sizeof line, "%s %u %u\n", FOLLOWCOMPOSE_STAMP,
             first_member, emote_first);

    /* Published in one move. The stamp is written last, after the swap, so a
     * machine that dies between the two comes back with no stamp and refills
     * rather than with a stamp over a half-swapped package. */
    {
        char live[1024];
        char old_stamp[1024];

        /* The old stamp goes FIRST. Between the remove below and the write at
         * the end there is a package with no cooked directory, and a stamp
         * left standing over that window is a machine that comes back trusting
         * a package that is not there. */
        path_join(old_stamp, sizeof old_stamp, pkg, "composed.txt");
        remove(old_stamp);
        path_join(live, sizeof live, pkg, ".cooked");
        rm_tree(live);
        if (rename(cooked, live) != 0)
            die("filled, but cannot move the package into place at %s", pkg);
    }
    write_text(pkg, "composed.txt", line);

    free(sizes);
    free(tps);
    free(gfx_txt);
    free(rom.p);
    return 0;
}

/* ---------------------------------------------------------------- ensure */
static int fc_dir_exists(const char *path)
{
    char probe[1024];
    FILE *f;

    if (path == NULL || path[0] == '\0')
        return 0;
    /* No stat() here on purpose: the one thing this has to answer is whether
     * the package is usable, and a directory with no mod.toml in it is not. */
    path_join(probe, sizeof probe, path, "mod.toml");
    f = fopen(probe, "rb");
    if (f == NULL)
        return 0;
    fclose(f);
    return 1;
}

/*
 * soundcompose's mods_root, and the same answer for the same reason: the folder the player
 * chose, or the install's own mods/ beside bin/.
 */

int mmo_followcompose_pkg(const mmo_launch_settings *s, const char *port_exe,
                          char *name, size_t namecap)
{
    char root[MMO_LAUNCH_PATH];
    char pkg[MMO_LAUNCH_PATH + 32];

    snprintf(name, namecap, "followers");
    mmo_launch_mods_root(s, port_exe, root, sizeof root);
    path_join(pkg, sizeof pkg, root, "followers");
    return fc_dir_exists(pkg);
}

/* The Play-time half. */
int mmo_followcompose_ensure(const mmo_launch_settings *s,
                             const char *port_exe,
                             void (*note)(void *ud, const char *line),
                             void *ud, char *err, size_t errcap)
{
    char root[MMO_LAUNCH_PATH];
    char pkg[MMO_LAUNCH_PATH + 32];
    char stamp[MMO_LAUNCH_PATH + 64];
    char hg_file[MMO_LAUNCH_PATH];
    char why[160];
    char have[128] = "";
    char want[128];
    char others[MMO_LAUNCH_TEXT + 256];
    int mmodel_first, emote_at;
    FILE *f;

    if (err != NULL && errcap > 0)
        err[0] = '\0';
    mmo_launch_mods_root(s, port_exe, root, sizeof root);
    path_join(pkg, sizeof pkg, root, "followers");
    path_join(stamp, sizeof stamp, pkg, "composed.txt");

    /*
     * Allocated against the packages this session will actually load, asked of the one place
     * that answers it, the `mods` row, or loadorder.txt when that row is empty.
     */
    mmo_launch_mods_list(s, root, others, sizeof others);
    mmodel_first = mmo_followcompose_base(root, others,
                                          "data/mmodel/mmodel.narc", 470,
                                          "followers");
    emote_at = mmo_followcompose_base(root, others,
                                      "data/mmodel/fldeff.narc", 201,
                                      "followers");

    /* The BASE is in the STAMP, so a player who adds a package that appends
     * to the same archive gets the follower package refilled around it rather
     * than a boot that dies on a collision or a hole. Refilling is a quarter
     * of a second; getting this wrong is a game that does not start. */
    snprintf(want, sizeof want, "%s %d %d\n", FOLLOWCOMPOSE_STAMP,
             mmodel_first, emote_at);
    f = fopen(stamp, "rb");
    if (f != NULL) {
        size_t n = fread(have, 1, sizeof have - 1, f);

        have[n] = '\0';
        fclose(f);
        if (strcmp(have, want) == 0 && fc_dir_exists(pkg))
            return 1;           /* filled by this build, at this base */
    }

    if (!mmo_sound_slot_status(s, 1, hg_file, sizeof hg_file,
                               why, sizeof why)) {
        snprintf(err, errcap, "a Pokemon walking behind you is drawn from your "
                 "own Heart Gold or Soul Silver cartridge (%s): choose one on "
                 "the settings face and it fills itself next time", why);
        return 0;
    }

    /* Composing, said as composing. */
    if (note != NULL)
        note(ud, "composing the Pokemon that walks behind you, out of your "
                 "own cartridge; this happens once...");
    if (mmo_followcompose(hg_file, pkg, mmodel_first, emote_at,
                          err, errcap) != 0)
        return -1;
    if (note != NULL)
        note(ud, "your Pokemon will walk behind you; it is kept for next time");
    return 1;
}
