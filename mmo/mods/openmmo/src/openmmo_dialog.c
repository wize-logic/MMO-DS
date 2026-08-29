/* The engine message box is a display of a server box. */

#include <stdio.h>
#include <string.h>

#include "bg_window.h"
#include "constants/field/window.h"
#include "constants/field_base_tiles.h"
#include "constants/heap.h"
#include "constants/menu.h"
#include "constants/narc.h"
#include "field/field_system.h"
#include "field_message.h"
#include "field_system.h"
#include "field_task.h"
#include "font.h"
#include "game_options.h"
#include "generated/text_banks.h"
#include "heap.h"
#include "menu.h"
#include "message.h"
#include "render_window.h"
#include "save_player.h"
#include "string_gf.h"
#include "string_list.h"
#include "string_template.h"
#include "system.h"

#include "../../../include/charcode.h"
#include "../../../include/client.h"

#define DIALOG_STRING_CHARS 256
/* A local prompt's line: a name off this server is up to 32 code points and
 * the sentence around it is short. */
#define DIALOG_LOCAL_CHARS  128
#define MENU_FRAME_TILE     (1024 - (18 + 12) - 9)
#define MENU_FRAME_PAL      11

typedef struct {
    Window window;
    String *str;
    MessageLoader *loader;
    Menu *menu;
    u8 printer;
    u8 added;
    u8 cancel;
    s8 kind;           /* 0 message, 1 yes/no, 2 species, 3 text list */
    u8 choice_count;
    s16 choice_bank;
    s16 choices[MMO_DIALOG_MENU_MAX];
    /*
     * Set for a box this client raised itself rather than one the server sent: the answer goes
     * to this instead of onto the wire, and the line is free text rather than a ROM entry.
     */
    void (*answer)(int yes);
} DialogBox;

static openmmo_client *s_client;
static int s_pending;
static DialogBox *s_live;

/* Fill the line's string variables, the decomp's SCRIPT_MANAGER_STR_TEMPLATE. */
#define DIALOG_FILLED_CHARS \
    (DIALOG_STRING_CHARS + MMO_DIALOG_STRVAR_MAX * MMO_DIALOG_STRVAR_CHARS)

static String *dialog_fill_strvars(const openmmo_dialog *src, const String *raw)
{
    StringTemplate *tmpl;
    String *out;
    String *arg;
    mmo_charcode buf[MMO_DIALOG_STRVAR_CHARS + 1];
    int i;

    tmpl = StringTemplate_New(MMO_DIALOG_STRVAR_MAX,
                              MMO_DIALOG_STRVAR_CHARS + 1, HEAP_ID_FIELD2);
    if (tmpl == NULL)
        return NULL;
    out = String_Init(DIALOG_FILLED_CHARS, HEAP_ID_FIELD2);
    arg = String_Init(MMO_DIALOG_STRVAR_CHARS + 1, HEAP_ID_FIELD2);
    if (out == NULL || arg == NULL) {
        if (out != NULL)
            String_Free(out);
        if (arg != NULL)
            String_Free(arg);
        StringTemplate_Free(tmpl);
        return NULL;
    }
    for (i = 0; i < src->strvar_count; i++) {
        mmo_utf8_to_charcode(src->strvar[i].text, buf,
                             MMO_DIALOG_STRVAR_CHARS + 1);
        String_CopyChars(arg, (const charcode_t *)buf);
        StringTemplate_SetString(tmpl, (u32)src->strvar[i].slot, arg, 0, TRUE,
                                 GAME_LANGUAGE);
    }
    StringTemplate_Format(tmpl, out, raw);
    String_Free(arg);
    StringTemplate_Free(tmpl);
    return out;
}

static const WindowTemplate sYesNoWindow = {
    .bgLayer = BG_LAYER_MAIN_3,
    .tilemapLeft = 25,
    .tilemapTop = 13,
    .width = 6,
    .height = 4,
    .palette = FIELD_MESSAGE_PALETTE_INDEX,
    .baseTile = BASE_TILE_YES_NO_MENU,
};

void openmmo_dialog_attach(openmmo_client *c)
{
    s_client = c;
    s_pending = 0;
    s_live = NULL;
}

void openmmo_dialog_mark_pending(void)
{
    s_pending = 1;
}

static void dialog_free(DialogBox *box)
{
    if (box == NULL)
        return;
    if (box->menu != NULL) {
        Menu_DestroyForExit(box->menu, HEAP_ID_FIELD2);
        box->menu = NULL;
    }
    if (box->added)
        Window_Remove(&box->window);
    if (box->str != NULL)
        String_Free(box->str);
    if (box->loader != NULL)
        MessageLoader_Free(box->loader);
    if (s_live == box)
        s_live = NULL;
    Heap_Free(box);
}

