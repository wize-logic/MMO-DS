/* The Pokegear's phone card. */

#include <stdio.h>
#include <string.h>

#include "openmmo_pokegear.h"
#include "openmmo_pokegear_tables.h"

#include "constants/charcode.h"
#include "constants/graphics.h"
#include "font.h"
#include "generated/fade_types.h"
#include "res/sound/pl_sound_data.naix" /* SEQ_*_sseq, the engine's own ids */
#include "graphics.h"
#include "gx_layers.h"
#include "heap.h"
#include "math_util.h"
#include "message.h"
#include "pc_boxes.h"
#include "pokedex.h"
#include "render_text.h"
#include "rtc.h"
#include "save_player.h"
#include "savedata/save_table.h"
#include "screen_fade.h"
#include "sound.h"
#include "sound_playback.h"
#include "string_gf.h"
#include "string_template.h"
#include "system.h"
#include "text.h"
#include "touch_screen.h"

#define PGPHONE_OBJ_PLTT   0
#define PGPHONE_SUB_PLTT   4   /* + skin */
#define PGPHONE_MAIN_PLTT  10  /* + skin */
#define PGPHONE_SUB_CHAR   16  /* + skin */
#define PGPHONE_SUB_SCRN   22  /* + skin */
#define PGPHONE_MAIN_CHAR  28  /* + skin */
#define PGPHONE_MAIN_SCRN  34  /* + skin */

#define MSG_PHONE       271
#define MSG_GREETINGS   640

/* bank 271 */
#define PHMSG_CALL              0   /* Call, Sort, Quit */
#define PHMSG_SORT_TITLE        3   /* Title, Alphabet, Location, Move, Quit */
#define PHMSG_EVAL_NATIONAL     15  /* Evaluate National Pokedex, Quit */
#define PHMSG_TOOLTIP           20  /* eight tooltips */
#define PHMSG_CLASS_FMT         29
#define PHMSG_RING              30  /* three */
#define PHMSG_CLICK             33  /* four */
#define PHMSG_OUT_OF_AREA       37
#define PHMSG_CLASS_PROFESSOR   38

enum { PH_TIP_NEXT_SEARCH, PH_TIP_WHERE_MOVE, PH_TIP_WHAT_DO, PH_TIP_SORT_TITLE,
       PH_TIP_SORT_ALPHABET, PH_TIP_SORT_LOCATION, PH_TIP_SORT_MANUAL, PH_TIP_SORT_FINISH,
       PH_TIP_MAX };

/* HeartGold's PhoneCallType, the phone book's `type` column. */
enum { PHCALL_GENERIC, PHCALL_MOM, PHCALL_PROF_ELM, PHCALL_PROF_OAK, PHCALL_KURT,
       PHCALL_BIKE_SHOP, PHCALL_KENJI, PHCALL_BILL, PHCALL_DAYCAREMAN, PHCALL_DAYCARELADY,
       PHCALL_BUENA, PHCALL_ETHAN_LYRA, PHCALL_GYMLEADER, PHCALL_BAOBA, PHCALL_IRWIN };

/* The callers (constants/phone_contacts.h). */
enum { PHC_MOTHER = 0, PHC_PROF_ELM = 1, PHC_PROF_OAK = 2, PHC_ETHAN = 3, PHC_LYRA = 4,
       PHC_KURT = 5, PHC_DAYCARE_MAN = 6, PHC_DAYCARE_LADY = 7, PHC_BUENA = 8,
       PHC_BILL = 9, PHC_BAOBA = 24 };

/* HeartGold's phone trainer classes (constants/trainer_class.h). */
#define TRAINERCLASS_PHONE_MOM               200
#define TRAINERCLASS_PHONE_POKEMON_PROFESSOR 201

/* PhoneCallScriptDef.scriptType */
enum { PHSCRIPT_NONE, PHSCRIPT_UNK1, PHSCRIPT_FLAG, PHSCRIPT_REMATCH, PHSCRIPT_ITEM, PHSCRIPT_WORD };

/* The handlers (sPhoneCallHandlers), by the state's scriptType. */
enum { PHH_SIMPLE = 0, PHH_BILL = 3, PHH_MOTHER = 4, PHH_PROF_OAK = 5, PHH_DAYCARE_LADY = 6,
       PHH_DAYCARE_MAN = 7, PHH_BUENA = 8, PHH_ETHAN_LYRA = 10, PHH_KURT = 12 };

#define PH_MAX_CONTACTS 16
#define PH_ROWS 6

enum { PHS_SETUP, PHS_INPUT, PHS_TEARDOWN, PHS_CONTEXT_MENU, PHS_SORT_MENU, PHS_DIM_BEFORE_CALL,
       PHS_SETUP_CALL, PHS_PLAY_CALL, PHS_FADE_IN, PHS_FADE_GEAR_CLOSE, PHS_WIPE_IN,
       PHS_WIPE_SWITCH_APP, PHS_QUIT };

/* tel/pmtel_book.dat: one row per caller (gear_phone.h PhoneBookEntry). */
typedef struct PhoneBookEntry {
    u8 id;
    u8 type;
    u8 unk2;
    u8 trainerClass;
    u16 trainerId;
    u16 mapId;
    u16 gift;
    u16 phoneScriptIfLocal;
    u8 greeting;
    u8 rematchWeekday;
    u8 rematchTimeOfDay;
    u8 unkF;
    u8 sortParam[4];
} PhoneBookEntry;

typedef struct PhoneColors {
    u8 fg1, bg1, sh1, fg3, sh3, fg2, bg2, sh2, fg4, sh4, fill1, fill2;
    TextColor nameDeselected, classDeselected, nameSelected, classSelected;
} PhoneColors;

typedef struct PokegearPhoneAppData PokegearPhoneAppData;

/* A call (PokegearPhoneCallContext + PokegearPhoneCallState). */
typedef struct PhoneCall {
    PokegearPhoneAppData *ph;
    MessageLoader *msg271;
    MessageLoader *msg640;
    MessageLoader *msgContact;
    StringTemplate *fmt;
    String *expand;
    String *read;
    String *contactName;
    String *contactClass;
    String *classFmt;
    String *ring[3];
    String *click[4];
    u8 printer;
    u8 playerGender;
    u8 msgIds[2];
    int mainState;
    int scriptState;
    int toneState;
    u16 toneTimer;
    const PhoneBookEntry *entry;
    const PhoneCallScriptDef *def;
    u8 callerID;
    u8 timeOfDay;
    u8 hour;
    u16 scriptID;
    u16 scriptType;
    u8 shared;
    u8 flag0, flag1, flag2, flag3;
    int playerHgMap;
    int canCall;
    u8 menuKind;
    PokegearMenu *menu;
} PhoneCall;

/* The list on the touch screen (PhoneContactListUI). */
typedef struct PhoneList {
    u8 numContacts;
    u8 cursorPos;
    u8 selectedIndex;
    u8 firstContactOnPage;
    u8 lastContactIndex;
    u8 listBottomIndex;
    u8 firstBgColor;
    u8 isScrolling;
    u8 scrollDirection;
    u8 isPageScroll;
    u8 scrollTimer;
    u8 pageScrollStep;
    u8 pageScrollFailed;
    Window *window;
    PhoneColors colors[2];
    Sprite *arrows[2];
    Sprite *moveArrows[7];
    Sprite *cursor[4];
} PhoneList;

struct PokegearPhoneAppData {
    enum HeapID heapID;
    int state;
    int substate;
    int subsub;
    PokegearAppData *pokegear;
    u8 menuInputStateBak;
    u8 skin;
    MessageLoader *msg;
    String *tooltip[PH_TIP_MAX];
    u8 textDelay;
    Window windows[4];
    Sprite *sprites[14];
    PokegearMenu *menu;
    PhoneBookEntry *book;
    int bookCount;
    u8 order[PH_MAX_CONTACTS];
    u8 numContacts;
    u8 callerID;
    PhoneList list;
    PhoneCall call;
    void *scrnRaw;
    NNSG2dScreenData *scrn;
};

/* ------------------------------------------------------------------ */
/* Tables                                                              */
/* ------------------------------------------------------------------ */

static const WindowTemplate sWindowTemplates[4] = {
    { BG_LAYER_SUB_2, 2, 19, 27, 4, 1, 0x375 },  /* the call's lines, top screen */
    { BG_LAYER_SUB_2, 4, 16, 9, 2, 1, 0x363 },   /* the caller's name */
    { BG_LAYER_MAIN_3, 1, 2, 27, 24, 2, 0x177 }, /* the contact list */
    { BG_LAYER_MAIN_1, 0, 21, 32, 2, 10, 0x3BF }, /* the tooltip bar */
};

static const PokegearSpriteTemplate sSpriteTemplates[8] = {
    { 0, 0, 0, 0, 2, 1, 6, NNS_G2D_VRAM_TYPE_2DMAIN },     /* cursor corners */
    { 0, 0, 0, 0, 3, 1, 6, NNS_G2D_VRAM_TYPE_2DMAIN },
    { 0, 0, 0, 0, 4, 1, 6, NNS_G2D_VRAM_TYPE_2DMAIN },
    { 0, 0, 0, 0, 5, 1, 6, NNS_G2D_VRAM_TYPE_2DMAIN },
    { 0, 124, 8, 0, 0, 1, 7, NNS_G2D_VRAM_TYPE_2DMAIN },   /* page arrows */
    { 0, 124, 156, 0, 1, 1, 7, NNS_G2D_VRAM_TYPE_2DMAIN },
    { 0, 0, 0, 0, 6, 1, 7, NNS_G2D_VRAM_TYPE_2DMAIN },     /* move arrows */
    { 0, 128, 160, 0, 8, 1, -1, NNS_G2D_VRAM_TYPE_2DMAIN }, /* the fast-forward button */
};

/* Call/Sort/Quit; the sorts; Evaluate National Pokedex/Quit: x, y, width. */
static const u8 sMenuCallSortQuit[3] = { PHMSG_CALL, PHMSG_CALL + 1, PHMSG_CALL + 2 };
static const u8 sMenuSorts[4] = { PHMSG_SORT_TITLE, PHMSG_SORT_TITLE + 1, PHMSG_SORT_TITLE + 2, PHMSG_SORT_TITLE + 4 };
static const u8 sMenuEvalNational[2] = { PHMSG_EVAL_NATIONAL, PHMSG_EVAL_NATIONAL + 1 };

/* The contact rows and the page arrows, in touch-screen pixels. */
static const TouchScreenRect sListRects[] = {
    { 0x08, 0x20, 0x08, 0xE0 }, { 0x20, 0x38, 0x08, 0xE0 }, { 0x38, 0x50, 0x08, 0xE0 },
    { 0x50, 0x68, 0x08, 0xE0 }, { 0x68, 0x80, 0x08, 0xE0 }, { 0x80, 0x98, 0x08, 0xE0 },
    { 0x08, 0x50, 0xE0, 0xF8 }, { 0x50, 0x98, 0xE0, 0xF8 },
    { TOUCHSCREEN_TABLE_TERMINATOR, 0, 0, 0 },
};

