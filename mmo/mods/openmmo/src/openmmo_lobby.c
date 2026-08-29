/* Character select / create as the engine's own menus. */

#include <nitro.h>
#include <stdio.h>
#include <string.h>

#include "generated/genders.h"
#include "generated/trainer_classes.h"
#include "constants/graphics.h"
#include "constants/heap.h"
#include "applications/naming_screen.h"
#include "bg_window.h"
#include "font.h"
#include "game_options.h"
#include "graphics.h"
#include "gx_layers.h"
#include "heap.h"
#include "list_menu.h"
#include "main.h"
#include "menu.h"
#include "overlay_manager.h"
#include "palette.h"
#include "pokemon.h"
#include "render_oam.h"
#include "render_window.h"
#include "sprite_system.h"
#include "sprite_util.h"
#include "vram_transfer.h"
#include "save_player.h"
#include "savedata.h"
#include "screen_fade.h"
#include "sound.h"
#include "sound_playback.h"
#include "string_gf.h"
#include "string_list.h"
#include "system.h"
#include "text.h"

#include "../../../include/appearance.h"
#include "../../../include/charcode.h"
#include "../../../include/creator.h"

#define LOBBY_HEAP       HEAP_ID_48
#define LOBBY_HEAP_SIZE  0x50000
#define LOBBY_NAME_MAX   10
#define LOBBY_MAX_CARDS  8

#define BACKGROUND_COLOR          GX_RGB(12, 12, 31)
#define UNFOCUSED_OPTION_BG_COLOR GX_RGB(26, 26, 26)
#define COLORS_LIST_END           0

#define UNFOCUSED_FRAME_TILE 1
#define FOCUSED_FRAME_TILE   (UNFOCUSED_FRAME_TILE + STANDARD_WINDOW_TILE_COUNT)
#define MSGBOX_TILE          (FOCUSED_FRAME_TILE + STANDARD_WINDOW_TILE_COUNT)
#define TITLE_TILE           64
#define LIST_TILE            (TITLE_TILE + 28 * 2)
#define HINT_TILE            (LIST_TILE + 26 * 14)
#define CARD_TILE_START      LIST_TILE

#define OPTION_WINDOW_WIDTH 26
#define CHAR_CARD_HEIGHT    TEXT_LINES_TILES(3)
#define NEW_CARD_HEIGHT     TEXT_LINES_TILES(1)
#define LIST_CREATE_W       16
#define TRAINER_SPRITE_X    200
#define TRAINER_SPRITE_Y    88

enum {
    LOBBY_SETUP = 0,
    LOBBY_FADE_IN,
    LOBBY_HOLD,
    LOBBY_LAUNCH_NAME,
    LOBBY_RUN_NAME,
    LOBBY_FADE_OUT,
    LOBBY_TEARDOWN
};

typedef struct Lobby {
    enum HeapID heapID;
    int state;
    int drawnStep;
    int drawnHeld;
    int drawnCursor;
    int cycleIndex;
    BgConfig *bgConfig;
    Window title;
    Window list;
    Window hint;
    Window cards[LOBBY_MAX_CARDS];
    int cardCount;
    StringList *choices;
    ListMenu *menu;
    NamingScreenArgs *namingArgs;
    ApplicationManager *namingApp;
    SaveData *saveData;
    SpriteSystem *spriteSys;
    SpriteManager *spriteMan;
    PaletteData *pltt;
    ManagedSprite *trainerSprite;
    int previewGfx;
    int pendingGfx;
    int pendingGender;
    int mosaic;
    int viewScroll;
    int vramLive;
} Lobby;

#define LOBBY_MOSAIC_MAX 15

mmo_creator *openmmo_lobby_creator(void);
void openmmo_lobby_commit(void);
void openmmo_sky_setup(BgConfig *bgConfig, enum HeapID heapID);
void openmmo_sky_tick(BgConfig *bgConfig);
void openmmo_sky_teardown(BgConfig *bgConfig);
int openmmo_lobby_ready_to_field(void);
void openmmo_lobby_enter_field(SaveData *save);
void openmmo_lobby_set_active(int on);

static const GXRgb sFocusBorder[] = {
    GX_RGB(1, 28, 20), GX_RGB(3, 28, 20), GX_RGB(5, 28, 20),
    GX_RGB(7, 28, 20), GX_RGB(9, 28, 20), GX_RGB(11, 28, 20),
    GX_RGB(13, 28, 20), GX_RGB(15, 28, 20), GX_RGB(17, 28, 20),
    GX_RGB(19, 28, 20), GX_RGB(21, 28, 20), GX_RGB(23, 28, 20),
    GX_RGB(25, 28, 20), GX_RGB(27, 28, 20), GX_RGB(29, 28, 20),
    GX_RGB(31, 28, 20), GX_RGB(29, 28, 20), GX_RGB(27, 28, 20),
    GX_RGB(25, 28, 20), GX_RGB(23, 28, 20), GX_RGB(21, 28, 20),
    GX_RGB(19, 28, 20), GX_RGB(17, 28, 20), GX_RGB(15, 28, 20),
    GX_RGB(13, 28, 20), GX_RGB(11, 28, 20), GX_RGB(9, 28, 20),
    GX_RGB(7, 28, 20), GX_RGB(5, 28, 20), GX_RGB(3, 28, 20),
    COLORS_LIST_END
};

static void Lobby_VBlank(void *data)
{
    Lobby *lobby = data;

    VramTransfer_Process();
    SpriteSystem_UpdateTransfer();
    RenderOam_Transfer();
    if (lobby->bgConfig != NULL)
        Bg_RunScheduledUpdates(lobby->bgConfig);
    OS_SetIrqCheckFlag(OS_IE_V_BLANK);
}

static void Lobby_SetBanks(void)
{
    GXBanks banks = {
        GX_VRAM_BG_128_A,
        GX_VRAM_BGEXTPLTT_NONE,
        GX_VRAM_SUB_BG_128_C,
        GX_VRAM_SUB_BGEXTPLTT_NONE,
        GX_VRAM_OBJ_64_E,
        GX_VRAM_OBJEXTPLTT_NONE,
        GX_VRAM_SUB_OBJ_16_I,
        GX_VRAM_SUB_OBJEXTPLTT_NONE,
        GX_VRAM_TEX_NONE,
        GX_VRAM_TEXPLTT_NONE,
    };

    GXLayers_SetBanks(&banks);
}

