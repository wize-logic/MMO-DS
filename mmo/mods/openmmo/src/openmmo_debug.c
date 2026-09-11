/* Select opens the tester's door into the world. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The generated vars/flags header can hand out its own name table; nothing in
 * the engine asks for it, so this file is the only definition of it in the
 * build and the only place its ~4,400 rows are paid for. It is what makes a
 * flag list read FLAG_RECEIVED_EXPLORER_KIT instead of 121. */
#define POKEPLATINUM_GENERATED_LOOKUP
#define POKEPLATINUM_GENERATED_LOOKUP_IMPL
#include "generated/vars_flags.h"

#include "bg_window.h"
#include "constants/field/window.h"
#include "constants/contests.h"
#include "constants/field_base_tiles.h"
#include "constants/map_object.h"
#include "constants/heap.h"
#include "constants/menu.h"
#include "field/field_system.h"
#include "field_map_change.h"
#include "field_message.h"
#include "field_system.h"
#include "field_task.h"
#include "font.h"
#include "game_options.h"
#include "generated/map_headers.h"
#include "generated/pokemon_contest_ranks.h"
#include "generated/pokemon_contest_types.h"
#include "heap.h"
#include "list_menu.h"
#include "location.h"
#include "menu.h"
#include "render_window.h"
#include "save_player.h"
#include "savedata.h"
#include "sound_playback.h"
#include "string_gf.h"
#include "string_list.h"
#include "system.h"
#include "text.h"
#include "vars_flags.h"

#include "../../../include/charcode.h"
#include "../../../include/endpoint.h"
#include "../../../include/client.h"

extern int openmmo_underground_enter(FieldSystem *fs, FieldTask *caller);
extern int openmmo_underground_leave(FieldSystem *fs, FieldTask *caller);
extern int openmmo_underground_active(void);
extern int openmmo_contest_start(FieldSystem *fs, FieldTask *caller, int rank,
                                 int type, int competition, int slot, int link);
extern int openmmo_contest_party_count(FieldSystem *fs);

#define DEBUG_HEAP        HEAP_ID_FIELD2
#define MENU_FRAME_TILE   (1024 - (18 + 12) - 9)
#define MENU_FRAME_PAL    11
#define DEBUG_LIST_TILE   1

/*
 * One screen of the long list, and the ceiling on how many rows one of them builds. A row is a
 * String out of FIELD2, so this is the number that decides what the menu costs: 128 rows of a
 * ~44-character name is about 12 KB, which that heap carries.
 */
#define DEBUG_LIST_ROWS   7
#define DEBUG_LIST_MAX    128
#define DEBUG_ROW_CHARS   48

/* Short menus (the ones drawn with Menu_ rather than ListMenu_). */
#define DEBUG_MENU_MAX    10

enum DebugScreen {
    SCREEN_ROOT = 0,
    SCREEN_WARP,
    SCREEN_FLAG_GROUPS,
    SCREEN_FLAG_LIST,
    SCREEN_VAR_LIST,
    SCREEN_VAR_EDIT,
    SCREEN_CONTEST_MODE,
    SCREEN_CONTEST_RANK,
    SCREEN_CONTEST_TYPE,
    SCREEN_CONTEST_SLOT,
    SCREEN_UNDERGROUND,
    SCREEN_NOTE,
};

enum {
    ROOT_WARP = 0,
    ROOT_FLAGS,
    ROOT_CONTEST,
    ROOT_UNDERGROUND,
    ROOT_CLOSE,
    ROOT_ROWS
};

/*
 * Where a tester wants to stand. `warpId` is the destination's own warp-event index, which is
 * how the engine's own door code names an arrival tile (field_map_change.c:226), more
 * durable than a pair of coordinates copied out of a map.
 */
typedef struct {
    const char *label;
    int header;
    int warpId;
    int x, z;
} DebugWarp;

static const DebugWarp WARPS[] = {
    /* The Super Contest door. Warp 0 of the lobby is its own front door onto
     * Hearthome, which is where a player walking in arrives. */
    { "Contest Hall Lobby", MAP_HEADER_CONTEST_HALL_LOBBY, 0, 0, 0 },
    /* The room the lobby's receptionist sends you through. Standing here
     * without the lobby's script having run is how the stage is measured. */
    { "Contest Hall Stage", MAP_HEADER_CONTEST_HALL_STAGE_NO_CONTEST, 0, 0, 0 },
    { "Hearthome City", MAP_HEADER_HEARTHOME_CITY, 0, 0, 0 },
    /* Where the Explorer Kit is given out, so the official way in can be
     * walked as well as skipped. */
    { "Underground Man's House", MAP_HEADER_ETERNA_CITY_UNDERGROUND_MAN_HOUSE, 0, 0, 0 },
    { "Eterna City", MAP_HEADER_ETERNA_CITY, 0, 0, 0 },
    { "Jubilife City", MAP_HEADER_JUBILIFE_CITY, 0, 0, 0 },
    { "Twinleaf Town", MAP_HEADER_TWINLEAF_TOWN, 0, 0, 0 },
};