/* The fast-forward button under a call's lines. */
static const TouchScreenRect sFastForwardRect[] = {
    { 0x88, 0xB8, 0x08, 0xF8 },
    { TOUCHSCREEN_TABLE_TERMINATOR, 0, 0, 0 },
};

/* The story's callers, in the order the game hands their numbers out. The
 * friend is whichever of Ethan and Lyra the player is not. */
static const u8 sStoryContacts[] = {
    PHC_MOTHER, PHC_PROF_ELM, PHC_ETHAN, PHC_KURT, PHC_BILL, PHC_DAYCARE_MAN,
    PHC_DAYCARE_LADY, PHC_BUENA, PHC_PROF_OAK, PHC_BAOBA,
};

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

/* GF_RTC_GetTimeOfDayWildParamByHour: morning, day, night. */
static u8 time_of_day(u8 hour)
{
    if (hour >= 4 && hour <= 9)
        return 0;
    if (hour >= 10 && hour <= 17)
        return 1;
    return 2;
}

/* ------------------------------------------------------------------ */
/* The phone book (phonebook_dat.c)                                    */
/* ------------------------------------------------------------------ */

static BOOL book_load(PokegearPhoneAppData *ph)
{
    FSFile file;
    u32 count = 0;

    FS_InitFile(&file);
    if (!FS_OpenFile(&file, "tel/pmtel_book.dat")) {
        printf("openmmo: pokegear: no tel/pmtel_book.dat in the package\n");
        return FALSE;
    }
    FS_ReadFile(&file, &count, sizeof count);
    if (count == 0 || count > 256) {
        FS_CloseFile(&file);
        return FALSE;
    }
    ph->book = Heap_Alloc(ph->heapID, count * sizeof(PhoneBookEntry));
    FS_ReadFile(&file, ph->book, (s32)(count * sizeof(PhoneBookEntry)));
    FS_CloseFile(&file);
    ph->bookCount = (int)count;
    return TRUE;
}

static const PhoneBookEntry *book_entry(PokegearPhoneAppData *ph, u8 id)
{
    if (ph->book == NULL || id >= ph->bookCount)
        return NULL;
    return &ph->book[id];
}

/* The contact's name: row 0 of its own bank. */
static void contact_name(PhoneCall *c, u8 id, String *dst)
{
    MessageLoader *m;

    String_Clear(dst);
    if (id >= POKEGEAR_PHONE_CONTACTS)
        return;
    m = openmmo_pokegear_msg(gPokegearContactBanks[id], c->ph->heapID);
    if (m == NULL)
        return;
    MessageLoader_GetString(m, 0, dst);
    MessageLoader_Free(m);
}

/* PhoneContact_GetClass: Mom has none, the story's people have a title of
 * their own in bank 271, a trainer has the class name. */
static void contact_class(PhoneCall *c, u8 id, String *dst)
{
    const PhoneBookEntry *e = book_entry(c->ph, id);

    String_Clear(dst);
    if (e == NULL || e->trainerClass == TRAINERCLASS_PHONE_MOM)
        return;
    if (e->trainerClass >= TRAINERCLASS_PHONE_POKEMON_PROFESSOR) {
        MessageLoader_GetString(c->msg271, (u32)(PHMSG_CLASS_PROFESSOR + e->trainerClass - TRAINERCLASS_PHONE_POKEMON_PROFESSOR), dst);
        return;
    }
    StringTemplate_SetTrainerClassName(c->fmt, 0, e->trainerClass);
    StringTemplate_Format(c->fmt, dst, c->classFmt);
}

/* ------------------------------------------------------------------ */
/* Layers, graphics, windows, sprites (overlay_101_021F017C.c)         */
/* ------------------------------------------------------------------ */

static void phone_init_bgs(PokegearPhoneAppData *ph)
{
    BgConfig *bg = ph->pokegear->bgConfig;
    BgTemplate t[6] = {
        { 0, 0, 0x800, 0, BG_SCREEN_SIZE_256x256, GX_BG_COLORMODE_16, GX_BG_SCRBASE_0xf000, GX_BG_CHARBASE_0x00000, GX_BG_EXTPLTT_01, 0, GX_BG_AREAOVER_XLU, FALSE },
        { 0, 0, 0x800, 0, BG_SCREEN_SIZE_256x256, GX_BG_COLORMODE_16, GX_BG_SCRBASE_0xe800, GX_BG_CHARBASE_0x00000, GX_BG_EXTPLTT_01, 2, GX_BG_AREAOVER_XLU, FALSE },
        { 0, 0, 0x800, 0, BG_SCREEN_SIZE_256x256, GX_BG_COLORMODE_16, GX_BG_SCRBASE_0xe000, GX_BG_CHARBASE_0x10000, GX_BG_EXTPLTT_01, 3, GX_BG_AREAOVER_XLU, FALSE },
        { 0, 0, 0x800, 0, BG_SCREEN_SIZE_256x256, GX_BG_COLORMODE_16, GX_BG_SCRBASE_0xf000, GX_BG_CHARBASE_0x00000, GX_BG_EXTPLTT_01, 0, GX_BG_AREAOVER_XLU, FALSE },
        { 0, 0, 0x800, 0, BG_SCREEN_SIZE_256x256, GX_BG_COLORMODE_16, GX_BG_SCRBASE_0xe800, GX_BG_CHARBASE_0x00000, GX_BG_EXTPLTT_01, 1, GX_BG_AREAOVER_XLU, FALSE },
        { 0, 0, 0x800, 0, BG_SCREEN_SIZE_256x256, GX_BG_COLORMODE_16, GX_BG_SCRBASE_0xe000, GX_BG_CHARBASE_0x00000, GX_BG_EXTPLTT_01, 2, GX_BG_AREAOVER_XLU, FALSE },
    };
    int i;

    GX_SetGraphicsMode(GX_DISPMODE_GRAPHICS, GX_BGMODE_0, GX_BG0_AS_2D);
    for (i = 0; i < 3; i++) {
        Bg_InitFromTemplate(bg, (u8)(BG_LAYER_MAIN_1 + i), &t[i], BG_TYPE_STATIC);
        Bg_InitFromTemplate(bg, (u8)(BG_LAYER_SUB_1 + i), &t[3 + i], BG_TYPE_STATIC);
        Bg_ClearTilemap(bg, (u8)(BG_LAYER_MAIN_1 + i));
        Bg_ClearTilesRange((u8)(BG_LAYER_MAIN_1 + i), 0x20, 0, ph->heapID);
        Bg_ClearTilemap(bg, (u8)(BG_LAYER_SUB_1 + i));
        Bg_ClearTilesRange((u8)(BG_LAYER_SUB_1 + i), 0x20, 0, ph->heapID);
    }
    Bg_SetPriority(BG_LAYER_MAIN_0, 1);
    Bg_SetOffset(bg, BG_LAYER_MAIN_3, BG_OFFSET_UPDATE_SET_Y, 0x20);
    for (i = 1; i < 8; i++)
        if (i != 4)
            Bg_ToggleLayer((u8)i, 0);
}

static void phone_exit_bgs(PokegearPhoneAppData *ph)
{
    Bg_SetPriority(BG_LAYER_MAIN_0, 0);
    Bg_SetOffset(ph->pokegear->bgConfig, BG_LAYER_MAIN_3, BG_OFFSET_UPDATE_SET_Y, 0);
    Pokegear_ClearAppBgLayers(ph->pokegear);
}

static void phone_load_graphics(PokegearPhoneAppData *ph)
{
    BgConfig *bg = ph->pokegear->bgConfig;
    NARC *narc = openmmo_pokegear_narc(PG_NARC_PHONE, ph->heapID);
    PaletteData *pd = ph->pokegear->plttData;

    Font_InitManager(FONT_SUBSCREEN, ph->heapID);
    if (narc == NULL)
        return;
    Graphics_LoadTilesToBgLayerFromOpenNARC(narc, PGPHONE_MAIN_CHAR + ph->skin, bg, BG_LAYER_MAIN_2, 0, 0, FALSE, ph->heapID);
    Graphics_LoadTilesToBgLayerFromOpenNARC(narc, PGPHONE_SUB_CHAR + ph->skin, bg, BG_LAYER_SUB_3, 0, 0, FALSE, ph->heapID);
    Graphics_LoadTilemapToBgLayerFromOpenNARC(narc, PGPHONE_SUB_SCRN + ph->skin, bg, BG_LAYER_SUB_3, 0, 0, FALSE, ph->heapID);
    ph->scrnRaw = Graphics_GetScrnDataFromOpenNARC(narc, PGPHONE_MAIN_SCRN + ph->skin, FALSE, &ph->scrn, ph->heapID);
    if (ph->scrn != NULL)
        Bg_CopyToTilemapRect(bg, BG_LAYER_MAIN_2, 0, 0, 32, 20, ph->scrn->rawData, 0, 0,
                             (u8)(ph->scrn->screenWidth / 8), (u8)(ph->scrn->screenHeight / 8));
    pltt_load(pd, narc, PGPHONE_MAIN_PLTT + ph->skin, ph->heapID, PLTTBUF_MAIN_BG, 0x1C0, 0, 0);
    pltt_load(pd, narc, PGPHONE_SUB_PLTT + ph->skin, ph->heapID, PLTTBUF_SUB_BG, 0x180, 0, 0);
    pltt_load(pd, narc, PGPHONE_OBJ_PLTT, ph->heapID, PLTTBUF_MAIN_OBJ, 0x160, 0x40, 0);
    pltt_load(pd, narc, PGPHONE_OBJ_PLTT, ph->heapID, PLTTBUF_SUB_OBJ, 0x160, 0x40, 0);
    NARC_dtor(narc);
    PaletteData_SetAutoTransparent(pd, TRUE);
    PaletteData_Blend(pd, PLTTBUF_MAIN_BG, 0, 0xE0, 16, COLOR_BLACK);
    PaletteData_Blend(pd, PLTTBUF_MAIN_OBJ, 0x40, 0xC0, 16, COLOR_BLACK);
    PaletteData_CommitFadedBuffers(pd);
    PaletteData_SetAutoTransparent(pd, FALSE);
    Bg_ScheduleTilemapTransfer(bg, BG_LAYER_MAIN_2);
    Bg_ScheduleTilemapTransfer(bg, BG_LAYER_SUB_3);
}

static void phone_unload_graphics(PokegearPhoneAppData *ph)
{
    if (ph->scrnRaw != NULL)
        Heap_Free(ph->scrnRaw);
    ph->scrnRaw = NULL;
    ph->scrn = NULL;
    Font_Free(FONT_SUBSCREEN);
}

