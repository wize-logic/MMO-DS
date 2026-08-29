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
 * The internal 3D resolution, sold as the two steps a player recognises: Sd is the engine
 * drawing at 2x, HD at 3x.
 */
#define MMO_LAUNCH_HD3D_MIN   2
#define MMO_LAUNCH_HD3D_MAX   3
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
    char feed_url[MMO_LAUNCH_PATH];     /* http:// base the update is fetched from;
                                         * empty = the install only checks, never
                                         * fetches. Plain http on purpose: the
                                         * signature is the trust (update.h). */
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
/* Eighteen names on the fullest plan, now that every panel setting is stated
 * rather than left to whatever the environment already said. Room for a few
 * more, because a plan that runs out refuses to start the game at all. */
#define MMO_LAUNCH_MAX_ENV  24
#define MMO_LAUNCH_ARENA    4096

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
int mmo_launch_plan_build(const mmo_launch_settings *s, const char *port_exe,
                          const char *view_exe, const char *chan, long pid,
                          mmo_launch_plan *p, char *err, size_t errcap);

/* The plan as text, one `chan` / `env` / `port` / `view` line each. */
void mmo_launch_plan_print(const mmo_launch_plan *p, FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_LAUNCH_PLAN_H */
