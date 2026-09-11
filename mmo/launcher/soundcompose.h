#ifndef MMO_SOUNDCOMPOSE_H
#define MMO_SOUNDCOMPOSE_H

#include <stddef.h>

/*
 * Compose one sound package archive from the player's own cartridges. track/font: 0 platinum,
 * 1 heartgold, 2 blackwhite.
 */
int mmo_soundcompose(int track, int font, const char *pt_rom,
                     const char *hg_rom, const char *bw_rom,
                     const char *out_path, char *err, size_t errcap);

/*
 * The 156 cries a Gen 5 cartridge adds, decoded to host PCM and packed into one file for the
 * imports package (mods/openmmo/src/openmmo_cries.c reads it).
 */
int mmo_soundcompose_cries(const char *bw_rom, const char *out_path,
                           char *err, size_t errcap);

#include "launch_plan.h"

/* The pair the settings name (font follows the soundtrack unless pinned);
 * 0 means platinum-with-platinum, the stock archive. */
int mmo_sound_pair(const mmo_launch_settings *s, int *track, int *font);

/* One cartridge slot's answer, the official way: the file it resolves to
 * (an explicit row, or the one found beside Platinum) and one line saying
 * what was read there. slot: 1 Heart Gold, 2 Black / White. Returns 1
 * resolved, 0 not; either output may be NULL. */
int mmo_sound_slot_status(const mmo_launch_settings *s, int slot,
                          char *file, size_t filecap,
                          char *why, size_t whycap);

/* The Play-time half: the named pair's package exists under the install's
 * mods folder with the current tables' stamp, or is composed right here
 * from the player's cartridges. A missing cartridge is a refusal that
 * names the slot; the launcher stays standing either way. */
int mmo_soundcompose_ensure(const mmo_launch_settings *s,
                            const char *port_exe,
                            void (*note)(void *ud, const char *line),
                            void *ud, char *err, size_t errcap);

#endif
