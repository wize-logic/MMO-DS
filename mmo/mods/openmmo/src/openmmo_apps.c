/* The engine's own screens, opened from the window's HUD bar. */

#include <stdio.h>
#include <string.h>

#include "field/field_system.h"
#include "field_system.h"
#include "field_task.h"
#include "party.h"
#include "pokedex.h"
#include "save_player.h"
#include "savedata.h"
#include "start_menu.h"

#include "../../../include/hud_channel.h"
#include "../../../include/platform.h"

/* start_menu.c's patched direct entry; its app codes, in its order. */
void StartMenu_OpenApp(FieldSystem *fieldSystem, int app, int slot);
enum {
    APPS_DIRECT_BAG = 0,
    APPS_DIRECT_PARTY,
    APPS_DIRECT_DEX,
    APPS_DIRECT_TRAINER,
    APPS_DIRECT_OPTIONS,
    APPS_DIRECT_SUMMARY
};

/* How long a press waits, and why it does not wait forever. */
#define APPS_HOLD_SECONDS 3

static int s_want = -1;     /* the screen the window asked for */
static int s_want_slot;     /* SUMMARY's party slot, from the argument */
static long s_want_at;      /* when it was asked for, for the wait above */
static int s_running;       /* a direct-opened sequence is still on its way */
static int s_held_why = -1; /* why the last held request could not open */

static const char *screen_name(int screen)
{
    switch (screen) {
    case OPENMMO_HUD_SCREEN_BAG:     return "bag";
    case OPENMMO_HUD_SCREEN_PARTY:   return "party";
    case OPENMMO_HUD_SCREEN_DEX:     return "pokedex";
    case OPENMMO_HUD_SCREEN_TRAINER: return "trainer card";
    case OPENMMO_HUD_SCREEN_OPTIONS: return "options";
    case OPENMMO_HUD_SCREEN_START:   return "start menu";
    case OPENMMO_HUD_SCREEN_SUMMARY: return "summary";
    default:                         return "?";
    }
}

void openmmo_apps_request(int arg)
{
    /* The low byte is the screen; SUMMARY carries its party slot above it
     * (hud_channel.h). */
    int screen = arg & 0xFF;
    int slot = (arg >> 8) & 0xFF;

    if (screen < 0 || screen >= OPENMMO_HUD_SCREEN_N) {
        printf("openmmo: hud asked for screen %d, which is not one\n", screen);
        return;
    }
    /* The newest ask wins. Two presses while a cutscene runs should open one
     * screen when it ends, not queue a pile of them. */
    s_want = screen;
    s_want_slot = slot;
    s_want_at = mmo_plat_seconds();
    printf("openmmo: hud asked for the %s screen\n", screen_name(screen));
}

/* Nonzero while a screen this file opened is on the way up, up, or on the
 * way down: the HUD is drawn over the guest and has to stand aside. */
int openmmo_apps_busy(void)
{
    return s_running || s_want >= 0;
}

static int direct_code(int screen)
{
    switch (screen) {
    case OPENMMO_HUD_SCREEN_BAG:     return APPS_DIRECT_BAG;
    case OPENMMO_HUD_SCREEN_PARTY:   return APPS_DIRECT_PARTY;
    case OPENMMO_HUD_SCREEN_DEX:     return APPS_DIRECT_DEX;
    case OPENMMO_HUD_SCREEN_TRAINER: return APPS_DIRECT_TRAINER;
    case OPENMMO_HUD_SCREEN_OPTIONS: return APPS_DIRECT_OPTIONS;
    case OPENMMO_HUD_SCREEN_SUMMARY: return APPS_DIRECT_SUMMARY;
    default:                         return -1;
    }
}

/* A screen this save cannot open yet is refused here, out loud: the window
 * draws the button disabled off the page's own facts, but a command can
 * still arrive (a stale click, a headless push), and the engine's own menu
 * would simply not have offered the row. */
