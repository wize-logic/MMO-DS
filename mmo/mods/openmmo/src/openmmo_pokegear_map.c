/*
 * The Pokegear's map card: HeartGold's own map of Johto and Kanto,
 * the player on it, every town's name and its line, and the Fly out of it.
 */

#include <stdio.h>
#include <string.h>

#include "openmmo_pokegear.h"
#include "openmmo_pokegear_tables.h"

#include "constants/graphics.h"
#include "font.h"
#include "generated/fade_types.h"
#include "graphics.h"
#include "gx_layers.h"
#include "heap.h"
#include "screen_fade.h"
#include "system.h"
#include "touch_screen.h"
#include "text.h"

/* pgmap_gra members. */
#define PGMAP_OBJ_PLTT      0
#define PGMAP_MAP_CHAR      10   /* the region's tiles, MAIN_2 */
#define PGMAP_MAP_SCRN      11   /* the region, 47x20 and the variants */
#define PGMAP_DETAIL_CHAR   12   /* the town blocks' tiles, SUB_3 */
#define PGMAP_DETAIL_SCRN   13   /* the town blocks, 4 across */
#define PGMAP_SUB_PLTT      14   /* + skin */
#define PGMAP_MAIN_PLTT     20   /* + skin */
#define PGMAP_FRAME_CHAR    26   /* + skin, MAIN_1 */
#define PGMAP_FRAME_SCRN    32   /* + skin */
#define PGMAP_PANEL_CHAR    50   /* + skin, SUB_2 */
#define PGMAP_PANEL_SCRN    56   /* + skin */

#define PGMAP_MSG_BANK      273
#define PGMAP_MSG_KANTO     0
#define PGMAP_MSG_JOHTO     1
#define PGMAP_MSG_FORMAT    3
#define PGMAP_MSG_CHOOSE    4
#define PGMAP_MSG_FLY_TO    5
#define PGMAP_MSG_CLOSE     6
#define PGMAP_MSG_FLY       7
#define PGMAP_MSG_QUIT      8

#define PGMAP_LOCATION_NAMES_BANK 433


enum {
    PGMAP_STATE_LOAD = 0,
    PGMAP_STATE_HANDLE_INPUT,
    PGMAP_STATE_UNLOAD,
    PGMAP_STATE_FADE_IN,
    PGMAP_STATE_FADE_OUT,
    PGMAP_STATE_FADE_IN_APP,
    PGMAP_STATE_FADE_OUT_APP,
    PGMAP_STATE_FLY_CONTEXT_MENU,
    PGMAP_STATE_QUIT
};

enum {
    PGMAP_SPRITE_MARKER0 = 0,
    PGMAP_SPRITE_MARKER1,
    PGMAP_SPRITE_MARKER2,
    PGMAP_SPRITE_MARKER3,
    PGMAP_SPRITE_GEAR_BATTLE,
    PGMAP_SPRITE_CURSOR,
    PGMAP_SPRITE_PLAYER,
    PGMAP_SPRITE_ROAMER_RAIKOU,
    PGMAP_SPRITE_ROAMER_ENTEI,
    PGMAP_SPRITE_ROAMER_LATIOS,
    PGMAP_SPRITE_ROAMER_LATIAS,
    PGMAP_SPRITE_ALWAYS_END,
    PGMAP_SPRITE_FLY_MENU_11 = PGMAP_SPRITE_ALWAYS_END,
    PGMAP_SPRITE_FLY_MENU_12,
    PGMAP_SPRITE_FLY_MENU_13,
    PGMAP_SPRITE_FLY_MENU_14,
    PGMAP_SPRITE_FLY_MENU_WARPS_BEGIN,
    PGMAP_SPRITE_FLY_MENU_WARPS_END = PGMAP_SPRITE_FLY_MENU_WARPS_BEGIN + PGMAP_NUM_FLYPOINTS,
    PGMAP_SPRITE_MAX = PGMAP_SPRITE_FLY_MENU_WARPS_END
};




typedef struct PokegearMapCursorState {
    fx32 x;
    fx32 y;
    fx32 xRatio;
    fx32 yRatio;
    int affineX;
    int affineY;
    fx32 dxStep;
    fx32 dyStep;
    fx32 fx;        /* unk_20 */
    fx32 fy;        /* unk_24 */
    u16 top;
    u16 bottom;
    u16 left;
    u16 right;
    s16 dx;
    s16 dy;
    s16 destX;
    s16 destY;
} PokegearMapCursorState;

typedef struct {
    const PokegearMapLocationSpec *locationSpec;
    u16 x;
    u16 y;
} PokegearMapSelected;

typedef struct {
    enum HeapID heapID;
    int state;
    int substate;
    u8 curRegion;
    s8 flyDestination;
    PokegearAppData *pokegear;
    PokegearObjectsManager *objManager;
    MessageLoader *msg;
    MessageLoader *names;
    StringTemplate *fmt;
    String *flavorTextString;
    String *regionNameStrings[2];
    String *mapNameString;
    String *chooseDestinationString;
    String *flyToLocationString;
    String *closeString;
    PokegearMenu *menu;
    PokegearMapCursorState cursorSpriteState;
    u16 minXscroll, maxXscroll, minYscroll, maxYscroll;
    s16 matrixX, matrixY;
    u16 mapID;
    u16 playerGender;
    s16 playerX, playerY;
    Coord2S16 cursorPos;
    PokegearMapSelected selectedLoc;
    u8 centerY, centerX;
    s8 yOffset, xOffset;
    u8 fadeStep;
    u8 zoomed : 1;
    u8 mapUnlockLevel : 2;
    u8 requestAffineUpdate : 1;
    u8 moving : 1;         /* draggingMarking: an animation is running */
    u8 zooming : 1;        /* unk_139_1 */
    u8 stepping : 1;       /* unk_139_2 */
    u8 dragging : 1;       /* unk_139_3: the pen holds the map */
    u8 cursorSpeed;
    u8 moveCursorDirection;
    u8 canFlyToGoldenrod : 1;
    u8 canSeeSafariZone : 1;
    u8 isMapSinjoh : 1;
    u8 isMapSSAqua : 1;
    u8 playerShown : 1;
    u8 flyMenuUpdate : 1;
    s16 dragWordX, dragWordY;
    s16 dragStartX, dragStartY;   /* unk_142, unk_144 */
    s16 pixelTop, pixelBottom, pixelLeft, pixelRight;
    u16 lastSelectedMapID;
    int flyChosenIdx;
    void *scrnRaw[4];
    NNSG2dScreenData *scrnPanel;    /* unk_16C */
    NNSG2dScreenData *scrnRegion;   /* unk_170 */
    NNSG2dScreenData *scrnDetail;   /* unk_174 */
    NNSG2dScreenData *scrnFrame;    /* unk_178 */
    Window windows[8];
} PokegearMapAppData;

/* ---- HeartGold's tables ---- */

static const u16 sMapXScrollLimits[] = { 26, 29, 45 };

/* The map panel's right edge in touch-screen pixels; the button column is
 * beside it. */
#define PGMAP_PANEL_RIGHT 200




static const TouchScreenRect sMapTouchRect = { .rect = { 8, 152, 8, 200 } };

static const TouchScreenRect sMapButtonHitboxes[] = {
    { .rect = { 16, 64, 216, 248 } },   /* the Y (markings) button */
    { .rect = { 88, 152, 216, 248 } },  /* the zoom button */
    { .rect = { TOUCHSCREEN_TABLE_TERMINATOR, 0, 0, 0 } },
};

/* ---- forward ---- */
static void map_update_all(PokegearMapAppData *m);
static void map_select_at(PokegearMapAppData *m, u8 x, u8 y);
static void map_print_detail(PokegearMapAppData *m, BOOL panelOnly);
static void map_highlight_selected(PokegearMapAppData *m, u8 on);
static void map_place_fly_points(PokegearMapAppData *m, u8 mode);
static void map_cursor_bounds(PokegearMapAppData *m);
static void map_cursor_to_player_cell(PokegearMapAppData *m);
static void map_scroll_to(PokegearMapAppData *m, u8 zoomed, u16 x0, u16 y0, s16 xIn, s16 yIn);
static void map_init_cursor(PokegearMapAppData *m);
static void map_vblank_affine(PokegearMapAppData *m, PokegearMapCursorState *cs);
static void map_shift_objects(PokegearMapAppData *m, s16 dx, s16 dy);
static void map_cull(PokegearMapAppData *m);
static void map_zoom_button(PokegearMapAppData *m, int button, int state);
static int map_fly_at(PokegearMapAppData *m, u16 x, u16 y);

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static u16 header_of(u16 hgMapId)
{
    return (u16)(POKEGEAR_PORTED_FIRST + hgMapId);
}

static void landmark_name(PokegearMapAppData *m, u16 hgMapId, String *dst)
{
    u32 label = openmmo_pokegear_label(header_of(hgMapId));

    String_Clear(dst);
    if (m->names == NULL || label == 0 || label >= MessageLoader_MessageCount(m->names))
        return;
    MessageLoader_GetString(m->names, label, dst);
}

static void pltt_load(PaletteData *pd, NARC *narc, int member, enum HeapID heapID,
                      enum PaletteBufferID buf, u32 size, u16 pos, u16 readPos)
{
    NNSG2dPaletteData *pltt;
    void *raw = Graphics_GetPlttDataFromOpenNARC(narc, (u32)member, &pltt, heapID);

    if (raw == NULL)
        return;
    if (size == 0)
        size = pltt->szByte;
    /* HEARTGOLD asks for more bytes than some members hold. */
    {
        static const u16 sBlack[0x100];
        u32 have = (u32)readPos * 2 < pltt->szByte ? pltt->szByte - (u32)readPos * 2 : 0;

        if (size > have) {
            if (have > 0)
                PaletteData_LoadBuffer(pd, (const u16 *)pltt->pRawData + readPos, buf, pos, (u16)have);
            PaletteData_LoadBuffer(pd, sBlack, buf, (u16)(pos + have / 2),
                                   (u16)(size - have > sizeof sBlack ? sizeof sBlack : size - have));
        } else {
            PaletteData_LoadBuffer(pd, (const u16 *)pltt->pRawData + readPos, buf, pos, (u16)size);
        }
    }
    Heap_Free(raw);
}

static void copy_rect(PokegearMapAppData *m, u8 layer, u8 dx, u8 dy, u8 dw, u8 dh,
                      const NNSG2dScreenData *s, u8 sx, u8 sy)
{
    if (s == NULL)
        return;
    Bg_CopyToTilemapRect(m->pokegear->bgConfig, layer, dx, dy, dw, dh, s->rawData, sx, sy,
                         (u8)(s->screenWidth / 8), (u8)(s->screenHeight / 8));
}

/* ------------------------------------------------------------------ */
/* Graphics (overlay_101_021E7FF4.c)                                   */
/* ------------------------------------------------------------------ */