#define WARPS_N ((int)(sizeof WARPS / sizeof WARPS[0]))

/* A group of the generated name table, chosen by substring. The table is
 * sorted by name and holds flags and vars together, so `want_var` is what
 * separates the two halves and the substring is what makes a list short
 * enough to read. NULL matches everything. */
typedef struct {
    const char *label;
    const char *match;
    int want_var;
} DebugGroup;

static const DebugGroup GROUPS[] = {
    { "Contest flags", "CONTEST", 0 },
    { "Underground flags", "UNDERGROUND", 0 },
    { "Secret base flags", "SECRET_BASE", 0 },
    { "Badge / gym flags", "BADGE", 0 },
    { "All flags", NULL, 0 },
    { "Contest vars", "CONTEST", 1 },
    { "Underground vars", "UNDERGROUND", 1 },
    { "All vars", NULL, 1 },
};

#define GROUPS_N ((int)(sizeof GROUPS / sizeof GROUPS[0]))

/* What a contest may be started as. */
typedef struct {
    const char *label;
    int competition;
    int rank;
    int type;
} DebugCompetition;

static const DebugCompetition COMPETITIONS[] = {
    { "Super Contest (all 3)", CONTEST_COMPETITION_LINK_OR_OFFICIAL, -1, -1 },
    { "Acting practice", CONTEST_COMPETITION_PRACTICE_ACTING, CONTEST_RANK_NORMAL, -1 },
    { "Dance practice", CONTEST_COMPETITION_PRACTICE_DANCE, CONTEST_RANK_NORMAL, CONTEST_TYPE_COOL },
    { "Visual practice", CONTEST_COMPETITION_PRACTICE_VISUAL, CONTEST_RANK_NORMAL, -1 },
};

#define COMPETITIONS_N ((int)(sizeof COMPETITIONS / sizeof COMPETITIONS[0]))

static const char *const RANKS[] = { "Normal", "Great", "Ultra", "Master" };
#define RANKS_N ((int)(sizeof RANKS / sizeof RANKS[0]))

static const char *const TYPES[] = { "Cool", "Beauty", "Cute", "Smart", "Tough" };
#define TYPES_N ((int)(sizeof TYPES / sizeof TYPES[0]))

enum {
    UG_ENTER = 0,
    UG_LEAVE,
    UG_GIVE_KIT,
    UG_BACK,
    UG_ROWS
};

typedef struct {
    /* The short menu, and the long list; only one is up at a time. */
    Window *menuWin;
    Menu *menu;
    Window listWin;
    ListMenu *list;
    int listAdded;

    StringList *choices;
    String *rowStr[DEBUG_LIST_MAX];
    int rowCount;

    Window msgWin;
    String *msgStr;
    u8 printer;
    u8 msgAdded;

    u8 screen;
    u8 doomed;         /* a screen would not draw; end the task this frame */
    u8 group;          /* which DebugGroup the list is showing */
    u16 listPos;       /* kept across a rebuild so a toggle does not scroll away */
    u16 cursorPos;

    /* The ids the current list's rows stand for, parallel to rowStr. */
    u16 rowId[DEBUG_LIST_MAX];
    int truncated;

    /* The contest being assembled, one menu at a time. */
    int comp, rank, type, slot;

    /* The var being edited, and the value being dialled. */
    int varId;
    int varValue;

    char note[64];
} DebugMenu;

static openmmo_client *s_client;
static DebugMenu *s_live;
static int s_want_open;

/* Built once, on the first open: the generated table is sorted by name, and a
 * list wants its rows in id order. */
static const char *s_flagName[NUM_FLAGS];
static const char *s_varName[NUM_VARS];
static int s_namesBuilt;

void openmmo_debug_attach(openmmo_client *c)
{
    s_client = c;
    s_live = NULL;
    s_want_open = 0;
}

static int debug_enabled(void)
{
    /* Two conditions, and the variable is the weaker of them. */
    const char *v = openmmo_dev_env("OPENMMO_DEBUG_MENU");

    if (v == NULL || v[0] == '\0')
        return 0;
    return v[0] != '0';
}

/* Select was pressed on a frame the field owned. The menu itself is a field
 * task, and a task cannot be started from inside the input hook, so the press
 * is latched and openmmo_debug_try_open picks it up on a settled frame. */
void openmmo_debug_request_open(void)
{
    if (!debug_enabled())
        return;
    s_want_open = 1;
}

static String *utf8_string(const char *s)
{
    mmo_charcode buf[DEBUG_ROW_CHARS + 1];
    String *out = String_Init(DEBUG_ROW_CHARS + 1, DEBUG_HEAP);

    if (out == NULL)
        return NULL;
    mmo_utf8_to_charcode(s != NULL ? s : "", buf, DEBUG_ROW_CHARS + 1);
    String_CopyChars(out, (const charcode_t *)buf);
    return out;
}

