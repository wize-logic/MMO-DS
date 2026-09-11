/*
 * HeartGold's Pokegear on this engine: the shape the shell and its four
 * cards share.
 */
#ifndef OPENMMO_POKEGEAR_H
#define OPENMMO_POKEGEAR_H

#include <nitro.h>
#include <nnsys.h>

#include "bg_window.h"
#include "constants/heap.h"
#include "field/field_system.h"
#include "message.h"
#include "narc.h"
#include "overlay_manager.h"
#include "palette.h"
#include "savedata.h"
#include "sprite_system.h"
#include "string_gf.h"
#include "string_template.h"

/* HeartGold's two heaps: the shell's (HEAP_ID_POKEGEAR, 0x32000) and the
 * running card's (HEAP_ID_POKEGEAR_APP, up to 0x40000). Two of this
 * engine's unnamed ids that no application of its own creates. */
#define POKEGEAR_HEAP           HEAP_ID_50
#define POKEGEAR_HEAP_SIZE      0x32000
#define POKEGEAR_CARD_HEAP      HEAP_ID_52

/* A ported map's header is 594 + its own HeartGold id (mmo/MAPS). */
#define POKEGEAR_PORTED_FIRST   594
#define POKEGEAR_HG_MAPS        541

/* The five archives, in pokegear.txt `narc` order. */
enum {
    PG_NARC_GEAR = 0,   /* application/pokegear/pgear_gra.narc */
    PG_NARC_MAP,        /* application/pokegear/map/pgmap_gra.narc */
    PG_NARC_CONF,       /* application/pokegear/configure/pgconf_gra.narc */
    PG_NARC_PHONE,      /* application/pokegear/phone/pgphone_gra.narc */
    PG_NARC_RADIO,      /* application/pokegear/radio/pgradio_gra.narc */
    PG_NARC_ENC,        /* fielddata/encountdata/g_enc_data.narc, for the radio's talk */
    PG_NARC_N
};

/* The cards, HeartGold's own numbering (PokegearAppId). */
enum {
    GEAR_APP_CONFIGURE = 0,
    GEAR_APP_RADIO,
    GEAR_APP_MAP,
    GEAR_APP_PHONE,
    GEAR_APP_CANCEL,
    GEAR_APP_NO_INPUT = -1
};

/* What a card hands back to the shell (PokegearReturnCode). */
enum {
    GEAR_RETURN_CONFIGURE = 0,
    GEAR_RETURN_RADIO,
    GEAR_RETURN_MAP,
    GEAR_RETURN_PHONE,
    GEAR_RETURN_4,      /* close the device */
    GEAR_RETURN_5,      /* the map chose a fly destination */
    GEAR_RETURN_CANCEL,
    GEAR_RETURN_7,      /* the map's marking mode (not carried) */
    GEAR_RETURN_8       /* a context menu is up */
};

/* GEARCARD_* bits of registeredCards. */
#define GEARCARD_PHONE  0
#define GEARCARD_MAP    1
#define GEARCARD_RADIO  2

#define POKEGEAR_REGION_KANTO   0
#define POKEGEAR_REGION_INDIGO  1
#define POKEGEAR_REGION_JOHTO   2

enum {
    MENU_INPUT_STATE_BUTTONS = 0,
    MENU_INPUT_STATE_TOUCH
};

#define TOUCH_MENU_NO_INPUT (-1)

/* HeartGold's PokegearArgs, filled by the field before the shell starts
 * (FieldSystem_InitPokegearArgs). Coordinates are HeartGold's shared
 * overworld cells (47x17), which is what its map is drawn in; a ported
 * matrix is a cut of that plane and the package says where the cut is. */
typedef struct PokegearArgs {
    u8 isScriptedLaunch;
    u8 menuInputState;
    u16 mapMusicID;
    u8 playerGender;
    u8 onMainMatrix;        /* the player stands on a region's overworld */
    u8 region;              /* 0 johto, 1 kanto, of the map opened from */
    u8 ported;              /* the map opened from is HeartGold's */
    int x;                  /* the player's tile on their own matrix */
    int z;
    u16 mapID;              /* this engine's header */
    u16 mapHeader;
    BOOL setFlyDestination;
    int mapCursorX;         /* HeartGold cells, where the fly was chosen */
    int mapCursorY;
    u16 selectedFlyDest;    /* the fly point's own header (ours) */
    u8 matrixXCoord;        /* the player's HeartGold cell */
    u8 matrixYCoord;
    SaveData *saveData;
    FieldSystem *fieldSystem;
} PokegearArgs;

