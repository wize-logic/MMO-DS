/*
 * What the front door remembers, and the two command lines it turns that into.
 */

#ifndef OPENMMO_LAUNCH_PLAN_H
#define OPENMMO_LAUNCH_PLAN_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MMO_LAUNCH_PATH  512
#define MMO_LAUNCH_TEXT  128
#define MMO_LAUNCH_NAME   64
#define MMO_LAUNCH_BIND  256
#define MMO_LAUNCH_PADS   12

/* The window's --layout, --filter and its two fitting flags, in the order the
 * menu cycles them. The names are the window's own spelling. */
enum { MMO_LAYOUT_SMART = 0, MMO_LAYOUT_STACKED, MMO_LAYOUT_WIDE, MMO_LAYOUT_FILL,
       MMO_LAYOUT_N };
enum { MMO_FILTER_NEAREST = 0, MMO_FILTER_LINEAR, MMO_FILTER_SCALE2X, MMO_FILTER_N };
enum { MMO_FIT_ASPECT = 0, MMO_FIT_INTEGER, MMO_FIT_STRETCH, MMO_FIT_N };

/* The port's frame pacing: the console's 60 fps, or as fast as the machine
 * runs. Unlimited is PC_PACE=0; console is the port's default and travels as
 * nothing at all. */
enum { MMO_PACE_CONSOLE = 0, MMO_PACE_UNLIMITED, MMO_PACE_N };

/*
 * What the mixer puts out is not a setting. Every session mixes at 48,000 samples a second
 * with cubic interpolation between a channel's samples (PC_AUDIO_RATE=48000,
 * PC_AUDIO_INTERP=cubic): the stream a device plays as it is.
 */

/* Whose music Sinnoh plays. Platinum is the game's own archive; anything
 * else is a composed package (`make -C mmo soundtrack`) built from the
 * player's own cartridges, one per soundtrack/soundfont pair, and picking
 * one that is not composed yet is refused by name rather than played past. */
enum { MMO_SOUNDTRACK_PLATINUM = 0, MMO_SOUNDTRACK_HEARTGOLD,
       MMO_SOUNDTRACK_BLACKWHITE, MMO_SOUNDTRACK_N };

/* Whose instruments play under whatever Platinum still renders. */
enum { MMO_SOUNDFONT_FOLLOW = 0, MMO_SOUNDFONT_PLATINUM,
       MMO_SOUNDFONT_HEARTGOLD, MMO_SOUNDFONT_BLACKWHITE, MMO_SOUNDFONT_N };

/*
 * What a pad button means, which on a DS was the only rebinding there was and here is the one
 * control setting the game itself used to keep.
 */
enum { MMO_BUTTON_NORMAL = 0, MMO_BUTTON_START_IS_X, MMO_BUTTON_L_IS_A,
       MMO_BUTTON_N };

#define MMO_LAUNCH_SCALE_AUTO 0
#define MMO_LAUNCH_SCALE_MAX  8
#define MMO_LAUNCH_RS_MAX     4
/*
 * The internal 3D resolution, sold as the three steps a player recognises: Sd is the engine
 * drawing at 2x, HD at 3x, ULTRA at 4x, the port's own ceiling (PC_VIEW_HD_MAX).
 */
#define MMO_LAUNCH_HD3D_MIN   2
#define MMO_LAUNCH_HD3D_MAX   4
#define MMO_LAUNCH_HD3D_N     (MMO_LAUNCH_HD3D_MAX - MMO_LAUNCH_HD3D_MIN + 1)
#define MMO_LAUNCH_VIEWPORT   16
/* How far out the field camera stands, as a percentage of the official client's own per-map distance. */
#define MMO_LAUNCH_CAMERA_MIN 100
#define MMO_LAUNCH_CAMERA_MAX 130
#define MMO_LAUNCH_CAMERA     MMO_LAUNCH_CAMERA_MAX

/*
 * One session's settings. An empty string means "not chosen", and the plan omits what was not
 * chosen rather than substituting a value of its own.
 */