static String *Lobby_Latin1(enum HeapID heap, const char *s)
{
    mmo_charcode buf[64];
    String *out = String_Init(64, heap);

    mmo_utf8_to_charcode(s != NULL ? s : "", buf, 64);
    String_CopyChars(out, (const charcode_t *)buf);
    return out;
}

static void Lobby_PrintColor(Window *window, enum HeapID heap, const char *s,
                             u32 x, u32 y, TextColor color)
{
    String *str = Lobby_Latin1(heap, s);

    Text_AddPrinterWithParamsAndColor(window, FONT_SYSTEM, str, x, y,
                                      TEXT_SPEED_NO_TRANSFER, color, NULL);
    String_Free(str);
}

static void Lobby_Print(Window *window, enum HeapID heap, const char *s, u32 x, u32 y)
{
    Lobby_PrintColor(window, heap, s, x, y, TEXT_COLOR(1, 2, 15));
}

static void Lobby_Latin1FromString(const String *src, char *dst, size_t cap)
{
    uint8_t utf16[96];
    mmo_charcode_result r;
    size_t i, n = 0;

    if (cap == 0)
        return;
    dst[0] = '\0';
    if (src == NULL)
        return;
    r = mmo_charcode_to_utf16le(String_GetData(src), utf16, sizeof utf16);
    for (i = 0; i + 1 < r.written * 2 && n + 1 < cap; i += 2) {
        unsigned cp = (unsigned)utf16[i] | ((unsigned)utf16[i + 1] << 8);

        dst[n++] = (cp < 256) ? (char)cp : '?';
    }
    dst[n] = '\0';
}

static void Lobby_CycleFocus(Lobby *lobby)
{
    GXRgb *slot = HW_BG_A_PLTT_COLOR(3, 6);

    if (sFocusBorder[lobby->cycleIndex] == COLORS_LIST_END)
        lobby->cycleIndex = 0;
    *slot = sFocusBorder[lobby->cycleIndex++];
}

static const struct {
    int gfx;
    enum TrainerClass cls;
} sGfxClass[] = {
    { 0, TRAINER_CLASS_PLAYER_MALE },
    { 97, TRAINER_CLASS_PLAYER_FEMALE },
    { 1, TRAINER_CLASS_NINJA_BOY },
    { 2, TRAINER_CLASS_TWINS },
    { 3, TRAINER_CLASS_SCHOOL_KID_MALE },
    { 4, TRAINER_CLASS_YOUNGSTER },
    { 5, TRAINER_CLASS_BUG_CATCHER },
    { 6, TRAINER_CLASS_LASS },
    { 7, TRAINER_CLASS_BATTLE_GIRL },
    { 8, TRAINER_CLASS_SCHOOL_KID_FEMALE },
    { 9, TRAINER_CLASS_BREEDER_MALE },
    { 10, TRAINER_CLASS_GUITARIST },
    { 11, TRAINER_CLASS_ACE_TRAINER_MALE },
    { 12, TRAINER_CLASS_BREEDER_FEMALE },
    { 13, TRAINER_CLASS_BEAUTY },
    { 14, TRAINER_CLASS_ACE_TRAINER_FEMALE },
    { 15, TRAINER_CLASS_POKEFAN_MALE },
    { 16, TRAINER_CLASS_POKEFAN_FEMALE },
    { 17, TRAINER_CLASS_VETERAN },
    { 18, TRAINER_CLASS_VETERAN },
    { 19, TRAINER_CLASS_COLLECTOR },
    { 20, TRAINER_CLASS_HIKER },
    { 22, TRAINER_CLASS_REPORTER },
    { 23, TRAINER_CLASS_CAMERAMAN },
    { 29, TRAINER_CLASS_SCIENTIST },
    { 30, TRAINER_CLASS_SCIENTIST },
    { 31, TRAINER_CLASS_ROUGHNECK },
    { 32, TRAINER_CLASS_SKIER_MALE },
    { 33, TRAINER_CLASS_SKIER_FEMALE },
    { 34, TRAINER_CLASS_POLICEMAN },
    { 35, TRAINER_CLASS_IDOL },
    { 36, TRAINER_CLASS_GENTLEMAN },
    { 37, TRAINER_CLASS_SOCIALITE },
    { 38, TRAINER_CLASS_CYCLIST_MALE },
    { 39, TRAINER_CLASS_CYCLIST_FEMALE },
    { 40, TRAINER_CLASS_WORKER },
    { 41, TRAINER_CLASS_RANCHER },
    { 42, TRAINER_CLASS_COWGIRL },
    { 43, TRAINER_CLASS_CLOWN },
    { 44, TRAINER_CLASS_ARTIST },
    { 45, TRAINER_CLASS_JOGGER },
    { 46, TRAINER_CLASS_SWIMMER_MALE },
    { 47, TRAINER_CLASS_SWIMMER_FEMALE },
    { 48, TRAINER_CLASS_TUBER_FEMALE },
    { 49, TRAINER_CLASS_TUBER_MALE },
    { 50, TRAINER_CLASS_RUIN_MANIAC },
    { 51, TRAINER_CLASS_BLACK_BELT },
    { 52, TRAINER_CLASS_CAMPER },
    { 53, TRAINER_CLASS_PICNICKER },
    { 54, TRAINER_CLASS_FISHERMAN },
    { 55, TRAINER_CLASS_PARASOL_LADY },
    { 56, TRAINER_CLASS_SAILOR },
    { 59, TRAINER_CLASS_WAITER },
    { 60, TRAINER_CLASS_WAITRESS },
    { 62, TRAINER_CLASS_RICH_BOY },
    { 63, TRAINER_CLASS_LADY },
    { 68, TRAINER_CLASS_ACE_TRAINER_SNOW_MALE },
    { 69, TRAINER_CLASS_ACE_TRAINER_SNOW_FEMALE },
    { 70, TRAINER_CLASS_PSYCHIC_MALE },
    { 124, TRAINER_CLASS_GALACTIC_GRUNT_MALE },
    { 125, TRAINER_CLASS_GALACTIC_GRUNT_FEMALE },
    { 175, TRAINER_CLASS_MAID },
    { 120, TRAINER_CLASS_GALACTIC_BOSS },
    { 121, TRAINER_CLASS_COMMANDER_MARS },
    { 122, TRAINER_CLASS_COMMANDER_SATURN },
    { 123, TRAINER_CLASS_COMMANDER_JUPITER },
    { 134, TRAINER_CLASS_ELITE_FOUR_AARON },
    { 135, TRAINER_CLASS_ELITE_FOUR_BERTHA },
    { 136, TRAINER_CLASS_ELITE_FOUR_FLINT },
    { 137, TRAINER_CLASS_ELITE_FOUR_LUCIAN },
    { 126, TRAINER_CLASS_LEADER_ROARK },
    { 127, TRAINER_CLASS_LEADER_GARDENIA },
    { 128, TRAINER_CLASS_LEADER_WAKE },
    { 129, TRAINER_CLASS_LEADER_MAYLENE },
    { 130, TRAINER_CLASS_LEADER_FANTINA },
    { 131, TRAINER_CLASS_LEADER_CANDICE },
    { 132, TRAINER_CLASS_LEADER_BYRON },
    { 133, TRAINER_CLASS_LEADER_VOLKNER },
    { 138, TRAINER_CLASS_CHAMPION_CYNTHIA },
    { 141, TRAINER_CLASS_TRAINER_CHERYL },
    { 142, TRAINER_CLASS_TRAINER_RILEY },
    { 143, TRAINER_CLASS_TRAINER_MARLEY },
    { 144, TRAINER_CLASS_TRAINER_BUCK },
    { 145, TRAINER_CLASS_TRAINER_MIRA },
    { 148, TRAINER_CLASS_RIVAL },
    { 169, TRAINER_CLASS_TOWER_TYCOON },
    { 215, TRAINER_CLASS_FACTORY_HEAD },
    { 216, TRAINER_CLASS_HALL_MATRON },
    { 217, TRAINER_CLASS_CASTLE_VALET },
    { 218, TRAINER_CLASS_ARCADE_STAR },
};

