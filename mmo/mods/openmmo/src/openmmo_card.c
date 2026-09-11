/* Whose trainer card, whose badges, and whose face on it. */

#include <nitro.h>
#include <nnsys.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/graphics.h"
#include "constants/heap.h"
#include "heap.h"
#include "narc.h"
#include "pokemon.h"

#include "../../../include/appearance.h"
#include "../../../include/client.h"
#include "../../../include/endpoint.h"
#include "../../../include/hud_channel.h"

/* The engine's TRAINER_APPEARANCE_* order (include/appearance.h), by the
 * catalog name of the body each one draws. */
static const char *const s_appearances[16] = {
    "school_kid_m", "bug_catcher", "ace_trainer_m", "roughneck",
    "ruin_maniac", "black_belt", "rich_boy", "psychic",
    "lass", "battle_girl", "beauty", "ace_trainer_f",
    "idol", "socialite", "cowgirl", "lady",
};
#define TRAINER_APPEARANCE_DEFAULT_ID (-1)
#define BADGES_PER_REGION 8
#define PAGE_BADGES 16          /* Johto's eight, then Kanto's */
#define CARD_REGIONS 3

/* Where HeartGold stands badge i on its page, as sprite centres: its
 * overlay 51's table at 0x021E801C, read by ov51_021E7AF4 (pokeheartgold
 * asm/overlay_trainer_card_main.s). Four across a band, two bands a region,
 * the Johto band above the Kanto one. */
static const struct { u8 x, y; } s_badge_at[PAGE_BADGES] = {
    { 0x60, 0x30 }, { 0x90, 0x30 }, { 0xC0, 0x30 }, { 0xF0, 0x30 },
    { 0x60, 0x58 }, { 0x90, 0x58 }, { 0xC0, 0x58 }, { 0xF0, 0x58 },
    { 0x60, 0x88 }, { 0x90, 0x88 }, { 0xC0, 0x88 }, { 0xF0, 0x88 },
    { 0x60, 0xB0 }, { 0x90, 0xB0 }, { 0xC0, 0xB0 }, { 0xF0, 0xB0 },
};

/* The members the porter appended, by the keys card_badges.txt names them
 * with (portmap.HG_CARD_PAGE). -1 until read, and -1 where a package has
 * no such row. */
static const char *const s_art_keys[] = {
    "page_tiles", "page_screen", "page_palette",
    "badge_char", "badge_pal", "badge_cell", "badge_anim",
};
#define ART_COUNT ((int)(sizeof s_art_keys / sizeof s_art_keys[0]))

static openmmo_client *s_client;
static int s_region = OPENMMO_HUD_CARD_SINNOH;
static int s_art_loaded;
static int s_art[ART_COUNT];
static int s_page_said;     /* the missing-member line, once a card */

static void load_art(void)
{
    const char *dir = getenv("PC_MODS_DIR");
    const char *mods = getenv("PC_MODS");
    char list[256];
    char *name, *save = NULL;
    int i;

    s_art_loaded = 1;
    for (i = 0; i < ART_COUNT; i++)
        s_art[i] = -1;
    if (dir == NULL || mods == NULL)
        return;
    snprintf(list, sizeof list, "%s", mods);
    for (name = strtok_r(list, ",", &save); name != NULL;
         name = strtok_r(NULL, ",", &save)) {
        char path[512];
        FILE *f;
        char key[24];
        int member;

        snprintf(path, sizeof path, "%s/%s/.cooked/generated/card_badges.txt",
                 dir, name);
        f = fopen(path, "r");
        if (f == NULL)
            continue;
        while (fscanf(f, "%23s %d", key, &member) == 2) {
            for (i = 0; i < ART_COUNT; i++)
                if (strcmp(key, s_art_keys[i]) == 0)
                    s_art[i] = member;
        }
        fclose(f);
    }
}

/* The archive member behind one of the page's keys, or -1. */
int openmmo_card_member(const char *key)
{
    int i;

    if (!s_art_loaded)
        load_art();
    for (i = 0; i < ART_COUNT; i++)
        if (strcmp(key, s_art_keys[i]) == 0)
            return s_art[i];
    return -1;
}

/* Whether the card being shown is a ported region's and the package holds
 * the whole page: the case's loaders draw HeartGold's page on a yes. */
