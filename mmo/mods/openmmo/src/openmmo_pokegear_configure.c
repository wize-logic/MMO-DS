/* The Pokegear's first card: the six skins. */

#include <string.h>

#include "openmmo_pokegear.h"

#include "constants/graphics.h"
#include "font.h"
#include "generated/fade_types.h"
#include "graphics.h"
#include "gx_layers.h"
#include "heap.h"
#include "screen_fade.h"
#include "system.h"
#include "touch_screen.h"

/* pgconf_gra members. */
#define PGCONF_OBJ_PLTT   0
#define PGCONF_OBJ_CHAR   1
#define PGCONF_OBJ_CELL   2
#define PGCONF_OBJ_ANIM   3
#define PGCONF_BG_PLTT    4   /* + skin */
#define PGCONF_BG_CHAR    10  /* + skin */
#define PGCONF_BG_SCRN    16  /* + skin */
#define PGCONF_BTN_SCRN   22  /* + skin */

#define PGCONF_MSG_BANK   270
#define PGCONF_SKINS      6

enum {
    PGCONF_STATE_LOAD = 0,
    PGCONF_STATE_HANDLE_INPUT,
    PGCONF_STATE_UNLOAD,
    PGCONF_STATE_CONTEXT_MENU,
    PGCONF_STATE_SWAP_SKINS,
    PGCONF_STATE_FADE_IN,
    PGCONF_STATE_FADE_OUT,
    PGCONF_STATE_FADE_IN_APP,
    PGCONF_STATE_FADE_OUT_APP,
    PGCONF_STATE_QUIT
};

typedef struct {
    enum HeapID heapID;
    int state;
    int substate;
    PokegearAppData *pokegear;
    u8 selectedSkin;
    u16 unlockedSkins;
    u8 skin;
    Sprite *sprites[9];
    MessageLoader *msg;
    PokegearMenu *menu;
    void *scrnRaw;
    NNSG2dScreenData *scrn;
} PokegearConfigureAppData;

static const PokegearSpriteTemplate sSpriteTemplates[5] = {
    { 0, 0, 0, 0, 0, 1, 6, NNS_G2D_VRAM_TYPE_2DMAIN },
    { 0, 0, 0, 0, 1, 1, 6, NNS_G2D_VRAM_TYPE_2DMAIN },
    { 0, 0, 0, 0, 2, 1, 6, NNS_G2D_VRAM_TYPE_2DMAIN },
    { 0, 0, 0, 0, 3, 1, 6, NNS_G2D_VRAM_TYPE_2DMAIN },
    { 0, 0, 0, 0, 8, 1, 4, NNS_G2D_VRAM_TYPE_2DMAIN },
};

static const PokegearCursorGrid sCursorButtons[6] = {
    { 0, 2, 1, 3, 3, 48, 44, -22, 22, -18, 18 },
    { 1, 0, 2, 4, 4, 128, 44, -22, 22, -18, 18 },
    { 2, 1, 0, 5, 5, 208, 44, -22, 22, -18, 18 },
    { 3, 5, 4, 0, 0, 48, 116, -22, 22, -18, 18 },
    { 4, 3, 5, 1, 1, 128, 116, -22, 22, -18, 18 },
    { 5, 4, 3, 2, 2, 208, 116, -22, 22, -18, 18 },
};

/* Where the CHANGE / QUIT popup opens for each skin's button (x, y in
 * tiles); the third column was the text alignment inside it. */
static const u8 sContextMenuParam[6][2] = {
    { 10, 2 }, { 20, 2 }, { 20, 2 }, { 10, 11 }, { 20, 11 }, { 20, 11 },
};

static const TouchScreenRect sSkinHitboxes[] = {
    { .rect = { 24, 64, 24, 72 } },
    { .rect = { 24, 64, 104, 152 } },
    { .rect = { 24, 64, 184, 232 } },
    { .rect = { 96, 136, 24, 72 } },
    { .rect = { 96, 136, 104, 152 } },
    { .rect = { 96, 136, 184, 232 } },
    { .rect = { TOUCHSCREEN_TABLE_TERMINATOR, 0, 0, 0 } },
};

