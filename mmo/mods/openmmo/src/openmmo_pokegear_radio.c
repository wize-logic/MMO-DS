/*
 * The Pokegear's radio card: the dial, the stations, and the
 * twelve shows that talk over their music.
 */

#include <stdio.h>
#include <string.h>

#include "openmmo_pokegear.h"
#include "openmmo_pokegear_tables.h"

#include "constants/charcode.h"
#include "constants/graphics.h"
#include "font.h"
#include "generated/fade_types.h"
#include "graphics.h"
#include "gx_layers.h"
#include "heap.h"
#include "math_util.h"
#include "pokedex.h"
#include "render_text.h"
#include "rtc.h"
#include "screen_fade.h"
#include "sound.h"
#include "sound_playback.h"
#include "system.h"
#include "text.h"
#include "touch_screen.h"

/* pgradio_gra members. */
#define PGRADIO_OBJ_PLTT   0
#define PGRADIO_SUB_PLTT   4    /* + skin */
#define PGRADIO_MAIN_PLTT  10   /* + skin */
#define PGRADIO_MAIN_CHAR  16   /* + skin, MAIN_2 */
#define PGRADIO_DIAL_SCRN  22   /* + skin, MAIN_2 and the buttons' states */
#define PGRADIO_MAIN3_SCRN 28   /* + skin */
#define PGRADIO_SUB_CHAR   34   /* + skin, SUB_3 */
#define PGRADIO_SUB_SCRN   40   /* + skin */

enum {
    RADIO_SEL_JOHTO = 0,
    RADIO_SEL_KANTO,
    RADIO_SEL_KANTO_EXPN,
    RADIO_SEL_NO_SIGNAL,
    RADIO_SEL_ALPH,
    RADIO_SEL_ROCKET,
    RADIO_SEL_MAHOGANY
};

enum {
    RADIO_MAIN_INIT = 0,
    RADIO_MAIN_INPUT,
    RADIO_MAIN_UNLOAD,
    RADIO_MAIN_FADE_IN,
    RADIO_MAIN_FADE_OUT,
    RADIO_MAIN_FADE_IN_APP,
    RADIO_MAIN_FADE_OUT_APP,
    RADIO_MAIN_QUIT
};

/* The shows (constants/radio_station.h). */
enum {
    RADIO_STATION_POKEMON_MUSIC = 0,
    RADIO_STATION_POKEMON_TALK,
    RADIO_STATION_POKEMON_SEARCH_PARTY,
    RADIO_STATION_SERIAL_RADIO_DRAMA,
    RADIO_STATION_BUENAS_PASSWORD,
    RADIO_STATION_TRAINER_PROFILES,
    RADIO_STATION_THAT_TOWN_THESE_PEOPLE,
    RADIO_STATION_POKE_FLUTE,
    RADIO_STATION_UNOWN,
    RADIO_STATION_TEAM_ROCKET,
    RADIO_STATION_MAHOGANY_SIGNAL,
    RADIO_STATION_COMMERCIALS,
    RADIO_STATION_N
};

/* Their text banks. */
#define MSG_MAHOGANY   409
#define MSG_UNOWN      410
#define MSG_BUENA      411
#define MSG_COMMERCIAL 412
#define MSG_DRAMA      413
#define MSG_TALK       414
#define MSG_TOWN       415
#define MSG_MUSIC      416
#define MSG_FLUTE      417
#define MSG_ROCKET     418
#define MSG_SEARCH     419
#define MSG_PROFILES   420
#define MSG_RADIO_TOWER_2F 66

enum {
    RADIO_PRINT_NULL = 0,
    RADIO_PRINT_WAIT_FIRST_LINE,
    RADIO_PRINT_NEXT_LINE,
    RADIO_PRINT_WAIT_SCROLL,
    RADIO_PRINT_WAIT_SCROLL_EXIT,
    RADIO_PRINT_WAIT_EXIT,
    RADIO_PRINT_EXIT
};

typedef struct RadioShow {
    enum HeapID heapID;
    SaveData *saveData;
    u16 mapID;
    Window *scriptWindow;
    Window *titleWindow;
    Window *hostWindow;
    u32 textColor;
    void *showData;
    MessageLoader *showMsg;
    RTCDate date;
    RTCTime time;
    StringTemplate *fmt;
    String *curLineStr;
    String *showTitle;
    String *showHost;
    u32 lastEpisodeID;
    u8 nextStation;
    u8 curStation;
    u8 lastStation;
    u8 bgColor;
    u8 runState;
    u8 printWithJingleState;
    u8 curLineIdx;
    u8 numLines;
    u8 printState;
    u8 textNoScroll;
    u8 isSecondLine : 1;
    u8 statik : 1;
    u8 regionNo : 1;
    u8 isPlayingJingle : 1;
    u8 triggerCommercials : 1;
    u8 delayCounter;
    u8 delayFrames;
    u8 scrollCounter;
    u8 scrollFrames;
    String *msgbufFormatted;
    String *msgbufRaw;
} RadioShow;

typedef struct {
    enum HeapID heapID;
    int state;
    int substate;
    PokegearAppData *pokegear;
    Sprite *sprites[5];
    u8 isDraggingCursor;
    u8 selectedButton;
    u8 skin;
    u8 stationSelection;
    u8 signalStrength;
    u8 stationActive;
    u8 station;
    s16 cursorX;
    s16 cursorY;
    u16 windowScrollStep;
    u8 windowScrollFinished;
    Window windows[3];
    RadioShow *show;
    void *scrnRaw;
    NNSG2dScreenData *scrn;
} PokegearRadioAppData;

/* ---- the dial (overlay_101_021F4F34.c) ---- */

static const TouchScreenRect sTuningArea = { .circle = { TOUCHSCREEN_USE_CIRCLE, 128, 92, 52 } };

static const TouchScreenRect sTuning_Johto[] = {
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 112, 76, 4 } },
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 112, 76, 16 } },
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 152, 76, 4 } },
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 152, 76, 20 } },
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 96, 108, 4 } },
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 96, 108, 16 } },
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 136, 116, 4 } },
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 136, 116, 20 } },
    { .rect = { TOUCHSCREEN_TABLE_TERMINATOR, 0, 0, 0 } },
};
static const TouchScreenRect sTuning_Kanto[] = {
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 112, 76, 4 } },
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 112, 76, 16 } },
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 152, 76, 4 } },
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 152, 76, 20 } },
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 96, 108, 4 } },
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 96, 108, 16 } },
    { .rect = { TOUCHSCREEN_TABLE_TERMINATOR, 0, 0, 0 } },
};
static const TouchScreenRect sTuning_KantoEXPN[] = {
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 112, 76, 4 } },
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 112, 76, 16 } },
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 152, 76, 4 } },
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 152, 76, 20 } },
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 96, 108, 4 } },
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 96, 108, 16 } },
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 136, 116, 4 } },
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 136, 116, 20 } },
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 128, 48, 4 } },
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 128, 48, 8 } },
    { .rect = { TOUCHSCREEN_TABLE_TERMINATOR, 0, 0, 0 } },
};
static const TouchScreenRect sTuning_NoSignal[] = {
    { .rect = { TOUCHSCREEN_TABLE_TERMINATOR, 0, 0, 0 } },
};
static const TouchScreenRect sTuning_RocketMahogany[] = {
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 128, 92, 38 } },
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 128, 92, 52 } },
    { .rect = { TOUCHSCREEN_TABLE_TERMINATOR, 0, 0, 0 } },
};
static const TouchScreenRect sTuning_Alph[] = {
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 128, 92, 4 } },
    { .circle = { TOUCHSCREEN_USE_CIRCLE, 128, 92, 16 } },
    { .rect = { TOUCHSCREEN_TABLE_TERMINATOR, 0, 0, 0 } },
};
static const TouchScreenRect *const sTuningHitboxes[] = {
    sTuning_Johto, sTuning_Kanto, sTuning_KantoEXPN, sTuning_NoSignal,
    sTuning_Alph, sTuning_RocketMahogany, sTuning_RocketMahogany,
};
static const TouchScreenRect sTuningButtonHitboxes[] = {
    { .rect = { 48, 76, 16, 48 } },
    { .rect = { 48, 76, 208, 240 } },
    { .rect = { 112, 140, 16, 48 } },
    { .rect = { 112, 140, 208, 240 } },
    { .rect = { TOUCHSCREEN_TABLE_TERMINATOR, 0, 0, 0 } },
};

/* The Ruins of Alph, whose signal is the Unown's. HeartGold's map ids. */
#define sAlphMaps gPokegearAlphMaps

/* ---- shows: forward ---- */
typedef struct RadioFuncs {
    BOOL (*setup)(RadioShow *);
    BOOL (*print)(RadioShow *);
    BOOL (*teardown)(RadioShow *);
} RadioFuncs;

static BOOL Show_Music_Setup(RadioShow *s); static BOOL Show_Music_Print(RadioShow *s); static BOOL Show_Music_Teardown(RadioShow *s);
static BOOL Show_Talk_Setup(RadioShow *s); static BOOL Show_Talk_Print(RadioShow *s); static BOOL Show_Talk_Teardown(RadioShow *s);
static BOOL Show_Search_Setup(RadioShow *s); static BOOL Show_Search_Print(RadioShow *s); static BOOL Show_Search_Teardown(RadioShow *s);
static BOOL Show_Drama_Setup(RadioShow *s); static BOOL Show_Drama_Print(RadioShow *s); static BOOL Show_Drama_Teardown(RadioShow *s);
static BOOL Show_Buena_Setup(RadioShow *s); static BOOL Show_Buena_Print(RadioShow *s); static BOOL Show_Buena_Teardown(RadioShow *s);
static BOOL Show_Profiles_Setup(RadioShow *s); static BOOL Show_Profiles_Print(RadioShow *s); static BOOL Show_Profiles_Teardown(RadioShow *s);
static BOOL Show_Town_Setup(RadioShow *s); static BOOL Show_Town_Print(RadioShow *s); static BOOL Show_Town_Teardown(RadioShow *s);
static BOOL Show_Flute_Setup(RadioShow *s); static BOOL Show_Flute_Print(RadioShow *s); static BOOL Show_Flute_Teardown(RadioShow *s);
static BOOL Show_Unown_Setup(RadioShow *s); static BOOL Show_Unown_Print(RadioShow *s); static BOOL Show_Unown_Teardown(RadioShow *s);
static BOOL Show_Rocket_Setup(RadioShow *s); static BOOL Show_Rocket_Print(RadioShow *s); static BOOL Show_Rocket_Teardown(RadioShow *s);
static BOOL Show_Mahogany_Setup(RadioShow *s); static BOOL Show_Mahogany_Print(RadioShow *s); static BOOL Show_Mahogany_Teardown(RadioShow *s);
static BOOL Show_Commercials_Setup(RadioShow *s); static BOOL Show_Commercials_Print(RadioShow *s); static BOOL Show_Commercials_Teardown(RadioShow *s);