static int refused(FieldSystem *fs, int screen)
{
    extern int openmmo_underground_active(void);

    /*
     * The Underground opens none of them, and this is a crash rather than a cosmetic refusal.
     */
    if (openmmo_underground_active()) {
        printf("openmmo: the %s screen does not open in the Underground\n",
               screen_name(screen));
        return 1;
    }
    if (screen == OPENMMO_HUD_SCREEN_DEX &&
        !Pokedex_IsObtained(SaveData_GetPokedex(fs->saveData))) {
        printf("openmmo: no pokedex on this save; the screen stays shut\n");
        return 1;
    }
    if (screen == OPENMMO_HUD_SCREEN_PARTY ||
        screen == OPENMMO_HUD_SCREEN_SUMMARY) {
        Party *party = SaveData_GetParty(fs->saveData);
        int need = screen == OPENMMO_HUD_SCREEN_SUMMARY ? s_want_slot + 1 : 1;

        if (party == NULL || Party_GetCurrentCount(party) < need) {
            printf("openmmo: no party member there; the screen stays shut\n");
            return 1;
        }
    }
    return 0;
}

/* Whether the press this is still holding has been held too long. Says so
 * once, where the waiting line said why it was waiting, so a log reads as one
 * story: asked for, waited on for a reason, then let go of. */
static int hold_expired(void)
{
    if (mmo_plat_seconds() - s_want_at <= APPS_HOLD_SECONDS)
        return 0;
    printf("openmmo: the %s screen waited %d seconds for the field and was"
           " dropped; it is not opened over whatever came next\n",
           screen_name(s_want), APPS_HOLD_SECONDS);
    s_want = -1;
    s_held_why = -1;
    return 1;
}

void openmmo_apps_pump(FieldSystem *fs)
{
    if (fs == NULL)
        return;

    /* The sequence is over when the field map is back with no task on it, 
     * true after the plain close's fade-in, and after a used item's own
     * field task (the descent, a surf) has run out too. */
    if (s_running && FieldSystem_IsRunningFieldMap(fs) && fs->task == NULL)
        s_running = 0;

    if (s_want < 0)
        return;

    /* The engine's own start menu is a field task and cleans up after
     * itself; the page keeps the command for compatibility. */
    if (s_want == OPENMMO_HUD_SCREEN_START) {
        if (refused(fs, s_want)) {
            s_want = -1;
            return;
        }
        if (fs->task != NULL || !FieldSystem_IsRunningFieldMap(fs)
            || FieldSystem_HasChildProcess(fs)) {
            hold_expired();
            return;
        }
        StartMenu_Open(fs);
        printf("openmmo: start menu opened from the hud\n");
        s_want = -1;
        return;
    }

    /* The same three conditions every screen in this tree opens under: no
     * field task (a line of dialog is one), the map running, and no other
     * app already up. */
    if (fs->task != NULL || !FieldSystem_IsRunningFieldMap(fs)
        || FieldSystem_HasChildProcess(fs)) {
        int why = (fs->task != NULL ? 1 : 0)
                  | (FieldSystem_IsRunningFieldMap(fs) ? 0 : 2)
                  | (FieldSystem_HasChildProcess(fs) ? 4 : 0);

        /* Said once per reason, not once per frame: a request held for a
         * whole cutscene must not be a log of ten thousand lines, and one
         * held forever must not be silent. */
        if (why != s_held_why) {
            s_held_why = why;
            printf("openmmo: the %s screen is waiting (task %d, map %d,"
                   " app %d)\n", screen_name(s_want), (why & 1) ? 1 : 0,
                   (why & 2) ? 0 : 1, (why & 4) ? 1 : 0);
        }
        hold_expired();
        return;
    }
    s_held_why = -1;
    if (refused(fs, s_want)) {
        s_want = -1;
        return;
    }
    StartMenu_OpenApp(fs, direct_code(s_want), s_want_slot);
    printf("openmmo: %s screen opened from the hud\n", screen_name(s_want));
    s_want = -1;
    s_running = 1;
}
