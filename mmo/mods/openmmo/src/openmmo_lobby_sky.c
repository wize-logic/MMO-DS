/* The drifting sky the character lobby sits in front of.
 *
 * BG3 is the sky: an eight-bit-per-pixel layer whose twenty-four tiles are one
 * flat row of colour each, so the picture is a vertical gradient held entirely
 * in the palette, 144 steps a panel, and the two panels take the two halves
 * of one 288-step ramp so a stacked window is one sky rather than two. BG2 is
 * the clouds: a full 256x256 four-bit layer, alpha-blended over BG3 by the
 * hardware, scrolled a fraction of a pixel a frame so it is never quite still.
 *
 * Nothing here is an asset. Both layers are generated at setup out of a fixed
 * seed, which is why there is no NARC to load, no licence to carry, and why the
 * clouds tile seamlessly on a torus, a scroll that wraps at 256 has to.
 *
 * PALETTE MAP, and it is why the numbers below are what they are. The lobby's
 * windows own 4bpp palettes 0..3 (font, then the two standard frames) and the
 * lower screen's message box owns palette 14. Entry 64..79 is palette 4, taken
 * here for the clouds, and 80..223 is the sky's own run of colours. Nothing
 * writes into anyone else's sixteen.
 */

#include <nitro.h>
#include <string.h>

#include "constants/heap.h"
#include "bg_window.h"
#include "gx_layers.h"
#include "heap.h"

void openmmo_sky_setup(BgConfig *bgConfig, enum HeapID heapID);
void openmmo_sky_tick(BgConfig *bgConfig);
void openmmo_sky_teardown(BgConfig *bgConfig);

#define SKY_GRAD_LO   80    /* first palette entry the gradient owns */
#define SKY_GRAD_N    144   /* how many; entry 224 starts the message box's */
#define SKY_GRAD_TILES 24   /* one per tile row of a 192-line screen */
#define SKY_CLOUD_PAL 4

#define SKY_MAP_TILES 32    /* a 256x256 layer is 32x32 tiles */
#define SKY_MAP_BYTES (SKY_MAP_TILES * SKY_MAP_TILES * 2)

/* Density is carried in eighths-of-a-full-lobe so it fits a byte: an
 * ellipse contributes 128 at its centre and nothing at its rim. */
#define SKY_DENS_FLOOR 13   /* below this the pixel is sky, not cloud */
#define SKY_DENS_SPAN  205  /* the run above the floor the six tints cover */
#define SKY_TINTS      6

#define SKY_ELLIPSES   128
#define SKY_GROUPS     16

#define SKY_BLEND_EVA  9
#define SKY_BLEND_EVB  7

/* Sixteenths of a pixel a frame: one pixel every sixteen, so the sky crosses
 * itself in a little over a minute. Fast enough that the screen is never
 * quite still, slow enough that it is not what you are looking at. */
#define SKY_DRIFT      1

typedef struct SkyEllipse {
    s16 cx, cy;
    u8 rx, ry;
    u8 weight; /* 128 is full strength */
} SkyEllipse;

typedef struct SkyRng {
    u32 state;
} SkyRng;

static SkyEllipse sSkyEllipses[SKY_ELLIPSES];
static int sSkyEllipseCount;
static int sSkyDrift;
static int sSkyLive;

/*
 * The gradient, as five-bit stops over one ramp that spans both panels: the upper screen takes
 * its first half and the lower screen its second, so a window that stacks the two reads as one
 * sky rather than as the same sky twice.
 */
static const struct {
    u16 at;     /* position in the 2 * SKY_GRAD_N ramp */
    u8 r, g, b; /* 0..31 */
} sSkyStops[] = {
    {   0,  3,  9, 25 },
    { 130,  8, 18, 31 },
    { 287, 22, 29, 31 },
};

/* Cloud tints, rim first. The rim is a light blue rather than a white so the
 * hardware blend has something soft to fade into at the edge of a lobe. */
static const struct {
    u8 r, g, b;
} sSkyTints[SKY_TINTS] = {
    { 14, 20, 29 }, { 18, 23, 30 }, { 22, 26, 31 },
    { 26, 29, 31 }, { 29, 30, 31 }, { 31, 31, 31 },
};

static u32 SkyRng_Next(SkyRng *rng)
{
    rng->state = rng->state * 1103515245u + 12345u;
    return (rng->state >> 16) & 0x7FFF;
}

static void Sky_AddEllipse(int cx, int cy, int rx, int ry, int weight)
{
    SkyEllipse *e;

    if (sSkyEllipseCount >= SKY_ELLIPSES)
        return;
    if (rx < 1 || ry < 1 || rx > 63 || ry > 63)
        return;
    e = &sSkyEllipses[sSkyEllipseCount++];
    e->cx = (s16)(cx & 255);
    e->cy = (s16)(cy & 255);
    e->rx = (u8)rx;
    e->ry = (u8)ry;
    e->weight = (u8)weight;
}

