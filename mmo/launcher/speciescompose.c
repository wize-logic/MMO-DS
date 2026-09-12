/* See the header. */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "speciescompose.h"

#include "launch_plan.h"
#include "soundcompose.h"   /* the cartridge slots, asked the same way */

#include "charcode.h"
#include "nitrorom.h"
#include "platform.h"
/* species_port.h before blackcompose.h: it pulls in mmo/include/mmo.h, which
 * spells u8..s32 off <stdint.h>, and blackcompose.h stands down when it has.
 * The other way round the app build warns five times over a typedef that says
 * the same thing twice. */
#include "species_port.h"       /* mmo/include: the four ids the engine spends */
#include "blackcompose.h"
#include "move_port.gen.h"      /* the frozen donor table, same */
#include "species_port.gen.h"   /* mmo/src, on the launcher include path */

/* u8, u16 and u32 come from blackcompose.h above (or from mmo.h where the app
 * build has already declared them); this file used to spell its own. */

/* The SHARED reader, under this FILE'S own NAMES, see nitrorom.h. */
typedef MmoBlob blob;
typedef MmoMembers members;
#define die         mmo_nitro_die
#define xmalloc     mmo_nitro_alloc
#define rd16        mmo_nitro_rd16
#define wr16        mmo_nitro_wr16
#define read_file   mmo_nitro_read_file
#define narc_members mmo_nitro_narc

/* ------------------------------------------------------------------ source */
/* The archives a Gen 5 image keeps these in. Names are stripped in an official
 * image, so each is an id; gen5_tables.py proved every one of them. */
#define SRC_PERSONAL   "a/0/1/6"
#define SRC_EVOLUTION  "a/0/1/9"
#define SRC_ICON       "a/0/0/7"
#define SRC_TEXT       "a/0/0/2"
#define SRC_TEXT_NAMES 70      /* the member holding species names */

/* Where they land, by this game's own paths. */
#define DST_PERSONAL   "poketool/personal/pl_personal.narc"
#define DST_LEARNSET   "poketool/personal/wotbl.narc"
#define DST_EVOLUTION  "poketool/personal/evo.narc"
#define DST_ICON       "poketool/icongra/pl_poke_icon.narc"
#define DST_MESSAGE    "msgdata/pl_msg.narc"
/* TEXT_BANK_SPECIES_NAME in the engine's generated/text_banks.h, and the
 * bank beside it: the same names with an article and a formatting tag. */
#define BANK_NAMES     412
#define BANK_ARTICLES  413

/* The icons are copied, one header byte aside. */
#define ICON_FIRST         7
#define ICON_BLACK_STRIDE  2
#define ICON_BYTES         1072
#define ICON_FLAG_OFFSET   34
#define ICON_FLAG_PLATINUM 0x00

/* ------------------------------------------------------------------ shapes */
/* Gen 5's personal entry, by the offsets gen5_tables.py proved. */
#define G5_TYPE1   6
#define G5_TYPE2   7
#define G5_CATCH   8
#define G5_EV      10
#define G5_ITEM1   12
#define G5_ITEM2   14
#define G5_ITEM3   16
#define G5_GENDER  18
#define G5_HATCH   19
#define G5_FRIEND  20
#define G5_RATE    21
#define G5_EGG1    22
#define G5_EGG2    23
#define G5_ABILITY1 24
#define G5_ABILITY2 25
#define G5_COLOR   33
#define G5_BASE_EXP 34
#define G5_MACHINES 0x28
#define G5_SIZE    60

/* Gen 4's SpeciesData, from the engine's include/struct_defs/species.h. The
 * colour byte carries the flip in bit 7 where Gen 5 carries it in bit 6. */
#define G4_SIZE     44
#define G4_MACHINES 0x1C
#define G4_MARSH    0x18   /* the Great Marsh flee rate, not carried */
#define G4_FLIP_BIT 0x80
#define G5_FLIP_BIT 0x40
#define G5_COLOR_MASK 0x3F

/* The EV yield word is six two-bit fields and nothing else. Gen 5 sets a bit
 * above them on two species (Diglett and Dugtrio); it is not an EV yield and
 * is masked off rather than carried into a field Gen 4 does not have. */
#define EV_MASK 0x0FFF
#define MAX_G4_BASE_EXP 255

/* Gen 5 deleted Gen 4's ??? type at 9, so everything from there moved down. */
#define GEN4_MYSTERY_TYPE 9
/* And inserted trade-for-a-species at 7, which this game has no number for. */
#define GEN5_TRADE_WITH_SPECIES 7

/* Seven rows of u16 method, u16 parameter, u16 target. Gen 5 stores the same
 * seven in 42 bytes and this game pads the member to 44; the two spare bytes
 * are zero in every one of this game's 508, which the Python's rebuild proves
 * rather than assumes. */
#define EVOLUTION_ROWS     7
#define EVOLUTION_ROW_SIZE 6
#define G4_EVO_SIZE        44
#define G5_EVO_SIZE        42

/*
 * The learnset archive pokemon.c indexes by species carries nothing a ported species needs,
 * the server owns a monster's moveset, but it is read on every seat, so an id past its end
 * is a read off the end rather than a missing feature.
 */
static const u8 EMPTY_LEARNSET[4] = { 0xFF, 0xFF, 0xFF, 0xFF };

/* ------------------------------------------------------------------ fields */
static int server_type(int gen5_type)
{
    return gen5_type < GEN4_MYSTERY_TYPE ? gen5_type : gen5_type + 1;
}

/* -1 for the one Gen 5 inserted: the server has no such method, and inventing
 * an id for it would be inventing the mechanic behind it too. */
static int server_method(int gen5_method)
{
    if (gen5_method < GEN5_TRADE_WITH_SPECIES)
        return gen5_method;
    if (gen5_method == GEN5_TRADE_WITH_SPECIES)
        return -1;
    return gen5_method - 1;
}

/*
 * A wire species as the engine numbers it. Two of the ids a fill appends are spent already,
 * SPECIES_EGG is 494 and SPECIES_BAD_EGG is 495, where Black puts Victini and Snivy, so
 * inside the engine those two live past Genesect.
 */
static int engine_species(int species)
{
    if (species == MMO_PORTED_EGG_ID)
        return MMO_PORTED_VICTINI_ENGINE_ID;
    if (species == MMO_PORTED_BAD_EGG_ID)
        return MMO_PORTED_SNIVY_ENGINE_ID;
    return species;
}

static int bit_of(const u8 *entry, int base, int index)
{
    return (entry[base + index / 8] >> (index % 8)) & 1;
}

/* This game's 16 machine bytes for one Gen 5 entry. */
static void machine_mask(const u8 *gen5, u8 *out)
{
    int mine;

    memset(out, 0, G4_SIZE - G4_MACHINES);
    for (mine = 0; mine < MMO_PORTED_MACHINES; mine++) {
        int theirs = MMO_PORTED_MACHINE_BIT[mine];

        if (theirs >= 0 && bit_of(gen5, G5_MACHINES, theirs))
            out[mine / 8] |= (u8)(1 << (mine % 8));
    }
}

void mmo_speciescompose_entry(const u8 *gen5, u8 *out, int machines)
{
    u16 rare;

    memset(out, 0, G4_SIZE);
    memcpy(out, gen5, 6);                       /* the six base stats */
    out[6] = (u8)server_type(gen5[G5_TYPE1]);
    out[7] = (u8)server_type(gen5[G5_TYPE2]);
    out[8] = gen5[G5_CATCH];
    {
        u16 exp = rd16(gen5 + G5_BASE_EXP);

        out[9] = (u8)(exp > MAX_G4_BASE_EXP ? MAX_G4_BASE_EXP : exp);
    }
    wr16(out + 10, (u16)(rd16(gen5 + G5_EV) & EV_MASK));
    wr16(out + 12, rd16(gen5 + G5_ITEM1));
    rare = rd16(gen5 + G5_ITEM2);
    if (rare == 0)
        rare = rd16(gen5 + G5_ITEM3);
    wr16(out + 14, rare);
    out[16] = gen5[G5_GENDER];
    out[17] = gen5[G5_HATCH];
    out[18] = gen5[G5_FRIEND];
    out[19] = gen5[G5_RATE];
    out[20] = gen5[G5_EGG1];
    out[21] = gen5[G5_EGG2];
    out[22] = gen5[G5_ABILITY1];
    out[23] = gen5[G5_ABILITY2];
    /* 0x18, the Great Marsh flee rate, stays zero: a ported species is not in
     * the Great Marsh and a rate there would be a claim about where it lives. */
    out[25] = (u8)((gen5[G5_COLOR] & G5_COLOR_MASK)
                   | ((gen5[G5_COLOR] & G5_FLIP_BIT) ? G4_FLIP_BIT : 0));
    if (machines)
        machine_mask(gen5, out + G4_MACHINES);
}

/* Rows keep their order and their parameter. */
void mmo_speciescompose_evolution(const u8 *gen5, u8 *out)
{
    int i, written = 0;

    memset(out, 0, G4_EVO_SIZE);
    for (i = 0; i < EVOLUTION_ROWS; i++) {
        const u8 *row = gen5 + i * EVOLUTION_ROW_SIZE;
        int method = rd16(row);
        int mapped;

        if (method == 0)
            continue;
        mapped = server_method(method);
        if (mapped < 0)
            continue;
        wr16(out + written * EVOLUTION_ROW_SIZE, (u16)mapped);
        wr16(out + written * EVOLUTION_ROW_SIZE + 2, rd16(row + 2));
        wr16(out + written * EVOLUTION_ROW_SIZE + 4,
             (u16)engine_species(rd16(row + 4)));
        written++;
    }
}

/* -------------------------------------------------------------- name bank */
/* Two MESSAGE formats, one read and one written. */
#define MSG_TABLE_KEY   0x2FD
#define MSG_STRING_KEY  0x91BD3
#define MSG_STRING_STEP 0x493D
#define MSG_TERMINATOR  0xFFFF
#define MSG_TAG         0xFFFE   /* Gen 5's newline; this game's format tag */

typedef struct { u16 *c; u32 n; } msg;          /* one entry, terminator kept */
typedef struct { msg *e; u32 n; u16 seed; } bank;

/* Three bits a character, not one. The key rotates left three per character
 * as a Gen 5 string is read, so recovering it from the terminator rotates
 * right three per character back to the first. */
static u16 ror16(u16 v) { return (u16)((v >> 3) | (v << 13)); }
static u16 rol16(u16 v) { return (u16)((v << 3) | (v >> 13)); }

/* This game's bank, decoded. */
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
        u32 off = mmo_nitro_rd32(b.p + 4 + i * 8) ^ mask;
        u32 len = mmo_nitro_rd32(b.p + 4 + i * 8 + 4) ^ mask;
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

/* And back. Held by rewriting one unchanged and requiring the same bytes,
 * which is the whole licence to write a longer one. */
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

        mmo_nitro_wr32(out.p + 4 + i * 8, at ^ mask);
        mmo_nitro_wr32(out.p + 4 + i * 8 + 4, bk->e[i].n ^ mask);
        for (j = 0; j < bk->e[i].n; j++) {
            wr16(out.p + at, (u16)(bk->e[i].c[j] ^ ck));
            ck = (u16)(ck + MSG_STRING_STEP);
            at += 2;
        }
    }
    return out;
}

/*
 * The cartridge's first text block, decrypted. Header is u16 blocks, u16 entries, u32 size,
 * u32 spare, then a u32 offset per block; a block is a u32 size, an entry table of u32 offset,
 * u16 length, u16 flags, then characters.
 */
static bank g5_text(blob m, const char *what)
{
    bank out;
    u32 base, size, i;
    u16 blocks;

    if (m.len < 16)
        die("%s is not a message file", what);
    blocks = rd16(m.p);
    out.n = rd16(m.p + 2);
    out.seed = 0;
    size = mmo_nitro_rd32(m.p + 4);
    if (blocks == 0 || blocks > 64 || out.n == 0 || out.n > 30000)
        die("%s is not a message file", what);
    if (size != m.len && size != m.len - 16)
        die("%s is not a message file", what);
    base = mmo_nitro_rd32(m.p + 12);
    out.e = xmalloc((size_t)out.n * sizeof *out.e);
    for (i = 0; i < out.n; i++) {
        u32 head = base + 4 + i * 8, off, start;
        u16 count, key;
        u32 j;

        if ((u64)head + 8 > m.len)
            die("%s: entry %u is past the end", what, i);
        off = mmo_nitro_rd32(m.p + head);
        count = rd16(m.p + head + 4);
        start = base + off;
        if (count == 0 || (u64)start + (u64)count * 2 > m.len)
            die("%s: entry %u is past the end", what, i);
        /* The key at the last character is the terminator xor what is there,
         * rotated back once per character to reach the first. */
        key = (u16)(rd16(m.p + start + (count - 1) * 2) ^ MSG_TERMINATOR);
        for (j = 0; j + 1 < count; j++)
            key = ror16(key);
        out.e[i].n = count;
        out.e[i].c = xmalloc((size_t)count * sizeof *out.e[i].c);
        for (j = 0; j < count; j++) {
            out.e[i].c[j] = (u16)(rd16(m.p + start + j * 2) ^ key);
            key = rol16(key);
        }
        if (out.e[i].c[count - 1] != MSG_TERMINATOR)
            die("%s: entry %u does not end where a string ends, so it was read "
                "wrong", what, i);
    }
    return out;
}