/* Index the generated table by id. A row whose name begins FLAG_ is a flag and
 * one that begins VAR_ is a var; every other row (DAILY_FLAGS_START,
 * VARS_END, the MAP_LOCAL_ bounds) is a marker sharing an id with a real
 * entry, and taking it would name a flag after its own boundary. */
static void build_names(void)
{
    long i;

    if (s_namesBuilt)
        return;
    s_namesBuilt = 1;
    memset(s_flagName, 0, sizeof s_flagName);
    memset(s_varName, 0, sizeof s_varName);

    for (i = 0; i < lengthof__VarFlag; i++) {
        const char *name = lookup__VarFlag[i].def;
        long id = lookup__VarFlag[i].value;

        if (name == NULL)
            continue;
        if (strncmp(name, "FLAG_", 5) == 0) {
            if (id >= 0 && id < NUM_FLAGS && s_flagName[id] == NULL)
                s_flagName[id] = name;
        } else if (strncmp(name, "VAR_", 4) == 0) {
            long slot = id - VARS_START;

            if (slot >= 0 && slot < NUM_VARS && s_varName[slot] == NULL)
                s_varName[slot] = name;
        }
    }
}

/* FLAG_RECEIVED_EXPLORER_KIT reads better as RECEIVED_EXPLORER_KIT when every
 * row on the screen starts the same way and the window is 26 tiles wide. */
static const char *short_name(const char *name)
{
    if (name == NULL)
        return "";
    if (strncmp(name, "FLAG_", 5) == 0)
        return name + 5;
    if (strncmp(name, "VAR_", 4) == 0)
        return name + 4;
    return name;
}

static int name_matches(const char *name, const char *match)
{
    if (match == NULL)
        return 1;
    return name != NULL && strstr(name, match) != NULL;
}

static VarsFlags *vars_flags(FieldSystem *fs)
{
    if (fs == NULL || fs->saveData == NULL)
        return NULL;
    return SaveData_GetVarsFlags(fs->saveData);
}

static void rows_free(DebugMenu *d)
{
    int i;

    for (i = 0; i < DEBUG_LIST_MAX; i++) {
        if (d->rowStr[i] != NULL)
            String_Free(d->rowStr[i]);
        d->rowStr[i] = NULL;
    }
    d->rowCount = 0;
}

/* Take down whatever widget is up, leaving the struct usable for the next
 * screen. Called between screens as well as at the end, so it must be exact
 * about what it has already released. */
static void widgets_free(DebugMenu *d)
{
    if (d->menu != NULL) {
        /* Menu_DestroyForExit takes the window with it. */
        Menu_DestroyForExit(d->menu, DEBUG_HEAP);
        d->menu = NULL;
        d->menuWin = NULL;
        d->choices = NULL;
    } else if (d->menuWin != NULL) {
        Window_EraseStandardFrame(d->menuWin, 0);
        Window_Remove(d->menuWin);
        Heap_FreeExplicit(DEBUG_HEAP, d->menuWin);
        d->menuWin = NULL;
    }
    if (d->list != NULL) {
        ListMenu_Free(d->list, NULL, NULL);
        d->list = NULL;
    }
    if (d->listAdded) {
        Window_EraseStandardFrame(&d->listWin, TRUE);
        Window_ClearAndCopyToVRAM(&d->listWin);
        Window_Remove(&d->listWin);
        d->listAdded = 0;
    }
    if (d->choices != NULL) {
        StringList_Free(d->choices);
        d->choices = NULL;
    }
    if (d->msgAdded) {
        Window_EraseMessageBox(&d->msgWin, 0);
        Window_Remove(&d->msgWin);
        d->msgAdded = 0;
    }
    if (d->msgStr != NULL) {
        String_Free(d->msgStr);
        d->msgStr = NULL;
    }
    rows_free(d);
}

static void debug_free(DebugMenu *d)
{
    if (d == NULL)
        return;
    widgets_free(d);
    if (s_live == d)
        s_live = NULL;
    Heap_Free(d);
}