static u8 reply_from_yesno(u32 result)
{
    /* Engine MENU_YES is 0; the wire treats 1 as yes. */
    return (result == MENU_YES) ? 1 : 0;
}

static u8 reply_from_menu(u32 result, int count, int zero_based)
{
    if (result == (u32)MENU_CANCEL || result == (u32)MENU_NOTHING_CHOSEN)
        return (u8)(zero_based ? count : count + 1);
    if (zero_based) {
        if ((int)result < 0 || (int)result >= count)
            return (u8)count;
        return (u8)result;
    }
    if (result < 1 || (int)result > count)
        return (u8)(count + 1);
    return (u8)result;
}

static int start_yesno(FieldSystem *fs, DialogBox *box)
{
    LoadStandardWindowGraphics(fs->bgConfig, BG_LAYER_MAIN_3,
                               MENU_FRAME_TILE, MENU_FRAME_PAL,
                               STANDARD_WINDOW_SYSTEM, HEAP_ID_FIELD2);
    box->menu = Menu_MakeYesNoChoice(fs->bgConfig, &sYesNoWindow,
                                     MENU_FRAME_TILE, MENU_FRAME_PAL,
                                     HEAP_ID_FIELD2);
    return box->menu != NULL;
}

static int start_menu(FieldSystem *fs, DialogBox *box)
{
    MenuTemplate tmpl;
    MessageLoader *names;
    StringList *list;
    Window *win;
    WindowTemplate wt;
    int i, n;
    u16 bank;
    u32 value;

    n = box->choice_count;
    if (n < 1)
        return 0;
    if (n > 8)
        n = 8;

    LoadStandardWindowGraphics(fs->bgConfig, BG_LAYER_MAIN_3,
                               MENU_FRAME_TILE, MENU_FRAME_PAL,
                               STANDARD_WINDOW_SYSTEM, HEAP_ID_FIELD2);

    bank = (box->kind == 3) ? (u16)box->choice_bank : TEXT_BANK_SPECIES_NAME;
    names = MessageLoader_Init(MSG_LOADER_LOAD_ON_DEMAND,
                               NARC_INDEX_MSGDATA__PL_MSG,
                               bank, HEAP_ID_FIELD2);
    if (names == NULL)
        return 0;
    list = StringList_New((u32)n, HEAP_ID_FIELD2);
    if (list == NULL) {
        MessageLoader_Free(names);
        return 0;
    }
    for (i = 0; i < n; i++) {
        u16 msg = (u16)box->choices[i];

        value = (box->kind == 3) ? (u32)i : (u32)(i + 1);
        StringList_AddFromMessageBank(list, names, msg, value);
    }
    MessageLoader_Free(names);

    memset(&wt, 0, sizeof wt);
    wt.bgLayer = BG_LAYER_MAIN_3;
    wt.tilemapLeft = 15;
    wt.tilemapTop = (u8)(19 - 2 * n);
    wt.width = 12;
    wt.height = (u8)(2 * n);
    wt.palette = FIELD_MESSAGE_PALETTE_INDEX;
    /*
     * Downward from the message window's own base, which is what the engine's start menu does
     * (start_menu.c:527).
     */
    wt.baseTile = (u16)(BASE_TILE_MESSAGE_WINDOW - wt.width * wt.height);

    win = Window_New(HEAP_ID_FIELD2, 1);
    if (win == NULL) {
        StringList_Free(list);
        return 0;
    }
    Window_AddFromTemplate(fs->bgConfig, win, &wt);
    Window_FillTilemap(win, 15);
    Window_DrawStandardFrame(win, 1, MENU_FRAME_TILE, MENU_FRAME_PAL);

    memset(&tmpl, 0, sizeof tmpl);
    tmpl.choices = list;
    tmpl.window = win;
    tmpl.fontID = FONT_SYSTEM;
    tmpl.xSize = 1;
    tmpl.ySize = (u8)n;
    tmpl.lineSpacing = 0;
    tmpl.suppressCursor = 0;
    tmpl.loopAround = 1;
    box->menu = Menu_NewAndCopyToVRAM(&tmpl, 8, 0, 0, HEAP_ID_FIELD2,
                                      PAD_BUTTON_B);
    if (box->menu == NULL) {
        Window_EraseStandardFrame(win, 0);
        Window_Remove(win);
        Heap_FreeExplicit(HEAP_ID_FIELD2, win);
        StringList_Free(list);
        return 0;
    }
    return 1;
}