static enum TrainerClass Lobby_ClassForGfx(int gfx, int gender)
{
    unsigned i;

    for (i = 0; i < NELEMS(sGfxClass); i++) {
        if (sGfxClass[i].gfx == gfx)
            return sGfxClass[i].cls;
    }
    return gender ? TRAINER_CLASS_PLAYER_FEMALE : TRAINER_CLASS_PLAYER_MALE;
}

static void Lobby_HideSprite(Lobby *lobby)
{
    if (lobby->trainerSprite != NULL) {
        Sprite_DeleteAndFreeResources(lobby->trainerSprite);
        lobby->trainerSprite = NULL;
    }
    /* NewManagedSpriteTrainer always uses these IDs for battlerType 0.
     * Delete does not free them, so the next load would keep the old tiles. */
    if (lobby->spriteMan != NULL) {
        SpriteManager_UnloadCharObjById(lobby->spriteMan, 20015);
        SpriteManager_UnloadPlttObjById(lobby->spriteMan, 20010);
        SpriteManager_UnloadCellObjById(lobby->spriteMan, 20007);
        SpriteManager_UnloadAnimObjById(lobby->spriteMan, 20007);
    }
    lobby->previewGfx = -1;
}

static void Lobby_ShowSprite(Lobby *lobby, int gfx, int gender)
{
    enum TrainerClass cls;

    if (gfx < 0 || lobby->spriteSys == NULL || lobby->spriteMan == NULL) {
        Lobby_HideSprite(lobby);
        return;
    }
    if (gfx == lobby->previewGfx && lobby->trainerSprite != NULL)
        return;
    Lobby_HideSprite(lobby);
    cls = Lobby_ClassForGfx(gfx, gender);
    lobby->trainerSprite = SpriteSystem_NewManagedSpriteTrainer(
        lobby->spriteSys, lobby->spriteMan, lobby->pltt,
        TRAINER_SPRITE_X, TRAINER_SPRITE_Y, cls, FACE_FRONT, 0, lobby->heapID);
    if (lobby->trainerSprite != NULL)
        lobby->previewGfx = gfx;
}

static void Lobby_PrintPair(Window *window, enum HeapID heap,
                            const char *label, const char *value, u32 y, TextColor color)
{
    String *vs;
    u32 x;

    Lobby_PrintColor(window, heap, label, 8, y, color);
    vs = Lobby_Latin1(heap, value != NULL ? value : "");
    x = (u32)Window_GetWidth(window) * 8
        - Font_CalcStringWidth(FONT_SYSTEM, vs, 0) - 8;
    Text_AddPrinterWithParamsAndColor(window, FONT_SYSTEM, vs, x, y,
                                      TEXT_SPEED_NO_TRANSFER, color, NULL);
    String_Free(vs);
}

static int Lobby_GfxAtAppearCursor(const mmo_creator *c)
{
    int seen = 0, i, total = mmo_appearance_count();

    for (i = 0; i < total; i++) {
        const mmo_appearance *a = mmo_appearance_at(i);

        if (a == NULL || !a->offered)
            continue;
        if (seen == c->cursor)
            return a->gfx;
        seen++;
    }
    return -1;
}

static void Lobby_SyncPreview(Lobby *lobby)
{
    mmo_creator *c = openmmo_lobby_creator();
    int gfx = -1;

    if (c->step == MMO_CREATOR_APPEAR)
        gfx = Lobby_GfxAtAppearCursor(c);
    lobby->pendingGfx = gfx;
    lobby->pendingGender = c->gender;
}

/* The portrait swap is Transform's pixelation: mosaic up a step a frame,
 * the new front loaded at the peak, mosaic back down. A first face (or a
 * bare hide) has nothing on screen to dissolve and just appears. */
static void Lobby_TickPreview(Lobby *lobby)
{
    if (lobby->pendingGfx != lobby->previewGfx) {
        if (lobby->trainerSprite == NULL) {
            if (lobby->pendingGfx >= 0)
                Lobby_ShowSprite(lobby, lobby->pendingGfx, lobby->pendingGender);
            lobby->mosaic = 0;
        } else if (lobby->mosaic < LOBBY_MOSAIC_MAX) {
            lobby->mosaic++;
        } else if (lobby->pendingGfx >= 0) {
            Lobby_ShowSprite(lobby, lobby->pendingGfx, lobby->pendingGender);
        } else {
            Lobby_HideSprite(lobby);
            lobby->mosaic = 0;
        }
    } else if (lobby->mosaic > 0) {
        lobby->mosaic--;
    }
    if (lobby->trainerSprite != NULL)
        ManagedSprite_SetMosaicFlag(lobby->trainerSprite, lobby->mosaic > 0);
    G2_SetOBJMosaicSize(lobby->mosaic, lobby->mosaic);
}