static void bank_free(bank *bk)
{
    u32 i;

    for (i = 0; i < bk->n; i++)
        free(bk->e[i].c);
    free(bk->e);
    bk->e = NULL;
    bk->n = 0;
}

/* ------------------------------------------------------------------- write */
static void mkdir_p(const char *path)
{
    char tmp[1024];
    size_t i;

    snprintf(tmp, sizeof tmp, "%s", path);
    for (i = 1; tmp[i] != '\0'; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mmo_plat_mkdir(tmp);
            tmp[i] = '/';
        }
    }
    mmo_plat_mkdir(tmp);
}

/* <pkg>/narc/<archive>/, made once and handed back, the way the porter lays a
 * package out (write_member in portspecies.py). */
static void narc_dir(char *out, size_t cap, const char *pkg,
                     const char *archive)
{
    snprintf(out, cap, "%s/narc/%s", pkg, archive);
    mkdir_p(out);
}

static void write_member(const char *dir, int index, const u8 *p, u32 n)
{
    char path[1088];
    FILE *f;

    snprintf(path, sizeof path, "%s/%d", dir, index);
    f = fopen(path, "wb");
    if (f == NULL)
        die("cannot write %s", path);
    if (n > 0 && fwrite(p, 1, n, f) != n) {
        fclose(f);
        die("cannot write %s", path);
    }
    fclose(f);
}

/* --------------------------------------------------------------- the loop */
/*
 * Black's sprite block: twenty members a species, the front half at +0 and the back at +9, the
 * two palettes last.
 */
#define SRC_SPRITE     "a/0/0/4"
#define BLK_SIZE       20
#define BLK_FRONT      0
#define BLK_BACK       9
#define BLK_CHARMAP    2
#define BLK_FEMALE_MAP 3
#define BLK_CELLS      4
#define BLK_CELL_ANIM  5
#define BLK_MULTI      6
#define BLK_MULTI_ANIM 7

#define DST_ANIM       "poketool/pokegra/mmo_anim.narc"
#define ANIM_STRIDE    12

/* The engine reads these species' sprites out of pl_otherpoke instead, and
 * paints Spinda's spots at Platinum's own coordinates, so they keep Platinum's
 * art and get an empty loop rather than one that would never be drawn. */
static const int KEEP_PLATINUM[] = {
    201, 327, 351, 386, 412, 413, 421, 422, 423, 479, 487, 492, 493
};

static int keeps_platinum(int species)
{
    size_t i;

    for (i = 0; i < sizeof KEEP_PLATINUM / sizeof KEEP_PLATINUM[0]; i++) {
        if (KEEP_PLATINUM[i] == species)
            return 1;
    }
    return 0;
}

/*
 * The extended LZ the engine port calls MI_UncompressLZ8 (pc/src/pc_mi.c): header byte 0x11,
 * the size in the next three, then flag-led runs whose length field widens with its top
 * nibble.
 */
static blob lz11(blob in, int species)
{
    blob out;
    u32 size, pos = 4, at = 0;

    if (in.len == 0 || in.p[0] != 0x11)
        return mmo_nitro_dup(in.p, in.len);
    if (in.len < 4)
        die("species %d has a compressed member too short to have a header",
            species);
    size = (mmo_nitro_rd32(in.p) >> 8);
    out.p = xmalloc(size ? size : 1);
    out.len = size;
    while (at < size) {
        u8 flags;
        int i;

        if (pos >= in.len)
            die("species %d's compressed member ends mid-run", species);
        flags = in.p[pos++];
        for (i = 0; i < 8 && at < size; i++) {
            u32 length, back, k;

            if (!(flags & (0x80 >> i))) {
                if (pos >= in.len)
                    die("species %d's compressed member ends mid-literal",
                        species);
                out.p[at++] = in.p[pos++];
                continue;
            }
            if (pos + 1 >= in.len)
                die("species %d's compressed member ends mid-reference",
                    species);
            {
                u8 top = (u8)(in.p[pos] >> 4);

                if (top == 1) {
                    if (pos + 3 >= in.len)
                        die("species %d: a long run runs past the member",
                            species);
                    length = ((u32)(in.p[pos] & 0xF) << 12
                              | (u32)in.p[pos + 1] << 4
                              | (u32)(in.p[pos + 2] >> 4)) + 0x111;
                    back = ((u32)(in.p[pos + 2] & 0xF) << 8
                            | in.p[pos + 3]) + 1;
                    pos += 4;
                } else if (top == 0) {
                    if (pos + 2 >= in.len)
                        die("species %d: a run runs past the member", species);
                    length = ((u32)(in.p[pos] & 0xF) << 4
                              | (u32)(in.p[pos + 1] >> 4)) + 0x11;
                    back = ((u32)(in.p[pos + 1] & 0xF) << 8
                            | in.p[pos + 2]) + 1;
                    pos += 3;
                } else {
                    length = (u32)top + 1;
                    back = ((u32)(in.p[pos] & 0xF) << 8 | in.p[pos + 1]) + 1;
                    pos += 2;
                }
            }
            if (back > at)
                die("species %d: a run reaches before the start of the member",
                    species);
            for (k = 0; k < length && at < size; k++) {
                out.p[at] = out.p[at - back];
                at++;
            }
        }
    }
    if (at != size)
        die("species %d: a compressed member declares %u bytes and yields %u",
            species, size, at);
    return out;
}

int mmo_speciescompose_anim(const char *bw_rom, const char *pkg,
                            void (*note)(void *ud, const char *line),
                            void *ud, char *err, size_t errcap)
{
    blob rom, sprite_arc;
    members sprites;
    char dir[1088];
    int species;
    jmp_buf jb;

    if (err != NULL && errcap > 0)
        err[0] = '\0';
    if (setjmp(jb) != 0)
        return -1;
    mmo_nitro_catch(&jb, err, errcap);

    if (bw_rom == NULL || bw_rom[0] == '\0')
        die("that fill needs a Black or White cartridge");
    if (note != NULL)
        note(ud, "Carrying the animation loops out of your cartridge...");
    rom = read_file(bw_rom);
    sprite_arc = mmo_nitro_file(rom, SRC_SPRITE, "that file",
                                "it is not a Black or White cartridge");
    sprites = narc_members(sprite_arc, SRC_SPRITE);
    if (sprites.n < (u32)(BLK_SIZE * (MMO_PORTED_LAST + 1)))
        die("that cartridge's sprite archive holds %u members, and this fill "
            "needs %d", sprites.n, BLK_SIZE * (MMO_PORTED_LAST + 1));

    narc_dir(dir, sizeof dir, pkg, DST_ANIM);

    for (species = 1; species <= MMO_PORTED_LAST; species++) {
        int engine = engine_species(species);
        int half, k;

        /*
         * No hole in the archive. The eggs, the species that keep Platinum's art and species 0
         * get empty members, which the plugin reads as "no loop here", an absent member
         * would be a read off the end instead.
         */
        if (keeps_platinum(species)) {
            for (k = 0; k < ANIM_STRIDE; k++)
                write_member(dir, species * ANIM_STRIDE + k, NULL, 0);
            continue;
        }
        for (half = 0; half < 2; half++) {
            int base = half == 0 ? BLK_FRONT : BLK_BACK;
            int at = half == 0 ? 0 : 6;
            const blob *b = &sprites.m[BLK_SIZE * species];
            blob female = b[base + BLK_FEMALE_MAP];
            blob got[6];
            int j;

            got[0] = lz11(b[base + BLK_CHARMAP], species);
            got[1] = female.len > 0 ? lz11(female, species)
                                    : mmo_nitro_dup(NULL, 0);
            got[2] = mmo_nitro_dup(b[base + BLK_CELLS].p,
                                   b[base + BLK_CELLS].len);
            got[3] = lz11(b[base + BLK_CELL_ANIM], species);
            got[4] = mmo_nitro_dup(b[base + BLK_MULTI].p,
                                   b[base + BLK_MULTI].len);
            got[5] = lz11(b[base + BLK_MULTI_ANIM], species);
            for (j = 0; j < 6; j++) {
                write_member(dir, engine * ANIM_STRIDE + at + j,
                             got[j].p, got[j].len);
                free(got[j].p);
            }
        }
    }
    /* And the two ids the engine spends on eggs, plus species 0. */
    {
        int empties[3];
        int i;

        empties[0] = MMO_PORTED_EGG_ID;
        empties[1] = MMO_PORTED_BAD_EGG_ID;
        empties[2] = 0;
        for (i = 0; i < 3; i++) {
            int k;

            for (k = 0; k < ANIM_STRIDE; k++)
                write_member(dir, empties[i] * ANIM_STRIDE + k, NULL, 0);
        }
    }

    free(rom.p);
    free(sprite_arc.p);
    free(sprites.m);
    mmo_nitro_catch(NULL, NULL, 0);
    return 0;
}


/* ------------------------------------------------------------------ sheets */
/* The BATTLE art, assembled rather than carried. */
#define PT_POKEGRA      "poketool/pokegra/pl_pokegra.narc"
#define PT_HEIGHT       "poketool/pokegra/height.narc"
#define DST_POKEGRA     PT_POKEGRA
#define DST_HEIGHT      PT_HEIGHT
#define BLK_PAL_NORMAL  18
#define BLK_PAL_SHINY   19
#define BLK_FEMALE_STILL 1
#define POKEGRA_MEMBERS 2964     /* this game's own, and both archives end at 494 */
#define HEIGHT_MEMBERS  1976
#define POKEGRA_STRIDE  6
#define HEIGHT_STRIDE   4
#define SHEET_FRAME     80       /* BC_FRAME, said again where the bytes are laid out */
#define SHEET_W         160
#define SHEET_H         80
#define SHEET_BYTES     6400     /* SHEET_W * SHEET_H / 2 */
#define NCGR_HEADER     0x30
#define PALETTE_BYTES   72
#define MAX_FRAMES      8        /* official bakes about six a face */
#define MAX_CANDIDATES  48       /* keyframes composed before the eight are chosen */
#define SCRAMBLE_A      1103515245u
#define SCRAMBLE_C      24691u
#define SCRAMBLE_SEED   0x1234
#define STRIP_MAGIC     "MMOA"

/* One composed keyframe, kept only to notice the next one drawing the same
 * thing. The canvas is 320x320 and a species covers a hundredth of it, so this
 * holds the box compose() reported and nothing outside it. */
typedef struct {
    int minx, miny, maxx, maxy;
    u8 *px;                      /* (maxx-minx+1) * (maxy-miny+1), row-major */
} picture;

static int picture_same(const picture *a, const picture *b)
{
    u32 n;

    if (a->px == NULL || b->px == NULL)
        return 0;
    if (a->minx != b->minx || a->miny != b->miny
        || a->maxx != b->maxx || a->maxy != b->maxy)
        return 0;
    n = (u32)(a->maxx - a->minx + 1) * (u32)(a->maxy - a->miny + 1);
    return memcmp(a->px, b->px, n) == 0;
}

/* What compose() just drew, out of the shared canvas and into `out`. */
static void picture_take(picture *out)
{
    int y;

    free(out->px);
    out->px = NULL;
    out->minx = mmo_black_minx;
    out->miny = mmo_black_miny;
    out->maxx = mmo_black_maxx;
    out->maxy = mmo_black_maxy;
    if (out->maxx < out->minx || out->maxy < out->miny) {
        out->minx = out->miny = 0;
        out->maxx = out->maxy = -1;
        return;
    }
    {
        u32 w = (u32)(out->maxx - out->minx + 1);
        u32 h = (u32)(out->maxy - out->miny + 1);

        out->px = xmalloc((size_t)w * h);
        for (y = 0; y < (int)h; y++)
            memcpy(out->px + (size_t)y * w,
                   mmo_black_canvas + (size_t)(out->miny + y) * CANVAS + out->minx,
                   w);
    }
}

/*
 * The SCRAMBLE, which is the engine's own build tool's (tools/nitrogfx/gfx.c Encode, and
 * tools/rescramble.py which measured which direction this game uses).
 */
static void scramble(u8 *p, u32 len, u16 seed)
{
    u32 words = len / 2, i;
    u32 s = seed;

    if (words == 0)
        return;
    wr16(p, (u16)s);
    for (i = 1; i < words; i++) {
        s = (s * SCRAMBLE_A + SCRAMBLE_C) & 0xFFFFFFFFu;
        wr16(p + i * 2, (u16)(rd16(p + i * 2) ^ (u16)s));
    }
}

static void unscramble(u8 *p, u32 len)
{
    u32 words = len / 2, i;
    u32 s;

    if (words == 0)
        return;
    s = rd16(p);
    for (i = 0; i < words; i++) {
        wr16(p + i * 2, (u16)(rd16(p + i * 2) ^ (u16)s));
        s = (s * SCRAMBLE_A + SCRAMBLE_C) & 0xFFFFFFFFu;
    }
}

/* Platinum's own sheet container, `pairs` pairs deep with `extra` trailer
 * bytes counted into both sizes. Held against a member the cartridge ships
 * before anything is written. */