static void map_init_bgs(PokegearMapAppData *m)
{
    BgConfig *bg = m->pokegear->bgConfig;
    BgTemplate t[6] = {
        { 0, 0, 0x800, 0, BG_SCREEN_SIZE_256x256, GX_BG_COLORMODE_16, GX_BG_SCRBASE_0xf000, GX_BG_CHARBASE_0x00000, GX_BG_EXTPLTT_01, 1, GX_BG_AREAOVER_XLU, FALSE },
        { 0, 0, 0x2000, 0, BG_SCREEN_SIZE_512x512, GX_BG_COLORMODE_256, GX_BG_SCRBASE_0xd000, GX_BG_CHARBASE_0x10000, GX_BG_EXTPLTT_01, 2, GX_BG_AREAOVER_XLU, FALSE },
        { 0, 0, 0x2000, 0, BG_SCREEN_SIZE_512x512, GX_BG_COLORMODE_256, GX_BG_SCRBASE_0xb000, GX_BG_CHARBASE_0x10000, GX_BG_EXTPLTT_01, 3, GX_BG_AREAOVER_XLU, FALSE },
        { 0, 0, 0x800, 0, BG_SCREEN_SIZE_256x256, GX_BG_COLORMODE_16, GX_BG_SCRBASE_0xf000, GX_BG_CHARBASE_0x00000, GX_BG_EXTPLTT_01, 0, GX_BG_AREAOVER_XLU, FALSE },
        { 0, 0, 0x800, 0, BG_SCREEN_SIZE_256x256, GX_BG_COLORMODE_16, GX_BG_SCRBASE_0xe800, GX_BG_CHARBASE_0x00000, GX_BG_EXTPLTT_01, 1, GX_BG_AREAOVER_XLU, FALSE },
        { 0, 0, 0x800, 0, BG_SCREEN_SIZE_256x256, GX_BG_COLORMODE_16, GX_BG_SCRBASE_0xe000, GX_BG_CHARBASE_0x10000, GX_BG_EXTPLTT_01, 2, GX_BG_AREAOVER_XLU, FALSE },
    };
    int i;

    GX_SetGraphicsMode(GX_DISPMODE_GRAPHICS, GX_BGMODE_5, GX_BG0_AS_2D);
    Bg_InitFromTemplate(bg, BG_LAYER_MAIN_1, &t[0], BG_TYPE_STATIC);
    Bg_InitFromTemplate(bg, BG_LAYER_MAIN_2, &t[1], BG_TYPE_STATIC_WITH_AFFINE);
    Bg_InitFromTemplate(bg, BG_LAYER_MAIN_3, &t[2], BG_TYPE_STATIC_WITH_AFFINE);
    Bg_InitFromTemplate(bg, BG_LAYER_SUB_1, &t[3], BG_TYPE_STATIC);
    Bg_InitFromTemplate(bg, BG_LAYER_SUB_2, &t[4], BG_TYPE_STATIC);
    Bg_InitFromTemplate(bg, BG_LAYER_SUB_3, &t[5], BG_TYPE_STATIC);
    for (i = 0; i < 3; i++) {
        Bg_ClearTilemap(bg, (u8)(i + BG_LAYER_MAIN_1));
        Bg_ClearTilesRange((u8)(i + BG_LAYER_MAIN_1), 0x40, 0, m->heapID);
        Bg_ClearTilemap(bg, (u8)(i + BG_LAYER_SUB_1));
        Bg_ClearTilesRange((u8)(i + BG_LAYER_SUB_1), 0x20, 0, m->heapID);
    }
    for (i = 1; i < 8; i++)
        if (i != 4)
            Bg_ToggleLayer((u8)i, 0);
}

static void map_unload_bgs(PokegearMapAppData *m)
{
    Pokegear_ClearAppBgLayers(m->pokegear);
    G2_SetBlendAlpha(0, 0, 0, 0);
}

static void map_load_graphics(PokegearMapAppData *m, u8 skin)
{
    BgConfig *bg = m->pokegear->bgConfig;
    NARC *narc = openmmo_pokegear_narc(PG_NARC_MAP, m->heapID);

    if (narc == NULL)
        return;
    Graphics_LoadTilesToBgLayerFromOpenNARC(narc, PGMAP_FRAME_CHAR + skin, bg, BG_LAYER_MAIN_1, 0, 0, FALSE, m->heapID);
    Graphics_LoadTilesToBgLayerFromOpenNARC(narc, PGMAP_MAP_CHAR, bg, BG_LAYER_MAIN_2, 0, 0, FALSE, m->heapID);
    Graphics_LoadTilesToBgLayerFromOpenNARC(narc, PGMAP_PANEL_CHAR + skin, bg, BG_LAYER_SUB_2, 0, 0, FALSE, m->heapID);
    Graphics_LoadTilesToBgLayerFromOpenNARC(narc, PGMAP_DETAIL_CHAR, bg, BG_LAYER_SUB_3, 0, 0, FALSE, m->heapID);
    m->scrnRaw[0] = Graphics_GetScrnDataFromOpenNARC(narc, PGMAP_PANEL_SCRN + skin, FALSE, &m->scrnPanel, m->heapID);
    m->scrnRaw[1] = Graphics_GetScrnDataFromOpenNARC(narc, PGMAP_MAP_SCRN, FALSE, &m->scrnRegion, m->heapID);
    m->scrnRaw[2] = Graphics_GetScrnDataFromOpenNARC(narc, PGMAP_DETAIL_SCRN, FALSE, &m->scrnDetail, m->heapID);
    m->scrnRaw[3] = Graphics_GetScrnDataFromOpenNARC(narc, PGMAP_FRAME_SCRN + skin, FALSE, &m->scrnFrame, m->heapID);
    NARC_dtor(narc);
    Bg_ScheduleTilemapTransfer(bg, BG_LAYER_MAIN_1);
    Bg_ScheduleTilemapTransfer(bg, BG_LAYER_MAIN_2);
}

static void map_unload_graphics(PokegearMapAppData *m)
{
    int i;

    for (i = 3; i >= 0; i--) {
        if (m->scrnRaw[i] != NULL)
            Heap_Free(m->scrnRaw[i]);
        m->scrnRaw[i] = NULL;
    }
    m->scrnPanel = m->scrnRegion = m->scrnDetail = m->scrnFrame = NULL;
}

static void map_init_windows(PokegearMapAppData *m)
{
    static const WindowTemplate t[8] = {
        { BG_LAYER_SUB_1, 1, 8, 5, 3, 1, 0x3F0 },
        { BG_LAYER_SUB_1, 3, 12, 12, 2, 1, 0x3D8 },
        { BG_LAYER_SUB_1, 1, 14, 28, 4, 1, 0x368 },
        { BG_LAYER_SUB_1, 5, 18, 11, 5, 1, 0x331 },
        { BG_LAYER_SUB_1, 18, 18, 11, 5, 1, 0x2FA },
        { BG_LAYER_SUB_1, 5, 20, 11, 2, 1, 0x2E4 },
        { BG_LAYER_SUB_1, 18, 22, 11, 2, 1, 0x2CE },
        { BG_LAYER_MAIN_1, 3, 0, 12, 3, 10, 0x3DB },
    };
    int i;

    for (i = 0; i < 8; i++) {
        Window_Add(m->pokegear->bgConfig, &m->windows[i], t[i].bgLayer, t[i].tilemapLeft,
                   t[i].tilemapTop, t[i].width, t[i].height, t[i].palette, t[i].baseTile);
        Window_FillTilemap(&m->windows[i], 0);
    }
}

static void map_remove_windows(PokegearMapAppData *m)
{
    int i;

    for (i = 0; i < 8; i++) {
        Window_ClearAndCopyToVRAM(&m->windows[i]);
        Window_Remove(&m->windows[i]);
    }
}

static void map_init_msg(PokegearMapAppData *m)
{
    m->msg = openmmo_pokegear_msg(PGMAP_MSG_BANK, m->heapID);
    m->names = MessageLoader_Init(MSG_LOADER_LOAD_ON_DEMAND, NARC_INDEX_MSGDATA__PL_MSG,
                                  PGMAP_LOCATION_NAMES_BANK, m->heapID);
    m->fmt = StringTemplate_New(2, 91, m->heapID);
    m->flavorTextString = String_Init(91, m->heapID);
    m->mapNameString = String_Init(40, m->heapID);
    if (m->msg != NULL) {
        m->regionNameStrings[0] = MessageLoader_GetNewString(m->msg, PGMAP_MSG_JOHTO);
        m->regionNameStrings[1] = MessageLoader_GetNewString(m->msg, PGMAP_MSG_KANTO);
        m->chooseDestinationString = MessageLoader_GetNewString(m->msg, PGMAP_MSG_CHOOSE);
        m->flyToLocationString = MessageLoader_GetNewString(m->msg, PGMAP_MSG_FLY_TO);
        m->closeString = MessageLoader_GetNewString(m->msg, PGMAP_MSG_CLOSE);
    } else {
        m->regionNameStrings[0] = String_Init(8, m->heapID);
        m->regionNameStrings[1] = String_Init(8, m->heapID);
        m->chooseDestinationString = String_Init(8, m->heapID);
        m->flyToLocationString = String_Init(8, m->heapID);
        m->closeString = String_Init(8, m->heapID);
    }
}

static void map_delete_msg(PokegearMapAppData *m)
{
    String_Free(m->closeString);
    String_Free(m->flyToLocationString);
    String_Free(m->chooseDestinationString);
    String_Free(m->mapNameString);
    String_Free(m->regionNameStrings[1]);
    String_Free(m->regionNameStrings[0]);
    String_Free(m->flavorTextString);
    StringTemplate_Free(m->fmt);
    if (m->names != NULL)
        MessageLoader_Free(m->names);
    if (m->msg != NULL)
        MessageLoader_Free(m->msg);
    m->msg = NULL;
    m->names = NULL;
}

static void map_load_palettes(PokegearMapAppData *m, u8 skin)
{
    PaletteData *pd = m->pokegear->plttData;
    NARC *narc = openmmo_pokegear_narc(PG_NARC_MAP, m->heapID);

    if (narc == NULL)
        return;
    pltt_load(pd, narc, PGMAP_MAIN_PLTT + skin, m->heapID, PLTTBUF_MAIN_BG, 0x1C0, 0, 0);
    pltt_load(pd, narc, PGMAP_SUB_PLTT + skin, m->heapID, PLTTBUF_SUB_BG, 0x180, 0, 0);
    pltt_load(pd, narc, PGMAP_OBJ_PLTT, m->heapID, PLTTBUF_MAIN_OBJ, 0x160, 0x40, 0);
    pltt_load(pd, narc, PGMAP_OBJ_PLTT, m->heapID, PLTTBUF_SUB_OBJ, 0x160, 0x40, 0);
    PaletteData_SetAutoTransparent(pd, TRUE);
    PaletteData_Blend(pd, PLTTBUF_MAIN_BG, 0, 0xE0, 16, COLOR_BLACK);
    PaletteData_Blend(pd, PLTTBUF_MAIN_OBJ, 0x40, 0xC0, 16, COLOR_BLACK);
    PaletteData_CommitFadedBuffers(pd);
    PaletteData_SetAutoTransparent(pd, FALSE);
    NARC_dtor(narc);
}

static void map_create_objects_manager(PokegearMapAppData *m)
{
    PokegearApp_CreateSpriteManager(m->pokegear, GEAR_APP_MAP);
    m->objManager = PokegearObjectsManager_Create(PGMAP_SPRITE_MAX, m->heapID);
    SetSubScreenViewRect(SpriteSystem_GetRenderer(m->pokegear->spriteSystem), 0, FX32_CONST(240));
}

static void map_destroy_objects_manager(PokegearMapAppData *m)
{
    SetSubScreenViewRect(SpriteSystem_GetRenderer(m->pokegear->spriteSystem), 0, FX32_CONST(192));
    PokegearObjectsManager_Release(m->pokegear, m->objManager);
    m->objManager = NULL;
    PokegearApp_DestroySpriteManager(m->pokegear);
}

