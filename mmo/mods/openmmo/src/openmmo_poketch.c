/* The device is the story's to switch on. */

#include <stdio.h>

#include "generated/vars_flags.h"

#include "field/field_system.h"
#include "field_system.h"
#include "poketch.h"
#include "savedata.h"

#include "../../../include/client.h"

/* How many of the twenty-five apps this save has. */
static int registered_apps(Poketch *poketch)
{
    int id, n = 0;

    for (id = 0; id < POKETCH_APPID_MAX; id++) {
        if (Poketch_IsAppRegistered(poketch, (enum PoketchAppID)id))
            n++;
    }
    return n;
}

/* Whether the server's seat says this character has been handed the Poketch.
 * Read off the seat rather than off VarsFlags: the seat lands in the join
 * burst but is written into the block on the first frame that has a field,
 * which is after the field has already asked whether the device is on. */
static int received_poketch(const openmmo_script_state *st)
{
    unsigned id = FLAG_RECEIVED_POKETCH;

    if (st == NULL || !st->seated)
        return 0;
    if (id >= (unsigned)MMO_SCRIPT_FLAG_MAX)
        return 0;
    return (st->flag[id >> 3] & (1u << (id & 7))) != 0;
}

void openmmo_poketch_seat(SaveData *save, const openmmo_script_state *st)
{
    Poketch *poketch;

    if (save == NULL)
        return;

    poketch = SaveData_GetPoketch(save);
    if (poketch == NULL)
        return;

    if (!received_poketch(st)) {
        printf("openmmo: no poketch on this character yet\n");
        return;
    }

    Poketch_Enable(poketch);
    printf("openmmo: poketch seated on, %d app(s) registered\n",
           registered_apps(poketch));
}

void openmmo_poketch_report(FieldSystem *fs)
{
    Poketch *poketch;
    PoketchSystem *sys;
    static int reported;

    if (reported || fs == NULL || fs->saveData == NULL)
        return;
    if (!FieldSystem_IsRunningFieldMap(fs))
        return;

    poketch = SaveData_GetPoketch(fs->saveData);
    sys = FieldSystem_GetPoketchSystem();
    printf("openmmo: poketch %s, system %s, %d app(s)\n",
           (poketch != NULL && Poketch_IsEnabled(poketch)) ? "on" : "off",
           sys != NULL ? "live" : "unavailable",
           poketch != NULL ? registered_apps(poketch) : 0);
    reported = 1;
}