static void phone_add_windows(PokegearPhoneAppData *ph)
{
    int i;

    for (i = 0; i < 4; i++) {
        const WindowTemplate *w = &sWindowTemplates[i];

        Window_Add(ph->pokegear->bgConfig, &ph->windows[i], w->bgLayer, w->tilemapLeft, w->tilemapTop,
                   w->width, w->height, w->palette, w->baseTile);
        Window_FillTilemap(&ph->windows[i], 0);
    }
    /* HeartGold's phone draws no page arrow (its alternate-arrow flag), and
     * this engine draws one from tiles 18..29 past the base: point the base
     * past the last window and keep those tiles blank. */
    Bg_ClearTilesRange(BG_LAYER_SUB_2, 31 * 0x20, 0x3E1 * 0x20, ph->heapID);
    TextPrinter_SetScrollArrowBaseTile(0x3E1);
}

static void phone_remove_windows(PokegearPhoneAppData *ph)
{
    int i;

    for (i = 0; i < 4; i++) {
        Window_ClearAndCopyToVRAM(&ph->windows[i]);
        Window_Remove(&ph->windows[i]);
    }
}

static void phone_load_text(PokegearPhoneAppData *ph)
{
    int i;

    ph->msg = openmmo_pokegear_msg(MSG_PHONE, ph->heapID);
    for (i = 0; i < PH_TIP_MAX; i++)
        ph->tooltip[i] = ph->msg != NULL ? MessageLoader_GetNewString(ph->msg, (u32)(PHMSG_TOOLTIP + i))
                                         : String_Init(4, ph->heapID);
    ph->textDelay = Options_TextFrameDelay(SaveData_GetOptions(ph->pokegear->saveData));
    RenderControlFlags_SetCanABSpeedUpPrint(TRUE);
}

static void phone_free_text(PokegearPhoneAppData *ph)
{
    int i;

    for (i = 0; i < PH_TIP_MAX; i++)
        if (ph->tooltip[i] != NULL)
            String_Free(ph->tooltip[i]);
    if (ph->msg != NULL)
        MessageLoader_Free(ph->msg);
    ph->msg = NULL;
    RenderControlFlags_SetCanABSpeedUpPrint(FALSE);
}

static void phone_make_sprites(PokegearPhoneAppData *ph)
{
    PokegearAppData *app = ph->pokegear;
    int i;

    for (i = 0; i <= 5; i++) {
        ph->sprites[i] = PokegearApp_CreateSprite(app, &sSpriteTemplates[i]);
        if (ph->sprites[i] == NULL)
            continue;
        Sprite_SetExplicitPriority(ph->sprites[i], 1);
        Sprite_SetDrawFlag(ph->sprites[i], FALSE);
        Sprite_SetAnimateFlag(ph->sprites[i], TRUE);
    }
    for (i = 6; i <= 12; i++) {
        ph->sprites[i] = PokegearApp_CreateSprite(app, &sSpriteTemplates[6]);
        if (ph->sprites[i] == NULL)
            continue;
        Sprite_SetExplicitPriority(ph->sprites[i], 0);
        Sprite_SetPriority(ph->sprites[i], 0);
        Sprite_SetDrawFlag(ph->sprites[i], FALSE);
        Sprite_SetAnimateFlag(ph->sprites[i], FALSE);
        Sprite_SetPositionXY(ph->sprites[i], 12, (s16)(i * 24 - 128));
    }
    ph->sprites[13] = PokegearApp_CreateSprite(app, &sSpriteTemplates[7]);
    if (ph->sprites[13] != NULL) {
        Sprite_SetExplicitPriority(ph->sprites[13], 0);
        Sprite_SetDrawFlag(ph->sprites[13], FALSE);
        Sprite_SetAnimateFlag(ph->sprites[13], FALSE);
    }
}

static void phone_delete_sprites(PokegearPhoneAppData *ph)
{
    int i;

    for (i = 0; i < 14; i++) {
        if (ph->sprites[i] != NULL)
            PokegearApp_DeleteSprite(ph->pokegear, ph->sprites[i]);
        ph->sprites[i] = NULL;
    }
}

/* PokegearPhone_SetTouchscreenDimState: the list goes dark for a call, the
 * fast-forward button comes up and the caller's name goes to the top screen. */
static void phone_dim(PokegearPhoneAppData *ph, BOOL dim)
{
    PaletteData *pd = ph->pokegear->plttData;

    if (ph->sprites[13] != NULL)
        Sprite_SetDrawFlag(ph->sprites[13], dim);
    Window_FillTilemap(&ph->windows[0], 0);
    Window_FillTilemap(&ph->windows[1], 0);
    if (dim) {
        PaletteData_Blend(pd, PLTTBUF_MAIN_BG, 0, 0xB0, 8, COLOR_BLACK);
        PaletteData_Blend(pd, PLTTBUF_MAIN_BG, 0xE0, 0x20, 8, COLOR_BLACK);
        PaletteData_Blend(pd, PLTTBUF_MAIN_OBJ, 0x60, 0x20, 8, COLOR_BLACK);
        PaletteData_Blend(pd, PLTTBUF_MAIN_OBJ, 0, 0x40, 8, COLOR_BLACK);
        contact_name(&ph->call, ph->callerID, ph->call.contactName);
        Text_AddPrinterWithParamsAndColor(&ph->windows[1], FONT_SYSTEM, ph->call.contactName, 0, 0,
                                          TEXT_SPEED_INSTANT, TEXT_COLOR(1, 2, 0), NULL);
    } else {
        PaletteData_Blend(pd, PLTTBUF_MAIN_BG, 0, 0xE0, 0, COLOR_BLACK);
        PaletteData_Blend(pd, PLTTBUF_MAIN_BG, 0xE0, 0x20, 0, COLOR_BLACK);
        PaletteData_Blend(pd, PLTTBUF_MAIN_OBJ, 0x40, 0xC0, 0, COLOR_BLACK);
        PaletteData_Blend(pd, PLTTBUF_MAIN_OBJ, 0, 0x40, 0, COLOR_BLACK);
    }
    PaletteData_SetAutoTransparent(pd, TRUE);
    PaletteData_CommitFadedBuffers(pd);
    PaletteData_SetAutoTransparent(pd, FALSE);
}

/* ------------------------------------------------------------------ */
/* The tooltip bar and the popups (overlay_101_021F0880.c)             */
/* ------------------------------------------------------------------ */

static void phone_tooltip(PokegearPhoneAppData *ph, u8 which, BOOL draw)
{
    BgConfig *bg = ph->pokegear->bgConfig;

    if (draw) {
        u32 x;

        if (ph->scrn != NULL)
            Bg_CopyToTilemapRect(bg, BG_LAYER_MAIN_1, 0, 20, 32, 4, ph->scrn->rawData, 0, 24,
                                 (u8)(ph->scrn->screenWidth / 8), (u8)(ph->scrn->screenHeight / 8));
        Window_FillTilemap(&ph->windows[3], 5);
        x = (256 - Font_CalcStringWidth(FONT_SYSTEM, ph->tooltip[which], 0)) / 2;
        Text_AddPrinterWithParamsAndColor(&ph->windows[3], FONT_SYSTEM, ph->tooltip[which], x, 0,
                                          TEXT_SPEED_INSTANT, TEXT_COLOR(3, 2, 5), NULL);
    } else {
        Window_ClearAndScheduleCopyToVRAM(&ph->windows[3]);
        Bg_FillTilemapRect(bg, BG_LAYER_MAIN_1, 0, 0, 20, 32, 4, TILEMAP_FILL_VAL_KEEP_PALETTE);
    }
    Bg_ScheduleTilemapTransfer(bg, BG_LAYER_MAIN_1);
}

/* PokegearPhoneApp_TouchscreenListMenu_Create, on this game's list menu. */
static PokegearMenu *phone_menu_open(PokegearPhoneAppData *ph, const u8 *rows, int n, u8 x, u8 y, u8 width)
{
    u16 entries[5];
    int i;

    for (i = 0; i < n && i < 5; i++)
        entries[i] = rows[i];
    return PokegearMenu_Open(ph->pokegear, BG_LAYER_MAIN_1, x, y, width, 0x304, 13, ph->msg, entries, n, ph->heapID);
}

/* ------------------------------------------------------------------ */
/* The contact list (overlay_101_021F0F48.c)                           */
/* ------------------------------------------------------------------ */

static void list_set_colors(PhoneColors *c, u8 bg1, u8 bg2, u8 fill1, u8 fill2)
{
    c->fg1 = 1; c->bg1 = bg1; c->sh1 = 2; c->fg3 = 3; c->sh3 = 4; c->fill1 = fill1;
    c->fg2 = 5; c->bg2 = bg2; c->sh2 = 6; c->fg4 = 7; c->sh4 = 8; c->fill2 = fill2;
    c->nameDeselected = TEXT_COLOR(c->fg1, c->sh1, c->bg1);
    c->classDeselected = TEXT_COLOR(c->fg2, c->sh2, c->bg2);
    c->nameSelected = TEXT_COLOR(c->fg3, c->sh3, c->bg1);
    c->classSelected = TEXT_COLOR(c->fg4, c->sh4, c->bg2);
}

static void list_init(PokegearPhoneAppData *ph)
{
    PhoneList *l = &ph->list;
    int i;

    memset(l, 0, sizeof *l);
    l->numContacts = ph->numContacts;
    l->window = &ph->windows[2];
    l->selectedIndex = 0xFF;
    for (i = 0; i < 2; i++)
        l->arrows[i] = ph->sprites[4 + i];
    for (i = 0; i < 7; i++)
        l->moveArrows[i] = ph->sprites[6 + i];
    for (i = 0; i < 4; i++)
        l->cursor[i] = ph->sprites[i];
    list_set_colors(&l->colors[0], 9, 10, 11, 10);
    list_set_colors(&l->colors[1], 12, 13, 14, 13);
}

static void list_draw_slot_bg(PhoneList *l, u8 slot, u8 colorIdx, BOOL copyNow)
{
    PhoneColors *c = &l->colors[colorIdx];
    u16 y = (u16)(24 * slot);

    Window_FillRectWithColor(l->window, c->fill1, 0, y, 216, 24);
    Window_FillRectWithColor(l->window, c->bg1, 8, y, 82, 20);
    Window_FillRectWithColor(l->window, c->bg2, 90, y, 126, 20);
    Window_FillRectWithColor(l->window, c->fill2, 1, (u16)(y + 1), 2, 2);
    Window_FillRectWithColor(l->window, c->fill1, 8, y, 2, 7);
    Window_FillRectWithColor(l->window, c->fill1, 9, (u16)(y + 9), 2, 2);
    Window_FillRectWithColor(l->window, c->fill1, 9, (u16)(y + 13), 2, 2);
    Window_FillRectWithColor(l->window, c->fill1, 9, (u16)(y + 17), 2, 2);
    if (copyNow)
        Window_CopyToVRAM(l->window);
}

