/*
 * The species a Gen 5 cartridge adds, filled on the player's own machine.
 */
#ifndef MMO_SPECIESCOMPOSE_H
#define MMO_SPECIESCOMPOSE_H

#include <stddef.h>

#include "launch_plan.h"

/*
 * The five archives a species is looked up in: what it is (pl_personal), what it learns
 * (wotbl), what it becomes (evo), what it looks like in a box (pl_poke_icon) and what it is
 * Called (the two message banks).
 */
int mmo_speciescompose_tables(const char *bw_rom, const char *pt_rom,
                              const char *pkg,
                              void (*note)(void *ud, const char *line),
                              void *ud, char *err, size_t errcap);

/*
 * One Gen 4 species entry synthesised from a Gen 5 one, exposed for the test that holds this
 * file and portspecies.py together over the shared range: the 493 species both games have are
 * rebuilt from the cartridge and held against this game's own archive, which is the whole
 * oracle for the field mapping.
 */
void mmo_speciescompose_entry(const unsigned char *gen5, unsigned char *out,
                              int machines);

/* One Gen 4 evolution member (44 bytes) from a Gen 5 one (42), exposed for the
 * same reason: 492 of the 493 shared species rebuild byte for byte. */
void mmo_speciescompose_evolution(const unsigned char *gen5,
                                  unsigned char *out);

/* The animation archive, which is a carry rather than a composition. */
int mmo_speciescompose_anim(const char *bw_rom, const char *pkg,
                            void (*note)(void *ud, const char *line),
                            void *ud, char *err, size_t errcap);

/* The battle art, which is assembled rather than carried. */
int mmo_speciescompose_sheets(const char *bw_rom, const char *pt_rom,
                              const char *pkg,
                              void (*note)(void *ud, const char *line),
                              void *ud, char *err, size_t errcap);

/*
 * The 92 moves the same cartridge adds, which are six archives and not one: the 16-byte table
 * entry, three message banks, the animation script and the battle stub a move id indexes. All
 * of them or none.
 */
int mmo_speciescompose_moves(const char *bw_rom, const char *pt_rom,
                             const char *pkg,
                             void (*note)(void *ud, const char *line),
                             void *ud, char *err, size_t errcap);

/* The 41 abilities the same cartridge adds: three message banks, and the same
 * shape of oracle as the rest, the 123 both games name must come off the
 * cartridge spelling this game's own entry character for character, which is
 * the whole claim that Gen 5 appended rather than renumbered. 0, or -1. */
int mmo_speciescompose_abilities(const char *bw_rom, const char *pt_rom,
                                 const char *pkg,
                                 void (*note)(void *ud, const char *line),
                                 void *ud, char *err, size_t errcap);

/* A Gen 4 species entry is 44 bytes; `diff` below is one count per offset. */
#define MMO_SPECIESCOMPOSE_ENTRY 44

/* The shared-range oracle, and it is the whole reason to trust any of this. */
int mmo_speciescompose_check(const char *bw_rom, const char *pt_rom,
                             int *diff, char *err, size_t errcap);

/*
 * The Play-time half: the `imports` package exists under the install's mods folder with this
 * build's stamp, or is filled right here from the player's own Black and Platinum.
 */
int mmo_speciescompose_ensure(const mmo_launch_settings *s,
                              const char *port_exe,
                              void (*note)(void *ud, const char *line),
                              void *ud, char *err, size_t errcap);

#endif