typedef struct Coord2S16 {
    s16 x;
    s16 y;
} Coord2S16;

/* The cursor's grid: which button is where and which is next in each of
 * the four directions (PokegearCursorGrid). */
typedef struct PokegearCursorGrid {
    u16 appId;
    u8 buttonLeft;
    u8 buttonRight;
    u8 buttonUp;
    u8 buttonDown;
    u8 x;
    u8 y;
    s8 leftOffset;
    s8 rightOffset;
    s8 topOffset;
    s8 bottomOffset;
} PokegearCursorGrid;

typedef struct PokegearCursor {
    u8 active : 1;
    u8 buttonsAre4Tiles : 1;
    u8 cursorPos;
    u8 count;
    u8 lastIndex;
    PokegearCursorGrid *grid;
    Sprite *cursorSprites[4];
} PokegearCursor;

typedef struct PokegearCursorManager {
    u16 count;
    u16 activeCursorIndex;
    PokegearCursor *cursors;
    PokegearCursor *lastCursor;
} PokegearCursorManager;

/* A sprite the map moves in bulk (PokegearManagedObject). */
typedef struct PokegearManagedObject {
    u8 active;
    u8 autoCull;
    u16 autoUpdateDisabled;
    Coord2S16 pos;
    s16 destX;
    s16 destY;
    u16 cellX;      /* unk_0C: the cell this stands on */
    u16 cellY;      /* unk_0E */
    fx32 fixX;      /* unk_10 */
    fx32 fixY;      /* unk_14 */
    fx32 stepX;     /* unk_18 */
    fx32 stepY;     /* unk_1C */
    Sprite *sprite;
    ManagedSprite *managed;
} PokegearManagedObject;

typedef struct PokegearObjectsManager {
    u16 max;
    u16 num;
    PokegearManagedObject *objects;
} PokegearObjectsManager;

/* This engine's shape of an unmanaged sprite off a card's resource set
 * (UnmanagedSpriteTemplate): the set is the char/cell/anim resource id the
 * card loaded, `pal` is the OBJ palette as HeartGold numbers it. */
typedef struct PokegearSpriteTemplate {
    int resourceSet;
    s16 x;
    s16 y;
    s16 z;
    u16 animation;
    int drawPriority;
    int pal;
    int vram;       /* NNS_G2D_VRAM_TYPE_2DMAIN or 2DSUB */
} PokegearSpriteTemplate;

typedef struct PokegearAppData PokegearAppData;
struct PokegearAppData {
    enum HeapID heapID;
    u8 app;
    u8 registeredCards;
    u8 isSwitchApp;
    u8 cursorInAppSwitchZone;
    u8 needClockUpdate;
    u8 skin;
    u8 fadeCounter;
    u8 menuInputState;
    u8 menuInputStateBak;
    int substate;
    int appReturnCode;
    PokegearArgs *args;
    SaveData *saveData;
    void (*vblankCB)(PokegearAppData *, void *);
    void (*reselectAppCB)(void *);
    void (*deselectAppCB)(void *);
    void *childAppdata;
    ApplicationManager *childApplication;
    BgConfig *bgConfig;
    PaletteData *plttData;
    PokegearCursorManager *cursorManager;
    RTCTime time;
    SpriteSystem *spriteSystem;
    SpriteManager *spriteManager;   /* the running card's */
    SpriteManager *uiManager;       /* the shell's skin sheet */
    ManagedSprite *uiSprites[11];
    void *buttonsScrnRaw;           /* unk_0C4: pgear_gra 54+skin */
    NNSG2dScreenData *buttonsScrn;  /* unk_0C8 */
    int vramLive;
};

/* ---- the package (openmmo_pokegear.c) ---- */
int openmmo_pokegear_available(void);
NARC *openmmo_pokegear_narc(int which, enum HeapID heapID);
int openmmo_pokegear_bank(int hgBank);
int openmmo_pokegear_seq(const char *name);
int openmmo_pokegear_box(int region, int *x0, int *y0, int *w, int *h);
/* A ported header's row: region (0/1), world-map cell, radio and phone
 * bits, HeartGold encounter member (or -1). 0 when the header is not one. */