static u8 list_bg_index(PhoneList *l, u8 slot)
{
    return l->firstBgColor ? (u8)(1 - (slot % 2)) : (u8)(slot % 2);
}

static void list_print(PokegearPhoneAppData *ph, u8 slot, u8 index, BOOL selected, BOOL copyNow)
{
    PhoneList *l = &ph->list;
    u8 colorIdx = list_bg_index(l, slot);
    PhoneColors *c = &l->colors[colorIdx];
    u16 y = (u16)(slot * 24);
    u8 id = ph->order[index];

    list_draw_slot_bg(l, slot, colorIdx, FALSE);
    contact_name(&ph->call, id, ph->call.contactName);
    contact_class(&ph->call, id, ph->call.contactClass);
    if (selected || index == l->selectedIndex) {
        Text_AddPrinterWithParamsAndColor(l->window, FONT_SUBSCREEN, ph->call.contactName, 16, (u32)(y + 2), TEXT_SPEED_NO_TRANSFER, c->nameSelected, NULL);
        Text_AddPrinterWithParamsAndColor(l->window, FONT_SYSTEM, ph->call.contactClass, 94, (u32)(y + 2), TEXT_SPEED_NO_TRANSFER, c->classSelected, NULL);
    } else {
        Text_AddPrinterWithParamsAndColor(l->window, FONT_SUBSCREEN, ph->call.contactName, 16, (u32)(y + 2), TEXT_SPEED_NO_TRANSFER, c->nameDeselected, NULL);
        Text_AddPrinterWithParamsAndColor(l->window, FONT_SYSTEM, ph->call.contactClass, 94, (u32)(y + 2), TEXT_SPEED_NO_TRANSFER, c->classDeselected, NULL);
    }
    if (copyNow)
        Window_CopyToVRAM(l->window);
}

static void list_update_arrows(PhoneList *l)
{
    if (l->arrows[0] != NULL)
        Sprite_SetDrawFlag(l->arrows[0], l->firstContactOnPage != 0);
    if (l->arrows[1] != NULL)
        Sprite_SetDrawFlag(l->arrows[1], l->firstContactOnPage + PH_ROWS < l->numContacts);
}

static void list_set_cursor(PhoneList *l, u8 position, BOOL visible)
{
    int i;

    for (i = 0; i < 4; i++)
        if (l->cursor[i] != NULL)
            Sprite_SetDrawFlag(l->cursor[i], visible);
    if (position >= PH_ROWS)
        position = l->cursorPos;
    if (l->cursorPos >= l->listBottomIndex && l->listBottomIndex > 0) {
        l->cursorPos = (u8)(l->listBottomIndex - 1);
        position = l->cursorPos;
    }
    if (l->cursor[0] != NULL) Sprite_SetPositionXY(l->cursor[0], 16, (s16)(position * 24 + 8));
    if (l->cursor[1] != NULL) Sprite_SetPositionXY(l->cursor[1], 16, (s16)(position * 24 + 30));
    if (l->cursor[2] != NULL) Sprite_SetPositionXY(l->cursor[2], 224, (s16)(position * 24 + 8));
    if (l->cursor[3] != NULL) Sprite_SetPositionXY(l->cursor[3], 224, (s16)(position * 24 + 30));
}

static void list_show_cursor(PhoneList *l, BOOL animate)
{
    int i;

    for (i = 0; i < 4; i++) {
        if (l->cursor[i] == NULL)
            continue;
        Sprite_SetAnimateFlag(l->cursor[i], animate);
        Sprite_SetDrawFlag(l->cursor[i], TRUE);
    }
}

static void list_draw_slot_bgs(PhoneList *l)
{
    u8 color = l->firstBgColor;
    int i;

    for (i = 0; i < 8; i++) {
        list_draw_slot_bg(l, (u8)i, color, FALSE);
        color ^= 1;
    }
}

/* PokegearPhone_SetContactListUIAndDraw */
static void list_draw_page(PokegearPhoneAppData *ph, u8 first, u8 cursorPos)
{
    PhoneList *l = &ph->list;
    int i, r;

    if (first >= l->numContacts)
        first = 0;
    list_draw_slot_bgs(l);
    l->listBottomIndex = 0;
    l->firstContactOnPage = first;
    l->selectedIndex = 0xFF;
    for (i = 0, r = first; i < PH_ROWS; i++, r++) {
        if (r >= l->numContacts) {
            l->lastContactIndex = (u8)(r - 1);
            l->listBottomIndex = (u8)i;
            break;
        }
        list_print(ph, (u8)(i + 1), (u8)r, FALSE, FALSE);
    }
    if (l->listBottomIndex == 0) {
        l->listBottomIndex = (u8)i;
        l->lastContactIndex = (u8)(i - 1 + first);
    }
    if (cursorPos >= l->listBottomIndex)
        cursorPos = 0;
    l->cursorPos = cursorPos;
    Window_CopyToVRAM(l->window);
    list_update_arrows(l);
    list_set_cursor(l, cursorPos, TRUE);
}

static void list_deselect(PokegearPhoneAppData *ph)
{
    PhoneList *l = &ph->list;
    u8 index = l->selectedIndex;

    l->selectedIndex = 0xFF;
    if (index != 0xFF && index >= l->firstContactOnPage && l->lastContactIndex >= index)
        list_print(ph, (u8)(index - l->firstContactOnPage + 1), index, FALSE, TRUE);
}

static BOOL list_start_scroll(PokegearPhoneAppData *ph, u8 direction)
{
    PhoneList *l = &ph->list;

    if (direction) {
        if (l->firstContactOnPage < 1)
            return FALSE;
        l->firstContactOnPage--;
        list_print(ph, 0, l->firstContactOnPage, FALSE, TRUE);
        l->lastContactIndex--;
    } else {
        if (l->lastContactIndex >= l->numContacts - 1)
            return FALSE;
        l->lastContactIndex++;
        list_print(ph, 7, l->lastContactIndex, FALSE, TRUE);
        l->firstContactOnPage++;
    }
    l->scrollTimer = 0;
    l->scrollDirection = direction;
    l->isScrolling = TRUE;
    ph->menuInputStateBak = 1;
    l->firstBgColor ^= 1;
    list_update_arrows(l);
    return TRUE;
}

static BOOL list_scroll_step(PhoneList *l)
{
    Window_Scroll(l->window, l->scrollDirection ? SCROLL_DIRECTION_DOWN : SCROLL_DIRECTION_UP, 8, 0);
    Window_CopyToVRAM(l->window);
    if (l->scrollTimer++ >= 2) {
        l->scrollTimer = 0;
        return TRUE;
    }
    return FALSE;
}

static void list_start_page_scroll(PokegearPhoneAppData *ph, u8 direction)
{
    PhoneList *l = &ph->list;

    l->pageScrollStep = 0;
    l->isPageScroll = 1;
    l->scrollDirection = direction;
    l->isScrolling = 1;
    ph->menuInputStateBak = 1;
    if (!list_start_scroll(ph, direction))
        l->pageScrollFailed = 1;
}

static BOOL list_scroll_many(PokegearPhoneAppData *ph)
{
    PhoneList *l = &ph->list;

    if (!list_scroll_step(l))
        return FALSE;
    if (l->pageScrollFailed || l->pageScrollStep++ >= 5 || !list_start_scroll(ph, l->scrollDirection)) {
        l->pageScrollStep = 0;
        l->isPageScroll = 0;
        l->pageScrollFailed = 0;
        return TRUE;
    }
    return FALSE;
}

static void list_scroll_in_progress(PokegearPhoneAppData *ph)
{
    PhoneList *l = &ph->list;
    BOOL done = l->isPageScroll ? list_scroll_many(ph) : list_scroll_step(l);

    if (done) {
        ph->menuInputStateBak = 0;
        l->isScrolling = 0;
    }
}

/* PhoneContactListUI_HandleKeyInput: the chosen row, or -1. */
static int list_keys(PokegearPhoneAppData *ph)
{
    PhoneList *l = &ph->list;
    u8 selected;

    if (l->isScrolling) {
        list_scroll_in_progress(ph);
        return -1;
    }
    if (l->scrollTimer != 0) {
        l->scrollTimer--;
        return -1;
    }
    selected = (u8)(l->firstContactOnPage + l->cursorPos);
    if (gSystem.pressedKeys & PAD_BUTTON_A) {
        l->selectedIndex = selected;
        list_print(ph, (u8)(l->cursorPos + 1), l->selectedIndex, TRUE, TRUE);
        PokegearApp_PlaySE(PG_SE_DECIDE);
        return l->selectedIndex;
    }
    if (gSystem.pressedKeysRepeatable & PAD_KEY_UP) {
        if (selected == 0)
            return -1;
        PokegearApp_PlaySE(PG_SE_CURSOR);
        if (l->cursorPos == 0) {
            list_start_scroll(ph, 1);
        } else {
            l->cursorPos--;
            list_set_cursor(l, l->cursorPos, TRUE);
            l->scrollTimer = 2;
        }
        return -1;
    }
    if (gSystem.pressedKeysRepeatable & PAD_KEY_DOWN) {
        if (selected >= l->numContacts - 1)
            return -1;
        PokegearApp_PlaySE(PG_SE_CURSOR);
        if (l->cursorPos == PH_ROWS - 1) {
            list_start_scroll(ph, 0);
        } else {
            l->cursorPos++;
            list_set_cursor(l, l->cursorPos, TRUE);
            l->scrollTimer = 2;
        }
        return -1;
    }
    if (gSystem.pressedKeys & PAD_KEY_LEFT) {
        if (l->firstContactOnPage != 0) {
            PokegearApp_PlaySE(PG_SE_CURSOR);
            list_start_page_scroll(ph, 1);
        }
        return -1;
    }
    if (gSystem.pressedKeys & PAD_KEY_RIGHT) {
        if (l->firstContactOnPage + PH_ROWS < l->numContacts) {
            PokegearApp_PlaySE(PG_SE_CURSOR);
            list_start_page_scroll(ph, 0);
        }
        return -1;
    }
    return -1;
}

/* PhoneContactListUI_HandleTouchInput: the chosen row + 1, 0 for a page
 * turn, -1 for nothing. */