static void Lobby_InitBgs(Lobby *lobby)
{
    GraphicsModes modes = {
        GX_DISPMODE_GRAPHICS,
        GX_BGMODE_0,
        GX_BGMODE_0,
        GX_BG0_AS_2D
    };
    BgTemplate layer = {
        .x = 0,
        .y = 0,
        .bufferSize = 0x800,
        .baseTile = 0,
        .screenSize = BG_SCREEN_SIZE_256x256,
        .colorMode = GX_BG_COLORMODE_16,
        .screenBase = GX_BG_SCRBASE_0xf800,
        .charBase = GX_BG_CHARBASE_0x00000,
        .bgExtPltt = GX_BG_EXTPLTT_01,
        .priority = 0,
        .areaOver = 0,
        .mosaic = FALSE,
    };

    lobby->bgConfig = BgConfig_New(lobby->heapID);
    SetAllGraphicsModes(&modes);
    Bg_InitFromTemplate(lobby->bgConfig, BG_LAYER_MAIN_0, &layer, BG_TYPE_STATIC);
    layer.screenBase = GX_BG_SCRBASE_0xf000;
    Bg_InitFromTemplate(lobby->bgConfig, BG_LAYER_SUB_0, &layer, BG_TYPE_STATIC);
    Bg_ClearTilemap(lobby->bgConfig, BG_LAYER_MAIN_0);
    Bg_ClearTilemap(lobby->bgConfig, BG_LAYER_SUB_0);
    Bg_ClearTilesRange(BG_LAYER_MAIN_0, 32, 0, lobby->heapID);
    Bg_ClearTilesRange(BG_LAYER_SUB_0, 32, 0, lobby->heapID);

    Font_LoadTextPalette(PAL_LOAD_MAIN_BG, PLTT_OFFSET(0), lobby->heapID);
    Font_LoadTextPalette(PAL_LOAD_MAIN_BG, PLTT_OFFSET(1), lobby->heapID);
    Font_LoadTextPalette(PAL_LOAD_SUB_BG, PLTT_OFFSET(1), lobby->heapID);
    *HW_BG_A_PLTT_COLOR(0, 0) = BACKGROUND_COLOR;
    *HW_BG_B_PLTT_COLOR(0, 0) = BACKGROUND_COLOR;
    *HW_BG_A_PLTT_COLOR(0, 15) = UNFOCUSED_OPTION_BG_COLOR;
    *HW_BG_A_PLTT_COLOR(1, 15) = UNFOCUSED_OPTION_BG_COLOR;

    LoadStandardWindowGraphics(lobby->bgConfig, BG_LAYER_MAIN_0,
                               UNFOCUSED_FRAME_TILE, 2, STANDARD_WINDOW_SYSTEM,
                               lobby->heapID);
    LoadStandardWindowGraphics(lobby->bgConfig, BG_LAYER_MAIN_0,
                               FOCUSED_FRAME_TILE, 3, STANDARD_WINDOW_FIELD,
                               lobby->heapID);
    *HW_BG_A_PLTT_COLOR(2, 1) = UNFOCUSED_OPTION_BG_COLOR;

    LoadMessageBoxGraphics(lobby->bgConfig, BG_LAYER_SUB_0, MSGBOX_TILE, 14,
                           0, lobby->heapID);
    LoadStandardWindowGraphics(lobby->bgConfig, BG_LAYER_SUB_0,
                               UNFOCUSED_FRAME_TILE, 2, STANDARD_WINDOW_SYSTEM,
                               lobby->heapID);

    /* Everything above owns 4bpp palettes 0..3 and 14 and the first eight
     * kilobytes of each engine's character memory; the sky takes what is left
     * of both, on two layers behind these windows. */
    openmmo_sky_setup(lobby->bgConfig, lobby->heapID);
}

static void Lobby_InitSprites(Lobby *lobby)
{
    SpriteResourceCapacities capacities = {
        .asStruct = {
            .charCapacity = 2,
            .plttCapacity = 2,
            .cellCapacity = 2,
            .animCapacity = 2,
            .mcellCapacity = 0,
            .manimCapacity = 0,
        },
    };
    RenderOamTemplate oam = {
        .mainOamStart = 0,
        .mainOamCount = 128,
        .mainAffineOamStart = 0,
        .mainAffineOamCount = 32,
        .subOamStart = 0,
        .subOamCount = 128,
        .subAffineOamStart = 0,
        .subAffineOamCount = 32,
    };
    CharTransferTemplateWithModes transfer = {
        .maxTasks = 4,
        .sizeMain = 1024 * 64,
        .sizeSub = 1024,
        .modeMain = GX_OBJVRAMMODE_CHAR_1D_64K,
        .modeSub = GX_OBJVRAMMODE_CHAR_1D_32K,
    };

    VramTransfer_New(32, lobby->heapID);
    lobby->spriteSys = SpriteSystem_Alloc(lobby->heapID);
    lobby->spriteMan = SpriteManager_New(lobby->spriteSys);
    SpriteSystem_Init(lobby->spriteSys, &oam, &transfer, 16);
    SpriteSystem_InitSprites(lobby->spriteSys, lobby->spriteMan, 4);
    SpriteSystem_InitManagerWithCapacities(lobby->spriteSys, lobby->spriteMan, &capacities);
    SetMainScreenViewRect(SpriteSystem_GetRenderer(lobby->spriteSys), 0, 0);
    lobby->pltt = PaletteData_New(lobby->heapID);
    PaletteData_AllocBuffer(lobby->pltt, PLTTBUF_MAIN_OBJ, PALETTE_SIZE_BYTES * 4, lobby->heapID);
    G2_SetBG0Priority(2);
    GXLayers_EngineAToggleLayers(GX_PLANEMASK_OBJ, 1);
    lobby->vramLive = 1;
}

static void Lobby_FreeSprites(Lobby *lobby)
{
    Lobby_HideSprite(lobby);
    if (lobby->pltt != NULL) {
        PaletteData_FreeBuffer(lobby->pltt, PLTTBUF_MAIN_OBJ);
        PaletteData_Free(lobby->pltt);
        lobby->pltt = NULL;
    }
    if (lobby->spriteSys != NULL && lobby->spriteMan != NULL) {
        SpriteSystem_FreeResourcesAndManager(lobby->spriteSys, lobby->spriteMan);
        lobby->spriteMan = NULL;
    }
    if (lobby->spriteSys != NULL) {
        SpriteSystem_Free(lobby->spriteSys);
        lobby->spriteSys = NULL;
    }
    if (lobby->vramLive) {
        VramTransfer_Free();
        lobby->vramLive = 0;
    }
}