int openmmo_pokegear_map(int header, int *region, int *wx, int *wy,
                         int *radio, int *calls, int *enc);

/* ---- the shell (openmmo_pokegear.c) ---- */
void PokegearApp_SetGraphicsBanks(void);
BOOL PokegearApp_HandleInputModeChangeToButtons(PokegearAppData *app);
int PokegearApp_HandleTouchInput_SwitchApps(PokegearAppData *app);
int PokegearApp_HandleKeyInput_SwitchApps(PokegearAppData *app);
BOOL PokegearApp_UpdateClockSprites(PokegearAppData *app, BOOL force);
void Pokegear_ClearAppBgLayers(PokegearAppData *app);
BOOL Pokegear_RunFadeLayers123(PokegearAppData *app, int direction);
u8 PokegearApp_AppIdToButtonIndex(PokegearAppData *app);
void PokegearApp_LoadSkinGraphics(PokegearAppData *app, u8 skin);
int Pokegear_RegionFromCoords(u16 x, u16 y);
int Pokegear_GetCurrentRegion(PokegearAppData *app);
void PokegearApp_CreateSpriteManager(PokegearAppData *app, int card);
void PokegearApp_DestroySpriteManager(PokegearAppData *app);
Sprite *PokegearApp_CreateSprite(PokegearAppData *app,
                                 const PokegearSpriteTemplate *tmpl);
void PokegearApp_DeleteSprite(PokegearAppData *app, Sprite *sprite);
void PokegearUI_ReloadSkin(PokegearAppData *app, u8 skin);
void PokegearApp_PlaySE(int which);
enum { PG_SE_CURSOR, PG_SE_DECIDE, PG_SE_CANCEL, PG_SE_APPCHANGE, PG_SE_MAPTOUCH,
       PG_SE_ZOOM_IN, PG_SE_ZOOM_OUT, PG_SE_YBUTTON, PG_SE_RING, PG_SE_HANGUP };

/* ---- the cursor manager ---- */
PokegearCursorManager *PokegearCursorManager_Alloc(int count, enum HeapID heapID);
void PokegearCursorManager_Free(PokegearCursorManager *m);
u16 PokegearCursorManager_AddButtons(PokegearCursorManager *m,
                                     const PokegearCursorGrid *spec, u8 n,
                                     u8 cursorPos, enum HeapID heapID,
                                     Sprite *s1, Sprite *s2, Sprite *s3, Sprite *s4);
BOOL PokegearCursorManager_RemoveCursor(PokegearCursorManager *m, u16 index);
u16 PokegearCursorManager_SetCursorSpritesDrawState(PokegearCursorManager *m, u16 index, BOOL draw);
u16 PokegearCursorManager_SetSpecIndexAndCursorPos(PokegearCursorManager *m, u16 index, u8 cursorPos);
u8 PokegearCursorManager_GetCursorPos(PokegearCursorManager *m);
u8 PokegearCursorManager_MoveActiveCursor(PokegearCursorManager *m, u8 move);
u8 PokegearCursorManager_SetActiveCursorPosition(PokegearCursorManager *m, u8 newIndex);
void PokegearCursorManager_SetCursorSpritesAnimateFlag(PokegearCursorManager *m, u16 index, BOOL active);

/* ---- the objects manager ---- */
PokegearObjectsManager *PokegearObjectsManager_Create(int count, enum HeapID heapID);
void PokegearObjectsManager_Release(PokegearAppData *app, PokegearObjectsManager *mgr);
void PokegearObjectsManager_UpdateAllSpritesPos(PokegearObjectsManager *mgr);
u16 PokegearObjectsManager_AppendSprite(PokegearObjectsManager *mgr, Sprite *sprite);
void PokegearObjectsManager_Reset(PokegearAppData *app, PokegearObjectsManager *mgr);
void PokegearObjectsManager_DeleteSpritesFromIndexToEnd(PokegearAppData *app, PokegearObjectsManager *mgr, u8 first);

static inline void PokegearManagedObject_SetCoordUpdateSprite(PokegearManagedObject *obj, s16 x, s16 y)
{
    obj->pos.x = x;
    obj->pos.y = y;
    Sprite_SetPositionXY(obj->sprite, obj->pos.x, obj->pos.y);
}

