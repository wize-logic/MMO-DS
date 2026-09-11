/* One renderer for every screen a HUD packet describes. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bg_window.h"
#include "constants/heap.h"
#include "constants/menu.h"
#include "field/field_system.h"
#include "field_message.h"
#include "field_system.h"
#include "field_task.h"
#include "font.h"
#include "game_options.h"
#include "heap.h"
#include "list_menu.h"
#include "menu.h"
#include "render_window.h"
#include "save_player.h"
#include "sound_playback.h"
#include "string_gf.h"
#include "string_list.h"
#include "system.h"
#include "text.h"

#include "../../../include/charcode.h"
#include "../../../include/endpoint.h"
#include "../../../include/client.h"
#include "../../../include/widget.h"

#define WIDGET_HEAP       HEAP_ID_FIELD2
#define WIDGET_FRAME_TILE (1024 - (18 + 12) - 9)
#define WIDGET_FRAME_PAL  11
#define WIDGET_BASE_TILE  1
#define WIDGET_WIN_PAL    13
#define WIDGET_STR_CHARS  64

typedef struct {
    mmo_screen screen;
    int focus;
    Window msgWin;
    Window choiceWin;
    StringList *choices;
    String *rowStr[MMO_WIDGET_ROWS_MAX];
    String *msgStr;
    Menu *menu;
    ListMenu *list;
    int msgAdded;
    int choiceAdded;
    u8 printer;
    int demo;
} WidgetScreen;

static openmmo_client *s_client;
static WidgetScreen *s_live;
static int s_armed;
static int s_opened;
static int s_pending;

static int want_open(void)
{
    const char *env = openmmo_dev_env("OPENMMO_WIDGET");

    if (env == NULL || env[0] == '\0' || env[0] == '0')
        return 0;
    return 1;
}

static String *utf8_string(const char *s)
{
    mmo_charcode buf[WIDGET_STR_CHARS];
    String *out = String_Init(WIDGET_STR_CHARS, WIDGET_HEAP);

    if (out == NULL)
        return NULL;
    mmo_utf8_to_charcode(s != NULL ? s : "", buf, WIDGET_STR_CHARS);
    String_CopyChars(out, (const charcode_t *)buf);
    return out;
}

static void widget_free(WidgetScreen *ws)
{
    int i;

    if (ws == NULL)
        return;
    if (ws->list != NULL) {
        ListMenu_Free(ws->list, NULL, NULL);
        ws->list = NULL;
    }
    if (ws->menu != NULL) {
        /* Menu_Free, not Menu_DestroyForExit: that one frees the window
         * pointer and the choices as well, and this window is a field
         * of the screen rather than its own allocation. */
        Menu_Free(ws->menu, NULL);
        ws->menu = NULL;
    }
    if (ws->choices != NULL) {
        StringList_Free(ws->choices);
        ws->choices = NULL;
    }
    for (i = 0; i < MMO_WIDGET_ROWS_MAX; i++) {
        if (ws->rowStr[i] != NULL)
            String_Free(ws->rowStr[i]);
        ws->rowStr[i] = NULL;
    }
    if (ws->msgStr != NULL) {
        String_Free(ws->msgStr);
        ws->msgStr = NULL;
    }
    if (ws->choiceAdded) {
        Window_EraseStandardFrame(&ws->choiceWin, TRUE);
        Window_ClearAndCopyToVRAM(&ws->choiceWin);
        Window_Remove(&ws->choiceWin);
        ws->choiceAdded = 0;
    }
    if (ws->msgAdded) {
        Window_EraseMessageBox(&ws->msgWin, 0);
        Window_Remove(&ws->msgWin);
        ws->msgAdded = 0;
    }
    if (s_live == ws)
        s_live = NULL;
    Heap_Free(ws);
}

/* The message box is the engine's own; the description's rectangle is
 * the same numbers, so the constructor picks them rather than us. */
static int paint_message(FieldSystem *fs, WidgetScreen *ws, const mmo_widget *w)
{
    const Options *options = SaveData_GetOptions(fs->saveData);

    ws->msgStr = utf8_string(w->text);
    if (ws->msgStr == NULL)
        return 0;
    FieldMessage_AddWindow(fs->bgConfig, &ws->msgWin, BG_LAYER_MAIN_3);
    FieldMessage_DrawWindow(&ws->msgWin, options);
    ws->msgAdded = 1;
    ws->printer = FieldMessage_Print(&ws->msgWin, ws->msgStr, options, 1);
    return 1;
}

static int build_rows(WidgetScreen *ws, const mmo_widget *w, int count)
{
    int i;

    ws->choices = StringList_New((u32)count, WIDGET_HEAP);
    if (ws->choices == NULL)
        return 0;
    for (i = 0; i < count; i++) {
        ws->rowStr[i] = utf8_string(w->row[i].label);
        if (ws->rowStr[i] == NULL)
            return 0;
        StringList_AddFromString(ws->choices, ws->rowStr[i], (u32)i);
    }
    return 1;
}