static void Lobby_FreeMenu(Lobby *lobby)
{
    if (lobby->menu != NULL) {
        ListMenu_Free(lobby->menu, NULL, NULL);
        lobby->menu = NULL;
    }
    if (lobby->choices != NULL) {
        StringList_Free(lobby->choices);
        lobby->choices = NULL;
    }
}

static void Lobby_FreeCards(Lobby *lobby)
{
    int i;

    for (i = 0; i < lobby->cardCount; i++) {
        if (Window_IsInUse(&lobby->cards[i])) {
            Window_EraseStandardFrame(&lobby->cards[i], TRUE);
            Window_ClearAndCopyToVRAM(&lobby->cards[i]);
            Window_Remove(&lobby->cards[i]);
        }
    }
    lobby->cardCount = 0;
    if (lobby->bgConfig != NULL) {
        Bg_FillTilemap(lobby->bgConfig, BG_LAYER_MAIN_0, 0);
        Bg_CopyTilemapBufferToVRAM(lobby->bgConfig, BG_LAYER_MAIN_0);
    }
}

static void Lobby_FreeWindows(Lobby *lobby)
{
    Lobby_FreeMenu(lobby);
    Lobby_FreeCards(lobby);
    if (Window_IsInUse(&lobby->hint))
        Window_Remove(&lobby->hint);
    if (Window_IsInUse(&lobby->list))
        Window_Remove(&lobby->list);
    if (Window_IsInUse(&lobby->title))
        Window_Remove(&lobby->title);
}

static void Lobby_FreeBgs(Lobby *lobby)
{
    if (lobby->bgConfig == NULL)
        return;
    openmmo_sky_teardown(lobby->bgConfig);
    Bg_FreeTilemapBuffer(lobby->bgConfig, BG_LAYER_SUB_0);
    Bg_FreeTilemapBuffer(lobby->bgConfig, BG_LAYER_MAIN_0);
    Heap_Free(lobby->bgConfig);
    lobby->bgConfig = NULL;
}

static void Lobby_PaintHint(Lobby *lobby, const char *text)
{
    Window_FillTilemap(&lobby->hint, 15);
    Lobby_Print(&lobby->hint, lobby->heapID, text, 8, 8);
    Window_DrawMessageBoxWithScrollCursor(&lobby->hint, 0, MSGBOX_TILE, 14);
    Window_CopyToVRAM(&lobby->hint);
}

static void Lobby_PaintTitle(Lobby *lobby, const char *text, int show)
{
    if (!show) {
        if (Window_IsInUse(&lobby->title)) {
            Window_EraseStandardFrame(&lobby->title, TRUE);
            Window_ClearAndCopyToVRAM(&lobby->title);
            Window_Remove(&lobby->title);
        }
        return;
    }
    if (!Window_IsInUse(&lobby->title)) {
        u8 w = (openmmo_lobby_creator()->step == MMO_CREATOR_APPEAR)
            ? LIST_CREATE_W : OPTION_WINDOW_WIDTH;

        Window_Add(lobby->bgConfig, &lobby->title, BG_LAYER_MAIN_0,
                   1, 1, w, 2, 1, TITLE_TILE);
    }
    Window_FillTilemap(&lobby->title, 15);
    Lobby_Print(&lobby->title, lobby->heapID, text, 8, 0);
    Window_DrawStandardFrame(&lobby->title, 0, UNFOCUSED_FRAME_TILE, 2);
    Window_CopyToVRAM(&lobby->title);
}

static TextColor Lobby_GenderColor(int gender)
{
    return gender == 1 ? TEXT_COLOR(3, 4, 15) : TEXT_COLOR(7, 8, 15);
}

/* Person cards that fit if NEW CHARACTER is reserved at the end. A third
 * person card is 8 rows and would eat the leftover 2-row slot. */
static int Lobby_CharFit(void)
{
    int y = 1, n = 0;
    int reserved = NEW_CARD_HEIGHT + 2;

    while (n < LOBBY_MAX_CARDS - 1) {
        if (y + CHAR_CARD_HEIGHT + 2 + reserved > 24)
            break;
        y += CHAR_CARD_HEIGHT + 2;
        n++;
    }
    return n;
}

static void Lobby_EnsureSelectView(Lobby *lobby)
{
    mmo_creator *c = openmmo_lobby_creator();
    int nchars = c->list.held;
    int fit = Lobby_CharFit();

    if (fit < 1)
        fit = 1;
    if (lobby->viewScroll < 0)
        lobby->viewScroll = 0;
    if (c->cursor >= nchars) {
        lobby->viewScroll = nchars > fit ? nchars - fit : 0;
        return;
    }
    if (lobby->viewScroll > c->cursor)
        lobby->viewScroll = c->cursor;
    if (c->cursor >= lobby->viewScroll + fit)
        lobby->viewScroll = c->cursor - fit + 1;
    if (lobby->viewScroll < 0)
        lobby->viewScroll = 0;
    if (nchars > 0 && lobby->viewScroll > nchars - 1)
        lobby->viewScroll = nchars - 1;
}

