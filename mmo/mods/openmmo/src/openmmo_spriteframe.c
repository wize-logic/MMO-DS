/*
 * The page a battle draws its mon sprites through, and the frame each
 * of them gets.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pokemon_sprite.h"

#include "openmmo_spriteframe.h"

#define PAGE_PITCH   128 /* bytes a page row is: 256 px at 4bpp */
#define SHEET_FRAME  80  /* a cartridge sheet is two 80x80 frames side by side */
#define SHEET_PITCH  80  /* bytes a raw pair row is */
#define SHADOW_BLOCK 80  /* the shadow sheet copied into the page is 80x80 */
#define COMP_MAX_W   128
#define COMP_MAX_H   88

static const struct openmmo_spriteframe_layout k80 = {
    "80x80",
    80,
    80,
    4,
    { { { 0, 0 }, { 80, 0 } }, { { 0, 80 }, { 80, 80 } }, { { 0, 160 }, { 80, 160 } }, { { 160, 0 }, { 160, 80 } } },
    160,
    160,
};

static const struct openmmo_spriteframe_layout k128 = {
    "128x88",
    128,
    88,
    2,
    { { { 0, 0 }, { 128, 0 } }, { { 0, 88 }, { 128, 88 } }, { { -1, -1 }, { -1, -1 } }, { { -1, -1 }, { -1, -1 } } },
    0,
    176,
};

static const struct openmmo_spriteframe_layout *s_active = &k80;
static const void *s_owner;
/* Which layout each live manager draws through. */
#define MAX_MANAGERS 8
static const void *s_man[MAX_MANAGERS];
static const struct openmmo_spriteframe_layout *s_lay[MAX_MANAGERS];
static int s_checked;
static int s_unplaced_said;

static u8 s_comp[MAX_MON_SPRITES][COMP_MAX_W * COMP_MAX_H];
static int s_comp_w[MAX_MON_SPRITES], s_comp_h[MAX_MON_SPRITES];

static unsigned page_offset(int x, int y)
{
    return (unsigned)y * PAGE_PITCH + (unsigned)x / 2;
}

static unsigned get4(const u8 *base, int pitch, int x, int y)
{
    u8 b = base[y * pitch + (x >> 1)];

    return (x & 1) ? (b >> 4) : (b & 15);
}

static void put4(u8 *base, int pitch, int x, int y, unsigned v)
{
    u8 *p = &base[y * pitch + (x >> 1)];

    if (x & 1) {
        *p = (u8)((*p & 0x0F) | (v << 4));
    } else {
        *p = (u8)((*p & 0xF0) | (v & 0x0F));
    }
}

/*
 * The cartridge's own numbers, out of pokemon_sprite.c before this file: its texture-
 * coordinate table, the byte offset each slot's frame is copied to, the shadow block's offset
 * and the shadow table.
 */
static void check_k80(void)
{
    static const int shipped_uv[4][2][4] = {
        { { 0, 0, 80, 80 }, { 80, 0, 160, 80 } },
        { { 0, 80, 80, 160 }, { 80, 80, 160, 160 } },
        { { 0, 160, 80, 240 }, { 80, 160, 160, 240 } },
        { { 160, 0, 240, 80 }, { 160, 80, 240, 160 } },
    };
    static const unsigned shipped_copy[4][2] = {
        { 0x0000, 0x0028 }, { 0x2800, 0x2828 }, { 0x5000, 0x5028 }, { 0x0050, 0x2850 }
    };
    static const int shipped_shadow[4][4] = {
        { 160, 160, 224, 176 }, { 160, 160, 224, 176 }, { 160, 176, 224, 192 }, { 160, 192, 224, 208 }
    };
    int s = 0, f = 0, k;
    int uv[4];

    if (s_checked) {
        return;
    }
    s_checked = 1;
    for (s = 0; s < 4; s++) {
        for (f = 0; f < 2; f++) {
            openmmo_spriteframe_uv(NULL, s, f, uv);
            for (k = 0; k < 4; k++) {
                if (uv[k] != shipped_uv[s][f][k]) {
                    goto bad;
                }
            }
            if (page_offset(k80.pos[s][f][0], k80.pos[s][f][1]) != shipped_copy[s][f]) {
                goto bad;
            }
        }
    }
    for (s = 0; s < 4; s++) {
        openmmo_spriteframe_shadow_uv(NULL, s, uv);
        for (k = 0; k < 4; k++) {
            if (uv[k] != shipped_shadow[s][k]) {
                f = -1;
                goto bad;
            }
        }
    }
    if (page_offset(k80.shadowX, k80.shadowY) != 0x5050) {
        s = f = -2;
        goto bad;
    }
    return;
bad:
    fprintf(stderr, "openmmo: the 80x80 sprite layout disagrees with the cartridge's page (slot %d frame %d)\n", s, f);
    fflush(stderr);
    abort();
}