static inline void PokegearManagedObject_SetCoord(PokegearManagedObject *obj, s16 x, s16 y)
{
    obj->pos.x = x;
    obj->pos.y = y;
}

static inline void PokegearManagedObject_SetCell(PokegearManagedObject *obj, s16 x, s16 y)
{
    obj->cellX = x;
    obj->cellY = y;
}

static inline void PokegearManagedObject_AddCoord(PokegearManagedObject *obj, s16 x, s16 y)
{
    obj->pos.x += x;
    obj->pos.y += y;
}

static inline void PokegearManagedObject_SetAutoCull(PokegearManagedObject *obj, BOOL on)
{
    obj->autoCull = on;
}

static inline void PokegearManagedObject_SetFixCoords(PokegearManagedObject *obj, s16 x, s16 y)
{
    obj->fixX = FX32_CONST(x);
    obj->fixY = FX32_CONST(y);
}

/* ---- the cards ---- */
BOOL PokegearMap_Init(ApplicationManager *man, int *state);
BOOL PokegearMap_Main(ApplicationManager *man, int *state);
BOOL PokegearMap_Exit(ApplicationManager *man, int *state);

BOOL PokegearConfigure_Init(ApplicationManager *man, int *state);
BOOL PokegearConfigure_Main(ApplicationManager *man, int *state);
BOOL PokegearConfigure_Exit(ApplicationManager *man, int *state);

BOOL PokegearRadio_Init(ApplicationManager *man, int *state);
BOOL PokegearRadio_Main(ApplicationManager *man, int *state);
BOOL PokegearRadio_Exit(ApplicationManager *man, int *state);

BOOL PokegearPhone_Init(ApplicationManager *man, int *state);
BOOL PokegearPhone_Main(ApplicationManager *man, int *state);
BOOL PokegearPhone_Exit(ApplicationManager *man, int *state);

/* A message bank of HeartGold's, by its own number, opened through the
 * package's appended member. NULL when the package has no such bank. */
MessageLoader *openmmo_pokegear_msg(int hgBank, enum HeapID heapID);

/* Where a ported header's name lives: this game's location-name bank at the
 * label its cooked header names. */
u32 openmmo_pokegear_label(int header);

/* The session's memory of the device (HeartGold's SavePokegear, which no
 * block of this client seats). */
u8 openmmo_pokegear_last_app(void);
void openmmo_pokegear_set_last_app(u8 app);
u8 openmmo_pokegear_skin(void);
void openmmo_pokegear_set_skin(u8 skin);
u8 openmmo_pokegear_zoomed(void);
void openmmo_pokegear_set_zoomed(u8 zoomed);
void openmmo_pokegear_radio_cursor(s16 *x, s16 *y);
void openmmo_pokegear_set_radio_cursor(s16 x, s16 y);

/* The radio's own sound player (HeartGold's PLAYER_RADIO). */
BOOL SndRadio_StartSeq(int seqNo);
void SndRadio_StopSeq(int fadeFrames);
int SndRadio_CountPlayingSeq(void);

#endif /* OPENMMO_POKEGEAR_H */

/* ---- the popup (openmmo_pokegear_menu.c) ----
 * HeartGold's TouchscreenListMenu: a two-or-three row list in a frame,
 * keys and pen both. On this engine's ListMenu. */
typedef struct PokegearMenu PokegearMenu;
PokegearMenu *PokegearMenu_Open(PokegearAppData *app, u8 bgLayer, u8 x, u8 y,
                                u8 width, u16 baseTile, u8 palette,
                                MessageLoader *msg, const u16 *entries, int n,
                                enum HeapID heapID);
int PokegearMenu_Input(PokegearMenu *m); /* -1 nothing, -2 cancel, else the row */
int PokegearMenu_LastInputWasTouch(PokegearMenu *m);
int PokegearMenu_Cursor(PokegearMenu *m);
void PokegearMenu_Close(PokegearMenu *m);
u8 PokegearMenu_X(PokegearMenu *m);
u8 PokegearMenu_Y(PokegearMenu *m);
u8 PokegearMenu_Width(PokegearMenu *m);

/* Fields the start menu's patched entry and exit need. */
PokegearArgs *openmmo_pokegear_launch(FieldSystem *fs);
int openmmo_pokegear_fly_wanted(const PokegearArgs *args, void **taskFn, void **taskData);
void openmmo_pokegear_free_args(PokegearArgs *args);