static int list_touch(PokegearPhoneAppData *ph)
{
    PhoneList *l = &ph->list;
    int r;

    if (l->isScrolling) {
        list_scroll_in_progress(ph);
        return -1;
    }
    r = TouchScreen_CheckRectanglePressed(sListRects);
    if (r == TOUCHSCREEN_INPUT_NONE)
        return -1;
    if (r < PH_ROWS && r < l->listBottomIndex) {
        l->cursorPos = (u8)r;
        l->selectedIndex = (u8)(l->firstContactOnPage + r);
        list_print(ph, (u8)(l->cursorPos + 1), l->selectedIndex, TRUE, TRUE);
        list_set_cursor(l, l->cursorPos, TRUE);
        PokegearApp_PlaySE(PG_SE_DECIDE);
        return l->selectedIndex + 1;
    }
    if (r == 6 && l->firstContactOnPage != 0) {
        list_start_page_scroll(ph, 1);
        PokegearApp_PlaySE(PG_SE_CURSOR);
        return 0;
    }
    if (r == 7 && l->firstContactOnPage + PH_ROWS < l->numContacts) {
        list_start_page_scroll(ph, 0);
        PokegearApp_PlaySE(PG_SE_CURSOR);
        return 0;
    }
    return -1;
}

/* PokegearPhone_SortList: by the phone book's own keys. */
static void phone_sort(PokegearPhoneAppData *ph, u8 key)
{
    int i, j;

    if (ph->book == NULL)
        return;
    for (i = 0; i < ph->numContacts - 1; i++) {
        for (j = ph->numContacts - 1; j > i; j--) {
            const PhoneBookEntry *a = book_entry(ph, ph->order[j]);
            const PhoneBookEntry *b = book_entry(ph, ph->order[i]);

            if (a != NULL && b != NULL && a->sortParam[key] < b->sortParam[key]) {
                u8 t = ph->order[i];

                ph->order[i] = ph->order[j];
                ph->order[j] = t;
            }
        }
    }
    ph->list.cursorPos = 0;
    ph->list.firstContactOnPage = 0;
    list_draw_page(ph, 0, 0);
}

static void phone_return_to_list(PokegearPhoneAppData *ph)
{
    list_deselect(ph);
    list_show_cursor(&ph->list, TRUE);
    list_set_cursor(&ph->list, 0xFF, TRUE);
}

/* ------------------------------------------------------------------ */
/* A call (overlay_101_021F1D74.c)                                     */
/* ------------------------------------------------------------------ */

static void call_create(PokegearPhoneAppData *ph)
{
    PhoneCall *c = &ph->call;
    int i;

    memset(c, 0, sizeof *c);
    c->ph = ph;
    c->msg271 = openmmo_pokegear_msg(MSG_PHONE, ph->heapID);
    c->msg640 = openmmo_pokegear_msg(MSG_GREETINGS, ph->heapID);
    c->fmt = StringTemplate_New(16, 37, ph->heapID);
    c->expand = String_Init(1081, ph->heapID);
    c->read = String_Init(1081, ph->heapID);
    c->contactName = String_Init(16, ph->heapID);
    c->contactClass = String_Init(44, ph->heapID);
    c->classFmt = c->msg271 != NULL ? MessageLoader_GetNewString(c->msg271, PHMSG_CLASS_FMT) : String_Init(4, ph->heapID);
    for (i = 0; i < 3; i++)
        c->ring[i] = c->msg271 != NULL ? MessageLoader_GetNewString(c->msg271, (u32)(PHMSG_RING + i)) : String_Init(4, ph->heapID);
    for (i = 0; i < 4; i++)
        c->click[i] = c->msg271 != NULL ? MessageLoader_GetNewString(c->msg271, (u32)(PHMSG_CLICK + i)) : String_Init(4, ph->heapID);
    c->playerGender = ph->pokegear->args->playerGender;
    c->playerHgMap = ph->pokegear->args->ported ? (int)ph->pokegear->args->mapID - POKEGEAR_PORTED_FIRST : -1;
    c->canCall = 1;
    if (ph->pokegear->args->ported) {
        int calls = 1;

        openmmo_pokegear_map(ph->pokegear->args->mapID, NULL, NULL, NULL, NULL, &calls, NULL);
        c->canCall = calls;
    }
}

static void call_destroy(PokegearPhoneAppData *ph)
{
    PhoneCall *c = &ph->call;
    int i;

    for (i = 0; i < 4; i++)
        if (c->click[i] != NULL)
            String_Free(c->click[i]);
    for (i = 0; i < 3; i++)
        if (c->ring[i] != NULL)
            String_Free(c->ring[i]);
    if (c->classFmt != NULL) String_Free(c->classFmt);
    if (c->contactClass != NULL) String_Free(c->contactClass);
    if (c->contactName != NULL) String_Free(c->contactName);
    if (c->read != NULL) String_Free(c->read);
    if (c->expand != NULL) String_Free(c->expand);
    if (c->fmt != NULL) StringTemplate_Free(c->fmt);
    if (c->msgContact != NULL) MessageLoader_Free(c->msgContact);
    if (c->msg640 != NULL) MessageLoader_Free(c->msg640);
    if (c->msg271 != NULL) MessageLoader_Free(c->msg271);
    memset(c, 0, sizeof *c);
}

/* PhoneCall_InitMsgDataAndBufferNames: the caller's bank, and the names
 * every line may use, the player, the caller, where each of them is. */
static void call_open_bank(PhoneCall *c)
{
    if (c->msgContact != NULL)
        MessageLoader_Free(c->msgContact);
    c->msgContact = c->callerID < POKEGEAR_PHONE_CONTACTS
                        ? openmmo_pokegear_msg(gPokegearContactBanks[c->callerID], c->ph->heapID)
                        : NULL;
    StringTemplate_SetPlayerName(c->fmt, 0, SaveData_GetTrainerInfo(c->ph->pokegear->saveData));
    contact_name(c, c->callerID, c->contactName);
    StringTemplate_SetString(c->fmt, 1, c->contactName, 2, 1, GAME_LANGUAGE);
    StringTemplate_SetLocationName(c->fmt, 2, openmmo_pokegear_label((int)c->ph->pokegear->args->mapID));
    if (c->entry != NULL)
        StringTemplate_SetLocationName(c->fmt, 3, openmmo_pokegear_label(POKEGEAR_PORTED_FIRST + c->entry->mapId));
}

static void call_fast_forward(PhoneCall *c)
{
    Sprite *s = c->ph->sprites[13];

    if (s != NULL && TouchScreen_CheckRectanglePressed(sFastForwardRect) == 0) {
        Sprite_SetAnimateFlag(s, TRUE);
        Sprite_RestartAnim(s);
    }
}

static void call_print(PhoneCall *c, MessageLoader *m, const u8 *ids)
{
    Window *w = &c->ph->windows[0];

    String_Clear(c->read);
    if (m != NULL)
        MessageLoader_GetString(m, ids[c->playerGender], c->read);
    StringTemplate_Format(c->fmt, c->expand, c->read);
    Window_FillTilemap(w, 0);
    c->printer = Text_AddPrinterWithParamsAndColor(w, FONT_SYSTEM, c->expand, 0, 0, c->ph->textDelay, TEXT_COLOR(1, 2, 0), NULL);
}

static void call_print_gendered(PhoneCall *c, MessageLoader *m, u8 male, u8 female)
{
    c->msgIds[0] = male;
    c->msgIds[1] = female;
    call_print(c, m, c->msgIds);
}

static void call_print_one(PhoneCall *c, MessageLoader *m, u8 id)
{
    call_print_gendered(c, m, id, id);
}

static BOOL call_printed(PhoneCall *c)
{
    call_fast_forward(c);
    return !Text_IsPrinterActive(c->printer);
}

/* PhoneCall_ApplyGenericNPCcallSideEffect: only the random word reaches a
 * line here; a flag, a rematch or a gift is a save's business. */
static void call_side_effect(PhoneCall *c, const PhoneCallScriptDef *def)
{
    if (def->scriptType == PHSCRIPT_WORD && c->msgContact != NULL && def->param0 != 0) {
        MessageLoader_GetString(c->msgContact, (u32)(def->param1 + LCRNG_Next() % def->param0), c->expand);
        StringTemplate_SetString(c->fmt, 4, c->expand, 2, 1, GAME_LANGUAGE);
    }
}

static void call_line_static(PhoneCall *c, String *s, BOOL clear)
{
    Window *w = &c->ph->windows[0];

    if (clear)
        Window_FillTilemap(w, 0);
    Text_AddPrinterWithParamsAndColor(w, FONT_SYSTEM, s, 0, 0, TEXT_SPEED_INSTANT, TEXT_COLOR(1, 2, 0), NULL);
}

/* PhoneCall_InitialRing */
static BOOL call_ring(PhoneCall *c)
{
    call_fast_forward(c);
    switch (c->toneState) {
    case 0:
        PokegearApp_PlaySE(PG_SE_RING);
        call_line_static(c, c->ring[0], TRUE);
        break;
    case 1:
        if (Sound_IsEffectPlaying(SEQ_SE_PL_CALL_sseq))
            return FALSE;
        PokegearApp_PlaySE(PG_SE_RING);
        call_line_static(c, c->ring[1], FALSE);
        break;
    case 2:
        if (Sound_IsEffectPlaying(SEQ_SE_PL_CALL_sseq))
            return FALSE;
        c->toneState = 0;
        return TRUE;
    }
    c->toneState++;
    return FALSE;
}

/* PhoneCall_HangupTone: Click! and the dots. */
static BOOL call_hangup(PhoneCall *c, BOOL tone)
{
    call_fast_forward(c);
    switch (c->toneState) {
    case 0:
        if (tone)
            PokegearApp_PlaySE(PG_SE_HANGUP);
        call_line_static(c, tone ? c->click[0] : c->ring[0], TRUE);
        c->toneTimer = 0;
        break;
    case 1:
    case 2:
        if (c->toneTimer++ < 10)
            return FALSE;
        c->toneTimer = 0;
        call_line_static(c, tone ? c->click[c->toneState] : c->ring[c->toneState], FALSE);
        break;
    case 3:
        if (c->toneTimer++ < 10)
            return FALSE;
        c->toneTimer = 0;
        c->toneState = 0;
        return TRUE;
    }
    c->toneState++;
    return FALSE;
}

static BOOL call_wait_button(PhoneCall *c)
{
    call_fast_forward(c);
    if (gSystem.pressedKeys & (PAD_BUTTON_A | PAD_BUTTON_B)) {
        PokegearApp_PlaySE(PG_SE_DECIDE);
        c->ph->pokegear->menuInputState = MENU_INPUT_STATE_BUTTONS;
        return TRUE;
    }
    if (TouchScreen_CheckRectanglePressed(sFastForwardRect) == 0) {
        PokegearApp_PlaySE(PG_SE_DECIDE);
        c->ph->pokegear->menuInputState = MENU_INPUT_STATE_TOUCH;
        return TRUE;
    }
    return FALSE;
}