const struct openmmo_spriteframe_layout *openmmo_spriteframe_of(const void *man)
{
    int i;

    check_k80();
    if (man != NULL) {
        for (i = 0; i < MAX_MANAGERS; i++) {
            if (s_man[i] == man && s_lay[i] != NULL) {
                return s_lay[i];
            }
        }
    }
    return &k80;
}

int openmmo_mon_frame_w(void)
{
    return s_active->frameW;
}

int openmmo_mon_frame_h(void)
{
    return s_active->frameH;
}

void openmmo_spriteframe_uv(const void *man, int slot, int frame, int uv[4])
{
    const struct openmmo_spriteframe_layout *L = openmmo_spriteframe_of(man);
    int x, y;

    if (slot < 0 || slot >= MAX_MON_SPRITES) {
        slot = 0;
    }
    frame = frame ? 1 : 0;
    x = L->pos[slot][frame][0];
    y = L->pos[slot][frame][1];
    if (x < 0) {
        x = y = 0;
    }
    uv[0] = x;
    uv[1] = y;
    uv[2] = x + L->frameW;
    uv[3] = y + L->frameH;
}

/* The shadow sheet is four 64x16 strips: none and small share the first,
 * medium is the second, large the third, the cartridge's own table. */
void openmmo_spriteframe_shadow_uv(const void *man, int size, int uv[4])
{
    const struct openmmo_spriteframe_layout *L = openmmo_spriteframe_of(man);
    int k = size >= 2 ? size - 1 : 0;

    uv[0] = L->shadowX;
    uv[1] = L->shadowY + 16 * k;
    uv[2] = L->shadowX + 64;
    uv[3] = L->shadowY + 16 * (k + 1);
}

unsigned openmmo_spriteframe_shadow_offset(const void *man)
{
    const struct openmmo_spriteframe_layout *L = openmmo_spriteframe_of(man);

    return page_offset(L->shadowX, L->shadowY);
}

void openmmo_spriteframe_set(void *man, int wide)
{
    PokemonSpriteManager *m = man;
    const struct openmmo_spriteframe_layout *from, *to = wide ? &k128 : &k80;

    if (m == NULL) {
        return;
    }
    from = openmmo_spriteframe_of(m);
    if (from != to && m->charRawData != NULL) {
        /* New() copied the shadow block to the cartridge's place before a layout
         * was chosen; move it to where this one draws it from. */
        static u8 tmp[SHADOW_BLOCK * SHADOW_BLOCK / 2];
        int y;

        for (y = 0; y < SHADOW_BLOCK; y++) {
            memcpy(tmp + y * (SHADOW_BLOCK / 2), m->charRawData + page_offset(from->shadowX, from->shadowY + y), SHADOW_BLOCK / 2);
            memset(m->charRawData + page_offset(from->shadowX, from->shadowY + y), 0, SHADOW_BLOCK / 2);
        }
        for (y = 0; y < SHADOW_BLOCK; y++) {
            memcpy(m->charRawData + page_offset(to->shadowX, to->shadowY + y), tmp + y * (SHADOW_BLOCK / 2), SHADOW_BLOCK / 2);
        }
        m->needLoadChar = TRUE;
    }
    {
        int i, free_i = -1;

        for (i = 0; i < MAX_MANAGERS; i++) {
            if (s_man[i] == man) {
                free_i = i;
                break;
            }
            if (s_man[i] == NULL && free_i < 0) {
                free_i = i;
            }
        }
        if (free_i < 0) {
            printf("openmmo: more than %d sprite managers alive; the layout is not recorded\n", MAX_MANAGERS);
            return;
        }
        s_man[free_i] = man;
        s_lay[free_i] = to;
    }
    s_active = to;
    s_owner = m;
    printf("openmmo: battle sprite layout %s\n", to->name);
}

