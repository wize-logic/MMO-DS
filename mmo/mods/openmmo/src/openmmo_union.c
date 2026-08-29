/* The communication club's join list, radio-free. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bg_window.h"
#include "constants/graphics.h"
#include "constants/heap.h"
#include "constants/menu.h"
#include "constants/string.h"
#include "field/field_system.h"
#include "field_message.h"
#include "field_system.h"
#include "field_task.h"
#include "font.h"
#include "game_options.h"
#include "heap.h"
#include "list_menu.h"
#include "render_window.h"
#include "save_player.h"
#include "sound_playback.h"
#include "string_gf.h"
#include "string_list.h"
#include "system.h"
#include "text.h"
#include "trainer_info.h"

#include "../../../include/charcode.h"
#include "../../../include/client.h"

#define UNION_HEAP          HEAP_ID_FIELD2
#define UNION_SLOTS         OPENMMO_LINK_MAX
#define UNION_FRAME_TILE    (1024 - (18 + 12) - 9)
#define UNION_FRAME_PAL     11
#define UNION_LIST_TILE     1
#define UNION_INFO_TILE     (1 + 20 * 5 * 2)

typedef struct {
    Window listWin;
    Window infoWin;
    Window msgWin;
    StringList *choices;
    ListMenu *menu;
    String *rowStr[UNION_SLOTS];
    String *msgStr;
    int rowCount;
    int listAdded;
    int infoAdded;
    int msgAdded;
    u8 printer;
    int demo;
    int count;
    int leader_at;
    char name[UNION_SLOTS][MMO_CHAR_NAME_MAX + 1];
} UnionRoom;

static openmmo_client *s_client;
static UnionRoom *s_live;
static int s_armed;
static int s_opened;
static int s_pending;

static int want_open(void)
{
    const char *env = getenv("OPENMMO_UNION");

    if (env == NULL || env[0] == '\0' || env[0] == '0')
        return 0;
    return 1;
}

static String *latin1(enum HeapID heap, const char *s)
{
    mmo_charcode buf[64];
    String *out = String_Init(64, heap);

    if (out == NULL)
        return NULL;
    mmo_utf8_to_charcode(s != NULL ? s : "", buf, 64);
    String_CopyChars(out, (const charcode_t *)buf);
    return out;
}

static void union_free(UnionRoom *u)
{
    int i;

    if (u == NULL)
        return;
    if (u->menu != NULL) {
        ListMenu_Free(u->menu, NULL, NULL);
        u->menu = NULL;
    }
    if (u->choices != NULL) {
        StringList_Free(u->choices);
        u->choices = NULL;
    }
    for (i = 0; i < UNION_SLOTS; i++) {
        if (u->rowStr[i] != NULL)
            String_Free(u->rowStr[i]);
        u->rowStr[i] = NULL;
    }
    if (u->msgStr != NULL) {
        String_Free(u->msgStr);
        u->msgStr = NULL;
    }
    if (u->listAdded) {
        Window_EraseStandardFrame(&u->listWin, TRUE);
        Window_ClearAndCopyToVRAM(&u->listWin);
        Window_Remove(&u->listWin);
        u->listAdded = 0;
    }
    if (u->infoAdded) {
        Window_EraseStandardFrame(&u->infoWin, TRUE);
        Window_ClearAndCopyToVRAM(&u->infoWin);
        Window_Remove(&u->infoWin);
        u->infoAdded = 0;
    }
    if (u->msgAdded) {
        Window_EraseMessageBox(&u->msgWin, 0);
        Window_Remove(&u->msgWin);
        u->msgAdded = 0;
    }
    if (s_live == u)
        s_live = NULL;
    Heap_Free(u);
}

static void fill_from_link(UnionRoom *u, const openmmo_link *link)
{
    int i;

    u->demo = 0;
    u->count = 0;
    u->leader_at = -1;
    if (link == NULL || !link->valid || !link->present || link->count <= 0)
        return;
    u->count = link->count;
    if (u->count > UNION_SLOTS)
        u->count = UNION_SLOTS;
    for (i = 0; i < u->count; i++) {
        strncpy(u->name[i], link->member[i].name, MMO_CHAR_NAME_MAX);
        u->name[i][MMO_CHAR_NAME_MAX] = '\0';
        if (u->name[i][0] == '\0')
            strncpy(u->name[i], "?", sizeof u->name[i]);
        if (link->member[i].entity_id == link->leader)
            u->leader_at = i;
    }
}

static void fill_demo(UnionRoom *u)
{
    u->demo = 1;
    u->count = 2;
    u->leader_at = 0;
    strncpy(u->name[0], "RED", MMO_CHAR_NAME_MAX);
    strncpy(u->name[1], "BLUE", MMO_CHAR_NAME_MAX);
    u->name[0][MMO_CHAR_NAME_MAX] = '\0';
    u->name[1][MMO_CHAR_NAME_MAX] = '\0';
}

static int paint_info(FieldSystem *fs, UnionRoom *u)
{
    TrainerInfo *info;
    String *name;

    Window_Add(fs->bgConfig, &u->infoWin, BG_LAYER_MAIN_3,
               23, 2, 8, 4, 13, UNION_INFO_TILE);
    u->infoAdded = 1;
    LoadStandardWindowGraphics(fs->bgConfig, BG_LAYER_MAIN_3,
                               UNION_FRAME_TILE, UNION_FRAME_PAL,
                               STANDARD_WINDOW_SYSTEM, UNION_HEAP);
    Window_DrawStandardFrame(&u->infoWin, 1, UNION_FRAME_TILE, UNION_FRAME_PAL);
    Window_FillTilemap(&u->infoWin, 15);

    info = (fs->saveData != NULL) ? SaveData_GetTrainerInfo(fs->saveData) : NULL;
    name = NULL;
    if (info != NULL && !TrainerInfo_HasNoName(info)) {
        /* NameNewString's 7-glyph fallback asserts when the field has
         * no CHAR_EOS. A new save's ?????? is that case. Bound the copy. */
        name = String_Init(TRAINER_NAME_LEN + 2, UNION_HEAP);
        if (name != NULL)
            String_CopyNumChars(name, TrainerInfo_Name(info),
                                TRAINER_NAME_LEN + 1);
    }
    if (name == NULL)
        name = latin1(UNION_HEAP, "YOU");
    if (name != NULL) {
        Text_AddPrinterWithParams(&u->infoWin, FONT_SYSTEM, name,
                                  2, 2, TEXT_SPEED_INSTANT, NULL);
        String_Free(name);
    }
    Window_CopyToVRAM(&u->infoWin);
    return 1;
}