static void map_create_sprites(PokegearMapAppData *m)
{
    /* The gear map's five (markers, the phone marker, the cursor, the player,
     * the roamers), then the fly map's four buttons and its fly points. */
    static const PokegearSpriteTemplate t[10] = {
        { 0, 0x20, 0x60, 0, 0, 0, 4, NNS_G2D_VRAM_TYPE_2DSUB },
        { 0, 0x10, 0x98, 0, 1, 0, 4, NNS_G2D_VRAM_TYPE_2DSUB },
        { 1, 0x20, 0x80, 0, 0, 1, 4, NNS_G2D_VRAM_TYPE_2DMAIN },
        { 1, 0x20, 0x60, 0, 1, 1, 4, NNS_G2D_VRAM_TYPE_2DMAIN },
        { 1, 0x20, 0x60, 0, 2, 1, 4, NNS_G2D_VRAM_TYPE_2DMAIN },
        { 1, 0, 0, 0, 5, 1, 2, NNS_G2D_VRAM_TYPE_2DMAIN },
        { 1, 0, 0, 0, 6, 1, 2, NNS_G2D_VRAM_TYPE_2DMAIN },
        { 1, 0, 0, 0, 7, 1, 2, NNS_G2D_VRAM_TYPE_2DMAIN },
        { 1, 0, 0, 0, 8, 1, 2, NNS_G2D_VRAM_TYPE_2DMAIN },
        { 1, 0, 0, 0, 9, 3, 0, NNS_G2D_VRAM_TYPE_2DMAIN },
    };
    PokegearManagedObject *o = m->objManager->objects;
    PokegearAppData *app = m->pokegear;
    int i;
    u16 idx;

    for (i = 0; i < 4; i++) {
        PokegearObjectsManager_AppendSprite(m->objManager, PokegearApp_CreateSprite(app, &t[0]));
        if (o[PGMAP_SPRITE_MARKER0 + i].sprite != NULL) {
            Sprite_SetPositionXY(o[PGMAP_SPRITE_MARKER0 + i].sprite, (s16)(104 * (i % 2) + 32), (s16)(21 * (i / 2) + 203));
            Sprite_SetExplicitPriority(o[PGMAP_SPRITE_MARKER0 + i].sprite, 0);
        }
    }
    PokegearObjectsManager_AppendSprite(m->objManager, PokegearApp_CreateSprite(app, &t[1]));
    if (o[PGMAP_SPRITE_GEAR_BATTLE].sprite != NULL)
        Sprite_SetExplicitPriority(o[PGMAP_SPRITE_GEAR_BATTLE].sprite, 0);
    PokegearObjectsManager_AppendSprite(m->objManager, PokegearApp_CreateSprite(app, &t[2]));
    PokegearObjectsManager_AppendSprite(m->objManager, PokegearApp_CreateSprite(app, &t[3]));
    for (i = 0; i < 4; i++) {
        PokegearObjectsManager_AppendSprite(m->objManager, PokegearApp_CreateSprite(app, &t[4]));
        if (o[PGMAP_SPRITE_ROAMER_RAIKOU + i].sprite != NULL)
            Sprite_UpdateAnim(o[PGMAP_SPRITE_ROAMER_RAIKOU + i].sprite, FX32_CONST(i));
    }
    for (i = 0; i < 4; i++) {
        idx = PokegearObjectsManager_AppendSprite(m->objManager, PokegearApp_CreateSprite(app, &t[5 + i]));
        if (idx == 0xFFFF || o[idx].sprite == NULL)
            continue;
        Sprite_GetPositionXY(o[idx].sprite, &o[idx].pos.x, &o[idx].pos.y);
        Sprite_SetExplicitPriority(o[idx].sprite, 0);
        Sprite_SetAnimateFlag(o[idx].sprite, TRUE);
        Sprite_SetDrawFlag(o[idx].sprite, FALSE);
    }
    for (i = 0; i < PGMAP_NUM_FLYPOINTS; i++) {
        idx = PokegearObjectsManager_AppendSprite(m->objManager, PokegearApp_CreateSprite(app, &t[9]));
        if (idx == 0xFFFF || o[idx].sprite == NULL)
            continue;
        Sprite_GetPositionXY(o[idx].sprite, &o[idx].pos.x, &o[idx].pos.y);
        Sprite_SetDrawFlag(o[idx].sprite, FALSE);
        Sprite_SetAnimateFlag(o[idx].sprite, FALSE);
    }
    for (i = 0; i < PGMAP_SPRITE_ALWAYS_END; i++) {
        if (o[i].sprite == NULL)
            continue;
        Sprite_GetPositionXY(o[i].sprite, &o[i].pos.x, &o[i].pos.y);
        Sprite_SetDrawFlag(o[i].sprite, FALSE);
    }
    if (o[PGMAP_SPRITE_CURSOR].sprite != NULL) {
        Sprite_SetAnimateFlag(o[PGMAP_SPRITE_CURSOR].sprite, TRUE);
        Sprite_SetAffineOverwriteMode(o[PGMAP_SPRITE_CURSOR].sprite, AFFINE_OVERWRITE_MODE_DOUBLE);
    }
    if (o[PGMAP_SPRITE_PLAYER].sprite != NULL) {
        Sprite_SetAnimateFlag(o[PGMAP_SPRITE_PLAYER].sprite, FALSE);
        Sprite_SetAnimFrame(o[PGMAP_SPRITE_PLAYER].sprite, m->playerGender);
    }
}

/* ov101_021EAF40: the region onto MAIN_3, then the patches. */
static void map_draw_region(PokegearMapAppData *m)
{
    copy_rect(m, BG_LAYER_MAIN_3, 0, 0, 47, 20, m->scrnRegion, 0, 0);
    switch (m->mapUnlockLevel) {
    case 0:
        copy_rect(m, BG_LAYER_MAIN_3, 22, 0, 6, 20, m->scrnRegion, 48, 0);
        break;
    case 1:
        copy_rect(m, BG_LAYER_MAIN_3, 29, 0, 3, 20, m->scrnRegion, 54, 0);
        break;
    }
    /* Every fly point is known (the cleared story), so no town wears the
     * "not yet" patch; the Safari Zone's gate stands as it does once
     * Goldenrod's fly point is set. */
    if (m->isMapSinjoh)
        copy_rect(m, BG_LAYER_MAIN_3, 19, 1, 3, 4, m->scrnRegion, 55, 20);
    if (m->isMapSSAqua)
        copy_rect(m, BG_LAYER_MAIN_3, 24, 15, 3, 3, m->scrnRegion, 55, 24);
}

/* ov101_021EB38C: the two buttons on the frame, 0 the Y button and 1 the
 * zoom, in state 0..2. */
static void map_zoom_button(PokegearMapAppData *m, int button, int state)
{
    if (button == 0)
        copy_rect(m, BG_LAYER_MAIN_1, 26, 2, 6, 7, m->scrnFrame, (u8)(6 * state), 21);
    else
        copy_rect(m, BG_LAYER_MAIN_1, 26, 11, 6, 9, m->scrnFrame, (u8)(6 * state + 18), 21);
    Bg_ScheduleTilemapTransfer(m->pokegear->bgConfig, BG_LAYER_MAIN_1);
}

static void map_set_bg_param(PokegearMapAppData *m)
{
    BgConfig *bg = m->pokegear->bgConfig;
    PokegearManagedObject *o = m->objManager->objects;
    int i;

    GX_SetGraphicsMode(GX_DISPMODE_GRAPHICS, GX_BGMODE_5, GX_BG0_AS_2D);
    for (i = 0; i < 2; i++) {
        Bg_SetControlParam(bg, (u8)(i + BG_LAYER_MAIN_2), BG_CONTROL_PARAM_SCREEN_SIZE, BG_SCREEN_SIZE_256x512);
        Bg_SetControlParam(bg, (u8)(i + BG_LAYER_MAIN_2), BG_CONTROL_PARAM_CHAR_BASE, GX_BG_CHARBASE_0x10000);
        Bg_SetOffset(bg, (u8)(i + BG_LAYER_MAIN_2), BG_OFFSET_UPDATE_SET_X, 0);
        Bg_SetOffset(bg, (u8)(i + BG_LAYER_MAIN_2), BG_OFFSET_UPDATE_SET_Y, 0);
    }
    G2_SetBlendAlpha(4, 8, 10, 6);
    Bg_ToggleLayer(BG_LAYER_MAIN_0, 1);
    for (i = 0; i < 3; i++) {
        Bg_ClearTilemap(bg, (u8)(i + BG_LAYER_MAIN_1));
        Bg_SetOffset(bg, (u8)(i + BG_LAYER_MAIN_1), BG_OFFSET_UPDATE_SET_X, 0);
        Bg_SetOffset(bg, (u8)(i + BG_LAYER_MAIN_1), BG_OFFSET_UPDATE_SET_Y, 0);
    }
    map_init_cursor(m);
    map_vblank_affine(m, &m->cursorSpriteState);
    copy_rect(m, BG_LAYER_MAIN_1, 0, 0, 32, 20, m->scrnFrame, 0, 0);
    map_draw_region(m);
    /* The Y button is the marking mode's, not carried: drawn dark. */
    map_zoom_button(m, 0, 2);
    map_zoom_button(m, 1, m->zoomed);
    copy_rect(m, BG_LAYER_SUB_2, 0, 7, 32, 17, m->scrnPanel, 0, 7);
    map_select_at(m, (u8)m->playerX, (u8)m->playerY);
    map_print_detail(m, FALSE);
    map_highlight_selected(m, 1);
    map_place_fly_points(m, 0);
    if (o[PGMAP_SPRITE_CURSOR].sprite != NULL && o[PGMAP_SPRITE_PLAYER].sprite != NULL) {
        if (m->pokegear->cursorInAppSwitchZone == TRUE) {
            PokegearCursorManager_SetCursorSpritesDrawState(m->pokegear->cursorManager, 0, TRUE);
            Sprite_SetDrawFlag(o[PGMAP_SPRITE_CURSOR].sprite, FALSE);
        } else {
            PokegearCursorManager_SetCursorSpritesDrawState(m->pokegear->cursorManager, 0, FALSE);
            Sprite_SetDrawFlag(o[PGMAP_SPRITE_CURSOR].sprite, TRUE);
        }
        Sprite_SetDrawFlag(o[PGMAP_SPRITE_PLAYER].sprite, m->playerShown);
    }
    PokegearCursorManager_SetSpecIndexAndCursorPos(m->pokegear->cursorManager, 0, PokegearApp_AppIdToButtonIndex(m->pokegear));
    PokegearObjectsManager_UpdateAllSpritesPos(m->objManager);
    Bg_ScheduleTilemapTransfer(bg, BG_LAYER_SUB_1);
    Bg_ScheduleTilemapTransfer(bg, BG_LAYER_SUB_3);
    Bg_ScheduleTilemapTransfer(bg, BG_LAYER_MAIN_1);
    Bg_ScheduleTilemapTransfer(bg, BG_LAYER_MAIN_2);
    Bg_ScheduleTilemapTransfer(bg, BG_LAYER_MAIN_3);
}

static void map_vblank_cb(PokegearAppData *app, void *appData)
{
    PokegearMapAppData *m = appData;

    (void)app;
    if (m->requestAffineUpdate)
        map_vblank_affine(m, &m->cursorSpriteState);
}

static BOOL map_load_gfx(PokegearMapAppData *m)
{
    switch (m->substate) {
    case 0:
        map_init_bgs(m);
        Font_InitManager(FONT_SUBSCREEN, m->heapID);
        break;
    case 1:
        map_load_graphics(m, m->pokegear->skin);
        map_init_windows(m);
        map_init_msg(m);
        break;
    case 2:
        map_create_objects_manager(m);
        map_load_palettes(m, m->pokegear->skin);
        map_create_sprites(m);
        break;
    case 3:
        map_set_bg_param(m);
        m->pokegear->vblankCB = map_vblank_cb;
        m->substate = 0;
        return TRUE;
    }
    m->substate++;
    return FALSE;
}