/* A short framed list, bottom-right, the shape openmmo_player.c uses. */
static int paint_menu(FieldSystem *fs, DebugMenu *d, const char *const *labels,
                      int n, int width)
{
    MenuTemplate tmpl;
    WindowTemplate wt;
    int i;

    if (n < 1)
        return 0;
    if (n > DEBUG_MENU_MAX)
        n = DEBUG_MENU_MAX;

    LoadStandardWindowGraphics(fs->bgConfig, BG_LAYER_MAIN_3,
                               MENU_FRAME_TILE, MENU_FRAME_PAL,
                               STANDARD_WINDOW_SYSTEM, DEBUG_HEAP);

    d->choices = StringList_New((u32)n, DEBUG_HEAP);
    if (d->choices == NULL)
        return 0;
    for (i = 0; i < n; i++) {
        d->rowStr[i] = utf8_string(labels[i]);
        if (d->rowStr[i] == NULL)
            return 0;
        StringList_AddFromString(d->choices, d->rowStr[i], (u32)i);
    }
    d->rowCount = n;

    memset(&wt, 0, sizeof wt);
    wt.bgLayer = BG_LAYER_MAIN_3;
    wt.tilemapLeft = (u8)(31 - width);
    wt.tilemapTop = (u8)(19 - 2 * n);
    wt.width = (u8)width;
    wt.height = (u8)(2 * n);
    wt.palette = FIELD_MESSAGE_PALETTE_INDEX;
    /* Downward from the message window's own base, the same reservation
     * openmmo_dialog.c makes: a list this tall would otherwise run straight
     * through the field message window's tiles underneath it. */
    wt.baseTile = (u16)(BASE_TILE_MESSAGE_WINDOW - wt.width * wt.height);

    d->menuWin = Window_New(DEBUG_HEAP, 1);
    if (d->menuWin == NULL)
        return 0;
    Window_AddFromTemplate(fs->bgConfig, d->menuWin, &wt);
    Window_FillTilemap(d->menuWin, 15);
    Window_DrawStandardFrame(d->menuWin, 1, MENU_FRAME_TILE, MENU_FRAME_PAL);

    memset(&tmpl, 0, sizeof tmpl);
    tmpl.choices = d->choices;
    tmpl.window = d->menuWin;
    tmpl.fontID = FONT_SYSTEM;
    tmpl.xSize = 1;
    tmpl.ySize = (u8)n;
    tmpl.lineSpacing = 0;
    tmpl.suppressCursor = 0;
    tmpl.loopAround = 1;
    d->menu = Menu_NewAndCopyToVRAM(&tmpl, 8, 0, 0, DEBUG_HEAP, PAD_BUTTON_B);
    if (d->menu == NULL)
        return 0;
    /* Menu_ owns the list now, and Menu_DestroyForExit frees it. */
    d->choices = NULL;
    return 1;
}

/* The long, scrolling list. Rows are built into rowStr and their ids kept in
 * rowId, so a pick knows which flag it landed on without parsing the label
 * back out. */
static int paint_list(FieldSystem *fs, DebugMenu *d)
{
    const DebugGroup *g = &GROUPS[d->group];
    VarsFlags *vf = vars_flags(fs);
    ListMenuTemplate tmpl;
    char line[DEBUG_ROW_CHARS + 1];
    int id, n = 0;

    if (vf == NULL)
        return 0;
    build_names();
    d->truncated = 0;

    d->choices = StringList_New(DEBUG_LIST_MAX, DEBUG_HEAP);
    if (d->choices == NULL)
        return 0;

    if (!g->want_var) {
        for (id = 0; id < NUM_FLAGS; id++) {
            const char *name = s_flagName[id];

            if (name == NULL || !name_matches(name, g->match))
                continue;
            if (n >= DEBUG_LIST_MAX) {
                d->truncated = 1;
                break;
            }
            snprintf(line, sizeof line, "%c %s",
                     VarsFlags_CheckFlag(vf, (u16)id) ? '*' : '-',
                     short_name(name));
            d->rowStr[n] = utf8_string(line);
            if (d->rowStr[n] == NULL)
                return 0;
            d->rowId[n] = (u16)id;
            StringList_AddFromString(d->choices, d->rowStr[n], (u32)n);
            n++;
        }
    } else {
        for (id = 0; id < NUM_VARS; id++) {
            const char *name = s_varName[id];

            if (name == NULL || !name_matches(name, g->match))
                continue;
            if (n >= DEBUG_LIST_MAX) {
                d->truncated = 1;
                break;
            }
            snprintf(line, sizeof line, "%u %s",
                     (unsigned)vf->vars[id], short_name(name));
            d->rowStr[n] = utf8_string(line);
            if (d->rowStr[n] == NULL)
                return 0;
            d->rowId[n] = (u16)id;
            StringList_AddFromString(d->choices, d->rowStr[n], (u32)n);
            n++;
        }
    }
    d->rowCount = n;

    if (n == 0) {
        /* An empty list has nothing to cursor over and ListMenu_New would be
         * asked for a count of zero. Say so on the message line instead. */
        StringList_Free(d->choices);
        d->choices = NULL;
        return 0;
    }
    if (d->listPos >= (u16)n)
        d->listPos = 0;
    if (d->cursorPos >= (u16)n)
        d->cursorPos = 0;

    Window_Add(fs->bgConfig, &d->listWin, BG_LAYER_MAIN_3,
               1, 1, 28, DEBUG_LIST_ROWS * 2, 13, DEBUG_LIST_TILE);
    d->listAdded = 1;
    LoadStandardWindowGraphics(fs->bgConfig, BG_LAYER_MAIN_3,
                               MENU_FRAME_TILE, MENU_FRAME_PAL,
                               STANDARD_WINDOW_SYSTEM, DEBUG_HEAP);
    Window_DrawStandardFrame(&d->listWin, 1, MENU_FRAME_TILE, MENU_FRAME_PAL);
    Window_FillTilemap(&d->listWin, 15);

    memset(&tmpl, 0, sizeof tmpl);
    tmpl.choices = d->choices;
    tmpl.window = &d->listWin;
    tmpl.count = (u16)n;
    tmpl.maxDisplay = (u16)(n < DEBUG_LIST_ROWS ? n : DEBUG_LIST_ROWS);
    tmpl.textXOffset = 8;
    tmpl.textColorFg = 1;
    tmpl.textColorBg = 15;
    tmpl.textColorShadow = 2;
    tmpl.fontID = FONT_SYSTEM;
    tmpl.cursorType = 0;
    tmpl.pagerMode = PAGER_MODE_SHOULDER_BUTTONS;
    d->list = ListMenu_New(&tmpl, d->listPos, d->cursorPos, DEBUG_HEAP);
    if (d->list == NULL)
        return 0;
    Window_CopyToVRAM(&d->listWin);
    return 1;
}