int openmmo_card_page_ready(void)
{
    int i;

    if (s_region == OPENMMO_HUD_CARD_SINNOH)
        return 0;
    if (!s_art_loaded)
        load_art();
    for (i = 0; i < ART_COUNT; i++)
        if (s_art[i] < 0) {
            if (!s_page_said)
                printf("openmmo: the card: this package has no %s; the case stands empty\n",
                       s_art_keys[i]);
            s_page_said = 1;
            return 0;
        }
    return 1;
}

/* Where the page stands badge `index`, 0..15. */
void openmmo_card_badge_at(int index, int *x, int *y)
{
    if (index < 0 || index >= PAGE_BADGES)
        index = 0;
    *x = s_badge_at[index].x;
    *y = s_badge_at[index].y;
}

void openmmo_card_attach(openmmo_client *c)
{
    s_client = c;
    s_region = OPENMMO_HUD_CARD_SINNOH;
}

/* The start menu's direct entry, with the screen command's slot byte. */
void openmmo_card_set_region(int region)
{
    /* A release is a Sinnoh client (endpoint.h): its card is this game's
     * case and nothing else, whatever the command's slot byte says. */
    if (region == OPENMMO_HUD_CARD_JOHTO_KANTO && !openmmo_dev_features()) {
        printf("openmmo: the Johto and Kanto card is not in this build\n");
        region = OPENMMO_HUD_CARD_SINNOH;
    }
    s_region = region == OPENMMO_HUD_CARD_JOHTO_KANTO ? OPENMMO_HUD_CARD_JOHTO_KANTO
                                                     : OPENMMO_HUD_CARD_SINNOH;
    s_page_said = 0;
    printf("openmmo: the %s card\n",
           s_region == OPENMMO_HUD_CARD_SINNOH ? "Sinnoh" : "Johto and Kanto");
}

int openmmo_card_region(void)
{
    return s_region;
}

/* Whether badge `index` of the card being shown is held, off the world
 * state: the case's slot for Sinnoh (never asked; those are TrainerInfo's
 * bits), the page's sixteen for the ported regions. */
int openmmo_card_badge_held(int index)
{
    const openmmo_world_state *ws;
    int i, want;

    if (s_client == NULL || index < 0)
        return 0;
    if (s_region == OPENMMO_HUD_CARD_SINNOH) {
        if (index >= BADGES_PER_REGION)
            return 0;
        want = index;
    } else {
        if (index >= PAGE_BADGES)
            return 0;
        want = BADGES_PER_REGION + index;
    }
    ws = openmmo_client_world_state(s_client);
    if (ws == NULL || !ws->valid)
        return 0;
    for (i = 0; i < ws->badge_count && i < MMO_WS_BADGE_MAX; i++)
        if (ws->badges[i] == want)
            return 1;
    return 0;
}

/*
 * A ported script's CheckBadge, answered from the world state: badge `badge` is one of the
 * source region's sixteen (Johto's eight, then Kanto's), which sit after Sinnoh's eight in the
 * wire's numbering.
 */
int openmmo_ported_badge(int badge, int *held)
{
    extern int openmmo_on_ported_map(void);
    const openmmo_world_state *ws;
    int i, want;

    if (!openmmo_on_ported_map() || badge < 0 || badge >= PAGE_BADGES)
        return 0;
    *held = 0;
    if (s_client == NULL)
        return 1;
    ws = openmmo_client_world_state(s_client);
    if (ws == NULL || !ws->valid)
        return 1;
    want = BADGES_PER_REGION + badge;
    for (i = 0; i < ws->badge_count && i < MMO_WS_BADGE_MAX; i++)
        if (ws->badges[i] == want) {
            *held = 1;
            break;
        }
    return 1;
}

/* How many of the region's badges are held: the card's own count, for the
 * ported regions the sixteen together. */
int openmmo_card_badge_count(void)
{
    const openmmo_world_state *ws;
    int i, n = 0;

    if (s_client == NULL)
        return 0;
    ws = openmmo_client_world_state(s_client);
    if (ws == NULL || !ws->valid)
        return 0;
    for (i = 0; i < ws->badge_count && i < MMO_WS_BADGE_MAX; i++) {
        int b = ws->badges[i];

        if (s_region == OPENMMO_HUD_CARD_SINNOH ? b < BADGES_PER_REGION
                                                : b >= BADGES_PER_REGION
                                                      && b < BADGES_PER_REGION * CARD_REGIONS)
            n++;
    }
    return n;
}

