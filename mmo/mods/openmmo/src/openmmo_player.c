/* A on a remote player is the official player menu. */

#include <stdio.h>
#include <string.h>

#include "bg_window.h"
#include "constants/field/window.h"
#include "constants/field_base_tiles.h"
#include "constants/heap.h"
#include "constants/menu.h"
#include "field/field_system.h"
#include "field_message.h"
#include "field_system.h"
#include "field_task.h"
#include "font.h"
#include "game_options.h"
#include "heap.h"
#include "menu.h"
#include "render_window.h"
#include "save_player.h"
#include "sound_playback.h"
#include "string_gf.h"
#include "string_list.h"
#include "system.h"
#include "text.h"

#include "../../../include/charcode.h"
#include "../../../include/client.h"
#include "../../../include/entity.h"

#define PLAYER_HEAP       HEAP_ID_FIELD2
#define MENU_FRAME_TILE   (1024 - (18 + 12) - 9)
#define MENU_FRAME_PAL    11
#define PLAYER_ROWS       7

enum {
    ACT_CHALLENGE = 0,
    ACT_WHISPER,
    ACT_TRADE,
    ACT_LINK,
    ACT_FRIEND,
    ACT_BLOCK,
    ACT_COPY
};

enum {
    CONFIRM_NONE = 0,
    CONFIRM_CHALLENGE,
    CONFIRM_TRADE,
    CONFIRM_UNFRIEND
};

typedef struct {
    Window msgWin;
    Window *menuWin;
    String *msgStr;
    String *rowStr[PLAYER_ROWS];
    StringList *choices;
    Menu *menu;
    u8 printer;
    u8 msgAdded;
    u8 confirm;
    int is_friend;
    char name[OPENMMO_ENTITY_NAME_MAX];
} PlayerMenu;

static openmmo_client *s_client;
static PlayerMenu *s_live;

static const WindowTemplate sYesNoWindow = {
    .bgLayer = BG_LAYER_MAIN_3,
    .tilemapLeft = 25,
    .tilemapTop = 13,
    .width = 6,
    .height = 4,
    .palette = FIELD_MESSAGE_PALETTE_INDEX,
    .baseTile = BASE_TILE_YES_NO_MENU,
};