static int paint_message(FieldSystem *fs, DebugMenu *d, const char *text)
{
    const Options *options = SaveData_GetOptions(fs->saveData);

    if (d->msgAdded) {
        Window_EraseMessageBox(&d->msgWin, 0);
        Window_Remove(&d->msgWin);
        d->msgAdded = 0;
    }
    if (d->msgStr != NULL) {
        String_Free(d->msgStr);
        d->msgStr = NULL;
    }
    d->msgStr = utf8_string(text);
    if (d->msgStr == NULL)
        return 0;
    FieldMessage_AddWindow(fs->bgConfig, &d->msgWin, BG_LAYER_MAIN_3);
    FieldMessage_DrawWindow(&d->msgWin, options);
    d->msgAdded = 1;
    d->printer = FieldMessage_Print(&d->msgWin, d->msgStr, options, 1);
    return 1;
}

/* The var editor is a message line and the pad: up/down step by one, left and
 * right by ten, A writes, B leaves the var as it was. A menu of deltas was the
 * alternative and it needs four presses to reach 30. */
static void var_edit_line(DebugMenu *d, char *out, size_t cap)
{
    const char *name = (d->varId >= 0 && d->varId < NUM_VARS)
                           ? short_name(s_varName[d->varId]) : "";

    snprintf(out, cap, "%s = %d\nUp/Down 1, Left/Right 10, A sets.",
             name[0] != '\0' ? name : "VAR", d->varValue);
}

static void screen_enter(FieldSystem *fs, DebugMenu *d, int screen);

/* Every screen is built the same way: tear the last one down, then paint. A
 * paint that fails takes the whole menu down rather than leaving a half-drawn
 * frame on the field. */
static int paint_screen(FieldSystem *fs, DebugMenu *d)
{
    const char *labels[DEBUG_MENU_MAX];
    char line[80];
    int i, n;

    switch (d->screen) {
    case SCREEN_ROOT:
        labels[ROOT_WARP] = "Warp";
        labels[ROOT_FLAGS] = "Story flags";
        labels[ROOT_CONTEST] = "Contest";
        labels[ROOT_UNDERGROUND] = "Underground";
        labels[ROOT_CLOSE] = "Close";
        return paint_menu(fs, d, labels, ROOT_ROWS, 14);

    case SCREEN_WARP:
        n = WARPS_N < DEBUG_MENU_MAX ? WARPS_N : DEBUG_MENU_MAX;
        for (i = 0; i < n; i++)
            labels[i] = WARPS[i].label;
        return paint_menu(fs, d, labels, n, 22);

    case SCREEN_FLAG_GROUPS:
        n = GROUPS_N < DEBUG_MENU_MAX ? GROUPS_N : DEBUG_MENU_MAX;
        for (i = 0; i < n; i++)
            labels[i] = GROUPS[i].label;
        return paint_menu(fs, d, labels, n, 20);

    case SCREEN_FLAG_LIST:
    case SCREEN_VAR_LIST:
        if (!paint_list(fs, d)) {
            snprintf(line, sizeof line, "%s: nothing to show.",
                     GROUPS[d->group].label);
            return paint_message(fs, d, line);
        }
        if (d->truncated)
            snprintf(line, sizeof line, "%s (first %d). A toggles.",
                     GROUPS[d->group].label, d->rowCount);
        else if (GROUPS[d->group].want_var)
            snprintf(line, sizeof line, "%s: A edits, B backs out.",
                     GROUPS[d->group].label);
        else
            snprintf(line, sizeof line, "%s: A toggles, B backs out.",
                     GROUPS[d->group].label);
        return paint_message(fs, d, line);

    case SCREEN_VAR_EDIT:
        var_edit_line(d, line, sizeof line);
        return paint_message(fs, d, line);

    case SCREEN_CONTEST_MODE:
        n = COMPETITIONS_N < DEBUG_MENU_MAX ? COMPETITIONS_N : DEBUG_MENU_MAX;
        for (i = 0; i < n; i++)
            labels[i] = COMPETITIONS[i].label;
        return paint_menu(fs, d, labels, n, 22);

    case SCREEN_CONTEST_RANK:
        for (i = 0; i < RANKS_N; i++)
            labels[i] = RANKS[i];
        return paint_menu(fs, d, labels, RANKS_N, 12);

    case SCREEN_CONTEST_TYPE:
        for (i = 0; i < TYPES_N; i++)
            labels[i] = TYPES[i];
        return paint_menu(fs, d, labels, TYPES_N, 12);

    case SCREEN_CONTEST_SLOT: {
        static char slotLabel[6][12];

        n = openmmo_contest_party_count(fs);
        if (n <= 0) {
            return paint_message(fs, d,
                                 "No Pokemon in the party to enter.");
        }
        if (n > 6)
            n = 6;
        for (i = 0; i < n; i++) {
            snprintf(slotLabel[i], sizeof slotLabel[i], "Slot %d", i + 1);
            labels[i] = slotLabel[i];
        }
        return paint_menu(fs, d, labels, n, 12);
    }

    case SCREEN_UNDERGROUND:
        labels[UG_ENTER] = "Enter";
        labels[UG_LEAVE] = "Leave";
        labels[UG_GIVE_KIT] = "Set Explorer Kit flag";
        labels[UG_BACK] = "Back";
        return paint_menu(fs, d, labels, UG_ROWS, 22);

    case SCREEN_NOTE:
        return paint_message(fs, d, d->note);

    default:
        return 0;
    }
}