/*
 * Fair-weather cumulus: a wide flat slab for the base, then a row of lobes along it, each
 * lifted by half its own radius and each wide enough to overlap its neighbours, a lobe
 * narrower than the gap it spans draws a string of beads rather than a cloud.
 */
static void Sky_BuildClouds(void)
{
    SkyRng rng = { 0x2468 };
    int g;

    sSkyEllipseCount = 0;
    for (g = 0; g < SKY_GROUPS; g++) {
        int gx = (int)(SkyRng_Next(&rng) % 256);
        int gy = (int)(SkyRng_Next(&rng) % 256);
        int roll = (int)(SkyRng_Next(&rng) % 6);
        int span, rad, lobes, i;

        if (roll == 0)
            span = 6 + (int)(SkyRng_Next(&rng) % 6);
        else if (roll < 4)
            span = 14 + (int)(SkyRng_Next(&rng) % 13);
        else
            span = 30 + (int)(SkyRng_Next(&rng) % 17);

        rad = span / 2;
        if (rad < 4)
            rad = 4;
        Sky_AddEllipse(gx, gy, span + rad, rad * 2 / 3, 115);

        lobes = 3 + (int)(SkyRng_Next(&rng) % 3);
        for (i = 0; i < lobes; i++) {
            int off = -span + (2 * span * i) / (lobes - 1)
                + (int)(SkyRng_Next(&rng) % 5) - 2;
            int r = rad - (int)(SkyRng_Next(&rng) % (rad / 3 + 1));

            Sky_AddEllipse(gx + off, gy - r / 2, r, r * 4 / 5, 128);
        }
    }
}

/* One eight-line strip of the cloud field, wrapped both ways. `top` is the
 * strip's first line in the 256-line torus. */
static void Sky_RenderStrip(u8 *dens, int top)
{
    int i;

    memset(dens, 0, 256 * 8);
    for (i = 0; i < sSkyEllipseCount; i++) {
        const SkyEllipse *e = &sSkyEllipses[i];
        int rx = e->rx, ry = e->ry;
        int wrap;

        for (wrap = -256; wrap <= 256; wrap += 256) {
            int cy = e->cy + wrap;
            int y0 = cy - ry, y1 = cy + ry;
            int y;

            if (y1 < top || y0 >= top + 8)
                continue;
            if (y0 < top)
                y0 = top;
            if (y1 >= top + 8)
                y1 = top + 7;
            for (y = y0; y <= y1; y++) {
                int dy = y - cy;
                int quota = 4096 - (dy * dy * 4096) / (ry * ry);
                u8 *row = dens + (y - top) * 256;
                int dx;

                if (quota <= 0)
                    continue;
                for (dx = -rx; dx <= rx; dx++) {
                    int q = quota - (dx * dx * 4096) / (rx * rx);
                    int at, add;

                    if (q <= 0)
                        continue;
                    add = (q * e->weight) >> 12; /* q/32 at full weight */
                    at = (e->cx + dx) & 255;
                    if (row[at] + add > 255)
                        row[at] = 255;
                    else
                        row[at] = (u8)(row[at] + add);
                }
            }
        }
    }
}

static void Sky_LoadPalettes(u8 bgLayer, enum HeapID heapID, int panel)
{
    GXRgb *pal = Heap_Alloc(heapID, sizeof(GXRgb) * SKY_GRAD_N);
    GXRgb clouds[16];
    int i, s;

    if (pal == NULL)
        return;
    for (i = 0; i < SKY_GRAD_N; i++) {
        int at = panel * SKY_GRAD_N + i;
        int r = sSkyStops[0].r, g = sSkyStops[0].g, b = sSkyStops[0].b;

        for (s = 0; s + 1 < (int)NELEMS(sSkyStops); s++) {
            int a = sSkyStops[s].at, z = sSkyStops[s + 1].at;

            if (at >= a && at <= z) {
                int t = at - a, d = z - a;

                r = sSkyStops[s].r + (sSkyStops[s + 1].r - sSkyStops[s].r) * t / d;
                g = sSkyStops[s].g + (sSkyStops[s + 1].g - sSkyStops[s].g) * t / d;
                b = sSkyStops[s].b + (sSkyStops[s + 1].b - sSkyStops[s].b) * t / d;
                break;
            }
        }
        pal[i] = GX_RGB(r, g, b);
    }
    Bg_LoadPalette(bgLayer, pal, sizeof(GXRgb) * SKY_GRAD_N,
                   SKY_GRAD_LO * sizeof(GXRgb));
    Heap_Free(pal);

    memset(clouds, 0, sizeof clouds);
    for (i = 0; i < SKY_TINTS; i++)
        clouds[i + 1] = GX_RGB(sSkyTints[i].r, sSkyTints[i].g, sSkyTints[i].b);
    Bg_LoadPalette(bgLayer, clouds, sizeof clouds,
                   SKY_CLOUD_PAL * 16 * sizeof(GXRgb));
}