static void ncgr_header(u8 *h, int pairs, u32 extra)
{
    u32 data = (u32)SHEET_BYTES * pairs;

    memcpy(h, "RGCN", 4);
    wr16(h + 4, 0xFEFF);
    wr16(h + 6, 0x0100);
    mmo_nitro_wr32(h + 8, NCGR_HEADER + data + extra);
    wr16(h + 12, 0x10);
    wr16(h + 14, 1);
    memcpy(h + 16, "RAHC", 4);
    mmo_nitro_wr32(h + 20, 0x20 + data + extra);
    wr16(h + 24, (u16)(SHEET_H / 8 * pairs));
    wr16(h + 26, SHEET_W / 8);
    mmo_nitro_wr32(h + 28, 3);          /* 4bpp */
    mmo_nitro_wr32(h + 32, 0);
    mmo_nitro_wr32(h + 36, 1);          /* linear, not tiled */
    mmo_nitro_wr32(h + 40, data);
    mmo_nitro_wr32(h + 44, 0x18);
}

/* Blank rows under one frame of a decoded sheet, the height byte's rule,
 * which is how this game's own bytes were measured. */
static int blank_below(const u8 *plain, int frame)
{
    int row, x;

    for (row = SHEET_FRAME - 1; row >= 0; row--) {
        u32 base = (u32)row * (SHEET_W / 2);

        for (x = frame * SHEET_FRAME; x < frame * SHEET_FRAME + SHEET_FRAME; x++) {
            u8 b = plain[base + x / 2];

            if ((x % 2) == 0 ? (b & 0xF) : (b >> 4))
                return SHEET_FRAME - 1 - row;
        }
    }
    return SHEET_FRAME;
}

/*
 * The licence to write A SHEET is that this game's own come back through the
 * same reader and writer unchanged, and that their height bytes are the blank
 * rows under them. Both are proven on the player's own Platinum before a byte
 * of theirs is composed, the same shape of oracle as the name bank's.
 */
static void check_sheets(members *pokegra, members *height)
{
    int species, k, agree = 0;
    u8 want[NCGR_HEADER];

    if (pokegra->n != POKEGRA_MEMBERS || height->n != HEIGHT_MEMBERS)
        die("your Platinum holds %u sprite members and %u height bytes, and "
            "this fill is built on %d and %d", pokegra->n, height->n,
            POKEGRA_MEMBERS, HEIGHT_MEMBERS);
    ncgr_header(want, 1, 0);
    if (pokegra->m[3].len < NCGR_HEADER
        || memcmp(pokegra->m[3].p, want, NCGR_HEADER) != 0)
        die("the sheet container this writes is not the one your Platinum "
            "ships");
    for (species = 1; species < MMO_PORTED_FIRST; species++) {
        for (k = 1; k <= 3; k += 2) {
            blob m = pokegra->m[species * POKEGRA_STRIDE + k];
            blob b = height->m[species * HEIGHT_STRIDE + k];
            u8 *plain;
            u16 seed;

            if (m.len != NCGR_HEADER + SHEET_BYTES)
                continue;
            plain = xmalloc(SHEET_BYTES);
            memcpy(plain, m.p + NCGR_HEADER, SHEET_BYTES);
            seed = rd16(plain);
            unscramble(plain, SHEET_BYTES);
            {
                u8 *again = xmalloc(SHEET_BYTES);

                memcpy(again, plain, SHEET_BYTES);
                scramble(again, SHEET_BYTES, seed);
                if (memcmp(again, m.p + NCGR_HEADER, SHEET_BYTES) != 0)
                    die("species %d's sheet does not re-encode to itself, so "
                        "this cannot be trusted to write one", species);
                free(again);
            }
            if (b.len < 1 || b.p[0] != (u8)blank_below(plain, 0))
                die("species %d's height byte is %u and its sheet has %d blank "
                    "rows under it, so the byte this writes would be wrong",
                    species, b.len ? b.p[0] : 0, blank_below(plain, 0));
            agree++;
            free(plain);
        }
    }
    if (agree < 900)
        die("only %d of your Platinum's own sheets could be held to their "
            "height bytes", agree);
}

/* One face's banks out of a species' block, ready to compose. */
static void face_load(struct face *f, const blob *blk, int half, int female,
                      int species)
{
    blob map, cells, canim, multi, manim;
    const u8 *pal;
    int i;

    memset(f, 0, sizeof *f);
    f->character = -1;
    map = lz11(blk[half + (female ? BLK_FEMALE_MAP : BLK_CHARMAP)], species);
    cells = mmo_nitro_dup(blk[half + BLK_CELLS].p, blk[half + BLK_CELLS].len);
    canim = lz11(blk[half + BLK_CELL_ANIM], species);
    multi = mmo_nitro_dup(blk[half + BLK_MULTI].p, blk[half + BLK_MULTI].len);
    manim = lz11(blk[half + BLK_MULTI_ANIM], species);
    if (!mmo_black_read_charmap(f, map.p, map.len)
        || !mmo_black_read_cells(f, cells.p, cells.len)
        || !mmo_black_read_anims(canim.p, canim.len, &f->cseqs, &f->ncseqs,
                                 &f->cframes, &f->ncframes)
        || !mmo_black_read_multicells(f, multi.p, multi.len)
        || !mmo_black_read_anims(manim.p, manim.len, &f->mseqs, &f->nmseqs,
                                 &f->mframes, &f->nmframes))
        die("species %d's %s sprite is not one this fill can read",
            species, half == BLK_FRONT ? "front" : "back");
    free(map.p);
    free(cells.p);
    free(canim.p);
    free(multi.p);
    free(manim.p);
    if (blk[BLK_PAL_NORMAL].len < PALETTE_BYTES)
        die("species %d has no palette on that cartridge", species);
    pal = blk[BLK_PAL_NORMAL].p;
    for (i = 0; i < 16; i++)
        f->pal[i] = (u16)(rd16(pal + 40 + i * 2) & 0x7FFF);
    f->back = half == BLK_BACK;
    mmo_black_marks(f);
}

static void face_free(struct face *f)
{
    free(f->map);
    free(f->oams);
    free(f->cells);
    free(f->cframes);
    free(f->cseqs);
    free(f->nodes);
    free(f->mcs);
    free(f->mframes);
    free(f->mseqs);
    free(f->change);
    memset(f, 0, sizeof *f);
}

/* Which moments of the loop to KEEP. */
static u32 choose_ticks(struct face *f, u32 *out)
{
    u32 *cand, ncand = 0, n = 0, t, i;
    picture prev, cur;

    memset(&prev, 0, sizeof prev);
    memset(&cur, 0, sizeof cur);
    prev.maxx = cur.maxx = -1;

    for (t = 0; t < f->period; t++)
        ncand += f->change ? f->change[t] : t == 0;
    if (ncand == 0) {
        out[0] = 0;
        return 1;
    }
    cand = xmalloc((size_t)ncand * sizeof *cand);
    ncand = 0;
    for (t = 0; t < f->period; t++) {
        if (f->change ? f->change[t] : t == 0)
            cand[ncand++] = t;
    }
    if (ncand > MAX_CANDIDATES) {
        u32 *thin = xmalloc((size_t)MAX_CANDIDATES * sizeof *thin);
        double step = (double)ncand / MAX_CANDIDATES;
        u32 k = 1;

        thin[0] = cand[0];
        for (i = 1; i < MAX_CANDIDATES; i++) {
            u32 at = cand[(u32)(i * step)];

            if (at != thin[k - 1])          /* sorted already, so this is the set */
                thin[k++] = at;
        }
        free(cand);
        cand = thin;
        ncand = k;
    }
    for (i = 0; i < ncand; i++) {
        mmo_black_compose(f, cand[i]);
        picture_take(&cur);
        if (n > 0 && picture_same(&prev, &cur))
            continue;
        out[n++] = cand[i];
        free(prev.px);
        prev = cur;
        memset(&cur, 0, sizeof cur);
        cur.maxx = -1;
    }
    free(prev.px);
    free(cur.px);
    free(cand);
    if (n == 0) {
        out[0] = 0;
        return 1;
    }
    if (n > MAX_FRAMES) {
        u32 keep[MAX_FRAMES];
        u8 taken[MAX_CANDIDATES];
        u32 k, kept = 1;

        memset(taken, 0, sizeof taken);
        keep[0] = out[0];
        taken[0] = 1;
        for (k = 1; k < MAX_FRAMES; k++) {
            double target = (double)f->period * k / MAX_FRAMES, bd = 0.0;
            u32 best = 0;
            int have = 0;

            for (i = 1; i < n; i++) {
                double d = (double)out[i] - target;

                if (taken[i])
                    continue;
                if (d < 0.0)
                    d = -d;
                if (!have || d < bd) {      /* the first minimum wins, as Python's min does */
                    have = 1;
                    bd = d;
                    best = i;
                }
            }
            if (!have)
                break;
            taken[best] = 1;
            keep[kept++] = out[best];
        }
        for (i = 0; i + 1 < kept; i++) {    /* back into tick order */
            u32 j, at = i;

            for (j = i + 1; j < kept; j++) {
                if (keep[j] < keep[at])
                    at = j;
            }
            if (at != i) {
                u32 swap = keep[i];

                keep[i] = keep[at];
                keep[at] = swap;
            }
        }
        for (i = 0; i < kept; i++)
            out[i] = keep[i];
        n = kept;
    }
    return n;
}

/* The placement, and then the frames in it. */
static int bake_face(struct face *f, const u32 *ticks, u32 n, u8 *frames)
{
    u32 i;

    mmo_black_box(f, ticks, n);
    f->hasByte = 0;
    f->byte = mmo_black_height_byte(f);
    f->hasByte = 1;
    mmo_black_fit(f, SHEET_FRAME, SHEET_FRAME);
    for (i = 0; i < n; i++) {
        mmo_black_compose(f, ticks[i]);
        mmo_black_frame(f, frames + (size_t)i * SHEET_FRAME * SHEET_FRAME);
    }
    /* Front to back, the first word of a sheet is the seed and decodes to
     * background whatever it held: the engine could never show those four
     * pixels and this game's own sheets never use them. */
    memset(frames, 0, 4);
    return f->byte;
}

/* The frames as one member: pairs of 80x80 stacked, this game's own container
 * around them, its scramble over that, and the trailer the plugin reads. */
static blob sheet_member(const u8 *frames, u32 n, const u8 *durations, u16 seed)
{
    u32 pairs = (n + 1) / 2, data = SHEET_BYTES * pairs, tail = 0;
    blob out;
    u32 k, y, x;
    u8 *px;

    if (n > 1)
        tail = (6 + n + 3) & ~3u;
    out.len = NCGR_HEADER + data + tail;
    out.p = xmalloc(out.len);
    memset(out.p, 0, out.len);
    ncgr_header(out.p, (int)pairs, tail);
    px = out.p + NCGR_HEADER;
    for (k = 0; k < n; k++) {
        u32 left = (k % 2) * SHEET_FRAME, top = (k / 2) * SHEET_FRAME;
        const u8 *frame = frames + (size_t)k * SHEET_FRAME * SHEET_FRAME;

        for (y = 0; y < SHEET_FRAME; y++) {
            for (x = 0; x < SHEET_FRAME; x++) {
                u8 v = frame[y * SHEET_FRAME + x];
                u32 X = left + x;
                u32 at = (top + y) * (SHEET_W / 2) + X / 2;

                px[at] |= (u8)((X % 2) == 0 ? (v & 0xF) : (v << 4));
            }
        }
    }
    if (n % 2) {
        /* An odd last frame repeats itself on the right, so the engine's own
         * frame flip shows the same picture rather than an empty one. */
        u32 top = ((n - 1) / 2) * SHEET_FRAME;
        const u8 *frame = frames + (size_t)(n - 1) * SHEET_FRAME * SHEET_FRAME;

        for (y = 0; y < SHEET_FRAME; y++) {
            for (x = 0; x < SHEET_FRAME; x++) {
                u8 v = frame[y * SHEET_FRAME + x];
                u32 X = SHEET_FRAME + x;
                u32 at = (top + y) * (SHEET_W / 2) + X / 2;

                px[at] |= (u8)((X % 2) == 0 ? (v & 0xF) : (v << 4));
            }
        }
    }
    scramble(px, data, seed);
    if (tail > 0) {
        u8 *t = px + data;

        memcpy(t, STRIP_MAGIC, 4);
        wr16(t + 4, (u16)n);
        memcpy(t + 6, durations, n);
    }
    return out;
}