/*
 * Tear the last screen down and paint the next. A paint that will not allocate falls back to
 * the root, and a root that will not allocate either sets `doomed`, which the task reads on
 * its way out of the frame.
 */
static void screen_enter(FieldSystem *fs, DebugMenu *d, int screen)
{
    widgets_free(d);
    d->screen = (u8)screen;
    if (paint_screen(fs, d))
        return;
    printf("openmmo: debug menu would not draw screen %d\n", screen);
    d->screen = SCREEN_ROOT;
    if (paint_screen(fs, d))
        return;
    widgets_free(d);
    d->doomed = 1;
}

static void note(FieldSystem *fs, DebugMenu *d, const char *text)
{
    snprintf(d->note, sizeof d->note, "%s", text);
    screen_enter(fs, d, SCREEN_NOTE);
}

/* A warp the tester asked for. */
static void do_warp(FieldTask *task, const DebugWarp *w)
{
    printf("openmmo: debug warp to header %d (%s)\n", w->header, w->label);
    FieldTask_StartMapChangeFull(task, (enum MapHeaderID)w->header,
                                 w->warpId, w->x, w->z, DIR_SOUTH);
}

static void toggle_flag(FieldSystem *fs, DebugMenu *d, int id)
{
    VarsFlags *vf = vars_flags(fs);
    int on;

    if (vf == NULL)
        return;
    on = VarsFlags_CheckFlag(vf, (u16)id);
    if (on)
        VarsFlags_ClearFlag(vf, (u16)id);
    else
        VarsFlags_SetFlag(vf, (u16)id);
    printf("openmmo: debug flag %d (%s) %s\n", id,
           s_flagName[id] != NULL ? s_flagName[id] : "?",
           on ? "cleared" : "set");
}