static const RadioFuncs sRadioShowFuncs[RADIO_STATION_N] = {
    { Show_Music_Setup, Show_Music_Print, Show_Music_Teardown },
    { Show_Talk_Setup, Show_Talk_Print, Show_Talk_Teardown },
    { Show_Search_Setup, Show_Search_Print, Show_Search_Teardown },
    { Show_Drama_Setup, Show_Drama_Print, Show_Drama_Teardown },
    { Show_Buena_Setup, Show_Buena_Print, Show_Buena_Teardown },
    { Show_Profiles_Setup, Show_Profiles_Print, Show_Profiles_Teardown },
    { Show_Town_Setup, Show_Town_Print, Show_Town_Teardown },
    { Show_Flute_Setup, Show_Flute_Print, Show_Flute_Teardown },
    { Show_Unown_Setup, Show_Unown_Print, Show_Unown_Teardown },
    { Show_Rocket_Setup, Show_Rocket_Print, Show_Rocket_Teardown },
    { Show_Mahogany_Setup, Show_Mahogany_Print, Show_Mahogany_Teardown },
    { Show_Commercials_Setup, Show_Commercials_Print, Show_Commercials_Teardown },
};

static void radio_seq(const char *name)
{
    int id = openmmo_pokegear_seq(name);

    if (id > 0)
        SndRadio_StartSeq(id);
    else
        printf("openmmo: pokegear: the package carries no %s\n", name);
}

/* ------------------------------------------------------------------ */
/* The show framework (overlay_101_021F57B8.c)                         */
/* ------------------------------------------------------------------ */

static RadioShow *RadioShow_Create(SaveData *saveData, u16 mapID, int regionNo, Window *w1, Window *w2, Window *w3, u32 textColor, enum HeapID heapID)
{
    RadioShow *s = Heap_Alloc(heapID, sizeof *s);

    memset(s, 0, sizeof *s);
    s->saveData = saveData;
    s->mapID = mapID;
    s->regionNo = (u8)(regionNo & 1);
    s->scriptWindow = w1;
    s->titleWindow = w2;
    s->hostWindow = w3;
    s->textColor = textColor;
    s->bgColor = (u8)(textColor & 0xFF);
    s->heapID = heapID;
    s->fmt = StringTemplate_New(8, 51, heapID);
    s->curLineStr = String_Init(51, heapID);
    s->showTitle = String_Init(51, heapID);
    s->showHost = String_Init(51, heapID);
    s->msgbufFormatted = String_Init(1351, heapID);
    s->msgbufRaw = String_Init(1351, heapID);
    return s;
}

static void RadioShow_Delete(RadioShow *s)
{
    String_Free(s->msgbufRaw);
    String_Free(s->msgbufFormatted);
    String_Free(s->showHost);
    String_Free(s->showTitle);
    String_Free(s->curLineStr);
    StringTemplate_Free(s->fmt);
    Heap_Free(s);
}

static u8 RadioShow_TranslateStationID(RadioShow *s, int station)
{
    if (station >= 8)
        station = 0;
    if (s->triggerCommercials) {
        s->triggerCommercials = FALSE;
        return RADIO_STATION_COMMERCIALS;
    }
    GetCurrentDateTime(&s->date, &s->time);
    switch (station) {
    case 0: return RADIO_STATION_POKEMON_MUSIC;
    case 1: return RADIO_STATION_POKEMON_TALK;
    case 2: return (u8)(RADIO_STATION_TRAINER_PROFILES + (s->time.hour % 2));
    case 3: return (u8)(RADIO_STATION_POKEMON_SEARCH_PARTY + (s->time.hour % 3));
    case 4: return RADIO_STATION_POKE_FLUTE;
    case 5: return RADIO_STATION_UNOWN;
    case 6: return RADIO_STATION_TEAM_ROCKET;
    case 7: return RADIO_STATION_MAHOGANY_SIGNAL;
    }
    return RADIO_STATION_POKEMON_MUSIC;
}

static void RadioShow_PrintTitleAndHost(RadioShow *s)
{
    Window_FillTilemap(s->titleWindow, 0);
    Window_FillTilemap(s->hostWindow, 0);
    Text_AddPrinterWithParamsAndColor(s->titleWindow, FONT_SYSTEM, s->showTitle, 0, 0, TEXT_SPEED_NO_TRANSFER, TEXT_COLOR(1, 2, 0), NULL);
    Text_AddPrinterWithParamsAndColor(s->hostWindow, FONT_SYSTEM, s->showHost, 0, 0, TEXT_SPEED_NO_TRANSFER, TEXT_COLOR(1, 2, 0), NULL);
    Window_ScheduleCopyToVRAM(s->titleWindow);
    Window_ScheduleCopyToVRAM(s->hostWindow);
}

static void RadioShow_BeginSegment(RadioShow *s, int station, int statik)
{
    s->isSecondLine = FALSE;
    if (station >= 8)
        station = 0;
    s->nextStation = (u8)station;
    if (s->curStation != RADIO_STATION_COMMERCIALS)
        s->lastStation = s->curStation;
    s->curStation = RadioShow_TranslateStationID(s, station);
    s->runState = 0;
    s->delayFrames = 45;
    s->delayCounter = 0;
    s->scrollFrames = 8;
    s->scrollCounter = 0;
    s->statik = (u8)(statik != 0);
    s->isPlayingJingle = FALSE;
    s->printWithJingleState = 0;
    if (s->curStation != RADIO_STATION_COMMERCIALS && s->lastStation != s->curStation)
        s->lastEpisodeID = 0;
    Window_FillTilemap(s->scriptWindow, (u8)((s->bgColor << 4) | s->bgColor));
    Window_CopyToVRAM(s->scriptWindow);
    sRadioShowFuncs[s->curStation].setup(s);
    RadioShow_PrintTitleAndHost(s);
}

static void RadioShow_EndSegment(RadioShow *s)
{
    if (s->showData != NULL)
        sRadioShowFuncs[s->curStation].teardown(s);
    Window_FillTilemap(s->scriptWindow, (u8)((s->bgColor << 4) | s->bgColor));
    Window_CopyToVRAM(s->scriptWindow);
    s->triggerCommercials = FALSE;
}

static void RadioShow_SetStaticLevel(RadioShow *s, BOOL statik)
{
    s->statik = (u8)(statik != 0);
}

static BOOL RadioShow_DelayAndScrollLine(RadioShow *s)
{
    if (s->delayCounter < s->delayFrames) {
        s->delayCounter++;
        return FALSE;
    }
    if (s->scrollCounter) {
        Window_Scroll(s->scriptWindow, SCROLL_DIRECTION_UP, 2, 0);
        Window_CopyToVRAM(s->scriptWindow);
    }
    if (s->scrollCounter++ < 8)
        return FALSE;
    s->scrollCounter = 0;
    s->delayCounter = 0;
    return TRUE;
}

static BOOL RadioShow_ScrollTextOffWindow(RadioShow *s)
{
    Window_Scroll(s->scriptWindow, SCROLL_DIRECTION_UP, 2, 0);
    Window_CopyToVRAM(s->scriptWindow);
    if (s->scrollCounter++ < s->scrollFrames)
        return FALSE;
    s->scrollCounter = 0;
    return TRUE;
}

static BOOL RadioShow_Delay(RadioShow *s)
{
    if (s->delayCounter++ < s->delayFrames)
        return FALSE;
    s->delayCounter = 0;
    return TRUE;
}

static void RadioShow_Main(RadioShow *s)
{
    switch (s->runState) {
    case 0:
        s->runState = (u8)sRadioShowFuncs[s->curStation].print(s);
        break;
    case 1:
        sRadioShowFuncs[s->curStation].teardown(s);
        s->scrollFrames = 16;
        s->delayFrames = 15;
        s->runState++;
        break;
    case 2:
        if (RadioShow_ScrollTextOffWindow(s))
            s->runState++;
        break;
    case 3:
        if (RadioShow_Delay(s)) {
            RadioShow_BeginSegment(s, s->nextStation, s->statik);
            s->runState = 0;
        }
        break;
    }
}

/* String_RadioAddStatic: a weak signal eats glyphs. HeartGold's two dot
 * glyphs are its own; this font's ellipsis and dot stand in. */
static u32 glyph_width(charcode_t c)
{
    const TextGlyph *g = Font_TryLoadGlyph(FONT_SYSTEM, c);

    return g != NULL ? g->width : 0;
}

static void String_RadioAddStatic(String *str, u8 level)
{
    u32 w3 = glyph_width(CHAR_ELLIPSIS);
    u32 w1 = glyph_width(CHAR_DOT);
    u32 n = String_Length(str);
    charcode_t *data = (charcode_t *)String_GetData(str);
    u32 i;

    for (i = 0; i < n; i++) {
        u32 w;

        if (data[i] == CHAR_SPACE || data[i] == CHAR_EOS || data[i] >= 0x0100 + 0x0100)
            continue;
        if (((MTRNG_Next() / 256u) % 101) >= level)
            continue;
        w = glyph_width(data[i]);
        if (w >= w3)
            data[i] = CHAR_ELLIPSIS;
        else if (w >= w1)
            data[i] = CHAR_DOT;
    }
}

static void PrintRadioLine(RadioShow *s, String *msg, int y)
{
    if (s->statik)
        String_RadioAddStatic(msg, 70);
    Text_AddPrinterWithParamsAndColor(s->scriptWindow, FONT_SYSTEM, msg, 0, (u32)(y * 16), TEXT_SPEED_NO_TRANSFER, (TextColor)s->textColor, NULL);
}

static BOOL RadioPrintAdvance(RadioShow *s)
{
    if (!s->isSecondLine)
        s->isSecondLine = TRUE;
    String_CopyLineNum(s->curLineStr, s->msgbufFormatted, s->curLineIdx++);
    PrintRadioLine(s, s->curLineStr, 1);
    Window_CopyToVRAM(s->scriptWindow);
    return s->curLineIdx >= s->numLines;
}