static BOOL dialog_task(FieldTask *task)
{
    FieldSystem *fs = FieldTask_GetFieldSystem(task);
    DialogBox *box = FieldTask_GetEnv(task);
    const Options *options;
    u32 result;
    u8 response;

    switch (task->state) {
    case 0:
        if (box->str != NULL) {
            options = SaveData_GetOptions(fs->saveData);
            FieldMessage_AddWindow(fs->bgConfig, &box->window, BG_LAYER_MAIN_3);
            FieldMessage_DrawWindow(&box->window, options);
            box->added = 1;
            box->printer = FieldMessage_Print(&box->window, box->str, options, 1);
            task->state = 1;
        } else {
            task->state = 2;
        }
        break;
    case 1:
        if (box->cancel) {
            if (box->answer != NULL)
                box->answer(0);
            Window_EraseMessageBox(&box->window, 0);
            task->state = 4;
            break;
        }
        if (FieldMessage_FinishedPrinting(box->printer) == TRUE) {
            if (box->kind == 0) {
                if (gSystem.pressedKeys & (PAD_BUTTON_A | PAD_BUTTON_B)) {
                    if (box->answer != NULL)
                        box->answer(0);
                    else if (s_client != NULL)
                        openmmo_client_reply_dialog(s_client, 0);
                    Window_EraseMessageBox(&box->window, 0);
                    task->state = 4;
                }
            } else {
                task->state = 2;
            }
        }
        break;
    case 2:
        if (box->cancel) {
            if (box->answer != NULL)
                box->answer(0);
            if (box->added)
                Window_EraseMessageBox(&box->window, 0);
            task->state = 4;
            break;
        }
        if (box->kind == 1) {
            if (!start_yesno(fs, box)) {
                printf("openmmo: yes/no menu would not allocate\n");
                if (box->answer != NULL)
                    box->answer(0);
                else if (s_client != NULL)
                    openmmo_client_reply_dialog(s_client, 0);
                if (box->added)
                    Window_EraseMessageBox(&box->window, 0);
                task->state = 4;
                break;
            }
        } else if (box->kind == 2 || box->kind == 3) {
            if (!start_menu(fs, box)) {
                printf("openmmo: choice menu would not allocate\n");
                if (s_client != NULL)
                    openmmo_client_reply_dialog(s_client,
                        (u8)(box->kind == 3 ? box->choice_count
                                            : box->choice_count + 1));
                if (box->added)
                    Window_EraseMessageBox(&box->window, 0);
                task->state = 4;
                break;
            }
        } else {
            task->state = 4;
            break;
        }
        task->state = 3;
        break;
    case 3:
        if (box->cancel) {
            if (box->answer != NULL)
                box->answer(0);
            if (box->added)
                Window_EraseMessageBox(&box->window, 0);
            task->state = 4;
            break;
        }
        result = Menu_ProcessInput(box->menu);
        if (result == (u32)MENU_NOTHING_CHOSEN)
            break;
        if (box->kind == 1)
            response = reply_from_yesno(result);
        else
            response = reply_from_menu(result, box->choice_count,
                                       box->kind == 3);
        if (box->answer != NULL)
            box->answer(response != 0);
        else if (s_client != NULL)
            openmmo_client_reply_dialog(s_client, response);
        if (box->added)
            Window_EraseMessageBox(&box->window, 0);
        printf("openmmo: dialog replied %u\n", (unsigned)response);
        task->state = 4;
        break;
    case 4:
        dialog_free(box);
        return TRUE;
    }
    return FALSE;
}

static int dialog_start(FieldSystem *fs, const openmmo_dialog *src)
{
    DialogBox *box;
    MessageLoader *loader = NULL;
    String *str = NULL;

    box = Heap_Alloc(HEAP_ID_FIELD2, sizeof(DialogBox));
    if (box == NULL)
        return 0;
    memset(box, 0, sizeof(*box));

    if (src->action_type == (s8)MMO_DIALOG_ACTION_YESNO)
        box->kind = 1;
    else if (src->action_type == (s8)MMO_DIALOG_ACTION_MENU)
        box->kind = 2;
    else if (src->action_type == (s8)MMO_DIALOG_ACTION_LIST)
        box->kind = 3;
    else
        box->kind = 0;
    box->choice_count = (u8)src->choice_count;
    box->choice_bank = src->choice_bank;
    if (src->choice_count > 0)
        memcpy(box->choices, src->choices,
               (size_t)src->choice_count * sizeof src->choices[0]);

    if (src->resolved && src->text_id != 0) {
        loader = MessageLoader_Init(MSG_LOADER_LOAD_ON_DEMAND,
                                    NARC_INDEX_MSGDATA__PL_MSG, src->bank,
                                    HEAP_ID_FIELD2);
        if (loader == NULL) {
            Heap_Free(box);
            return 0;
        }
        str = String_Init(DIALOG_STRING_CHARS, HEAP_ID_FIELD2);
        if (str == NULL) {
            MessageLoader_Free(loader);
            Heap_Free(box);
            return 0;
        }
        MessageLoader_GetString(loader, src->entry, str);
        if (src->strvar_count > 0) {
            String *filled = dialog_fill_strvars(src, str);
            if (filled != NULL) {
                String_Free(str);
                str = filled;
            }
        }
        box->loader = loader;
        box->str = str;
    } else if (box->kind == 0) {
        Heap_Free(box);
        return 0;
    }

    s_live = box;
    FieldSystem_CreateTask(fs, dialog_task, box);
    return 1;
}

