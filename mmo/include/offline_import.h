/* A save file, in the bytes the server reads it out of. */
#ifndef MMO_OFFLINE_IMPORT_H
#define MMO_OFFLINE_IMPORT_H

#include <stddef.h>

#include "codec.h"
#include "mmo.h"

/* The magic and the version at the head of every report. A reader that does
 * not know the version says so and refuses; it never guesses at the fields. */
#define MMO_IMPORT_MAGIC   "OMIR"
#define MMO_IMPORT_VERSION 4

/* Six stats, in the server's own PokemonStat order: HP, ATK, DEF, SPA, SPD,
 * SPE. The save has exactly six of each and the client has no id space to name
 * them by, so the order is the agreement. */
#define MMO_IMPORT_STATS 6

/*
 * The five contest conditions, in contest-type order: cool, beauty, cute, smart, tough. Four
 * ranks of Super Contest ribbon per condition, so the ribbon mask is twenty bits.
 */
#define MMO_IMPORT_CONDITIONS   5
#define MMO_IMPORT_RIBBON_RANKS 4

/* What one report may hold. These are the client's own ceilings and the server
 * carries the same numbers: six in the party plus eighteen boxes of thirty is
 * 546, so 1024 is generous, and a report past one of these is a bug on this
 * side rather than a save. */
#define MMO_IMPORT_MAX_MONSTERS 1024
#define MMO_IMPORT_MAX_BAG      2048
#define MMO_IMPORT_MAX_DEX      2048
#define MMO_IMPORT_MAX_FLAGS    8192
#define MMO_IMPORT_MAX_VARS     4096
#define MMO_IMPORT_MAX_BLOCKS   64

/* One move slot. */
typedef struct {
    u16 move;
    u8  pp;
    u8  pp_ups;
} mmo_import_move;

/* One monster, exactly as the save holds it. `box` is 0 for the party and 1
 * for storage; `slot` is the index within whichever it is (boxes are numbered
 * straight through, box 2 slot 0 being slot 30). */
typedef struct {
    s32 pid;
    u16 dex;
    u8  form;
    u8  level;
    s32 xp;
    u8  ivs[MMO_IMPORT_STATS];
    u8  evs[MMO_IMPORT_STATS];
    mmo_import_move moves[4];
    int nmoves;
    char nickname[32];
    char ot_name[32];
    s32 ot_id;
    u16 ability;
    u8  hidden_ability;
    u8  nature;
    u8  shiny;
    u16 held_item;
    u8  friendship;
    u8  egg;
    u8  egg_cycles;
    u8  box;
    u16 slot;
    /* What a poffin and a contest left behind: the five conditions the visual
     * and dance rounds score on, how saturated with poffins it is, and a bit
     * per Super Contest ribbon at bit `type * MMO_IMPORT_RIBBON_RANKS + rank`,
     * which is the engine's own `ribbonsDS2` layout. */
    u8  cond[MMO_IMPORT_CONDITIONS];
    u8  sheen;
    u64 ribbons_super;
    /*
     * Where the save says it was caught, which is a location label and not a map: the engine
     * stamps a caught monster with the label of the map the battle was on
     * (`BattleSystem_SetPokemonCatchData`), and 593 map headers share 125 labels, so there is
     * no map to read back out of it.
     */
    u16 met_location;
    /* The ball it is in, as a wire item id, or 0 for a ball with none. */
    u16 ball;
    u8  pokerus;
    u8  markings;
    /*
     * What it is suffering from: the engine's own twelve-bit condition word, already masked to
     * the bits that engine defines, and 0 for a healthy one. Last in the row because that is
     * where the live wire carries it too.
     */
    u16 status;
} mmo_import_mon;

typedef struct {
    u16 item;
    u16 quantity;
} mmo_import_item;

/* Where the save left the player. There is no region: a save read with no
 * server behind it cannot know the server's region ids, and the map header the
 * engine holds is a bank and a map. The server fills the region in from the
 * character the save is landing on. */
typedef struct {
    u8  bank;
    u16 map;
    s16 x;
    s16 y;
} mmo_import_pos;

typedef struct {
    u16 id;
    s16 value;
} mmo_import_var;

/* One engine save block, carried whole and never read. `data` is borrowed for
 * the length of the encode. */
typedef struct {
    u8           id;
    const void  *data;
    size_t       len;
} mmo_import_block;

/* A whole report, as the caller fills it in. The arrays are borrowed. */
typedef struct {
    char sha256[65];      /* of the save FILE; recognises the same save twice */
    s32  client_revision; /* -1 where the build carries no revision */
    s32  trainer_id;
    s32  money;
    s32  badges;          /* the engine's own bitfield, one bit a badge */
    s32  play_seconds;
    mmo_import_pos position;
    /*
     * Where a white out would have put the player: the engine's black-out warp id, which is a
     * 1-based row of its own spawn table (`spawn_locations.c`) and not a map.
     */
    u16  black_out_warp;

    const mmo_import_mon  *monsters;
    int                    nmonsters;
    const mmo_import_item *bag;
    int                    nbag;
    const u16             *dex_seen;
    int                    ndex_seen;
    const u16             *dex_caught;
    int                    ndex_caught;
    const u16             *flags;   /* engine flag numbers that are set */
    int                    nflags;
    const mmo_import_var  *vars;    /* engine variable numbers and values */
    int                    nvars;
    const mmo_import_block *blocks;
    int                    nblocks;
} mmo_import_report;

/* Encode `r` into `w`. Returns 0, or -1 with `w->err` set on an allocation
 * failure or a list past its ceiling above. */
int mmo_import_report_encode(mmo_wbuf *w, const mmo_import_report *r);

/*
 * Write the encoded report to `path`, through a temporary and a rename so a crash mid-write
 * never leaves half a report where a whole one was. 0 on success; -1 with a line on stderr
 * saying which step refused.
 */
int mmo_import_report_write(const char *path, const mmo_import_report *r);

/* Read a report file whole. On success *out is malloc'd and *n is its length;
 * the caller frees. Refuses a file that is not a report of a version this
 * build writes, and says which. 0 on success, -1 otherwise. */
int mmo_import_report_read(const char *path, u8 **out, size_t *n);

/*
 * What a report says about itself, without decoding the rest of it: the play time in seconds,
 * the save hash and the wallet. Any pointer may be NULL.
 */
int mmo_import_report_stamp(const u8 *report, size_t n,
                            s32 *play_seconds, char sha256[65], s32 *money);

#endif /* MMO_OFFLINE_IMPORT_H */