static void RadioPrintInit(RadioShow *s, int msgId, int textNoScroll)
{
    s->textNoScroll = (u8)textNoScroll;
    String_Clear(s->msgbufRaw);
    if (s->showMsg != NULL)
        MessageLoader_GetString(s->showMsg, (u32)msgId, s->msgbufRaw);
    StringTemplate_Format(s->fmt, s->msgbufFormatted, s->msgbufRaw);
    s->curLineIdx = 0;
    s->numLines = (u8)String_NumLines(s->msgbufFormatted);
    s->printState = RADIO_PRINT_NULL;
    String_CopyLineNum(s->curLineStr, s->msgbufFormatted, s->curLineIdx++);
    PrintRadioLine(s, s->curLineStr, s->isSecondLine);
    Window_CopyToVRAM(s->scriptWindow);
    if (s->curLineIdx >= s->numLines)
        s->printState = RADIO_PRINT_WAIT_EXIT;
    else if (!s->isSecondLine)
        s->printState = RADIO_PRINT_WAIT_FIRST_LINE;
    else
        s->printState = RADIO_PRINT_WAIT_SCROLL;
}

static void RadioPrintInitEz(RadioShow *s, int msgId)
{
    RadioPrintInit(s, msgId, 0);
}

static void RadioPrintAndPlayJingle(RadioShow *s, int msgId)
{
    RadioPrintInitEz(s, msgId);
    s->textNoScroll = 1;
    s->isPlayingJingle = TRUE;
    SndRadio_StopSeq(0);
    radio_seq("SEQ_GS_RADIO_JINGLE");
}

static BOOL Radio_RunTextPrinter(RadioShow *s)
{
    switch (s->printState) {
    case RADIO_PRINT_NULL:
        break;
    case RADIO_PRINT_NEXT_LINE:
        if (RadioPrintAdvance(s))
            s->printState = s->textNoScroll ? RADIO_PRINT_EXIT : RADIO_PRINT_WAIT_SCROLL_EXIT;
        else
            s->printState = RADIO_PRINT_WAIT_SCROLL;
        break;
    case RADIO_PRINT_WAIT_SCROLL:
        if (RadioShow_DelayAndScrollLine(s))
            s->printState = RADIO_PRINT_NEXT_LINE;
        break;
    case RADIO_PRINT_WAIT_FIRST_LINE:
        if (RadioShow_Delay(s))
            s->printState = RADIO_PRINT_NEXT_LINE;
        break;
    case RADIO_PRINT_WAIT_SCROLL_EXIT:
        if (RadioShow_DelayAndScrollLine(s))
            s->printState = RADIO_PRINT_EXIT;
        break;
    case RADIO_PRINT_WAIT_EXIT:
        if (!RadioShow_Delay(s))
            break;
        /* fallthrough */
    case RADIO_PRINT_EXIT:
        s->printState = RADIO_PRINT_NULL;
        return TRUE;
    }
    return FALSE;
}