static void Lobby_PaintSelect(Lobby *lobby)
{
    mmo_creator *c = openmmo_lobby_creator();
    int i, n, y, tile, h, idx, nchars, fit;
    const mmo_character *ch;
    TextColor color;

    Lobby_EnsureSelectView(lobby);
    Lobby_FreeCards(lobby);
    Lobby_FreeMenu(lobby);
    if (Window_IsInUse(&lobby->list)) {
        Window_EraseStandardFrame(&lobby->list, TRUE);
        Window_ClearAndCopyToVRAM(&lobby->list);
        Window_Remove(&lobby->list);
    }
    if (Window_IsInUse(&lobby->title)) {
        Window_EraseStandardFrame(&lobby->title, TRUE);
        Window_ClearAndCopyToVRAM(&lobby->title);
        Window_Remove(&lobby->title);
    }

    nchars = c->list.held;
    fit = Lobby_CharFit();
    if (fit > nchars)
        fit = nchars;
    n = 0;
    y = 1;
    tile = CARD_TILE_START;
    for (i = 0; i < fit; i++) {
        idx = lobby->viewScroll + i;
        if (idx >= nchars)
            break;
        h = CHAR_CARD_HEIGHT;
        if (y + h + 2 > 24)
            break;
        Window_Add(lobby->bgConfig, &lobby->cards[n], BG_LAYER_MAIN_0,
                   3, (u8)y, OPTION_WINDOW_WIDTH, (u8)h, 1, (u16)tile);
        Window_FillTilemap(&lobby->cards[n], 15);
        ch = &c->list.entry[idx];
        color = Lobby_GenderColor(ch->gender);
        Lobby_PrintColor(&lobby->cards[n], lobby->heapID,
                         ch->name[0] ? ch->name : "?", 8, 0, color);
        Lobby_PrintPair(&lobby->cards[n], lobby->heapID, "PLAYER",
                        ch->gender == 1 ? "GIRL" :
                        (ch->gender == 0 ? "BOY" : "?"),
                        TEXT_LINES(1), color);
        Lobby_PrintPair(&lobby->cards[n], lobby->heapID, "REGION",
                        mmo_region_name(ch->region),
                        TEXT_LINES(2), color);
        Window_CopyToVRAM(&lobby->cards[n]);
        lobby->cardCount++;
        tile += OPTION_WINDOW_WIDTH * h;
        y += h + 2;
        n++;
    }
    h = NEW_CARD_HEIGHT;
    if (n < LOBBY_MAX_CARDS && y + h + 2 <= 24) {
        Window_Add(lobby->bgConfig, &lobby->cards[n], BG_LAYER_MAIN_0,
                   3, (u8)y, OPTION_WINDOW_WIDTH, (u8)h, 1, (u16)tile);
        Window_FillTilemap(&lobby->cards[n], 15);
        Lobby_Print(&lobby->cards[n], lobby->heapID, "NEW CHARACTER", 8, 0);
        Window_CopyToVRAM(&lobby->cards[n]);
        lobby->cardCount++;
    }
    lobby->drawnStep = c->step;
    lobby->drawnHeld = c->list.held;
    lobby->drawnCursor = -1;
}

static void Lobby_PaintSelectFrames(Lobby *lobby)
{
    mmo_creator *c = openmmo_lobby_creator();
    int i, vis;

    if (c->cursor >= c->list.held)
        vis = lobby->cardCount > 0 ? lobby->cardCount - 1 : 0;
    else
        vis = c->cursor - lobby->viewScroll;
    for (i = 0; i < lobby->cardCount; i++) {
        if (!Window_IsInUse(&lobby->cards[i]))
            continue;
        if (i == vis) {
            Window_DrawStandardFrame(&lobby->cards[i], TRUE,
                                     FOCUSED_FRAME_TILE, 3);
            Bg_ChangeTilemapRectPalette(lobby->bgConfig, BG_LAYER_MAIN_0,
                                        Window_GetXPos(&lobby->cards[i]),
                                        Window_GetYPos(&lobby->cards[i]),
                                        Window_GetWidth(&lobby->cards[i]),
                                        Window_GetHeight(&lobby->cards[i]), 0);
        } else {
            Window_DrawStandardFrame(&lobby->cards[i], TRUE,
                                     UNFOCUSED_FRAME_TILE, 2);
            Bg_ChangeTilemapRectPalette(lobby->bgConfig, BG_LAYER_MAIN_0,
                                        Window_GetXPos(&lobby->cards[i]),
                                        Window_GetYPos(&lobby->cards[i]),
                                        Window_GetWidth(&lobby->cards[i]),
                                        Window_GetHeight(&lobby->cards[i]), 1);
        }
    }
    Bg_CopyTilemapBufferToVRAM(lobby->bgConfig, BG_LAYER_MAIN_0);
    lobby->drawnCursor = c->cursor;
}

static void Lobby_BuildMenu(Lobby *lobby)
{
    mmo_creator *c = openmmo_lobby_creator();
    mmo_creator_row row;
    ListMenuTemplate tmpl;
    int i, n, lines;

    Lobby_FreeMenu(lobby);
    Lobby_FreeCards(lobby);

    n = mmo_creator_row_count(c);
    if (n <= 0)
        return;
    lobby->choices = StringList_New((u32)n, lobby->heapID);
    for (i = 0; i < n; i++) {
        String *s;

        if (!mmo_creator_row_at(c, i, &row))
            continue;
        s = Lobby_Latin1(lobby->heapID, row.text);
        StringList_AddFromString(lobby->choices, s, (u32)i);
        String_Free(s);
    }

    lines = n < MMO_CREATOR_VISIBLE ? n : MMO_CREATOR_VISIBLE;
    {
        u8 w = (c->step == MMO_CREATOR_APPEAR) ? LIST_CREATE_W : OPTION_WINDOW_WIDTH;
        u8 h = (u8)TEXT_LINES_TILES(lines);

        if (h < 4)
            h = 4;
        if (Window_IsInUse(&lobby->list)) {
            Window_EraseStandardFrame(&lobby->list, TRUE);
            Window_ClearAndCopyToVRAM(&lobby->list);
            Window_Remove(&lobby->list);
        }
        Window_Add(lobby->bgConfig, &lobby->list, BG_LAYER_MAIN_0,
                   1, 4, w, h, 1, LIST_TILE);
    }
    Window_FillTilemap(&lobby->list, 15);
    Window_DrawStandardFrame(&lobby->list, 0, UNFOCUSED_FRAME_TILE, 2);

    memset(&tmpl, 0, sizeof tmpl);
    tmpl.choices = lobby->choices;
    tmpl.window = &lobby->list;
    tmpl.count = (u16)n;
    tmpl.maxDisplay = (u16)lines;
    tmpl.textXOffset = 8;
    tmpl.cursorXOffset = 0;
    tmpl.textColorFg = 1;
    tmpl.textColorBg = 15;
    tmpl.textColorShadow = 2;
    tmpl.pagerMode = PAGER_MODE_LEFT_RIGHT_PAD;
    tmpl.fontID = FONT_SYSTEM;
    tmpl.cursorType = 0;
    lobby->menu = ListMenu_New(&tmpl, (u16)c->scroll,
                               (u16)(c->cursor - c->scroll), lobby->heapID);
}

static void Lobby_PaintCreate(Lobby *lobby)
{
    mmo_creator *c = openmmo_lobby_creator();

    Lobby_PaintTitle(lobby, mmo_creator_title(c), 1);
    if (c->step == MMO_CREATOR_WAIT) {
        Lobby_FreeMenu(lobby);
        Lobby_FreeCards(lobby);
        if (!Window_IsInUse(&lobby->list))
            Window_Add(lobby->bgConfig, &lobby->list, BG_LAYER_MAIN_0,
                       1, 4, LIST_CREATE_W, 4, 1, LIST_TILE);
        Window_FillTilemap(&lobby->list, 15);
        Lobby_Print(&lobby->list, lobby->heapID, "Please wait.", 8, 8);
        Window_DrawStandardFrame(&lobby->list, 0, UNFOCUSED_FRAME_TILE, 2);
        Window_CopyToVRAM(&lobby->list);
    } else {
        Lobby_BuildMenu(lobby);
    }
    lobby->drawnStep = c->step;
    lobby->drawnHeld = c->list.held;
    lobby->drawnCursor = c->cursor;
}