static void conf_load_graphics_internal(PokegearConfigureAppData *c)
{
    NARC *narc = openmmo_pokegear_narc(PG_NARC_CONF, c->heapID);

    if (narc == NULL)
        return;
    Graphics_LoadTilesToBgLayerFromOpenNARC(narc, PGCONF_BG_CHAR + c->skin, c->pokegear->bgConfig, BG_LAYER_MAIN_3, 0, 0, FALSE, c->heapID);
    Graphics_LoadTilemapToBgLayerFromOpenNARC(narc, PGCONF_BG_SCRN + c->skin, c->pokegear->bgConfig, BG_LAYER_MAIN_3, 0, 0, FALSE, c->heapID);
    c->scrnRaw = Graphics_GetScrnDataFromOpenNARC(narc, PGCONF_BTN_SCRN + c->skin, FALSE, &c->scrn, c->heapID);
    NARC_dtor(narc);
    Bg_ScheduleTilemapTransfer(c->pokegear->bgConfig, BG_LAYER_MAIN_3);
}

static void conf_unload_graphics_internal(PokegearConfigureAppData *c)
{
    if (c->scrnRaw != NULL) {
        Heap_Free(c->scrnRaw);
        c->scrnRaw = NULL;
        c->scrn = NULL;
    }
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

static void conf_load_palettes(PokegearConfigureAppData *c, BOOL isInit)
{
    PaletteData *pd = c->pokegear->plttData;
    NARC *narc = openmmo_pokegear_narc(PG_NARC_CONF, c->heapID);

    if (narc == NULL)
        return;
    pltt_load(pd, narc, PGCONF_BG_PLTT + c->skin, c->heapID, PLTTBUF_MAIN_BG, 0x1C0, 0, 0);
    pltt_load(pd, narc, PGCONF_BG_PLTT + c->skin, c->heapID, PLTTBUF_SUB_BG, 0x180, 0, 0);
    if (isInit) {
        pltt_load(pd, narc, PGCONF_OBJ_PLTT, c->heapID, PLTTBUF_MAIN_OBJ, 0x160, 0x40, 0);
        pltt_load(pd, narc, PGCONF_OBJ_PLTT, c->heapID, PLTTBUF_SUB_OBJ, 0x160, 0x40, 0);
    }
    PaletteData_SetAutoTransparent(pd, TRUE);
    if (isInit) {
        PaletteData_Blend(pd, PLTTBUF_MAIN_BG, 0, 0xE0, 16, COLOR_BLACK);
        PaletteData_Blend(pd, PLTTBUF_MAIN_OBJ, 0x40, 0xC0, 16, COLOR_BLACK);
    } else {
        PaletteData_Blend(pd, PLTTBUF_MAIN_BG, 0, 0x100, 0, COLOR_BLACK);
        PaletteData_Blend(pd, PLTTBUF_MAIN_OBJ, 0, 0x100, 0, COLOR_BLACK);
    }
    PaletteData_CommitFadedBuffers(pd);
    PaletteData_SetAutoTransparent(pd, FALSE);
    NARC_dtor(narc);
}

static void conf_set_new_skin(PokegearConfigureAppData *c, int skin)
{
    BgConfig *bg = c->pokegear->bgConfig;

    c->skin = (u8)skin;
    Bg_ClearTilemap(bg, BG_LAYER_MAIN_2);
    if (c->scrn != NULL)
        Bg_CopyToTilemapRect(bg, BG_LAYER_MAIN_2, (u8)(10 * (skin % 3) + 2), (u8)(9 * (skin / 3) + 2), 9, 7,
                             c->scrn->rawData, 0, 0, (u8)(c->scrn->screenWidth / 8), (u8)(c->scrn->screenHeight / 8));
    Bg_ScheduleTilemapTransfer(bg, BG_LAYER_MAIN_2);
}

static void conf_draw_unlocked_buttons(PokegearConfigureAppData *c)
{
    BgConfig *bg = c->pokegear->bgConfig;
    u16 mask = 1;
    int i;

    for (i = 0; i < PGCONF_SKINS; i++, mask <<= 1) {
        if (c->unlockedSkins & mask)
            continue;
        if (c->scrn != NULL)
            Bg_CopyToTilemapRect(bg, BG_LAYER_MAIN_3, (u8)(10 * (i % 3) + 3), (u8)(9 * (i / 3) + 3), 6, 5,
                                 c->scrn->rawData, 6, 0, (u8)(c->scrn->screenWidth / 8), (u8)(c->scrn->screenHeight / 8));
    }
    Bg_ScheduleTilemapTransfer(bg, BG_LAYER_MAIN_3);
    conf_set_new_skin(c, c->skin);
}

static void conf_init_bgs(PokegearConfigureAppData *c)
{
    BgConfig *bg = c->pokegear->bgConfig;
    BgTemplate t[6] = {
        { 0, 0, 0x800, 0, BG_SCREEN_SIZE_256x256, GX_BG_COLORMODE_16, GX_BG_SCRBASE_0xf000, GX_BG_CHARBASE_0x10000, GX_BG_EXTPLTT_01, 1, GX_BG_AREAOVER_XLU, FALSE },
        { 0, 0, 0x800, 0, BG_SCREEN_SIZE_256x256, GX_BG_COLORMODE_16, GX_BG_SCRBASE_0xe800, GX_BG_CHARBASE_0x00000, GX_BG_EXTPLTT_01, 2, GX_BG_AREAOVER_XLU, FALSE },
        { 0, 0, 0x800, 0, BG_SCREEN_SIZE_256x256, GX_BG_COLORMODE_16, GX_BG_SCRBASE_0xe000, GX_BG_CHARBASE_0x00000, GX_BG_EXTPLTT_01, 3, GX_BG_AREAOVER_XLU, FALSE },
        { 0, 0, 0x800, 0, BG_SCREEN_SIZE_256x256, GX_BG_COLORMODE_16, GX_BG_SCRBASE_0xf000, GX_BG_CHARBASE_0x00000, GX_BG_EXTPLTT_01, 0, GX_BG_AREAOVER_XLU, FALSE },
        { 0, 0, 0x800, 0, BG_SCREEN_SIZE_256x256, GX_BG_COLORMODE_16, GX_BG_SCRBASE_0xe800, GX_BG_CHARBASE_0x00000, GX_BG_EXTPLTT_01, 1, GX_BG_AREAOVER_XLU, FALSE },
        { 0, 0, 0x800, 0, BG_SCREEN_SIZE_256x256, GX_BG_COLORMODE_16, GX_BG_SCRBASE_0xe000, GX_BG_CHARBASE_0x00000, GX_BG_EXTPLTT_01, 2, GX_BG_AREAOVER_XLU, FALSE },
    };
    int i;

    GX_SetGraphicsMode(GX_DISPMODE_GRAPHICS, GX_BGMODE_0, GX_BG0_AS_2D);
    Bg_InitFromTemplate(bg, BG_LAYER_MAIN_1, &t[0], BG_TYPE_STATIC);
    Bg_InitFromTemplate(bg, BG_LAYER_MAIN_2, &t[1], BG_TYPE_STATIC);
    Bg_InitFromTemplate(bg, BG_LAYER_MAIN_3, &t[2], BG_TYPE_STATIC);
    Bg_InitFromTemplate(bg, BG_LAYER_SUB_1, &t[3], BG_TYPE_STATIC);
    Bg_InitFromTemplate(bg, BG_LAYER_SUB_2, &t[4], BG_TYPE_STATIC);
    Bg_InitFromTemplate(bg, BG_LAYER_SUB_3, &t[5], BG_TYPE_STATIC);
    for (i = 0; i < 3; i++) {
        Bg_ClearTilemap(bg, (u8)(BG_LAYER_MAIN_1 + i));
        Bg_ClearTilesRange((u8)(BG_LAYER_MAIN_1 + i), 0x20, 0, c->heapID);
        Bg_ClearTilemap(bg, (u8)(BG_LAYER_SUB_1 + i));
        Bg_ClearTilesRange((u8)(BG_LAYER_SUB_1 + i), 0x20, 0, c->heapID);
    }
    for (i = 1; i < 8; i++)
        if (i != 4)
            Bg_ToggleLayer((u8)i, 0);
}

static void conf_create_sprites(PokegearConfigureAppData *c)
{
    int i;

    for (i = 0; i <= 4; i++) {
        c->sprites[i] = PokegearApp_CreateSprite(c->pokegear, &sSpriteTemplates[i]);
        if (c->sprites[i] == NULL)
            continue;
        Sprite_SetExplicitPriority(c->sprites[i], 1);
        Sprite_SetDrawFlag(c->sprites[i], FALSE);
        Sprite_SetAnimateFlag(c->sprites[i], TRUE);
    }
    for (i = 5; i <= 8; i++) {
        c->sprites[i] = PokegearApp_CreateSprite(c->pokegear, &sSpriteTemplates[i - 5]);
        if (c->sprites[i] == NULL)
            continue;
        Sprite_SetExplicitPriority(c->sprites[i], 1);
        Sprite_SetPriority(c->sprites[i], 0);
        Sprite_SetDrawFlag(c->sprites[i], FALSE);
        Sprite_SetAnimateFlag(c->sprites[i], FALSE);
    }
}

static void conf_delete_sprites(PokegearConfigureAppData *c)
{
    int i;

    for (i = 0; i < 9; i++) {
        PokegearApp_DeleteSprite(c->pokegear, c->sprites[i]);
        c->sprites[i] = NULL;
    }
}

static void conf_set_initial_cursor(PokegearConfigureAppData *c)
{
    PokegearCursorManager *m = c->pokegear->cursorManager;

    conf_draw_unlocked_buttons(c);
    if (c->pokegear->cursorInAppSwitchZone == TRUE) {
        PokegearCursorManager_SetCursorSpritesDrawState(m, 0, TRUE);
        PokegearCursorManager_SetCursorSpritesDrawState(m, 1, FALSE);
        PokegearCursorManager_SetSpecIndexAndCursorPos(m, 0, PokegearApp_AppIdToButtonIndex(c->pokegear));
    } else {
        PokegearCursorManager_SetCursorSpritesDrawState(m, 0, FALSE);
        PokegearCursorManager_SetCursorSpritesDrawState(m, 1, TRUE);
        PokegearCursorManager_SetSpecIndexAndCursorPos(m, 1, 0);
    }
}

static BOOL conf_load_gfx(PokegearConfigureAppData *c)
{
    switch (c->substate) {
    case 0:
        conf_init_bgs(c);
        Font_InitManager(FONT_SUBSCREEN, c->heapID);
        conf_load_graphics_internal(c);
        PokegearApp_CreateSpriteManager(c->pokegear, GEAR_APP_CONFIGURE);
        conf_load_palettes(c, TRUE);
        break;
    case 1:
        conf_create_sprites(c);
        PokegearCursorManager_AddButtons(c->pokegear->cursorManager, sCursorButtons, 6, 0, c->heapID,
                                         c->sprites[0], c->sprites[1], c->sprites[2], c->sprites[3]);
        PokegearCursorManager_SetCursorSpritesDrawState(c->pokegear->cursorManager, 1, FALSE);
        c->msg = openmmo_pokegear_msg(PGCONF_MSG_BANK, c->heapID);
        conf_set_initial_cursor(c);
        c->substate = 0;
        return TRUE;
    }
    c->substate++;
    return FALSE;
}

static BOOL conf_unload_gfx(PokegearConfigureAppData *c)
{
    if (c->menu != NULL) {
        PokegearMenu_Close(c->menu);
        c->menu = NULL;
    }
    if (c->msg != NULL) {
        MessageLoader_Free(c->msg);
        c->msg = NULL;
    }
    PokegearCursorManager_RemoveCursor(c->pokegear->cursorManager, 1);
    conf_delete_sprites(c);
    PokegearApp_DestroySpriteManager(c->pokegear);
    conf_unload_graphics_internal(c);
    Font_Free(FONT_SUBSCREEN);
    Pokegear_ClearAppBgLayers(c->pokegear);
    return TRUE;
}

static void conf_toggle_button_focus(PokegearConfigureAppData *c, int skin, BOOL selected)
{
    if (selected && c->menu != NULL) {
        int x = PokegearMenu_X(c->menu) * 8;
        int y = PokegearMenu_Y(c->menu) * 8;
        int width = (PokegearMenu_Width(c->menu) + 2) * 8;

        G2_SetWnd0InsidePlane(31, FALSE);
        G2_SetWnd1InsidePlane(31, FALSE);
        G2_SetWndOutsidePlane(31, TRUE);
        G2_SetWnd0Position(x, y, x + width, y + 56);
        G2_SetWnd1Position(24 + (skin % 3) * 80, 24 + (skin / 3) * 72, 72 + (skin % 3) * 80, 64 + (skin / 3) * 72);
        GX_SetVisibleWnd(GX_WNDMASK_W0 | GX_WNDMASK_W1);
        G2_SetBlendBrightness(GX_BLEND_PLANEMASK_BG0 | GX_BLEND_PLANEMASK_BG1 | GX_BLEND_PLANEMASK_BG2 | GX_BLEND_PLANEMASK_BG3 | GX_BLEND_PLANEMASK_OBJ, -8);
    } else {
        G2_SetWnd0InsidePlane(GX_WND_PLANEMASK_NONE, FALSE);
        G2_SetWnd1InsidePlane(GX_WND_PLANEMASK_NONE, FALSE);
        G2_SetWndOutsidePlane(GX_WND_PLANEMASK_NONE, FALSE);
        GX_SetVisibleWnd(GX_WNDMASK_NONE);
        G2_SetBlendBrightness(GX_BLEND_PLANEMASK_BG0 | GX_BLEND_PLANEMASK_BG1 | GX_BLEND_PLANEMASK_BG2 | GX_BLEND_PLANEMASK_BG3 | GX_BLEND_PLANEMASK_OBJ, 0);
        G2_BlendNone();
    }
}

static void conf_spawn_context_menu(PokegearConfigureAppData *c, u8 skin)
{
    static const u16 entries[2] = { 0, 1 }; /* CHANGE, QUIT */

    c->selectedSkin = skin;
    c->menu = PokegearMenu_Open(c->pokegear, BG_LAYER_MAIN_1, sContextMenuParam[skin][0],
                                sContextMenuParam[skin][1], 0, 28, 14, c->msg, entries, 2, c->heapID);
    conf_toggle_button_focus(c, skin, TRUE);
    PokegearCursorManager_SetCursorSpritesAnimateFlag(c->pokegear->cursorManager, 0xFFFF, FALSE);
}

static void conf_on_reselect(void *appData)
{
    PokegearConfigureAppData *c = appData;

    PokegearCursorManager_SetSpecIndexAndCursorPos(c->pokegear->cursorManager, 1, 0xFF);
    PokegearCursorManager_SetCursorSpritesDrawState(c->pokegear->cursorManager, 0, FALSE);
    PokegearCursorManager_SetCursorSpritesDrawState(c->pokegear->cursorManager, 1, TRUE);
}

static void conf_set_app_cursor_active(PokegearConfigureAppData *c)
{
    c->pokegear->cursorInAppSwitchZone = FALSE;
    PokegearCursorManager_SetSpecIndexAndCursorPos(c->pokegear->cursorManager, 1, 0xFF);
    PokegearCursorManager_SetCursorSpritesDrawState(c->pokegear->cursorManager, 0, FALSE);
    PokegearCursorManager_SetCursorSpritesDrawState(c->pokegear->cursorManager, 1, TRUE);
}

static BOOL conf_skin_unlocked(PokegearConfigureAppData *c, u8 skin)
{
    return (c->unlockedSkins >> skin) & 1;
}

static int conf_handle_key_input(PokegearConfigureAppData *c)
{
    PokegearCursorManager *m = c->pokegear->cursorManager;
    u8 input;

    if (gSystem.pressedKeys & PAD_BUTTON_B) {
        c->pokegear->cursorInAppSwitchZone = TRUE;
        PokegearCursorManager_SetCursorSpritesDrawState(m, 1, FALSE);
        PokegearCursorManager_SetCursorSpritesDrawState(m, 0, TRUE);
        PokegearCursorManager_SetSpecIndexAndCursorPos(m, 0, PokegearApp_AppIdToButtonIndex(c->pokegear));
        PokegearApp_PlaySE(PG_SE_CANCEL);
        return TOUCH_MENU_NO_INPUT;
    }
    if (gSystem.pressedKeys & PAD_BUTTON_A) {
        input = PokegearCursorManager_GetCursorPos(m);
        if (!conf_skin_unlocked(c, input))
            return TOUCH_MENU_NO_INPUT;
        conf_spawn_context_menu(c, input);
        PokegearApp_PlaySE(PG_SE_DECIDE);
        return GEAR_RETURN_8;
    }
    if (gSystem.pressedKeys & PAD_KEY_LEFT) {
        PokegearCursorManager_MoveActiveCursor(m, 0);
        PokegearApp_PlaySE(PG_SE_CURSOR);
    } else if (gSystem.pressedKeys & PAD_KEY_RIGHT) {
        PokegearCursorManager_MoveActiveCursor(m, 1);
        PokegearApp_PlaySE(PG_SE_CURSOR);
    } else if (gSystem.pressedKeys & PAD_KEY_UP) {
        PokegearCursorManager_MoveActiveCursor(m, 2);
        PokegearApp_PlaySE(PG_SE_CURSOR);
    } else if (gSystem.pressedKeys & PAD_KEY_DOWN) {
        PokegearCursorManager_MoveActiveCursor(m, 3);
        PokegearApp_PlaySE(PG_SE_CURSOR);
    }
    return TOUCH_MENU_NO_INPUT;
}

static int conf_handle_touch_input(PokegearConfigureAppData *c)
{
    int input = PokegearApp_HandleTouchInput_SwitchApps(c->pokegear);

    if (input != TOUCH_MENU_NO_INPUT)
        return input;
    input = TouchScreen_CheckRectanglePressed(sSkinHitboxes);
    if (input != TOUCHSCREEN_INPUT_NONE) {
        if (!conf_skin_unlocked(c, (u8)input))
            return TOUCH_MENU_NO_INPUT;
        if (c->pokegear->cursorInAppSwitchZone == TRUE)
            conf_set_app_cursor_active(c);
        PokegearCursorManager_SetActiveCursorPosition(c->pokegear->cursorManager, (u8)input);
        conf_spawn_context_menu(c, (u8)input);
        PokegearApp_PlaySE(PG_SE_DECIDE);
        c->pokegear->menuInputState = MENU_INPUT_STATE_TOUCH;
        return GEAR_RETURN_8;
    }
    return TOUCH_MENU_NO_INPUT;
}

static int conf_context_menu(PokegearConfigureAppData *c)
{
    int input = PokegearMenu_Input(c->menu);

    if (input == -1)
        return PGCONF_STATE_CONTEXT_MENU;
    c->pokegear->menuInputState = PokegearMenu_LastInputWasTouch(c->menu) ? MENU_INPUT_STATE_TOUCH : MENU_INPUT_STATE_BUTTONS;
    PokegearMenu_Close(c->menu);
    c->menu = NULL;
    conf_toggle_button_focus(c, 0, FALSE);
    PokegearCursorManager_SetCursorSpritesAnimateFlag(c->pokegear->cursorManager, 0xFFFF, TRUE);
    return input == 0 ? PGCONF_STATE_SWAP_SKINS : PGCONF_STATE_HANDLE_INPUT;
}

static void conf_load_and_set_skin(PokegearConfigureAppData *c)
{
    conf_unload_graphics_internal(c);
    c->skin = c->selectedSkin;
    c->pokegear->skin = c->skin;
    PokegearApp_LoadSkinGraphics(c->pokegear, c->skin);
    PokegearUI_ReloadSkin(c->pokegear, c->skin);
    conf_load_graphics_internal(c);
    conf_load_palettes(c, FALSE);
    conf_draw_unlocked_buttons(c);
    conf_set_new_skin(c, c->skin);
}

static BOOL conf_swap_skins(PokegearConfigureAppData *c)
{
    switch (c->substate) {
    case 0:
        StartScreenFade(FADE_MAIN_THEN_SUB, FADE_TYPE_UPWARD_OUT, FADE_TYPE_UPWARD_OUT, COLOR_BLACK, 6, 1, c->heapID);
        break;
    case 1:
        if (!IsScreenFadeDone())
            return FALSE;
        conf_load_and_set_skin(c);
        break;
    case 2:
        StartScreenFade(FADE_SUB_THEN_MAIN, FADE_TYPE_DOWNWARD_IN, FADE_TYPE_DOWNWARD_IN, COLOR_BLACK, 6, 1, c->heapID);
        break;
    case 3:
        if (!IsScreenFadeDone())
            return FALSE;
        c->substate = 0;
        return TRUE;
    }
    c->substate++;
    return FALSE;
}

static int conf_fade_in(PokegearConfigureAppData *c)
{
    PaletteData *pd = c->pokegear->plttData;
    int i;

    switch (c->state) {
    case 0:
        StartScreenFade(FADE_BOTH_SCREENS, FADE_TYPE_BRIGHTNESS_IN, FADE_TYPE_BRIGHTNESS_IN, COLOR_BLACK, 6, 1, c->heapID);
        for (i = 0; i < 8; i++)
            Bg_ToggleLayer((u8)i, 1);
        PaletteData_SetAutoTransparent(pd, TRUE);
        PaletteData_Blend(pd, PLTTBUF_MAIN_BG, 0, 0xE0, 0, COLOR_BLACK);
        PaletteData_Blend(pd, PLTTBUF_MAIN_OBJ, 0x40, 0xC0, 0, COLOR_BLACK);
        PaletteData_CommitFadedBuffers(pd);
        PaletteData_SetAutoTransparent(pd, FALSE);
        GXLayers_EngineAToggleLayers(GX_PLANEMASK_OBJ, 1);
        GXLayers_EngineBToggleLayers(GX_PLANEMASK_OBJ, 1);
        c->state++;
        break;
    case 1:
        if (IsScreenFadeDone()) {
            c->state = 0;
            return PGCONF_STATE_HANDLE_INPUT;
        }
        break;
    }
    return PGCONF_STATE_FADE_IN;
}

static int conf_fade_out(PokegearConfigureAppData *c)
{
    int i;

    switch (c->state) {
    case 0:
        StartScreenFade(FADE_BOTH_SCREENS, FADE_TYPE_BRIGHTNESS_OUT, FADE_TYPE_BRIGHTNESS_OUT, COLOR_BLACK, 6, 1, c->heapID);
        c->state++;
        break;
    case 1:
        if (IsScreenFadeDone()) {
            for (i = 0; i < 8; i++)
                Bg_ToggleLayer((u8)i, 0);
            c->state = 0;
            return PGCONF_STATE_UNLOAD;
        }
        break;
    }
    return PGCONF_STATE_FADE_OUT;
}

static int conf_fade_in_app(PokegearConfigureAppData *c)
{
    PaletteData *pd = c->pokegear->plttData;
    int i;

    switch (c->state) {
    case 0:
        PaletteData_SetAutoTransparent(pd, TRUE);
        G2_SetBlendBrightness(GX_BLEND_PLANEMASK_BG1 | GX_BLEND_PLANEMASK_BG2 | GX_BLEND_PLANEMASK_BG3, 0);
        for (i = 0; i < 3; i++) {
            Bg_ToggleLayer((u8)(i + BG_LAYER_MAIN_1), 1);
            Bg_ToggleLayer((u8)(i + BG_LAYER_SUB_1), 1);
        }
        c->pokegear->fadeCounter = 0;
        c->state++;
        break;
    case 1:
        if (Pokegear_RunFadeLayers123(c->pokegear, 0))
            c->state++;
        break;
    case 2:
        PaletteData_SetAutoTransparent(pd, FALSE);
        c->pokegear->fadeCounter = 0;
        c->state = 0;
        return PGCONF_STATE_HANDLE_INPUT;
    }
    return PGCONF_STATE_FADE_IN_APP;
}

static int conf_fade_out_app(PokegearConfigureAppData *c)
{
    PaletteData *pd = c->pokegear->plttData;
    int i;

    switch (c->state) {
    case 0:
        PaletteData_SetAutoTransparent(pd, TRUE);
        c->pokegear->fadeCounter = 0;
        c->state++;
        break;
    case 1:
        if (Pokegear_RunFadeLayers123(c->pokegear, 1))
            c->state++;
        break;
    case 2:
        PaletteData_Blend(pd, PLTTBUF_MAIN_BG, 0, 0xE0, 16, COLOR_BLACK);
        PaletteData_Blend(pd, PLTTBUF_MAIN_OBJ, 0x40, 0xC0, 16, COLOR_BLACK);
        PaletteData_CommitFadedBuffers(pd);
        for (i = 0; i < 3; i++) {
            Bg_ToggleLayer((u8)(i + BG_LAYER_MAIN_1), 0);
            Bg_ToggleLayer((u8)(i + BG_LAYER_SUB_1), 0);
        }
        PaletteData_SetAutoTransparent(pd, FALSE);
        c->pokegear->fadeCounter = 0;
        c->state = 0;
        return PGCONF_STATE_UNLOAD;
    }
    return PGCONF_STATE_FADE_OUT_APP;
}

static int conf_handle_input(PokegearConfigureAppData *c)
{
    int input = conf_handle_touch_input(c);

    if (input == TOUCH_MENU_NO_INPUT) {
        PokegearApp_HandleInputModeChangeToButtons(c->pokegear);
        if (c->pokegear->cursorInAppSwitchZone == TRUE)
            input = PokegearApp_HandleKeyInput_SwitchApps(c->pokegear);
        else
            input = conf_handle_key_input(c);
    }
    switch (input) {
    case TOUCH_MENU_NO_INPUT:
        break;
    case GEAR_RETURN_4:
        c->pokegear->appReturnCode = input;
        return PGCONF_STATE_FADE_OUT;
    case GEAR_RETURN_8:
        return PGCONF_STATE_CONTEXT_MENU;
    default:
        c->pokegear->appReturnCode = input;
        return PGCONF_STATE_FADE_OUT_APP;
    }
    return PGCONF_STATE_HANDLE_INPUT;
}

BOOL PokegearConfigure_Init(ApplicationManager *man, int *state)
{
    PokegearAppData *app = ApplicationManager_Args(man);
    PokegearConfigureAppData *c;

    (void)state;
    Heap_Create(HEAP_ID_APPLICATION, POKEGEAR_CARD_HEAP, 0x20000);
    c = ApplicationManager_NewData(man, sizeof *c, POKEGEAR_CARD_HEAP);
    memset(c, 0, sizeof *c);
    c->pokegear = app;
    c->heapID = POKEGEAR_CARD_HEAP;
    app->childAppdata = c;
    app->reselectAppCB = conf_on_reselect;
    app->deselectAppCB = NULL;
    c->skin = app->skin;
    c->unlockedSkins = 0xFF;
    return TRUE;
}

BOOL PokegearConfigure_Main(ApplicationManager *man, int *state)
{
    PokegearConfigureAppData *c = ApplicationManager_Data(man);

    switch (*state) {
    case PGCONF_STATE_LOAD:
        if (conf_load_gfx(c))
            *state = c->pokegear->isSwitchApp ? PGCONF_STATE_FADE_IN_APP : PGCONF_STATE_FADE_IN;
        break;
    case PGCONF_STATE_HANDLE_INPUT:
        *state = conf_handle_input(c);
        break;
    case PGCONF_STATE_UNLOAD:
        if (conf_unload_gfx(c))
            *state = PGCONF_STATE_QUIT;
        break;
    case PGCONF_STATE_CONTEXT_MENU:
        *state = conf_context_menu(c);
        break;
    case PGCONF_STATE_SWAP_SKINS:
        if (conf_swap_skins(c))
            *state = PGCONF_STATE_HANDLE_INPUT;
        break;
    case PGCONF_STATE_FADE_IN:
        *state = conf_fade_in(c);
        break;
    case PGCONF_STATE_FADE_OUT:
        *state = conf_fade_out(c);
        break;
    case PGCONF_STATE_FADE_IN_APP:
        *state = conf_fade_in_app(c);
        break;
    case PGCONF_STATE_FADE_OUT_APP:
        *state = conf_fade_out_app(c);
        break;
    case PGCONF_STATE_QUIT:
        return TRUE;
    }
    return FALSE;
}

BOOL PokegearConfigure_Exit(ApplicationManager *man, int *state)
{
    PokegearConfigureAppData *c = ApplicationManager_Data(man);

    (void)state;
    openmmo_pokegear_set_skin(c->skin);
    c->pokegear->skin = c->skin;
    c->pokegear->reselectAppCB = NULL;
    c->pokegear->deselectAppCB = NULL;
    c->pokegear->isSwitchApp = TRUE;
    ApplicationManager_FreeData(man);
    Heap_Destroy(POKEGEAR_CARD_HEAP);
    return TRUE;
}