static BOOL Radio_RunTextPrinter_WaitJingle(RadioShow *s)
{
    switch (s->printWithJingleState) {
    case 0:
        if (Radio_RunTextPrinter(s))
            s->printWithJingleState++;
        break;
    case 1:
        if (!SndRadio_CountPlayingSeq()) {
            s->printWithJingleState = 0;
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void show_open_bank(RadioShow *s, int bank, int titleId, int hostId)
{
    s->showMsg = openmmo_pokegear_msg(bank, s->heapID);
    String_Clear(s->showTitle);
    String_Clear(s->showHost);
    if (s->showMsg == NULL)
        return;
    if (titleId >= 0)
        MessageLoader_GetString(s->showMsg, (u32)titleId, s->showTitle);
    if (hostId >= 0)
        MessageLoader_GetString(s->showMsg, (u32)hostId, s->showHost);
}

static void show_close_bank(RadioShow *s)
{
    if (s->showMsg != NULL)
        MessageLoader_Free(s->showMsg);
    s->showMsg = NULL;
}

static void *show_alloc(RadioShow *s, u32 size)
{
    void *d = Heap_Alloc(s->heapID, size);

    memset(d, 0, size);
    s->showData = d;
    return d;
}

static BOOL show_free(RadioShow *s)
{
    show_close_bank(s);
    if (s->showData != NULL)
        Heap_Free(s->showData);
    s->showData = NULL;
    return FALSE;
}

/* A landmark's name into the template: HeartGold's map section, which is
 * this package's location label. */
static void buffer_landmark(RadioShow *s, u32 idx, u16 hgMapId)
{
    u32 label = openmmo_pokegear_label(POKEGEAR_PORTED_FIRST + hgMapId);

    StringTemplate_SetLocationName(s->fmt, idx, label);
}

static void buffer_species(RadioShow *s, u32 idx, u16 species)
{
    charcode_t name[16];
    String *str = String_Init(16, s->heapID);

    MessageLoader_GetSpeciesName(species, s->heapID, name);
    String_CopyChars(str, name);
    StringTemplate_SetString(s->fmt, idx, str, 0, 0, GAME_LANGUAGE);
    String_Free(str);
}

/* ---- Pokemon Music (shows/pokemon_music.c) ---- */

enum { PKMUS_MARCH, PKMUS_LULLABY, PKMUS_HOENN, PKMUS_SINNOH, PKMUS_GBSOUNDS };

typedef struct {
    u8 hasNationalDex;
    u8 playingTrack;
    u8 queuedMsg;
    u8 weekday;
    u8 queuedTrack;
    u8 state;
    RTCDate tuneInDate;
    RTCDate afterIntroDate;
} PokemonMusicData;

static const char *const sMusicSeqs[] = {
    "SEQ_GS_RADIO_MARCH", "SEQ_GS_RADIO_KOMORIUTA", "SEQ_GS_RADIO_R_101", "SEQ_GS_RADIO_R_201",
};

static void Show_Music_Start(RadioShow *s, u8 track)
{
    PokemonMusicData *d = s->showData;

    if (track == PKMUS_GBSOUNDS)
        track = PKMUS_MARCH;
    d->playingTrack = track;
    radio_seq(sMusicSeqs[track]);
}

static BOOL Show_Music_Setup(RadioShow *s)
{
    PokemonMusicData *d = show_alloc(s, sizeof(PokemonMusicData));
    RTCTime dummy;

    d->hasNationalDex = (u8)Pokedex_IsNationalDexObtained(SaveData_GetPokedex(s->saveData));
    GetCurrentDateTime(&d->tuneInDate, &dummy);
    d->weekday = (u8)d->tuneInDate.week;
    d->queuedMsg = (u8)(3 + d->weekday);
    switch (d->weekday) {
    case RTC_WEEK_SUNDAY:
        d->queuedTrack = PKMUS_MARCH;
        d->queuedMsg = 12;
        break;
    case RTC_WEEK_MONDAY:
    case RTC_WEEK_FRIDAY:
        d->queuedTrack = PKMUS_MARCH;
        break;
    case RTC_WEEK_WEDNESDAY:
        if (d->hasNationalDex) {
            d->queuedTrack = PKMUS_HOENN;
            d->queuedMsg = 10;
        } else {
            d->queuedTrack = PKMUS_MARCH;
        }
        break;
    case RTC_WEEK_TUESDAY:
    case RTC_WEEK_SATURDAY:
        d->queuedTrack = PKMUS_LULLABY;
        break;
    case RTC_WEEK_THURSDAY:
        if (d->hasNationalDex) {
            d->queuedTrack = PKMUS_SINNOH;
            d->queuedMsg = 11;
        } else {
            d->queuedTrack = PKMUS_LULLABY;
        }
        break;
    default:
        d->queuedTrack = PKMUS_MARCH;
        break;
    }
    show_open_bank(s, MSG_MUSIC, 0, 1);
    Sound_StopBGM(Sound_GetCurrentBGM(), 0);
    Show_Music_Start(s, d->queuedTrack);
    s->isSecondLine = FALSE;
    return FALSE;
}

static BOOL Show_Music_Teardown(RadioShow *s)
{
    return show_free(s);
}

static BOOL Show_Music_Print(RadioShow *s)
{
    PokemonMusicData *d = s->showData;
    RTCDate date;

    switch (d->state) {
    case 0:
        RadioPrintInitEz(s, 2);
        d->state++;
        break;
    case 1:
        if (Radio_RunTextPrinter(s))
            d->state++;
        break;
    case 2:
        GetCurrentDate(&d->afterIntroDate);
        RadioPrintInitEz(s, d->queuedMsg);
        d->state++;
        break;
    case 3:
        if (Radio_RunTextPrinter(s)) {
            GetCurrentDate(&date);
            if (date.day == d->afterIntroDate.day && date.day == d->tuneInDate.day) {
                d->state = 2;
                return FALSE;
            }
            s->isPlayingJingle = TRUE;
            Sound_StopBGM(Sound_GetCurrentBGM(), 30);
            d->state++;
        }
        break;
    case 4:
        if (RadioShow_DelayAndScrollLine(s))
            return TRUE;
        break;
    }
    return FALSE;
}

/* ---- Pokemon Talk (shows/pokemon_talk.c) ---- */

#define TALK_LANDMARKS_MAX 150
#define TALK_SPECIES_MAX   59

typedef struct {
    u16 state;
    u16 numLandmarks;
    u16 numSpecies;
    u16 numPrioritySpecies;
    u16 landmarkSpeciesPairs[5][2];
    u16 flavorTextPairs[5][2];
    u16 landmarksBuffer[TALK_LANDMARKS_MAX];
    u16 speciesBuffer[TALK_SPECIES_MAX];
    u16 prioritySpeciesBuffer[TALK_SPECIES_MAX];
    u8 selector;
} PokemonTalkData;

/* The places the show never names (sFilterLandmarks): the Square, the
 * contest park, the Ruins' halls, the Ice Path's and Whirl Islands'
 * basements, the hidden room, the Safari's entrance. */
#define sFilterLandmarks gPokegearTalkFilter

static BOOL talk_filtered(u16 hgMapId)
{
    int j;

    for (j = 0; j < (int)(sizeof sFilterLandmarks / sizeof sFilterLandmarks[0]); j++)
        if (sFilterLandmarks[j] == hgMapId)
            return TRUE;
    return FALSE;
}

static BOOL talk_sampled(PokemonTalkData *d, u16 landmark, u8 num)
{
    int i;

    for (i = 0; i < num; i++)
        if (landmark == d->landmarkSpeciesPairs[i][0])
            return TRUE;
    return FALSE;
}

static void talk_add_species(PokemonTalkData *d, const Pokedex *dex, u16 species)
{
    int i;

    if (species == 0 || species > 493 || d->numSpecies >= TALK_SPECIES_MAX)
        return;
    for (i = 0; i < d->numSpecies; i++)
        if (species == d->speciesBuffer[i])
            return;
    d->speciesBuffer[d->numSpecies++] = species;
    if (!Pokedex_HasCaughtSpecies(dex, species) && d->numPrioritySpecies < TALK_SPECIES_MAX)
        d->prioritySpeciesBuffer[d->numPrioritySpecies++] = species;
}

/* HeartGold's encounter record (wild_encounter.h), read straight off the
 * cartridge's archive the package carries. */
static u16 talk_sample_species(PokemonTalkData *d, const Pokedex *dex, int encMember, enum HeapID heapID)
{
    NARC *narc = openmmo_pokegear_narc(PG_NARC_ENC, heapID);
    u8 *rec;
    int i;

    d->numSpecies = 0;
    d->numPrioritySpecies = 0;
    if (narc == NULL || encMember < 0 || (u32)encMember >= narc->numFiles) {
        if (narc != NULL)
            NARC_dtor(narc);
        return 0;
    }
    rec = NARC_AllocAndReadWholeMember(narc, (u32)encMember, heapID);
    NARC_dtor(narc);
    if (rec == NULL)
        return 0;
    if (rec[0] != 0) {
        for (i = 0; i < 12; i++) {
            talk_add_species(d, dex, (u16)(rec[0x08 + 12 + i * 2] | (rec[0x08 + 12 + i * 2 + 1] << 8)));
            talk_add_species(d, dex, (u16)(rec[0x08 + 36 + i * 2] | (rec[0x08 + 36 + i * 2 + 1] << 8)));
            talk_add_species(d, dex, (u16)(rec[0x08 + 60 + i * 2] | (rec[0x08 + 60 + i * 2 + 1] << 8)));
        }
    }
    if (rec[1] != 0)
        for (i = 0; i < 5; i++)
            talk_add_species(d, dex, (u16)(rec[0x64 + i * 4 + 2] | (rec[0x64 + i * 4 + 3] << 8)));
    if (rec[2] != 0)
        for (i = 0; i < 2; i++)
            talk_add_species(d, dex, (u16)(rec[0x78 + i * 4 + 2] | (rec[0x78 + i * 4 + 3] << 8)));
    if (rec[3] != 0)
        for (i = 0; i < 5; i++)
            talk_add_species(d, dex, (u16)(rec[0x80 + i * 4 + 2] | (rec[0x80 + i * 4 + 3] << 8)));
    if (rec[4] != 0)
        for (i = 0; i < 5; i++)
            talk_add_species(d, dex, (u16)(rec[0x94 + i * 4 + 2] | (rec[0x94 + i * 4 + 3] << 8)));
    if (rec[5] != 0)
        for (i = 0; i < 5; i++)
            talk_add_species(d, dex, (u16)(rec[0xA8 + i * 4 + 2] | (rec[0xA8 + i * 4 + 3] << 8)));
    talk_add_species(d, dex, (u16)(rec[0xC0] | (rec[0xC1] << 8)));
    Heap_Free(rec);
    if (d->numSpecies == 0)
        return 0;
    if (d->numPrioritySpecies == 0 || (d->numPrioritySpecies == 1 && (LCRNG_Next() % 1000) < 500))
        return d->speciesBuffer[LCRNG_Next() % d->numSpecies];
    return d->prioritySpeciesBuffer[LCRNG_Next() % d->numPrioritySpecies];
}

static void talk_sample(RadioShow *s, PokemonTalkData *d)
{
    const Pokedex *dex = SaveData_GetPokedex(s->saveData);
    int hg, i;

    d->numLandmarks = 0;
    for (hg = 0; hg < POKEGEAR_HG_MAPS; hg++) {
        int region, enc;

        if (!openmmo_pokegear_map(POKEGEAR_PORTED_FIRST + hg, &region, NULL, NULL, NULL, NULL, &enc))
            continue;
        if (enc < 0 || region != s->regionNo || talk_filtered((u16)hg))
            continue;
        d->landmarksBuffer[d->numLandmarks++] = (u16)hg;
        if (d->numLandmarks >= TALK_LANDMARKS_MAX)
            break;
    }
    if (d->numLandmarks == 0)
        return;
    for (i = 0; i < 5; i++) {
        u16 index, landmark;
        int tries = 0, enc = -1;

        do {
            index = (u16)(LCRNG_Next() % d->numLandmarks);
            landmark = d->landmarksBuffer[index];
        } while (talk_sampled(d, landmark, (u8)i) && ++tries < 64);
        openmmo_pokegear_map(POKEGEAR_PORTED_FIRST + landmark, NULL, NULL, NULL, NULL, NULL, &enc);
        d->landmarkSpeciesPairs[i][0] = landmark;
        d->landmarkSpeciesPairs[i][1] = talk_sample_species(d, dex, enc, s->heapID);
    }
}

static BOOL Show_Talk_Setup(RadioShow *s)
{
    PokemonTalkData *d = show_alloc(s, sizeof(PokemonTalkData));
    int step, i;
    u8 p1, p2;

    show_open_bank(s, MSG_TALK, 0, 1);
    talk_sample(s, d);
    step = ((LCRNG_Next() % 3) + 1) * 2;
    p1 = (u8)(LCRNG_Next() % 13);
    p2 = (u8)(LCRNG_Next() % 13);
    for (i = 0; i < 5; i++) {
        d->flavorTextPairs[i][0] = p1;
        d->flavorTextPairs[i][1] = p2;
        p1 = (u8)((p1 + step) % 13);
        p2 = (u8)((p2 + step) % 13);
    }
    Sound_StopBGM(Sound_GetCurrentBGM(), 0);
    radio_seq("SEQ_GS_OHKIDO_RABO");
    return FALSE;
}

static BOOL Show_Talk_Teardown(RadioShow *s)
{
    return show_free(s);
}

static BOOL Show_Talk_Print(RadioShow *s)
{
    PokemonTalkData *d = s->showData;
    int i;

    switch (d->state) {
    case 0:
        RadioPrintInitEz(s, 2);
        d->state++;
        break;
    case 1:
        if (!Radio_RunTextPrinter(s))
            break;
        d->state = 3;
        /* fallthrough */
    case 3:
        if (d->numLandmarks == 0 || d->landmarkSpeciesPairs[d->selector][1] == 0) {
            d->state = 6;
            break;
        }
        buffer_landmark(s, 0, d->landmarkSpeciesPairs[d->selector][0]);
        buffer_species(s, 1, d->landmarkSpeciesPairs[d->selector][1]);
        RadioPrintInitEz(s, 4);
        d->state++;
        break;
    case 4:
        if (!Radio_RunTextPrinter(s))
            break;
        for (i = 0; i < 2; i++) {
            String_Clear(s->curLineStr);
            if (s->showMsg != NULL)
                MessageLoader_GetString(s->showMsg, (u32)((19 - 6) * i + (6 + d->flavorTextPairs[d->selector][i])), s->curLineStr);
            StringTemplate_SetString(s->fmt, (u32)(i + 2), s->curLineStr, 1, 1, GAME_LANGUAGE);
        }
        RadioPrintInitEz(s, 5);
        d->state++;
        break;
    case 5:
        if (!Radio_RunTextPrinter(s))
            break;
        d->selector++;
        d->state = d->selector < 5 ? 3 : 6;
        break;
    case 6:
        if (RadioShow_DelayAndScrollLine(s)) {
            RadioPrintAndPlayJingle(s, 3);
            d->state++;
        }
        break;
    case 7:
        if (Radio_RunTextPrinter_WaitJingle(s)) {
            s->triggerCommercials = TRUE;
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* ---- Pokemon Search Party (shows/pokemon_search_party.c): thirteen
 * episodes, every one of them on the air in the cleared story. ---- */

typedef struct {
    u16 state;
    u16 episodeID;
} EpisodeShowData;

static BOOL Show_Search_Setup(RadioShow *s)
{
    EpisodeShowData *d = show_alloc(s, sizeof(EpisodeShowData));

    show_open_bank(s, MSG_SEARCH, 0, 1);
    d->episodeID = (u16)(LCRNG_Next() % 13);
    if (d->episodeID == s->lastEpisodeID)
        d->episodeID = (u16)((d->episodeID + 1) % 13);
    s->lastEpisodeID = d->episodeID;
    Sound_StopBGM(Sound_GetCurrentBGM(), 0);
    radio_seq("SEQ_GS_RADIO_VARIETY");
    return FALSE;
}

static BOOL Show_Search_Teardown(RadioShow *s)
{
    return show_free(s);
}

static BOOL episode_print(RadioShow *s, EpisodeShowData *d, int firstEpisodeMsg)
{
    switch (d->state) {
    case 0:
        RadioPrintInitEz(s, 2);
        d->state++;
        break;
    case 1:
        if (Radio_RunTextPrinter(s)) {
            RadioPrintInitEz(s, firstEpisodeMsg + d->episodeID);
            d->state++;
        }
        break;
    case 2:
        if (Radio_RunTextPrinter(s))
            d->state++;
        break;
    case 3:
        if (RadioShow_DelayAndScrollLine(s)) {
            RadioPrintAndPlayJingle(s, 3);
            d->state++;
        }
        break;
    case 4:
        if (Radio_RunTextPrinter_WaitJingle(s)) {
            s->triggerCommercials = TRUE;
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static BOOL Show_Search_Print(RadioShow *s)
{
    return episode_print(s, s->showData, 4);
}

/* ---- Serial Radio Drama (shows/serial_radio_drama.c) ---- */

static BOOL Show_Drama_Setup(RadioShow *s)
{
    EpisodeShowData *d = show_alloc(s, sizeof(EpisodeShowData));

    show_open_bank(s, MSG_DRAMA, 0, 1);
    d->episodeID = (u16)(LCRNG_Next() % 22);
    if (d->episodeID == s->lastEpisodeID)
        d->episodeID = (u16)((d->episodeID + 1) % 22);
    s->lastEpisodeID = d->episodeID;
    Sound_StopBGM(Sound_GetCurrentBGM(), 0);
    radio_seq("SEQ_GS_RADIO_VARIETY");
    return FALSE;
}

static BOOL Show_Drama_Teardown(RadioShow *s)
{
    if (s->showData == NULL)
        return FALSE;
    return show_free(s);
}

static BOOL Show_Drama_Print(RadioShow *s)
{
    return episode_print(s, s->showData, 4);
}

/* ---- Buena's Password (shows/buenas_password.c) ---- */

typedef struct {
    u16 state;
    u16 msgID;
} BuenaData;

static BOOL Show_Buena_Setup(RadioShow *s)
{
    BuenaData *d = show_alloc(s, sizeof(BuenaData));
    MessageLoader *tower;
    RTCDate date;

    show_open_bank(s, MSG_BUENA, 0, 1);
    d->msgID = 5; /* no Blue Card */
    tower = openmmo_pokegear_msg(MSG_RADIO_TOWER_2F, s->heapID);
    GetCurrentDate(&date);
    if (tower != NULL) {
        u32 set = (u32)((date.year * 366 + date.month * 31 + date.day) % 30);
        String *answer = MessageLoader_GetNewString(tower, 40 + set);

        StringTemplate_SetString(s->fmt, 0, answer, 2, 1, GAME_LANGUAGE);
        String_Free(answer);
        MessageLoader_Free(tower);
    }
    Sound_StopBGM(Sound_GetCurrentBGM(), 0);
    radio_seq("SEQ_GS_AIKOTOBA");
    return FALSE;
}

static BOOL Show_Buena_Teardown(RadioShow *s)
{
    return show_free(s);
}

static BOOL Show_Buena_Print(RadioShow *s)
{
    BuenaData *d = s->showData;

    switch (d->state) {
    case 0:
        RadioPrintInitEz(s, 2);
        d->state++;
        break;
    case 1:
        if (Radio_RunTextPrinter(s)) {
            RadioPrintInitEz(s, d->msgID);
            d->state++;
        }
        break;
    case 2:
        if (Radio_RunTextPrinter(s))
            d->state++;
        break;
    case 3:
        if (RadioShow_DelayAndScrollLine(s)) {
            RadioPrintAndPlayJingle(s, 3);
            d->state++;
        }
        break;
    case 4:
        if (Radio_RunTextPrinter_WaitJingle(s)) {
            s->triggerCommercials = TRUE;
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* ---- Trainer Profiles and That Town, These People: three lines drawn
 * from the bank, none twice. ---- */

typedef struct {
    u16 state;
    u8 msgIDs[3];
    u8 selector;
} ThreeLinesData;

static BOOL three_lines_unique(ThreeLinesData *d, u8 msgID, u8 index)
{
    int i;

    for (i = 0; i < index; i++)
        if (d->msgIDs[i] == msgID)
            return TRUE;
    return FALSE;
}

static void three_lines_pick(ThreeLinesData *d, int first, int count)
{
    int i, tries;
    u8 msgID;

    for (i = 0; i < 3; i++) {
        tries = 0;
        do {
            msgID = (u8)(first + (LCRNG_Next() % count));
        } while (three_lines_unique(d, msgID, (u8)i) && ++tries < 64);
        d->msgIDs[i] = msgID;
    }
}

static BOOL three_lines_print(RadioShow *s, ThreeLinesData *d)
{
    switch (d->state) {
    case 0:
        RadioPrintInitEz(s, 2);
        d->state++;
        break;
    case 1:
        if (!Radio_RunTextPrinter(s))
            break;
        d->state++;
        /* fallthrough */
    case 2:
        RadioPrintInitEz(s, d->msgIDs[d->selector]);
        d->state++;
        break;
    case 3:
        if (!Radio_RunTextPrinter(s))
            break;
        d->selector++;
        d->state = d->selector < 3 ? 2 : 4;
        break;
    case 4:
        if (RadioShow_DelayAndScrollLine(s)) {
            RadioPrintAndPlayJingle(s, 3);
            d->state++;
        }
        break;
    case 5:
        if (Radio_RunTextPrinter_WaitJingle(s)) {
            s->triggerCommercials = TRUE;
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static BOOL Show_Profiles_Setup(RadioShow *s)
{
    ThreeLinesData *d = show_alloc(s, sizeof(ThreeLinesData));

    show_open_bank(s, MSG_PROFILES, 0, 1);
    three_lines_pick(d, 4, 16);
    Sound_StopBGM(Sound_GetCurrentBGM(), 0);
    radio_seq("SEQ_GS_RADIO_TRAINER");
    return FALSE;
}

static BOOL Show_Profiles_Teardown(RadioShow *s) { return show_free(s); }
static BOOL Show_Profiles_Print(RadioShow *s) { return three_lines_print(s, s->showData); }

static BOOL Show_Town_Setup(RadioShow *s)
{
    ThreeLinesData *d = show_alloc(s, sizeof(ThreeLinesData));

    show_open_bank(s, MSG_TOWN, 0, 1);
    three_lines_pick(d, 4, 20);
    Sound_StopBGM(Sound_GetCurrentBGM(), 0);
    radio_seq("SEQ_GS_RADIO_PT");
    return FALSE;
}

static BOOL Show_Town_Teardown(RadioShow *s) { return show_free(s); }
static BOOL Show_Town_Print(RadioShow *s) { return three_lines_print(s, s->showData); }

/* ---- the Poke Flute, the Unown, the Rocket announcement, the Mahogany
 * signal: music and a title, or one line over and over. ---- */

typedef struct {
    u8 state;
} OneLineData;

static BOOL Show_Flute_Setup(RadioShow *s)
{
    show_alloc(s, sizeof(OneLineData));
    show_open_bank(s, MSG_FLUTE, 0, -1);
    Sound_StopBGM(Sound_GetCurrentBGM(), 1);
    radio_seq("SEQ_GS_HUE");
    return FALSE;
}
static BOOL Show_Flute_Teardown(RadioShow *s) { return show_free(s); }
static BOOL Show_Flute_Print(RadioShow *s) { (void)s; return FALSE; }

static BOOL Show_Unown_Setup(RadioShow *s)
{
    show_alloc(s, sizeof(OneLineData));
    show_open_bank(s, MSG_UNOWN, 0, -1);
    Sound_StopBGM(Sound_GetCurrentBGM(), 1);
    radio_seq("SEQ_GS_RADIO_UNKNOWN");
    return FALSE;
}
static BOOL Show_Unown_Teardown(RadioShow *s) { return show_free(s); }
static BOOL Show_Unown_Print(RadioShow *s) { (void)s; return FALSE; }

static BOOL Show_Rocket_Setup(RadioShow *s)
{
    show_alloc(s, sizeof(OneLineData));
    show_open_bank(s, MSG_ROCKET, 0, 1);
    Sound_StopBGM(Sound_GetCurrentBGM(), 1);
    radio_seq("SEQ_GS_SENKYO_R");
    return FALSE;
}
static BOOL Show_Rocket_Teardown(RadioShow *s) { return show_free(s); }
static BOOL Show_Rocket_Print(RadioShow *s)
{
    OneLineData *d = s->showData;

    switch (d->state) {
    case 0:
        RadioPrintInitEz(s, 2);
        d->state++;
        break;
    case 1:
        if (Radio_RunTextPrinter(s))
            d->state = 0;
        break;
    }
    return FALSE;
}

static BOOL Show_Mahogany_Setup(RadioShow *s)
{
    show_alloc(s, sizeof(OneLineData));
    show_open_bank(s, MSG_MAHOGANY, 0, -1);
    Sound_StopBGM(Sound_GetCurrentBGM(), 1);
    radio_seq("SEQ_GS_KAIDENPA");
    return FALSE;
}
static BOOL Show_Mahogany_Teardown(RadioShow *s) { return show_free(s); }
static BOOL Show_Mahogany_Print(RadioShow *s) { (void)s; return FALSE; }

/* ---- Commercials (shows/commercials.c): region and channel filtered,
 * every unlock true. ---- */

typedef struct {
    u16 state;
    u16 msgID;
} CommercialsData;

/* region bits: 1 Johto, 2 Kanto; channel: 0xFF any. */
static const u8 sCommercials[][2] = {
    { 3, 0xFF }, { 3, 0xFF }, { 3, 0xFF }, { 3, 0xFF }, { 3, 0xFF },
    { 1, 0xFF }, { 1, 0xFF }, { 1, 0xFF }, { 1, 0xFF },
    { 3, 2 }, { 3, 2 },
    { 3, 0xFF }, { 3, 0xFF }, { 3, 0xFF }, { 3, 0xFF },
    { 1, 3 }, { 1, 3 }, { 1, 3 }, { 1, 2 }, { 1, 2 }, { 3, 2 },
    { 3, 2 }, { 1, 3 },
    { 3, 1 }, { 3, 1 }, { 3, 1 }, { 3, 1 }, { 3, 1 }, { 3, 1 },
    { 3, 3 },
    { 2, 3 }, { 2, 3 }, { 2, 3 }, { 2, 3 }, { 2, 3 },
    { 3, 3 },
};

static BOOL Show_Commercials_Setup(RadioShow *s)
{
    CommercialsData *d = show_alloc(s, sizeof(CommercialsData));
    u8 unlocked[36];
    u8 num = 0, i;

    s->triggerCommercials = FALSE;
    show_open_bank(s, MSG_COMMERCIAL, 0, -1);
    for (i = 0; i < (u8)(sizeof sCommercials / sizeof sCommercials[0]); i++) {
        u8 chan = sCommercials[i][1];

        if (chan != 0xFF && chan != s->nextStation)
            continue;
        if (sCommercials[i][0] & (s->regionNo + 1))
            unlocked[num++] = i;
    }
    d->msgID = (u16)(2 + (num ? unlocked[LCRNG_Next() % num] : 0));
    return FALSE;
}

static BOOL Show_Commercials_Teardown(RadioShow *s) { return show_free(s); }

static BOOL Show_Commercials_Print(RadioShow *s)
{
    CommercialsData *d = s->showData;

    switch (d->state) {
    case 0:
        RadioPrintInit(s, d->msgID, 1);
        d->state++;
        break;
    case 1:
        if (Radio_RunTextPrinter(s))
            d->state++;
        break;
    case 2:
        if (RadioShow_Delay(s))
            return TRUE;
        break;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* The video (overlay_101_021F49F8.c)                                  */
/* ------------------------------------------------------------------ */

static const PokegearSpriteTemplate sSpriteTemplates[5] = {
    { 0, 0, 0, 0, 1, 1, 5, NNS_G2D_VRAM_TYPE_2DMAIN },
    { 0, 0, 0, 0, 2, 1, 5, NNS_G2D_VRAM_TYPE_2DMAIN },
    { 0, 0, 0, 0, 3, 1, 5, NNS_G2D_VRAM_TYPE_2DMAIN },
    { 0, 0, 0, 0, 4, 1, 5, NNS_G2D_VRAM_TYPE_2DMAIN },
    { 0, 128, 128, 0, 0, 1, 4, NNS_G2D_VRAM_TYPE_2DMAIN },
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

static void radio_init_bgs(PokegearRadioAppData *r)
{
    BgConfig *bg = r->pokegear->bgConfig;
    BgTemplate t[6] = {
        { 0, 0, 0x800, 0, BG_SCREEN_SIZE_256x256, GX_BG_COLORMODE_16, GX_BG_SCRBASE_0xf000, GX_BG_CHARBASE_0x00000, GX_BG_EXTPLTT_01, 1, GX_BG_AREAOVER_XLU, FALSE },
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
        Bg_ClearTilesRange((u8)(BG_LAYER_MAIN_1 + i), 0x20, 0, r->heapID);
        Bg_ClearTilemap(bg, (u8)(BG_LAYER_SUB_1 + i));
        Bg_ClearTilesRange((u8)(BG_LAYER_SUB_1 + i), 0x20, 0, r->heapID);
    }
    for (i = 1; i < 8; i++)
        if (i != 4)
            Bg_ToggleLayer((u8)i, 0);
}

static void radio_load_graphics(PokegearRadioAppData *r)
{
    BgConfig *bg = r->pokegear->bgConfig;
    NARC *narc = openmmo_pokegear_narc(PG_NARC_RADIO, r->heapID);

    Font_InitManager(FONT_SUBSCREEN, r->heapID);
    if (narc == NULL)
        return;
    Graphics_LoadTilesToBgLayerFromOpenNARC(narc, PGRADIO_MAIN_CHAR + r->skin, bg, BG_LAYER_MAIN_2, 0, 0, FALSE, r->heapID);
    Graphics_LoadTilesToBgLayerFromOpenNARC(narc, PGRADIO_SUB_CHAR + r->skin, bg, BG_LAYER_SUB_3, 0, 0, FALSE, r->heapID);
    Graphics_LoadTilemapToBgLayerFromOpenNARC(narc, PGRADIO_DIAL_SCRN + r->skin, bg, BG_LAYER_MAIN_2, 0, 0x800, FALSE, r->heapID);
    Graphics_LoadTilemapToBgLayerFromOpenNARC(narc, PGRADIO_MAIN3_SCRN + r->skin, bg, BG_LAYER_MAIN_3, 0, 0x800, FALSE, r->heapID);
    Graphics_LoadTilemapToBgLayerFromOpenNARC(narc, PGRADIO_SUB_SCRN + r->skin, bg, BG_LAYER_SUB_3, 0, 0x800, FALSE, r->heapID);
    r->scrnRaw = Graphics_GetScrnDataFromOpenNARC(narc, PGRADIO_DIAL_SCRN + r->skin, FALSE, &r->scrn, r->heapID);
    NARC_dtor(narc);
    Bg_ScheduleTilemapTransfer(bg, BG_LAYER_MAIN_2);
    Bg_ScheduleTilemapTransfer(bg, BG_LAYER_SUB_3);
}

static void radio_unload_graphics(PokegearRadioAppData *r)
{
    if (r->scrnRaw != NULL)
        Heap_Free(r->scrnRaw);
    r->scrnRaw = NULL;
    r->scrn = NULL;
    Font_Free(FONT_SUBSCREEN);
}

static void radio_load_palettes(PokegearRadioAppData *r)
{
    PaletteData *pd = r->pokegear->plttData;
    NARC *narc = openmmo_pokegear_narc(PG_NARC_RADIO, r->heapID);

    if (narc == NULL)
        return;
    pltt_load(pd, narc, PGRADIO_MAIN_PLTT + r->skin, r->heapID, PLTTBUF_MAIN_BG, 0x1C0, 0, 0);
    pltt_load(pd, narc, PGRADIO_SUB_PLTT + r->skin, r->heapID, PLTTBUF_SUB_BG, 0x180, 0, 0);
    pltt_load(pd, narc, PGRADIO_OBJ_PLTT, r->heapID, PLTTBUF_MAIN_OBJ, 0x180, 0x40, 0);
    pltt_load(pd, narc, PGRADIO_OBJ_PLTT, r->heapID, PLTTBUF_SUB_OBJ, 0x180, 0x40, 0);
    PaletteData_SetAutoTransparent(pd, TRUE);
    PaletteData_Blend(pd, PLTTBUF_MAIN_BG, 0, 0xE0, 16, COLOR_BLACK);
    PaletteData_Blend(pd, PLTTBUF_MAIN_OBJ, 0x40, 0xC0, 16, COLOR_BLACK);
    PaletteData_CommitFadedBuffers(pd);
    PaletteData_SetAutoTransparent(pd, FALSE);
    NARC_dtor(narc);
}

static void radio_init_windows(PokegearRadioAppData *r)
{
    static const WindowTemplate t[3] = {
        { BG_LAYER_SUB_2, 2, 19, 28, 4, 0, 0x38F },
        { BG_LAYER_SUB_2, 4, 16, 14, 2, 0, 0x373 },
        { BG_LAYER_MAIN_1, 5, 1, 22, 2, 0, 0x3D3 },
    };
    int i, region = 0, radio = 1;

    for (i = 0; i < 3; i++) {
        Window_Add(r->pokegear->bgConfig, &r->windows[i], t[i].bgLayer, t[i].tilemapLeft, t[i].tilemapTop,
                   t[i].width, t[i].height, t[i].palette, t[i].baseTile);
        Window_FillTilemap(&r->windows[i], 0);
    }
    openmmo_pokegear_map(r->pokegear->args->mapID, &region, NULL, NULL, &radio, NULL, NULL);
    r->show = RadioShow_Create(r->pokegear->saveData, r->pokegear->args->mapID, region,
                               &r->windows[0], &r->windows[2], &r->windows[1], TEXT_COLOR(1, 2, 0), r->heapID);
}

static void radio_unload_windows(PokegearRadioAppData *r)
{
    int i;

    RadioShow_Delete(r->show);
    r->show = NULL;
    for (i = 0; i < 3; i++) {
        Window_ClearAndCopyToVRAM(&r->windows[i]);
        Window_Remove(&r->windows[i]);
    }
}

static void radio_create_sprites(PokegearRadioAppData *r)
{
    int i;

    for (i = 0; i < 5; i++) {
        r->sprites[i] = PokegearApp_CreateSprite(r->pokegear, &sSpriteTemplates[i]);
        if (r->sprites[i] == NULL)
            continue;
        Sprite_SetExplicitPriority(r->sprites[i], 1);
        Sprite_SetDrawFlag(r->sprites[i], FALSE);
        Sprite_SetAnimateFlag(r->sprites[i], TRUE);
    }
    if (r->sprites[4] != NULL) {
        Sprite_SetExplicitPriority(r->sprites[4], 3);
        Sprite_SetDrawFlag(r->sprites[4], TRUE);
        Sprite_SetPositionXY(r->sprites[4], r->cursorX, r->cursorY);
    }
}

static void radio_unload_sprites(PokegearRadioAppData *r)
{
    int i;

    for (i = 0; i < 5; i++) {
        PokegearApp_DeleteSprite(r->pokegear, r->sprites[i]);
        r->sprites[i] = NULL;
    }
}

static void radio_init_app_cursor(PokegearRadioAppData *r)
{
    if (r->pokegear->cursorInAppSwitchZone == TRUE) {
        PokegearCursorManager_SetCursorSpritesDrawState(r->pokegear->cursorManager, 0, TRUE);
        PokegearCursorManager_SetSpecIndexAndCursorPos(r->pokegear->cursorManager, 0, PokegearApp_AppIdToButtonIndex(r->pokegear));
    } else {
        PokegearCursorManager_SetCursorSpritesDrawState(r->pokegear->cursorManager, 0, FALSE);
    }
}

static void radio_run(PokegearAppData *app, void *arg)
{
    PokegearRadioAppData *r = arg;

    (void)app;
    if (r->stationActive && r->show != NULL)
        RadioShow_Main(r->show);
}

static BOOL radio_video_init(PokegearRadioAppData *r)
{
    switch (r->substate) {
    case 0:
        radio_init_bgs(r);
        radio_load_graphics(r);
        radio_init_windows(r);
        PokegearApp_CreateSpriteManager(r->pokegear, GEAR_APP_RADIO);
        radio_load_palettes(r);
        break;
    case 1:
        radio_create_sprites(r);
        radio_init_app_cursor(r);
        r->pokegear->vblankCB = radio_run;
        r->substate = 0;
        return TRUE;
    }
    r->substate++;
    return FALSE;
}

static u8 radio_tuned_station(PokegearRadioAppData *r, s16 x, s16 y, u8 *signal);

static BOOL radio_video_unload(PokegearRadioAppData *r)
{
    switch (r->substate) {
    case 0:
        if (radio_tuned_station(r, r->cursorX, r->cursorY, NULL) == 0xFF
            || (r->show != NULL && (r->show->isPlayingJingle || r->show->curStation == RADIO_STATION_COMMERCIALS))) {
            Sound_FadeOutBGM(0, 4);
        } else {
            r->substate = 2;
            break;
        }
        r->substate++;
        break;
    case 1:
        if (Sound_IsFadeActive())
            break;
        Sound_PlayBGM(r->pokegear->args->mapMusicID);
        r->substate++;
        break;
    case 2:
        r->pokegear->vblankCB = NULL;
        radio_unload_sprites(r);
        PokegearApp_DestroySpriteManager(r->pokegear);
        radio_unload_windows(r);
        radio_unload_graphics(r);
        Pokegear_ClearAppBgLayers(r->pokegear);
        r->substate = 0;
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* The dial                                                            */
/* ------------------------------------------------------------------ */

static void radio_toggle_buttons(PokegearRadioAppData *r, u8 on, u8 off)
{
    static const u8 coords[4][2] = { { 2, 6 }, { 26, 6 }, { 2, 14 }, { 26, 14 } };
    BgConfig *bg = r->pokegear->bgConfig;

    if (r->scrn == NULL)
        return;
    if (off < 4)
        Bg_CopyToTilemapRect(bg, BG_LAYER_MAIN_2, coords[off][0], coords[off][1], 4, 4, r->scrn->rawData, 0, 24,
                             (u8)(r->scrn->screenWidth / 8), (u8)(r->scrn->screenHeight / 8));
    if (on < 4)
        Bg_CopyToTilemapRect(bg, BG_LAYER_MAIN_2, coords[on][0], coords[on][1], 4, 4, r->scrn->rawData, 4, 24,
                             (u8)(r->scrn->screenWidth / 8), (u8)(r->scrn->screenHeight / 8));
    Bg_ScheduleTilemapTransfer(bg, BG_LAYER_MAIN_2);
}

static void radio_on_reselect(void *arg)
{
    PokegearRadioAppData *r = arg;

    r->isDraggingCursor = 0;
}

static void radio_on_deselect(void *arg)
{
    PokegearRadioAppData *r = arg;

    r->isDraggingCursor = 0;
    r->pokegear->cursorInAppSwitchZone = FALSE;
    PokegearCursorManager_SetCursorSpritesDrawState(r->pokegear->cursorManager, 0, FALSE);
}

static void radio_open_station(PokegearRadioAppData *r)
{
    if (!r->stationActive) {
        RadioShow_BeginSegment(r->show, r->station, r->signalStrength != 2);
        r->stationActive = TRUE;
    }
}

static void radio_close_station(PokegearRadioAppData *r)
{
    if (r->stationActive) {
        RadioShow_EndSegment(r->show);
        Window_FillTilemap(&r->windows[2], 0);
        Window_FillTilemap(&r->windows[1], 0);
        Window_ScheduleCopyToVRAM(&r->windows[2]);
        Window_ScheduleCopyToVRAM(&r->windows[1]);
        r->stationActive = FALSE;
    }
}

static u8 radio_tuned_station(PokegearRadioAppData *r, s16 x, s16 y, u8 *signal)
{
    int station = TouchScreen_CheckTouchedHitTableID(sTuningHitboxes[r->stationSelection], (u32)x, (u32)y);
    u8 ret;

    if (station == TOUCHSCREEN_INPUT_NONE) {
        if (signal)
            *signal = 0;
        return 0xFF;
    }
    switch (r->stationSelection) {
    case RADIO_SEL_JOHTO:
    case RADIO_SEL_KANTO:
    case RADIO_SEL_KANTO_EXPN:
    case RADIO_SEL_NO_SIGNAL:
        ret = (u8)(station / 2);
        break;
    case RADIO_SEL_ROCKET:
        ret = 6;
        break;
    case RADIO_SEL_MAHOGANY:
        ret = 7;
        break;
    default:
        ret = 5;
        break;
    }
    if (signal)
        *signal = (u8)(2 - (station % 2));
    return ret;
}

static void radio_start(PokegearRadioAppData *r)
{
    u8 signal = 0;

    r->station = radio_tuned_station(r, r->cursorX, r->cursorY, &signal);
    r->signalStrength = signal;
    if (r->station < 8)
        radio_open_station(r);
}

static BOOL radio_coords_to_station(PokegearRadioAppData *r, s16 x, s16 y)
{
    u8 signal = 0;
    u8 station = radio_tuned_station(r, x, y, &signal);

    if (station == 0xFF) {
        if (r->signalStrength != 0) {
            radio_close_station(r);
            Sound_StopBGM(Sound_GetCurrentBGM(), 0);
        }
        r->signalStrength = 0;
        r->station = 0xFF;
        return FALSE;
    }
    if (station == r->station) {
        if (signal != r->signalStrength) {
            RadioShow_SetStaticLevel(r->show, signal != 2);
            r->signalStrength = signal;
        }
        return FALSE;
    }
    if (r->station != 0xFF)
        radio_close_station(r);
    r->station = station;
    r->signalStrength = signal;
    radio_open_station(r);
    return TRUE;
}

static void radio_snap_cursor(PokegearRadioAppData *r, u8 channel)
{
    r->cursorX = sTuning_Johto[channel * 2].circle.x;
    r->cursorY = sTuning_Johto[channel * 2].circle.y;
    if (r->sprites[4] != NULL)
        Sprite_SetPositionXY(r->sprites[4], r->cursorX, r->cursorY);
    radio_coords_to_station(r, r->cursorX, r->cursorY);
}

static u8 radio_available_channels(PokegearRadioAppData *r)
{
    int region = 0, radio = 0, i;
    u16 mapID = r->pokegear->args->mapID;

    if (!openmmo_pokegear_map(mapID, &region, NULL, NULL, &radio, NULL, NULL))
        return RADIO_SEL_NO_SIGNAL;
    if (!radio)
        return RADIO_SEL_NO_SIGNAL;
    for (i = 0; i < (int)(sizeof sAlphMaps / sizeof sAlphMaps[0]); i++)
        if (mapID == POKEGEAR_PORTED_FIRST + sAlphMaps[i])
            return RADIO_SEL_ALPH;
    if (region)
        return RADIO_SEL_KANTO_EXPN;
    return RADIO_SEL_JOHTO;
}

static void radio_begin_slide(PokegearRadioAppData *r, int direction)
{
    BgConfig *bg = r->pokegear->bgConfig;
    int i;

    G2S_SetWnd0Position(0, 112, 255, 192);
    G2S_SetWnd1Position(255, 112, 0, 192);
    G2S_SetWndOutsidePlane(17, FALSE);
    G2S_SetWnd0InsidePlane(31, FALSE);
    G2S_SetWnd1InsidePlane(31, FALSE);
    GXS_SetVisibleWnd(3);
    if (direction == 0) {
        for (i = 0; i < 3; i++) {
            Bg_SetOffset(bg, (u8)(BG_LAYER_SUB_1 + i), BG_OFFSET_UPDATE_SET_Y, -80);
            Bg_ToggleLayer((u8)(BG_LAYER_SUB_1 + i), 1);
        }
    } else {
        for (i = 0; i < 3; i++)
            Bg_SetOffset(bg, (u8)(BG_LAYER_SUB_1 + i), BG_OFFSET_UPDATE_SUB_Y, 0);
    }
    r->windowScrollStep = 0;
    r->windowScrollFinished = FALSE;
}

static BOOL radio_run_slide(PokegearRadioAppData *r, int direction)
{
    BgConfig *bg = r->pokegear->bgConfig;
    int i;

    if (r->windowScrollFinished)
        return TRUE;
    for (i = 0; i < 3; i++)
        Bg_SetOffset(bg, (u8)(BG_LAYER_SUB_1 + i), direction == 0 ? BG_OFFSET_UPDATE_ADD_Y : BG_OFFSET_UPDATE_SUB_Y, 10);
    if (++r->windowScrollStep < 8)
        return FALSE;
    r->windowScrollStep = 0;
    r->windowScrollFinished = TRUE;
    if (direction == 1) {
        for (i = 0; i < 3; i++) {
            Bg_ToggleLayer((u8)(BG_LAYER_SUB_1 + i), 0);
            Bg_ClearTilemap(bg, (u8)(BG_LAYER_SUB_1 + i));
            Bg_SetOffset(bg, (u8)(BG_LAYER_SUB_1 + i), BG_OFFSET_UPDATE_SET_Y, 0);
            Bg_ScheduleTilemapTransfer(bg, (u8)(BG_LAYER_SUB_1 + i));
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

static int radio_handle_key_input(PokegearRadioAppData *r)
{
    int xSpeed = 0, ySpeed = 0;
    s16 prevX, prevY;
    u8 prevButton;

    if (gSystem.pressedKeys & PAD_BUTTON_B) {
        r->pokegear->cursorInAppSwitchZone = TRUE;
        PokegearCursorManager_SetCursorSpritesDrawState(r->pokegear->cursorManager, 0, TRUE);
        PokegearCursorManager_SetSpecIndexAndCursorPos(r->pokegear->cursorManager, 0, PokegearApp_AppIdToButtonIndex(r->pokegear));
        PokegearApp_PlaySE(PG_SE_CANCEL);
        return TOUCH_MENU_NO_INPUT;
    }
    if (gSystem.pressedKeys & PAD_BUTTON_A) {
        prevButton = r->selectedButton;
        r->selectedButton = (u8)((r->selectedButton + 1) % 4);
        radio_toggle_buttons(r, r->selectedButton, prevButton);
        radio_snap_cursor(r, r->selectedButton);
        PokegearApp_PlaySE(PG_SE_CURSOR);
        return TOUCH_MENU_NO_INPUT;
    }
    if (gSystem.heldKeys & PAD_KEY_LEFT) xSpeed = -2;
    if (gSystem.heldKeys & PAD_KEY_RIGHT) xSpeed = 2;
    if (gSystem.heldKeys & PAD_KEY_UP) ySpeed = -2;
    if (gSystem.heldKeys & PAD_KEY_DOWN) ySpeed = 2;
    if (xSpeed == 0 && ySpeed == 0)
        return TOUCH_MENU_NO_INPUT;
    prevX = r->cursorX;
    prevY = r->cursorY;
    if (TouchScreen_IsTouchInHitTable(&sTuningArea, (u32)(r->cursorX + xSpeed), (u32)r->cursorY))
        r->cursorX = (s16)(r->cursorX + xSpeed);
    if (TouchScreen_IsTouchInHitTable(&sTuningArea, (u32)r->cursorX, (u32)(r->cursorY + ySpeed)))
        r->cursorY = (s16)(r->cursorY + ySpeed);
    if ((r->cursorX != prevX || r->cursorY != prevY) && r->sprites[4] != NULL)
        Sprite_SetPositionXY(r->sprites[4], r->cursorX, r->cursorY);
    radio_toggle_buttons(r, 4, r->selectedButton);
    radio_coords_to_station(r, r->cursorX, r->cursorY);
    return TOUCH_MENU_NO_INPUT;
}

static int radio_handle_drag(PokegearRadioAppData *r)
{
    if (!gSystem.touchHeld) {
        r->isDraggingCursor = FALSE;
        return TOUCH_MENU_NO_INPUT;
    }
    if (TouchScreen_IsTouchInHitTable(&sTuningArea, gSystem.touchX, gSystem.touchY)) {
        r->cursorX = (s16)gSystem.touchX;
        r->cursorY = (s16)gSystem.touchY;
        if (r->sprites[4] != NULL)
            Sprite_SetPositionXY(r->sprites[4], r->cursorX, r->cursorY);
        radio_coords_to_station(r, r->cursorX, r->cursorY);
    }
    return TOUCH_MENU_NO_INPUT;
}

static int radio_handle_touch_internal(PokegearRadioAppData *r, BOOL *wasTouch)
{
    TouchScreenRect near;
    int ret;

    if (!gSystem.touchPressed)
        return TOUCH_MENU_NO_INPUT;
    ret = PokegearApp_HandleTouchInput_SwitchApps(r->pokegear);
    if (ret != TOUCH_MENU_NO_INPUT) {
        *wasTouch = TRUE;
        return ret;
    }
    ret = TouchScreen_CheckRectanglePressed(sTuningButtonHitboxes);
    if (ret != TOUCHSCREEN_INPUT_NONE) {
        radio_toggle_buttons(r, (u8)ret, r->selectedButton);
        radio_snap_cursor(r, (u8)ret);
        PokegearApp_PlaySE(PG_SE_CURSOR);
        r->selectedButton = (u8)ret;
        *wasTouch = TRUE;
        return TOUCH_MENU_NO_INPUT;
    }
    if (!TouchScreen_IsTouchInHitTable(&sTuningArea, gSystem.touchX, gSystem.touchY))
        return TOUCH_MENU_NO_INPUT;
    near.circle.code = TOUCHSCREEN_USE_CIRCLE;
    near.circle.r = 8;
    near.circle.x = (u8)r->cursorX;
    near.circle.y = (u8)r->cursorY;
    if (TouchScreen_IsTouchInHitTable(&near, gSystem.touchX, gSystem.touchY)) {
        r->cursorX = (s16)gSystem.touchX;
        r->cursorY = (s16)gSystem.touchY;
        if (r->sprites[4] != NULL)
            Sprite_SetPositionXY(r->sprites[4], r->cursorX, r->cursorY);
        r->isDraggingCursor = TRUE;
        *wasTouch = TRUE;
        radio_toggle_buttons(r, 4, r->selectedButton);
        radio_coords_to_station(r, r->cursorX, r->cursorY);
    }
    return TOUCH_MENU_NO_INPUT;
}

static int radio_handle_touch_input(PokegearRadioAppData *r, BOOL *wasTouch)
{
    int input;

    *wasTouch = FALSE;
    if (r->isDraggingCursor) {
        *wasTouch = TRUE;
        return radio_handle_drag(r);
    }
    input = radio_handle_touch_internal(r, wasTouch);
    if (*wasTouch) {
        r->pokegear->menuInputState = MENU_INPUT_STATE_TOUCH;
        if (r->pokegear->cursorInAppSwitchZone == TRUE)
            radio_on_deselect(r);
    }
    return input;
}

/* ------------------------------------------------------------------ */
/* The card (pokegear_radio.c)                                         */
/* ------------------------------------------------------------------ */

static int radio_input_loop(PokegearRadioAppData *r)
{
    BOOL wasTouch = FALSE;
    int result = radio_handle_touch_input(r, &wasTouch);

    if (!wasTouch) {
        PokegearApp_HandleInputModeChangeToButtons(r->pokegear);
        if (r->pokegear->cursorInAppSwitchZone == TRUE)
            result = PokegearApp_HandleKeyInput_SwitchApps(r->pokegear);
        else
            result = radio_handle_key_input(r);
    }
    switch (result) {
    case TOUCH_MENU_NO_INPUT:
        break;
    case GEAR_RETURN_4:
        r->pokegear->appReturnCode = result;
        return RADIO_MAIN_FADE_OUT;
    default:
        r->pokegear->appReturnCode = result;
        return RADIO_MAIN_FADE_OUT_APP;
    }
    return RADIO_MAIN_INPUT;
}

static int radio_fade_in(PokegearRadioAppData *r)
{
    PaletteData *pd = r->pokegear->plttData;
    int i;

    switch (r->state) {
    case 0:
        StartScreenFade(FADE_BOTH_SCREENS, FADE_TYPE_BRIGHTNESS_IN, FADE_TYPE_BRIGHTNESS_IN, COLOR_BLACK, 6, 1, r->heapID);
        for (i = 0; i < 8; i++)
            Bg_ToggleLayer((u8)i, 1);
        PaletteData_SetAutoTransparent(pd, TRUE);
        PaletteData_Blend(pd, PLTTBUF_MAIN_BG, 0, 0xE0, 0, COLOR_BLACK);
        PaletteData_Blend(pd, PLTTBUF_MAIN_OBJ, 0x40, 0xC0, 0, COLOR_BLACK);
        PaletteData_CommitFadedBuffers(pd);
        PaletteData_SetAutoTransparent(pd, FALSE);
        GXLayers_EngineAToggleLayers(GX_PLANEMASK_OBJ, 1);
        GXLayers_EngineBToggleLayers(GX_PLANEMASK_OBJ, 1);
        Sound_StopBGM(Sound_GetCurrentBGM(), 6);
        r->state++;
        break;
    case 1:
        if (IsScreenFadeDone()) {
            radio_start(r);
            r->state = 0;
            return RADIO_MAIN_INPUT;
        }
        break;
    }
    return RADIO_MAIN_FADE_IN;
}

static int radio_fade_out(PokegearRadioAppData *r)
{
    int i;

    switch (r->state) {
    case 0:
        StartScreenFade(FADE_BOTH_SCREENS, FADE_TYPE_BRIGHTNESS_OUT, FADE_TYPE_BRIGHTNESS_OUT, COLOR_BLACK, 6, 1, r->heapID);
        r->state++;
        break;
    case 1:
        if (IsScreenFadeDone()) {
            radio_close_station(r);
            for (i = 0; i < 8; i++)
                Bg_ToggleLayer((u8)i, 0);
            r->state = 0;
            return RADIO_MAIN_UNLOAD;
        }
        break;
    }
    return RADIO_MAIN_FADE_OUT;
}

static int radio_fade_in_app(PokegearRadioAppData *r)
{
    PaletteData *pd = r->pokegear->plttData;
    int i;

    switch (r->state) {
    case 0:
        PaletteData_SetAutoTransparent(pd, TRUE);
        radio_begin_slide(r, 0);
        r->pokegear->fadeCounter = 0;
        for (i = 0; i < 3; i++)
            Bg_ToggleLayer((u8)(i + BG_LAYER_MAIN_1), 1);
        G2_SetBlendBrightness(GX_BLEND_PLANEMASK_BG1 | GX_BLEND_PLANEMASK_BG2 | GX_BLEND_PLANEMASK_BG3, 0);
        Sound_StopBGM(Sound_GetCurrentBGM(), 6);
        r->state++;
        break;
    case 1:
        if (Pokegear_RunFadeLayers123(r->pokegear, 0) & radio_run_slide(r, 0))
            r->state++;
        break;
    case 2:
        PaletteData_SetAutoTransparent(pd, FALSE);
        r->pokegear->fadeCounter = 0;
        radio_start(r);
        r->state = 0;
        return RADIO_MAIN_INPUT;
    }
    return RADIO_MAIN_FADE_IN_APP;
}

static int radio_fade_out_app(PokegearRadioAppData *r)
{
    PaletteData *pd = r->pokegear->plttData;
    int i;

    switch (r->state) {
    case 0:
        radio_begin_slide(r, 1);
        PaletteData_SetAutoTransparent(pd, TRUE);
        r->pokegear->fadeCounter = 0;
        r->state++;
        break;
    case 1:
        if (Pokegear_RunFadeLayers123(r->pokegear, 1) & radio_run_slide(r, 1))
            r->state++;
        break;
    case 2:
        PaletteData_Blend(pd, PLTTBUF_MAIN_BG, 0, 0xE0, 16, COLOR_BLACK);
        PaletteData_Blend(pd, PLTTBUF_MAIN_OBJ, 0x40, 0xC0, 16, COLOR_BLACK);
        PaletteData_CommitFadedBuffers(pd);
        radio_close_station(r);
        for (i = 0; i < 3; i++) {
            Bg_ToggleLayer((u8)(i + BG_LAYER_MAIN_1), 0);
            Bg_ToggleLayer((u8)(i + BG_LAYER_SUB_1), 0);
        }
        PaletteData_SetAutoTransparent(pd, FALSE);
        r->pokegear->fadeCounter = 0;
        r->state = 0;
        return RADIO_MAIN_UNLOAD;
    }
    return RADIO_MAIN_FADE_OUT_APP;
}

BOOL PokegearRadio_Init(ApplicationManager *man, int *state)
{
    PokegearAppData *app = ApplicationManager_Args(man);
    PokegearRadioAppData *r;

    (void)state;
    Heap_Create(HEAP_ID_APPLICATION, POKEGEAR_CARD_HEAP, 0x20000);
    r = ApplicationManager_NewData(man, sizeof *r, POKEGEAR_CARD_HEAP);
    memset(r, 0, sizeof *r);
    r->pokegear = app;
    r->heapID = POKEGEAR_CARD_HEAP;
    app->childAppdata = r;
    app->reselectAppCB = radio_on_reselect;
    app->deselectAppCB = radio_on_deselect;
    r->skin = app->skin;
    openmmo_pokegear_radio_cursor(&r->cursorX, &r->cursorY);
    r->selectedButton = 3;
    r->stationSelection = radio_available_channels(r);
    r->station = 0xFF;
    return TRUE;
}

BOOL PokegearRadio_Main(ApplicationManager *man, int *state)
{
    PokegearRadioAppData *r = ApplicationManager_Data(man);

    switch (*state) {
    case RADIO_MAIN_INIT:
        if (radio_video_init(r))
            *state = r->pokegear->isSwitchApp ? RADIO_MAIN_FADE_IN_APP : RADIO_MAIN_FADE_IN;
        break;
    case RADIO_MAIN_INPUT:
        *state = radio_input_loop(r);
        break;
    case RADIO_MAIN_UNLOAD:
        if (radio_video_unload(r))
            *state = RADIO_MAIN_QUIT;
        break;
    case RADIO_MAIN_FADE_IN:
        *state = radio_fade_in(r);
        break;
    case RADIO_MAIN_FADE_OUT:
        *state = radio_fade_out(r);
        break;
    case RADIO_MAIN_FADE_IN_APP:
        *state = radio_fade_in_app(r);
        break;
    case RADIO_MAIN_FADE_OUT_APP:
        *state = radio_fade_out_app(r);
        break;
    case RADIO_MAIN_QUIT:
        return TRUE;
    }
    return FALSE;
}

BOOL PokegearRadio_Exit(ApplicationManager *man, int *state)
{
    PokegearRadioAppData *r = ApplicationManager_Data(man);

    (void)state;
    openmmo_pokegear_set_radio_cursor(r->cursorX, r->cursorY);
    r->pokegear->reselectAppCB = NULL;
    r->pokegear->deselectAppCB = NULL;
    r->pokegear->isSwitchApp = TRUE;
    ApplicationManager_FreeData(man);
    Heap_Destroy(POKEGEAR_CARD_HEAP);
    return TRUE;
}