static void Sky_LoadGradient(BgConfig *bgConfig, u8 bgLayer, enum HeapID heapID)
{
    u8 *tiles = Heap_Alloc(heapID, SKY_GRAD_TILES * 64);
    u16 *map = Heap_Alloc(heapID, SKY_MAP_BYTES);
    int t, row, col;

    if (tiles == NULL || map == NULL) {
        if (tiles != NULL)
            Heap_Free(tiles);
        if (map != NULL)
            Heap_Free(map);
        return;
    }
    for (t = 0; t < SKY_GRAD_TILES; t++) {
        for (row = 0; row < 8; row++) {
            int line = t * 8 + row;
            u8 index = (u8)(SKY_GRAD_LO + line * SKY_GRAD_N / 192);

            memset(tiles + t * 64 + row * 8, index, 8);
        }
    }
    Bg_LoadTiles(bgConfig, bgLayer, tiles, SKY_GRAD_TILES * 64, 0);

    for (row = 0; row < SKY_MAP_TILES; row++) {
        u16 tile = (u16)(row < SKY_GRAD_TILES ? row : SKY_GRAD_TILES - 1);

        for (col = 0; col < SKY_MAP_TILES; col++)
            map[row * SKY_MAP_TILES + col] = tile;
    }
    Bg_LoadTilemapBuffer(bgConfig, bgLayer, map, SKY_MAP_BYTES);
    Bg_CopyTilemapBufferToVRAM(bgConfig, bgLayer);

    Heap_Free(map);
    Heap_Free(tiles);
}

static void Sky_LoadClouds(BgConfig *bgConfig, u8 bgLayer, enum HeapID heapID)
{
    u8 *dens = Heap_Alloc(heapID, 256 * 8);
    u8 *tiles = Heap_Alloc(heapID, SKY_MAP_TILES * 32);
    u16 *map = Heap_Alloc(heapID, SKY_MAP_BYTES);
    int strip, col, row, x;

    if (dens == NULL || tiles == NULL || map == NULL) {
        if (dens != NULL)
            Heap_Free(dens);
        if (tiles != NULL)
            Heap_Free(tiles);
        if (map != NULL)
            Heap_Free(map);
        return;
    }
    for (strip = 0; strip < SKY_MAP_TILES; strip++) {
        Sky_RenderStrip(dens, strip * 8);
        memset(tiles, 0, SKY_MAP_TILES * 32);
        for (row = 0; row < 8; row++) {
            for (x = 0; x < 256; x++) {
                int d = dens[row * 256 + x];
                int level, nibble;

                if (d <= SKY_DENS_FLOOR)
                    continue;
                level = (d - SKY_DENS_FLOOR) * SKY_TINTS / SKY_DENS_SPAN;
                if (level >= SKY_TINTS)
                    level = SKY_TINTS - 1;
                nibble = level + 1;
                col = x >> 3;
                /* 4bpp: two pixels a byte, low nibble first. */
                if (x & 1)
                    tiles[col * 32 + row * 4 + ((x & 7) >> 1)] |= (u8)(nibble << 4);
                else
                    tiles[col * 32 + row * 4 + ((x & 7) >> 1)] |= (u8)nibble;
            }
        }
        Bg_LoadTiles(bgConfig, bgLayer, tiles, SKY_MAP_TILES * 32,
                     strip * SKY_MAP_TILES);
    }
    for (row = 0; row < SKY_MAP_TILES; row++) {
        for (col = 0; col < SKY_MAP_TILES; col++) {
            map[row * SKY_MAP_TILES + col] =
                (u16)((row * SKY_MAP_TILES + col)
                      | TILEMAP_PALETTE_SHIFT(SKY_CLOUD_PAL));
        }
    }
    Bg_LoadTilemapBuffer(bgConfig, bgLayer, map, SKY_MAP_BYTES);
    Bg_CopyTilemapBufferToVRAM(bgConfig, bgLayer);

    Heap_Free(map);
    Heap_Free(tiles);
    Heap_Free(dens);
}

/*
 * Both panels get the same two layers. The lower screen is nearly all background, one
 * message box at the foot of it, so it is the panel the sky is really for; the upper one
 * shows it under and around the cards.
 */