/* PhoneCall_PrintGreeting: the caller's own hello for the hour. */
static BOOL call_greeting(PhoneCall *c)
{
    u16 idx;

    call_fast_forward(c);
    switch (c->toneState) {
    case 0:
        idx = (u16)(c->timeOfDay * 2);
        if (c->entry == NULL || c->entry->greeting >= 8) {
            c->toneState = 0;
            return TRUE;
        }
        call_print_gendered(c, c->msg640, gPokegearGreetings[c->entry->greeting][idx],
                            gPokegearGreetings[c->entry->greeting][idx + 1]);
        break;
    case 1:
        if (!call_printed(c))
            return FALSE;
        c->toneState = 0;
        return TRUE;
    }
    c->toneState++;
    return FALSE;
}

/* The call's own popup (PhoneCall_TouchscreenListMenu_Create): the list is
 * dimmed a shade more and the fast-forward button steps aside. */
static void call_menu_open(PhoneCall *c, const u8 *rows, int n, u8 x, u8 y, u8 width)
{
    PaletteData *pd = c->ph->pokegear->plttData;

    phone_tooltip(c->ph, PH_TIP_WHAT_DO, TRUE);
    c->menu = phone_menu_open(c->ph, rows, n, x, y, width);
    if (c->ph->sprites[13] != NULL)
        Sprite_SetDrawFlag(c->ph->sprites[13], FALSE);
    PaletteData_Blend(pd, PLTTBUF_MAIN_BG, 0x10, 0x10, 0, COLOR_BLACK);
    PaletteData_Blend(pd, PLTTBUF_MAIN_BG, 0xA0, 0x10, 0, COLOR_BLACK);
    PaletteData_SetAutoTransparent(pd, TRUE);
    PaletteData_CommitFadedBuffers(pd);
    PaletteData_SetAutoTransparent(pd, FALSE);
}

static void call_menu_close(PhoneCall *c)
{
    PaletteData *pd = c->ph->pokegear->plttData;

    if (c->menu != NULL) {
        c->ph->pokegear->menuInputState = PokegearMenu_LastInputWasTouch(c->menu) ? MENU_INPUT_STATE_TOUCH : MENU_INPUT_STATE_BUTTONS;
        PokegearMenu_Close(c->menu);
    }
    c->menu = NULL;
    phone_tooltip(c->ph, 0, FALSE);
    if (c->ph->sprites[13] != NULL)
        Sprite_SetDrawFlag(c->ph->sprites[13], TRUE);
    PaletteData_Blend(pd, PLTTBUF_MAIN_BG, 0x10, 0x10, 8, COLOR_BLACK);
    PaletteData_Blend(pd, PLTTBUF_MAIN_BG, 0xA0, 0x10, 8, COLOR_BLACK);
    PaletteData_SetAutoTransparent(pd, TRUE);
    PaletteData_CommitFadedBuffers(pd);
    PaletteData_SetAutoTransparent(pd, FALSE);
}

/* ---- each caller ---- */

/* GearPhoneCall_Simple: a greeting, then the script's one line. */
static BOOL handler_simple(PhoneCall *c)
{
    const PhoneCallScriptDef *def = c->def;

    switch (c->scriptState) {
    case 0:
        call_open_bank(c);
        call_side_effect(c, def);
        if (c->entry == NULL || c->entry->greeting == 0xFF)
            c->scriptState++;
        break;
    case 1:
        if (!call_greeting(c))
            return FALSE;
        break;
    case 2:
        call_print(c, c->msgContact, def->msgIds);
        break;
    case 3:
        if (!call_printed(c))
            return FALSE;
        return TRUE;
    }
    c->scriptState++;
    return FALSE;
}

/* GearPhoneCall_Mother: her hello for where the player is. The savings
 * talk that follows in HeartGold is money, which is the server's. */
static BOOL handler_mother(PhoneCall *c)
{
    switch (c->scriptState) {
    case 0:
        call_open_bank(c);
        StringTemplate_SetNumber(c->fmt, 10, 0, 6, PADDING_MODE_NONE, CHARSET_MODE_EN);
        call_print_one(c, c->msgContact,
                       (u8)(7 + (c->playerHgMap >= 0 && c->playerHgMap < POKEGEAR_HG_MAPS ? gPokegearMomCallIntro[c->playerHgMap] : 0)));
        break;
    default:
        if (!call_printed(c))
            return FALSE;
        return TRUE;
    }
    c->scriptState++;
    return FALSE;
}

/* GearPhoneCall_Bill: a greeting, the PC's box and how much room is left. */
static BOOL handler_bill(PhoneCall *c)
{
    PCBoxes *pc;
    u32 count;

    switch (c->scriptState) {
    case 0:
        call_open_bank(c);
        break;
    case 1:
        if (!call_greeting(c))
            return FALSE;
        call_print_gendered(c, c->msgContact, 3, 4);
        break;
    case 2:
        if (!call_printed(c))
            return FALSE;
        pc = SaveData_GetPCBoxes(c->ph->pokegear->saveData);
        PCBoxes_BufferBoxName(pc, PCBoxes_GetCurrentBoxID(pc), c->expand);
        StringTemplate_SetString(c->fmt, 10, c->expand, 2, 1, GAME_LANGUAGE);
        count = MAX_PC_BOXES * MAX_MONS_PER_BOX - PCBoxes_CountAllBoxMons(pc);
        StringTemplate_SetNumber(c->fmt, 11, (int)count, 3, PADDING_MODE_NONE, CHARSET_MODE_EN);
        if (count == 0)
            call_print_one(c, c->msgContact, 9);
        else
            call_print_one(c, c->msgContact, (u8)(5 + LCRNG_Next() % 3));
        break;
    case 3:
        if (!call_printed(c))
            return FALSE;
        call_print_one(c, c->msgContact, 8);
        break;
    case 4:
        if (!call_printed(c))
            return FALSE;
        return TRUE;
    }
    c->scriptState++;
    return FALSE;
}

/* GetOakNationalDexRating, and the fanfare that goes with it. */
static u8 oak_national_rating(u16 caught, u8 gender, u16 *fanfare)
{
    static const u16 steps[] = { 100, 150, 200, 250, 300, 350, 400, 435, 465, 475, 483 };
    int i;

    *fanfare = SEQ_FANFA4_sseq;
    for (i = 0; i < 11; i++)
        if (caught <= steps[i])
            return (u8)(46 + i);
    *fanfare = SEQ_FANFA6_sseq;
    return gender ? 25 : 24;
}

/* GearPhoneCall_ProfOak: the National Pokedex's evaluation. HeartGold's
 * Johto count wants a table this game has no dex for. */
static BOOL handler_oak(PhoneCall *c)
{
    const Pokedex *dex;
    int r;
    u16 fanfare, seen, caught;
    u8 line;

    switch (c->scriptState) {
    case 0:
        call_open_bank(c);
        call_print_gendered(c, c->msgContact, 13, 14);
        break;
    case 1:
        if (!call_printed(c))
            return FALSE;
        call_print_one(c, c->msgContact, 15);
        break;
    case 2:
        if (!call_printed(c))
            return FALSE;
        call_menu_open(c, sMenuEvalNational, 2, 7, 11, 22);
        break;
    case 3:
        r = PokegearMenu_Input(c->menu);
        if (r == -1)
            return FALSE;
        call_menu_close(c);
        if (r != 0) {
            call_print_one(c, c->msgContact, 21);
            c->scriptState = 255;
            return FALSE;
        }
        dex = SaveData_GetPokedex(c->ph->pokegear->saveData);
        seen = Pokedex_CountSeen_National(dex);
        caught = Pokedex_CountCaught_National(dex);
        StringTemplate_SetNumber(c->fmt, 5, seen, 3, PADDING_MODE_NONE, CHARSET_MODE_EN);
        StringTemplate_SetNumber(c->fmt, 6, caught, 3, PADDING_MODE_NONE, CHARSET_MODE_EN);
        call_print_one(c, c->msgContact, 20);
        break;
    case 4:
        if (!call_printed(c))
            return FALSE;
        dex = SaveData_GetPokedex(c->ph->pokegear->saveData);
        line = oak_national_rating(Pokedex_CountCaught_National(dex), c->playerGender, &fanfare);
        Sound_PlayFanfare(fanfare);
        call_print_one(c, c->msgContact, line);
        if (line == 24 || line == 25) {
            c->scriptState = 255;
            return FALSE;
        }
        break;
    case 5:
        if (!call_printed(c))
            return FALSE;
        if (Sound_IsBGMPausedByFanfare())
            return FALSE;
        call_print_one(c, c->msgContact, 21);
        break;
    default:
        if (!call_printed(c))
            return FALSE;
        if (c->scriptState == 255 && Sound_IsBGMPausedByFanfare())
            return FALSE;
        return TRUE;
    }
    c->scriptState++;
    return FALSE;
}

/* GearPhoneCall_DayCareLady: nobody is boarded here. */
static BOOL handler_daycare_lady(PhoneCall *c)
{
    switch (c->scriptState) {
    case 0:
        call_open_bank(c);
        call_print_gendered(c, c->msgContact, 3, 4);
        break;
    case 1:
        if (!call_printed(c))
            return FALSE;
        call_print_one(c, c->msgContact, 10);
        break;
    default:
        if (!call_printed(c))
            return FALSE;
        return TRUE;
    }
    c->scriptState++;
    return FALSE;
}

/* GearPhoneCall_DayCareMan: no egg, nobody boarded, see you. */
static BOOL handler_daycare_man(PhoneCall *c)
{
    switch (c->scriptState) {
    case 0:
        call_open_bank(c);
        call_print_one(c, c->msgContact, 2);
        break;
    case 1:
        if (!call_printed(c))
            return FALSE;
        call_print_one(c, c->msgContact, 4);
        break;
    case 2:
        if (!call_printed(c))
            return FALSE;
        call_print_one(c, c->msgContact, 11);
        break;
    default:
        if (!call_printed(c))
            return FALSE;
        return TRUE;
    }
    c->scriptState++;
    return FALSE;
}

/* GearPhoneCall_Buena: her hello for the hour and a bit of chatter. */
static BOOL handler_buena(PhoneCall *c)
{
    switch (c->scriptState) {
    case 0:
        call_open_bank(c);
        call_print_gendered(c, c->msgContact, (u8)(c->timeOfDay * 2 + 3), (u8)(c->timeOfDay * 2 + 4));
        break;
    case 1:
        if (!call_printed(c))
            return FALSE;
        call_print_one(c, c->msgContact, (u8)(10 + LCRNG_Next() % 3));
        break;
    default:
        if (!call_printed(c))
            return FALSE;
        return TRUE;
    }
    c->scriptState++;
    return FALSE;
}

/* GearPhoneCall_GetEthanLyraMessage: the friend has a line about where the
 * player is calling from. */
static u8 friend_line_for(int hgMap)
{
    int i;

    for (i = 0; i < 73; i++)
        if (hgMap == gPokegearFriendMaps[i])
            return (u8)(13 + i);
    return (u8)(10 + LCRNG_Next() % 3);
}