int mmo_speciescompose_sheets(const char *bw_rom, const char *pt_rom,
                              const char *pkg,
                              void (*note)(void *ud, const char *line),
                              void *ud, char *err, size_t errcap)
{
    blob rom, pt, sprite_arc, pokegra_arc, height_arc;
    members sprites, pokegra, height;
    char dgra[1088], dhgt[1088], line[192];
    int species, baked = 0, scaled = 0;
    jmp_buf jb;

    if (err != NULL && errcap > 0)
        err[0] = '\0';
    if (setjmp(jb) != 0)
        return -1;
    mmo_nitro_catch(&jb, err, errcap);

    if (bw_rom == NULL || bw_rom[0] == '\0')
        die("that fill needs a Black or White cartridge");
    if (pt_rom == NULL || pt_rom[0] == '\0')
        die("that fill needs your own Platinum cartridge too: the sheet it "
            "writes is this game's own, and its writer is proven on yours");
    if (pkg == NULL || pkg[0] == '\0')
        die("that fill has no package to write into");

    if (note != NULL)
        note(ud, "Reading the battle sprites off your cartridge...");
    rom = read_file(bw_rom);
    sprite_arc = mmo_nitro_file(rom, SRC_SPRITE, "that file",
                                "it is not a Black or White cartridge");
    sprites = narc_members(sprite_arc, SRC_SPRITE);
    if (sprites.n < (u32)(BLK_SIZE * (MMO_PORTED_LAST + 1)))
        die("that cartridge's sprite archive holds %u members, and this fill "
            "needs %d", sprites.n, BLK_SIZE * (MMO_PORTED_LAST + 1));

    pt = read_file(pt_rom);
    pokegra_arc = mmo_nitro_file(pt, PT_POKEGRA, "that Platinum file",
                                 "it is not a Platinum cartridge");
    height_arc = mmo_nitro_file(pt, PT_HEIGHT, "that Platinum file",
                                "it is not a Platinum cartridge");
    pokegra = narc_members(pokegra_arc, PT_POKEGRA);
    height = narc_members(height_arc, PT_HEIGHT);
    check_sheets(&pokegra, &height);

    narc_dir(dgra, sizeof dgra, pkg, DST_POKEGRA);
    narc_dir(dhgt, sizeof dhgt, pkg, DST_HEIGHT);

    /*
     * Every SPECIES, not only the 156 a fill appends: Black redrew all 649 and the owner chose
     * Black's look for this client (2026-08-30), as the official client did.
     */
    for (species = 1; species <= MMO_PORTED_LAST; species++) {
        const blob *blk = &sprites.m[BLK_SIZE * species];
        int engine = engine_species(species);
        int has_female = blk[BLK_FEMALE_MAP].len > 0;
        int half, pass;

        if (keeps_platinum(species))
            continue;
        for (half = 0; half < 2; half++) {
            int base = half == 0 ? BLK_BACK : BLK_FRONT;
            int face = half == 0 ? 0 : 2;

            for (pass = 0; pass < 2; pass++) {
                int female = pass == 1, gender = female ? 0 : 1;
                struct face f;
                u32 ticks[MAX_CANDIDATES], n, i;
                u8 *frames, durations[MAX_FRAMES];
                blob member;
                int byte, g;

                if (female && !has_female)
                    continue;
                face_load(&f, blk, base, female, species);
                n = choose_ticks(&f, ticks);
                frames = xmalloc((size_t)n * SHEET_FRAME * SHEET_FRAME);
                byte = bake_face(&f, ticks, n, frames);
                for (i = 0; i < n; i++) {
                    u32 next = i + 1 < n ? ticks[i + 1] : f.period;
                    u32 held = next > ticks[i] ? next - ticks[i] : 1;

                    durations[i] = (u8)(held > 255 ? 255 : held);
                }
                if (f.factor < 1.0f)
                    scaled++;
                member = sheet_member(frames, n, durations,
                                      (u16)(SCRAMBLE_SEED + species * 4 + face
                                            + gender));
                /* A species with no female sprite holds the default picture in
                 * both gender slots, as Platinum does. */
                for (g = female ? 0 : 1; g >= 0; g--) {
                    write_member(dgra, engine * POKEGRA_STRIDE + face + g,
                                 member.p, member.len);
                    {
                        u8 one = (u8)byte;

                        write_member(dhgt, engine * HEIGHT_STRIDE + face + g,
                                     &one, 1);
                    }
                    if (female || has_female)
                        break;
                }
                free(member.p);
                free(frames);
                face_free(&f);
                baked++;
            }
        }
        write_member(dgra, engine * POKEGRA_STRIDE + 4,
                     blk[BLK_PAL_NORMAL].p, blk[BLK_PAL_NORMAL].len);
        write_member(dgra, engine * POKEGRA_STRIDE + 5,
                     blk[BLK_PAL_SHINY].p, blk[BLK_PAL_SHINY].len);
        if (note != NULL && species % 64 == 0) {
            snprintf(line, sizeof line, "Composing the battle sprites: %d of "
                     "%d species...", species, MMO_PORTED_LAST);
            note(ud, line);
        }
    }

    /*
     * No hole under the two IDS the engine spends on eggs. Victini and Snivy live past
     * Genesect inside the engine, so 494 and 495 address nothing, and an absent member is a
     * read off the end rather than an empty picture.
     */
    {
        int eggs[2], i, k;

        eggs[0] = MMO_PORTED_EGG_ID;
        eggs[1] = MMO_PORTED_BAD_EGG_ID;
        for (i = 0; i < 2; i++) {
            u8 zero = 0;

            for (k = 0; k < POKEGRA_STRIDE; k++)
                write_member(dgra, eggs[i] * POKEGRA_STRIDE + k, NULL, 0);
            for (k = 0; k < HEIGHT_STRIDE; k++)
                write_member(dhgt, eggs[i] * HEIGHT_STRIDE + k, &zero, 1);
        }
    }

    if (note != NULL) {
        snprintf(line, sizeof line, "%d battle sprites composed, %d scaled to "
                 "fit the frame", baked, scaled);
        note(ud, line);
    }

    free(rom.p);
    free(pt.p);
    free(sprite_arc.p);
    free(pokegra_arc.p);
    free(height_arc.p);
    free(sprites.m);
    free(pokegra.m);
    free(height.m);
    mmo_nitro_catch(NULL, NULL, 0);
    return 0;
}


/* ------------------------------------------------------------------- moves */
/* The 92 MOVES A gen 5 cartridge adds. */
#define SRC_MOVES          "a/0/2/1"
#define SRC_TEXT_MOVE_NAMES 203
#define SRC_TEXT_MOVE_DESCS 202
#define DST_MOVE_TABLE     "poketool/waza/pl_waza_tbl.narc"
#define DST_MOVE_ANIM      "wazaeffect/we.arc"
#define DST_MOVE_SEQ       "battle/skill/waza_seq.narc"
#define BANK_MOVE_DESCS    646
#define BANK_MOVE_NAMES    647
#define BANK_MOVE_UPPER    648
#define BANK_USED_IN_BATTLE  0   /* "{nickname} used\nMOVE!" and its two other forms */
#define USED_FORMS         3

/* This game's own counts. 471 and 501 are not 468: the table carries three
 * dummies past Shadow Force and the script archives thirty-three placeholders,
 * and the engine's MAX_MOVES is 468 either way. The fill writes over them. */
#define MOVE_TABLE_ROM_MEMBERS 471
#define MOVE_ANIM_ROM_MEMBERS  501
#define MOVE_SEQ_ROM_MEMBERS   501
#define MOVE_BANK_ENTRIES      468
#define MOVE_ENTRY_BYTES       16
#define MOVE_CLASS_STATUS      2
#define MOVE_EFFECTS           512   /* this game's stop at 276; the arrays are its ceiling */
#define EFFECT_HIT             0
#define EFFECT_SPLASH          85
#define LAST_GEN4_EFFECT       276
#define KINGS_ROCK             0x20
#define PRESENTATION           0xC0  /* hides HP gauges, hides shadows */
#define MOVE_TYPES             18
#define MOVE_CLASSES           3
#define GEN5_ALWAYS_HITS       101
#define LINE_CHARS             23    /* the longest line in this game's own 467 descriptions */
#define LINE_CHARS_LOOSE       25    /* allowed only when it saves a sixth line */
#define BOX_LINES              5
#define MOVE_NAME_CHARS        12    /* the longest name this game holds */
#define MSG_NEWLINE            0xE000
#define G5_APOSTROPHE          0x2019 /* the typographic one this game's banks use */

/* Gen 5's move row, by the offsets gen5_tables.py proved. A secondary effect's
 * chance is written in one of three places depending on what it does to the
 * target and never in more than one, so the chance is the largest of them. */
#define G5M_TYPE      0
#define G5M_CATEGORY  2
#define G5M_POWER     3
#define G5M_ACCURACY  4
#define G5M_PP        5
#define G5M_PRIORITY  6
#define G5M_CHANCE_A  10
#define G5M_CHANCE_B  15
#define G5M_CHANCE_C  27
#define G5M_EFFECT    16
#define G5M_RANGE     20
#define G5M_FLAGS     32
#define G5M_SIZE      36

/* Gen 5 physical/special/status, in this game's numbering. */
static const int GEN5_CLASS[3] = { 2, 0, 1 };
/* Gen 5's range byte to this game's RANGE_* bit, the majority over the shared
 * 467; the ten that disagree are gen5_tables.RANGE_CHANGED. */
static const int GEN5_RANGE[14] = {
    0, 512, 256, 1024, 8, 4, 32, 16, 64, 2, 64, 128, 32, 1
};
/* (Gen 5 bit, this game's bit): contact, protect, magic coat, snatch, mirror. */
static const int FLAG_BITS[5][2] = { { 0, 0 }, { 3, 1 }, { 4, 2 }, { 5, 3 }, { 6, 4 } };

typedef struct {
    int effect, cls, power, type, accuracy, pp, chance, range, priority, flags;
    int c_effect, c_type;
} move;

static void move_unpack(const u8 *m, move *out)
{
    out->effect = rd16(m);
    out->cls = m[2];
    out->power = m[3];
    out->type = m[4];
    out->accuracy = m[5];
    out->pp = m[6];
    out->chance = m[7];
    out->range = rd16(m + 8);
    out->priority = (signed char)m[10];
    out->flags = m[11];
    out->c_effect = m[12];
    out->c_type = m[13];
}

static void move_pack(const move *in, u8 *out)
{
    memset(out, 0, MOVE_ENTRY_BYTES);
    wr16(out, (u16)in->effect);
    out[2] = (u8)in->cls;
    out[3] = (u8)in->power;
    out[4] = (u8)in->type;
    out[5] = (u8)in->accuracy;
    out[6] = (u8)in->pp;
    out[7] = (u8)in->chance;
    wr16(out + 8, (u16)in->range);
    out[10] = (u8)in->priority;
    out[11] = (u8)in->flags;
    out[12] = (u8)in->c_effect;
    out[13] = (u8)in->c_type;
}

/* One Gen 5 row, decoded. */
static void move_gen5(const u8 *m, move *out)
{
    int chance = m[G5M_CHANCE_A];

    if (m[G5M_CHANCE_B] > chance)
        chance = m[G5M_CHANCE_B];
    if (m[G5M_CHANCE_C] > chance)
        chance = m[G5M_CHANCE_C];
    out->type = server_type(m[G5M_TYPE]);
    out->cls = GEN5_CLASS[m[G5M_CATEGORY] < 3 ? m[G5M_CATEGORY] : 0];
    out->power = m[G5M_POWER];
    out->accuracy = m[G5M_ACCURACY] == GEN5_ALWAYS_HITS ? 0 : m[G5M_ACCURACY];
    out->pp = m[G5M_PP];
    out->priority = (signed char)m[G5M_PRIORITY];
    out->chance = chance;
    out->effect = rd16(m + G5M_EFFECT);
    out->range = m[G5M_RANGE];
    out->flags = rd16(m + G5M_FLAGS);
    out->c_effect = 0;
    out->c_type = 0;
}

/*
 * The three answers no gen 5 row can give, measured off this game's own table on the player's
 * own cartridge: king's rock by effect, contest data by type and class, and the battle stub by
 * effect.
 */
typedef struct {
    move own[MOVE_BANK_ENTRIES];
    u8 kings[MOVE_EFFECTS];
    int contest[MOVE_TYPES][MOVE_CLASSES][2];
    int has_contest[MOVE_TYPES][MOVE_CLASSES];
    int contest_class[MOVE_CLASSES][2];
    int stub[MOVE_EFFECTS];          /* the move whose waza_seq member is that effect's */
    int stub_groups, stub_unanimous;
} rules;

static void rules_build(rules *r, members *table, members *seq)
{
    int m, e, t, c;

    memset(r, 0, sizeof *r);
    for (m = 1; m <= MMO_PORTED_MOVE_SHARED; m++)
        move_unpack(table->m[m].p, &r->own[m]);

    /* King's rock: the majority over the moves this game gives that effect. */
    for (e = 0; e < MOVE_EFFECTS; e++) {
        int yes = 0, all = 0;

        for (m = 1; m <= MMO_PORTED_MOVE_SHARED; m++) {
            if (r->own[m].effect != e)
                continue;
            all++;
            yes += (r->own[m].flags & KINGS_ROCK) != 0;
        }
        r->kings[e] = (u8)(all > 0 ? (yes * 2 > all) : 2);   /* 2 = nothing to go on */
    }

    /* Contest data: the commonest pair for a type and class, and for a class
     * alone where this game has no move of that type and class at all. */
    for (t = 0; t < MOVE_TYPES; t++) {
        for (c = 0; c < MOVE_CLASSES; c++) {
            int best = -1, bestn = 0;

            for (m = 1; m <= MMO_PORTED_MOVE_SHARED; m++) {
                int n = 0, k;

                if (r->own[m].type != t || r->own[m].cls != c)
                    continue;
                for (k = 1; k <= MMO_PORTED_MOVE_SHARED; k++) {
                    if (r->own[k].type == t && r->own[k].cls == c
                        && r->own[k].c_effect == r->own[m].c_effect
                        && r->own[k].c_type == r->own[m].c_type)
                        n++;
                }
                if (n > bestn) {
                    bestn = n;
                    best = m;
                }
            }
            if (best >= 0) {
                r->contest[t][c][0] = r->own[best].c_effect;
                r->contest[t][c][1] = r->own[best].c_type;
                r->has_contest[t][c] = 1;
            }
        }
    }
    for (c = 0; c < MOVE_CLASSES; c++) {
        int best = -1, bestn = 0;

        for (m = 1; m <= MMO_PORTED_MOVE_SHARED; m++) {
            int n = 0, k;

            if (r->own[m].cls != c)
                continue;
            for (k = 1; k <= MMO_PORTED_MOVE_SHARED; k++) {
                if (r->own[k].cls == c
                    && r->own[k].c_effect == r->own[m].c_effect
                    && r->own[k].c_type == r->own[m].c_type)
                    n++;
            }
            if (n > bestn) {
                bestn = n;
                best = m;
            }
        }
        if (best >= 0) {
            r->contest_class[c][0] = r->own[best].c_effect;
            r->contest_class[c][1] = r->own[best].c_type;
        }
    }

    /* The battle stub: the commonest waza_seq member among the moves sharing
     * an effect. 61 of the 64 effects two or more moves share have one stub. */
    for (e = 0; e < MOVE_EFFECTS; e++) {
        int best = -1, bestn = 0, all = 0, distinct = 0;

        r->stub[e] = -1;
        for (m = 1; m <= MMO_PORTED_MOVE_SHARED; m++) {
            int n = 0, k, first = 1;

            if (r->own[m].effect != e)
                continue;
            all++;
            for (k = 1; k <= MMO_PORTED_MOVE_SHARED; k++) {
                if (r->own[k].effect != e)
                    continue;
                if (seq->m[k].len == seq->m[m].len
                    && memcmp(seq->m[k].p, seq->m[m].p, seq->m[m].len) == 0) {
                    n++;
                    if (k < m)
                        first = 0;
                }
            }
            distinct += first;
            if (n > bestn) {
                bestn = n;
                best = m;
            }
        }
        r->stub[e] = best;
        if (all > 1) {
            r->stub_groups++;
            r->stub_unanimous += distinct == 1;
        }
    }
}

