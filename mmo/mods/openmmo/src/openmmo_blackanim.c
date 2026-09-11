/* Black's battle sprite loop, composed live. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef OPENMMO_BLACKANIM_HARNESS
#include "pokemon_sprite.h"

#include "pc_modfs.h"
#endif

/* The composing and the placing are shared. */
#include "blackcompose.h"

#define read_charmap    mmo_black_read_charmap
#define read_cells      mmo_black_read_cells
#define read_anims      mmo_black_read_anims
#define read_multicells mmo_black_read_multicells
#define compose         mmo_black_compose
#define s_canvas        mmo_black_canvas
#define s_cminx         mmo_black_minx
#define s_cminy         mmo_black_miny
#define s_cmaxx         mmo_black_maxx
#define s_cmaxy         mmo_black_maxy

#define ANIM_NARC        "poketool/pokegra/mmo_anim.narc"
#define POKEGRA_NARC     "poketool/pokegra/pl_pokegra.narc"
#define ANIM_STRIDE      12
#define POKEGRA_STRIDE   6
#define FRAME            80          /* the cartridge's frame, and the harness's */
#define FRAME_MAX_W      128         /* the widest frame a layout draws through */
#define FRAME_MAX_H      88
#define HEIGHT_NARC      "poketool/pokegra/height.narc"
#define ROW_BYTES        80          /* one 160-pixel row of a pair, 4bpp */
#define HALF_BYTES       40
#define PAIR_BYTES       6400

static struct face s_face[MAX_MON_SPRITES];

/* WHAT compose() JUST DREW, as the box of canvas pixels it actually set. */
static u8 s_frame[FRAME_MAX_W * FRAME_MAX_H];

/* Still wanted here for the palette load() reads; the rest of the decoding
 * went with the composer. */
static u16 rd16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }

static u8 *member(unsigned index, u32 *out_size)
{
    unsigned size = 0;
    u8 *buf;

    if (!pc_modfs_member_stat(ANIM_NARC, index, &size) || size == 0)
        return NULL;
    buf = malloc(size);
    if (buf == NULL)
        return NULL;
    if (!pc_modfs_member_read(ANIM_NARC, index, buf, 0, size)) {
        free(buf);
        return NULL;
    }
    *out_size = size;
    return buf;
}

/* The placement is the composer'S now, and so is the change map. */
#define mark_changes  mmo_black_marks
#define place         mmo_black_place
#define frame_of(f)   mmo_black_frame((f), s_frame)

/* ------------------------------------------------------------ the slots */

static void unload(struct face *f)
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
    f->character = -1;
}

static int load(struct face *f, int character)
{
    int species = character / POKEGRA_STRIDE, slot = character % POKEGRA_STRIDE;
    int front = slot >= 2, female = (slot & 1) == 0;
    unsigned base = (unsigned)species * ANIM_STRIDE + (front ? 0 : 6);
    u8 *blob;
    u32 len;
    u8 palette[40 + 32];

    unload(f);
    f->character = character;
    if (slot >= 4)
        return 0;                           /* a palette member, not a picture */
    blob = female ? member(base + 1, &len) : NULL;
    if (blob == NULL)
        blob = member(base, &len);
    if (blob == NULL)
        return 0;
    if (!read_charmap(f, blob, len)) {
        free(blob);
        return 0;
    }
    free(blob);
    if ((blob = member(base + 2, &len)) == NULL || !read_cells(f, blob, len)) {
        free(blob);
        return 0;
    }
    free(blob);
    if ((blob = member(base + 3, &len)) == NULL
        || !read_anims(blob, len, &f->cseqs, &f->ncseqs, &f->cframes, &f->ncframes)) {
        free(blob);
        return 0;
    }
    free(blob);
    if ((blob = member(base + 4, &len)) == NULL || !read_multicells(f, blob, len)) {
        free(blob);
        return 0;
    }
    free(blob);
    if ((blob = member(base + 5, &len)) == NULL
        || !read_anims(blob, len, &f->mseqs, &f->nmseqs, &f->mframes, &f->nmframes)) {
        free(blob);
        return 0;
    }
    free(blob);
    /* The palette, for a front scaled down onto it (16 BGR555 words at 40). */
    memset(palette, 0, sizeof palette);
    if (pc_modfs_member_read(POKEGRA_NARC, (unsigned)species * POKEGRA_STRIDE + 4, palette, 0, sizeof palette)) {
        int i;

        for (i = 0; i < 16; i++)
            f->pal[i] = (u16)(rd16(palette + 40 + i * 2) & 0x7FFF);
    }
    mark_changes(f);
    f->back = !front;
    {
        /* The height byte the fill wrote beside the sheet: blank rows under the
         * resting pose, the member the engine's own LoadPokemonSpriteYOffset reads
         * (species * 4 + face + male). It seats the pose in whatever frame this
         * composes into, so the engine's feet land where the fill put them. */
        u8 b = 0;

        f->hasByte = pc_modfs_member_read(HEIGHT_NARC, (unsigned)species * 4 + (front ? 2 : 0) + (female ? 0 : 1), &b, 0, 1);
        f->byte = b;
    }
    f->boxed = 0;
    place(f, FRAME, FRAME);
    f->t = 0;
    f->loaded = 1;
    return 1;
}