static int paint_list(FieldSystem *fs, UnionRoom *u)
{
    ListMenuTemplate tmpl;
    char line[48];
    int i, n;

    n = u->count;
    if (n <= 0)
        n = 1;
    if (n > UNION_SLOTS)
        n = UNION_SLOTS;

    u->choices = StringList_New((u32)n, UNION_HEAP);
    if (u->choices == NULL)
        return 0;

    for (i = 0; i < n; i++) {
        if (i < u->count && u->name[i][0] != '\0') {
            snprintf(line, sizeof line, "%d. %s%s",
                     i + 1, u->name[i],
                     (i == u->leader_at) ? " *" : "");
        } else {
            snprintf(line, sizeof line, "%d. --------", i + 1);
        }
        u->rowStr[i] = latin1(UNION_HEAP, line);
        if (u->rowStr[i] == NULL)
            return 0;
        StringList_AddFromString(u->choices, u->rowStr[i], (u32)i);
        u->rowCount++;
    }

    Window_Add(fs->bgConfig, &u->listWin, BG_LAYER_MAIN_3,
               1, 2, 20, (u8)(n * 2), 13, UNION_LIST_TILE);
    u->listAdded = 1;
    Window_DrawStandardFrame(&u->listWin, 1, UNION_FRAME_TILE, UNION_FRAME_PAL);
    Window_FillTilemap(&u->listWin, 15);

    memset(&tmpl, 0, sizeof tmpl);
    tmpl.choices = u->choices;
    tmpl.window = &u->listWin;
    tmpl.count = (u16)n;
    tmpl.maxDisplay = (u16)n;
    tmpl.textXOffset = 8;
    tmpl.textColorFg = 1;
    tmpl.textColorBg = 15;
    tmpl.textColorShadow = 2;
    tmpl.fontID = FONT_SYSTEM;
    tmpl.cursorType = 0;
    u->menu = ListMenu_New(&tmpl, 0, 0, UNION_HEAP);
    if (u->menu == NULL)
        return 0;
    Window_CopyToVRAM(&u->listWin);
    return 1;
}