static int rules_kings(const rules *r, int effect, int cls)
{
    if (effect >= 0 && effect < MOVE_EFFECTS && r->kings[effect] != 2)
        return r->kings[effect];
    return cls != MOVE_CLASS_STATUS;
}

/* This game's 16-byte entry out of a Gen 5 row, with the three answers above. */
static void move_entry(const rules *r, const move *g5, int donor_flags, u8 *out)
{
    move e = *g5;
    int i;

    if (e.effect > LAST_GEN4_EFFECT)
        e.effect = e.cls != MOVE_CLASS_STATUS ? EFFECT_HIT : EFFECT_SPLASH;
    if (g5->range < 0 || g5->range >= (int)(sizeof GEN5_RANGE / sizeof GEN5_RANGE[0]))
        die("a move on that cartridge has range byte %d, which this fill has no "
            "row for", g5->range);
    e.range = GEN5_RANGE[g5->range];
    e.flags = 0;
    for (i = 0; i < 5; i++) {
        if (g5->flags & (1 << FLAG_BITS[i][0]))
            e.flags |= 1 << FLAG_BITS[i][1];
    }
    if (rules_kings(r, e.effect, e.cls))
        e.flags |= KINGS_ROCK;
    e.flags |= donor_flags & PRESENTATION;
    if (e.type >= 0 && e.type < MOVE_TYPES && e.cls >= 0 && e.cls < MOVE_CLASSES
        && r->has_contest[e.type][e.cls]) {
        e.c_effect = r->contest[e.type][e.cls][0];
        e.c_type = r->contest[e.type][e.cls][1];
    } else if (e.cls >= 0 && e.cls < MOVE_CLASSES) {
        e.c_effect = r->contest_class[e.cls][0];
        e.c_type = r->contest_class[e.cls][1];
    }
    move_pack(&e, out);
}

/* A gen 5 string, wrapped for this game'S BOX. */
static u32 g5_text_copy(const msg *from, u16 *text, u32 cap)
{
    u32 n = 0, i;

    for (i = 0; i + 1 < from->n && n < cap; i++) {
        u16 ch = from->c[i];

        if (ch == 0)
            continue;
        if (ch == MSG_TAG)              /* Gen 5's own newline */
            ch = ' ';
        if (ch == '\'')
            ch = G5_APOSTROPHE;
        text[n++] = ch;
    }
    return n;
}

/* Greedy, on words: the first word starts a line and a word that would take
 * the line past `width` starts the next one. `breaks` take the first character
 * of each line after the first; the count of lines comes back. */
static u32 g5_wrap(const u16 *text, u32 n, int width, u32 *breaks, u32 bcap)
{
    u32 lines = 1, at = 0, i, word_start = 0;
    int in_word = 0;

    for (i = 0; i <= n; i++) {
        int space = i == n || text[i] == ' ';

        if (!space) {
            if (!in_word) {
                in_word = 1;
                word_start = i;
            }
            continue;
        }
        if (!in_word)
            continue;
        in_word = 0;
        if (at == 0) {
            at = i - word_start;
        } else if (at + 1 + (i - word_start) <= (u32)width) {
            at += 1 + (i - word_start);
        } else {
            if (lines - 1 < bcap)
                breaks[lines - 1] = word_start;
            lines++;
            at = i - word_start;
        }
    }
    return lines;
}

/* The wrapped text as one bank entry: each line through the client's own
 * charmap, this game's newline between them, its terminator at the end. */
static void g5_encode_lines(const u16 *text, u32 n, const u32 *breaks,
                            u32 lines, msg *out, int move_id, int upper)
{
    mmo_charcode codes[512];
    u32 at = 0, line, i;
    u32 written = 0;
    u8 utf16[512 * 2];

    out->c = xmalloc((size_t)(n + lines + 1) * sizeof *out->c);
    for (line = 0; line < lines; line++) {
        u32 from = line == 0 ? 0 : breaks[line - 1];
        u32 to = line + 1 < lines ? breaks[line] : n;
        u32 len = 0;
        mmo_charcode_result r;

        while (to > from && text[to - 1] == ' ')
            to--;
        for (i = from; i < to && len < 511; i++) {
            u16 ch = text[i];

            if (upper && ch >= 'a' && ch <= 'z')
                ch = (u16)(ch - 'a' + 'A');
            utf16[len * 2] = (u8)ch;
            utf16[len * 2 + 1] = (u8)(ch >> 8);
            len++;
        }
        r = mmo_utf16le_to_charcode(utf16, len * 2, codes,
                                    sizeof codes / sizeof codes[0]);
        if (r.unmapped != 0)
            die("move %d uses %zu character(s) this game has no glyph for",
                move_id, r.unmapped);
        if (r.truncated)
            die("move %d's text does not fit this game's bank", move_id);
        for (i = 0; i < (u32)r.written; i++)
            out->c[at++] = codes[i];
        written += (u32)r.written;
        if (line + 1 < lines)
            out->c[at++] = MSG_NEWLINE;
    }
    out->c[at++] = MSG_TERMINATOR;
    out->n = at;
    (void)written;
}

/* The bank a battle reads at move * 3, whose entries carry the move's NAME
 * inline. Not growing it printed a blank box for every ported move (seen
 * 2026-08-31). Each new triple is the last shared move's triple with its name
 * swapped, so nothing here has an opinion about how this game words it. */
static void grow_used_in_battle(bank *used, const bank *names)
{
    u32 last = (u32)MMO_PORTED_MOVE_SHARED, k, m;
    u32 head_len[USED_FORMS], tail_at[USED_FORMS];
    u32 have;
    msg *grown;

    if (names->n <= last || used->n < (last + 1) * USED_FORMS)
        die("the attack-message bank holds %u entries and this fill is built on "
            "at least %u", used->n, (last + 1) * USED_FORMS);
    have = names->e[last].n > 0 ? names->e[last].n - 1 : 0;
    for (k = 0; k < USED_FORMS; k++) {
        const msg *e = &used->e[last * USED_FORMS + k];
        u32 i, at = 0;

        for (i = 0; i < e->n && e->c[i] != MSG_NEWLINE; i++)
            ;
        if (i >= e->n)
            die("attack message %u carries no line break, so this cannot find "
                "the move's name in it", last * USED_FORMS + k);
        at = i + 1;
        if (at + have > e->n
            || memcmp(e->c + at, names->e[last].c, have * sizeof *e->c) != 0)
            die("attack message %u does not carry the name of move %u where "
                "this fill expects it", last * USED_FORMS + k, last);
        head_len[k] = at;
        tail_at[k] = at + have;
    }
    grown = xmalloc((size_t)names->n * USED_FORMS * sizeof *grown);
    for (m = 0; m < (last + 1) * USED_FORMS; m++)
        grown[m] = used->e[m];
    for (m = last + 1; m < names->n; m++) {
        u32 name_len = names->e[m].n > 0 ? names->e[m].n - 1 : 0;

        for (k = 0; k < USED_FORMS; k++) {
            const msg *e = &used->e[last * USED_FORMS + k];
            u32 tail = e->n - tail_at[k];
            msg *to = &grown[m * USED_FORMS + k];
            u32 at = 0;

            to->n = head_len[k] + name_len + tail;
            to->c = xmalloc((size_t)to->n * sizeof *to->c);
            memcpy(to->c, e->c, head_len[k] * sizeof *to->c);
            at = head_len[k];
            memcpy(to->c + at, names->e[m].c, name_len * sizeof *to->c);
            at += name_len;
            memcpy(to->c + at, e->c + tail_at[k], tail * sizeof *to->c);
        }
    }
    free(used->e);
    used->e = grown;
    used->n = names->n * USED_FORMS;
}

/* A bank rewritten unchanged has to give the same bytes back: the licence to
 * write a longer one, the same one the species name bank is held to. */
static void bank_holds(const bank *bk, blob was, int which)
{
    blob back = msg_write(bk);

    if (back.len != was.len || memcmp(back.p, was.p, back.len) != 0)
        die("rewriting your message bank %d unchanged does not give the same "
            "bytes, so this cannot be trusted to write a longer one", which);
    free(back.p);
}