static BOOL handler_friend(PhoneCall *c)
{
    switch (c->scriptState) {
    case 0:
        call_open_bank(c);
        break;
    case 1:
        call_print_one(c, c->msgContact, (u8)(4 + c->timeOfDay));
        break;
    case 2:
        if (!call_printed(c))
            return FALSE;
        call_print_one(c, c->msgContact, friend_line_for(c->playerHgMap));
        break;
    default:
        if (!call_printed(c))
            return FALSE;
        return TRUE;
    }
    c->scriptState++;
    return FALSE;
}

/* GearPhoneCall_Kurt: nothing of the player's is on his bench. */
static BOOL handler_kurt(PhoneCall *c)
{
    switch (c->scriptState) {
    case 0:
        call_open_bank(c);
        call_print_one(c, c->msgContact, 2);
        break;
    case 1:
        if (!call_printed(c))
            return FALSE;
        call_print_one(c, c->msgContact, 5);
        break;
    default:
        if (!call_printed(c))
            return FALSE;
        return TRUE;
    }
    c->scriptState++;
    return FALSE;
}

static BOOL call_run_handler(PhoneCall *c)
{
    switch (c->scriptType) {
    case PHH_BILL: return handler_bill(c);
    case PHH_MOTHER: return handler_mother(c);
    case PHH_PROF_OAK: return handler_oak(c);
    case PHH_DAYCARE_LADY: return handler_daycare_lady(c);
    case PHH_DAYCARE_MAN: return handler_daycare_man(c);
    case PHH_BUENA: return handler_buena(c);
    case PHH_ETHAN_LYRA: return handler_friend(c);
    case PHH_KURT: return handler_kurt(c);
    default: return handler_simple(c);
    }
}

/* PhoneCall_GetCallScriptId, each caller's branch folded to the cleared
 * story: Elm has the late-game remarks, Oak evaluates, Baoba has the whole
 * Safari open, Buena's password hour is the radio's. */
static void call_choose_script(PhoneCall *c)
{
    const PhoneBookEntry *e = c->entry;
    BOOL local = e != NULL && c->playerHgMap >= 0 && e->mapId == c->playerHgMap;

    c->scriptType = PHH_SIMPLE;
    c->scriptID = 0;
    Window_FillTilemap(&c->ph->windows[0], 0);
    Window_ScheduleCopyToVRAM(&c->ph->windows[0]);
    if (e == NULL)
        return;
    switch (e->type) {
    case PHCALL_MOM:
        if (local) c->scriptID = 23; else c->scriptType = PHH_MOTHER;
        break;
    case PHCALL_PROF_ELM:
        c->scriptID = local ? 1 : (u16)(20 + LCRNG_Next() % 2);
        break;
    case PHCALL_PROF_OAK:
        if (local) c->scriptID = 68; else c->scriptType = PHH_PROF_OAK;
        break;
    case PHCALL_KURT:
        if (local) c->scriptID = 83; else c->scriptType = PHH_KURT;
        break;
    case PHCALL_BILL:
        if (local) c->scriptID = 92; else c->scriptType = PHH_BILL;
        break;
    case PHCALL_DAYCAREMAN:
        c->scriptType = PHH_DAYCARE_MAN;
        break;
    case PHCALL_DAYCARELADY:
        if (local) c->scriptID = 97; else c->scriptType = PHH_DAYCARE_LADY;
        break;
    case PHCALL_BUENA:
        if (c->hour % 3 == 2) c->scriptID = 100;
        else if (local) c->scriptID = 98;
        else c->scriptType = PHH_BUENA;
        break;
    case PHCALL_ETHAN_LYRA:
        if (local) c->scriptID = c->playerGender ? 101 : 102; else c->scriptType = PHH_ETHAN_LYRA;
        break;
    case PHCALL_BAOBA:
        c->scriptID = local ? 140 : 154;
        break;
    default:
        c->scriptID = e->phoneScriptIfLocal;
        break;
    }
    if (c->scriptID >= POKEGEAR_PHONE_SCRIPTS)
        c->scriptID = 0;
    c->def = &gPokegearPhoneScripts[c->scriptID];
}

/* PhoneCall_InitContext */
static BOOL call_begin(PokegearPhoneAppData *ph, u8 callerID)
{
    PhoneCall *c = &ph->call;
    RTCDate date;
    RTCTime time;

    c->mainState = 0;
    c->scriptState = 0;
    c->toneState = 0;
    c->toneTimer = 0;
    c->callerID = callerID;
    c->entry = book_entry(ph, callerID);
    c->menu = NULL;
    c->shared = 0;
    c->flag0 = c->flag1 = c->flag2 = c->flag3 = 0;
    GetCurrentDateTime(&date, &time);
    c->hour = (u8)time.hour;
    c->timeOfDay = time_of_day(c->hour);
    if (!c->canCall) {
        c->mainState = 256;
        return TRUE;
    }
    call_choose_script(c);
    return TRUE;
}

/* PhoneCall_Main */
static BOOL call_main(PhoneCall *c)
{
    switch (c->mainState) {
    case 0:
        if (!call_ring(c))
            return FALSE;
        break;
    case 1:
        if (!call_run_handler(c))
            return FALSE;
        break;
    case 2:
        if (!call_wait_button(c))
            return FALSE;
        break;
    case 3:
        if (!call_hangup(c, TRUE))
            return FALSE;
        c->mainState = 0;
        return TRUE;
    case 256: /* out of the area */
        if (!call_hangup(c, TRUE))
            return FALSE;
        String_Clear(c->read);
        if (c->msg271 != NULL)
            MessageLoader_GetString(c->msg271, PHMSG_OUT_OF_AREA, c->read);
        StringTemplate_Format(c->fmt, c->expand, c->read);
        Window_FillTilemap(&c->ph->windows[0], 0);
        c->printer = Text_AddPrinterWithParamsAndColor(&c->ph->windows[0], FONT_SYSTEM, c->expand, 0, 0, c->ph->textDelay, TEXT_COLOR(1, 2, 0), NULL);
        break;
    case 257:
        if (!call_printed(c))
            return FALSE;
        break;
    case 258:
        if (!call_wait_button(c))
            return FALSE;
        c->mainState = 0;
        return TRUE;
    }
    c->mainState++;
    return FALSE;
}

static void call_end(PokegearPhoneAppData *ph)
{
    PhoneCall *c = &ph->call;

    if (c->menu != NULL)
        call_menu_close(c);
    if (c->msgContact != NULL)
        MessageLoader_Free(c->msgContact);
    c->msgContact = NULL;
    Window_FillTilemap(&ph->windows[0], 0);
    Window_FillTilemap(&ph->windows[1], 0);
    Window_CopyToVRAM(&ph->windows[0]);
    Window_CopyToVRAM(&ph->windows[1]);
    RenderControlFlags_SetSpeedUpOnTouch(FALSE);
    phone_dim(ph, FALSE);
    phone_return_to_list(ph);
}

/* ------------------------------------------------------------------ */
/* Setup and teardown (overlay_101_021EFD20.c PokegearPhone_SetUp)     */
/* ------------------------------------------------------------------ */

static void phone_pick_contacts(PokegearPhoneAppData *ph)
{
    int i;

    ph->numContacts = 0;
    for (i = 0; i < (int)(sizeof sStoryContacts / sizeof sStoryContacts[0]); i++) {
        u8 id = sStoryContacts[i];

        if (id == PHC_ETHAN && ph->pokegear->args->playerGender == 0)
            id = PHC_LYRA;
        if (book_entry(ph, id) == NULL)
            continue;
        ph->order[ph->numContacts++] = id;
    }
}

static void phone_on_reselect(void *arg)
{
    PokegearPhoneAppData *ph = arg;

    list_set_cursor(&ph->list, 0xFF, TRUE);
}

static void phone_leave_zone(PokegearPhoneAppData *ph)
{
    ph->pokegear->cursorInAppSwitchZone = 0;
    list_set_cursor(&ph->list, 0xFF, TRUE);
    PokegearCursorManager_SetCursorSpritesDrawState(ph->pokegear->cursorManager, 0, FALSE);
}

static BOOL phone_setup(PokegearPhoneAppData *ph)
{
    switch (ph->subsub) {
    case 0:
        phone_init_bgs(ph);
        phone_load_graphics(ph);
        phone_add_windows(ph);
        phone_load_text(ph);
        PokegearApp_CreateSpriteManager(ph->pokegear, GEAR_APP_PHONE);
        break;
    case 1:
        phone_make_sprites(ph);
        call_create(ph);
        book_load(ph);
        phone_pick_contacts(ph);
        list_init(ph);
        list_draw_page(ph, 0, 0);
        if (ph->pokegear->cursorInAppSwitchZone == 0) {
            PokegearCursorManager_SetCursorSpritesDrawState(ph->pokegear->cursorManager, 0, FALSE);
        } else {
            PokegearCursorManager_SetCursorSpritesDrawState(ph->pokegear->cursorManager, 0, TRUE);
            PokegearCursorManager_SetSpecIndexAndCursorPos(ph->pokegear->cursorManager, 0, PokegearApp_AppIdToButtonIndex(ph->pokegear));
            list_set_cursor(&ph->list, 0xFF, FALSE);
        }
        ph->pokegear->reselectAppCB = phone_on_reselect;
        ph->subsub = 0;
        return TRUE;
    }
    ph->subsub++;
    return FALSE;
}