static int add_choice_window(FieldSystem *fs, WidgetScreen *ws,
                             const mmo_widget *w, int rows)
{
    Window_Add(fs->bgConfig, &ws->choiceWin, BG_LAYER_MAIN_3,
               (u8)w->rect.left, (u8)w->rect.top, (u8)w->rect.width,
               (u8)(rows * 2), WIDGET_WIN_PAL, WIDGET_BASE_TILE);
    ws->choiceAdded = 1;
    LoadStandardWindowGraphics(fs->bgConfig, BG_LAYER_MAIN_3,
                               WIDGET_FRAME_TILE, WIDGET_FRAME_PAL,
                               STANDARD_WINDOW_SYSTEM, WIDGET_HEAP);
    if (w->framed)
        Window_DrawStandardFrame(&ws->choiceWin, 1, WIDGET_FRAME_TILE,
                                 WIDGET_FRAME_PAL);
    Window_FillTilemap(&ws->choiceWin, 15);
    return 1;
}

static int paint_grid(FieldSystem *fs, WidgetScreen *ws, const mmo_widget *w)
{
    MenuTemplate tmpl;
    int rows = mmo_widget_visible(w);
    int cols = (w->cols > 0) ? w->cols : 1;

    if (rows < 1)
        return 0;
    if (!build_rows(ws, w, w->row_count))
        return 0;
    if (!add_choice_window(fs, ws, w, rows))
        return 0;

    memset(&tmpl, 0, sizeof tmpl);
    tmpl.choices = ws->choices;
    tmpl.window = &ws->choiceWin;
    tmpl.fontID = FONT_SYSTEM;
    tmpl.xSize = (u8)cols;
    tmpl.ySize = (u8)rows;
    tmpl.lineSpacing = 0;
    tmpl.suppressCursor = 0;
    tmpl.loopAround = (u8)(w->loop ? 1 : 0);
    ws->menu = Menu_NewAndCopyToVRAM(&tmpl, 8, 0, 0, WIDGET_HEAP,
                                     w->cancelable ? PAD_BUTTON_B : 0);
    return ws->menu != NULL;
}

/* The list is the one that scrolls: maxDisplay below count is the
 * engine's own scroll model, and the description carries the window. */
static int paint_list(FieldSystem *fs, WidgetScreen *ws, const mmo_widget *w)
{
    ListMenuTemplate tmpl;
    int rows = mmo_widget_visible(w);

    if (rows < 1)
        return 0;
    if (!build_rows(ws, w, w->row_count))
        return 0;
    if (!add_choice_window(fs, ws, w, rows))
        return 0;

    memset(&tmpl, 0, sizeof tmpl);
    tmpl.choices = ws->choices;
    tmpl.window = &ws->choiceWin;
    tmpl.count = (u16)w->row_count;
    tmpl.maxDisplay = (u16)rows;
    tmpl.textXOffset = 8;
    tmpl.textColorFg = 1;
    tmpl.textColorBg = 15;
    tmpl.textColorShadow = 2;
    tmpl.fontID = FONT_SYSTEM;
    tmpl.cursorType = 0;
    ws->list = ListMenu_New(&tmpl, 0, 0, WIDGET_HEAP);
    if (ws->list == NULL)
        return 0;
    Window_CopyToVRAM(&ws->choiceWin);
    return 1;
}

static int paint_focus(FieldSystem *fs, WidgetScreen *ws)
{
    const mmo_widget *w;

    if (ws->focus < 0)
        return 1;
    w = &ws->screen.part[ws->focus];
    switch (w->kind) {
    case MMO_WIDGET_GRID:
        return paint_grid(fs, ws, w);
    case MMO_WIDGET_LIST:
        return paint_list(fs, ws, w);
    default:
        /* The two-choice prompt is already drawn from the packet that
         * means one (the dialog box's type 5), and typing is the text
         * field's own screen. Neither gets a second implementation
         * here, and neither is guessed at. */
        printf("openmmo: widget refuses to draw %s, no packet compiles "
               "to one and it is not this renderer's surface\n",
               mmo_widget_kind_name(w->kind));
        return 0;
    }
}

static void report_pick(WidgetScreen *ws, int cursor)
{
    const mmo_widget *w = &ws->screen.part[ws->focus];
    s64 value = 0;

    if (!mmo_widget_pick(w, cursor, &value)) {
        printf("openmmo: widget pick out of range (%d of %d)\n", cursor,
               w->row_count);
        return;
    }
    /* Nothing on the wire has been measured as the answer to one of
     * these, so the pick is reported and no c2s frame is sent. */
    printf("openmmo: widget %s pick %d value %lld \"%s\"%s\n",
           ws->screen.name, cursor, (long long)value, w->row[cursor].label,
           ws->screen.reply_op ? "" : " (no reply opcode)");
}

