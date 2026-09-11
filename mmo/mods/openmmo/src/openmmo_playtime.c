/* The play-time clock, which the port never winds. */

#include "field/field_system.h"

#include "play_time.h"
#include "save_player.h"
#include "savedata.h"

#include "../../../include/platform.h"

/* The port's one supported accessor for the live field system; NULL when no
 * map is loaded. */
extern FieldSystem *pc_lab_field_system(void);

static int s_armed;
static long s_last;

void openmmo_playtime_tick(void)
{
    SaveData *save = SaveData_Ptr();
    long now;

    if (save == NULL)
        return;
    if (!s_armed) {
        if (pc_lab_field_system() == NULL)
            return;
        s_armed = 1;
        s_last = mmo_plat_seconds();
        return;
    }
    now = mmo_plat_seconds();
    if (now > s_last) {
        PlayTime *pt = SaveData_GetPlayTime(save);

        if (pt != NULL)
            PlayTime_Increment(pt, (u32)(now - s_last));
        s_last = now;
    }
}