/* The engine's appearance for the body the player picked, or the default,
 * which is the card's own drawing of the player, and the case's cue to ask
 * openmmo_card_portrait for a picture of whatever else was picked. */
int openmmo_card_appearance(void)
{
    const mmo_appearance *body;
    int gfx, i;

    if (s_client == NULL)
        return TRAINER_APPEARANCE_DEFAULT_ID;
    gfx = openmmo_client_body_gfx(s_client);
    body = mmo_appearance_by_gfx(gfx);
    if (body == NULL || body->name == NULL)
        return TRAINER_APPEARANCE_DEFAULT_ID;
    for (i = 0; i < 16; i++)
        if (strcmp(body->name, s_appearances[i]) == 0)
            return i;
    return TRAINER_APPEARANCE_DEFAULT_ID;
}

/* --- the portrait -------------------------------------------------------- */

#define PORTRAIT_TILES_W  10
#define PORTRAIT_TILES_H  11
#define PORTRAIT_W        (PORTRAIT_TILES_W * 8)   /* the face box: 80 x 88 */
#define PORTRAIT_H        (PORTRAIT_TILES_H * 8)
#define PORTRAIT_ART      80              /* the front's own square */
#define PORTRAIT_TILE     64              /* the face layer is 8bpp */
#define PORTRAIT_COLOURS  4               /* the palette block a face lands in */
#define FRONT_TILE        32              /* a 4bpp tile */

/* An OAM read out: where it sits relative to the face's top-left corner
 * before any nudge, how many tiles across and down, and its first one. */
typedef struct {
    int x, y, tw, th, first;
} portrait_oam_box;

/* The block handed to the case: it frees this pointer, so the character data
 * the case reads sits at its head and the pixels behind it. */
typedef struct {
    NNSG2dCharacterData head;
    u8 pixels[PORTRAIT_TILES_W * PORTRAIT_TILES_H * PORTRAIT_TILE];
} card_portrait;

static void portrait_pixel(u8 *pixels, int x, int y, u8 colour)
{
    int tile;

    /* Colour 0 is the picture's own transparency and is left alone, which is
     * also what the hardware would do with an OAM lying over another. */
    if (x < 0 || x >= PORTRAIT_W || y < 0 || y >= PORTRAIT_H || colour == 0)
        return;
    tile = (y / 8) * PORTRAIT_TILES_W + (x / 8);
    pixels[tile * PORTRAIT_TILE + (y % 8) * 8 + (x % 8)] =
        (u8)(PLTT_OFFSET(PORTRAIT_COLOURS) / sizeof(u16) + colour);
}

/* One OAM read out of a cell. Answers 0 for a shape the hardware does not
 * have, which is the one attr0 pattern that names no rectangle. */
static int portrait_read_oam(const NNSG2dCellOAMAttrData *oam, int mult,
                             portrait_oam_box *out)
{
    static const u8 sizes[3][4][2] = {
        { { 1, 1 }, { 2, 2 }, { 4, 4 }, { 8, 8 } },
        { { 2, 1 }, { 4, 1 }, { 4, 2 }, { 8, 4 } },
        { { 1, 2 }, { 1, 4 }, { 2, 4 }, { 4, 8 } },
    };
    int shape = (oam->attr0 >> 14) & 3;
    int size = (oam->attr1 >> 14) & 3;
    int y = oam->attr0 & 0xFF;
    int x = oam->attr1 & 0x1FF;

    if (shape == 3)
        return 0;
    if (y >= 128)
        y -= 256;
    if (x >= 256)
        x -= 512;
    /* The OAM's corner is signed from the centre of the front's own square,
     * and the face's pixels start at that square's corner. */
    out->x = x + PORTRAIT_ART / 2;
    out->y = y + PORTRAIT_ART / 2;
    out->tw = sizes[shape][size][0];
    out->th = sizes[shape][size][1];
    out->first = (oam->attr2 & 0x3FF) * mult;
    return 1;
}

