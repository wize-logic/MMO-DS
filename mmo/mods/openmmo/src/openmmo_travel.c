/* Travel between PCs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "field/field_system.h"
#include "field_system.h"
#include "overlay005/field_menu.h"
#include "string_gf.h"
#include "res/text/bank/menu_entries.h"
#include "../../../include/charcode.h"
#include "../../../include/client.h"
#include "../../../include/endpoint.h"

/* The menu value TRAVEL answers with. The PC scripts name 0..4 and send
 * everything else to SWITCH OFF (scripts_common.s, CommonScript_PCMenu and
 * CommonScript_PCMenuHallOfFame), which is exactly the exit wanted. */
#define TRAVEL_MENU_INDEX 9

static openmmo_client *s_client;
static int s_seated;
static int s_requested; /* TRAVEL was picked; /pc goes when the field settles */
static int s_off;       /* a release: no row, no /pc, no debug door */

void openmmo_travel_attach(openmmo_client *c)
{
    s_client = c;
    s_seated = 0;
    s_requested = 0;
    /* A release is a Sinnoh client (endpoint.h, openmmo_dev_features): the
     * ported regions are reached from a working build and nowhere else, and
     * the server answers /pc only to a developer account, so a row here
     * would be a row nothing answers. */
    s_off = !openmmo_dev_features();
    if (s_off) {
        printf("openmmo: travel between PCs is not in this build\n");
        return;
    }
    /* The debug door the other server prompts have (OPENMMO_DIALOG and
     * friends, openmmo_boot.c): ask for the travel list once the field
     * settles, so a headless run reaches the list without a PC. */
    if (openmmo_dev_env("OPENMMO_TRAVEL") != NULL) {
        s_requested = 1;
        printf("openmmo: will ask the server for the PC travel list after join\n");
    }
}

/* Called from ScrCmd_AddMenuEntryImm ahead of every row: the row before
 * SWITCH OFF is where TRAVEL goes, and the first PC row of a menu is how
 * this learns the PC is being used at all. Only with a server seated: a
 * menu with a TRAVEL row nothing answers is worse than none. */
void openmmo_travel_menu_entry(void *menuManager, unsigned entryStringID)
{
    FieldMenuManager *man = menuManager;
    mmo_charcode label[16];

    if (s_off || man == NULL || s_client == NULL || !s_seated)
        return;
    if (entryStringID != MenuEntries_Text_PC_SwitchOff)
        return;
    if (man->optionsCount >= FIELD_MENU_ENTRIES_MAX)
        return;
    if (openmmo_client_send_chat(s_client, "/pc here") != 0)
        return;
    /* Add a row off the bank the menu reads, then write TRAVEL over its
     * buffer: menuChoicesStrings[i].entry points at choicesStringsBuffers[i],
     * so the buffer is what the menu draws. */
    FieldMenuManager_AddMenuEntry(man, MenuEntries_Text_PC_SwitchOff, TRAVEL_MENU_INDEX);
    mmo_utf8_to_charcode("TRAVEL", label, sizeof label / sizeof label[0]);
    String_CopyChars(man->choicesStringsBuffers[man->optionsCount - 1],
                     (const charcode_t *)label);
}

/* Called from the menu's resume with what was picked. */
void openmmo_travel_menu_selected(unsigned selected)
{
    if (!s_off && selected == TRAVEL_MENU_INDEX && s_client != NULL && s_seated) {
        s_requested = 1;
        printf("openmmo: travel: picked; /pc once the PC is off\n");
    }
}

void openmmo_travel_tick(FieldSystem *fs, int seated)
{
    s_seated = seated;
    if (s_off || !s_requested || fs == NULL || s_client == NULL)
        return;
    /* The debug door does not wait for the seat: OPENMMO_BOOT_WORLD's path
     * never raises it, and the probe's whole point is a session with nobody
     * at a PC. A real pick still waits. */
    if (!seated && openmmo_dev_env("OPENMMO_TRAVEL") == NULL)
        return;
    /* The PC's own script is still logging off while fs->task is set; a
     * list opened under it would be a box inside a box. */
    if (fs->task != NULL || !FieldSystem_IsRunningFieldMap(fs)
        || FieldSystem_HasChildProcess(fs)) {
        return;
    }
    if (openmmo_client_send_chat(s_client, "/pc") != 0)
        return;
    s_requested = 0;
    printf("openmmo: travel: sending /pc\n");
}