static void Lobby_Paint(Lobby *lobby)
{
    mmo_creator *c = openmmo_lobby_creator();

    if (c->step == MMO_CREATOR_SELECT)
        Lobby_PaintSelect(lobby);
    else
        Lobby_PaintCreate(lobby);
    Lobby_PaintHint(lobby, mmo_creator_hint(c));
    if (c->step == MMO_CREATOR_SELECT)
        Lobby_PaintSelectFrames(lobby);
    Lobby_SyncPreview(lobby);
}

static void Lobby_InitWindows(Lobby *lobby)
{
    int i;

    Window_Init(&lobby->title);
    Window_Init(&lobby->list);
    Window_Init(&lobby->hint);
    for (i = 0; i < LOBBY_MAX_CARDS; i++)
        Window_Init(&lobby->cards[i]);
    Window_Add(lobby->bgConfig, &lobby->hint, BG_LAYER_SUB_0,
               2, 19, 27, 4, 1, HINT_TILE);
}

static void Lobby_SetupVisuals(Lobby *lobby)
{
    SetVBlankCallback(NULL, NULL);
    DisableHBlank();
    GXLayers_DisableEngineALayers();
    GXLayers_DisableEngineBLayers();
    GX_SetVisiblePlane(0);
    GXS_SetVisiblePlane(0);
    Lobby_SetBanks();
    ResetVisibleHardwareWindows(DS_SCREEN_MAIN);
    ResetVisibleHardwareWindows(DS_SCREEN_SUB);
    SetAutorepeat(4, 8);
    Text_ResetAllPrinters();
    Lobby_InitBgs(lobby);
    Lobby_InitWindows(lobby);
    Lobby_InitSprites(lobby);
    Lobby_Paint(lobby);
    GXLayers_EngineAToggleLayers(GX_PLANEMASK_BG0, 1);
    GXLayers_EngineBToggleLayers(GX_PLANEMASK_BG0, 1);
    GXLayers_TurnBothDispOn();
    SetVBlankCallback(Lobby_VBlank, lobby);
    /*
     * The title's own theme is a slow fanfare that was never meant to loop under a menu. The
     * plaza's is the game's music for a room full of other players, which is what this screen
     * is the door to.
     */
    Sound_ConfigureBGMChannelsAndReverb(SOUND_CHANNEL_CONFIG_DEFAULT);
    Sound_SetSceneAndPlayBGM(SOUND_SCENE_17, SEQ_PL_WIFIUNION_sseq, 1);
    Sound_PlayEffect(SEQ_SE_DP_WIN_OPEN_sseq);
    printf("openmmo: lobby bgm %d\n", (int)Sound_GetCurrentBGM());
}

static void Lobby_TeardownVisuals(Lobby *lobby)
{
    SetVBlankCallback(NULL, NULL);
    G2_SetOBJMosaicSize(0, 0);
    lobby->mosaic = 0;
    Lobby_FreeSprites(lobby);
    Lobby_FreeWindows(lobby);
    Lobby_FreeBgs(lobby);
}

static void Lobby_InputSelect(Lobby *lobby)
{
    mmo_creator *c = openmmo_lobby_creator();
    int before = c->cursor;

    if (JOY_NEW(PAD_KEY_UP))
        mmo_creator_move(c, MMO_CREATOR_UP);
    if (JOY_NEW(PAD_KEY_DOWN))
        mmo_creator_move(c, MMO_CREATOR_DOWN);
    if (c->cursor != before)
        Sound_PlayEffect(SE_CONFIRM_sseq_3);
    if (JOY_NEW(PAD_BUTTON_B)) {
        Sound_PlayEffect(SE_CONFIRM_sseq_3);
        mmo_creator_back(c);
    }
    if (JOY_NEW(PAD_BUTTON_A) || JOY_NEW(PAD_BUTTON_START)) {
        Sound_PlayEffect(SE_CONFIRM_sseq_3);
        mmo_creator_confirm(c);
        if (mmo_creator_has_pick(c) || mmo_creator_ready(c)
            || mmo_creator_has_delete(c))
            openmmo_lobby_commit();
    }
    (void)lobby;
}

static void Lobby_SyncMenuCursor(Lobby *lobby, mmo_creator *c)
{
    u16 listPos = 0, cursorPos = 0;

    ListMenu_GetListAndCursorPos(lobby->menu, &listPos, &cursorPos);
    c->scroll = (int)listPos;
    c->cursor = (int)(listPos + cursorPos);
}

static void Lobby_InputCreate(Lobby *lobby)
{
    mmo_creator *c = openmmo_lobby_creator();
    u32 choice;

    if (lobby->menu == NULL)
        return;
    if (JOY_NEW(PAD_BUTTON_START)) {
        Lobby_SyncMenuCursor(lobby, c);
        mmo_creator_confirm(c);
        if (mmo_creator_has_pick(c) || mmo_creator_ready(c)
            || mmo_creator_has_delete(c))
            openmmo_lobby_commit();
        return;
    }
    choice = ListMenu_ProcessInput(lobby->menu);
    Lobby_SyncMenuCursor(lobby, c);
    if (ListMenu_GetLastAction(lobby->menu) != LIST_MENU_ACTION_NONE)
        Sound_PlayEffect(SE_CONFIRM_sseq_3);
    if (choice == (u32)MENU_NOTHING_CHOSEN)
        return;
    if (choice == (u32)MENU_CANCEL) {
        Sound_PlayEffect(SE_CONFIRM_sseq_3);
        mmo_creator_back(c);
        return;
    }
    Sound_PlayEffect(SE_CONFIRM_sseq_3);
    c->cursor = (int)choice;
    mmo_creator_confirm(c);
    if (mmo_creator_has_pick(c) || mmo_creator_ready(c)
        || mmo_creator_has_delete(c))
        openmmo_lobby_commit();
}

static void Lobby_Input(Lobby *lobby)
{
    mmo_creator *c = openmmo_lobby_creator();

    if (c->step == MMO_CREATOR_WAIT || mmo_creator_needs_entry(c))
        return;
    if (c->step == MMO_CREATOR_SELECT)
        Lobby_InputSelect(lobby);
    else
        Lobby_InputCreate(lobby);
}