typedef struct {
    char user[MMO_LAUNCH_NAME];
    char pass[MMO_LAUNCH_NAME];
    char rom[MMO_LAUNCH_PATH];
    char audio_device[MMO_LAUNCH_TEXT];
    /* Where the signed inventory lives and which key signs it. Both empty is
     * an install nobody publishes updates for, which is what a build tree is,
     * and the check is skipped. One set without the other is refused: a feed
     * read without a key is a feed nobody vouched for. */
    char feed[MMO_LAUNCH_PATH];         /* directory holding the four feed files */
    char feed_key[MMO_LAUNCH_PATH];     /* PEM public key the feeds are signed by */
    char feed_url[MMO_LAUNCH_PATH];     /* https:// (or http://) base the update
                                         * is fetched from; empty = the install
                                         * only checks, never fetches. The
                                         * signature is the trust either way
                                         * (update.h); TLS keeps the path honest
                                         * (fetch.h). */
    char feed_ca[MMO_LAUNCH_PATH];      /* PEM certificates trusted beside the
                                         * compiled-in roots, for an https
                                         * channel under a private authority;
                                         * empty = the public roots alone */
    int  scale;                         /* 0 = auto, else 1..MMO_LAUNCH_SCALE_MAX */
    char viewport[MMO_LAUNCH_VIEWPORT]; /* auto, native, 16:9, 16:10, 4:3, or N */
    int  camera;                        /* field camera distance, percent of
                                         * the official client's per-map value; 100..130,
                                         * OPENMMO_CAMERA_DISTANCE when not 100 */
    int  render_scale;                  /* 1..MMO_LAUNCH_RS_MAX */
    int  hd3d;                          /* MMO_LAUNCH_HD3D_MIN..MAX, PC_HD3D;
                                         * written to the file as sd or hd */
    int  pace;                          /* MMO_PACE_*, unlimited = PC_PACE=0 */
    int  layout;
    int  filter;
    int  fit;
    int  button_mode;
    int  fullscreen;
    int  audio;                         /* 0 emits --no-audio */
    /* Whether Discord is told where the player is and who they are. On by
     * default, the way a game with a presence line normally ships; the game
     * reads it as OPENMMO_DISCORD and opens nothing without it. */
    int  discord;
    int  music;                         /* 0..100, default 100; OPENMMO_MUSIC */
    int  sfx;                           /* 0..100, default 100; OPENMMO_SFX */
    int  soundtrack;                    /* MMO_SOUNDTRACK_*, appends the
                                         * composed package to PC_MODS */
    int  soundfont;                     /* MMO_SOUNDFONT_*, picks which
                                         * pair's package that is */
    char rom_hg[MMO_LAUNCH_PATH];       /* a Heart Gold cartridge, when it
                                         * is not beside the Platinum one */
    char rom_bw[MMO_LAUNCH_PATH];       /* a Black or White cartridge, same */
    /* Runtime content packages. The engine reads these as PC_MODS / PC_MODS_DIR
     * at boot (pc_modfs_boot). Empty means not chosen: the plan omits them,
     * and an unset PC_MODS loads nothing. Listing the compile-time plugin
     * (`openmmo`) here is a boot error on the engine. */
    char mods[MMO_LAUNCH_TEXT];         /* package list, e.g. bodies,hub */
    char mods_dir[MMO_LAUNCH_PATH];     /* folder of packages; empty = omit */
    /* The official client's theme for the window's in-game UI: an extracted
     * client's data/themes/default. The art never ships with this client,
     * so empty means the window's own primitive look. */
    char theme[MMO_LAUNCH_PATH];        /* --theme for the window; empty = omit */
    /*
     * The player's rebinds, in the window's own --bind syntax (`a=Z,start=Space`, SDL key
     * names), holding only what differs from the window's defaults. The map still belongs to
     * the window, this is the row it is handed, written by the launcher's Controls panel.
     */
    char bind[MMO_LAUNCH_BIND];
    /* Which offline saved game this install plays, 1..MMO_LAUNCH_SLOTS. */
    int  slot;
} mmo_launch_settings;

/* The window's own names for the cycled settings; NULL for an out-of-range id. */
const char *mmo_launch_layout_name(int layout);
const char *mmo_launch_filter_name(int filter);
const char *mmo_launch_fit_name(int fit);
const char *mmo_launch_pace_name(int pace);
const char *mmo_launch_button_mode_name(int mode);
/* sd or hd for the two resolutions; the number stays out of sight. */
const char *mmo_launch_hd3d_name(int hd3d);

/*
 * The twelve pad buttons the window binds, indexed 0..MMO_LAUNCH_PADS-1 in the DS's own order.
 */
const char *mmo_launch_pad_name(int pad);
const char *mmo_launch_pad_label(int pad);
const char *mmo_launch_pad_default(int pad);

/*
 * Read one pad's current key out of a bind spec, the default when the spec does not name it,
 * the last item when it names it twice, which is how the window reads the same string. Returns
 * 0, or -1 for a pad out of range.
 */