static int paint_message(FieldSystem *fs, UnionRoom *u)
{
    const Options *options = SaveData_GetOptions(fs->saveData);

    u->msgStr = latin1(UNION_HEAP, "Choose a friend to join.");
    if (u->msgStr == NULL)
        return 0;
    FieldMessage_AddWindow(fs->bgConfig, &u->msgWin, BG_LAYER_MAIN_3);
    FieldMessage_DrawWindow(&u->msgWin, options);
    u->msgAdded = 1;
    u->printer = FieldMessage_Print(&u->msgWin, u->msgStr, options, 1);
    return 1;
}

static BOOL union_task(FieldTask *task)
{
    FieldSystem *fs = FieldTask_GetFieldSystem(task);
    UnionRoom *u = FieldTask_GetEnv(task);
    u32 choice;
    int idx;

    switch (task->state) {
    case 0:
        if (!paint_info(fs, u) || !paint_list(fs, u) || !paint_message(fs, u)) {
            printf("openmmo: union room would not allocate\n");
            union_free(u);
            return TRUE;
        }
        task->state = 1;
        break;

    case 1:
        if (FieldMessage_FinishedPrinting(u->printer) == TRUE)
            task->state = 2;
        break;

    case 2:
        choice = ListMenu_ProcessInput(u->menu);
        if (choice == (u32)MENU_NOTHING_CHOSEN)
            break;
        Sound_PlayEffect(SE_CONFIRM_sseq_3);
        if (choice == (u32)MENU_CANCEL) {
            printf("openmmo: union room closed\n");
            union_free(u);
            return TRUE;
        }
        idx = (int)choice;
        if (idx >= 0 && idx < u->count && u->name[idx][0] != '\0') {
            printf("openmmo: union room pick %d %s%s\n",
                   idx, u->name[idx],
                   (idx == u->leader_at) ? " (leader)" : "");
        } else {
            printf("openmmo: union room pick empty\n");
        }
        union_free(u);
        return TRUE;
    }
    return FALSE;
}

void openmmo_union_attach(openmmo_client *c)
{
    s_client = c;
    s_live = NULL;
    s_opened = 0;
    s_pending = 0;
    if (want_open() && !s_armed) {
        s_armed = 1;
        printf("openmmo: union room armed\n");
    }
}

void openmmo_union_mark_pending(void)
{
    s_pending = 1;
}

int openmmo_union_try_open(FieldSystem *fs)
{
    UnionRoom *u;
    const openmmo_link *link;
    int demo;

    if (fs == NULL)
        return 0;
    if (!want_open() && !s_pending)
        return 0;
    if (s_opened || s_live != NULL)
        return 0;
    if (fs->task != NULL || !FieldSystem_IsRunningFieldMap(fs)
        || FieldSystem_HasChildProcess(fs)) {
        s_pending = 1;
        return 0;
    }

    link = openmmo_client_link(s_client);
    demo = !(link != NULL && link->valid && link->present && link->count > 0);
    if (!want_open() && demo) {
        s_pending = 0;
        return 0;
    }

    u = Heap_Alloc(UNION_HEAP, sizeof(UnionRoom));
    if (u == NULL) {
        printf("openmmo: union room would not allocate\n");
        s_pending = 0;
        return 0;
    }
    memset(u, 0, sizeof(*u));
    if (demo)
        fill_demo(u);
    else
        fill_from_link(u, link);

    s_live = u;
    FieldSystem_CreateTask(fs, union_task, u);
    s_opened = 1;
    s_pending = 0;
    if (demo) {
        printf("openmmo: union room opened (demo)\n");
    } else {
        printf("openmmo: union room opened (link %d)\n", u->count);
    }
    return 1;
}