static BOOL phone_teardown(PokegearPhoneAppData *ph)
{
    if (ph->menu != NULL)
        PokegearMenu_Close(ph->menu);
    ph->menu = NULL;
    call_destroy(ph);
    phone_delete_sprites(ph);
    PokegearApp_DestroySpriteManager(ph->pokegear);
    phone_free_text(ph);
    phone_remove_windows(ph);
    phone_unload_graphics(ph);
    phone_exit_bgs(ph);
    if (ph->book != NULL)
        Heap_Free(ph->book);
    ph->book = NULL;
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

/* PokegearPhone_HandleKeyInput_ContactList */
static int phone_keys(PokegearPhoneAppData *ph)
{
    int r;

    if ((gSystem.pressedKeys & PAD_BUTTON_B) && ph->menuInputStateBak == 0) {
        ph->pokegear->cursorInAppSwitchZone = 1;
        PokegearApp_PlaySE(PG_SE_CANCEL);
        PokegearCursorManager_SetCursorSpritesDrawState(ph->pokegear->cursorManager, 0, TRUE);
        PokegearCursorManager_SetSpecIndexAndCursorPos(ph->pokegear->cursorManager, 0, PokegearApp_AppIdToButtonIndex(ph->pokegear));
        list_set_cursor(&ph->list, 0xFF, FALSE);
        return -1;
    }
    r = list_keys(ph);
    if (r >= 0) {
        ph->callerID = ph->order[r];
        PokegearApp_PlaySE(PG_SE_DECIDE);
        list_show_cursor(&ph->list, FALSE);
        ph->menu = phone_menu_open(ph, sMenuCallSortQuit, 3, 13, 9, 16);
        phone_tooltip(ph, PH_TIP_NEXT_SEARCH, TRUE);
        return 8;
    }
    return -1;
}

/* PokegearPhone_HandleTouchInput */
static int phone_touch(PokegearPhoneAppData *ph)
{
    int r;

    if (ph->menuInputStateBak == 0) {
        r = PokegearApp_HandleTouchInput_SwitchApps(ph->pokegear);
        if (r != TOUCH_MENU_NO_INPUT) {
            ph->pokegear->menuInputState = MENU_INPUT_STATE_TOUCH;
            return r;
        }
    }
    r = list_touch(ph);
    if (r >= 0) {
        if (ph->pokegear->cursorInAppSwitchZone == 1)
            phone_leave_zone(ph);
        if (r == 0)
            return -1;
        ph->callerID = ph->order[r - 1];
        PokegearApp_PlaySE(PG_SE_DECIDE);
        list_show_cursor(&ph->list, FALSE);
        ph->menu = phone_menu_open(ph, sMenuCallSortQuit, 3, 13, 9, 16);
        phone_tooltip(ph, PH_TIP_NEXT_SEARCH, TRUE);
        ph->pokegear->menuInputState = MENU_INPUT_STATE_TOUCH;
        return 8;
    }
    return -1;
}

static int phone_input(PokegearPhoneAppData *ph)
{
    int input = phone_touch(ph);

    if (input == TOUCH_MENU_NO_INPUT) {
        if (ph->menuInputStateBak == 0)
            PokegearApp_HandleInputModeChangeToButtons(ph->pokegear);
        if (ph->pokegear->cursorInAppSwitchZone == 1)
            input = PokegearApp_HandleKeyInput_SwitchApps(ph->pokegear);
        else
            input = phone_keys(ph);
    }
    switch (input) {
    case TOUCH_MENU_NO_INPUT:
        break;
    case GEAR_RETURN_4:
        ph->pokegear->appReturnCode = input;
        return PHS_FADE_GEAR_CLOSE;
    case 8:
        return PHS_CONTEXT_MENU;
    default:
        ph->pokegear->appReturnCode = input;
        return PHS_WIPE_SWITCH_APP;
    }
    return PHS_INPUT;
}

static void phone_menu_close(PokegearPhoneAppData *ph)
{
    if (ph->menu != NULL) {
        ph->pokegear->menuInputState = PokegearMenu_LastInputWasTouch(ph->menu) ? MENU_INPUT_STATE_TOUCH : MENU_INPUT_STATE_BUTTONS;
        PokegearMenu_Close(ph->menu);
    }
    ph->menu = NULL;
}

/* PokegearPhone_HandleSubmenuInput: Call, Sort, Quit. */
static int phone_context_menu(PokegearPhoneAppData *ph)
{
    int r = ph->menu != NULL ? PokegearMenu_Input(ph->menu) : -2;

    if (r == -1)
        return PHS_CONTEXT_MENU;
    phone_menu_close(ph);
    if (r == 1) {
        ph->menu = phone_menu_open(ph, sMenuSorts, 4, 13, 5, 16);
        ph->subsub = 0;
        phone_tooltip(ph, PH_TIP_SORT_TITLE, TRUE);
        return PHS_SORT_MENU;
    }
    phone_tooltip(ph, 0, FALSE);
    if (r == 0)
        return PHS_DIM_BEFORE_CALL;
    phone_return_to_list(ph);
    return PHS_INPUT;
}

/* PokegearPhone_HandleSortMenuInput: Title, Alphabet, Location, Quit. The
 * tooltip names the sort under the cursor, as HeartGold's callback does. */
static int phone_sort_menu(PokegearPhoneAppData *ph)
{
    int r = ph->menu != NULL ? PokegearMenu_Input(ph->menu) : -2;

    if (r == -1) {
        int row = PokegearMenu_Cursor(ph->menu);
        static const u8 tips[4] = { PH_TIP_SORT_TITLE, PH_TIP_SORT_ALPHABET, PH_TIP_SORT_LOCATION, PH_TIP_SORT_FINISH };

        if (row >= 0 && row < 4 && row != ph->subsub) {
            ph->subsub = row;
            phone_tooltip(ph, tips[row], TRUE);
        }
        return PHS_SORT_MENU;
    }
    ph->subsub = 0;
    phone_menu_close(ph);
    if (r >= 0 && r <= 2)
        phone_sort(ph, (u8)r);
    phone_tooltip(ph, 0, FALSE);
    phone_return_to_list(ph);
    return PHS_INPUT;
}

/* ------------------------------------------------------------------ */
/* Fades                                                               */
/* ------------------------------------------------------------------ */

static int phone_fade(PokegearPhoneAppData *ph, int in, int app)
{
    PaletteData *pd = ph->pokegear->plttData;
    int i;

    switch (ph->substate) {
    case 0:
        if (app) {
            PaletteData_SetAutoTransparent(pd, TRUE);
            ph->pokegear->fadeCounter = 0;
            if (in) {
                G2_SetBlendBrightness(GX_BLEND_PLANEMASK_BG1 | GX_BLEND_PLANEMASK_BG2 | GX_BLEND_PLANEMASK_BG3, 0);
                for (i = 0; i < 3; i++) {
                    Bg_ToggleLayer((u8)(i + BG_LAYER_MAIN_1), 1);
                    Bg_ToggleLayer((u8)(i + BG_LAYER_SUB_1), 1);
                }
            }
        } else {
            StartScreenFade(FADE_BOTH_SCREENS, in ? FADE_TYPE_BRIGHTNESS_IN : FADE_TYPE_BRIGHTNESS_OUT,
                            in ? FADE_TYPE_BRIGHTNESS_IN : FADE_TYPE_BRIGHTNESS_OUT, COLOR_BLACK, 6, 1, ph->heapID);
            if (in) {
                phone_dim(ph, FALSE);
                for (i = 0; i < 8; i++)
                    Bg_ToggleLayer((u8)i, 1);
                GXLayers_EngineAToggleLayers(GX_PLANEMASK_OBJ, 1);
                GXLayers_EngineBToggleLayers(GX_PLANEMASK_OBJ, 1);
            }
        }
        ph->substate++;
        break;
    case 1:
        if (app) {
            if (!Pokegear_RunFadeLayers123(ph->pokegear, in ? 0 : 1))
                break;
            if (!in) {
                PaletteData_Blend(pd, PLTTBUF_MAIN_BG, 0, 0xE0, 16, COLOR_BLACK);
                PaletteData_Blend(pd, PLTTBUF_MAIN_OBJ, 0x40, 0xC0, 16, COLOR_BLACK);
                PaletteData_CommitFadedBuffers(pd);
                for (i = 0; i < 3; i++) {
                    Bg_ToggleLayer((u8)(i + BG_LAYER_MAIN_1), 0);
                    Bg_ToggleLayer((u8)(i + BG_LAYER_SUB_1), 0);
                }
            }
            PaletteData_SetAutoTransparent(pd, FALSE);
            ph->pokegear->fadeCounter = 0;
        } else {
            if (!IsScreenFadeDone())
                break;
            if (!in)
                for (i = 0; i < 8; i++)
                    Bg_ToggleLayer((u8)i, 0);
        }
        ph->substate = 0;
        return in ? PHS_INPUT : PHS_TEARDOWN;
    }
    return app ? (in ? PHS_WIPE_IN : PHS_WIPE_SWITCH_APP) : (in ? PHS_FADE_IN : PHS_FADE_GEAR_CLOSE);
}

/* ------------------------------------------------------------------ */
/* The card                                                            */
/* ------------------------------------------------------------------ */

BOOL PokegearPhone_Init(ApplicationManager *man, int *state)
{
    PokegearAppData *app = ApplicationManager_Args(man);
    PokegearPhoneAppData *ph;

    (void)state;
    Heap_Create(HEAP_ID_APPLICATION, POKEGEAR_CARD_HEAP, 0x30000);
    ph = ApplicationManager_NewData(man, sizeof *ph, POKEGEAR_CARD_HEAP);
    memset(ph, 0, sizeof *ph);
    ph->pokegear = app;
    ph->heapID = POKEGEAR_CARD_HEAP;
    ph->skin = app->skin;
    app->childAppdata = ph;
    app->reselectAppCB = NULL;
    app->deselectAppCB = NULL;
    Sound_SetSceneAndPlayBGM(SOUND_SCENE_SUB_55, SEQ_NONE, 0);
    return TRUE;
}

BOOL PokegearPhone_Main(ApplicationManager *man, int *state)
{
    PokegearPhoneAppData *ph = ApplicationManager_Data(man);

    switch (*state) {
    case PHS_SETUP:
        if (phone_setup(ph))
            *state = ph->pokegear->isSwitchApp ? PHS_WIPE_IN : PHS_FADE_IN;
        break;
    case PHS_INPUT:
        *state = phone_input(ph);
        break;
    case PHS_TEARDOWN:
        if (phone_teardown(ph))
            *state = PHS_QUIT;
        break;
    case PHS_CONTEXT_MENU:
        *state = phone_context_menu(ph);
        break;
    case PHS_SORT_MENU:
        *state = phone_sort_menu(ph);
        break;
    case PHS_DIM_BEFORE_CALL:
        phone_dim(ph, TRUE);
        *state = PHS_SETUP_CALL;
        break;
    case PHS_SETUP_CALL:
        call_begin(ph, ph->callerID);
        RenderControlFlags_SetSpeedUpOnTouch(TRUE);
        printf("openmmo: pokegear: calling contact %d (script %d, handler %d)\n", ph->callerID, ph->call.scriptID, ph->call.scriptType);
        *state = PHS_PLAY_CALL;
        break;
    case PHS_PLAY_CALL:
        if (call_main(&ph->call)) {
            call_end(ph);
            *state = PHS_INPUT;
        }
        break;
    case PHS_FADE_IN:
        *state = phone_fade(ph, 1, 0);
        break;
    case PHS_FADE_GEAR_CLOSE:
        *state = phone_fade(ph, 0, 0);
        break;
    case PHS_WIPE_IN:
        *state = phone_fade(ph, 1, 1);
        break;
    case PHS_WIPE_SWITCH_APP:
        *state = phone_fade(ph, 0, 1);
        break;
    case PHS_QUIT:
        return TRUE;
    }
    return FALSE;
}

BOOL PokegearPhone_Exit(ApplicationManager *man, int *state)
{
    PokegearPhoneAppData *ph = ApplicationManager_Data(man);

    (void)state;
    ph->pokegear->reselectAppCB = NULL;
    ph->pokegear->deselectAppCB = NULL;
    ph->pokegear->isSwitchApp = TRUE;
    ApplicationManager_FreeData(man);
    Heap_Destroy(POKEGEAR_CARD_HEAP);
    return TRUE;
}
