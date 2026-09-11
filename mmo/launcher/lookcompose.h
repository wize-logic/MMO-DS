/*
 * The four player looks this engine does not ship, filled on the player's own
 * machine out of their HeartGold, Black and Platinum.
 */
#ifndef MMO_LOOKCOMPOSE_H
#define MMO_LOOKCOMPOSE_H

#include <stddef.h>

#include "launch_plan.h"

/* Fill the `looks` package from the three images. */
int mmo_lookcompose(const char *hg_rom, const char *bw_rom, const char *pt_rom,
                    const char *pkg, int mmodel_first, int class_first,
                    int back_first, const char *others,
                    char *err, size_t errcap);

/*
 * The Play-time half: the package exists under the install's mods folder with this build's
 * stamp at the bases the other packages leave, or is filled right here.
 */
int mmo_lookcompose_ensure(const mmo_launch_settings *s, const char *port_exe,
                           void (*note)(void *ud, const char *line),
                           void *ud, char *err, size_t errcap);

#endif