/* Lay the picture at the slot's current tick into the first pair of a
 * freshly read sheet, both halves. 1 when done; 0 when the fill has no cells
 * for this member, and the caller plays the baked strip instead. */
int openmmo_blackanim_lay(int slot, int character, u8 *raw, u32 bytes, int fw, int fh)
{
    struct face *f;
    int x, y, cx0, cy0;

    if (slot < 0 || slot >= MAX_MON_SPRITES || raw == NULL || bytes < PAIR_BYTES)
        return 0;
    if (fw < FRAME || fw > FRAME_MAX_W) fw = FRAME;
    if (fh < FRAME || fh > FRAME_MAX_H) fh = FRAME;
    f = &s_face[slot];
    if (f->character != character || !f->loaded) {
        if (!load(f, character))
            return 0;
    }
    if (f->fw != fw || f->fh != fh)
        place(f, fw, fh);
    compose(f, f->t);
    frame_of(f);
#ifndef OPENMMO_BLACKANIM_HARNESS
    {
        /* The whole picture, for the page copy of a layout wider than the pair. */
        extern void openmmo_spriteframe_composed_set(int slot, const unsigned char *pix, int w, int h);

        openmmo_spriteframe_composed_set(slot, s_frame, fw, fh);
    }
#endif
    /*
     * The pair keeps an 80x80 picture whatever the frame: the cartridge's own layout copies
     * it, and the battle's OBJ mirror re-tiles it for the copies the move animations build.
     */
    cx0 = (fw - FRAME) / 2;
    cy0 = fh - FRAME;
    for (y = 0; y < FRAME; y++) {
        u8 *row = raw + y * ROW_BYTES;

        memset(row, 0, ROW_BYTES);
        for (x = 0; x < FRAME; x++) {
            u8 v = s_frame[(cy0 + y) * fw + cx0 + x];

            if (!v)
                continue;
            if (x & 1) {
                row[x >> 1] |= (u8)(v << 4);
                row[HALF_BYTES + (x >> 1)] |= (u8)(v << 4);
            } else {
                row[x >> 1] |= v;
                row[HALF_BYTES + (x >> 1)] |= v;
            }
        }
    }
    return 1;
}

/* Whether the pose at f->t reaches under the resting feet. */
static int dips_below_seat(struct face *f)
{
    int x, y;

    compose(f, f->t);
    frame_of(f);
    for (y = f->seat; y < f->fh; y++)
        for (x = 0; x < f->fw; x++)
            if (s_frame[y * f->fw + x])
                return 1;
    return 0;
}

int openmmo_blackanim_tick(int slot, int partial)
{
    struct face *f;

    if (slot < 0 || slot >= MAX_MON_SPRITES)
        return 0;
    f = &s_face[slot];
    if (!f->loaded || f->period <= 1)
        return 0;
    /*
     * A partial draw, the faint's sink, a reveal, shows a window of the frame that ends at
     * the feet.
     */
    if (partial) {
        if (f->held)
            return 0;
        f->held = 1;
        if (f->t == 0 || !dips_below_seat(f))
            return 0;
        f->t = 0;
        return 2;
    }
    f->held = 0;
    f->t = (f->t + 1) % f->period;
    return f->change ? f->change[f->t] : 0;
}