static BOOL map_unload_gfx(PokegearMapAppData *m)
{
    m->pokegear->vblankCB = NULL;
    if (m->menu != NULL) {
        PokegearMenu_Close(m->menu);
        m->menu = NULL;
    }
    PokegearObjectsManager_Reset(m->pokegear, m->objManager);
    map_destroy_objects_manager(m);
    map_delete_msg(m);
    map_remove_windows(m);
    map_unload_graphics(m);
    Font_Free(FONT_SUBSCREEN);
    map_unload_bgs(m);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* The cursor and the scroll (overlay_101_021E9270.c)                  */
/* ------------------------------------------------------------------ */

static void map_cursor_bounds(PokegearMapAppData *m)
{
    PokegearMapCursorState *c = &m->cursorSpriteState;

    if (m->zoomed) {
        c->top = (u16)((c->y + c->affineY - 8) / 16 + 1);
        c->left = (u16)((c->x + c->affineX - 8) / 16 + 1);
        c->bottom = (u16)(c->top + 7);
        c->right = (u16)(c->left + 11);
    } else {
        c->top = (u16)((-c->y) / 8 + 1);
        c->left = (u16)(c->x / 8 + 1);
        c->bottom = (u16)(c->top + 16);
        c->right = (u16)(c->left + 23);
    }
}

/* ov101_021E9464 */
static void map_pixel_to_cell(PokegearMapAppData *m, s16 xIn, s16 yIn, u16 *xOut, u16 *yOut)
{
    s16 x = (s16)(xIn - m->centerX);
    s16 y = (s16)(yIn - m->centerY);

    if (m->zoomed) {
        *xOut = (u16)(x / 16);
        *yOut = (u16)(y / 16);
    } else {
        *xOut = (u16)(x / 8);
        *yOut = (u16)(y / 8);
    }
}

/* ov101_021E94C0 */
static void map_cursor_to_player_cell(PokegearMapAppData *m)
{
    u8 scale = (u8)(m->zoomed + 1);
    s16 x = (s16)(m->playerX - m->cursorSpriteState.left);
    s16 y = (s16)(m->playerY - m->cursorSpriteState.top);

    x = (s16)(x * (8 * scale) + m->centerX + 4 * scale);
    y = (s16)(y * (8 * scale) + m->centerY + 4 * scale);
    if (m->objManager->objects[PGMAP_SPRITE_CURSOR].sprite != NULL)
        PokegearManagedObject_SetCoordUpdateSprite(&m->objManager->objects[PGMAP_SPRITE_CURSOR], x, y);
}

/* ov101_021E9530 */
static void map_scroll_to(PokegearMapAppData *m, u8 zoomed, u16 x0, u16 y0, s16 xIn, s16 yIn)
{
    PokegearMapCursorState *c = &m->cursorSpriteState;
    u16 x, y;
    s16 xCenter, yCenter, xOffset, yOffset, top, bottom, left, right;
    u8 grid;

    if (zoomed) {
        grid = 16;
        map_pixel_to_cell(m, xIn, yIn, &x, &y);
        xCenter = (s16)(5 - x);
        yCenter = (s16)(4 - y);
        top = (s16)(y0 - y - yCenter);
        bottom = (s16)(y0 + (7 - y) - yCenter);
        left = (s16)(x0 - x - xCenter);
        right = (s16)(x0 + (11 - x) - xCenter);
    } else {
        grid = 8;
        x = (u16)(x0 - (c->x / 8 + 1));
        y = (u16)(y0 - (c->y / 8 + 1));
        xCenter = (s16)(11 - x);
        yCenter = (s16)(8 - y);
        top = (s16)(y0 - y - yCenter);
        bottom = (s16)(y0 + (16 - y) - yCenter);
        left = (s16)(x0 - x - xCenter);
        right = (s16)(x0 + (23 - x) - xCenter);
    }
    if (top < (s16)m->minYscroll) {
        yOffset = (s16)(top - m->minYscroll);
        yCenter += yOffset;
        top -= yOffset;
        bottom -= yOffset;
    } else if (bottom > (s16)m->maxYscroll) {
        yOffset = (s16)(bottom - m->maxYscroll);
        yCenter += yOffset;
        bottom -= yOffset;
        top -= yOffset;
    }
    if (left < (s16)m->minXscroll) {
        xOffset = (s16)(left - m->minXscroll);
        xCenter += xOffset;
        left -= xOffset;
        right -= xOffset;
    } else if (right > (s16)m->maxXscroll) {
        xOffset = (s16)(right - m->maxXscroll);
        xCenter += xOffset;
        right -= xOffset;
        left -= xOffset;
    }
    c->top = (u16)top;
    c->bottom = (u16)bottom;
    c->left = (u16)left;
    c->right = (u16)right;
    c->dx = (s16)(-(xCenter * grid));
    c->dy = (s16)(-(yCenter * grid));
    c->destX = (s16)(c->x + c->dx);
    c->destY = (s16)(c->y + c->dy);
    c->fx = FX32_CONST(c->x);
    c->fy = FX32_CONST(c->y);
    c->dxStep = FX_Div(FX32_CONST(c->dx), FX32_CONST(m->cursorSpeed));
    c->dyStep = FX_Div(FX32_CONST(c->dy), FX32_CONST(m->cursorSpeed));
}

/* ov101_021E9848 / ov101_021EC49C's first half: the affine centre. */
static void map_affine_center(PokegearMapAppData *m, u16 xIn, u16 yIn, s16 px, s16 py, int *xOut, int *yOut)
{
    u8 r2, r1;

    if (!m->zoomed) {
        r2 = (u8)((px - m->centerX) / 16);
        r1 = (u8)((py - m->centerY) / 16);
        *xOut = r2 <= 5 ? xIn * 8 + 8 : xIn * 8;
        *yOut = r1 > 4 ? yIn * 8 + 8 : yIn * 8;
    } else {
        r2 = (u8)(((px - m->centerX) / 8) % 2);
        r1 = (u8)(((py - m->centerY) / 8) % 2);
        *xOut = xIn * 8 + r2 * 8;
        *yOut = yIn * 8 + r1 * 8;
    }
}

/* ov101_021E990C: where everything starts. */
static void map_init_cursor(PokegearMapAppData *m)
{
    PokegearManagedObject *o = m->objManager->objects;
    PokegearMapCursorState *c = &m->cursorSpriteState;
    s16 x, y;
    u16 ratio, grid, gridHalf;
    VecFx32 scale;

    ratio = (u16)(m->zoomed ? 2 : 1);
    grid = (u16)(m->zoomed ? 16 : 8);
    c->xRatio = FX32_CONST(ratio);
    c->yRatio = FX32_CONST(ratio);
    gridHalf = (u16)(grid / 2);
    map_scroll_to(m, FALSE, (u16)m->playerX, (u16)m->playerY, 0, 0);
    c->x = c->destX;
    c->y = c->destY;
    x = (s16)((m->playerX - c->left) * 8 + m->centerX + 4);
    y = (s16)((m->playerY - c->top) * 8 + m->centerY + 4);
    if (m->zoomed) {
        map_scroll_to(m, TRUE, (u16)m->playerX, (u16)m->playerY, x, y);
        map_affine_center(m, (u16)m->playerX, (u16)m->playerY, x, y, &c->affineX, &c->affineY);
        c->x = c->destX;
        c->y = c->destY;
        x = (s16)((m->playerX - c->left) * grid + m->centerX + gridHalf);
        y = (s16)((m->playerY - c->top) * grid + m->centerY + gridHalf);
    }
    PokegearManagedObject_SetCoord(&o[PGMAP_SPRITE_CURSOR], x, y);
    PokegearManagedObject_SetCell(&o[PGMAP_SPRITE_CURSOR], m->playerX, m->playerY);
    scale.x = scale.y = FX32_CONST(ratio);
    scale.z = FX32_ONE;
    if (o[PGMAP_SPRITE_CURSOR].sprite != NULL)
        Sprite_SetAffineScale(o[PGMAP_SPRITE_CURSOR].sprite, &scale);
    x = (s16)((m->matrixX - c->left) * grid + m->centerX + gridHalf);
    y = (s16)((m->matrixY - c->top) * grid + m->centerY + gridHalf);
    PokegearManagedObject_SetCoord(&o[PGMAP_SPRITE_PLAYER], x, y);
    PokegearManagedObject_SetCell(&o[PGMAP_SPRITE_PLAYER], m->matrixX, m->matrixY);
}

static void map_vblank_affine(PokegearMapAppData *m, PokegearMapCursorState *c)
{
    MtxFx22 mtx;
    fx32 xScale = FX_Inv(c->xRatio);
    fx32 yScale = FX_Inv(c->yRatio);
    int i;

    mtx._00 = xScale;
    mtx._01 = 0;
    mtx._10 = 0;
    mtx._11 = yScale;
    for (i = 0; i < 2; i++) {
        Bg_SetOffset(m->pokegear->bgConfig, (u8)(i + BG_LAYER_MAIN_2), BG_OFFSET_UPDATE_SET_X, c->x + m->xOffset);
        Bg_SetOffset(m->pokegear->bgConfig, (u8)(i + BG_LAYER_MAIN_2), BG_OFFSET_UPDATE_SET_Y, c->y + m->yOffset);
        Bg_SetAffineParams(m->pokegear->bgConfig, (u8)(i + BG_LAYER_MAIN_2), &mtx, c->affineX, c->affineY);
    }
    m->requestAffineUpdate = FALSE;
}

/* ov101_021E9BF4 */
static void map_shift_objects(PokegearMapAppData *m, s16 dx, s16 dy)
{
    PokegearManagedObject *o = m->objManager->objects;
    u16 i;

    PokegearManagedObject_AddCoord(&o[PGMAP_SPRITE_PLAYER], dx, dy);
    if (m->dragging)
        PokegearManagedObject_AddCoord(&o[PGMAP_SPRITE_CURSOR], dx, dy);
    for (i = 0; i < PGMAP_NUM_FLYPOINTS; i++)
        PokegearManagedObject_AddCoord(&o[PGMAP_SPRITE_FLY_MENU_WARPS_BEGIN + i], dx, dy);
}

static BOOL map_fade_screen(PokegearMapAppData *m, u8 direction)
{
    PaletteData *pd = m->pokegear->plttData;

    if (m->fadeStep > 16)
        return TRUE;
    if (direction == 0) {
        PaletteData_Blend(pd, PLTTBUF_MAIN_BG, 0, 0xE0, (u8)(16 - m->fadeStep), COLOR_BLACK);
        PaletteData_Blend(pd, PLTTBUF_MAIN_OBJ, 0x40, 0xC0, (u8)(16 - m->fadeStep), COLOR_BLACK);
    } else {
        PaletteData_Blend(pd, PLTTBUF_MAIN_BG, 0, 0xE0, m->fadeStep, COLOR_BLACK);
        PaletteData_Blend(pd, PLTTBUF_MAIN_OBJ, 0x40, 0xC0, m->fadeStep, COLOR_BLACK);
    }
    if (m->fadeStep >= 16) {
        m->fadeStep += 2;
        return TRUE;
    }
    m->fadeStep += 2;
    return FALSE;
}

/* The panel's slide on the top screen when a card gives way to this one
 * (PokegearMap_Begin/RunScrollMarkingsPanelTopScreen). */
static int s_panelStep, s_panelDone;

static void map_begin_panel_scroll(PokegearMapAppData *m, u8 direction)
{
    BgConfig *bg = m->pokegear->bgConfig;
    PokegearManagedObject *o = m->objManager->objects;
    int i;

    G2S_SetWnd0Position(0x00, 0x40, 0xFF, 0xC0);
    G2S_SetWnd1Position(0xFF, 0x40, 0x00, 0xC0);
    G2S_SetWndOutsidePlane(0x11, FALSE);
    G2S_SetWnd0InsidePlane(0x1F, FALSE);
    G2S_SetWnd1InsidePlane(0x1F, FALSE);
    GXS_SetVisibleWnd(3);
    if (direction == 0) {
        for (i = 0; i < 3; i++) {
            Bg_SetOffset(bg, (u8)(i + BG_LAYER_SUB_1), BG_OFFSET_UPDATE_SET_Y, -0x80);
            Bg_ToggleLayer((u8)(i + BG_LAYER_SUB_1), 1);
        }
        for (i = 0; i < 4; i++)
            PokegearManagedObject_SetCoord(&o[PGMAP_SPRITE_MARKER0 + i], (s16)((i % 2) * 104 + 32), (s16)((i / 2) * 21 + 331));
        PokegearManagedObject_SetCoord(&o[PGMAP_SPRITE_GEAR_BATTLE], 16, 280);
        PokegearObjectsManager_UpdateAllSpritesPos(m->objManager);
    } else {
        Bg_SetOffset(bg, BG_LAYER_SUB_1, BG_OFFSET_UPDATE_SUB_Y, 0);
        Bg_SetOffset(bg, BG_LAYER_SUB_2, BG_OFFSET_UPDATE_SUB_Y, 0);
        Bg_SetOffset(bg, BG_LAYER_SUB_3, BG_OFFSET_UPDATE_SUB_Y, 0);
    }
    s_panelStep = 0;
    s_panelDone = 0;
}

static BOOL map_run_panel_scroll(PokegearMapAppData *m, u8 direction)
{
    BgConfig *bg = m->pokegear->bgConfig;
    PokegearManagedObject *o = m->objManager->objects;
    int i;

    if (s_panelDone)
        return TRUE;
    if (direction == 0) {
        for (i = 0; i < 3; i++)
            Bg_SetOffset(bg, (u8)(i + BG_LAYER_SUB_1), BG_OFFSET_UPDATE_ADD_Y, 32);
        for (i = PGMAP_SPRITE_MARKER0; i <= PGMAP_SPRITE_GEAR_BATTLE; i++)
            PokegearManagedObject_AddCoord(&o[i], 0, -32);
    } else {
        for (i = 0; i < 3; i++)
            Bg_SetOffset(bg, (u8)(i + BG_LAYER_SUB_1), BG_OFFSET_UPDATE_SUB_Y, 32);
        for (i = PGMAP_SPRITE_MARKER0; i <= PGMAP_SPRITE_GEAR_BATTLE; i++)
            PokegearManagedObject_AddCoord(&o[i], 0, 32);
    }
    PokegearObjectsManager_UpdateAllSpritesPos(m->objManager);
    if (++s_panelStep < 4)
        return FALSE;
    s_panelStep = 0;
    s_panelDone = 1;
    if (direction == 1) {
        for (i = 0; i < 3; i++) {
            Bg_ToggleLayer((u8)(i + BG_LAYER_SUB_1), 0);
            Bg_ClearTilemap(bg, (u8)(i + BG_LAYER_SUB_1));
            Bg_SetOffset(bg, (u8)(i + BG_LAYER_SUB_1), BG_OFFSET_UPDATE_SET_Y, 0);
            Bg_ScheduleTilemapTransfer(bg, (u8)(i + BG_LAYER_SUB_1));
        }
    }
    GXS_SetVisibleWnd(0);
    G2S_SetWnd0Position(0, 0, 0, 0);
    G2S_SetWnd1Position(0, 0, 0, 0);
    G2S_SetWnd0InsidePlane(0, FALSE);
    G2S_SetWnd1InsidePlane(0, FALSE);
    G2S_SetWndOutsidePlane(0, FALSE);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Places, regions and fly points                                      */
/* ------------------------------------------------------------------ */

static BOOL map_location_hidden(PokegearMapAppData *m, u16 mapId)
{
    switch (mapId) {
    case 1: case 2: case 174:       /* Routes 47 and 48, the Safari gate */
        return !m->canSeeSafariZone;
    case 521:                       /* Sinjoh */
        return !m->isMapSinjoh;
    case 412:                       /* the S.S. Aqua */
        return !m->isMapSSAqua;
    }
    return FALSE;
}

static const PokegearMapLocationSpec *map_spec_at(PokegearMapAppData *m, u8 x, u8 y)
{
    int i;

    for (i = 0; i < PGMAP_NUM_LOCATIONS; i++) {
        const PokegearMapLocationSpec *s = &gPokegearLocationSpecs[i];

        if (x >= s->x && y >= s->y && x < s->x + s->width && y < s->y + s->height)
            return map_location_hidden(m, s->mapId) ? NULL : s;
    }
    return NULL;
}

static const PokegearMapLocationSpec *map_spec_by_id(PokegearMapAppData *m, u16 mapId)
{
    int i;

    for (i = 0; i < PGMAP_NUM_LOCATIONS; i++)
        if (gPokegearLocationSpecs[i].mapId == mapId)
            return map_location_hidden(m, mapId) ? NULL : &gPokegearLocationSpecs[i];
    return NULL;
}

/* ov101_021EA794 */
static void map_select_at(PokegearMapAppData *m, u8 x, u8 y)
{
    m->selectedLoc.locationSpec = map_spec_at(m, x, y);
    if (m->mapUnlockLevel == 0 && !(x == 25 && y == 10) && x >= 22)
        m->selectedLoc.locationSpec = NULL;
    m->selectedLoc.x = x;
    m->selectedLoc.y = y;
}

static BOOL map_same_region(PokegearMapAppData *m, u16 x, u16 y)
{
    int region = Pokegear_RegionFromCoords(x, y);

    return m->curRegion == POKEGEAR_REGION_INDIGO || region == m->curRegion;
}

/* ov101_021EA804: the Plateau and Route 26 are reachable from both regions. */
static BOOL map_can_fly_to(PokegearMapAppData *m, u16 mapId, u16 x, u16 y)
{
    if (mapId == 58 || mapId == 30) /* the Plateau and Route 26 */
        return TRUE;
    return map_same_region(m, x, y);
}

/* ov101_021EA81C: the fly point under a cell, every one of them known. */
static int map_flypoint_at(u16 x, u16 y)
{
    int i;

    for (i = 0; i < PGMAP_NUM_FLYPOINTS; i++) {
        const MapFlypointParam *f = &gPokegearFlypoints[i];

        if (x < f->x || x >= f->x + f->width || y < f->y || y >= f->y + f->height)
            continue;
        return i;
    }
    return -1;
}

/* PokegearMap_GetFlyDestinationAtCoord: the map a Fly from here reaches,
 * or 0. */
static int map_fly_at(PokegearMapAppData *m, u16 x, u16 y)
{
    int idx = map_flypoint_at(x, y);

    if (idx < 0 || !map_can_fly_to(m, gPokegearFlypoints[idx].mapIDforWarp, x, y))
        return 0;
    return gPokegearFlypoints[idx].mapIDforWarp;
}

/* ov101_021EA4D0: the fly point sprites, every town's shown; the one under
 * the cursor lit (sequence 11), the rest plain (10). */
static void map_place_fly_points(PokegearMapAppData *m, u8 mode)
{
    PokegearManagedObject *o = m->objManager->objects;
    u16 i;

    for (i = 0; i < PGMAP_NUM_FLYPOINTS; i++) {
        const MapFlypointParam *f = &gPokegearFlypoints[i];
        s16 halfWidth = (s16)(f->width * 4);
        s16 halfHeight = (s16)(f->height * 4);
        s16 x = (s16)((f->x - m->cursorSpriteState.left) * 8 + m->centerX + halfWidth);
        s16 y = (s16)(((f->y + 2) - m->cursorSpriteState.top) * 8 + m->centerY + halfHeight);
        u16 idx = (u16)(i + PGMAP_SPRITE_FLY_MENU_WARPS_BEGIN);

        if (o[idx].sprite == NULL)
            continue;
        switch (mode) {
        case 0:
            o[idx].pos.x = x;
            o[idx].pos.y = y;
            /* Every town is open here (HeartGold gates each on a visited
             * flag), so the other region's points reach the button column
             * beside the map panel; a point is drawn only inside the panel. */
            Sprite_SetDrawFlag(o[idx].sprite, !m->zoomed && x + halfWidth <= PGMAP_PANEL_RIGHT);
            if (map_can_fly_to(m, f->mapIDforWarp, f->x, f->y))
                Sprite_SetAnim(o[idx].sprite, 10);
            break;
        case 1:
            o[idx].destX = x;
            o[idx].destY = y;
            Sprite_SetDrawFlag(o[idx].sprite, !m->zoomed && x + halfWidth <= PGMAP_PANEL_RIGHT);
            break;
        case 2:
            Sprite_SetDrawFlag(o[idx].sprite, FALSE);
            break;
        }
    }
}

/* ov101_021EA8A8: on a fly point, light it and name its town. */
static int map_select_fly(PokegearMapAppData *m, u8 x, u8 y)
{
    PokegearManagedObject *o = m->objManager->objects;
    int idx = map_flypoint_at(x, (u16)(y - 2));
    int mapId;

    if (idx < 0) {
        map_select_at(m, x, y);
        if (m->flyDestination >= 0 && o[PGMAP_SPRITE_FLY_MENU_WARPS_BEGIN + m->flyDestination].sprite != NULL) {
            Sprite_SetAnim(o[PGMAP_SPRITE_FLY_MENU_WARPS_BEGIN + m->flyDestination].sprite, 10);
            m->flyDestination = -1;
        }
        return -1;
    }
    mapId = gPokegearFlypoints[idx].mapIDforName;
    m->selectedLoc.x = x;
    m->selectedLoc.y = y;
    m->selectedLoc.locationSpec = map_spec_by_id(m, (u16)mapId);
    if (m->flyDestination != idx) {
        if (m->flyDestination >= 0 && o[PGMAP_SPRITE_FLY_MENU_WARPS_BEGIN + m->flyDestination].sprite != NULL) {
            Sprite_SetAnim(o[PGMAP_SPRITE_FLY_MENU_WARPS_BEGIN + m->flyDestination].sprite, 10);
            m->flyDestination = -1;
        }
        if (map_can_fly_to(m, (u16)mapId, x, (u16)(y - 2))
            && o[PGMAP_SPRITE_FLY_MENU_WARPS_BEGIN + idx].sprite != NULL) {
            Sprite_SetAnim(o[PGMAP_SPRITE_FLY_MENU_WARPS_BEGIN + idx].sprite, 11);
            m->flyDestination = (s8)idx;
        }
    }
    return mapId;
}

/* ov101_021EAA0C: the panel, region, name, the town's block and its
 * line. Markers and the phone's flag are HeartGold's save's and stay off. */
static void map_print_panel(PokegearMapAppData *m, BOOL nameOnly, int regionNo)
{
    BgConfig *bg = m->pokegear->bgConfig;
    const PokegearMapLocationSpec *spec = m->selectedLoc.locationSpec;
    PokegearManagedObject *o = m->objManager->objects;
    u32 i;

    String_Clear(m->mapNameString);
    for (i = 0; i < 3; i++)
        Window_FillTilemap(&m->windows[i], 0);
    Text_AddPrinterWithParamsAndColor(&m->windows[0], FONT_SYSTEM, m->regionNameStrings[regionNo], 2, 4,
                                      TEXT_SPEED_NO_TRANSFER, TEXT_COLOR(1, 2, 0), NULL);
    if (spec != NULL) {
        u8 detailSrcX, blockId;

        landmark_name(m, spec->mapId, m->mapNameString);
        Text_AddPrinterWithParamsAndColor(&m->windows[1], FONT_SYSTEM, m->mapNameString, 0, 0,
                                          TEXT_SPEED_NO_TRANSFER, TEXT_COLOR(1, 2, 0), NULL);
        if (nameOnly) {
            for (i = 0; i <= 1; i++)
                Window_CopyToVRAM(&m->windows[i]);
            copy_rect(m, BG_LAYER_SUB_2, 23, 11, 8, 7, m->scrnPanel, 0, 0);
            Bg_ScheduleTilemapTransfer(bg, BG_LAYER_SUB_1);
            Bg_ScheduleTilemapTransfer(bg, BG_LAYER_SUB_2);
            return;
        }
        if (spec->blockID != 0) {
            detailSrcX = 8;
            blockId = (u8)(spec->blockID - 1);
        } else {
            detailSrcX = 0;
            blockId = 0;
        }
        copy_rect(m, BG_LAYER_SUB_2, 23, 11, 8, 7, m->scrnPanel, detailSrcX, 0);
        copy_rect(m, BG_LAYER_SUB_3, 24, 11, 7, 7, m->scrnDetail, (u8)((blockId % 4) * 7), (u8)((blockId / 4) * 7));
        String_Clear(m->flavorTextString);
        if (m->msg != NULL)
            MessageLoader_GetString(m->msg, spec->flavorText, m->flavorTextString);
        Text_AddPrinterWithParamsAndColor(&m->windows[2], FONT_SYSTEM, m->flavorTextString, 0, 0,
                                          TEXT_SPEED_NO_TRANSFER, TEXT_COLOR(1, 2, 0), NULL);
    } else {
        copy_rect(m, BG_LAYER_SUB_2, 23, 11, 8, 7, m->scrnPanel, 0, 0);
    }
    if (o[PGMAP_SPRITE_GEAR_BATTLE].sprite != NULL)
        Sprite_SetDrawFlag(o[PGMAP_SPRITE_GEAR_BATTLE].sprite, FALSE);
    for (i = 0; i < 4; i++) {
        if (o[i + PGMAP_SPRITE_MARKER0].sprite != NULL)
            Sprite_SetDrawFlag(o[i + PGMAP_SPRITE_MARKER0].sprite, FALSE);
        Window_FillTilemap(&m->windows[i + 3], 0);
    }
    for (i = 0; i <= 4; i++)
        Window_CopyToVRAM(&m->windows[i]);
    Bg_ScheduleTilemapTransfer(bg, BG_LAYER_SUB_1);
    Bg_ScheduleTilemapTransfer(bg, BG_LAYER_SUB_2);
    Bg_ScheduleTilemapTransfer(bg, BG_LAYER_SUB_3);
}

/* ov101_021EAD90: Kanto's name for 0 and 1, Johto's for 2. */
static void map_print_detail(PokegearMapAppData *m, BOOL nameOnly)
{
    map_print_panel(m, nameOnly, (Pokegear_RegionFromCoords((u16)m->playerX, (u16)(m->playerY - 2)) / 2) ^ 1);
}

/* ov101_021EB1E0: the selected town lit on MAIN_2. */
static void map_highlight_selected(PokegearMapAppData *m, u8 on)
{
    BgConfig *bg = m->pokegear->bgConfig;
    int i;

    if (!on || m->selectedLoc.locationSpec == NULL) {
        Bg_ClearTilemap(bg, BG_LAYER_MAIN_2);
        Bg_ScheduleTilemapTransfer(bg, BG_LAYER_MAIN_2);
        m->lastSelectedMapID = 0;
        return;
    }
    if (m->selectedLoc.locationSpec->mapId == m->lastSelectedMapID)
        return;
    m->lastSelectedMapID = m->selectedLoc.locationSpec->mapId;
    Bg_ClearTilemap(bg, BG_LAYER_MAIN_2);
    for (i = 0; i < PGMAP_NUM_LOCATIONS; i++) {
        const PokegearMapLocationSpec *s = &gPokegearLocationSpecs[i];
        u16 dx, dy;

        if (m->selectedLoc.locationSpec->mapId != s->mapId)
            continue;
        if (s->destWidth * s->destHeight >= 9) {
            dx = (u16)(s->x - 1);
            dy = (u16)(s->y - 1);
        } else {
            dx = s->x;
            dy = s->y;
        }
        copy_rect(m, BG_LAYER_MAIN_2, (u8)dx, (u8)dy, s->destWidth, s->destHeight, m->scrnRegion, s->srcX, s->srcY);
    }
    Bg_ScheduleTilemapTransfer(bg, BG_LAYER_MAIN_2);
}

static void map_hide_cursor(PokegearMapAppData *m)
{
    if (m->objManager->objects[PGMAP_SPRITE_CURSOR].sprite != NULL)
        Sprite_SetDrawFlag(m->objManager->objects[PGMAP_SPRITE_CURSOR].sprite, FALSE);
    m->dragging = FALSE;
}

static void map_deselect_app(void *appData)
{
    PokegearMapAppData *m = appData;

    PokegearCursorManager_SetCursorSpritesDrawState(m->pokegear->cursorManager, 0, FALSE);
    map_cursor_to_player_cell(m);
    if (m->objManager->objects[PGMAP_SPRITE_CURSOR].sprite != NULL)
        Sprite_SetDrawFlag(m->objManager->objects[PGMAP_SPRITE_CURSOR].sprite, TRUE);
    map_highlight_selected(m, 1);
    Bg_ToggleLayer(BG_LAYER_MAIN_2, 1);
}

static void map_show_cursor(void *appData)
{
    PokegearMapAppData *m = appData;

    map_cursor_to_player_cell(m);
    if (m->objManager->objects[PGMAP_SPRITE_CURSOR].sprite != NULL)
        Sprite_SetDrawFlag(m->objManager->objects[PGMAP_SPRITE_CURSOR].sprite, TRUE);
    m->dragging = FALSE;
}

/* PokegearMap_PrintLandmarkNameAndFlavorText: the line on the frame's top
 * ("Choose your destination." or "Fly to X?"). */
static void map_print_fly_line(PokegearMapAppData *m, int mapId)
{
    Window_FillTilemap(&m->windows[7], 0);
    if (mapId < 0) {
        Text_AddPrinterWithParamsAndColor(&m->windows[7], FONT_SYSTEM, m->chooseDestinationString, 8, 0,
                                          TEXT_SPEED_INSTANT, TEXT_COLOR(3, 2, 0), NULL);
    } else {
        landmark_name(m, (u16)mapId, m->mapNameString);
        StringTemplate_SetString(m->fmt, 0, m->mapNameString, 0, 0, GAME_LANGUAGE);
        StringTemplate_Format(m->fmt, m->flavorTextString, m->flyToLocationString);
        Text_AddPrinterWithParamsAndColor(&m->windows[7], FONT_SYSTEM, m->flavorTextString, 8, 0,
                                          TEXT_SPEED_INSTANT, TEXT_COLOR(3, 2, 0), NULL);
    }
    Bg_ScheduleTilemapTransfer(m->pokegear->bgConfig, BG_LAYER_MAIN_1);
}

static void map_clear_fly_line(PokegearMapAppData *m)
{
    Window_FillTilemap(&m->windows[7], 0);
    Window_CopyToVRAM(&m->windows[7]);
    Bg_ScheduleTilemapTransfer(m->pokegear->bgConfig, BG_LAYER_MAIN_1);
}

/* PokegearMap_SpawnFlyContextMenu */
static void map_spawn_fly_menu(PokegearMapAppData *m, u32 cellX)
{
    static const u16 entries[2] = { PGMAP_MSG_FLY, PGMAP_MSG_QUIT };
    u8 x = (cellX < 8 || cellX > 15) ? 11 : 3;

    m->menu = PokegearMenu_Open(m->pokegear, BG_LAYER_MAIN_1, x, 4, 0, 0x1C, 14, m->msg, entries, 2, m->heapID);
}

/* Where HeartGold lands a Fly to that map: its tile on the shared plane,
 * turned into the tile on the cut this package made of it. */
static int map_fly_landing(int hgMapId, int *header, int *x, int *z)
{
    int i, region, x0, y0;

    for (i = 0; i < (int)(PGMAP_NUM_LANDINGS); i++) {
        if (gPokegearFlyLandings[i].mapId != hgMapId)
            continue;
        *header = POKEGEAR_PORTED_FIRST + hgMapId;
        if (!openmmo_pokegear_map(*header, &region, NULL, NULL, NULL, NULL, NULL)
            || !openmmo_pokegear_box(region, &x0, &y0, NULL, NULL))
            return 0;
        *x = (int)gPokegearFlyLandings[i].x - x0 * 32;
        *z = (int)gPokegearFlyLandings[i].y - y0 * 32;
        return 1;
    }
    return 0;
}

/* ov101_021EB784: the fly menu over the town under the cursor. */
static int map_ask_fly(PokegearMapAppData *m, int flyDest)
{
    u16 x, y;

    if (flyDest <= 0)
        return -1;
    m->flyChosenIdx = flyDest;
    if (m->pokegear->menuInputState == MENU_INPUT_STATE_TOUCH)
        map_pixel_to_cell(m, (s16)gSystem.touchX, (s16)gSystem.touchY, &x, &y);
    else
        map_pixel_to_cell(m, m->objManager->objects[PGMAP_SPRITE_CURSOR].pos.x,
                          m->objManager->objects[PGMAP_SPRITE_CURSOR].pos.y, &x, &y);
    if (m->selectedLoc.locationSpec != NULL)
        map_print_fly_line(m, m->selectedLoc.locationSpec->mapId);
    else
        map_print_fly_line(m, flyDest);
    map_spawn_fly_menu(m, x);
    return GEAR_RETURN_8;
}

/* ------------------------------------------------------------------ */
/* Input (overlay_101_021EB568.c)                                      */
/* ------------------------------------------------------------------ */

/* ov101_021EB654: a held direction moves the cursor a cell. */
static BOOL map_step_cursor(PokegearMapAppData *m)
{
    u8 flag = 0;
    u32 held = gSystem.heldKeys;
    PokegearManagedObject *o = &m->objManager->objects[PGMAP_SPRITE_CURSOR];

    if (held & PAD_KEY_UP) {
        if (m->playerY > m->minYscroll + 1) {
            m->playerY--;
            m->moveCursorDirection |= 1;
            flag = 1;
        }
    } else if (held & PAD_KEY_DOWN) {
        if (m->playerY < m->maxYscroll) {
            m->playerY++;
            m->moveCursorDirection |= 2;
            flag = 1;
        }
    }
    if (held & PAD_KEY_LEFT) {
        if (m->playerX > m->minXscroll + 1) {
            m->playerX--;
            m->moveCursorDirection |= 4;
            flag = 1;
        }
    } else if (held & PAD_KEY_RIGHT) {
        if (m->playerX < m->maxXscroll - 1) {
            m->playerX++;
            m->moveCursorDirection |= 8;
            flag = 1;
        }
    }
    if (flag) {
        m->cursorSpeed = 2;
        m->moving = 1;
        m->stepping = 1;
        m->cursorPos = o->pos;
        return TRUE;
    }
    return FALSE;
}

static void map_after_move(PokegearMapAppData *m)
{
    map_select_fly(m, (u8)m->playerX, (u8)m->playerY);
    map_print_detail(m, FALSE);
    map_highlight_selected(m, 1);
}

/* ov101_021EC49C: the zoom's targets. */
static void map_zoom_targets(PokegearMapAppData *m, u16 x, u16 y, int *xRet, int *yRet)
{
    PokegearManagedObject *o = m->objManager->objects;
    PokegearManagedObject *cursor = &o[PGMAP_SPRITE_CURSOR];
    u16 i;
    u8 grid = (u8)(8 * (m->zoomed + 1));
    u8 half = (u8)(grid / 2);

    map_scroll_to(m, m->zoomed, (u16)m->playerX, (u16)m->playerY, cursor->pos.x, cursor->pos.y);
    map_affine_center(m, x, y, cursor->pos.x, cursor->pos.y, xRet, yRet);
    cursor->destX = (s16)((x - m->cursorSpriteState.left) * grid + m->centerX + half);
    cursor->destY = (s16)((y - m->cursorSpriteState.top) * grid + m->centerY + half);
    o[PGMAP_SPRITE_PLAYER].destX = (s16)((o[PGMAP_SPRITE_PLAYER].cellX - m->cursorSpriteState.left) * grid + m->centerX + half);
    o[PGMAP_SPRITE_PLAYER].destY = (s16)((o[PGMAP_SPRITE_PLAYER].cellY - m->cursorSpriteState.top) * grid + m->centerY + half);
    map_place_fly_points(m, 1);
    for (i = PGMAP_SPRITE_CURSOR; i < m->objManager->num; i++) {
        o[i].stepX = FX_Div(FX32_CONST(o[i].destX - o[i].pos.x), FX32_CONST(m->cursorSpeed));
        o[i].stepY = FX_Div(FX32_CONST(o[i].destY - o[i].pos.y), FX32_CONST(m->cursorSpeed));
        PokegearManagedObject_SetFixCoords(&o[i], o[i].pos.x, o[i].pos.y);
    }
}

static void map_toggle_zoom(PokegearMapAppData *m)
{
    m->zoomed ^= 1;
    m->cursorSpeed = 4;
    map_zoom_targets(m, (u16)m->playerX, (u16)m->playerY, &m->cursorSpriteState.affineX, &m->cursorSpriteState.affineY);
    m->moving = 1;
    m->zooming = 1;
    map_zoom_button(m, 1, m->zoomed);
    PokegearApp_PlaySE(m->zoomed ? PG_SE_ZOOM_IN : PG_SE_ZOOM_OUT);
    map_place_fly_points(m, 2);
}

/* ov101_021EB818 */
static int map_keys(PokegearMapAppData *m)
{
    u32 newKeys = gSystem.pressedKeys;
    int flyDest;

    if (gSystem.heldKeys == 0 || m->moving || m->zooming || m->stepping)
        return -1;
    if (newKeys & PAD_BUTTON_X) {
        map_toggle_zoom(m);
        return -1;
    }
    if (newKeys & PAD_BUTTON_A) {
        flyDest = map_fly_at(m, (u16)m->playerX, (u16)(m->playerY - 2));
        if (flyDest > 0) {
            PokegearApp_PlaySE(PG_SE_DECIDE);
            return map_ask_fly(m, flyDest);
        }
        return -1;
    }
    if (map_step_cursor(m))
        map_after_move(m);
    return -1;
}

/* PokegearMap_HandleKeyInput */
static int map_handle_key_input(PokegearMapAppData *m)
{
    int ret;

    if ((gSystem.pressedKeys & PAD_BUTTON_B) && !m->stepping) {
        m->pokegear->cursorInAppSwitchZone = TRUE;
        PokegearCursorManager_SetCursorSpritesDrawState(m->pokegear->cursorManager, 0, TRUE);
        PokegearCursorManager_SetSpecIndexAndCursorPos(m->pokegear->cursorManager, 0, PokegearApp_AppIdToButtonIndex(m->pokegear));
        map_hide_cursor(m);
        PokegearApp_PlaySE(PG_SE_CANCEL);
        return -1;
    }
    ret = map_keys(m);
    if (ret == GEAR_RETURN_8)
        return ret;
    map_update_all(m);
    return -1;
}

/* ov101_021EBDEC: the pen's reach when it takes hold of the map. */
static void map_begin_drag(PokegearMapAppData *m)
{
    s16 x, y, xTile, yTile, xPixel, yPixel, y0, y1, x1, x0;
    u8 grid, half, width, height;

    if (m->zoomed) {
        width = 12;
        height = 8;
    } else {
        width = 24;
        height = 17;
    }
    grid = (u8)(8 * (1 + m->zoomed));
    half = (u8)(grid / 2);
    x = (s16)(gSystem.touchX - m->centerX);
    y = (s16)(gSystem.touchY - m->centerY);
    xPixel = (s16)((x % grid) - half);
    yPixel = (s16)((y % grid) - half);
    xTile = (s16)(x / grid);
    yTile = (s16)(y / grid);
    y0 = (s16)(m->playerY - yTile);
    y1 = (s16)(m->playerY + (height - 1 - yTile));
    x0 = (s16)(m->playerX - xTile);
    x1 = (s16)(m->playerX + (width - 1 - xTile));
    if (y0 < (s16)m->minYscroll) y0 = (s16)m->minYscroll;
    if (x0 < (s16)m->minXscroll) x0 = (s16)m->minXscroll;
    if (y1 > (s16)m->maxYscroll) y1 = (s16)m->maxYscroll;
    if (x1 > (s16)m->maxXscroll) x1 = (s16)m->maxXscroll;
    m->pixelTop = (s16)((m->playerY - y0) * grid + yPixel);
    m->pixelLeft = (s16)((m->playerX - x0) * grid + xPixel);
    m->pixelBottom = (s16)((y1 - m->playerY) * grid + yPixel);
    m->pixelRight = (s16)((x1 - m->playerX) * grid + xPixel);
}

/* ov101_021EC980: the cell under the pen. */
static void map_cell_under_pen(PokegearMapAppData *m, s16 *px, s16 *py)
{
    static const u8 limits[2][4] = { { 1, 16, 1, 22 }, { 1, 7, 1, 10 } };
    const u8 *l = limits[m->zoomed];
    s16 r7 = (s16)((gSystem.touchX - m->centerX) / ((m->zoomed + 1) * 8));
    s16 r4 = (s16)((gSystem.touchY - m->centerY) / ((m->zoomed + 1) * 8));

    if (r4 < l[0]) r4 = l[0];
    if (r4 > l[1]) r4 = l[1];
    if (r7 < l[2]) r7 = l[2];
    if (r7 > l[3]) r7 = l[3];
    r7 += (s16)m->cursorSpriteState.left;
    r4 += (s16)m->cursorSpriteState.top;
    if (px) *px = r7;
    if (py) *py = r4;
    map_cursor_to_player_cell(m);
}

/* ov101_021EBA44 / FlyMap_HandleTouchInput_NotDragging: a tap on the map
 * moves the cursor there; on a town, the fly menu. */
static int map_touch(PokegearMapAppData *m, BOOL *isTouch)
{
    u16 pixel = 1;
    int input, flyDest;

    if (!gSystem.touchHeld)
        return -1;
    if (m->moving || m->zooming)
        return -1;
    input = TouchScreen_CheckRectanglePressed(sMapButtonHitboxes);
    if (input != TOUCHSCREEN_INPUT_NONE) {
        *isTouch = TRUE;
        map_cursor_to_player_cell(m);
        if (input == 1)
            map_toggle_zoom(m);
        return -1;
    }
    if (!TouchScreen_LocationPressed(&sMapTouchRect))
        return -1;
    if (!Bg_DoesPixelAtXYMatchVal(m->pokegear->bgConfig, BG_LAYER_MAIN_1, gSystem.touchX, gSystem.touchY, &pixel))
        return -1;
    PokegearApp_PlaySE(PG_SE_MAPTOUCH);
    *isTouch = TRUE;
    map_cell_under_pen(m, &m->playerX, &m->playerY);
    map_after_move(m);
    flyDest = map_fly_at(m, (u16)m->playerX, (u16)(m->playerY - 2));
    if (flyDest > 0) {
        PokegearApp_PlaySE(PG_SE_DECIDE);
        return map_ask_fly(m, flyDest);
    }
    m->dragStartX = (s16)gSystem.touchX;
    m->dragStartY = (s16)gSystem.touchY;
    map_begin_drag(m);
    m->dragging = 1;
    return -1;
}

/* ov101_021EBF44 .. ov101_021EC04C: a drag is clamped to the plane and to
 * keeping the cursor on the screen. */
static s16 drag_clamp_x(PokegearMapAppData *m, s16 x)
{
    s16 xMin = (s16)(m->cursorSpriteState.left - x + 1);
    s16 xMax = (s16)(m->cursorSpriteState.right - x - 1);

    if (m->playerX >= xMin && xMax >= m->playerX)
        return x;
    if (m->playerX <= xMin)
        return (s16)(x + (xMin - m->playerX));
    return (s16)(x - (m->playerX - xMax));
}

static s16 drag_x(PokegearMapAppData *m, s16 x, s16 dxMax)
{
    s16 dx;

    if (x > 0) {
        dx = (s16)(m->cursorSpriteState.left - m->minXscroll);
        if (dx <= 0)
            return 0;
        if (dx < dxMax)
            return drag_clamp_x(m, dx);
    } else {
        dx = (s16)(m->maxXscroll - m->cursorSpriteState.right);
        if (dx <= 0)
            return 0;
        if (dx < dxMax)
            return drag_clamp_x(m, (s16)-dx);
    }
    return drag_clamp_x(m, x);
}

static s16 drag_clamp_y(PokegearMapAppData *m, s16 y)
{
    s16 yMin = (s16)(m->cursorSpriteState.top - y + 1);
    s16 yMax = (s16)(m->cursorSpriteState.bottom - y);

    if (m->playerY >= yMin && yMax >= m->playerY)
        return y;
    if (m->playerY <= yMin)
        return (s16)(y + (yMin - m->playerY));
    return (s16)(y - (m->playerY - yMax));
}

static s16 drag_y(PokegearMapAppData *m, s16 y, s16 dyMax)
{
    s16 dy;

    if (y > 0) {
        dy = (s16)(m->cursorSpriteState.top - m->minYscroll);
        if (dy <= 0)
            return 0;
        if (dy < dyMax)
            return drag_clamp_y(m, dy);
    } else {
        dy = (s16)(m->maxYscroll - m->cursorSpriteState.bottom);
        if (dy <= 0)
            return 0;
        if (dy < dyMax)
            return drag_clamp_y(m, (s16)-dy);
    }
    return drag_clamp_y(m, y);
}

/* FlyMap_HandleTouchInput_DraggingMap */
static int map_drag(PokegearMapAppData *m)
{
    s16 touchX = (s16)gSystem.touchX, touchY = (s16)gSystem.touchY;
    s16 x, y, absX, absY, xOffset, yOffset, half, grid;

    grid = (s16)((m->zoomed * 8 + 8) / 2);
    half = (s16)(m->zoomed ? 9 : 5);
    if (m->cursorSpeed != 0) {
        m->cursorSpriteState.x -= m->dragWordX;
        m->cursorSpriteState.y -= m->dragWordY;
        map_shift_objects(m, m->dragWordX, m->dragWordY);
        PokegearObjectsManager_UpdateAllSpritesPos(m->objManager);
        m->requestAffineUpdate = TRUE;
        m->cursorSpeed = 0;
        return -1;
    }
    if (!gSystem.touchHeld) {
        m->dragging = FALSE;
        return -1;
    }
    x = absX = (s16)(touchX - m->dragStartX);
    y = absY = (s16)(touchY - m->dragStartY);
    if (absX < 0) absX = (s16)-absX;
    if (absY < 0) absY = (s16)-absY;
    absX /= half;
    absY /= half;
    xOffset = (s16)(x % half);
    yOffset = (s16)(y % half);
    if (absX < 1 && absY < 1)
        return -1;
    m->dragWordX = m->dragWordY = 0;
    if (absX > 0) {
        x = drag_x(m, (s16)(x / half), absX);
        if (x != 0) {
            m->cursorSpriteState.x -= x * grid;
            m->cursorSpriteState.right -= x;
            m->cursorSpriteState.left -= x;
            m->dragStartX = (s16)(touchX - xOffset);
            m->cursorSpeed = 1;
            m->dragWordX = (s16)(x * grid);
        }
    } else {
        x = 0;
    }
    if (absY > 0) {
        y = drag_y(m, (s16)(y / half), absY);
        if (y != 0) {
            m->cursorSpriteState.y -= y * grid;
            m->cursorSpriteState.bottom -= y;
            m->cursorSpriteState.top -= y;
            m->dragStartY = (s16)(touchY - yOffset);
            m->cursorSpeed = 1;
            m->dragWordY = (s16)(y * grid);
        }
    } else {
        y = 0;
    }
    if (x != 0 || y != 0) {
        map_shift_objects(m, (s16)(x * grid), (s16)(y * grid));
        map_cull(m);
        PokegearObjectsManager_UpdateAllSpritesPos(m->objManager);
        m->requestAffineUpdate = TRUE;
    }
    return -1;
}

/* PokegearMap_HandleTouchInput */
static int map_handle_touch_input(PokegearMapAppData *m, BOOL *isTouch)
{
    int ret = -1;

    if (!m->dragging)
        ret = PokegearApp_HandleTouchInput_SwitchApps(m->pokegear);
    if (ret != -1) {
        *isTouch = TRUE;
        return ret;
    }
    if (!m->dragging) {
        ret = map_touch(m, isTouch);
        if (*isTouch && m->pokegear->cursorInAppSwitchZone == TRUE) {
            m->pokegear->cursorInAppSwitchZone = FALSE;
            map_deselect_app(m);
        }
        if (ret == GEAR_RETURN_8)
            return ret;
        map_update_all(m);
    } else {
        *isTouch = TRUE;
        ret = map_drag(m);
    }
    return ret;
}

/* ov101_021EC304: the cursor's step, scrolling when it reaches an edge;
 * ov101_021EC778: the zoom's frames. */
static void map_auto_cull(PokegearManagedObject *o, s16 y)
{
    if (!o->autoCull || o->sprite == NULL)
        return;
    Sprite_SetDrawFlag(o->sprite, !(y > 216 || y < 0));
}

static void map_cull(PokegearMapAppData *m)
{
    PokegearManagedObject *o = m->objManager->objects;
    u16 i;

    for (i = PGMAP_SPRITE_CURSOR; i < m->objManager->num; i++)
        if (i != PGMAP_SPRITE_PLAYER || m->playerShown)
            map_auto_cull(&o[i], o[i].pos.y);
}

static void map_run_step(PokegearMapAppData *m)
{
    static const u8 scrollLimits[2][2] = { { 22, 10 }, { 16, 7 } };
    PokegearManagedObject *cursor = &m->objManager->objects[PGMAP_SPRITE_CURSOR];
    u8 flag = 0;
    s16 dx = 0, dy = 0, delta;
    u16 x, y;

    if (!m->stepping)
        return;
    delta = (s16)(m->zoomed ? 8 : 4);
    map_pixel_to_cell(m, m->cursorPos.x, m->cursorPos.y, &x, &y);
    if (m->moveCursorDirection & 1) {
        if (y <= 1) { dy -= delta; flag = 1; } else cursor->pos.y -= delta;
    } else if (m->moveCursorDirection & 2) {
        if (y >= scrollLimits[1][m->zoomed]) { dy += delta; flag = 1; } else cursor->pos.y += delta;
    }
    if (m->moveCursorDirection & 4) {
        if (x <= 1) { dx -= delta; flag = 1; } else cursor->pos.x -= delta;
    } else if (m->moveCursorDirection & 8) {
        if (x >= scrollLimits[0][m->zoomed]) { dx += delta; flag = 1; } else cursor->pos.x += delta;
    }
    if (flag) {
        m->requestAffineUpdate = TRUE;
        m->cursorSpriteState.x += dx;
        m->cursorSpriteState.y += dy;
        map_shift_objects(m, (s16)-dx, (s16)-dy);
        map_cull(m);
    }
    PokegearObjectsManager_UpdateAllSpritesPos(m->objManager);
    if (--m->cursorSpeed == 0) {
        map_cursor_bounds(m);
        m->moving = 0;
        m->stepping = 0;
        m->moveCursorDirection = 0;
    }
}

static void map_run_zoom(PokegearMapAppData *m)
{
    PokegearManagedObject *o = m->objManager->objects;
    PokegearManagedObject *cursor = &o[PGMAP_SPRITE_CURSOR];
    VecFx32 scale;
    u16 i;
    s16 x, y;

    if (!m->zooming)
        return;
    if (m->zoomed) {
        m->cursorSpriteState.xRatio += FX32_CONST(0.25);
        m->cursorSpriteState.yRatio += FX32_CONST(0.25);
    } else {
        m->cursorSpriteState.xRatio -= FX32_CONST(0.25);
        m->cursorSpriteState.yRatio -= FX32_CONST(0.25);
    }
    scale.x = m->cursorSpriteState.xRatio;
    scale.y = m->cursorSpriteState.yRatio;
    scale.z = FX32_ONE;
    if (cursor->sprite != NULL)
        Sprite_SetAffineScale(cursor->sprite, &scale);
    if (--m->cursorSpeed == 0) {
        m->cursorSpriteState.x = m->cursorSpriteState.destX;
        m->cursorSpriteState.y = m->cursorSpriteState.destY;
        for (i = PGMAP_SPRITE_CURSOR; i < m->objManager->num; i++) {
            PokegearManagedObject_SetCoord(&o[i], o[i].destX, o[i].destY);
            if (i != PGMAP_SPRITE_PLAYER || m->playerShown)
                map_auto_cull(&o[i], o[i].destY);
        }
        m->moving = 0;
        m->zooming = 0;
        m->moveCursorDirection = 0;
        map_place_fly_points(m, 0);
        map_cull(m);
    } else {
        m->cursorSpriteState.fx += m->cursorSpriteState.dxStep;
        m->cursorSpriteState.fy += m->cursorSpriteState.dyStep;
        m->cursorSpriteState.x = FX_Whole(m->cursorSpriteState.fx);
        m->cursorSpriteState.y = FX_Whole(m->cursorSpriteState.fy);
        for (i = PGMAP_SPRITE_CURSOR; i < m->objManager->num; i++) {
            o[i].fixX += o[i].stepX;
            o[i].fixY += o[i].stepY;
            x = (s16)FX_Whole(o[i].fixX);
            y = (s16)FX_Whole(o[i].fixY);
            PokegearManagedObject_SetCoord(&o[i], x, y);
            if (i != PGMAP_SPRITE_PLAYER || m->playerShown)
                map_auto_cull(&o[i], y);
        }
    }
    PokegearObjectsManager_UpdateAllSpritesPos(m->objManager);
    m->requestAffineUpdate = TRUE;
}

static void map_update_all(PokegearMapAppData *m)
{
    map_run_step(m);
    map_run_zoom(m);
    if (m->objManager->objects[PGMAP_SPRITE_PLAYER].sprite != NULL && !m->playerShown)
        Sprite_SetDrawFlag(m->objManager->objects[PGMAP_SPRITE_PLAYER].sprite, FALSE);
}

/* ------------------------------------------------------------------ */
/* The card (pokegear_map.c)                                           */
/* ------------------------------------------------------------------ */

static void map_init_internal(PokegearMapAppData *m)
{
    PokegearArgs *args = m->pokegear->args;

    m->pokegear->childAppdata = m;
    m->pokegear->reselectAppCB = map_show_cursor;
    m->pokegear->deselectAppCB = map_deselect_app;
    m->pokegear->app = GEAR_APP_MAP;
    m->zoomed = openmmo_pokegear_zoomed();
    m->mapUnlockLevel = 2;
    m->matrixX = args->matrixXCoord;
    m->matrixY = (s16)(args->matrixYCoord + 2);
    m->mapID = args->mapID;
    m->playerGender = args->playerGender;
    m->playerX = args->matrixXCoord;
    m->playerY = (s16)(args->matrixYCoord + 2);
    m->minXscroll = 1;
    m->minYscroll = 1;
    m->maxXscroll = sMapXScrollLimits[m->mapUnlockLevel];
    m->maxYscroll = 17;
    m->centerY = 8;
    m->centerX = 8;
    m->yOffset = 0;
    m->xOffset = 0;
    m->canSeeSafariZone = TRUE;
    m->canFlyToGoldenrod = TRUE;
    m->isMapSinjoh = FALSE;
    m->isMapSSAqua = FALSE;
    m->playerShown = args->ported;
    m->flyDestination = -1;
    m->curRegion = (u8)Pokegear_GetCurrentRegion(m->pokegear);
    m->lastSelectedMapID = 0;
}

static int map_handle_input(PokegearMapAppData *m)
{
    BOOL isTouch = FALSE;
    int input = map_handle_touch_input(m, &isTouch);

    if (!isTouch) {
        PokegearApp_HandleInputModeChangeToButtons(m->pokegear);
        if (m->pokegear->cursorInAppSwitchZone == TRUE)
            input = PokegearApp_HandleKeyInput_SwitchApps(m->pokegear);
        else
            input = map_handle_key_input(m);
    }
    m->pokegear->appReturnCode = input;
    switch (input) {
    case TOUCH_MENU_NO_INPUT:
        break;
    case GEAR_RETURN_8:
        m->pokegear->appReturnCode = 0;
        return PGMAP_STATE_FLY_CONTEXT_MENU;
    case GEAR_RETURN_4:
        return PGMAP_STATE_FADE_OUT;
    default:
        return PGMAP_STATE_FADE_OUT_APP;
    }
    return PGMAP_STATE_HANDLE_INPUT;
}

/* FlyMap_HandleContextMenu */
static int map_context_menu(PokegearMapAppData *m)
{
    int ret = PokegearMenu_Input(m->menu);
    int header, x, z;

    if (ret == -1)
        return PGMAP_STATE_FLY_CONTEXT_MENU;
    m->pokegear->menuInputState = PokegearMenu_LastInputWasTouch(m->menu) ? MENU_INPUT_STATE_TOUCH : MENU_INPUT_STATE_BUTTONS;
    PokegearMenu_Close(m->menu);
    m->menu = NULL;
    if (ret == 0 && map_fly_landing(m->flyChosenIdx, &header, &x, &z)) {
        m->pokegear->args->setFlyDestination = TRUE;
        m->pokegear->args->selectedFlyDest = (u16)header;
        m->pokegear->args->mapCursorX = x;
        m->pokegear->args->mapCursorY = z;
        m->pokegear->appReturnCode = GEAR_RETURN_4;
        printf("openmmo: pokegear: fly chosen, HeartGold map %d -> header %d (%d,%d)\n",
               m->flyChosenIdx, header, x, z);
        return PGMAP_STATE_FADE_OUT;
    }
    if (ret == 0)
        printf("openmmo: pokegear: no landing known for HeartGold map %d\n", m->flyChosenIdx);
    map_clear_fly_line(m);
    return PGMAP_STATE_HANDLE_INPUT;
}

static int map_fade_in(PokegearMapAppData *m)
{
    PaletteData *pd = m->pokegear->plttData;
    int i;

    switch (m->state) {
    case 0:
        StartScreenFade(FADE_BOTH_SCREENS, FADE_TYPE_BRIGHTNESS_IN, FADE_TYPE_BRIGHTNESS_IN, COLOR_BLACK, 6, 1, m->heapID);
        for (i = 0; i < 8; i++)
            Bg_ToggleLayer((u8)i, 1);
        PaletteData_SetAutoTransparent(pd, TRUE);
        PaletteData_Blend(pd, PLTTBUF_MAIN_BG, 0, 224, 0, COLOR_BLACK);
        PaletteData_Blend(pd, PLTTBUF_MAIN_OBJ, 64, 192, 0, COLOR_BLACK);
        PaletteData_CommitFadedBuffers(pd);
        PaletteData_SetAutoTransparent(pd, FALSE);
        GXLayers_EngineAToggleLayers(GX_PLANEMASK_OBJ, 1);
        GXLayers_EngineBToggleLayers(GX_PLANEMASK_OBJ, 1);
        m->state++;
        break;
    case 1:
        if (IsScreenFadeDone()) {
            m->state = 0;
            return PGMAP_STATE_HANDLE_INPUT;
        }
        break;
    }
    return PGMAP_STATE_FADE_IN;
}

static int map_fade_out(PokegearMapAppData *m)
{
    int i;

    switch (m->state) {
    case 0:
        StartScreenFade(FADE_BOTH_SCREENS, FADE_TYPE_BRIGHTNESS_OUT, FADE_TYPE_BRIGHTNESS_OUT, COLOR_BLACK, 6, 1, m->heapID);
        m->state++;
        break;
    case 1:
        if (IsScreenFadeDone()) {
            for (i = 0; i < 8; i++)
                Bg_ToggleLayer((u8)i, 0);
            GXLayers_EngineAToggleLayers(GX_PLANEMASK_OBJ, 0);
            GXLayers_EngineBToggleLayers(GX_PLANEMASK_OBJ, 0);
            m->state = 0;
            return PGMAP_STATE_UNLOAD;
        }
        break;
    }
    return PGMAP_STATE_FADE_OUT;
}

static int map_fade_in_app(PokegearMapAppData *m)
{
    PaletteData *pd = m->pokegear->plttData;
    int i;

    switch (m->state) {
    case 0:
        PaletteData_SetAutoTransparent(pd, TRUE);
        m->fadeStep = 0;
        for (i = 0; i < 3; i++)
            Bg_ToggleLayer((u8)(i + BG_LAYER_MAIN_1), 1);
        G2_SetBlendBrightness(GX_BLEND_PLANEMASK_BG1 | GX_BLEND_PLANEMASK_BG2 | GX_BLEND_PLANEMASK_BG3, 0);
        map_begin_panel_scroll(m, 0);
        m->state++;
        break;
    case 1:
        if (map_fade_screen(m, 0) & map_run_panel_scroll(m, 0))
            m->state++;
        break;
    case 2:
        PaletteData_SetAutoTransparent(pd, FALSE);
        m->state = 0;
        return PGMAP_STATE_HANDLE_INPUT;
    }
    return PGMAP_STATE_FADE_IN_APP;
}

static int map_fade_out_app(PokegearMapAppData *m)
{
    PaletteData *pd = m->pokegear->plttData;
    int i;

    switch (m->state) {
    case 0:
        PaletteData_SetAutoTransparent(pd, TRUE);
        for (i = 0; i < 3; i++)
            Bg_ToggleLayer((u8)(i + BG_LAYER_MAIN_1), 1);
        map_begin_panel_scroll(m, 1);
        /* The fly points wear the skin's palette, which the wipe leaves lit. */
        map_place_fly_points(m, 2);
        m->fadeStep = 0;
        m->state++;
        break;
    case 1:
        if (map_fade_screen(m, 1) & map_run_panel_scroll(m, 1))
            m->state++;
        break;
    case 2:
        PaletteData_Blend(pd, PLTTBUF_MAIN_BG, 0, 224, 16, COLOR_BLACK);
        PaletteData_Blend(pd, PLTTBUF_MAIN_OBJ, 64, 192, 16, COLOR_BLACK);
        PaletteData_CommitFadedBuffers(pd);
        for (i = 0; i < 3; i++)
            Bg_ToggleLayer((u8)(i + BG_LAYER_MAIN_1), 0);
        PaletteData_SetAutoTransparent(pd, FALSE);
        m->state = 0;
        return PGMAP_STATE_UNLOAD;
    }
    return PGMAP_STATE_FADE_OUT_APP;
}

BOOL PokegearMap_Init(ApplicationManager *man, int *state)
{
    PokegearAppData *app = ApplicationManager_Args(man);
    PokegearMapAppData *m;

    (void)state;
    Heap_Create(HEAP_ID_APPLICATION, POKEGEAR_CARD_HEAP, 0x30000);
    m = ApplicationManager_NewData(man, sizeof *m, POKEGEAR_CARD_HEAP);
    memset(m, 0, sizeof *m);
    m->pokegear = app;
    m->heapID = POKEGEAR_CARD_HEAP;
    map_init_internal(m);
    return TRUE;
}

BOOL PokegearMap_Main(ApplicationManager *man, int *state)
{
    PokegearMapAppData *m = ApplicationManager_Data(man);

    switch (*state) {
    case PGMAP_STATE_LOAD:
        if (map_load_gfx(m))
            *state = m->pokegear->isSwitchApp ? PGMAP_STATE_FADE_IN_APP : PGMAP_STATE_FADE_IN;
        break;
    case PGMAP_STATE_HANDLE_INPUT:
        *state = map_handle_input(m);
        break;
    case PGMAP_STATE_UNLOAD:
        if (map_unload_gfx(m))
            *state = PGMAP_STATE_QUIT;
        break;
    case PGMAP_STATE_FADE_IN:
        *state = map_fade_in(m);
        break;
    case PGMAP_STATE_FADE_OUT:
        *state = map_fade_out(m);
        break;
    case PGMAP_STATE_FADE_IN_APP:
        *state = map_fade_in_app(m);
        break;
    case PGMAP_STATE_FADE_OUT_APP:
        *state = map_fade_out_app(m);
        break;
    case PGMAP_STATE_FLY_CONTEXT_MENU:
        *state = map_context_menu(m);
        break;
    case PGMAP_STATE_QUIT:
        return TRUE;
    }
    return FALSE;
}

BOOL PokegearMap_Exit(ApplicationManager *man, int *state)
{
    PokegearMapAppData *m = ApplicationManager_Data(man);

    (void)state;
    openmmo_pokegear_set_zoomed(m->zoomed);
    m->pokegear->reselectAppCB = NULL;
    m->pokegear->deselectAppCB = NULL;
    if (m->pokegear->appReturnCode != GEAR_RETURN_CANCEL)
        m->pokegear->isSwitchApp = TRUE;
    ApplicationManager_FreeData(man);
    Heap_Destroy(POKEGEAR_CARD_HEAP);
    return TRUE;
}