int mmo_speciescompose_moves(const char *bw_rom, const char *pt_rom,
                             const char *pkg,
                             void (*note)(void *ud, const char *line),
                             void *ud, char *err, size_t errcap)
{
    blob rom, pt, moves_arc, text_arc, tbl_arc, anim_arc, seq_arc, msg_arc;
    members g5moves, text, table, anims, seqs, msgs;
    bank g5names, g5descs, descs, names, upper, used;
    rules *r;
    char dtbl[1088], danim[1088], dseq[1088], dmsg[1088], line[192];
    int m, six = 0;
    jmp_buf jb;

    if (err != NULL && errcap > 0)
        err[0] = '\0';
    if (setjmp(jb) != 0)
        return -1;
    mmo_nitro_catch(&jb, err, errcap);

    if (bw_rom == NULL || bw_rom[0] == '\0')
        die("that fill needs a Black or White cartridge");
    if (pt_rom == NULL || pt_rom[0] == '\0')
        die("that fill needs your own Platinum cartridge too: the move table it "
            "grows, and the three answers no Gen 5 row can give, are read off "
            "yours");
    if (pkg == NULL || pkg[0] == '\0')
        die("that fill has no package to write into");

    if (note != NULL)
        note(ud, "Reading the moves off your cartridge...");
    rom = read_file(bw_rom);
    moves_arc = mmo_nitro_file(rom, SRC_MOVES, "that file",
                               "it is not a Black or White cartridge");
    g5moves = narc_members(moves_arc, SRC_MOVES);
    if ((int)g5moves.n <= MMO_PORTED_MOVE_LAST)
        die("that cartridge's move table holds %u entries and this fill needs "
            "%d", g5moves.n, MMO_PORTED_MOVE_LAST + 1);
    text_arc = mmo_nitro_file(rom, SRC_TEXT, "that file",
                              "it is not a Black or White cartridge");
    text = narc_members(text_arc, SRC_TEXT);
    if (text.n <= SRC_TEXT_MOVE_NAMES)
        die("that cartridge's text archive has no member %d",
            SRC_TEXT_MOVE_NAMES);
    g5names = g5_text(text.m[SRC_TEXT_MOVE_NAMES], "that cartridge's move names");
    g5descs = g5_text(text.m[SRC_TEXT_MOVE_DESCS],
                      "that cartridge's move descriptions");
    if ((int)g5names.n <= MMO_PORTED_MOVE_LAST
        || (int)g5descs.n <= MMO_PORTED_MOVE_LAST)
        die("that cartridge names %u moves and describes %u, and this fill "
            "needs %d of each", g5names.n, g5descs.n, MMO_PORTED_MOVE_LAST + 1);

    pt = read_file(pt_rom);
    tbl_arc = mmo_nitro_file(pt, DST_MOVE_TABLE, "that Platinum file",
                             "it is not a Platinum cartridge");
    anim_arc = mmo_nitro_file(pt, DST_MOVE_ANIM, "that Platinum file",
                              "it is not a Platinum cartridge");
    seq_arc = mmo_nitro_file(pt, DST_MOVE_SEQ, "that Platinum file",
                             "it is not a Platinum cartridge");
    msg_arc = mmo_nitro_file(pt, DST_MESSAGE, "that Platinum file",
                             "it is not a Platinum cartridge");
    table = narc_members(tbl_arc, DST_MOVE_TABLE);
    anims = narc_members(anim_arc, DST_MOVE_ANIM);
    seqs = narc_members(seq_arc, DST_MOVE_SEQ);
    msgs = narc_members(msg_arc, DST_MESSAGE);
    if (table.n != MOVE_TABLE_ROM_MEMBERS || anims.n != MOVE_ANIM_ROM_MEMBERS
        || seqs.n != MOVE_SEQ_ROM_MEMBERS)
        die("your Platinum holds %u move entries, %u animations and %u battle "
            "stubs, and this fill is built on %d, %d and %d", table.n, anims.n,
            seqs.n, MOVE_TABLE_ROM_MEMBERS, MOVE_ANIM_ROM_MEMBERS,
            MOVE_SEQ_ROM_MEMBERS);
    for (m = 0; m < (int)table.n; m++) {
        if (table.m[m].len != MOVE_ENTRY_BYTES)
            die("move %d is %u bytes in your Platinum's table, not %d", m,
                table.m[m].len, MOVE_ENTRY_BYTES);
    }
    if (msgs.n <= BANK_MOVE_UPPER)
        die("your Platinum's message archive has no bank %d", BANK_MOVE_UPPER);

    r = xmalloc(sizeof *r);
    rules_build(r, &table, &seqs);

    /*
     * The SHARED RANGE is the oracle, as it is for the species tables: the 467 moves both
     * games have are rebuilt out of Black and held against this game's own entries, and the
     * fill refuses whole unless they disagree in exactly the ways Gen 5 is known to have
     * changed them.
     */
    {
        static const struct { const char *name; int want; } expect[] = {
            { "power", MMO_PORTED_MOVE_DIFF_POWER },
            { "accuracy", MMO_PORTED_MOVE_DIFF_ACCURACY },
            { "pp", MMO_PORTED_MOVE_DIFF_PP },
            { "chance", MMO_PORTED_MOVE_DIFF_CHANCE },
            { "priority", MMO_PORTED_MOVE_DIFF_PRIORITY },
            { "effect", MMO_PORTED_MOVE_DIFF_EFFECT },
            { "range", MMO_PORTED_MOVE_DIFF_RANGE },
            { "type", MMO_PORTED_MOVE_DIFF_TYPE },
            { "class", MMO_PORTED_MOVE_DIFF_CLASS },
            { "contact", MMO_PORTED_MOVE_DIFF_FLAG_CONTACT },
            { "protect", MMO_PORTED_MOVE_DIFF_FLAG_PROTECT },
            { "magic coat", MMO_PORTED_MOVE_DIFF_FLAG_MAGIC_COAT },
            { "snatch", MMO_PORTED_MOVE_DIFF_FLAG_SNATCH },
            { "mirror move", MMO_PORTED_MOVE_DIFF_FLAG_MIRROR_MOVE }
        };
        int got[14];
        size_t k;

        memset(got, 0, sizeof got);
        for (m = 1; m <= MMO_PORTED_MOVE_SHARED; m++) {
            move g5, mine;
            u8 built[MOVE_ENTRY_BYTES];
            int i;

            move_gen5(g5moves.m[m].p, &g5);
            move_entry(r, &g5, r->own[m].flags, built);
            move_unpack(built, &mine);
            got[0] += mine.power != r->own[m].power;
            got[1] += mine.accuracy != r->own[m].accuracy;
            got[2] += mine.pp != r->own[m].pp;
            got[3] += mine.chance != r->own[m].chance;
            got[4] += mine.priority != r->own[m].priority;
            got[5] += mine.effect != r->own[m].effect;
            got[6] += mine.range != r->own[m].range;
            got[7] += mine.type != r->own[m].type;
            got[8] += mine.cls != r->own[m].cls;
            for (i = 0; i < 5; i++)
                got[9 + i] += ((mine.flags ^ r->own[m].flags)
                               >> FLAG_BITS[i][1]) & 1;
        }
        for (k = 0; k < sizeof expect / sizeof expect[0]; k++) {
            if (got[k] != expect[k].want)
                die("%s: %d of the moves both games have disagree with your "
                    "Platinum's table and %d were measured, so this decode has "
                    "moved; nothing written", expect[k].name, got[k],
                    expect[k].want);
        }
    }

    descs = msg_read(msgs.m[BANK_MOVE_DESCS], "the move description bank");
    names = msg_read(msgs.m[BANK_MOVE_NAMES], "the move name bank");
    upper = msg_read(msgs.m[BANK_MOVE_UPPER], "the upper-case move name bank");
    used = msg_read(msgs.m[BANK_USED_IN_BATTLE], "the attack message bank");
    if ((int)descs.n != MOVE_BANK_ENTRIES || (int)names.n != MOVE_BANK_ENTRIES
        || (int)upper.n != MOVE_BANK_ENTRIES)
        die("your Platinum's move banks hold %u, %u and %u entries and this "
            "fill is built on %d", descs.n, names.n, upper.n,
            MOVE_BANK_ENTRIES);
    bank_holds(&descs, msgs.m[BANK_MOVE_DESCS], BANK_MOVE_DESCS);
    bank_holds(&names, msgs.m[BANK_MOVE_NAMES], BANK_MOVE_NAMES);
    bank_holds(&upper, msgs.m[BANK_MOVE_UPPER], BANK_MOVE_UPPER);
    bank_holds(&used, msgs.m[BANK_USED_IN_BATTLE], BANK_USED_IN_BATTLE);

    snprintf(line, sizeof line, "Writing the %d moves your cartridge adds...",
             MMO_PORTED_MOVE_LAST + 1 - MMO_PORTED_MOVE_FIRST);
    if (note != NULL)
        note(ud, line);

    narc_dir(dtbl, sizeof dtbl, pkg, DST_MOVE_TABLE);
    narc_dir(danim, sizeof danim, pkg, DST_MOVE_ANIM);
    narc_dir(dseq, sizeof dseq, pkg, DST_MOVE_SEQ);
    narc_dir(dmsg, sizeof dmsg, pkg, DST_MESSAGE);

    /* The three banks grow together with the table, so a name and the entry it
     * names cannot be written by two different runs. */
    {
        msg *gdesc = xmalloc((size_t)(MMO_PORTED_MOVE_LAST + 1) * sizeof *gdesc);
        msg *gname = xmalloc((size_t)(MMO_PORTED_MOVE_LAST + 1) * sizeof *gname);
        msg *gupper = xmalloc((size_t)(MMO_PORTED_MOVE_LAST + 1) * sizeof *gupper);
        blob out;
        u32 i;

        for (i = 0; i < descs.n; i++) {
            gdesc[i] = descs.e[i];
            gname[i] = names.e[i];
            gupper[i] = upper.e[i];
        }
        for (m = MMO_PORTED_MOVE_FIRST; m <= MMO_PORTED_MOVE_LAST; m++) {
            int donor = MMO_PORTED_MOVE_ANIM[m - MMO_PORTED_MOVE_FIRST];
            u16 buf[512];
            u32 breaks[64], lines;
            move g5;
            u8 built[MOVE_ENTRY_BYTES];

            if (donor < 1 || donor > MMO_PORTED_MOVE_SHARED)
                die("move %d borrows animation %d, which is not one of this "
                    "game's own moves", m, donor);
            move_gen5(g5moves.m[m].p, &g5);
            move_entry(r, &g5, r->own[donor].flags, built);
            write_member(dtbl, m, built, MOVE_ENTRY_BYTES);
            write_member(danim, m, anims.m[donor].p, anims.m[donor].len);
            {
                move e;
                int stub;

                move_unpack(built, &e);
                stub = r->stub[e.effect];
                if (stub < 0)
                    stub = r->stub[EFFECT_HIT];
                if (stub < 0)
                    die("this game has no battle stub for effect %d and none "
                        "for a plain hit either", e.effect);
                write_member(dseq, m, seqs.m[stub].p, seqs.m[stub].len);
            }

            {
                u32 n = g5_text_copy(&g5descs.e[m], buf,
                                     sizeof buf / sizeof buf[0]);
                u32 loose_breaks[64], loose;

                lines = g5_wrap(buf, n, LINE_CHARS, breaks,
                                sizeof breaks / sizeof breaks[0]);
                if (lines > BOX_LINES) {
                    loose = g5_wrap(buf, n, LINE_CHARS_LOOSE, loose_breaks,
                                    sizeof loose_breaks / sizeof loose_breaks[0]);
                    if (loose <= BOX_LINES) {
                        memcpy(breaks, loose_breaks, sizeof breaks);
                        lines = loose;
                    } else {
                        six++;
                    }
                }
                g5_encode_lines(buf, n, breaks, lines, &gdesc[m], m, 0);
            }
            {
                u16 nbuf[64];
                u32 n = g5_text_copy(&g5names.e[m], nbuf,
                                     sizeof nbuf / sizeof nbuf[0]);

                if (n > (u32)MOVE_NAME_CHARS)
                    die("move %d is named with %u characters on that cartridge, "
                        "more than any name this game holds", m, n);
                g5_encode_lines(nbuf, n, NULL, 1, &gname[m], m, 0);
                g5_encode_lines(nbuf, n, NULL, 1, &gupper[m], m, 1);
            }
        }
        free(descs.e);
        free(names.e);
        free(upper.e);
        descs.e = gdesc;
        names.e = gname;
        upper.e = gupper;
        descs.n = names.n = upper.n = (u32)MMO_PORTED_MOVE_LAST + 1;

        out = msg_write(&descs);
        write_member(dmsg, BANK_MOVE_DESCS, out.p, out.len);
        free(out.p);
        out = msg_write(&names);
        write_member(dmsg, BANK_MOVE_NAMES, out.p, out.len);
        free(out.p);
        out = msg_write(&upper);
        write_member(dmsg, BANK_MOVE_UPPER, out.p, out.len);
        free(out.p);
        grow_used_in_battle(&used, &names);
        out = msg_write(&used);
        write_member(dmsg, BANK_USED_IN_BATTLE, out.p, out.len);
        free(out.p);
    }

    if (note != NULL) {
        snprintf(line, sizeof line, "%d moves written; %d descriptions needed a "
                 "sixth line", MMO_PORTED_MOVE_LAST + 1 - MMO_PORTED_MOVE_FIRST,
                 six);
        note(ud, line);
    }

    bank_free(&descs);
    bank_free(&names);
    bank_free(&upper);
    bank_free(&used);
    bank_free(&g5names);
    bank_free(&g5descs);
    free(r);
    free(rom.p);
    free(pt.p);
    free(moves_arc.p);
    free(text_arc.p);
    free(tbl_arc.p);
    free(anim_arc.p);
    free(seq_arc.p);
    free(msg_arc.p);
    free(g5moves.m);
    free(text.m);
    free(table.m);
    free(anims.m);
    free(seqs.m);
    free(msgs.m);
    mmo_nitro_catch(NULL, NULL, 0);
    return 0;
}


/* --------------------------------------------------------------- abilities */
/* The 41 abilities the same cartridge adds, which are three message banks. */
#define SRC_TEXT_ABILITY_NAMES  182
#define SRC_TEXT_ABILITY_DESCS  183
#define SRC_TEXT_ABILITY_UPPER  285
#define BANK_ABILITY_NAMES      610
#define BANK_ABILITY_UPPER      611
#define BANK_ABILITY_DESCS      612
#define ABILITY_BANK_ENTRIES    124
#define ABILITY_FIRST           124
#define ABILITY_LAST            164
#define ABILITY_LINE_CHARS      26   /* this game's box, over its own 124 */
#define ABILITY_BOX_LINES       2
#define ABILITY_NAME_CHARS      12