void openmmo_sky_setup(BgConfig *bgConfig, enum HeapID heapID)
{
    static const struct {
        u8 grad, cloud;
        u8 gradScreen, cloudScreen;
    } sPanels[] = {
        { BG_LAYER_MAIN_3, BG_LAYER_MAIN_2,
          GX_BG_SCRBASE_0xe800, GX_BG_SCRBASE_0xf000 },
        { BG_LAYER_SUB_3, BG_LAYER_SUB_2,
          GX_BG_SCRBASE_0xe000, GX_BG_SCRBASE_0xe800 },
    };
    BgTemplate layer = {
        .x = 0,
        .y = 0,
        .bufferSize = SKY_MAP_BYTES,
        .baseTile = 0,
        .screenSize = BG_SCREEN_SIZE_256x256,
        .colorMode = GX_BG_COLORMODE_256,
        .screenBase = GX_BG_SCRBASE_0xe800,
        .charBase = GX_BG_CHARBASE_0x08000,
        .bgExtPltt = GX_BG_EXTPLTT_01,
        .priority = 3,
        .areaOver = 0,
        .mosaic = FALSE,
    };
    unsigned i;

    Sky_BuildClouds();
    for (i = 0; i < NELEMS(sPanels); i++) {
        layer.colorMode = GX_BG_COLORMODE_256;
        layer.charBase = GX_BG_CHARBASE_0x08000;
        layer.screenBase = sPanels[i].gradScreen;
        Bg_InitFromTemplate(bgConfig, sPanels[i].grad, &layer, BG_TYPE_STATIC);

        layer.colorMode = GX_BG_COLORMODE_16;
        layer.charBase = GX_BG_CHARBASE_0x10000;
        layer.screenBase = sPanels[i].cloudScreen;
        Bg_InitFromTemplate(bgConfig, sPanels[i].cloud, &layer, BG_TYPE_STATIC);

        Sky_LoadPalettes(sPanels[i].grad, heapID, (int)i);
        Sky_LoadGradient(bgConfig, sPanels[i].grad, heapID);
        Sky_LoadClouds(bgConfig, sPanels[i].cloud, heapID);
        /* The lower panel starts 192 lines further down the same cloud
         * torus, so the two screens are one continuous sky and not the same
         * dozen clouds printed twice. */
        Bg_SetOffset(bgConfig, sPanels[i].cloud, BG_OFFSET_UPDATE_SET_Y,
                     (int)i * 192);
    }

    /* The clouds are the blend's first target and the sky its second, so a
     * lobe's rim reads as thin cloud rather than as a hard pixel edge. The
     * windows and the trainer sprite are in neither mask and stay opaque. */
    G2_SetBlendAlpha(GX_BLEND_PLANEMASK_BG2,
                     GX_BLEND_PLANEMASK_BG3 | GX_BLEND_PLANEMASK_BD,
                     SKY_BLEND_EVA, SKY_BLEND_EVB);
    G2S_SetBlendAlpha(GX_BLEND_PLANEMASK_BG2,
                      GX_BLEND_PLANEMASK_BG3 | GX_BLEND_PLANEMASK_BD,
                      SKY_BLEND_EVA, SKY_BLEND_EVB);

    GXLayers_EngineAToggleLayers(GX_PLANEMASK_BG2 | GX_PLANEMASK_BG3, 1);
    GXLayers_EngineBToggleLayers(GX_PLANEMASK_BG2 | GX_PLANEMASK_BG3, 1);
    sSkyDrift = 0;
    sSkyLive = 1;
}

void openmmo_sky_tick(BgConfig *bgConfig)
{
    int x;

    if (!sSkyLive)
        return;
    sSkyDrift = (sSkyDrift + SKY_DRIFT) & (256 * 16 - 1);
    x = sSkyDrift / 16;
    Bg_ScheduleScroll(bgConfig, BG_LAYER_MAIN_2, BG_OFFSET_UPDATE_SET_X, x);
    Bg_ScheduleScroll(bgConfig, BG_LAYER_SUB_2, BG_OFFSET_UPDATE_SET_X, x);
}

void openmmo_sky_teardown(BgConfig *bgConfig)
{
    if (!sSkyLive)
        return;
    G2_SetBlendAlpha(GX_BLEND_PLANEMASK_NONE, GX_BLEND_PLANEMASK_NONE, 31, 0);
    G2S_SetBlendAlpha(GX_BLEND_PLANEMASK_NONE, GX_BLEND_PLANEMASK_NONE, 31, 0);
    Bg_FreeTilemapBuffer(bgConfig, BG_LAYER_SUB_2);
    Bg_FreeTilemapBuffer(bgConfig, BG_LAYER_SUB_3);
    Bg_FreeTilemapBuffer(bgConfig, BG_LAYER_MAIN_2);
    Bg_FreeTilemapBuffer(bgConfig, BG_LAYER_MAIN_3);
    sSkyLive = 0;
}