void openmmo_player_attach(openmmo_client *c)
{
    s_client = c;
    s_live = NULL;
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

static void player_free(PlayerMenu *p)
{
    int i;

    if (p == NULL)
        return;
    if (p->menu != NULL) {
        Menu_DestroyForExit(p->menu, PLAYER_HEAP);
        p->menu = NULL;
        p->menuWin = NULL;
        p->choices = NULL;
    }
    if (p->choices != NULL) {
        StringList_Free(p->choices);
        p->choices = NULL;
    }
    for (i = 0; i < PLAYER_ROWS; i++) {
        if (p->rowStr[i] != NULL)
            String_Free(p->rowStr[i]);
        p->rowStr[i] = NULL;
    }
    if (p->msgStr != NULL) {
        String_Free(p->msgStr);
        p->msgStr = NULL;
    }
    if (p->msgAdded) {
        Window_EraseMessageBox(&p->msgWin, 0);
        Window_Remove(&p->msgWin);
        p->msgAdded = 0;
    }
    if (s_live == p)
        s_live = NULL;
    Heap_Free(p);
}

static const char *friend_label(int is_friend)
{
    return is_friend ? "Remove Friend" : "Add Friend";
}

static int start_actions(FieldSystem *fs, PlayerMenu *p)
{
    MenuTemplate tmpl;
    WindowTemplate wt;
    static const char *const labels[PLAYER_ROWS] = {
        "Challenge",
        "Whisper",
        "Trade",
        "Invite to Link",
        NULL,
        "Block",
        "Copy Name",
    };
    const char *label;
    int i, n = PLAYER_ROWS;

    LoadStandardWindowGraphics(fs->bgConfig, BG_LAYER_MAIN_3,
                               MENU_FRAME_TILE, MENU_FRAME_PAL,
                               STANDARD_WINDOW_SYSTEM, PLAYER_HEAP);

    p->choices = StringList_New((u32)n, PLAYER_HEAP);
    if (p->choices == NULL)
        return 0;
    for (i = 0; i < n; i++) {
        label = (i == ACT_FRIEND) ? friend_label(p->is_friend) : labels[i];
        p->rowStr[i] = latin1(PLAYER_HEAP, label);
        if (p->rowStr[i] == NULL)
            return 0;
        StringList_AddFromString(p->choices, p->rowStr[i], (u32)i);
    }

    memset(&wt, 0, sizeof wt);
    wt.bgLayer = BG_LAYER_MAIN_3;
    wt.tilemapLeft = 13;
    wt.tilemapTop = (u8)(19 - 2 * n);
    wt.width = 16;
    wt.height = (u8)(2 * n);
    wt.palette = FIELD_MESSAGE_PALETTE_INDEX;
    /*
     * Downward from the message window's own base, which is what the engine's start menu does
     * (start_menu.c:527).
     */
    wt.baseTile = (u16)(BASE_TILE_MESSAGE_WINDOW - wt.width * wt.height);

    p->menuWin = Window_New(PLAYER_HEAP, 1);
    if (p->menuWin == NULL)
        return 0;
    Window_AddFromTemplate(fs->bgConfig, p->menuWin, &wt);
    Window_FillTilemap(p->menuWin, 15);
    Window_DrawStandardFrame(p->menuWin, 1, MENU_FRAME_TILE, MENU_FRAME_PAL);

    memset(&tmpl, 0, sizeof tmpl);
    tmpl.choices = p->choices;
    tmpl.window = p->menuWin;
    tmpl.fontID = FONT_SYSTEM;
    tmpl.xSize = 1;
    tmpl.ySize = (u8)n;
    tmpl.lineSpacing = 0;
    tmpl.suppressCursor = 0;
    tmpl.loopAround = 1;
    p->menu = Menu_NewAndCopyToVRAM(&tmpl, 8, 0, 0, PLAYER_HEAP, PAD_BUTTON_B);
    if (p->menu == NULL) {
        Window_EraseStandardFrame(p->menuWin, 0);
        Window_Remove(p->menuWin);
        Heap_FreeExplicit(PLAYER_HEAP, p->menuWin);
        p->menuWin = NULL;
        return 0;
    }
    p->choices = NULL;
    return 1;
}

static int start_confirm(FieldSystem *fs, PlayerMenu *p)
{
    if (p->menu != NULL) {
        Menu_DestroyForExit(p->menu, PLAYER_HEAP);
        p->menu = NULL;
        p->menuWin = NULL;
    }
    LoadStandardWindowGraphics(fs->bgConfig, BG_LAYER_MAIN_3,
                               MENU_FRAME_TILE, MENU_FRAME_PAL,
                               STANDARD_WINDOW_SYSTEM, PLAYER_HEAP);
    p->menu = Menu_MakeYesNoChoice(fs->bgConfig, &sYesNoWindow,
                                   MENU_FRAME_TILE, MENU_FRAME_PAL,
                                   PLAYER_HEAP);
    return p->menu != NULL;
}

static int paint_message(FieldSystem *fs, PlayerMenu *p, const char *text)
{
    const Options *options = SaveData_GetOptions(fs->saveData);

    if (p->msgAdded) {
        Window_EraseMessageBox(&p->msgWin, 0);
        Window_Remove(&p->msgWin);
        p->msgAdded = 0;
    }
    if (p->msgStr != NULL) {
        String_Free(p->msgStr);
        p->msgStr = NULL;
    }
    p->msgStr = latin1(PLAYER_HEAP, text);
    if (p->msgStr == NULL)
        return 0;
    FieldMessage_AddWindow(fs->bgConfig, &p->msgWin, BG_LAYER_MAIN_3);
    FieldMessage_DrawWindow(&p->msgWin, options);
    p->msgAdded = 1;
    p->printer = FieldMessage_Print(&p->msgWin, p->msgStr, options, 1);
    return 1;
}

extern void openmmo_player_compose_whisper(const char *name);

static void do_action_by(u32 act, const char *name, int is_friend);

/* One action by name, the menu below and the window's frames (over
 * OPENMMO_HUD_CMD_PLAYER) both land here, so there is one action layer
 * however the player was picked. The verb numbering is the page's own
 * (hud_channel.h OPENMMO_HUD_ACT_*), pinned equal to ACT_* below. */
void openmmo_player_do(int act, const char *name)
{
    int is_friend;

    if (name == NULL || name[0] == '\0' || act < 0 || act > ACT_COPY)
        return;
    is_friend = s_client != NULL && openmmo_client_is_friend(s_client, name);
    do_action_by((u32)act, name, is_friend);
}

static void do_action(PlayerMenu *p, u32 act)
{
    do_action_by(act, p->name, p->is_friend);
}

static void do_action_by(u32 act, const char *name, int is_friend)
{
    switch (act) {
    case ACT_WHISPER:
        printf("openmmo: player whisper %s\n", name);
        openmmo_player_compose_whisper(name);
        break;
    case ACT_TRADE:
        printf("openmmo: player trade %s\n", name);
        if (s_client != NULL && openmmo_client_trade_request(s_client, name) != 0)
            printf("openmmo: trade request would not send\n");
        break;
    case ACT_LINK:
        printf("openmmo: player invite %s\n", name);
        if (s_client != NULL && openmmo_client_link_invite(s_client, name) != 0)
            printf("openmmo: link invite would not send\n");
        break;
    case ACT_FRIEND:
        if (is_friend) {
            printf("openmmo: player unfriend %s\n", name);
            if (s_client != NULL
                && openmmo_client_remove_friend(s_client, name) != 0)
                printf("openmmo: unfriend would not send\n");
        } else {
            printf("openmmo: player friend %s\n", name);
            if (s_client != NULL
                && openmmo_client_add_friend(s_client, name) != 0)
                printf("openmmo: add friend would not send\n");
        }
        break;
    case ACT_BLOCK:
        printf("openmmo: player block %s\n", name);
        if (s_client != NULL && openmmo_client_block(s_client, name) != 0)
            printf("openmmo: block would not send\n");
        break;
    case ACT_COPY:
        printf("openmmo: player copy %s\n", name);
        break;
    case ACT_CHALLENGE: {
        char line[80];

        printf("openmmo: player challenge %s\n", name);
        snprintf(line, sizeof line, "/challenge %s", name);
        if (s_client != NULL && openmmo_client_send_chat(s_client, line) != 0)
            printf("openmmo: challenge would not send\n");
        break;
    }
    default:
        break;
    }
}

static BOOL player_task(FieldTask *task)
{
    FieldSystem *fs = FieldTask_GetFieldSystem(task);
    PlayerMenu *p = FieldTask_GetEnv(task);
    u32 choice;
    char line[64];

    switch (task->state) {
    case 0:
        /*
         * No name box under the list. The plate over their head has already said who this is,
         * and a message window here is a second answer to a question nobody asked, it also
         * costs the whole message-box row at the bottom of a screen that is showing the world.
         */
        if (!start_actions(fs, p)) {
            printf("openmmo: player menu would not allocate\n");
            player_free(p);
            return TRUE;
        }
        task->state = 2;
        break;

    case 2:
        choice = Menu_ProcessInput(p->menu);
        if (choice == (u32)MENU_NOTHING_CHOSEN)
            break;
        Sound_PlayEffect(SE_CONFIRM_sseq_3);
        if (choice == (u32)MENU_CANCEL) {
            printf("openmmo: player menu closed\n");
            player_free(p);
            return TRUE;
        }
        if (choice == (u32)ACT_CHALLENGE) {
            snprintf(line, sizeof line, "Challenge %s?", p->name);
            p->confirm = CONFIRM_CHALLENGE;
            /* The list goes before the question is painted, not after. */
            if (!start_confirm(fs, p) || !paint_message(fs, p, line)) {
                player_free(p);
                return TRUE;
            }
            task->state = 3;
            break;
        }
        if (choice == (u32)ACT_TRADE) {
            /* An offer stands in front of the other player for a minute; a
             * question mark on this side first, exactly as the challenge
             * gets one, so a slip of the pointer never asks anyone anything. */
            snprintf(line, sizeof line, "Offer %s a trade?", p->name);
            p->confirm = CONFIRM_TRADE;
            if (!start_confirm(fs, p) || !paint_message(fs, p, line)) {
                player_free(p);
                return TRUE;
            }
            task->state = 3;
            break;
        }
        if (choice == (u32)ACT_FRIEND && p->is_friend) {
            snprintf(line, sizeof line, "Remove %s?", p->name);
            p->confirm = CONFIRM_UNFRIEND;
            if (!start_confirm(fs, p) || !paint_message(fs, p, line)) {
                player_free(p);
                return TRUE;
            }
            task->state = 3;
            break;
        }
        do_action(p, choice);
        player_free(p);
        return TRUE;

    case 3:
        if (FieldMessage_FinishedPrinting(p->printer) == TRUE)
            task->state = 4;
        break;

    case 4:
        choice = Menu_ProcessInput(p->menu);
        if (choice == (u32)MENU_NOTHING_CHOSEN)
            break;
        Sound_PlayEffect(SE_CONFIRM_sseq_3);
        if (choice == (u32)MENU_YES) {
            if (p->confirm == CONFIRM_CHALLENGE)
                do_action(p, ACT_CHALLENGE);
            else if (p->confirm == CONFIRM_TRADE)
                do_action(p, ACT_TRADE);
            else if (p->confirm == CONFIRM_UNFRIEND)
                do_action(p, ACT_FRIEND);
        } else {
            printf("openmmo: player menu closed\n");
        }
        player_free(p);
        return TRUE;
    }
    return FALSE;
}

int openmmo_player_try_open(FieldSystem *fs, const char *name)
{
    PlayerMenu *p;

    if (fs == NULL || name == NULL || name[0] == '\0')
        return 0;
    if (s_live != NULL)
        return 0;
    if (fs->task != NULL || !FieldSystem_IsRunningFieldMap(fs)
        || FieldSystem_HasChildProcess(fs))
        return 0;

    p = Heap_Alloc(PLAYER_HEAP, sizeof(PlayerMenu));
    if (p == NULL) {
        printf("openmmo: player menu would not allocate\n");
        return 0;
    }
    memset(p, 0, sizeof(*p));
    snprintf(p->name, sizeof p->name, "%s", name);
    p->is_friend = s_client != NULL && openmmo_client_is_friend(s_client, name);
    s_live = p;
    FieldSystem_CreateTask(fs, player_task, p);
    printf("openmmo: player menu %s\n", p->name);
    return 1;
}
