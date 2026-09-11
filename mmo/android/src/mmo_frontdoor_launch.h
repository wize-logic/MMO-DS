/* The desktop plan library, on the device. */
#ifndef MMO_FRONTDOOR_LAUNCH_H
#define MMO_FRONTDOOR_LAUNCH_H

#include <stddef.h>

#include "launch_plan.h"

/* Point the plan library at the app's own external directory. Once. */
void fdl_root(const char *external_dir);

/* Where the saves live: <root>/save. Empty until fdl_root. */
const char *fdl_save_dir(void);

/* The last run's leftovers, each turned into a sentence appended to `note`:
 * an offline record still open, an export the game wrote on its way out, a
 * report the server took. */
void fdl_housekeep(char *note, size_t cap);

/* A cartridge slot's answer for a file at `path`: 1 when it is that
 * cartridge, else 0 with the launcher's own sentence in `why`. */
int fdl_slot_file(int slot, const char *path, char *why, size_t cap);

/* The same from the first sixteen bytes of the image, for a document the
 * system picker granted and no path can reach. */
int fdl_slot_header(int slot, const unsigned char *hdr, size_t n,
                    char *why, size_t cap);

/* Whether an offline save newer than the server's copy is waiting. 1 with
 * the two play times (server_seconds is -1 for a save that never came from
 * a session), 0 for nothing to offer. */
int fdl_offer(long *save_seconds, long *server_seconds);

/* The environment of a session: PC_SAVE none, the export handoff the game
 * writes Continue Offline into, and, when the player took the offer, the
 * report and the session records behind it. 0, or -1 with a sentence. */
int fdl_prepare_online(int take, char *note, size_t cap);

/* The environment of an offline run: the save, the clock and the recording,
 * with the session's record begun and remembered for the next front door to
 * close. 0, or -1 with a sentence. */
int fdl_prepare_offline(char *note, size_t cap);

/* The stamped saves, newest first; and putting one back. */
int fdl_saves(char out[][MMO_LAUNCH_STAMP], int max);
int fdl_restore(const char *stamp, char *note, size_t cap);

/* The soundtrack's package, composed from the player's cartridges when it is
 * not already under <root>/mods. `pt`, `hg` and `bw` are files. 0, or -1
 * with the reason. */
int fdl_compose(int track, const char *pt, const char *hg, const char *bw,
                void (*note)(void *ud, const char *line), void *ud,
                char *err, size_t errcap);

/*
 * The follower package, filled from the player's own Heart Gold when it is not already under
 * <root>/mods.
 */
/* The species, moves and abilities a Black or White cartridge adds, filled
 * from the player's own Platinum and Black into the app's mods folder. Same
 * answers as fdl_compose_followers. */
int fdl_compose_species(const char *pt, const char *bw, int world,
                        void (*note)(void *ud, const char *line), void *ud,
                        char *err, size_t errcap);

int fdl_compose_followers(const char *hg, int world,
                          void (*note)(void *ud, const char *line), void *ud,
                          char *err, size_t errcap);

#endif