static BOOL debug_task(FieldTask *task)
{
    FieldSystem *fs = FieldTask_GetFieldSystem(task);
    DebugMenu *d = FieldTask_GetEnv(task);
    u32 choice;
    char line[80];

    switch (task->state) {
    case 0:
        d->screen = SCREEN_ROOT;
        if (!paint_screen(fs, d)) {
            printf("openmmo: debug menu would not allocate\n");
            debug_free(d);
            return TRUE;
        }
        task->state = 1;
        break;

    case 1:
        /* A screen made of a message alone (a note, the var editor, an empty
         * list) has no menu to poll; it waits for the line to finish and then
         * reads the pad itself. */
        if (d->menu == NULL && d->list == NULL) {
            if (FieldMessage_FinishedPrinting(d->printer) != TRUE)
                break;
            if (d->screen == SCREEN_VAR_EDIT) {
                u32 rep = gSystem.pressedKeysRepeatable;
                u32 hit = gSystem.pressedKeys;
                int before = d->varValue;

                if (rep & PAD_KEY_UP)
                    d->varValue++;
                if (rep & PAD_KEY_DOWN)
                    d->varValue--;
                if (rep & PAD_KEY_RIGHT)
                    d->varValue += 10;
                if (rep & PAD_KEY_LEFT)
                    d->varValue -= 10;
                if (d->varValue < 0)
                    d->varValue = 0;
                if (d->varValue > 65535)
                    d->varValue = 65535;
                if (d->varValue != before) {
                    var_edit_line(d, line, sizeof line);
                    paint_message(fs, d, line);
                    break;
                }
                if (hit & PAD_BUTTON_A) {
                    VarsFlags *vf = vars_flags(fs);

                    if (vf != NULL && d->varId >= 0 && d->varId < NUM_VARS) {
                        vf->vars[d->varId] = (u16)d->varValue;
                        printf("openmmo: debug var %d (%s) = %d\n",
                               VARS_START + d->varId,
                               s_varName[d->varId] != NULL
                                   ? s_varName[d->varId] : "?",
                               d->varValue);
                    }
                    Sound_PlayEffect(SE_CONFIRM_sseq_3);
                    screen_enter(fs, d, SCREEN_VAR_LIST);
                    break;
                }
                if (hit & PAD_BUTTON_B) {
                    screen_enter(fs, d, SCREEN_VAR_LIST);
                    break;
                }
                break;
            }
            /* A note, or a list with no rows: any key goes back. */
            if (gSystem.pressedKeys & (PAD_BUTTON_A | PAD_BUTTON_B)) {
                if (d->screen == SCREEN_FLAG_LIST
                    || d->screen == SCREEN_VAR_LIST)
                    screen_enter(fs, d, SCREEN_FLAG_GROUPS);
                else if (d->screen == SCREEN_CONTEST_SLOT)
                    screen_enter(fs, d, SCREEN_ROOT);
                else
                    screen_enter(fs, d, SCREEN_ROOT);
            }
            break;
        }

        if (d->list != NULL) {
            choice = ListMenu_ProcessInput(d->list);
            if (choice == (u32)MENU_NOTHING_CHOSEN)
                break;
            if (choice == (u32)MENU_CANCEL) {
                Sound_PlayEffect(SE_CONFIRM_sseq_3);
                d->listPos = 0;
                d->cursorPos = 0;
                screen_enter(fs, d, SCREEN_FLAG_GROUPS);
                break;
            }
            {
                int row = (int)choice;

                if (row < 0 || row >= d->rowCount)
                    break;
                Sound_PlayEffect(SE_CONFIRM_sseq_3);
                ListMenu_GetListAndCursorPos(d->list, &d->listPos,
                                             &d->cursorPos);
                if (d->screen == SCREEN_FLAG_LIST) {
                    toggle_flag(fs, d, d->rowId[row]);
                    /* Rebuilt rather than repainted: the row's own text
                     * carries the state, and the scroll position is kept
                     * above so the cursor comes back where it was. */
                    screen_enter(fs, d, SCREEN_FLAG_LIST);
                } else {
                    VarsFlags *vf = vars_flags(fs);

                    d->varId = d->rowId[row];
                    d->varValue = (vf != NULL) ? vf->vars[d->varId] : 0;
                    screen_enter(fs, d, SCREEN_VAR_EDIT);
                }
            }
            break;
        }

        choice = Menu_ProcessInput(d->menu);
        if (choice == (u32)MENU_NOTHING_CHOSEN)
            break;
        Sound_PlayEffect(SE_CONFIRM_sseq_3);

        if (choice == (u32)MENU_CANCEL) {
            switch (d->screen) {
            case SCREEN_ROOT:
                printf("openmmo: debug menu closed\n");
                debug_free(d);
                return TRUE;
            case SCREEN_FLAG_LIST:
            case SCREEN_VAR_LIST:
                screen_enter(fs, d, SCREEN_FLAG_GROUPS);
                break;
            case SCREEN_CONTEST_RANK:
            case SCREEN_CONTEST_TYPE:
            case SCREEN_CONTEST_SLOT:
                screen_enter(fs, d, SCREEN_CONTEST_MODE);
                break;
            default:
                screen_enter(fs, d, SCREEN_ROOT);
                break;
            }
            break;
        }

        switch (d->screen) {
        case SCREEN_ROOT:
            switch ((int)choice) {
            case ROOT_WARP:
                screen_enter(fs, d, SCREEN_WARP);
                break;
            case ROOT_FLAGS:
                screen_enter(fs, d, SCREEN_FLAG_GROUPS);
                break;
            case ROOT_CONTEST:
                screen_enter(fs, d, SCREEN_CONTEST_MODE);
                break;
            case ROOT_UNDERGROUND:
                screen_enter(fs, d, SCREEN_UNDERGROUND);
                break;
            default:
                printf("openmmo: debug menu closed\n");
                debug_free(d);
                return TRUE;
            }
            break;

        case SCREEN_WARP:
            if ((int)choice >= 0 && (int)choice < WARPS_N) {
                /*
                 * The widgets go before the map change starts: the map is about to be torn
                 * down and nothing would be left to erase them against. The menu itself does
                 * Not go, the map change is a task pushed in front of this one and this one
                 * is what it returns to.
                 */
                widgets_free(d);
                do_warp(task, &WARPS[choice]);
                task->state = 2;
                return FALSE;
            }
            break;

        case SCREEN_FLAG_GROUPS:
            if ((int)choice >= 0 && (int)choice < GROUPS_N) {
                d->group = (u8)choice;
                d->listPos = 0;
                d->cursorPos = 0;
                screen_enter(fs, d,
                             GROUPS[d->group].want_var ? SCREEN_VAR_LIST
                                                       : SCREEN_FLAG_LIST);
            }
            break;

        case SCREEN_CONTEST_MODE:
            if ((int)choice >= 0 && (int)choice < COMPETITIONS_N) {
                const DebugCompetition *c = &COMPETITIONS[choice];

                d->comp = (int)choice;
                /* A screen the official client does not show is a screen this does not
                 * show either: a practice contest has no rank to pick, and
                 * dance practice has no type. */
                d->rank = (c->rank >= 0) ? c->rank : 0;
                d->type = (c->type >= 0) ? c->type : 0;
                if (c->rank < 0)
                    screen_enter(fs, d, SCREEN_CONTEST_RANK);
                else if (c->type < 0)
                    screen_enter(fs, d, SCREEN_CONTEST_TYPE);
                else
                    screen_enter(fs, d, SCREEN_CONTEST_SLOT);
            }
            break;

        case SCREEN_CONTEST_RANK:
            if ((int)choice >= 0 && (int)choice < RANKS_N) {
                d->rank = (int)choice;
                if (COMPETITIONS[d->comp].type < 0)
                    screen_enter(fs, d, SCREEN_CONTEST_TYPE);
                else
                    screen_enter(fs, d, SCREEN_CONTEST_SLOT);
            }
            break;

        case SCREEN_CONTEST_TYPE:
            if ((int)choice >= 0 && (int)choice < TYPES_N) {
                d->type = (int)choice;
                screen_enter(fs, d, SCREEN_CONTEST_SLOT);
            }
            break;

        case SCREEN_CONTEST_SLOT:
            d->slot = (int)choice;
            widgets_free(d);
            if (openmmo_contest_start(fs, task, d->rank, d->type,
                                      COMPETITIONS[d->comp].competition,
                                      d->slot, 0)) {
                printf("openmmo: debug contest %s, %s rank, %s, slot %d\n",
                       COMPETITIONS[d->comp].label, RANKS[d->rank],
                       TYPES[d->type], d->slot + 1);
                task->state = 2;
                return FALSE;
            }
            note(fs, d, "The contest would not start.");
            break;

        case SCREEN_UNDERGROUND:
            switch ((int)choice) {
            case UG_ENTER:
                widgets_free(d);
                if (openmmo_underground_enter(fs, task)) {
                    task->state = 2;
                    return FALSE;
                }
                note(fs, d, "Cannot go underground from here.");
                break;
            case UG_LEAVE:
                widgets_free(d);
                if (openmmo_underground_leave(fs, task)) {
                    task->state = 2;
                    return FALSE;
                }
                note(fs, d, "Not underground.");
                break;
            case UG_GIVE_KIT: {
                VarsFlags *vf = vars_flags(fs);

                if (vf != NULL) {
                    VarsFlags_SetFlag(vf, FLAG_RECEIVED_EXPLORER_KIT);
                    printf("openmmo: debug set FLAG_RECEIVED_EXPLORER_KIT\n");
                }
                note(fs, d, "Explorer Kit flag set.");
                break;
            }
            default:
                screen_enter(fs, d, SCREEN_ROOT);
                break;
            }
            break;

        default:
            screen_enter(fs, d, SCREEN_ROOT);
            break;
        }
        break;

    case 2:
        /* A warp, a contest or an underground trip was pushed in front of
         * this task and has now returned. The menu is over: everything it
         * drew was erased before the call, and the map under it is not the
         * one it was drawn on. */
        debug_free(d);
        return TRUE;
    }

    if (d->doomed) {
        printf("openmmo: debug menu closed (nothing would draw)\n");
        debug_free(d);
        return TRUE;
    }
    return FALSE;
}