int mmo_launch_bind_get(const char *spec, int pad, char *key, size_t cap);

/*
 * Rebind one pad and rewrite the spec, keeping only what differs from the defaults, so a spec
 * that says nothing but the defaults is the empty string, and the row disappears from the
 * file. Returns 0, or -1 when it does not fit or the arguments are out of range.
 */
int mmo_launch_bind_set(char *spec, size_t cap, int pad, const char *key);

/* Whether a spec is well-formed: PAD=KEY items joined by commas, every pad
 * one of the twelve, every key non-empty. The key NAMES are SDL's and only
 * the window can resolve them; a name it does not know fails the launch
 * with the window's own message. Returns 0, or -1. */
int mmo_launch_bind_check(const char *spec);

/* Everything unset, the window's own defaults for the rest. */
void mmo_launch_defaults(mmo_launch_settings *s);

/* Where the settings are kept: $XDG_CONFIG_HOME/openmmo/launcher.cfg, or $HOME/.config/... . */
int mmo_launch_config_path(char *out, size_t cap);

/*
 * The config file is `key value` lines, `#` comments, one setting per line, and an unknown key
 * is an error rather than a silent drop, a typo that reads as a default is the failure this
 * format exists to avoid.
 */
int mmo_launch_parse(const char *text, mmo_launch_settings *s,
                     char *err, size_t errcap);

/* The inverse: the bytes a config file should hold. Returns the length, or -1
 * if `cap` is too small. Round-trips through mmo_launch_parse. */
int mmo_launch_format(const mmo_launch_settings *s, char *out, size_t cap);

/* Read/write that file. load returns 0 on success, 1 if there is no file yet
 * (leaving `s` untouched), 2 on success from a file that still holds the
 * retired `pass` row and wants writing back out without it, -1 on a malformed
 * one. save returns 0 or -1. */
int mmo_launch_load(const char *path, mmo_launch_settings *s,
                    char *err, size_t errcap);
int mmo_launch_save(const char *path, const mmo_launch_settings *s);

/* Whether these settings can start a game, with the reason if not. */
int mmo_launch_check(const mmo_launch_settings *s, char *err, size_t errcap);

/*
 * The cartridge the game will open. `path` may be the .nds itself or the directory that holds
 * pokeplatinum.us.nds.
 */
int mmo_launch_rom_file(const char *path, char *out, size_t cap);

/*
 * Three cartridges, not one. A session needs the player's own backups of Platinum, which the
 * game runs on, and of Heart Gold and Black, which the packages it cooks are filled from, and
 * the front door refuses Play until all three are there.
 */
typedef struct {
    char platinum[MMO_LAUNCH_PATH];
    char heartgold[MMO_LAUNCH_PATH];
    char black[MMO_LAUNCH_PATH];
} mmo_launch_roms;

/* Fill `out` from the settings. Returns 0 with all three resolved, or -1 with
 * `err` naming what is missing or what a named file turned out to be, in the
 * words the front door shows. */
int mmo_launch_roms_resolve(const mmo_launch_settings *s, mmo_launch_roms *out,
                            char *err, size_t errcap);

/* The slots, in the order the sentence names them. */
enum {
    MMO_LAUNCH_CART_PLATINUM = 0,
    MMO_LAUNCH_CART_HEARTGOLD = 1,
    MMO_LAUNCH_CART_BLACK = 2
};

/* The slot's name on the box. */
const char *mmo_launch_cart_name(int slot);

/* The four-character game code at 0x0C of an .nds image, into `code` (five
 * bytes), or NULL when the file cannot be read that far. */
const char *mmo_launch_rom_code(const char *path, char *code);

/* Whether a header code is a cartridge that fills `slot`. */
int mmo_launch_cart_is(int slot, const char *code);

/* The first .nds in `dir` whose header fills `slot`, into `out`. 1 when one
 * was found; 0 otherwise, an empty or unreadable `dir` included. */
int mmo_launch_cart_scan(const char *dir, int slot, char *out, size_t cap);

/* The folder the working tree keeps cartridges in (mmo_launch_roms_dir),
 * searched after the Platinum image's own folder. A release names none. */
void mmo_launch_roms_fallback(const char *dir);
const char *mmo_launch_roms_fallback_dir(void);

/* A per-process channel name, so two launchers on one machine cannot publish
 * into one page. */
void mmo_launch_chan(char *out, size_t cap, long pid);