void openmmo_spriteframe_free(void *man)
{
    int i;

    if (man == NULL) {
        return;
    }
    for (i = 0; i < MAX_MANAGERS; i++) {
        if (s_man[i] == man) {
            s_man[i] = NULL;
            s_lay[i] = NULL;
        }
    }
    if (s_owner == man) {
        s_owner = NULL;
        s_active = &k80;
    }
}

void openmmo_spriteframe_composed_set(int slot, const unsigned char *pix, int w, int h)
{
    if (slot < 0 || slot >= MAX_MON_SPRITES) {
        return;
    }
    if (pix == NULL || w <= 0 || h <= 0 || w > COMP_MAX_W || h > COMP_MAX_H) {
        s_comp_w[slot] = s_comp_h[slot] = 0;
        return;
    }
    memcpy(s_comp[slot], pix, (size_t)w * (size_t)h);
    s_comp_w[slot] = w;
    s_comp_h[slot] = h;
}

int openmmo_spriteframe_composed(int slot, const unsigned char **pix, int *w, int *h)
{
    if (slot < 0 || slot >= MAX_MON_SPRITES || s_comp_w[slot] <= 0) {
        return 0;
    }
    *pix = s_comp[slot];
    *w = s_comp_w[slot];
    *h = s_comp_h[slot];
    return 1;
}

/*
 * The cartridge's copy loop, in frame-local pixels rather than pair bytes: a horizontal flip
 * mirrors the frame, a vertical one its rows, and a mosaic of intensity m repeats the top-left
 * pixel of every cell m bytes wide and 2m rows tall (the cartridge writes that pixel into both
 * nybbles of the cell's first byte and copies the byte and the row along).
 */
static void shown_pixels(const PokemonSpriteTransforms *t, const u8 *pic, u8 *out, int w, int h)
{
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int sx = x, sy = y;

            if (t->flipH) {
                sx = w - 1 - x;
            }
            if (t->flipV) {
                sy = h - 1 - y;
            }
            if (!t->flipH && !t->flipV && t->mosaicIntensity) {
                int mb = t->mosaicIntensity;

                sx = ((x / 2) / mb) * mb * 2;
                sy = (y / (2 * mb)) * (2 * mb);
            }
            out[y * w + x] = pic[sy * w + sx];
        }
    }
}

int openmmo_spriteframe_buffer(void *man, int slot, const unsigned char *rawPair)
{
    PokemonSpriteManager *m = man;
    const struct openmmo_spriteframe_layout *L = openmmo_spriteframe_of(m);
    static u8 pic[COMP_MAX_W * COMP_MAX_H], shown[COMP_MAX_W * COMP_MAX_H];
    const u8 *comp;
    int cw, ch, f, x, y;

    if (L == &k80 || m == NULL || rawPair == NULL || slot < 0 || slot >= MAX_MON_SPRITES) {
        return 0;
    }
    for (f = 0; f < 2; f++) {
        int cx = L->pos[slot][f][0], cy = L->pos[slot][f][1];
        int w, h, dx, dy;

        if (cx < 0) {
            if (!s_unplaced_said) {
                s_unplaced_said = 1;
                printf("openmmo: sprite slot %d is active in the %s layout, which has no place for it\n", slot, L->name);
            }
            return 1;
        }
        if (openmmo_spriteframe_composed(slot, &comp, &cw, &ch) && cw <= L->frameW && ch <= L->frameH) {
            w = cw;
            h = ch;
            memcpy(pic, comp, (size_t)w * (size_t)h);
        } else {
            w = h = SHEET_FRAME;
            for (y = 0; y < h; y++) {
                for (x = 0; x < w; x++) {
                    pic[y * w + x] = (u8)get4(rawPair, SHEET_PITCH, f * SHEET_FRAME + x, y);
                }
            }
        }
        shown_pixels(&m->sprites[slot].transforms, pic, shown, w, h);
        for (y = 0; y < L->frameH; y++) {
            memset(m->charRawData + page_offset(cx, cy + y), 0, (size_t)L->frameW / 2);
        }
        dx = cx + (L->frameW - w) / 2;
        dy = cy + (L->frameH - h);
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                if (shown[y * w + x]) {
                    put4(m->charRawData, PAGE_PITCH, dx + x, dy + y, shown[y * w + x]);
                }
            }
        }
    }
    return 1;
}