/* One OAM blitted into the face's tiles, `dx`/`dy` past where it sits. */
static void portrait_blit(u8 *pixels, const u8 *front, u32 frontSize,
                          const portrait_oam_box *box, int dx, int dy)
{
    int tx, ty, row, col;

    for (ty = 0; ty < box->th; ty++) {
        for (tx = 0; tx < box->tw; tx++) {
            int index = box->first + ty * box->tw + tx;
            const u8 *tile = front + index * FRONT_TILE;

            if ((u32)((index + 1) * FRONT_TILE) > frontSize)
                continue;
            for (row = 0; row < 8; row++) {
                for (col = 0; col < 8; col += 2) {
                    u8 two = tile[row * 4 + col / 2];
                    int px = box->x + dx + tx * 8 + col;
                    int py = box->y + dy + ty * 8 + row;

                    portrait_pixel(pixels, px, py, (u8)(two & 0xF));
                    portrait_pixel(pixels, px + 1, py, (u8)(two >> 4));
                }
            }
        }
    }
}

/* The trainer class whose front stands for the body the player picked, or -1
 * for the two player bodies, the card has its own drawing of those, and it
 * is the one the official client puts on a player's card. */
static int portrait_class(void)
{
    extern int openmmo_body_trainer_class(int gfx);
    int gfx;

    if (s_client == NULL)
        return -1;
    gfx = openmmo_client_body_gfx(s_client);
    if (gfx == MMO_APPEAR_GFX_PLAYER_M || gfx == MMO_APPEAR_GFX_PLAYER_F)
        return -1;
    return openmmo_body_trainer_class(gfx);
}

/* The card's picture of the body the player picked, on the case's heap, with
 * its sixteen colours loaded where the card wants a face. NULL leaves the
 * case drawing what it drew before. The caller owns the block; the case frees
 * it beside the picture it loads itself. */