static BOOL widget_task(FieldTask *task)
{
    FieldSystem *fs = FieldTask_GetFieldSystem(task);
    WidgetScreen *ws = FieldTask_GetEnv(task);
    const mmo_widget *msg = NULL;
    u32 choice;
    int i;

    for (i = 0; i < ws->screen.part_count; i++) {
        if (ws->screen.part[i].kind == MMO_WIDGET_MESSAGE) {
            msg = &ws->screen.part[i];
            break;
        }
    }

    switch (task->state) {
    case 0:
        if (msg != NULL && !paint_message(fs, ws, msg)) {
            printf("openmmo: widget screen would not allocate\n");
            widget_free(ws);
            return TRUE;
        }
        if (!paint_focus(fs, ws)) {
            if (ws->focus >= 0)
                printf("openmmo: widget screen would not allocate\n");
            if (msg == NULL) {
                widget_free(ws);
                return TRUE;
            }
            ws->focus = -1;
        }
        task->state = (msg != NULL) ? 1 : 2;
        break;

    case 1:
        if (FieldMessage_FinishedPrinting(ws->printer) == TRUE)
            task->state = 2;
        break;

    case 2:
        if (ws->focus < 0) {
            /* A screen that is only text closes on the same press the
             * engine's own message box takes. */
            if (gSystem.pressedKeys & (PAD_BUTTON_A | PAD_BUTTON_B)) {
                printf("openmmo: widget %s closed\n", ws->screen.name);
                widget_free(ws);
                return TRUE;
            }
            break;
        }
        choice = (ws->list != NULL) ? ListMenu_ProcessInput(ws->list)
                                    : Menu_ProcessInput(ws->menu);
        if (choice == (u32)MENU_NOTHING_CHOSEN)
            break;
        Sound_PlayEffect(SE_CONFIRM_sseq_3);
        if (choice == (u32)MENU_CANCEL) {
            printf("openmmo: widget %s cancelled\n", ws->screen.name);
        } else {
            report_pick(ws, (int)choice);
        }
        widget_free(ws);
        return TRUE;
    }
    return FALSE;
}

/*
 * With no session there is no packet to draw, so a boot that wants to see the screen gets one
 * built the same way a real one is, through the compiler, from a shape the wire actually
 * carries.
 */
static int fill_demo(mmo_screen *out)
{
    const char *env = openmmo_dev_env("OPENMMO_WIDGET");
    mmo_option_list demo;
    int i;

    if (env != NULL && strcmp(env, "confirm") == 0) {
        mmo_confirm_prompt c;

        memset(&c, 0, sizeof c);
        c.visible = 1;
        c.entity_id = 1;
        c.request_s = 30;
        c.response_s = 30;
        return mmo_screen_from_confirm(&c, out);
    }

    memset(&demo, 0, sizeof demo);
    demo.kind = 1;
    demo.count = (env != NULL && strcmp(env, "list") == 0)
                     ? MMO_WIDGET_GRID_MAX + 4
                     : 3;
    for (i = 0; i < demo.count; i++) {
        demo.entry[i].type_id = (s8)(i + 1);
        demo.entry[i].sub_type = 0;
        demo.entry[i].value[0] = (s16)(i * 10);
    }
    return mmo_screen_from_option_list(&demo, out);
}

void openmmo_widget_attach(openmmo_client *c)
{
    s_client = c;
    s_live = NULL;
    s_opened = 0;
    s_pending = 0;
    if (want_open() && !s_armed) {
        s_armed = 1;
        printf("openmmo: widget kit armed\n");
    }
}

void openmmo_widget_mark_pending(void)
{
    s_pending = 1;
}

int openmmo_widget_try_open(FieldSystem *fs)
{
    WidgetScreen *ws;
    mmo_screen screen;
    int demo = 0;

    if (fs == NULL)
        return 0;
    if (!want_open())
        return 0;
    if (s_live != NULL)
        return 0;
    if (fs->task != NULL || !FieldSystem_IsRunningFieldMap(fs)
        || FieldSystem_HasChildProcess(fs)) {
        return 0;
    }

    mmo_screen_clear(&screen);
    if (s_pending) {
        /* A packet arrived. It opens the screen it describes, and once:
         * the store still holds the same screen afterwards, so opening
         * off the store alone would put it straight back up. */
        s_pending = 0;
        if (!mmo_screen_from_ui(openmmo_client_ui(s_client), &screen))
            return 0;
    } else {
        if (s_opened)
            return 0;
        if (!mmo_screen_from_ui(openmmo_client_ui(s_client), &screen)) {
            if (!fill_demo(&screen))
                return 0;
            demo = 1;
        }
    }
    s_opened = 1;

    ws = Heap_Alloc(WIDGET_HEAP, sizeof(WidgetScreen));
    if (ws == NULL) {
        printf("openmmo: widget screen would not allocate\n");
        return 0;
    }
    memset(ws, 0, sizeof(*ws));
    ws->screen = screen;
    ws->focus = mmo_screen_focus(&screen);
    ws->demo = demo;

    s_live = ws;
    FieldSystem_CreateTask(fs, widget_task, ws);
    printf("openmmo: widget %s from 0x%02x, %d part(s), %s %d row(s)%s\n",
           screen.name, screen.opcode, screen.part_count,
           ws->focus >= 0 ? mmo_widget_kind_name(screen.part[ws->focus].kind)
                          : "message",
           ws->focus >= 0 ? screen.part[ws->focus].row_count : 0,
           demo ? " (demo)" : "");
    return 1;
}