int openmmo_debug_try_open(FieldSystem *fs)
{
    DebugMenu *d;

    if (!s_want_open)
        return 0;
    if (fs == NULL)
        return 0;
    if (s_live != NULL) {
        s_want_open = 0;
        return 0;
    }
    if (fs->task != NULL || !FieldSystem_IsRunningFieldMap(fs)
        || FieldSystem_HasChildProcess(fs))
        return 0; /* keep the press; the next idle frame opens it */

    s_want_open = 0;
    d = Heap_Alloc(DEBUG_HEAP, sizeof(DebugMenu));
    if (d == NULL) {
        printf("openmmo: debug menu would not allocate\n");
        return 0;
    }
    memset(d, 0, sizeof(*d));
    d->varId = -1;
    s_live = d;
    FieldSystem_CreateTask(fs, debug_task, d);
    printf("openmmo: debug menu opened\n");
    return 1;
}

/*
 * The frame that did not fit. The 3D engine holds 2048 polygons and 6144 vertices a frame, and
 * a polygon past that is dropped: the port counts it (pc_gpu3d_ram_overflows,
 * pc/hw/pc_gpu3d.c).
 */
extern unsigned long pc_gpu3d_ram_overflows;

void openmmo_poly_overflow_tick(void)
{
    static unsigned quiet, frames, reports;
    static unsigned long seen;

    frames++;
    if (pc_gpu3d_ram_overflows != seen) {
        seen = pc_gpu3d_ram_overflows;
        if (quiet == 0) {
            printf("openmmo: the 3D engine's polygon or vertex RAM overflowed "
                   "(frame %u, %lu polygon(s) dropped so far, report %u); "
                   "what was drawn last is missing\n",
                   frames, seen, ++reports);
            quiet = 60;
        }
    }
    if (quiet > 0)
        quiet--;
}