/* The cartridge image the game reads its assets out of. Not supplied by this
 * build and never shipped beside it: it is game data a player builds or owns.
 * The name is the one the game's own default lookup uses. */
#define MMO_LAUNCH_ROM_NAME "pokeplatinum.us.nds"

/*
 * Where a player's cartridge images live: the repo's own `roms/` beside this program, which is
 * also where codegen's GBA images already are. Written empty when there is no such folder.
 */
void mmo_launch_roms_dir(const char *argv0, char *roms, size_t rcap);

/* Where the three files a launch needs are, given this program's own argv[0]. */
void mmo_launch_paths(const char *argv0, char *port, size_t pcap,
                      char *view, size_t vcap, char *rom, size_t rcap);

#define MMO_LAUNCH_MAX_ARGV 32
/*
 * Twenty-one names on the fullest plan, now that every panel setting is stated rather than
 * left to whatever the environment already said, and an offline one names a save file, a clock
 * and a recording as well (a session's names one place to export to).
 */
#define MMO_LAUNCH_MAX_ENV  28
#define MMO_LAUNCH_ARENA    6144

/* ------------------------------------------------------------------ */
/* Playing with no server                                              */
/* ------------------------------------------------------------------ */

/*
 * The second row on the front door. Offline the save file is the truth: the game is the port,
 * the clock is the host's, and the session is recorded, so everything about that run lives
 * under one folder the update never touches.
 */
#define MMO_LAUNCH_STAMP     20     /* YYYYmmdd-HHMMSS and a terminator */
#define MMO_LAUNCH_SAVE_KEEP 10
/* How many offline saved games one install holds. */
#define MMO_LAUNCH_SLOTS     4

/*
 * What a slot's first offline game starts with in its pocket, which is the cartridge's own
 * opening wallet (Platinum hands the player 3000 at its new game, `TrainerInfo` at
 * `sub_020270AC`).
 */
#define MMO_LAUNCH_FRESH_MONEY 3000

typedef struct {
    char stamp[MMO_LAUNCH_STAMP];   /* this session's name on disk */
    char dir[MMO_LAUNCH_PATH];      /* <install>/save */
    char save[MMO_LAUNCH_PATH];     /* PC_SAVE */
    char record[MMO_LAUNCH_PATH];   /* PC_RECORD_INPUT */
    char link[MMO_LAUNCH_PATH];     /* the record of this session */
    char rtc[MMO_LAUNCH_TEXT];      /* PC_RTC, as mmo_plat_stamp writes it */
    /* Held for as long as this session owns the save file, and dropped by
     * _end or _close. Never copied: a struct with this in it names one lock,
     * and the copy would drop a lock it does not own. */
    struct mmo_plat_lock *lock;
} mmo_launch_offline;

/*
 * The install root the save folder hangs off, given the game program's path: one above `bin/`
 * in a release and one above `fused/` in a built tree, and the folder holding the program
 * anywhere else.
 */
void mmo_launch_install_root(const char *port_exe, char *out, size_t cap);

/*
 * The folder packages live in: the settings' own row when it names a directory that exists,
 * and otherwise the `mods` beside the game program's `bin/`.
 */
void mmo_launch_mods_root(const mmo_launch_settings *s, const char *port_exe,
                          char *out, size_t cap);

/*
 * Where one slot's saved game lives, given the install root: `<root>/save` for slot 1 and
 * `<root>/save/slot<n>` for the rest.
 */
void mmo_launch_slot_dir(const char *root, int slot, char *out, size_t cap);

/* The same from the game's own path, which is what every caller has. */
void mmo_launch_save_dir(const char *port_exe, int slot, char *out,
                         size_t cap);

/*
 * Name a session and make its folders, and take the save file for it. `wall` is the local
 * clock as mmo_plat_stamp writes it (`YYYY-MM-DD HH:MM:SS`); NULL asks the host.
 */
int mmo_launch_offline_open(const char *port_exe, int slot, const char *wall,
                            mmo_launch_offline *o, char *err, size_t errcap);

/*
 * Write the opening half of the session's record: `version` first, then the build, the clock
 * and the image this session started from. Written before the game starts, so a session that
 * crashes still leaves one.
 */
int mmo_launch_offline_begin(const mmo_launch_offline *o, int revision,
                             char *err, size_t errcap);

/*
 * Close it: hash the recording and the image the session left, keep a stamped copy of that
 * image beside the save, and drop the oldest past MMO_LAUNCH_SAVE_KEEP.
 */
int mmo_launch_offline_end(mmo_launch_offline *o, char *err, size_t errcap);