static void Lobby_LaunchName(Lobby *lobby)
{
    mmo_creator *c = openmmo_lobby_creator();
    Options *options = lobby->saveData != NULL ? SaveData_GetOptions(lobby->saveData) : NULL;

    lobby->namingArgs = NamingScreenArgs_Init(lobby->heapID,
                                              NAMING_SCREEN_TYPE_PLAYER,
                                              c->gender ? GENDER_FEMALE : GENDER_MALE,
                                              LOBBY_NAME_MAX,
                                              options);
    lobby->namingApp = ApplicationManager_New(&gNamingScreenAppTemplate,
                                              lobby->namingArgs,
                                              lobby->heapID);
}

static void Lobby_FinishName(Lobby *lobby)
{
    mmo_creator *c = openmmo_lobby_creator();
    char name[MMO_CHAR_NAME_MAX + 1];

    if (lobby->namingArgs != NULL
        && lobby->namingArgs->returnCode == NAMING_SCREEN_CODE_OK) {
        Lobby_Latin1FromString(lobby->namingArgs->textInputStr, name, sizeof name);
        if (!mmo_creator_set_name(c, name))
            mmo_creator_back(c);
    } else {
        mmo_creator_back(c);
    }

    if (lobby->namingApp != NULL) {
        ApplicationManager_Free(lobby->namingApp);
        lobby->namingApp = NULL;
    }
    if (lobby->namingArgs != NULL) {
        NamingScreenArgs_Free(lobby->namingArgs);
        lobby->namingArgs = NULL;
    }
    Font_UseLazyGlyphAccess(FONT_SYSTEM);
}

BOOL openmmo_lobby_init(ApplicationManager *appMan, int *state)
{
    Lobby *lobby;

    (void)state;
    Heap_Create(HEAP_ID_APPLICATION, LOBBY_HEAP, LOBBY_HEAP_SIZE);
    lobby = ApplicationManager_NewData(appMan, sizeof(Lobby), LOBBY_HEAP);
    memset(lobby, 0, sizeof(Lobby));
    lobby->heapID = LOBBY_HEAP;
    lobby->saveData = ((ApplicationArgs *)ApplicationManager_Args(appMan))->saveData;
    lobby->state = LOBBY_SETUP;
    lobby->drawnStep = -1;
    lobby->previewGfx = -1;
    lobby->pendingGfx = -1;
    openmmo_lobby_set_active(1);
    if (openmmo_lobby_creator()->step == MMO_CREATOR_HIDDEN)
        mmo_creator_set_list(openmmo_lobby_creator(), NULL);
    printf("openmmo: character lobby\n");
    return TRUE;
}

BOOL openmmo_lobby_main(ApplicationManager *appMan, int *state)
{
    Lobby *lobby = ApplicationManager_Data(appMan);
    mmo_creator *c = openmmo_lobby_creator();

    (void)state;
    switch (lobby->state) {
    case LOBBY_SETUP:
        Lobby_SetupVisuals(lobby);
        StartScreenFade(FADE_BOTH_SCREENS, FADE_TYPE_BRIGHTNESS_IN,
                        FADE_TYPE_BRIGHTNESS_IN, COLOR_BLACK, 6, 1, lobby->heapID);
        lobby->state = LOBBY_FADE_IN;
        return FALSE;

    case LOBBY_FADE_IN:
        if (IsScreenFadeDone())
            lobby->state = LOBBY_HOLD;
        Lobby_CycleFocus(lobby);
        openmmo_sky_tick(lobby->bgConfig);
        return FALSE;

    case LOBBY_HOLD:
        Lobby_TickPreview(lobby);
        if (lobby->trainerSprite != NULL)
            ManagedSprite_TickFrame(lobby->trainerSprite);
        if (lobby->spriteMan != NULL)
            SpriteSystem_DrawSprites(lobby->spriteMan);
        Lobby_CycleFocus(lobby);
        openmmo_sky_tick(lobby->bgConfig);
        if (openmmo_lobby_ready_to_field()) {
            StartScreenFade(FADE_BOTH_SCREENS, FADE_TYPE_BRIGHTNESS_OUT,
                            FADE_TYPE_BRIGHTNESS_OUT, COLOR_BLACK, 6, 1, lobby->heapID);
            lobby->state = LOBBY_FADE_OUT;
            return FALSE;
        }
        if (c->step == MMO_CREATOR_NAME) {
            Sound_PlayEffect(SE_CONFIRM_sseq_3);
            Lobby_TeardownVisuals(lobby);
            lobby->state = LOBBY_LAUNCH_NAME;
            return FALSE;
        }
        Lobby_Input(lobby);
        if (lobby->drawnStep != c->step || lobby->drawnHeld != c->list.held) {
            Lobby_Paint(lobby);
        } else if (lobby->drawnCursor != c->cursor) {
            if (c->step == MMO_CREATOR_SELECT) {
                int oldScroll = lobby->viewScroll;

                Lobby_EnsureSelectView(lobby);
                if (lobby->viewScroll != oldScroll)
                    Lobby_PaintSelect(lobby);
                Lobby_PaintSelectFrames(lobby);
            }
            Lobby_SyncPreview(lobby);
            lobby->drawnCursor = c->cursor;
        }
        return FALSE;

    case LOBBY_LAUNCH_NAME:
        Lobby_LaunchName(lobby);
        lobby->state = LOBBY_RUN_NAME;
        return FALSE;

    case LOBBY_RUN_NAME:
        if (lobby->namingApp != NULL && ApplicationManager_Exec(lobby->namingApp)) {
            Lobby_FinishName(lobby);
            lobby->state = LOBBY_SETUP;
        }
        return FALSE;

    case LOBBY_FADE_OUT:
        if (IsScreenFadeDone())
            lobby->state = LOBBY_TEARDOWN;
        return FALSE;

    case LOBBY_TEARDOWN:
        return TRUE;

    default:
        return TRUE;
    }
}

BOOL openmmo_lobby_exit(ApplicationManager *appMan, int *state)
{
    ApplicationArgs *args = ApplicationManager_Args(appMan);

    (void)state;
    Lobby_TeardownVisuals(ApplicationManager_Data(appMan));
    if (args != NULL)
        openmmo_lobby_enter_field(args->saveData);
    ApplicationManager_FreeData(appMan);
    Heap_Destroy(LOBBY_HEAP);
    return TRUE;
}
