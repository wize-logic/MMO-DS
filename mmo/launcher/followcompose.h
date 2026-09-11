#ifndef MMO_FOLLOWCOMPOSE_H
#define MMO_FOLLOWCOMPOSE_H

#include <stddef.h>

#include "launch_plan.h"

/* Fill the follower package from one Heart Gold or Soul Silver image. */
int mmo_followcompose(const char *rom, const char *pkg,
                      int mmodel_first, int emote_at,
                      char *err, size_t errcap);

/* One archive's base over a comma-separated package list: one past the
 * highest member any of them claims, or `floor_count` when none does. `root`
 * is the folder those packages live in; a `followers` row is skipped, since
 * this package cannot allocate around itself. */
int mmo_followcompose_base(const char *root, const char *others,
                           const char *archive, int floor_count);

/*
 * The Play-time half: the follower package exists under the install's mods folder with the
 * current tables' stamp, or is filled right here from the cartridge the settings name.
 */
int mmo_followcompose_ensure(const mmo_launch_settings *s,
                             const char *port_exe,
                             void (*note)(void *ud, const char *line),
                             void *ud, char *err, size_t errcap);

/* Where the package lands, for the launch plan's mods list. Writes the
 * package's directory name (not a path), and returns 1 when the directory is
 * actually there. */
int mmo_followcompose_pkg(const mmo_launch_settings *s, const char *port_exe,
                          char *name, size_t namecap);

#endif