int mmo_speciescompose_abilities(const char *bw_rom, const char *pt_rom,
                                 const char *pkg,
                                 void (*note)(void *ud, const char *line),
                                 void *ud, char *err, size_t errcap)
{
    blob rom, pt, text_arc, msg_arc;
    members text, msgs;
    bank g5names, g5uppers, g5descs, names, upper, descs;
    char dmsg[1088], line[192];
    int a, wide = 0;
    jmp_buf jb;

    if (err != NULL && errcap > 0)
        err[0] = '\0';
    if (setjmp(jb) != 0)
        return -1;
    mmo_nitro_catch(&jb, err, errcap);

    if (bw_rom == NULL || bw_rom[0] == '\0')
        die("that fill needs a Black or White cartridge");
    if (pt_rom == NULL || pt_rom[0] == '\0')
        die("that fill needs your own Platinum cartridge too: the ability banks "
            "it grows are this game's own");
    if (pkg == NULL || pkg[0] == '\0')
        die("that fill has no package to write into");

    rom = read_file(bw_rom);
    text_arc = mmo_nitro_file(rom, SRC_TEXT, "that file",
                              "it is not a Black or White cartridge");
    text = narc_members(text_arc, SRC_TEXT);
    if (text.n <= SRC_TEXT_ABILITY_UPPER)
        die("that cartridge's text archive has no member %d",
            SRC_TEXT_ABILITY_UPPER);
    g5names = g5_text(text.m[SRC_TEXT_ABILITY_NAMES],
                      "that cartridge's ability names");
    g5uppers = g5_text(text.m[SRC_TEXT_ABILITY_UPPER],
                       "that cartridge's upper case ability names");
    g5descs = g5_text(text.m[SRC_TEXT_ABILITY_DESCS],
                      "that cartridge's ability descriptions");
    if ((int)g5names.n <= ABILITY_LAST || (int)g5uppers.n <= ABILITY_LAST
        || (int)g5descs.n <= ABILITY_LAST)
        die("that cartridge names %u abilities and this fill needs %d",
            g5names.n, ABILITY_LAST + 1);

    pt = read_file(pt_rom);
    msg_arc = mmo_nitro_file(pt, DST_MESSAGE, "that Platinum file",
                             "it is not a Platinum cartridge");
    msgs = narc_members(msg_arc, DST_MESSAGE);
    if (msgs.n <= BANK_ABILITY_DESCS)
        die("your Platinum's message archive has no bank %d",
            BANK_ABILITY_DESCS);
    names = msg_read(msgs.m[BANK_ABILITY_NAMES], "the ability name bank");
    upper = msg_read(msgs.m[BANK_ABILITY_UPPER],
                     "the upper case ability name bank");
    descs = msg_read(msgs.m[BANK_ABILITY_DESCS], "the ability description bank");
    if ((int)names.n != ABILITY_BANK_ENTRIES
        || (int)upper.n != ABILITY_BANK_ENTRIES
        || (int)descs.n != ABILITY_BANK_ENTRIES)
        die("your Platinum's ability banks hold %u, %u and %u entries and this "
            "fill is built on %d", names.n, upper.n, descs.n,
            ABILITY_BANK_ENTRIES);
    bank_holds(&names, msgs.m[BANK_ABILITY_NAMES], BANK_ABILITY_NAMES);
    bank_holds(&upper, msgs.m[BANK_ABILITY_UPPER], BANK_ABILITY_UPPER);
    bank_holds(&descs, msgs.m[BANK_ABILITY_DESCS], BANK_ABILITY_DESCS);

    /* The numbering, on every id both games have. */
    for (a = 1; a < ABILITY_FIRST; a++) {
        u16 buf[64];
        u32 n = g5_text_copy(&g5names.e[a], buf, sizeof buf / sizeof buf[0]);
        msg mine, theirs;

        g5_encode_lines(buf, n, NULL, 1, &mine, a, 0);
        n = g5_text_copy(&g5uppers.e[a], buf, sizeof buf / sizeof buf[0]);
        g5_encode_lines(buf, n, NULL, 1, &theirs, a, 0);
        if (mine.n != names.e[a].n
            || memcmp(mine.c, names.e[a].c, mine.n * sizeof *mine.c) != 0)
            die("ability %d is not spelled the same on your two cartridges, so "
                "they do not number abilities the same way; nothing written", a);
        /* And this game's upper case entry is that name upper-cased, which is
         * the rule that lets the fill carry Black's own upper bank. */
        if (theirs.n != upper.e[a].n
            || memcmp(theirs.c, upper.e[a].c, theirs.n * sizeof *theirs.c) != 0)
            die("ability %d's upper case name is not the rule this fill "
                "carries; nothing written", a);
        free(mine.c);
        free(theirs.c);
    }

    if (note != NULL)
        note(ud, "Writing the abilities your cartridge adds...");
    narc_dir(dmsg, sizeof dmsg, pkg, DST_MESSAGE);
    {
        u32 count = (u32)(ABILITY_LAST + 1 - ABILITY_FIRST);
        msg *gname = xmalloc((size_t)(ABILITY_LAST + 1) * sizeof *gname);
        msg *gupper = xmalloc((size_t)(ABILITY_LAST + 1) * sizeof *gupper);
        msg *gdesc = xmalloc((size_t)(ABILITY_LAST + 1) * sizeof *gdesc);
        blob out;
        u32 i;

        for (i = 0; i < names.n; i++) {
            gname[i] = names.e[i];
            gupper[i] = upper.e[i];
            gdesc[i] = descs.e[i];
        }
        for (a = ABILITY_FIRST; a <= ABILITY_LAST; a++) {
            u16 buf[512];
            u32 breaks[8], lines, n;

            n = g5_text_copy(&g5names.e[a], buf, sizeof buf / sizeof buf[0]);
            if (n > (u32)ABILITY_NAME_CHARS)
                die("ability %d is named with %u characters on that cartridge, "
                    "more than any name this game holds", a, n);
            g5_encode_lines(buf, n, NULL, 1, &gname[a], a, 0);
            n = g5_text_copy(&g5uppers.e[a], buf, sizeof buf / sizeof buf[0]);
            g5_encode_lines(buf, n, NULL, 1, &gupper[a], a, 0);
            n = g5_text_copy(&g5descs.e[a], buf, sizeof buf / sizeof buf[0]);
            lines = g5_wrap(buf, n, ABILITY_LINE_CHARS, breaks,
                            sizeof breaks / sizeof breaks[0]);
            if (lines > ABILITY_BOX_LINES)
                wide++;
            g5_encode_lines(buf, n, breaks, lines, &gdesc[a], a, 0);
        }
        free(names.e);
        free(upper.e);
        free(descs.e);
        names.e = gname;
        upper.e = gupper;
        descs.e = gdesc;
        names.n = upper.n = descs.n = (u32)ABILITY_LAST + 1;

        out = msg_write(&names);
        write_member(dmsg, BANK_ABILITY_NAMES, out.p, out.len);
        free(out.p);
        out = msg_write(&upper);
        write_member(dmsg, BANK_ABILITY_UPPER, out.p, out.len);
        free(out.p);
        out = msg_write(&descs);
        write_member(dmsg, BANK_ABILITY_DESCS, out.p, out.len);
        free(out.p);
        if (note != NULL) {
            snprintf(line, sizeof line, "%u abilities written; %d descriptions "
                     "needed a third line", count, wide);
            note(ud, line);
        }
    }

    bank_free(&names);
    bank_free(&upper);
    bank_free(&descs);
    bank_free(&g5names);
    bank_free(&g5uppers);
    bank_free(&g5descs);
    free(rom.p);
    free(pt.p);
    free(text_arc.p);
    free(msg_arc.p);
    free(text.m);
    free(msgs.m);
    mmo_nitro_catch(NULL, NULL, 0);
    return 0;
}

/* ------------------------------------------------------------------ oracle */
/* This game's own species archive, by the path its image keeps it at. */
#define PT_PERSONAL "poketool/personal/pl_personal.narc"

int mmo_speciescompose_check(const char *bw_rom, const char *pt_rom,
                             int *diff, char *err, size_t errcap)
{
    blob bw, pt, bw_arc, pt_arc;
    members g5, g4;
    int species, last, compared = 0, i;
    jmp_buf jb;

    if (err != NULL && errcap > 0)
        err[0] = '\0';
    if (setjmp(jb) != 0)
        return -1;
    mmo_nitro_catch(&jb, err, errcap);

    if (diff == NULL)
        die("that check has nowhere to put its counts");
    for (i = 0; i < MMO_SPECIESCOMPOSE_ENTRY; i++)
        diff[i] = 0;

    bw = read_file(bw_rom);
    pt = read_file(pt_rom);
    bw_arc = mmo_nitro_file(bw, SRC_PERSONAL, "that Gen 5 file",
                            "it is not a Black or White cartridge");
    pt_arc = mmo_nitro_file(pt, PT_PERSONAL, "that Platinum file",
                            "it is not a Platinum cartridge");
    g5 = narc_members(bw_arc, SRC_PERSONAL);
    g4 = narc_members(pt_arc, PT_PERSONAL);

    /*
     * The shared range is every species this game numbers, less the two ids it spends on the
     * egg and the bad egg, Black puts Victini and Snivy there, so those two members are not
     * the same creature in the two images and comparing them would be comparing a Pokemon
     * against an egg.
     */
    last = MMO_PORTED_FIRST - 1;
    if ((int)g4.n <= last || (int)g5.n <= last)
        die("one of those images holds fewer species than the other numbers");
    for (species = 1; species <= last; species++) {
        u8 built[G4_SIZE];

        if (g5.m[species].len < G5_SIZE || g4.m[species].len < G4_SIZE)
            continue;
        /* Machines off: Gen 5 genuinely changed what some species learn, so
         * the bits are a different question with an oracle of their own, and
         * counting them here would drown the mapping this is about. */
        mmo_speciescompose_entry(g5.m[species].p, built, 0);
        for (i = 0; i < G4_SIZE; i++) {
            /*
             * The bytes this fill does not carry are not compared, because comparing them
             * would only be checking that a constant is a constant: 0x18 is the Great Marsh
             * flee rate and 0x1C.. are the machine flags, and both are written as zero.
             */
            if (i == G4_MARSH || i >= G4_MACHINES)
                continue;
            if (built[i] != g4.m[species].p[i])
                diff[i]++;
        }
        compared++;
    }

    free(bw.p);
    free(pt.p);
    free(bw_arc.p);
    free(pt_arc.p);
    free(g5.m);
    free(g4.m);
    mmo_nitro_catch(NULL, NULL, 0);
    return compared;
}

/* ------------------------------------------------------------------- names */
/* A cartridge name as this game spells it: uppercase, then through the client's own charmap. */
static void encode_name(const msg *from, msg *out, int species)
{
    u8 utf16[128 * 2];
    mmo_charcode codes[128];
    mmo_charcode_result r;
    u32 n = 0, i;

    for (i = 0; i + 1 < from->n && n < 128; i++) {
        u16 ch = from->c[i];

        if (ch == 0 || ch == MSG_TAG)
            continue;                   /* padding, and Gen 5's own newline */
        if (ch >= 'a' && ch <= 'z')
            ch = (u16)(ch - 'a' + 'A');
        utf16[n * 2] = (u8)ch;
        utf16[n * 2 + 1] = (u8)(ch >> 8);
        n++;
    }
    if (n == 0)
        die("species %d has no name on that cartridge", species);
    r = mmo_utf16le_to_charcode(utf16, n * 2, codes,
                                sizeof codes / sizeof codes[0]);
    /* Refused rather than drawn as the fallback glyph: a name with a question
     * mark in it is a Pokemon nobody can look up, and it would be written into
     * the bank once and read for ever. */
    if (r.unmapped != 0)
        die("species %d's name uses %zu character(s) this game has no glyph "
            "for", species, r.unmapped);
    if (r.truncated)
        die("species %d's name does not fit this game's bank", species);
    /* The converter terminates for us, and its EOS is this format's. */
    out->n = (u32)r.written + 1;
    out->c = xmalloc((size_t)out->n * sizeof *out->c);
    for (i = 0; i < (u32)r.written; i++)
        out->c[i] = codes[i];
    out->c[r.written] = MSG_TERMINATOR;
}

/* How much of an articles entry is the article itself: everything before its
 * opening tag. */
static u32 art_len(const msg *e)
{
    u32 i;

    for (i = 0; i < e->n && e->c[i] != MSG_TAG; i++)
        ;
    return i;
}

/*
 * The articles bank beside the names: "a " or "an ", an opening tag, the name, a closing tag.
 */
static void grow_articles(bank *bk, const bank *names, mmo_charcode vowels[5])
{
    const msg *a, *an;
    u32 open_at, close_at, i;
    const msg *sample;

    if (bk->n >= names->n)
        return;
    if (bk->n <= 23)
        die("the articles bank holds %u entries, too few to read its own shape "
            "from", bk->n);
    sample = &bk->e[1];
    for (open_at = 0; open_at < sample->n && sample->c[open_at] != MSG_TAG;
         open_at++)
        ;
    if (open_at + 4 > sample->n)
        die("the articles bank's first entry carries no opening tag");
    for (close_at = sample->n; close_at > 0; close_at--) {
        if (sample->c[close_at - 1] == MSG_TAG)
            break;
    }
    if (close_at == 0)
        die("the articles bank's first entry carries no closing tag");
    /* The loop leaves close_at one past the last tag, and the closing tag
     * starts at it, the four units from the tag itself. */
    close_at -= 1;
    if (close_at + 4 > sample->n)
        die("the articles bank's first entry ends inside its closing tag");
    a = &bk->e[1];
    an = &bk->e[23];
    {
        msg *grown = xmalloc((size_t)names->n * sizeof *grown);

        for (i = 0; i < bk->n; i++)
            grown[i] = bk->e[i];
        for (i = bk->n; i < names->n; i++) {
            const msg *name = &names->e[i];
            u32 body = name->n > 0 ? name->n - 1 : 0;   /* less the terminator */
            const msg *art = a;
            u32 at = 0, k;
            int v;

            for (v = 0; v < 5; v++) {
                if (body > 0 && name->c[0] == vowels[v]) {
                    art = an;
                    break;
                }
            }
            grown[i].n = open_at + 4 + body + 4 + 1;
            /* the article's own run is everything before its opening tag */
            grown[i].n = art_len(art) + 4 + body + 4 + 1;
            grown[i].c = xmalloc((size_t)grown[i].n * sizeof *grown[i].c);
            for (k = 0; k < art_len(art); k++)
                grown[i].c[at++] = art->c[k];
            for (k = 0; k < 4; k++)
                grown[i].c[at++] = sample->c[open_at + k];
            for (k = 0; k < body; k++)
                grown[i].c[at++] = name->c[k];
            for (k = 0; k < 4; k++)
                grown[i].c[at++] = sample->c[close_at + k];
            grown[i].c[at++] = MSG_TERMINATOR;
            grown[i].n = at;
        }
        free(bk->e);
        bk->e = grown;
        bk->n = names->n;
    }
}