void *openmmo_card_portrait(NNSG2dCharacterData **out)
{
    TrainerClassGraphicIndex art;
    NNSG2dCharacterData *front;
    NNSG2dCellDataBank *bank;
    NNSG2dPaletteData *palette;
    const NNSG2dCellData *cell;
    card_portrait *portrait = NULL;
    void *frontBuf = NULL, *cellBuf = NULL, *paletteBuf = NULL;
    NARC *narc;
    int cls = portrait_class();
    int mult, i;
    int boxes = 0, left = 0, top = 0, right = 0, bottom = 0, dx = 0, dy = 0;

    /* A composed look carries the card face its own game drew, HeartGold's
     * picture of Ethan or Lyra, Black's front of Hilbert or Hilda, as
     * sixteen colours and the face box's 110 tiles (openmmo_look.c), so
     * nothing is rebuilt from a front here. */
    if (out != NULL) {
        extern int openmmo_look_card_face(int look, unsigned short *colours,
                                          unsigned char *tiles, unsigned tilesLen);
        extern int openmmo_look_local(void);
        int look = openmmo_look_local();
        u16 colours[16];

        portrait = look >= 0 ? Heap_Alloc(HEAP_ID_TRAINER_CASE, sizeof(card_portrait)) : NULL;
        if (portrait != NULL) {
            memset(portrait, 0, sizeof *portrait);
            if (openmmo_look_card_face(look, colours, portrait->pixels,
                                       sizeof portrait->pixels)) {
                u32 k;

                for (k = 0; k < sizeof portrait->pixels; k++) {
                    if (portrait->pixels[k] != 0)
                        portrait->pixels[k] = (u8)(PLTT_OFFSET(PORTRAIT_COLOURS) / sizeof(u16)
                                                   + (portrait->pixels[k] & 0xF));
                }
                portrait->head.H = PORTRAIT_TILES_H;
                portrait->head.W = PORTRAIT_TILES_W;
                portrait->head.pixelFmt = GX_TEXFMT_PLTT256;
                portrait->head.mappingType = GX_OBJVRAMMODE_CHAR_2D;
                portrait->head.characterFmt = NNS_G2D_CHARACTER_FMT_CHAR;
                portrait->head.szByte = sizeof portrait->pixels;
                portrait->head.pRawData = portrait->pixels;
                DC_FlushRange(colours, sizeof colours);
                GXS_LoadBGPltt(colours, PLTT_OFFSET(PORTRAIT_COLOURS), sizeof colours);
                *out = &portrait->head;
                return portrait;
            }
            Heap_Free(portrait);
            portrait = NULL;
        }
    }

    if (cls < 0 || out == NULL)
        return NULL;
    SpriteSystem_SetTrainerClassGraphicsIndex((enum TrainerClass)cls,
                                              FACE_FRONT, &art);
    narc = NARC_ctor(art.narcID, HEAP_ID_TRAINER_CASE);
    if (narc == NULL)
        return NULL;

    frontBuf = NARC_AllocAndReadWholeMember(narc, art.tiles, HEAP_ID_TRAINER_CASE);
    cellBuf = NARC_AllocAndReadWholeMember(narc, art.cells, HEAP_ID_TRAINER_CASE);
    paletteBuf = NARC_AllocAndReadWholeMember(narc, art.palette, HEAP_ID_TRAINER_CASE);
    if (frontBuf == NULL || cellBuf == NULL || paletteBuf == NULL
        || !NNS_G2dGetUnpackedCharacterData(frontBuf, &front)
        || !NNS_G2dGetUnpackedCellBank(cellBuf, &bank)
        || !NNS_G2dGetUnpackedPaletteData(paletteBuf, &palette)
        || bank->numCells < 1) {
        printf("openmmo: the card: trainer class %d has no front to draw\n", cls);
        goto done;
    }
    /* A 2D bank indexes a VRAM grid rather than a run of tiles, and no
     * trainer class is one; a picture built from one would be scrambled, so
     * it is refused and the card keeps its own. */
    if (bank->mappingMode > NNS_G2D_CHARACTERMAPPING_1D_256) {
        printf("openmmo: the card: trainer class %d maps its tiles a way this"
               " does not read\n", cls);
        goto done;
    }
    mult = 1 << (int)bank->mappingMode;

    portrait = Heap_Alloc(HEAP_ID_TRAINER_CASE, sizeof(card_portrait));
    if (portrait == NULL)
        goto done;
    memset(portrait, 0, sizeof *portrait);
    portrait->head.H = PORTRAIT_TILES_H;
    portrait->head.W = PORTRAIT_TILES_W;
    portrait->head.pixelFmt = GX_TEXFMT_PLTT256;
    portrait->head.mappingType = GX_OBJVRAMMODE_CHAR_2D;
    portrait->head.characterFmt = NNS_G2D_CHARACTER_FMT_CHAR;
    portrait->head.szByte = sizeof portrait->pixels;
    portrait->head.pRawData = portrait->pixels;

    /* Where the cell's pose sits, and the least nudge that brings the whole of
     * it into the face. */
    cell = NNS_G2dGetCellDataByIdx(bank, 0);
    for (i = 0; cell != NULL && i < cell->numOAMAttrs; i++) {
        portrait_oam_box box;

        if (!portrait_read_oam(&cell->pOamAttrArray[i], mult, &box))
            continue;
        if (boxes == 0) {
            left = box.x;
            top = box.y;
            right = box.x + box.tw * 8;
            bottom = box.y + box.th * 8;
        } else {
            if (box.x < left)
                left = box.x;
            if (box.y < top)
                top = box.y;
            if (box.x + box.tw * 8 > right)
                right = box.x + box.tw * 8;
            if (box.y + box.th * 8 > bottom)
                bottom = box.y + box.th * 8;
        }
        boxes++;
    }
    if (left < 0)
        dx = -left;
    else if (right > PORTRAIT_W)
        dx = PORTRAIT_W - right;
    if (top < 0)
        dy = -top;
    else if (bottom > PORTRAIT_H)
        dy = PORTRAIT_H - bottom;

    for (i = 0; cell != NULL && i < cell->numOAMAttrs; i++) {
        portrait_oam_box box;

        if (portrait_read_oam(&cell->pOamAttrArray[i], mult, &box))
            portrait_blit(portrait->pixels, front->pRawData, front->szByte,
                          &box, dx, dy);
    }

    /* The face's own colours, where the card loads a face's. */
    DC_FlushRange(palette->pRawData, PALETTE_SIZE_BYTES);
    GXS_LoadBGPltt(palette->pRawData, PLTT_OFFSET(PORTRAIT_COLOURS),
                   PALETTE_SIZE_BYTES);
    *out = &portrait->head;

done:
    if (frontBuf != NULL)
        Heap_Free(frontBuf);
    if (cellBuf != NULL)
        Heap_Free(cellBuf);
    if (paletteBuf != NULL)
        Heap_Free(paletteBuf);
    NARC_dtor(narc);
    return portrait;
}