/* Let the save file go without writing anything down. */
void mmo_launch_offline_close(mmo_launch_offline *o);

/*
 * The stamped images in `dir`, newest first, as the "restore an earlier save"
 * row reads them. Returns how many were written.
 */
int mmo_launch_offline_list(const char *dir,
                            char out[][MMO_LAUNCH_STAMP], int max);

/*
 * Put `stamp` back as the save file. The image being replaced is kept first, under a stamp of
 * its own, so restoring the wrong one is itself one restore to undo.
 */
int mmo_launch_offline_restore(const char *dir, const char *stamp,
                               const char *wall, char *err, size_t errcap);

/* ------------------------------------------------------------------ */
/* Carrying a session out to the offline row                           */
/* ------------------------------------------------------------------ */

/*
 * A session's party, bag, money, flags and tile are the server's, seated into the engine's RAM
 * save and never written anywhere.
 */
typedef struct {
    char save[MMO_LAUNCH_PATH];   /* the handoff, and the session's PC_SAVE */
    char mark[MMO_LAUNCH_PATH];   /* the marker beside it */
    char dir[MMO_LAUNCH_PATH];    /* <install>/save */
    /* The save report this session was told to offer, or empty for none. The
     * other direction of the same handoff: one file the front door names for
     * the session to write, one it names for the session to read. Set by the
     * caller after the player has answered the offer, never by _open. */
    char import[MMO_LAUNCH_PATH];
    /* The session records behind that report, gathered into one file, or empty
     * when there are none to gather. Evidence for the import and nothing else:
     * a session told to offer a save with no chain beside it offers the save
     * all the same, and the import simply stays marked. */
    char chain[MMO_LAUNCH_PATH];
} mmo_launch_export;

/*
 * Name this session's handoff and clear anything left at those names, so the game cannot load
 * one as a player save and cannot adopt a stale one.
 */
int mmo_launch_export_open(const char *port_exe, int slot, long pid,
                           mmo_launch_export *x, char *err, size_t errcap);

/*
 * After the session: 1 when an export was adopted, 0 when none was asked for, -1 with a
 * reason.
 */
int mmo_launch_export_adopt(const mmo_launch_export *x, const char *wall,
                            int revision, char *stamp, size_t stampcap,
                            char *err, size_t errcap);

/* Drop the handoff and its marker, whatever the session did with them. */
void mmo_launch_export_close(const mmo_launch_export *x);

/*
 * The same, once the adopt above has had its answer: the pair goes, unless the adopt refused
 * and the image is still where the game left it. 1 when the image was kept and the caller
 * should say where it is, 0 when it was dropped.
 */
int mmo_launch_export_settle(const mmo_launch_export *x, int adopted);

/*
 * Every handoff an earlier launch kept, adopted now. A refused adopt leaves the image and its
 * marker where the game wrote them (mmo_launch_export_settle above), which is only worth
 * anything if somebody comes back for them: this is who comes back.
 */
int mmo_launch_export_sweep(const char *port_exe, int revision,
                            void (*note)(void *ud, const char *line), void *ud);

/* ------------------------------------------------------------------ */
/* Taking an offline save back online                                  */
/* ------------------------------------------------------------------ */

/* Which side is newer, and what it would cost to say so. */
typedef struct {
    char report[MMO_LAUNCH_PATH];   /* the report to offer, when there is one */
    long save_seconds;              /* the offline save's own play time */
    long server_seconds;            /* the last export's, or -1 for none */
    /*
     * Why the two sides could not be compared, or empty. An export marker that will not open,
     * or one with no play time in it, is not the same thing as no export at all: it is a
     * character the server does have, whose clock this front door cannot read.
     */
    char unsure[MMO_LAUNCH_TEXT];
    /*
     * 1 when this saved game was carried in from a file rather than played here
     * (mmo_launch_bundle_read below wrote the marker that says so), and the name it was
     * carried out under.
     */
    int  from_elsewhere;
    char from[MMO_LAUNCH_TEXT];
} mmo_launch_import;

/*
 * 1 when there is a save worth offering and `out` is filled in, 0 when there is not. Never an
 * error: a front door that cannot read a report simply does not offer one, and the player
 * plays.
 */
int mmo_launch_import_offer(const char *port_exe, int slot,
                            mmo_launch_import *out);

/* 1 when the report at `report` was taken by the server during the session that offered it. */
int mmo_launch_import_landed(const char *report);