/* ------------------------------------------------------------------ tables */
int mmo_speciescompose_tables(const char *bw_rom, const char *pt_rom,
                              const char *pkg,
                              void (*note)(void *ud, const char *line),
                              void *ud, char *err, size_t errcap)
{
    blob rom, pt, personal_arc, evo_arc, icon_arc, text_arc, msg_arc;
    members personal, evo, icons, text, msgs;
    bank names, g5names, articles;
    mmo_charcode vowels[5];
    char dper[1088], dlrn[1088], devo[1088], dico[1088], dmsg[1088];
    char line[192];
    int species, count;
    jmp_buf jb;

    if (err != NULL && errcap > 0)
        err[0] = '\0';
    if (setjmp(jb) != 0)
        return -1;
    mmo_nitro_catch(&jb, err, errcap);

    if (bw_rom == NULL || bw_rom[0] == '\0')
        die("that fill needs a Black or White cartridge");
    if (pt_rom == NULL || pt_rom[0] == '\0')
        die("that fill needs your own Platinum cartridge too: the name bank it "
            "grows is this game's own");
    if (pkg == NULL || pkg[0] == '\0')
        die("that fill has no package to write into");

    if (note != NULL)
        note(ud, "Reading your Black cartridge...");
    rom = read_file(bw_rom);

    /*
     * Everything is read and checked before a byte is written, so a cartridge that cannot
     * answer for itself costs the package nothing.
     */
    personal_arc = mmo_nitro_file(rom, SRC_PERSONAL, "that file",
                                  "it is not a Black or White cartridge");
    evo_arc = mmo_nitro_file(rom, SRC_EVOLUTION, "that file",
                             "it is not a Black or White cartridge");
    icon_arc = mmo_nitro_file(rom, SRC_ICON, "that file",
                              "it is not a Black or White cartridge");
    personal = narc_members(personal_arc, SRC_PERSONAL);
    evo = narc_members(evo_arc, SRC_EVOLUTION);
    icons = narc_members(icon_arc, SRC_ICON);
    text_arc = mmo_nitro_file(rom, SRC_TEXT, "that file",
                              "it is not a Black or White cartridge");
    text = narc_members(text_arc, SRC_TEXT);
    if (text.n <= SRC_TEXT_NAMES)
        die("that cartridge's text archive has no member %d", SRC_TEXT_NAMES);
    g5names = g5_text(text.m[SRC_TEXT_NAMES], "that cartridge's species names");
    if ((int)g5names.n <= MMO_PORTED_LAST)
        die("that cartridge names %u species and this fill needs %d",
            g5names.n, MMO_PORTED_LAST + 1);

    pt = read_file(pt_rom);
    msg_arc = mmo_nitro_file(pt, DST_MESSAGE, "that Platinum file",
                             "it is not a Platinum cartridge");
    msgs = narc_members(msg_arc, DST_MESSAGE);
    if (msgs.n <= BANK_ARTICLES)
        die("your Platinum's message archive has no bank %d", BANK_ARTICLES);
    names = msg_read(msgs.m[BANK_NAMES], "the species name bank");
    articles = msg_read(msgs.m[BANK_ARTICLES], "the species article bank");
    if ((int)names.n != MMO_PORTED_NAME_BASE)
        die("the species name bank holds %u entries and this fill is built on "
            "%d", names.n, MMO_PORTED_NAME_BASE);
    /*
     * The licence to write a longer BANK is that writing this one back
     * unchanged gives the same bytes. Without it a fill that grows the bank is
     * asserting it understands a format it has only read, and the cost of
     * being wrong is every name in the game.
     */
    {
        blob back = msg_write(&names);

        if (back.len != msgs.m[BANK_NAMES].len
            || memcmp(back.p, msgs.m[BANK_NAMES].p, back.len) != 0)
            die("rewriting your name bank unchanged does not give the same "
                "bytes, so this cannot be trusted to write a longer one");
        free(back.p);
    }
    {
        static const char *const V[5] = { "A", "E", "I", "O", "U" };
        int v;

        for (v = 0; v < 5; v++) {
            mmo_charcode one[2];

            mmo_utf8_to_charcode(V[v], one, 2);
            vowels[v] = one[0];
        }
    }

    count = MMO_PORTED_LAST - MMO_PORTED_FIRST + 1;
    /* The archives are indexed by species, so the last one this fill carries
     * has to be in them, said as a count rather than discovered as a read
     * off the end halfway through writing. */
    if ((int)personal.n <= MMO_PORTED_LAST)
        die("that cartridge's species table holds %u entries, and this fill "
            "needs %d", personal.n, MMO_PORTED_LAST + 1);
    if ((int)evo.n <= MMO_PORTED_LAST)
        die("that cartridge's evolution table holds %u entries, and this fill "
            "needs %d", evo.n, MMO_PORTED_LAST + 1);
    for (species = MMO_PORTED_FIRST; species <= MMO_PORTED_LAST; species++) {
        if (personal.m[species].len < G5_SIZE)
            die("species %d is %u bytes on that cartridge, not %d",
                species, personal.m[species].len, G5_SIZE);
        if (evo.m[species].len < G5_EVO_SIZE)
            die("species %d's evolutions are %u bytes on that cartridge, "
                "not %d", species, evo.m[species].len, G5_EVO_SIZE);
        {
            u32 at = (u32)(ICON_FIRST + ICON_BLACK_STRIDE * species);

            if (at >= icons.n || icons.m[at].len != ICON_BYTES)
                die("species %d has no icon in that cartridge's %s",
                    species, SRC_ICON);
        }
    }

    snprintf(line, sizeof line, "Reading %d species out of your cartridge...",
             count);
    if (note != NULL)
        note(ud, line);

    narc_dir(dper, sizeof dper, pkg, DST_PERSONAL);
    narc_dir(dlrn, sizeof dlrn, pkg, DST_LEARNSET);
    narc_dir(devo, sizeof devo, pkg, DST_EVOLUTION);
    narc_dir(dico, sizeof dico, pkg, DST_ICON);
    narc_dir(dmsg, sizeof dmsg, pkg, DST_MESSAGE);

    for (species = MMO_PORTED_FIRST; species <= MMO_PORTED_LAST; species++) {
        int at = MMO_PORTED_SPECIES_BASE + species - MMO_PORTED_FIRST;
        u8 entry[G4_SIZE], evolution[G4_EVO_SIZE];

        mmo_speciescompose_entry(personal.m[species].p, entry, 1);
        mmo_speciescompose_evolution(evo.m[species].p, evolution);
        write_member(dper, at, entry, G4_SIZE);
        write_member(dlrn, at, EMPTY_LEARNSET, sizeof EMPTY_LEARNSET);
        write_member(devo, at, evolution, G4_EVO_SIZE);

        {
            u32 from = (u32)(ICON_FIRST + ICON_BLACK_STRIDE * species);
            int icon_at = MMO_PORTED_ICON_BASE + species - MMO_PORTED_FIRST;
            u8 *icon = xmalloc(ICON_BYTES);

            memcpy(icon, icons.m[from].p, ICON_BYTES);
            icon[ICON_FLAG_OFFSET] = ICON_FLAG_PLATINUM;
            write_member(dico, icon_at, icon, ICON_BYTES);
            free(icon);
        }
    }

    /* The two banks last, because the articles are grown from the names and
     * the names have to be complete first. */
    {
        msg *grown = xmalloc((size_t)(names.n + count) * sizeof *grown);
        blob out;
        u32 i;

        for (i = 0; i < names.n; i++)
            grown[i] = names.e[i];
        for (species = MMO_PORTED_FIRST; species <= MMO_PORTED_LAST; species++)
            encode_name(&g5names.e[species],
                        &grown[names.n + species - MMO_PORTED_FIRST], species);
        free(names.e);
        names.e = grown;
        names.n += (u32)count;

        out = msg_write(&names);
        write_member(dmsg, BANK_NAMES, out.p, out.len);
        free(out.p);

        grow_articles(&articles, &names, vowels);
        out = msg_write(&articles);
        write_member(dmsg, BANK_ARTICLES, out.p, out.len);
        free(out.p);
    }

    bank_free(&names);
    bank_free(&articles);
    bank_free(&g5names);
    free(pt.p);
    free(msg_arc.p);
    free(text_arc.p);
    free(msgs.m);
    free(text.m);
    free(rom.p);
    free(personal_arc.p);
    free(evo_arc.p);
    free(icon_arc.p);
    free(personal.m);
    free(evo.m);
    free(icons.m);
    mmo_nitro_catch(NULL, NULL, 0);
    return 0;
}

/* -------------------------------------------------------------- the seam */
/* The play-time half. */
/* v2: the height byte is the blank rows under a centred resting pose again
 * (blackcompose.c's mmo_black_height_byte), so a package filled by an older
 * build, whose bytes seat every menu sprite twenty pixels low, is refilled
 * rather than trusted. */
#define SPECIESCOMPOSE_STAMP "v2 508 3828 92 41"

static int sc_pkg_dir(const char *path)
{
    char probe[MMO_LAUNCH_PATH + 64];
    FILE *f;

    snprintf(probe, sizeof probe, "%s/mod.toml", path);
    f = fopen(probe, "rb");
    if (f == NULL)
        return 0;
    fclose(f);
    return 1;
}

int mmo_speciescompose_ensure(const mmo_launch_settings *s,
                              const char *port_exe,
                              void (*note)(void *ud, const char *line),
                              void *ud, char *err, size_t errcap)
{
    char root[MMO_LAUNCH_PATH];
    char pkg[MMO_LAUNCH_PATH + 32];
    char stamp[MMO_LAUNCH_PATH + 64];
    char bw_file[MMO_LAUNCH_PATH];
    char pt_file[MMO_LAUNCH_PATH];
    char mark[MMO_LAUNCH_PATH + 64];
    char have[128] = "";
    char why[160];
    FILE *f;

    if (err != NULL && errcap > 0)
        err[0] = '\0';
    mmo_launch_mods_root(s, port_exe, root, sizeof root);
    snprintf(pkg, sizeof pkg, "%s%s%s", root, mmo_plat_sep(), "imports");
    snprintf(stamp, sizeof stamp, "%s%s%s", pkg, mmo_plat_sep(),
             "composed.txt");

    f = fopen(stamp, "rb");
    if (f != NULL) {
        size_t n = fread(have, 1, sizeof have - 1, f);

        have[n] = '\0';
        fclose(f);
        if (strcmp(have, SPECIESCOMPOSE_STAMP "\n") == 0 && sc_pkg_dir(pkg))
            return 1;           /* filled by this build */
    }

    /* A package somebody else put there is left alone. */
    snprintf(mark, sizeof mark, "%s%s%s", pkg, mmo_plat_sep(), "composing.txt");
    f = fopen(mark, "rb");
    if (f != NULL) {
        fclose(f);
    } else if (have[0] == '\0' && sc_pkg_dir(pkg)) {
        return 1;
    }

    if (!mmo_sound_slot_status(s, MMO_LAUNCH_CART_BLACK, bw_file,
                               sizeof bw_file, why, sizeof why)) {
        snprintf(err, errcap, "the Pokemon a Black or White cartridge adds are "
                 "read from your own copy of it (%s): choose one on the "
                 "settings face and they fill themselves next time", why);
        return 0;
    }
    snprintf(pt_file, sizeof pt_file, "%s", s->rom);
    if (pt_file[0] == '\0') {
        snprintf(err, errcap, "the species a Black cartridge adds are appended "
                 "to your own Platinum's tables, and no Platinum is chosen");
        return 0;
    }

    /* SAID AS COMPOSING, and said before the first read rather than after it. */
    if (note != NULL)
        note(ud, "composing the Pokemon your other cartridges add, out of your "
                 "own copies; this happens once...");
    mkdir_p(pkg);
    f = fopen(mark, "wb");
    if (f != NULL) {
        fputs(SPECIESCOMPOSE_STAMP "\n", f);
        fclose(f);
    }
    if (mmo_speciescompose_tables(bw_file, pt_file, pkg, note, ud, err,
                                  errcap) != 0
        || mmo_speciescompose_anim(bw_file, pkg, note, ud, err, errcap) != 0
        || mmo_speciescompose_sheets(bw_file, pt_file, pkg, note, ud, err,
                                     errcap) != 0
        || mmo_speciescompose_moves(bw_file, pt_file, pkg, note, ud, err,
                                    errcap) != 0
        || mmo_speciescompose_abilities(bw_file, pt_file, pkg, note, ud, err,
                                        errcap) != 0)
        return -1;
    /* And the cries, which are the same cartridge through the sound archive
     * rather than the sprite one, one file beside the members rather than a
     * member, because the engine plays a ported cry as a raw wave and the SDAT
     * is a claim two other packages already fight over. */
    {
        char cries[MMO_LAUNCH_PATH + 64];

        snprintf(cries, sizeof cries, "%s%s%s", pkg, mmo_plat_sep(),
                 "cries.bin");
        if (mmo_soundcompose_cries(bw_file, cries, err, errcap) != 0)
            return -1;
    }

    /* The package says what it is, and the stamp goes LAST: a machine that
     * dies mid-fill comes back with no stamp and fills again, rather than with
     * a stamp over half a package. */
    {
        char path[MMO_LAUNCH_PATH + 64];

        snprintf(path, sizeof path, "%s%s%s", pkg, mmo_plat_sep(), "mod.toml");
        f = fopen(path, "wb");
        if (f != NULL) {
            fputs("id = \"imports\"\n"
                  "name = \"Imported Cartridge Content\"\n"
                  "version = \"1.0.0\"\n"
                  "authors = [\"openmmo\"]\n"
                  "requires = []\n"
                  "load_after = []\n", f);
            fclose(f);
        }
        f = fopen(stamp, "wb");
        if (f != NULL) {
            fputs(SPECIESCOMPOSE_STAMP "\n", f);
            fclose(f);
        }
        remove(mark);
    }
    if (note != NULL)
        note(ud, "the Pokemon your cartridges add are ready; they are kept for "
                 "next time");
    return 1;
}