/* A yes/no this client raised itself, over a line of free text. */
int openmmo_dialog_ask_local(FieldSystem *fs, const char *utf8,
                             void (*cb)(int yes))
{
    DialogBox *box;
    mmo_charcode buf[DIALOG_LOCAL_CHARS + 1];

    if (fs == NULL || utf8 == NULL || cb == NULL)
        return 0;
    if (s_live != NULL)
        return 0;
    if (fs->task != NULL || !FieldSystem_IsRunningFieldMap(fs)
        || FieldSystem_HasChildProcess(fs))
        return 0;

    box = Heap_Alloc(HEAP_ID_FIELD2, sizeof(DialogBox));
    if (box == NULL)
        return 0;
    memset(box, 0, sizeof(*box));
    box->kind = 1;
    box->answer = cb;
    box->str = String_Init(DIALOG_LOCAL_CHARS + 1, HEAP_ID_FIELD2);
    if (box->str == NULL) {
        Heap_Free(box);
        return 0;
    }
    mmo_utf8_to_charcode(utf8, buf, DIALOG_LOCAL_CHARS + 1);
    String_CopyChars(box->str, (const charcode_t *)buf);

    s_live = box;
    FieldSystem_CreateTask(fs, dialog_task, box);
    return 1;
}

int openmmo_dialog_try_open(FieldSystem *fs)
{
    const openmmo_dialog *box;

    if (fs == NULL || s_client == NULL)
        return 0;
    box = openmmo_client_dialog(s_client);
    if (box == NULL || !box->valid)
        return 0;
    if (!s_pending)
        return 0;

    if (box->close) {
        if (s_live != NULL)
            s_live->cancel = 1;
        s_pending = 0;
        printf("openmmo: dialog closed\n");
        return 1;
    }

    if (!box->open) {
        s_pending = 0;
        return 0;
    }

    if (box->text_id != 0 && !box->resolved) {
        printf("openmmo: dialog refused text 0x%08x type %d (%s)\n",
               (unsigned)box->text_id, (int)box->action_type,
               box->why != NULL ? box->why : "unresolvable");
        s_pending = 0;
        return 0;
    }

    if (box->text_id == 0
        && box->action_type != (s8)MMO_DIALOG_ACTION_YESNO
        && box->action_type != (s8)MMO_DIALOG_ACTION_MENU
        && box->action_type != (s8)MMO_DIALOG_ACTION_LIST) {
        s_pending = 0;
        return 0;
    }

    if (s_live != NULL)
        s_live->cancel = 1;

    if (fs->task != NULL || !FieldSystem_IsRunningFieldMap(fs)
        || FieldSystem_HasChildProcess(fs))
        return 0;

    if (!dialog_start(fs, box)) {
        printf("openmmo: dialog box would not allocate (type %d bank %u entry %u)\n",
               (int)box->action_type, box->bank, box->entry);
        s_pending = 0;
        return 0;
    }
    s_pending = 0;
    if (box->action_type == (s8)MMO_DIALOG_ACTION_YESNO) {
        printf("openmmo: yes/no Menu_MakeYesNoChoice type %d bank %u entry %u\n",
               (int)box->action_type, box->bank, box->entry);
    } else if (box->action_type == (s8)MMO_DIALOG_ACTION_MENU
               || box->action_type == (s8)MMO_DIALOG_ACTION_LIST) {
        printf("openmmo: menu Menu_New type %d %d choice(s)\n",
               (int)box->action_type, box->choice_count);
    } else {
        printf("openmmo: dialog FieldMessage type %d bank %u entry %u\n",
               (int)box->action_type, box->bank, box->entry);
    }
    return 1;
}