/* Gather the session records behind the offline save into one file, and name it in `out`. */
int mmo_launch_chain_collect(const char *port_exe, int slot, char *out,
                             size_t outcap, char *err, size_t errcap);

/* ------------------------------------------------------------------ */
/* Carrying a saved game to another machine                            */
/* ------------------------------------------------------------------ */

/* A slot's whole save folder, out to one file and back. */

/*
 * Write slot `slot` of this install to `path`. The live save is claimed for the read the way
 * every other toucher of it claims it, so a bundle can never be half of a game a session is in
 * the middle of writing.
 */
int mmo_launch_bundle_write(const char *port_exe, int slot, const char *path,
                            char *err, size_t errcap);

/* Read `path` into slot `slot`, replacing what is there. */
int mmo_launch_bundle_read(const char *port_exe, int slot, const char *path,
                           char *landed, size_t landedcap,
                           char *err, size_t errcap);

/*
 * What a bundle says about itself, without unpacking it: the character it was carried out as,
 * its play time, and the slot's name for it. For the row that asks "replace this saved game
 * with that one?" before it does.
 */
typedef struct {
    char from[MMO_LAUNCH_TEXT];   /* the character it was carried out as */
    long play_seconds;            /* its play time, or -1 */
    long written;                 /* the unix time it was written, or -1 */
    int  sessions;                /* how many offline sessions travel with it */
    int  has_report;              /* whether it can be offered without a play */
    int  has_anchor;              /* whether its play can still be checked */
} mmo_launch_bundle_info;

int mmo_launch_bundle_look(const char *path, mmo_launch_bundle_info *out,
                           char *err, size_t errcap);

/*
 * The two command lines and the environment between them. Every pointer in the argv and env
 * arrays points into `arena`, so a plan is one self-contained object a caller may build,
 * print, and hand to fork/exec without owning any other allocation.
 */
typedef struct {
    char   chan[MMO_LAUNCH_NAME];
    char  *port_argv[MMO_LAUNCH_MAX_ARGV];
    char  *view_argv[MMO_LAUNCH_MAX_ARGV];
    char  *env[MMO_LAUNCH_MAX_ENV];     /* NAME=VALUE for the port */
    int    port_argc;
    int    view_argc;
    int    envc;
    char   arena[MMO_LAUNCH_ARENA];
    size_t used;
} mmo_launch_plan;

/*
 * Compose both command lines. `chan` may be NULL, in which case the plan names one from `pid`.
 */
/* Which packages this session loads before the launcher adds its own: the
 * `mods` row, or loadorder.txt in the mods folder when that row is empty,
 * the same fallback the engine makes when PC_MODS is unset. */
void mmo_launch_mods_list(const mmo_launch_settings *s, const char *modroot,
                          char *out, size_t cap);

int mmo_launch_plan_build(const mmo_launch_settings *s, const char *port_exe,
                          const char *view_exe, const char *chan, long pid,
                          mmo_launch_plan *p, char *err, size_t errcap);

/*
 * The same plan for the offline row: no session, the save file `o` names, the host's clock,
 * and the pad recorded into `o`. The window is told `--offline` so it draws no screen that
 * would need a server.
 */
int mmo_launch_plan_build_offline(const mmo_launch_settings *s,
                                  const char *port_exe, const char *view_exe,
                                  const char *chan, long pid,
                                  const mmo_launch_offline *o,
                                  mmo_launch_plan *p, char *err, size_t errcap);

/* The session plan again, with somewhere for "Continue Offline" to write. */
int mmo_launch_plan_build_export(const mmo_launch_settings *s,
                                 const char *port_exe, const char *view_exe,
                                 const char *chan, long pid,
                                 const mmo_launch_export *x,
                                 mmo_launch_plan *p, char *err, size_t errcap);

/*
 * The whole of a session launch in one call: name the handoff this session may write itself
 * out to, offer the offline save when `take_save_online` says the player asked for it, and
 * build the plan around whichever of the two exist.
 */
int mmo_launch_plan_build_session(const mmo_launch_settings *s,
                                  const char *port_exe, const char *view_exe,
                                  const char *chan, long pid,
                                  int take_save_online,
                                  void (*note)(void *ud, const char *line),
                                  void (*warn)(void *ud, const char *line),
                                  void *ud, mmo_launch_export *x,
                                  mmo_launch_plan *p, char *err, size_t errcap);

/* The plan as text, one `chan` / `env` / `port` / `view` line each. */
void mmo_launch_plan_print(const mmo_launch_plan *p, FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_LAUNCH_PLAN_H */
