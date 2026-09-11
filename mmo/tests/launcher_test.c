/* The front door's arithmetic, without a front door. */

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include "launch_menu.h"
#include "launch_plan.h"
#include "../include/offline_chain.h"
#include "../include/offline_import.h"
#include "../include/save_bundle.h"

static int failures;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) {                                                             \
            printf("  ok   %s\n", msg);                                         \
        } else {                                                                \
            printf("  FAIL %s\n", msg);                                         \
            failures++;                                                         \
        }                                                                       \
    } while (0)

/* Whether `argv` holds `flag` followed by `value` (or just `flag`, if value is
 * NULL), the question every check below is really asking. */
static int has_flag(char *const *argv, int argc, const char *flag,
                    const char *value)
{
    int i;

    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], flag) != 0)
            continue;
        if (value == NULL)
            return 1;
        return i + 1 < argc && strcmp(argv[i + 1], value) == 0;
    }
    return 0;
}

static const char *env_of(const mmo_launch_plan *p, const char *name)
{
    size_t n = strlen(name);
    int i;

    for (i = 0; i < p->envc; i++) {
        if (strncmp(p->env[i], name, n) == 0 && p->env[i][n] == '=')
            return p->env[i] + n + 1;
    }
    return NULL;
}

/* A cartridge as the front door reads it: sixteen bytes with the game code at
 * 0x0C. Platinum goes under the game's own name, the other two under any. */
static int write_rom(const char *dir, const char *name, const char *code)
{
    unsigned char hdr[16] = { 0 };
    char path[512];
    int fd;

    memcpy(hdr + 0x0C, code, 4);
    snprintf(path, sizeof path, "%s/%s", dir, name);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -1;
    if (write(fd, hdr, sizeof hdr) != (ssize_t)sizeof hdr) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

#define ROM_HG_NAME "heartgold-backup.nds"
#define ROM_BW_NAME "black-backup.nds"

/* A folder holding the three cartridges a session needs, which is what
 * mmo_launch_check asks for now. `which` is a bit set: 1 Platinum, 2 Heart
 * Gold, 4 Black. Returns the folder, or NULL. */
static const char *fake_roms(int which)
{
    static char dirs[8][64];
    static int used;
    char tmp[] = "/tmp/openmmo-romdir-XXXXXX";
    char *dir;

    if (used >= 8 || mkdtemp(tmp) == NULL)
        return NULL;
    dir = dirs[used++];
    snprintf(dir, sizeof dirs[0], "%s", tmp);
    if ((which & 1) && write_rom(dir, MMO_LAUNCH_ROM_NAME, "CPUE") != 0)
        return NULL;
    if ((which & 2) && write_rom(dir, ROM_HG_NAME, "IPKE") != 0)
        return NULL;
    if ((which & 4) && write_rom(dir, ROM_BW_NAME, "IRBO") != 0)
        return NULL;
    return dir;
}

static void drop_roms(const char *dir)
{
    static const char *const names[] = {
        MMO_LAUNCH_ROM_NAME, ROM_HG_NAME, ROM_BW_NAME, "platinum-backup.nds"
    };
    char path[512];
    size_t i;

    if (dir == NULL)
        return;
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        remove(path);
    }
    rmdir(dir);
}

/* The folder every good_settings() names, made once. */
static const char *s_roms;

static void good_settings(mmo_launch_settings *s)
{
    mmo_launch_defaults(s);
    if (s_roms == NULL)
        s_roms = fake_roms(7);
    snprintf(s->rom, sizeof s->rom, "%s", s_roms != NULL ? s_roms : "/nonexistent");
    snprintf(s->user, sizeof s->user, "test");
    snprintf(s->pass, sizeof s->pass, "test");
}

/* ------------------------------------------------------------------ */

static void check_defaults(void)
{
    mmo_launch_settings s;

    printf("the defaults are the window's own, so no config file and an empty one agree:\n");
    mmo_launch_defaults(&s);
    CHECK(s.scale == MMO_LAUNCH_SCALE_AUTO && s.render_scale == 2,
          "scale auto and render scale 2, as viewer.c starts");
    CHECK(s.layout == MMO_LAYOUT_FILL, "the fill layout");
    CHECK(s.filter == MMO_FILTER_LINEAR, "linear filtering (sharp-bilinear)");
    CHECK(s.hd3d == MMO_LAUNCH_HD3D_MIN &&
          strcmp(mmo_launch_hd3d_name(s.hd3d), "sd") == 0 &&
          strcmp(s.viewport, "auto") == 0,
          "3D detail at SD and viewport auto");
    CHECK(s.camera == MMO_LAUNCH_CAMERA,
          "the camera stands where it is fixed, not where a player put it");
    CHECK(s.pace == MMO_PACE_CONSOLE &&
          strcmp(mmo_launch_pace_name(s.pace), "console") == 0 &&
          mmo_launch_pace_name(MMO_PACE_N) == NULL,
          "frame pacing at the console's rate, as the port starts");
    CHECK(s.audio == 1, "sound on");
    CHECK(s.music == 100 && s.sfx == 100, "music and sfx at full scale");
    CHECK(s.rom[0] == '\0', "nothing is chosen for the player: no ROM");
    CHECK(s.mods[0] == '\0' && s.mods_dir[0] == '\0',
          "and no content packages until they name some");
    CHECK(s.discord == 1,
          "presence is on out of the box, and one row turns it off");
    CHECK(strcmp(mmo_launch_layout_name(MMO_LAYOUT_WIDE), "wide") == 0 &&
          strcmp(mmo_launch_layout_name(MMO_LAYOUT_FILL), "fill") == 0 &&
          strcmp(mmo_launch_filter_name(MMO_FILTER_SCALE2X), "scale2x") == 0 &&
          strcmp(mmo_launch_fit_name(MMO_FIT_STRETCH), "stretch") == 0,
          "the choice names are the window's own spelling");
    CHECK(mmo_launch_layout_name(MMO_LAYOUT_N) == NULL &&
          mmo_launch_filter_name(-1) == NULL &&
          mmo_launch_button_mode_name(MMO_BUTTON_N) == NULL,
          "an out-of-range choice has no name rather than a wrong one");
    /* The game's own default, and the one the save used to hold. Every other
     * button mode is something a player asked for. */
    CHECK(s.button_mode == MMO_BUTTON_NORMAL &&
          strcmp(mmo_launch_button_mode_name(s.button_mode), "normal") == 0,
          "buttons mean what they say");
}

static void check_config(void)
{
    mmo_launch_settings a, b;
    char text[4096];
    char err[192];
    int n;

    printf("settings survive the file they are kept in:\n");
    good_settings(&a);
    a.scale = 4; a.render_scale = 3; a.layout = MMO_LAYOUT_WIDE;
    a.filter = MMO_FILTER_SCALE2X; a.fit = MMO_FIT_INTEGER;
    a.hd3d = 3;
    a.pace = MMO_PACE_UNLIMITED;
    snprintf(a.viewport, sizeof a.viewport, "16:9");
    a.fullscreen = 1; a.audio = 0;
    a.discord = 0;
    a.music = 40; a.sfx = 80;
    a.button_mode = MMO_BUTTON_L_IS_A;
    snprintf(a.audio_device, sizeof a.audio_device, "Speakers");
    snprintf(a.mods, sizeof a.mods, "bodies,hub");
    snprintf(a.mods_dir, sizeof a.mods_dir, "/tmp/a mods dir");

    n = mmo_launch_format(&a, text, sizeof text);
    CHECK(n > 0, "the settings format to something");
    /*
     * The one field that must not survive. It used to, as plain text, in a file that goes into
     * every backup of the profile and gets pasted into bug reports.
     */
    CHECK(strstr(text, "\npass ") == NULL && strncmp(text, "pass ", 5) != 0,
          "the password is not written to the file");
    /* And the one that stopped being a field: the next save drops the row, so
     * a file that carried a choice stops carrying it. */
    CHECK(strstr(text, "\nsound ") == NULL && strncmp(text, "sound ", 6) != 0,
          "the retired mixer row is not written either");
    mmo_launch_defaults(&b);
    CHECK(mmo_launch_parse(text, &b, err, sizeof err) == 0, "and parse back");
    CHECK(b.pass[0] == '\0', "and does not come back out of one");
    memset(a.pass, 0, sizeof a.pass);   /* all of it: b's came from a memset */
    CHECK(memcmp(&a, &b, sizeof a) == 0,
          "byte for byte, every field that is kept round-trips");

    mmo_launch_defaults(&b);
    CHECK(mmo_launch_parse("# nothing but a comment\n\n   \n", &b, err, sizeof err) == 0,
          "comments and blank lines are skipped");
    CHECK(b.scale == MMO_LAUNCH_SCALE_AUTO, "and leave the defaults alone");

    /* The whole reason the format refuses unknown keys: a typo that reads as a
     * default is a setting the player believes they changed. */
    CHECK(mmo_launch_parse("scal 3\n", &b, err, sizeof err) == -1,
          "a misspelt key is refused rather than ignored");
    CHECK(strstr(err, "scal") != NULL, "and the message names it");
    CHECK(mmo_launch_parse("scale 99\n", &b, err, sizeof err) == 0 &&
          b.scale == MMO_LAUNCH_SCALE_MAX,
          "a scale past the top lands on the top");
    CHECK(mmo_launch_parse("scale two\n", &b, err, sizeof err) == -1,
          "but one that is not a number is still refused");
    CHECK(mmo_launch_parse("scale auto\n", &b, err, sizeof err) == 0 &&
          b.scale == MMO_LAUNCH_SCALE_AUTO,
          "scale auto is the sentinel, not a number");
    CHECK(mmo_launch_parse("viewport 21:9\n", &b, err, sizeof err) == -1,
          "a viewport the window does not have is refused");
    CHECK(mmo_launch_parse("viewport 16:10\n", &b, err, sizeof err) == 0 &&
          strcmp(b.viewport, "16:10") == 0,
          "viewport 16:10 is one of the named ratios");
    /*
     * The distance is fixed now, so every one of these is the same check: the row is still
     * accepted, because a config written while it was a setting must not stop the front door,
     * and the value in it goes nowhere.
     */
    CHECK(mmo_launch_parse("camera-distance 130\n", &b, err, sizeof err) == 0 &&
          b.camera == MMO_LAUNCH_CAMERA,
          "a camera row is read and the distance stays where it is fixed");
    CHECK(mmo_launch_parse("camera-distance 90\n", &b, err, sizeof err) == 0 &&
          b.camera == MMO_LAUNCH_CAMERA,
          "and so is one that asks to be nearer");
    CHECK(mmo_launch_parse("camera-distance 200\n", &b, err, sizeof err) == 0 &&
          b.camera == MMO_LAUNCH_CAMERA,
          "and so is the 200 that once stopped the front door starting");
    CHECK(mmo_launch_parse("hd3d ultra\n", &b, err, sizeof err) == 0 &&
          b.hd3d == MMO_LAUNCH_HD3D_MAX,
          "ultra is the front door's top step, the port's own 4x");
    CHECK(mmo_launch_parse("hd3d hd\n", &b, err, sizeof err) == 0 &&
          b.hd3d == MMO_LAUNCH_HD3D_MIN + 1,
          "hd is the step below it");
    CHECK(mmo_launch_parse("hd3d sd\n", &b, err, sizeof err) == 0 &&
          b.hd3d == MMO_LAUNCH_HD3D_MIN,
          "and sd is the one below that");
    CHECK(mmo_launch_parse("hd3d fine\n", &b, err, sizeof err) == -1,
          "a resolution the front door does not sell is refused");
    /*
     * The migration, which is the whole reason a number is still read: every config written
     * before this row had a name says 1, 2 or 3, and each of them moves up a step rather than
     * being refused or left where it was.
     */
    CHECK(mmo_launch_parse("hd3d 1\n", &b, err, sizeof err) == 0 &&
          b.hd3d == MMO_LAUNCH_HD3D_MIN,
          "an old config at the console's own 1x comes up to SD");
    CHECK(mmo_launch_parse("hd3d 2\n", &b, err, sizeof err) == 0 &&
          b.hd3d == MMO_LAUNCH_HD3D_MIN + 1,
          "and one at 2x comes up to HD");
    CHECK(mmo_launch_parse("hd3d 3\n", &b, err, sizeof err) == 0 &&
          b.hd3d == MMO_LAUNCH_HD3D_MIN + 1,
          "and one at 3x is HD too");
    CHECK(mmo_launch_parse("hd3d 4\n", &b, err, sizeof err) == 0 &&
          b.hd3d == MMO_LAUNCH_HD3D_MAX,
          "the port's 4x lands on the front door's top step, not a refusal");
    CHECK(mmo_launch_parse("filter bilinear\n", &b, err, sizeof err) == -1,
          "so is a filter the window does not have");
    CHECK(mmo_launch_parse("pace unlimited\n", &b, err, sizeof err) == 0 &&
          b.pace == MMO_PACE_UNLIMITED,
          "pace unlimited turns the pacer off");
    CHECK(mmo_launch_parse("pace fast\n", &b, err, sizeof err) == -1,
          "a pace the port does not have is refused");
    /* The mixer is not a setting any more. Every value the row ever held
     * still parses, and none of them do anything: a file written while the
     * choice existed has to start the front door. */
    CHECK(mmo_launch_parse("sound ds\n", &b, err, sizeof err) == 0 &&
          mmo_launch_parse("sound hd\n", &b, err, sizeof err) == 0 &&
          mmo_launch_parse("sound loud\n", &b, err, sizeof err) == 0,
          "the retired sound row is read and ignored, whatever it says");
    CHECK(mmo_launch_parse("button-mode swap-xy\n", &b, err, sizeof err) == -1,
          "so is a button mode the game does not have");
    CHECK(mmo_launch_parse("layout wide", &b, err, sizeof err) == 0 &&
          b.layout == MMO_LAYOUT_WIDE,
          "a last line with no newline is still a setting");

    mmo_launch_defaults(&b);
    CHECK(mmo_launch_parse("mods-dir /a/b c/d\n", &b, err, sizeof err) == 0 &&
          strcmp(b.mods_dir, "/a/b c/d") == 0,
          "a value keeps its spaces: paths have them");
    CHECK(mmo_launch_parse("mods-dir\n", &b, err, sizeof err) == 0 &&
          b.mods_dir[0] == '\0',
          "a key with no value clears it back to the engine's own default");
    /* The two the single-player boot took with it. A launcher.cfg written
     * before it went carries both, and neither may stop the front door. */
    CHECK(mmo_launch_parse("session 0\nsave /a/b.sav\n", &b, err, sizeof err) == 0,
          "session and save are retired keys, read as nothing rather than refused");
    /* The two that went with the character list. A launcher.cfg written while
     * the front door still named a row carries both, and neither may stop it. */
    CHECK(mmo_launch_parse("character Dawn\nworld 1\n", &b, err, sizeof err) == 0,
          "character and world are retired too, read as nothing rather than refused");
    /* The bind row came back live (2026-08-24): the launcher's Controls
     * panel writes it, in the window's own --bind syntax, holding only what
     * differs from the window's defaults. An old cfg's line still loads. */
    CHECK(mmo_launch_parse("bind a=Return,b=Left Shift\n", &b, err, sizeof err) == 0 &&
          strcmp(b.bind, "a=Return,b=Left Shift") == 0,
          "bind is live again: the Controls panel's row, in --bind's syntax");
    CHECK(mmo_launch_parse("bind q=Z\n", &b, err, sizeof err) != 0,
          "a bind naming a button the pad does not have is refused");
    CHECK(mmo_launch_parse("bind a\n", &b, err, sizeof err) != 0,
          "a bind with no key is refused");
    {
        char spec[MMO_LAUNCH_BIND] = "";
        char key[MMO_LAUNCH_TEXT];
        int pad_a = 4;          /* the DS order: up down left right a b ... */

        CHECK(strcmp(mmo_launch_pad_name(pad_a), "a") == 0 &&
              mmo_launch_pad_name(MMO_LAUNCH_PADS) == NULL,
              "the pad table is in the DS's order and NULL past its end");
        CHECK(mmo_launch_bind_get("", pad_a, key, sizeof key) == 0 &&
              strcmp(key, "Z") == 0,
              "an empty spec answers the window's default");
        CHECK(mmo_launch_bind_set(spec, sizeof spec, pad_a, "Return") == 0 &&
              strcmp(spec, "a=Return") == 0,
              "a rebind writes only the difference");
        CHECK(mmo_launch_bind_get(spec, pad_a, key, sizeof key) == 0 &&
              strcmp(key, "Return") == 0,
              "and reads back as the current key");
        CHECK(mmo_launch_bind_set(spec, sizeof spec, pad_a, "Z") == 0 &&
              spec[0] == '\0',
              "rebinding back to the default empties the row");
        CHECK(mmo_launch_bind_set(spec, sizeof spec, pad_a, "Left Shift") == 0 &&
              mmo_launch_bind_set(spec, sizeof spec, 10, "Space") == 0 &&
              strcmp(spec, "a=Left Shift,start=Space") == 0,
              "two rebinds hold two items, spaces in a key name and all");
        CHECK(mmo_launch_bind_set(spec, sizeof spec, pad_a, "a,b") != 0 &&
              mmo_launch_bind_set(spec, sizeof spec, pad_a, "a=b") != 0,
              "a key holding the syntax's own separators is refused");
        CHECK(mmo_launch_bind_get("a=Q,a=P", pad_a, key, sizeof key) == 0 &&
              strcmp(key, "P") == 0,
              "the last item to name a pad wins, the way the window reads it");
    }
    CHECK(mmo_launch_parse("mods bodies,hub\nmods-dir /tmp/mods\n", &b, err,
                           sizeof err) == 0
          && strcmp(b.mods, "bodies,hub") == 0
          && strcmp(b.mods_dir, "/tmp/mods") == 0,
          "mods and mods-dir are the runtime package list and its folder");
    CHECK(mmo_launch_parse("mods\nmods-dir\n", &b, err, sizeof err) == 0
          && b.mods[0] == '\0' && b.mods_dir[0] == '\0',
          "and an empty value omits them again");
}

static void check_config_file(void)
{
    mmo_launch_settings a, b;
    char path[512];
    char err[192];

    printf("the settings file is written whole or not at all:\n");
    snprintf(path, sizeof path, "/tmp/openmmo-launcher-test-%ld.cfg", (long)getpid());
    remove(path);

    mmo_launch_defaults(&b);
    CHECK(mmo_launch_load(path, &b, err, sizeof err) == 1,
          "a first run reports there is no file yet rather than failing");

    good_settings(&a);
    a.scale = 5;
    a.fullscreen = 1;
    CHECK(mmo_launch_save(path, &a) == 0, "the settings are written");
    mmo_launch_defaults(&b);
    CHECK(mmo_launch_load(path, &b, err, sizeof err) == 0 && b.scale == 5 &&
          b.fullscreen == a.fullscreen,
          "and read back from the file");
    /*
     * The second save, over a file that is now there. Every save after a fresh install is this
     * one, and it went unwritten on Windows for as long as the launcher has existed, because
     * rename() will not replace an existing file there.
     */
    a.scale = 3;
    a.hd3d = MMO_LAUNCH_HD3D_MIN;
    CHECK(mmo_launch_save(path, &a) == 0, "a second save replaces the first");
    mmo_launch_defaults(&b);
    CHECK(mmo_launch_load(path, &b, err, sizeof err) == 0 && b.scale == 3 &&
          b.hd3d == MMO_LAUNCH_HD3D_MIN,
          "and it is the second one that is read back, not the first");
    /* The saved name reads back as itself: a written SD must not migrate to
     * HD the next time the file is opened, which is what writing the old
     * number would have done. */
    CHECK(mmo_launch_save(path, &b) == 0 &&
          mmo_launch_load(path, &b, err, sizeof err) == 0 &&
          b.hd3d == MMO_LAUNCH_HD3D_MIN,
          "and saving it again leaves it at SD rather than creeping up");
    {
        char nest[] = "/tmp/openmmo-cfg-XXXXXX";
        char nested[512];
        char *root = mkdtemp(nest);

        if (root == NULL) {
            printf("  FAIL no temp directory for a first-save parent\n");
            failures++;
        } else {
            snprintf(nested, sizeof nested, "%s/openmmo/launcher.cfg", root);
            CHECK(mmo_launch_save(nested, &a) == 0,
                  "the first save creates the missing config directory");
            CHECK(access(nested, R_OK) == 0, "and the file is there");
            remove(nested);
            snprintf(nested, sizeof nested, "%s/openmmo", root);
            rmdir(nested);
            rmdir(root);
        }
    }
    {
        char tmp[600];
        FILE *f;

        snprintf(tmp, sizeof tmp, "%s.new", path);
        f = fopen(tmp, "r");
        CHECK(f == NULL, "no half-written temp file is left behind");
        if (f != NULL) fclose(f);
    }

    {
        FILE *f = fopen(path, "w");

        if (f != NULL) {
            fputs("colour purple\n", f);
            fclose(f);
        }
        mmo_launch_defaults(&b);
        CHECK(mmo_launch_load(path, &b, err, sizeof err) == -1,
              "a corrupt file is refused, not half applied");
    }
    remove(path);
}

static void check_refusals(void)
{
    mmo_launch_settings s;
    char err[192];

    printf("what the front door refuses to launch, and says why:\n");
    good_settings(&s);
    CHECK(mmo_launch_check(&s, err, sizeof err) == 0, "a complete set of settings is fine");

    good_settings(&s);
    s.rom[0] = '\0';
    CHECK(mmo_launch_check(&s, err, sizeof err) == -1 && strstr(err, "ROM") != NULL,
          "no ROM: this build supplies none, and says so");

    good_settings(&s);
    snprintf(s.rom, sizeof s.rom, "/no/such/rom-%ld.nds", (long)getpid());
    CHECK(mmo_launch_check(&s, err, sizeof err) == -1 && strstr(err, "cannot be read") != NULL,
          "a ROM that is not there is caught before the fork, not after it");

    {
        const char *dir = fake_roms(7);
        char nds[512], file[MMO_LAUNCH_PATH];
        mmo_launch_plan plan;

        if (dir == NULL) {
            printf("  FAIL no temp directory for a ROM folder\n");
            failures++;
        } else {
            snprintf(nds, sizeof nds, "%s/%s", dir, MMO_LAUNCH_ROM_NAME);
            good_settings(&s);
            snprintf(s.rom, sizeof s.rom, "%s", dir);
            CHECK(mmo_launch_rom_file(s.rom, file, sizeof file) == 0 &&
                  strcmp(file, nds) == 0,
                  "a folder resolves to pokeplatinum.us.nds inside it");
            CHECK(mmo_launch_check(&s, err, sizeof err) == 0,
                  "and a folder with all three cartridges is enough to launch");
            CHECK(mmo_launch_plan_build(&s, "/b/port", "/b/view", "c", 1,
                                        &plan, err, sizeof err) == 0,
                  "the plan builds from a folder");
            {
                int i, found = 0;

                for (i = 0; i < plan.envc; i++) {
                    if (strncmp(plan.env[i], "PC_ROM=", 7) == 0 &&
                        strcmp(plan.env[i] + 7, nds) == 0)
                        found = 1;
                }
                CHECK(found, "PC_ROM is the file inside the folder, not the folder");
            }
            /* The same image under the name a dumper gave it. */
            {
                char other[512];

                snprintf(other, sizeof other, "%s/platinum-backup.nds", dir);
                rename(nds, other);
                CHECK(mmo_launch_rom_file(s.rom, file, sizeof file) == 0 &&
                      strcmp(file, other) == 0,
                      "a Platinum backup under any name is found by its header");
                rename(other, nds);
            }
            /* The other two: missing, of the wrong game, or the twin. */
            {
                char black[512];

                snprintf(black, sizeof black, "%s/%s", dir, ROM_BW_NAME);
                remove(black);
                CHECK(mmo_launch_check(&s, err, sizeof err) == -1 &&
                      strstr(err, "Black") != NULL,
                      "without a Black cartridge the front door refuses, naming it");
                snprintf(s.rom_bw, sizeof s.rom_bw, "%s", nds);
                CHECK(mmo_launch_check(&s, err, sizeof err) == -1 &&
                      strstr(err, "not Black") != NULL,
                      "a named file of another game is refused by what it is");
                s.rom_bw[0] = '\0';
                write_rom(dir, ROM_BW_NAME, "IRAO");
                CHECK(mmo_launch_check(&s, err, sizeof err) == 0,
                      "White stands in for Black");
                write_rom(dir, ROM_HG_NAME, "IPGE");
                CHECK(mmo_launch_check(&s, err, sizeof err) == 0,
                      "and SoulSilver for Heart Gold");
                write_rom(dir, ROM_HG_NAME, "ADAE");
                CHECK(mmo_launch_check(&s, err, sizeof err) == -1 &&
                      strstr(err, "Heart Gold") != NULL,
                      "Diamond does not: the Heart Gold slot is named as missing");
                write_rom(dir, ROM_HG_NAME, "IPKE");
                write_rom(dir, ROM_BW_NAME, "IRBO");
            }
            /*
             * The accepted codes are the registry's rows, not a second list kept here: the
             * first three characters are the game and the fourth is the language, so a build
             * of the same game the registry has never opened still counts as owning it, and a
             * code naming no game at all does not.
             */
            {
                CHECK(mmo_launch_cart_is(MMO_LAUNCH_CART_HEARTGOLD, "IPKJ")
                      && mmo_launch_cart_is(MMO_LAUNCH_CART_HEARTGOLD, "IPKD"),
                      "another language of Heart Gold is still Heart Gold");
                CHECK(!mmo_launch_cart_is(MMO_LAUNCH_CART_HEARTGOLD, "ZZZZ")
                      && !mmo_launch_cart_is(MMO_LAUNCH_CART_BLACK, "IPKE"),
                      "a code naming no game, and the wrong game, are refused");
                CHECK(mmo_launch_cart_is(MMO_LAUNCH_CART_PLATINUM, "CPUE")
                      && !mmo_launch_cart_is(MMO_LAUNCH_CART_PLATINUM, "CPUJ"),
                      "the Platinum slot takes the one build the engine is, "
                      "and no other");
            }
            /* The Platinum slot: the wrong game, or nothing at all. */
            {
                char hg[512];

                snprintf(hg, sizeof hg, "%s/%s", dir, ROM_HG_NAME);
                snprintf(s.rom, sizeof s.rom, "%s", hg);
                CHECK(mmo_launch_check(&s, err, sizeof err) == -1 &&
                      strstr(err, "not Platinum") != NULL,
                      "a Heart Gold image named as the ROM is refused by what it is");
                snprintf(s.rom, sizeof s.rom, "%s", dir);
            }
            remove(nds);
            CHECK(mmo_launch_check(&s, err, sizeof err) == -1 &&
                  strstr(err, "Platinum") != NULL,
                  "a folder with no Platinum image names the missing cartridge");
            drop_roms(dir);
        }
    }

    good_settings(&s);
    s.user[0] = '\0';
    CHECK(mmo_launch_check(&s, err, sizeof err) == -1,
          "a password with no account name");

    /* viewer.c:1133 exits 2 on this pair. Catching it here is the difference
     * between a message in the launcher and a window that never appears. */
    good_settings(&s);
    s.filter = MMO_FILTER_SCALE2X;
    s.render_scale = 1;
    CHECK(mmo_launch_check(&s, err, sizeof err) == -1 && strstr(err, "scale2x") != NULL,
          "scale2x under render scale 2, which the window refuses on sight");
    s.render_scale = 2;
    CHECK(mmo_launch_check(&s, err, sizeof err) == 0, "and is fine at 2");
}

static void check_plan(void)
{
    mmo_launch_settings s;
    mmo_launch_plan p;
    char err[192];
    const char *v;

    printf("the launch plan is two command lines the two programs accept:\n");
    good_settings(&s);
    CHECK(mmo_launch_plan_build(&s, "/b/pokeplatinum", "/b/openmmo-view",
                                "chan-0", 0, &p, err, sizeof err) == 0,
          "a default plan is built");
    CHECK(has_flag(p.view_argv, p.view_argc, "--scale", "auto"),
          "the window is asked for --scale auto");
    CHECK(has_flag(p.view_argv, p.view_argc, "--layout", "fill"),
          "and --layout fill");
    CHECK(has_flag(p.view_argv, p.view_argc, "--render-scale", "1") &&
          has_flag(p.view_argv, p.view_argc, "--filter", "linear"),
          "sharp-bilinear over a frame the engine already drew at SD, so the"
          " window's own prescale stands down");
    v = env_of(&p, "PC_ASPECT");
    CHECK(v != NULL && strcmp(v, "auto") == 0,
          "PC_ASPECT=auto so the world follows the window");
    v = env_of(&p, "PC_HD3D");
    CHECK(v != NULL && strcmp(v, "2") == 0,
          "PC_HD3D=2 is STATED at the default, so a hand-set one cannot win");
    s.hd3d = MMO_LAUNCH_HD3D_MIN + 1;
    CHECK(mmo_launch_plan_build(&s, "/b/pokeplatinum", "/b/openmmo-view",
                                "chan-0", 0, &p, err, sizeof err) == 0,
          "a plan at HD is built");
    v = env_of(&p, "PC_HD3D");
    CHECK(v != NULL && strcmp(v, "3") == 0,
          "PC_HD3D=3 is passed when the panel asks for HD");
    s.hd3d = MMO_LAUNCH_HD3D_MAX;
    CHECK(mmo_launch_plan_build(&s, "/b/pokeplatinum", "/b/openmmo-view",
                                "chan-0", 0, &p, err, sizeof err) == 0,
          "a plan at ULTRA is built");
    v = env_of(&p, "PC_HD3D");
    CHECK(v != NULL && strcmp(v, "4") == 0,
          "PC_HD3D=4 is passed when the panel asks for ULTRA, the port's ceiling");
    s.hd3d = MMO_LAUNCH_HD3D_MIN + 1;
    CHECK(mmo_launch_plan_build(&s, "/b/pokeplatinum", "/b/openmmo-view",
                                "chan-0", 0, &p, err, sizeof err) == 0,
          "and back at HD");
    CHECK(has_flag(p.view_argv, p.view_argc, "--render-scale", "1"),
          "and the window's prescale stands down: the frame already"
          " carries the engine's sub-pixels");
    v = env_of(&p, "PC_PACE");
    CHECK(v != NULL && strcmp(v, "1") == 0, "PC_PACE=1 at the console rate");
    /* Stated rather than left out, and the same number in every plan: the
     * distance is fixed, so the game is told it outright instead of inferring
     * the official client's own from a variable that was not set. */
    v = env_of(&p, "OPENMMO_CAMERA_DISTANCE");
    CHECK(v != NULL && atoi(v) == MMO_LAUNCH_CAMERA,
          "the camera env states the one distance every session runs at");
    s.camera = 130;
    CHECK(mmo_launch_plan_build(&s, "/b/pokeplatinum", "/b/openmmo-view",
                                "chan-0", 0, &p, err, sizeof err) == 0,
          "a plan with the camera farther out is built");
    v = env_of(&p, "OPENMMO_CAMERA_DISTANCE");
    CHECK(v != NULL && strcmp(v, "130") == 0,
          "OPENMMO_CAMERA_DISTANCE carries the percent to the mod");
    s.camera = 100;
    s.pace = MMO_PACE_UNLIMITED;
    CHECK(mmo_launch_plan_build(&s, "/b/pokeplatinum", "/b/openmmo-view",
                                "chan-0", 0, &p, err, sizeof err) == 0,
          "a plan at unlimited pace is built");
    v = env_of(&p, "PC_PACE");
    CHECK(v != NULL && strcmp(v, "0") == 0,
          "PC_PACE=0 is passed when the pacer is turned off");
    v = env_of(&p, "PC_AUDIO_RATE");
    CHECK(v != NULL && strcmp(v, "48000") == 0,
          "PC_AUDIO_RATE=48000 in every plan: the mixer runs at the device's"
          " rate, STATED so a hand-set one cannot put the console's grain"
          " back");
    v = env_of(&p, "PC_AUDIO_INTERP");
    CHECK(v != NULL && strcmp(v, "cubic") == 0,
          "and PC_AUDIO_INTERP=cubic smooths between a channel's samples");

    good_settings(&s);
    s.scale = 3; s.render_scale = 2; s.layout = MMO_LAYOUT_WIDE;
    s.filter = MMO_FILTER_LINEAR; s.fit = MMO_FIT_INTEGER;
    s.fullscreen = 1;

    CHECK(mmo_launch_plan_build(&s, "/b/pokeplatinum", "/b/openmmo-view",
                                "chan-1", 0, &p, err, sizeof err) == 0,
          "a plan is built");
    CHECK(strcmp(p.chan, "chan-1") == 0, "the channel is the one asked for");
    CHECK(p.port_argc == 1 && strcmp(p.port_argv[0], "/b/pokeplatinum") == 0 &&
          p.port_argv[1] == NULL,
          "the game takes no arguments at all, it is all environment");

    v = env_of(&p, "PC_VIEW");
    CHECK(v != NULL && strcmp(v, "chan-1") == 0,
          "PC_VIEW names the page, so both halves meet on one channel");
    v = env_of(&p, "PC_ROM");
    {
        char romfile[MMO_LAUNCH_PATH];

        CHECK(v != NULL && mmo_launch_rom_file(s.rom, romfile, sizeof romfile) == 0 &&
                  strcmp(v, romfile) == 0,
              "PC_ROM is the Platinum image the chosen folder holds");
    }
    v = env_of(&p, "PC_SAVE");
    CHECK(v != NULL && strcmp(v, "none") == 0,
          "every plan forces PC_SAVE=none: the server's party is not a local file");
    v = env_of(&p, "OPENMMO_SESSION");
    CHECK(v != NULL && strcmp(v, "1") == 0,
          "OPENMMO_SESSION says a session is wanted, and says nothing else");
    CHECK(env_of(&p, "OPENMMO_SERVER") == NULL &&
          env_of(&p, "OPENMMO_GAMEPORT") == NULL,
          "and no address travels with it: the game dials the one it was built for");
    v = env_of(&p, "OPENMMO_USER");
    CHECK(v != NULL && strcmp(v, "test") == 0, "OPENMMO_USER is the account");
    v = env_of(&p, "OPENMMO_PASS");
    CHECK(v != NULL && strcmp(v, "test") == 0, "OPENMMO_PASS is its password");
    CHECK(env_of(&p, "OPENMMO_CHARACTER") == NULL,
          "no plan names a character: the engine's own list is where one is picked");
    CHECK(env_of(&p, "OPENMMO_BOOT_WORLD") == NULL,
          "and no plan skips the title into a local field, that is a seam, not a setting");
    /* The game reads its button mode from here and no longer from the save, so
     * this variable is the whole of that setting, and it is stated even at
     * normal, because an omitted name leaves whatever the environment said. */
    v = env_of(&p, "OPENMMO_BUTTON_MODE");
    CHECK(v != NULL && strcmp(v, "normal") == 0,
          "OPENMMO_BUTTON_MODE=normal is stated rather than left out");
    CHECK(env_of(&p, "OPENMMO_MUSIC") != NULL && env_of(&p, "OPENMMO_SFX") != NULL,
          "and the mixer scales are stated at 100 for the same reason");
    v = env_of(&p, "OPENMMO_DISCORD");
    CHECK(v != NULL && strcmp(v, "1") == 0,
          "OPENMMO_DISCORD=1 is what turns the presence line on");
    s.discord = 0;
    CHECK(mmo_launch_plan_build(&s, "/b/pokeplatinum", "/b/openmmo-view",
                                "chan-1", 0, &p, err, sizeof err) == 0,
          "a plan with presence turned off is built");
    v = env_of(&p, "OPENMMO_DISCORD");
    CHECK(v != NULL && strcmp(v, "0") == 0,
          "and off is stated, so a leftover 1 in the environment cannot win");
    s.discord = 1;
    s.button_mode = MMO_BUTTON_L_IS_A;
    CHECK(mmo_launch_plan_build(&s, "/b/pokeplatinum", "/b/openmmo-view",
                                "chan-1", 0, &p, err, sizeof err) == 0,
          "a plan with a button mode is built");
    v = env_of(&p, "OPENMMO_BUTTON_MODE");
    CHECK(v != NULL && strcmp(v, "l-is-a") == 0,
          "and one that is chosen travels to the game by name");
    s.button_mode = MMO_BUTTON_NORMAL;
    s.music = 0;
    s.sfx = 50;
    CHECK(mmo_launch_plan_build(&s, "/b/pokeplatinum", "/b/openmmo-view",
                                "chan-1", 0, &p, err, sizeof err) == 0,
          "a plan with mixer scales is built");
    v = env_of(&p, "OPENMMO_MUSIC");
    CHECK(v != NULL && strcmp(v, "0") == 0, "OPENMMO_MUSIC carries a lowered music scale");
    v = env_of(&p, "OPENMMO_SFX");
    CHECK(v != NULL && strcmp(v, "50") == 0, "OPENMMO_SFX carries a lowered sfx scale");
    s.music = 100;
    s.sfx = 100;
    CHECK(mmo_launch_plan_build(&s, "/b/pokeplatinum", "/b/openmmo-view",
                                "chan-1", 0, &p, err, sizeof err) == 0,
          "the plan is rebuilt for the checks below");
    CHECK(p.env[p.envc] == NULL, "the environment is NULL-terminated for exec");

    CHECK(strcmp(p.view_argv[0], "/b/openmmo-view") == 0 &&
          strcmp(p.view_argv[1], "chan-1") == 0,
          "the window is given the channel as its first argument");
    CHECK(has_flag(p.view_argv, p.view_argc, "--scale", "3"), "--scale");
    CHECK(has_flag(p.view_argv, p.view_argc, "--layout", "wide"), "--layout");
    CHECK(has_flag(p.view_argv, p.view_argc, "--render-scale", "1"),
          "--render-scale, which stands down under either resolution");
    CHECK(has_flag(p.view_argv, p.view_argc, "--filter", "linear"), "--filter");
    CHECK(has_flag(p.view_argv, p.view_argc, "--integer", NULL), "--integer");
    CHECK(has_flag(p.view_argv, p.view_argc, "--fullscreen", NULL), "--fullscreen");
    CHECK(!has_flag(p.view_argv, p.view_argc, "--stretch", NULL),
          "and not --stretch: the fit is one choice, not two flags");
    CHECK(p.view_argv[p.view_argc] == NULL, "the window's argv is NULL-terminated too");
    /* The one filter with an opinion of its own keeps the scale it was given,
     * whatever the engine drew at. */
    s.filter = MMO_FILTER_SCALE2X;
    CHECK(mmo_launch_plan_build(&s, "/b/pokeplatinum", "/b/openmmo-view",
                                "chan-1", 0, &p, err, sizeof err) == 0 &&
          has_flag(p.view_argv, p.view_argc, "--render-scale", "2"),
          "and scale2x is handed the render scale the panel holds");
    s.filter = MMO_FILTER_LINEAR;

    printf("a saved sign-in travels as a name with no password:\n");
    good_settings(&s);
    s.pass[0] = '\0';
    CHECK(mmo_launch_plan_build(&s, "/b/port", "/b/view", "c", 0, &p, err, sizeof err) == 0,
          "a plan for an account whose password is not being typed");
    {
        const char *u = env_of(&p, "OPENMMO_USER");
        const char *w = env_of(&p, "OPENMMO_PASS");

        CHECK(u != NULL && strcmp(u, s.user) == 0, "the account still travels");
        /* The whole point: the game reads this as "use the saved sign-in".
         * Omitted, it read as "the game decides", and the game decided on a
         * password of its own. */
        CHECK(w != NULL && w[0] == '\0',
              "and the empty password is stated, not left out");
    }

    printf("what is not chosen is left out, not filled in:\n");
    good_settings(&s);
    s.user[0] = '\0'; s.pass[0] = '\0';
    CHECK(mmo_launch_plan_build(&s, "/b/port", "/b/view", "c", 0, &p, err, sizeof err) == 0,
          "a plan with no account typed in yet");
    {
        const char *u = env_of(&p, "OPENMMO_USER");
        const char *w = env_of(&p, "OPENMMO_PASS");

        CHECK(u != NULL && u[0] == '\0' && w != NULL && w[0] == '\0',
              "the account rows are still stated, empty: left out they would "
              "be whatever the environment already said");
    }
    {
        const char *sess = env_of(&p, "OPENMMO_SESSION");
        const char *sav = env_of(&p, "PC_SAVE");

        CHECK(sess != NULL && strcmp(sess, "1") == 0 &&
              sav != NULL && strcmp(sav, "none") == 0,
              "the session and the ephemeral save are not settings: they are "
              "in every plan");
    }
    CHECK(env_of(&p, "PC_MODS") == NULL && env_of(&p, "PC_MODS_DIR") == NULL,
          "no content packages when none were named");

    /* A real folder, because a mods-dir naming one that is not there reads as
     * unset now and this check is about the folder reaching the game. */
    {
        char modtmp[] = "/tmp/openmmo-mods-XXXXXX";
        char *moddir = mkdtemp(modtmp);

        CHECK(moddir != NULL, "a folder to point the mods row at");
        snprintf(s.mods, sizeof s.mods, "bodies,hub");
        snprintf(s.mods_dir, sizeof s.mods_dir, "%s",
                 moddir != NULL ? moddir : "/tmp");
        CHECK(mmo_launch_plan_build(&s, "/b/port", "/b/view", "c", 0, &p, err,
                                    sizeof err) == 0,
              "a plan with runtime packages");
        v = env_of(&p, "PC_MODS_DIR");
        CHECK(v != NULL && strcmp(v, s.mods_dir) == 0,
              "PC_MODS_DIR is the chosen folder");
        v = env_of(&p, "PC_MODS");
        CHECK(v != NULL && strcmp(v, "bodies,hub") == 0,
              "PC_MODS is the package list, not invented");

        /*
         * The follower package names itself, because nobody can type it: it is filled from the
         * player's own cartridge at Play rather than shipped (followcompose.c), so the list a
         * player never edits has to grow one row on its own when the package is there, and
         * must not grow it when it is not, or the engine refuses a package that does not
         * exist.
         */
        {
            char pkgdir[512];
            char toml[600];
            FILE *f;

            snprintf(pkgdir, sizeof pkgdir, "%s/followers",
                     moddir != NULL ? moddir : "/tmp");
            CHECK(mkdir(pkgdir, 0777) == 0 || errno == EEXIST,
                  "a follower package folder to find");
            snprintf(toml, sizeof toml, "%s/mod.toml", pkgdir);
            f = fopen(toml, "wb");
            CHECK(f != NULL, "a follower package to find");
            if (f != NULL) {
                fputs("id = \"followers\"\n", f);
                fclose(f);
            }
            CHECK(mmo_launch_plan_build(&s, "/b/port", "/b/view", "c", 0, &p,
                                        err, sizeof err) == 0,
                  "a plan built beside a filled follower package");
            v = env_of(&p, "PC_MODS");
            CHECK(v != NULL && strcmp(v, "bodies,hub,followers") == 0,
                  "the follower package names itself, last and once");

            /* And a config that already says it does not get it twice: a
             * repeated name loads the package twice and the second pass
             * re-claims every member the first did. */
            snprintf(s.mods, sizeof s.mods, "bodies,followers,hub");
            CHECK(mmo_launch_plan_build(&s, "/b/port", "/b/view", "c", 0, &p,
                                        err, sizeof err) == 0,
                  "a plan whose config already names the follower package");
            v = env_of(&p, "PC_MODS");
            CHECK(v != NULL && strcmp(v, "bodies,followers,hub") == 0,
                  "a package the config already names is not added again");
            snprintf(s.mods, sizeof s.mods, "bodies,hub");

            /*
             * And an empty `mods` row still means loadorder.txt. Naming any package in PC_MODS
             * stops the engine reading that file at all, so the moment the follower package
             * started naming itself a player who keeps a loadorder would have lost every
             * package in it.
             */
            {
                char lo[600];
                FILE *g;

                snprintf(lo, sizeof lo, "%s/loadorder.txt",
                         moddir != NULL ? moddir : "/tmp");
                g = fopen(lo, "wb");
                CHECK(g != NULL, "a loadorder to fall back to");
                if (g != NULL) {
                    fputs("# a comment\n\nbodies\n  hub  \n", g);
                    fclose(g);
                }
                s.mods[0] = '\0';
                CHECK(mmo_launch_plan_build(&s, "/b/port", "/b/view", "c", 0,
                                            &p, err, sizeof err) == 0,
                      "a plan with no mods row and a loadorder beside it");
                v = env_of(&p, "PC_MODS");
                CHECK(v != NULL && strcmp(v, "bodies,hub,followers") == 0,
                      "an empty mods row still means loadorder.txt, plus the "
                      "package that names itself");
                unlink(lo);

                /* And with neither, the follower package is still named and
                 * nothing is invented around it. */
                CHECK(mmo_launch_plan_build(&s, "/b/port", "/b/view", "c", 0,
                                            &p, err, sizeof err) == 0,
                      "a plan with no mods row and no loadorder");
                v = env_of(&p, "PC_MODS");
                CHECK(v != NULL && strcmp(v, "followers") == 0,
                      "and with neither, only the package that names itself");
                snprintf(s.mods, sizeof s.mods, "bodies,hub");
            }

            /* And a content package ON the install names itself. */
            {
                char impdir[512];
                char imptoml[600];
                FILE *g;

                snprintf(impdir, sizeof impdir, "%s/imports",
                         moddir != NULL ? moddir : "/tmp");
                CHECK(mkdir(impdir, 0777) == 0 || errno == EEXIST,
                      "a content package folder to find");
                snprintf(imptoml, sizeof imptoml, "%s/mod.toml", impdir);
                g = fopen(imptoml, "wb");
                CHECK(g != NULL, "a content package to find");
                if (g != NULL) {
                    fputs("id = \"imports\"\n", g);
                    fclose(g);
                }
                CHECK(mmo_launch_plan_build(&s, "/b/port", "/b/view", "c", 0,
                                            &p, err, sizeof err) == 0,
                      "a plan built beside a filled content package");
                v = env_of(&p, "PC_MODS");
                CHECK(v != NULL && strcmp(v, "bodies,hub,imports,followers")
                      == 0,
                      "a content package on the install names itself, before "
                      "the follower package");

                /* And a config that already says it does not get it twice. */
                snprintf(s.mods, sizeof s.mods, "imports,bodies");
                CHECK(mmo_launch_plan_build(&s, "/b/port", "/b/view", "c", 0,
                                            &p, err, sizeof err) == 0,
                      "a plan whose config already names the content package");
                v = env_of(&p, "PC_MODS");
                CHECK(v != NULL && strcmp(v, "imports,bodies,followers") == 0,
                      "a content package the config names is not added again");

                /* And with no row at all, it is still the reason the game has
                 * Gen 5 on a machine whose settings file says nothing. */
                s.mods[0] = '\0';
                CHECK(mmo_launch_plan_build(&s, "/b/port", "/b/view", "c", 0,
                                            &p, err, sizeof err) == 0,
                      "a plan with no mods row beside a content package");
                v = env_of(&p, "PC_MODS");
                CHECK(v != NULL && strcmp(v, "imports,followers") == 0,
                      "with no row at all, the content package is still named");
                snprintf(s.mods, sizeof s.mods, "bodies,hub");

                unlink(imptoml);
                rmdir(impdir);

                /* And it is gone the moment the package is: a name in
                 * PC_MODS that no folder answers is a boot the engine
                 * refuses outright (pc_modfs: unknown package). */
                CHECK(mmo_launch_plan_build(&s, "/b/port", "/b/view", "c", 0,
                                            &p, err, sizeof err) == 0,
                      "a plan built after the content package was removed");
                v = env_of(&p, "PC_MODS");
                CHECK(v != NULL && strcmp(v, "bodies,hub,followers") == 0,
                      "a package that is not there is not named");
            }

            unlink(toml);
            rmdir(pkgdir);
        }

        /*
         * And the rule that made the folder have to exist: an install that wrote its own
         * absolute mods path into the config and was then deleted left the row behind as a
         * landmine, so a row naming a folder that is gone means what no row means.
         */
        if (moddir != NULL)
            rmdir(moddir);
        CHECK(mmo_launch_plan_build(&s, "/b/port", "/b/view", "c", 0, &p, err,
                                    sizeof err) == 0,
              "a plan whose mods folder has since been deleted still builds");
        CHECK(env_of(&p, "PC_MODS_DIR") == NULL,
              "and a mods-dir that is gone reads as unset, not as a dead path");
        CHECK(s.mods_dir[0] != '\0', "while the row itself is kept");
    }
    s.mods[0] = '\0';
    s.mods_dir[0] = '\0';

    good_settings(&s);
    s.audio = 0;
    snprintf(s.audio_device, sizeof s.audio_device, "Speakers");
    CHECK(mmo_launch_plan_build(&s, "/b/port", "/b/view", "c", 0, &p, err, sizeof err) == 0 &&
          has_flag(p.view_argv, p.view_argc, "--no-audio", NULL) &&
          !has_flag(p.view_argv, p.view_argc, "--audio-device", NULL),
          "sound off opens no device, and does not also name one");
    s.audio = 1;
    CHECK(mmo_launch_plan_build(&s, "/b/port", "/b/view", "c", 0, &p, err, sizeof err) == 0 &&
          has_flag(p.view_argv, p.view_argc, "--audio-device", "Speakers") &&
          !has_flag(p.view_argv, p.view_argc, "--no-audio", NULL),
          "sound on plays it on the named device");

    printf("a plan is never built out of settings that cannot start:\n");
    good_settings(&s);
    s.rom[0] = '\0';
    CHECK(mmo_launch_plan_build(&s, "/b/port", "/b/view", "c", 0, &p, err, sizeof err) == -1,
          "no ROM: refused at the plan, not only at the menu");
    good_settings(&s);
    CHECK(mmo_launch_plan_build(&s, "", "/b/view", "c", 0, &p, err, sizeof err) == -1,
          "and a plan needs both programs' paths");

    printf("the channel keeps two sessions on one machine apart:\n");
    {
        char a[64], b[64];

        mmo_launch_chan(a, sizeof a, 4321);
        mmo_launch_chan(b, sizeof b, 4322);
        CHECK(strcmp(a, b) != 0 && strstr(a, "4321") != NULL,
              "it is named for the process that made it");
        good_settings(&s);
        CHECK(mmo_launch_plan_build(&s, "/b/port", "/b/view", NULL, 4321, &p,
                                    err, sizeof err) == 0 &&
              strcmp(p.chan, a) == 0 &&
              strcmp(p.view_argv[1], a) == 0,
              "and a plan with no channel named uses it on both halves");
    }
}

static void check_print(void)
{
    mmo_launch_settings s;
    mmo_launch_plan p;
    char err[192];
    char path[512];
    char text[2048];
    FILE *f;
    size_t n = 0;

    printf("the printed plan is readable without printing the password:\n");
    good_settings(&s);
    snprintf(s.pass, sizeof s.pass, "hunter2");
    if (mmo_launch_plan_build(&s, "/b/port", "/b/view", "c", 0, &p, err, sizeof err) != 0) {
        printf("  FAIL the plan could not be built (%s)\n", err);
        failures++;
        return;
    }
    snprintf(path, sizeof path, "/tmp/openmmo-launch-plan-%ld.txt", (long)getpid());
    f = fopen(path, "w");
    if (f == NULL) {
        printf("  FAIL no temp file to print into\n");
        failures++;
        return;
    }
    mmo_launch_plan_print(&p, f);
    fclose(f);
    f = fopen(path, "r");
    if (f != NULL) {
        n = fread(text, 1, sizeof text - 1, f);
        fclose(f);
    }
    text[n] = '\0';
    remove(path);

    CHECK(strstr(text, "chan c\n") != NULL, "it names the channel");
    CHECK(strstr(text, "env PC_ROM=") != NULL, "and the environment");
    CHECK(strstr(text, "port /b/port") != NULL && strstr(text, "view /b/view") != NULL,
          "and both command lines");
    CHECK(strstr(text, "hunter2") == NULL, "and never the password itself");
    CHECK(strstr(text, "OPENMMO_PASS=<hidden>") != NULL,
          "which is shown as hidden rather than dropped, so the plan is still whole");
}

/*
 * Where the three files are, in the two layouts that have to work: a built tree, where the
 * game is in `fused/` beside the launcher, and an unpacked release, where the two programs are
 * flat in `bin/` and the ROM is the directory beside it.
 */
static void mkfile(const char *path, int mode)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);

    if (fd >= 0)
        close(fd);
}

static void check_paths(void)
{
    char tmp[] = "/tmp/openmmo-paths-XXXXXX";
    char p[MMO_LAUNCH_PATH], v[MMO_LAUNCH_PATH], r[MMO_LAUNCH_PATH];
    char argv0[MMO_LAUNCH_PATH], path[MMO_LAUNCH_PATH], want[MMO_LAUNCH_PATH];
    char *dir = mkdtemp(tmp);

    if (dir == NULL) {
        printf("  FAIL no temp directory to lay a release out in\n");
        failures++;
        return;
    }
    /* The environment overrides below are the launcher's own, and this suite
     * must not inherit a developer's. */
    unsetenv("OPENMMO_PORT");
    unsetenv("OPENMMO_VIEWER");

    /* The release: openmmo-<v>/{bin,rom}. */
    snprintf(path, sizeof path, "%s/bin", dir);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/rom", dir);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/bin/pokeplatinum", dir);
    mkfile(path, 0755);
    snprintf(path, sizeof path, "%s/bin/openmmo-view", dir);
    mkfile(path, 0755);
    snprintf(path, sizeof path, "%s/rom/%s", dir, MMO_LAUNCH_ROM_NAME);
    mkfile(path, 0644);

    snprintf(argv0, sizeof argv0, "%s/bin/openmmo-launch", dir);
    mmo_launch_paths(argv0, p, sizeof p, v, sizeof v, r, sizeof r);
    snprintf(want, sizeof want, "%s/bin/pokeplatinum", dir);
    CHECK(strcmp(p, want) == 0, "a release finds the game flat beside the launcher");
    snprintf(want, sizeof want, "%s/bin/openmmo-view", dir);
    CHECK(strcmp(v, want) == 0, "and the window beside it too");
    CHECK(r[0] != '\0' && access(r, R_OK) == 0,
          "and the ROM in the directory it told the player to use");

    /* The built tree: tree/build/{fused/pokeplatinum,openmmo-view}, and its own
     * root, because `rom/` is resolved from the game's directory and a tree
     * sharing the release's would be the release's ROM found by accident. */
    snprintf(path, sizeof path, "%s/tree", dir);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/tree/build", dir);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/tree/build/fused", dir);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/tree/build/fused/pokeplatinum", dir);
    mkfile(path, 0755);
    snprintf(path, sizeof path, "%s/tree/build/openmmo-view", dir);
    mkfile(path, 0755);

    snprintf(argv0, sizeof argv0, "%s/tree/build/openmmo-launch", dir);
    mmo_launch_paths(argv0, p, sizeof p, v, sizeof v, r, sizeof r);
    snprintf(want, sizeof want, "%s/tree/build/fused/pokeplatinum", dir);
    CHECK(strcmp(p, want) == 0, "a built tree still finds the game under fused/");
    CHECK(r[0] == '\0',
          "and reports no ROM rather than a path with nothing at it");

    /* The cartridge belongs where the game reads it from, which under fused/
     * is not the folder beside the launcher. A tree that has one there is a
     * tree that can start, so the default has to name it. */
    snprintf(path, sizeof path, "%s/tree/build/rom", dir);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/tree/build/rom/%s", dir, MMO_LAUNCH_ROM_NAME);
    mkfile(path, 0644);
    mmo_launch_paths(argv0, p, sizeof p, v, sizeof v, r, sizeof r);
    CHECK(r[0] != '\0' && access(r, R_OK) == 0,
          "the default ROM is the rom/ the game itself resolves, not the launcher's");
    snprintf(path, sizeof path, "%s/tree/build/rom/%s", dir, MMO_LAUNCH_ROM_NAME);
    remove(path);

    /* Neither layout has a game: the built tree's path is what gets named,
     * because that is the one an unbuilt tree fixes with a make. */
    snprintf(path, sizeof path, "%s/empty", dir);
    mkdir(path, 0755);
    snprintf(argv0, sizeof argv0, "%s/empty/openmmo-launch", dir);
    mmo_launch_paths(argv0, p, sizeof p, v, sizeof v, r, sizeof r);
    snprintf(want, sizeof want, "%s/empty/fused/pokeplatinum", dir);
    CHECK(strcmp(p, want) == 0, "with neither, the game it names has a make to fix it");

    setenv("OPENMMO_PORT", "/elsewhere/game", 1);
    setenv("OPENMMO_VIEWER", "/elsewhere/window", 1);
    snprintf(argv0, sizeof argv0, "%s/bin/openmmo-launch", dir);
    mmo_launch_paths(argv0, p, sizeof p, v, sizeof v, r, sizeof r);
    CHECK(strcmp(p, "/elsewhere/game") == 0 && strcmp(v, "/elsewhere/window") == 0,
          "and the environment overrides both, whatever is on disk");
    unsetenv("OPENMMO_PORT");
    unsetenv("OPENMMO_VIEWER");

    snprintf(path, sizeof path, "%s/rom/%s", dir, MMO_LAUNCH_ROM_NAME); remove(path);
    snprintf(path, sizeof path, "%s/bin/pokeplatinum", dir);   remove(path);
    snprintf(path, sizeof path, "%s/bin/openmmo-view", dir);   remove(path);
    snprintf(path, sizeof path, "%s/tree/build/fused/pokeplatinum", dir); remove(path);
    snprintf(path, sizeof path, "%s/tree/build/openmmo-view", dir); remove(path);
    snprintf(path, sizeof path, "%s/tree/build/rom", dir); rmdir(path);
    snprintf(path, sizeof path, "%s/tree/build/fused", dir); rmdir(path);
    snprintf(path, sizeof path, "%s/tree/build", dir);  rmdir(path);
    snprintf(path, sizeof path, "%s/tree", dir);        rmdir(path);
    snprintf(path, sizeof path, "%s/empty", dir);       rmdir(path);
    snprintf(path, sizeof path, "%s/rom", dir);         rmdir(path);
    snprintf(path, sizeof path, "%s/bin", dir);         rmdir(path);
    rmdir(dir);
}

static void check_menu_keys(void)
{
    mmo_launch_settings s;
    mmo_launch_menu m;
    char scratch[64];
    int i, act;

    printf("the same keys the window takes change the settings:\n");
    good_settings(&s);
    mmo_launch_menu_init(&m, &s);

    CHECK(m.sel == MMO_LAUNCH_R_USER && !m.editing,
          "the menu starts on ACCOUNT, not editing");
    CHECK(mmo_launch_menu_key(&m, MMO_LAUNCH_KEY_DOWN) == MMO_LAUNCH_MENU_NONE &&
          m.sel == MMO_LAUNCH_R_PASS,
          "down moves to the next row");
    CHECK(mmo_launch_menu_key(&m, MMO_LAUNCH_KEY_UP) == MMO_LAUNCH_MENU_NONE &&
          m.sel == MMO_LAUNCH_R_USER,
          "up moves back");

    /* Wrap: up from ACCOUNT is QUIT, the last row. */
    CHECK(mmo_launch_menu_key(&m, MMO_LAUNCH_KEY_UP) == MMO_LAUNCH_MENU_NONE &&
          m.sel == MMO_LAUNCH_R_QUIT,
          "up from the first row wraps to QUIT");
    CHECK(mmo_launch_menu_key(&m, MMO_LAUNCH_KEY_ENTER) == MMO_LAUNCH_MENU_QUIT,
          "enter on QUIT is quit");

    m.sel = MMO_LAUNCH_R_SCALE;
    CHECK(s.scale == MMO_LAUNCH_SCALE_AUTO, "scale starts at the window's default");
    CHECK(mmo_launch_menu_key(&m, MMO_LAUNCH_KEY_RIGHT) == MMO_LAUNCH_MENU_NONE &&
          s.scale == 1,
          "right on SCALE raises it from auto to 1");
    CHECK(mmo_launch_menu_key(&m, MMO_LAUNCH_KEY_LEFT) == MMO_LAUNCH_MENU_NONE &&
          s.scale == MMO_LAUNCH_SCALE_AUTO,
          "left puts it back");

    m.sel = MMO_LAUNCH_R_PACE;
    CHECK(s.pace == MMO_PACE_CONSOLE &&
          strcmp(mmo_launch_menu_value(&m, MMO_LAUNCH_R_PACE, scratch,
                                       sizeof scratch),
                 "CONSOLE RATE (60 FPS)") == 0,
          "pacing starts at the console's rate");
    CHECK(mmo_launch_menu_key(&m, MMO_LAUNCH_KEY_RIGHT) == MMO_LAUNCH_MENU_NONE &&
          s.pace == MMO_PACE_UNLIMITED,
          "right on PACING turns the pacer off");
    mmo_launch_menu_key(&m, MMO_LAUNCH_KEY_RIGHT);
    CHECK(s.pace == MMO_PACE_CONSOLE, "and again wraps back");

    m.sel = MMO_LAUNCH_R_VIEWPORT;
    CHECK(strcmp(mmo_launch_menu_value(&m, MMO_LAUNCH_R_VIEWPORT, scratch,
                                       sizeof scratch), "auto") == 0,
          "the viewport row starts on auto");
    CHECK(mmo_launch_menu_key(&m, MMO_LAUNCH_KEY_RIGHT) == MMO_LAUNCH_MENU_NONE &&
          strcmp(s.viewport, "native") == 0,
          "right on VIEWPORT steps to native");
    snprintf(s.viewport, sizeof s.viewport, "512");
    CHECK(strcmp(mmo_launch_menu_value(&m, MMO_LAUNCH_R_VIEWPORT, scratch,
                                       sizeof scratch), "512") == 0,
          "a hand-typed width from the config shows as itself");
    mmo_launch_menu_key(&m, MMO_LAUNCH_KEY_RIGHT);
    CHECK(strcmp(s.viewport, "auto") == 0,
          "and cycling steps it onto the named ring at auto");

    mmo_launch_menu_key(&m, MMO_LAUNCH_KEY_LEFT);
    CHECK(s.camera == MMO_LAUNCH_CAMERA_MAX,
          "and left again wraps to the ceiling");
    s.camera = 100;

    m.sel = MMO_LAUNCH_R_HD3D;
    CHECK(s.hd3d == MMO_LAUNCH_HD3D_MIN &&
          strcmp(mmo_launch_menu_value(&m, MMO_LAUNCH_R_HD3D, scratch,
                                       sizeof scratch), "SD") == 0,
          "3D detail starts at SD, and says so rather than saying a number");
    CHECK(mmo_launch_menu_key(&m, MMO_LAUNCH_KEY_RIGHT) == MMO_LAUNCH_MENU_NONE &&
          s.hd3d == MMO_LAUNCH_HD3D_MIN + 1 &&
          strcmp(mmo_launch_menu_value(&m, MMO_LAUNCH_R_HD3D, scratch,
                                       sizeof scratch), "HD") == 0,
          "right on 3D DETAIL is HD");
    CHECK(mmo_launch_menu_key(&m, MMO_LAUNCH_KEY_RIGHT) == MMO_LAUNCH_MENU_NONE &&
          s.hd3d == MMO_LAUNCH_HD3D_MAX &&
          strcmp(mmo_launch_menu_value(&m, MMO_LAUNCH_R_HD3D, scratch,
                                       sizeof scratch), "ULTRA") == 0,
          "and right again is ULTRA, the port's own ceiling");
    mmo_launch_menu_key(&m, MMO_LAUNCH_KEY_LEFT);
    mmo_launch_menu_key(&m, MMO_LAUNCH_KEY_LEFT);
    CHECK(s.hd3d == MMO_LAUNCH_HD3D_MIN, "two lefts put it back");

    m.sel = MMO_LAUNCH_R_AUDIO;
    CHECK(s.audio == 1 &&
          strcmp(mmo_launch_menu_value(&m, MMO_LAUNCH_R_AUDIO, scratch,
                                       sizeof scratch), "ON") == 0,
          "sound starts on");
    CHECK(mmo_launch_menu_key(&m, MMO_LAUNCH_KEY_RIGHT) == MMO_LAUNCH_MENU_NONE &&
          s.audio == 0,
          "right on SOUND turns it off");

    m.sel = MMO_LAUNCH_R_ROM;
    CHECK(mmo_launch_menu_key(&m, MMO_LAUNCH_KEY_ENTER) == MMO_LAUNCH_MENU_PICK_ROM &&
          !m.editing,
          "enter on CARTRIDGES opens the file manager, not a typed field");

    m.sel = MMO_LAUNCH_R_USER;
    mmo_launch_menu_key(&m, MMO_LAUNCH_KEY_ENTER);
    mmo_launch_menu_text(&m, "er");
    mmo_launch_menu_key(&m, MMO_LAUNCH_KEY_ENTER);
    CHECK(strcmp(s.user, "tester") == 0,
          "a typed row opens on the value it holds and writes back what was typed");

    /* A script of the same keys: from the top, down to PLAY, enter. */
    mmo_launch_menu_init(&m, &s);
    act = MMO_LAUNCH_MENU_NONE;
    for (i = 0; i < MMO_LAUNCH_R_PLAY; i++)
        act = mmo_launch_menu_line(&m, "down");
    CHECK(m.sel == MMO_LAUNCH_R_PLAY, "a script of downs lands on PLAY");
    CHECK(mmo_launch_menu_line(&m, "enter") == MMO_LAUNCH_MENU_PLAY,
          "and enter there is Play, not a cycle");
    CHECK(mmo_launch_menu_line(&m, "wobble") == -1,
          "an unknown script line is refused rather than ignored");
    (void)act;
}

/* ------------------------------------------------------------------ */
/* Playing with no server                                              */
/* ------------------------------------------------------------------ */

/* The line of a `key value` record, or NULL. */
static int link_line(const char *path, int n, char *out, size_t cap)
{
    FILE *f = fopen(path, "rb");
    int i = 0;

    out[0] = '\0';
    if (f == NULL)
        return -1;
    while (fgets(out, (int)cap, f) != NULL) {
        size_t l = strlen(out);

        while (l > 0 && (out[l - 1] == '\n' || out[l - 1] == '\r'))
            out[--l] = '\0';
        if (i++ == n) {
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    out[0] = '\0';
    return -1;
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");

    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}

/* Entries in `dir` whose name ends in `suffix`, or -1 when there is no such
 * folder. The anchor is named for the clock, so the check that it was written
 * cannot name the file it is looking for. */
static int count_suffix(const char *dir, const char *suffix)
{
    size_t slen = strlen(suffix);
    DIR *d = opendir(dir);
    struct dirent *e;
    int n = 0;

    if (d == NULL)
        return -1;
    while ((e = readdir(d)) != NULL) {
        size_t len = strlen(e->d_name);

        if (len > slen && strcmp(e->d_name + len - slen, suffix) == 0)
            n++;
    }
    closedir(d);
    return n;
}

/* How many lines the sweep had to say. Every one of them is a handoff it
 * could not adopt, so silence is the good answer. */
static void count_note(void *ud, const char *line)
{
    (void)line;
    ++*(int *)ud;
}

static void check_offline(void)
{
    mmo_launch_settings s;
    mmo_launch_plan p;
    mmo_launch_offline o;
    char root[] = "/tmp/openmmo-offline-XXXXXX";
    char err[192];
    char line[256];
    char path[512];
    char stamps[MMO_LAUNCH_SAVE_KEEP + 4][MMO_LAUNCH_STAMP];
    const char *v;
    char *dir;
    int i, n;

    printf("the offline row plays the save file, with no server behind it:\n");

    /* The root a game program implies, in the two layouts that ship. */
    mmo_launch_install_root("/opt/openmmo/bin/pokeplatinum", line, sizeof line);
    CHECK(strcmp(line, "/opt/openmmo") == 0,
          "a release's game in bin/ puts save/ at the install root");
    mmo_launch_install_root("/w/mmo/build/fused/pokeplatinum", line, sizeof line);
    CHECK(strcmp(line, "/w/mmo/build") == 0,
          "and a built tree's in fused/ beside the other programs");
    mmo_launch_install_root("/somewhere/pokeplatinum", line, sizeof line);
    CHECK(strcmp(line, "/somewhere") == 0,
          "anywhere else the folder holding the game is the root");

    dir = mkdtemp(root);
    if (dir == NULL) {
        CHECK(0, "a temporary install root could be made");
        return;
    }
    snprintf(path, sizeof path, "%s/bin", dir);
    if (mkdir(path, 0700) != 0) {
        CHECK(0, "a temporary bin/ could be made");
        return;
    }
    snprintf(path, sizeof path, "%s/bin/pokeplatinum", dir);

    CHECK(mmo_launch_offline_open(path, 1, "2026-09-04 11:30:15", &o,
                                  err, sizeof err) == 0,
          "a session names itself from the clock it will be played on");
    CHECK(strcmp(o.stamp, "20260904-113015") == 0,
          "the stamp is that same reading, so the recording and the clock a"
          " replay would need are one answer");
    snprintf(line, sizeof line, "%s/save", dir);
    CHECK(strcmp(o.dir, line) == 0, "the folder is save/ under the install");
    CHECK(access(o.dir, R_OK) == 0, "and it is made rather than assumed");
    snprintf(line, sizeof line, "%s/save/sessions", dir);
    CHECK(access(line, R_OK) == 0, "with sessions/ inside it");

    /* One session at a time owns the save. Two launchers on one install both
     * write it and both keep a stamped copy of what the other left, so the
     * game a player is in the middle of is the one that loses. */
    {
        mmo_launch_offline two;

        CHECK(mmo_launch_offline_open(path, 1, "2026-09-04 11:31:00", &two,
                                      err, sizeof err) != 0
                  && strstr(err, "another launcher") != NULL,
              "a second launcher is refused the save the first has open, and"
              " told which it is");
        mmo_launch_offline_close(&o);
        CHECK(mmo_launch_offline_open(path, 1, "2026-09-04 11:31:00", &two,
                                      err, sizeof err) == 0,
              "and takes it the moment the first session lets go");
        mmo_launch_offline_close(&two);
        mmo_launch_offline_close(&two);
        CHECK(1, "letting go twice is not an error: a session that never"
                 " started is closed on every way out of the row");
    }

    CHECK(mmo_launch_offline_open(path, 1, "not a date", &o, err, sizeof err) != 0,
          "a clock that does not read as a date is refused, not guessed at");
    CHECK(mmo_launch_offline_open(path, 1, "2026-09-04 11:30:15", &o,
                                  err, sizeof err) == 0, "and back");

    /* The plan. */
    good_settings(&s);
    CHECK(mmo_launch_plan_build_offline(&s, "/b/pokeplatinum",
                                        "/b/openmmo-view", "chan-off", 0, &o,
                                        &p, err, sizeof err) == 0,
          "an offline plan is built");
    v = env_of(&p, "PC_SAVE");
    CHECK(v != NULL && strcmp(v, o.save) == 0,
          "PC_SAVE names the image: offline the file IS the game");
    v = env_of(&p, "PC_RTC");
    CHECK(v != NULL && strcmp(v, "2026-09-04 11:30:15") == 0,
          "PC_RTC carries this machine's clock, so day and night are real");
    v = env_of(&p, "PC_RECORD_INPUT");
    CHECK(v != NULL && strcmp(v, o.record) == 0,
          "PC_RECORD_INPUT writes the session down as it is played");
    v = env_of(&p, "OPENMMO_SESSION");
    CHECK(v != NULL && strcmp(v, "0") == 0,
          "OPENMMO_SESSION is STATED as 0, so a leftover one cannot turn it on");
    v = env_of(&p, "OPENMMO_USER");
    CHECK(v != NULL && v[0] == '\0',
          "and no account travels with it, for the same reason");
    CHECK(has_flag(p.view_argv, p.view_argc, "--offline", NULL),
          "the window is told, so it draws no screen that needs a server");
    CHECK(mmo_launch_plan_build(&s, "/b/pokeplatinum", "/b/openmmo-view",
                                "chan-on", 0, &p, err, sizeof err) == 0 &&
          !has_flag(p.view_argv, p.view_argc, "--offline", NULL),
          "a session's plan is unchanged: PC_SAVE=none and no --offline");
    v = env_of(&p, "PC_SAVE");
    CHECK(v != NULL && strcmp(v, "none") == 0,
          "the server's party is still never written to a local file");

    /* The record. version first, whatever else changes. */
    CHECK(mmo_launch_offline_begin(&o, 12, err, sizeof err) == 0,
          "the session is written down before the game starts");
    CHECK(link_line(o.link, 0, line, sizeof line) == 0 &&
          strcmp(line, "version 1") == 0,
          "version is the first line, so a reader that knows nothing else can"
          " still say `not mine'");
    CHECK(link_line(o.link, 1, line, sizeof line) == 0 &&
          strcmp(line, "revision 12") == 0,
          "then which build played it");
    CHECK(link_line(o.link, 3, line, sizeof line) == 0 &&
          strcmp(line, "boot-sha256 none") == 0,
          "a first session starts from no file at all, which is what a New"
          " Game is");
    CHECK(mmo_launch_offline_begin(&o, -1, err, sizeof err) == 0 &&
          link_line(o.link, 1, line, sizeof line) == 0 &&
          strcmp(line, "revision unknown") == 0,
          "an install that cannot name its revision says so, rather than 0");

    /* Closing it keeps the image the session left. */
    write_file(o.save, "an image");
    write_file(o.record, "0 keys none\n");
    CHECK(mmo_launch_offline_end(&o, err, sizeof err) == 0,
          "and closed when the game ends");
    CHECK(link_line(o.link, 5, line, sizeof line) == 0 &&
          strncmp(line, "recording-sha256 ", 17) == 0 && strlen(line) == 17 + 64,
          "with the recording's hash");
    CHECK(link_line(o.link, 6, line, sizeof line) == 0 &&
          strncmp(line, "quit-sha256 ", 12) == 0 && strlen(line) == 12 + 64,
          "and the image's");
    CHECK(link_line(o.link, 7, line, sizeof line) != 0,
          "a session the game did not close writes no end frame, rather than"
          " a made-up one");
    snprintf(path, sizeof path, "%s.%s", o.save, o.stamp);
    CHECK(access(path, R_OK) == 0,
          "the image the session left is kept under its own stamp");

    /* Eleven of them, and the oldest goes. */
    for (i = 0; i < MMO_LAUNCH_SAVE_KEEP + 1; i++) {
        snprintf(path, sizeof path, "%s/save/platinum.sav.202609%02d-000000",
                 dir, i + 1);
        write_file(path, "old");
    }
    n = mmo_launch_offline_list(o.dir, stamps,
                                (int)(sizeof stamps / sizeof stamps[0]));
    CHECK(n >= MMO_LAUNCH_SAVE_KEEP + 1, "every kept image is listed");
    CHECK(strcmp(stamps[0], stamps[n - 1]) > 0,
          "newest first, by the stamp's own order and never by readdir's");
    write_file(o.save, "another image");
    o.stamp[14] = '9';
    /* A stale one from the session before, cleared by the start of this one:
     * an end frame is the field a reader has no way to sanity-check, so it
     * must never be inherited. */
    snprintf(path, sizeof path, "%s.frames", o.save);
    write_file(path, "999\n");
    CHECK(mmo_launch_offline_begin(&o, 12, err, sizeof err) == 0 &&
          access(path, F_OK) != 0,
          "a session begins by throwing away any end frame left lying about");
    write_file(path, "27168\n");
    CHECK(mmo_launch_offline_end(&o, err, sizeof err) == 0,
          "a second session closes too");
    CHECK(link_line(o.link, 7, line, sizeof line) == 0 &&
          strcmp(line, "end-frame 27168") == 0,
          "and the frame the game stopped on is the record's last line");
    CHECK(access(path, F_OK) != 0,
          "the game's own file is taken, not copied: an end frame can only be"
          " read once");
    n = mmo_launch_offline_list(o.dir, stamps,
                                (int)(sizeof stamps / sizeof stamps[0]));
    CHECK(n == MMO_LAUNCH_SAVE_KEEP,
          "and the folder is held at ten: the oldest goes, never the newest");

    /* Restoring keeps what it replaces. */
    snprintf(path, sizeof path, "%s/save/platinum.sav.20260901-000000", dir);
    write_file(path, "the one to go back to");
    CHECK(mmo_launch_offline_restore(o.dir, "20260901-000000",
                                     "2026-09-05 08:00:00",
                                     err, sizeof err) == 0,
          "an earlier game can be put back");
    CHECK(link_line(o.save, 0, line, sizeof line) == 0 &&
          strcmp(line, "the one to go back to") == 0,
          "it is the save file afterwards");
    snprintf(path, sizeof path, "%s/save/platinum.sav.20260905-080000", dir);
    CHECK(access(path, R_OK) == 0,
          "and the one it replaced was kept first, so a wrong choice is one"
          " more restore to undo");

    /* And never under a session playing that file. A restore that landed
     * there would leave the live session writing out a game it never loaded,
     * over the top of the one the player has only just asked to have back. */
    {
        mmo_launch_offline two;
        char exe[512];

        snprintf(exe, sizeof exe, "%s/bin/pokeplatinum", dir);
        snprintf(path, sizeof path, "%s/save/platinum.sav.20260906-000000",
                 dir);
        write_file(path, "the one to go back to next");
        write_file(o.save, "the game being played");
        CHECK(mmo_launch_offline_open(exe, 1, "2026-09-06 09:00:00", &two,
                                      err, sizeof err) == 0,
              "another launcher opens the save a restore would replace");
        {
            /* Through a buffer the size a caller declares, not the roomy one
             * this file uses: half a sentence is worse than none, and the
             * front door's own `err` is exactly this wide. */
            char tight[MMO_LAUNCH_TEXT];

            CHECK(mmo_launch_offline_restore(o.dir, "20260906-000000",
                                             "2026-09-06 09:01:00",
                                             tight, sizeof tight) != 0
                      && strstr(tight, "another launcher") != NULL,
                  "a restore is refused while that session has it, and the"
                  " whole sentence fits the buffer a caller declares");
        }
        CHECK(link_line(o.save, 0, line, sizeof line) == 0 &&
              strcmp(line, "the game being played") == 0,
              "so the game being played is still the one at the name");
        snprintf(path, sizeof path, "%s/save/platinum.sav.20260906-090100",
                 dir);
        CHECK(access(path, F_OK) != 0,
              "nothing was set aside for a restore that did not happen");
        snprintf(path, sizeof path, "%s/save/platinum.sav.restore", dir);
        CHECK(link_line(path, 2, line, sizeof line) == 0 &&
              strcmp(line, "at 20260905-080000") == 0,
              "and the marker still stands over the restore that did: a"
              " refusal is not written down as one");
        snprintf(path, sizeof path, "%s/save/platinum.sav.20260906-000000",
                 dir);
        CHECK(access(path, R_OK) == 0,
              "with the image asked for still under its own stamp");
        mmo_launch_offline_close(&two);
        CHECK(mmo_launch_offline_restore(o.dir, "20260906-000000",
                                         "2026-09-06 09:02:00",
                                         err, sizeof err) == 0 &&
              link_line(o.save, 0, line, sizeof line) == 0 &&
              strcmp(line, "the one to go back to next") == 0,
              "and the row works the moment that session lets go");
    }
    CHECK(mmo_launch_offline_restore(o.dir, "../../etc/passwd", NULL,
                                     err, sizeof err) != 0,
          "a name that is not a stamp is refused before anything is opened");
    CHECK(mmo_launch_offline_restore(o.dir, "20990101-000000", NULL,
                                     err, sizeof err) != 0,
          "and a stamp this folder does not keep says so");
}

/* Carrying a session out to the offline row: the handoff the game writes, the
 * marker that says the player asked for it, and what the front door does with
 * the pair afterwards. */
static void check_export(void)
{
    mmo_launch_settings s;
    mmo_launch_plan p;
    mmo_launch_export x;
    char root[] = "/tmp/openmmo-export-XXXXXX";
    char err[192];
    char line[256];
    char exe[512];
    char path[512];
    char rept[560];
    char answered[600];
    char stamps[MMO_LAUNCH_SAVE_KEEP + 4][MMO_LAUNCH_STAMP];
    const char *v;
    char *dir;
    int i, n;

    printf("a session can be carried out to the offline row:\n");

    dir = mkdtemp(root);
    if (dir == NULL) {
        CHECK(0, "a temporary install root could be made");
        return;
    }
    snprintf(path, sizeof path, "%s/bin", dir);
    if (mkdir(path, 0700) != 0) {
        CHECK(0, "a temporary bin/ could be made");
        return;
    }
    snprintf(exe, sizeof exe, "%s/bin/pokeplatinum", dir);

    CHECK(mmo_launch_export_open(exe, 1, 4321, &x, err, sizeof err) == 0,
          "a session names somewhere to write itself out to");
    snprintf(line, sizeof line, "%s/save/export-4321.sav", dir);
    CHECK(strcmp(x.save, line) == 0,
          "under save/, named for the launcher that owns the session");
    snprintf(line, sizeof line, "%s.ok", x.save);
    CHECK(strcmp(x.mark, line) == 0, "with the marker beside it");
    snprintf(line, sizeof line, "%s/save/sessions", dir);
    CHECK(access(line, R_OK) == 0,
          "and sessions/ made, because adopting is not the moment to find it"
          " missing");

    /* The plan says the name to the game and nothing else changes. */
    good_settings(&s);
    CHECK(mmo_launch_plan_build_export(&s, "/b/pokeplatinum", "/b/openmmo-view",
                                       "chan-x", 0, &x, &p, err, sizeof err) == 0,
          "a session plan is built with somewhere to export to");
    v = env_of(&p, "OPENMMO_EXPORT");
    CHECK(v != NULL && strcmp(v, x.save) == 0,
          "OPENMMO_EXPORT names the handoff");
    v = env_of(&p, "PC_SAVE");
    CHECK(v != NULL && strcmp(v, "none") == 0,
          "and PC_SAVE still says none: a plan names no player save, and the"
          " game is what points the chip at the handoff");
    CHECK(!has_flag(p.view_argv, p.view_argc, "--offline", NULL),
          "a session is still a session");
    CHECK(mmo_launch_plan_build(&s, "/b/pokeplatinum", "/b/openmmo-view",
                                "chan-x", 0, &p, err, sizeof err) == 0 &&
          env_of(&p, "OPENMMO_EXPORT") == NULL,
          "a plan built without one names none, rather than an empty value");

    /* An old image at the handoff name would be loaded as a player save. */
    write_file(x.save, "somebody else's game");
    CHECK(mmo_launch_export_open(exe, 1, 4321, &x, err, sizeof err) == 0 &&
          access(x.save, F_OK) != 0,
          "an image left at the name is cleared before the game can load it");
    snprintf(path, sizeof path, "%s/save/export-4321.sav.tmp", dir);
    write_file(path, "half a write from last time");
    CHECK(mmo_launch_export_open(exe, 1, 4321, &x, err, sizeof err) == 0,
          "and so is the temporary the engine would adopt under that name");
    snprintf(path, sizeof path, "%s/save/export-4321.sav.tmp", dir);
    CHECK(access(path, F_OK) != 0, "which is the one a crash leaves behind");

    /* Nothing asked for, nothing adopted. */
    write_file(x.save, "what a script's own save left");
    line[0] = 'x';
    CHECK(mmo_launch_export_adopt(&x, "2026-09-04 12:00:00", 7, line,
                                  sizeof line, err, sizeof err) == 0
              && line[0] == '\0',
          "an image with no marker is not an export: a script inside the"
          " session writes one too");
    snprintf(path, sizeof path, "%s/save/platinum.sav", dir);
    CHECK(access(path, F_OK) != 0, "so the offline save is not touched");

    /* Asked for, with nothing there to replace. */
    write_file(x.mark, "character Probe\n");
    CHECK(mmo_launch_export_adopt(&x, "2026-09-04 12:00:00", 7, line,
                                  sizeof line, err, sizeof err) == 1,
          "a marked image is adopted as the game the offline row plays");
    CHECK(strcmp(line, "20260904-120000") == 0, "and named for the clock");
    CHECK(link_line(path, 0, line, sizeof line) == 0 &&
          strcmp(line, "what a script's own save left") == 0,
          "the image is the save file afterwards");
    CHECK(access(x.save, F_OK) != 0, "and the handoff is gone, not copied");
    snprintf(path, sizeof path, "%s/save/sessions/20260904-120000.export", dir);
    CHECK(link_line(path, 0, line, sizeof line) == 0 &&
          strcmp(line, "version 1") == 0,
          "the anchor is written down, version first");
    CHECK(link_line(path, 1, line, sizeof line) == 0 &&
          strcmp(line, "revision 7") == 0, "with the build that exported it");
    CHECK(link_line(path, 3, line, sizeof line) == 0 &&
          strncmp(line, "export-sha256 ", 14) == 0 && strlen(line) == 14 + 64,
          "and the hash an offline session's boot-sha256 will match, which is"
          " where the chain starts");
    CHECK(link_line(path, 4, line, sizeof line) == 0 &&
          strcmp(line, "character Probe") == 0, "and whose session it was");

    /* Asked for again, over a game that is already there, and that game has
     * a report of its own beside it, and the answer the server gave about the
     * one before that. Both are about an image the adopt is about to replace.
     * The report goes down first: writing one clears the answer beside it. */
    {
        mmo_import_report r;

        memset(&r, 0, sizeof r);
        r.client_revision = -1;
        r.play_seconds = 4000;
        snprintf(r.sha256, sizeof r.sha256,
                 "2222222222222222222222222222222222222222222222222222222222222222");
        snprintf(rept, sizeof rept, "%s/save/platinum.sav.report", dir);
        CHECK(mmo_import_report_write(rept, &r) == 0,
              "the game being replaced has a report of its own beside it");
    }
    snprintf(answered, sizeof answered, "%s/save/platinum.sav.report.landed",
             dir);
    write_file(answered, "an answer about a save older still");
    CHECK(mmo_launch_export_open(exe, 1, 4321, &x, err, sizeof err) == 0,
          "the paths are named the same way next time");
    write_file(x.save, "the second export");
    write_file(x.mark, "character Probe\nnot a field\n");
    CHECK(mmo_launch_export_adopt(&x, "2026-09-05 09:30:00", 7, line,
                                  sizeof line, err, sizeof err) == 1,
          "and adopted over the one already there");
    CHECK(access(rept, F_OK) != 0 && access(answered, F_OK) != 0,
          "the replaced game's report and the answer about it go with it");
    {
        mmo_launch_import offer;

        CHECK(mmo_launch_import_offer(exe, 1, &offer) == 0,
              "so nothing offers the server a game the player no longer has");
    }
    snprintf(path, sizeof path, "%s/save/platinum.sav.20260905-093000", dir);
    CHECK(link_line(path, 0, line, sizeof line) == 0 &&
          strcmp(line, "what a script's own save left") == 0,
          "the game being replaced is kept first, never overwritten");
    snprintf(path, sizeof path, "%s/save/platinum.sav", dir);
    CHECK(link_line(path, 0, line, sizeof line) == 0 &&
          strcmp(line, "the second export") == 0,
          "and the new one is what PLAY OFFLINE picks up");

    /* Eleven kept images, and the oldest still goes. */
    for (i = 0; i < MMO_LAUNCH_SAVE_KEEP + 1; i++) {
        snprintf(path, sizeof path, "%s/save/platinum.sav.202608%02d-000000",
                 dir, i + 1);
        write_file(path, "old");
    }
    CHECK(mmo_launch_export_open(exe, 1, 4321, &x, err, sizeof err) == 0, "again");
    write_file(x.save, "the third export");
    write_file(x.mark, "");
    CHECK(mmo_launch_export_adopt(&x, "2026-09-06 09:30:00", -1, line,
                                  sizeof line, err, sizeof err) == 1,
          "a marker with no name in it is still the player asking");
    n = mmo_launch_offline_list(x.dir, stamps,
                                (int)(sizeof stamps / sizeof stamps[0]));
    CHECK(n == MMO_LAUNCH_SAVE_KEEP,
          "an adopted export prunes to ten like every other kept image");
    snprintf(path, sizeof path, "%s/save/sessions/20260906-093000.export", dir);
    CHECK(link_line(path, 1, line, sizeof line) == 0 &&
          strcmp(line, "revision unknown") == 0,
          "an install that cannot name its revision says so, rather than 0");
    CHECK(link_line(path, 4, line, sizeof line) != 0,
          "and a marker with no character names none");

    /* The claim the anchor is for: an offline session that boots from the
     * adopted image starts its chain at the hash the export wrote down. */
    {
        mmo_launch_offline o;
        char anchor[512];
        char boot[256];

        snprintf(anchor, sizeof anchor,
                 "%s/save/sessions/20260906-093000.export", dir);
        CHECK(mmo_launch_offline_open(exe, 1, "2026-09-06 10:00:00", &o,
                                      err, sizeof err) == 0
                  && mmo_launch_offline_begin(&o, 7, err, sizeof err) == 0,
              "an offline session opens on the adopted game");
        CHECK(link_line(o.link, 3, boot, sizeof boot) == 0
                  && link_line(anchor, 3, line, sizeof line) == 0
                  && strcmp(boot + 12, line + 14) == 0,
              "and its boot-sha256 is the export's own hash: the chain starts"
              " where the session left the server");
        mmo_launch_offline_close(&o);
    }

    /* A marker with no image is a failure worth a sentence. */
    CHECK(mmo_launch_export_open(exe, 1, 4321, &x, err, sizeof err) == 0, "once more");
    write_file(x.mark, "character Probe\n");
    CHECK(mmo_launch_export_adopt(&x, "2026-09-07 09:30:00", 7, line,
                                  sizeof line, err, sizeof err) == -1
              && strstr(err, "no") != NULL,
          "an export the game said it made but did not is said out loud");

    /* And closing takes the pair away whatever happened. */
    write_file(x.save, "left over");
    snprintf(path, sizeof path, "%s.tmp", x.save);
    write_file(path, "left over");
    mmo_launch_export_close(&x);
    CHECK(access(x.save, F_OK) != 0 && access(x.mark, F_OK) != 0
              && access(path, F_OK) != 0,
          "the handoff, its temporary and the marker are all gone afterwards");

    /* But a refused adopt keeps the game that was played. Until the image has
     * landed it is the only copy of that session, and a full disk or a folder
     * that will not take a write is a reason to try again in a minute, not a
     * reason to throw a game away. */
    {
        char sdir[512];

        snprintf(sdir, sizeof sdir, "%s/save", dir);
        CHECK(mmo_launch_export_open(exe, 1, 4321, &x, err, sizeof err) == 0,
              "a last session names its handoff");
        write_file(x.save, "the session that was refused");
        write_file(x.mark, "character Probe\n");
        CHECK(chmod(sdir, 0500) == 0, "the save folder can be closed to writes");
        CHECK(mmo_launch_export_adopt(&x, "2026-09-08 09:30:00", 7, line,
                                      sizeof line, err, sizeof err) == -1
                  && err[0] != '\0',
              "a folder that will not take a write refuses the adopt, and says"
              " why");
        /* Opened again before settling: a folder that refuses a write refuses
         * a remove too, and the remove is what is being asked about. */
        CHECK(chmod(sdir, 0700) == 0, "and can be opened again");
        CHECK(mmo_launch_export_settle(&x, -1) == 1,
              "settling a refused adopt keeps the handoff rather than dropping"
              " it");
        CHECK(link_line(x.save, 0, line, sizeof line) == 0 &&
              strcmp(line, "the session that was refused") == 0,
              "so the image is still on disk, byte for byte");
        CHECK(access(x.mark, F_OK) == 0,
              "and the marker with it, because an image with no marker is not"
              " an export any more");
        snprintf(path, sizeof path, "%s/save/platinum.sav", dir);
        CHECK(link_line(path, 0, line, sizeof line) == 0 &&
              strcmp(line, "the third export") == 0,
              "and the offline save is the one it was: nothing was replaced");

        /* The same handoff again, now the folder takes writes. */
        CHECK(mmo_launch_export_adopt(&x, "2026-09-08 09:31:00", 7, line,
                                      sizeof line, err, sizeof err) == 1,
              "a second attempt adopts what the first could not");
        CHECK(mmo_launch_export_settle(&x, 1) == 0
                  && access(x.save, F_OK) != 0 && access(x.mark, F_OK) != 0,
              "and settling an adopt that landed takes the pair away");
        CHECK(link_line(path, 0, line, sizeof line) == 0 &&
              strcmp(line, "the session that was refused") == 0,
              "the game that was nearly thrown away is what PLAY OFFLINE picks"
              " up");
    }

    /* And a launch after that one comes back for what was kept. Keeping an
     * image is only worth something if somebody meets it again, and until
     * this the desktop named its own handoff and walked past everyone
     * else's. */
    {
        char sdir[512], older[512], newer[512], mark[512];
        struct utimbuf when;
        int said = 0;

        snprintf(sdir, sizeof sdir, "%s/save", dir);
        /* Named against the clock on purpose. The name carries the process id
         * of the launcher that wrote it, so name order is not time order, and
         * a sweep that trusted either the name or the folder's own order would
         * leave the wrong game standing. */
        snprintf(older, sizeof older, "%s/export-99.sav", sdir);
        snprintf(newer, sizeof newer, "%s/export-11.sav", sdir);
        write_file(older, "the game played first");
        snprintf(mark, sizeof mark, "%s.ok", older);
        write_file(mark, "character Probe\nplay-time 100\n");
        when.actime = when.modtime = 1000000;
        CHECK(utime(mark, &when) == 0, "a handoff nobody adopted can be dated");
        write_file(newer, "the game played second");
        snprintf(mark, sizeof mark, "%s.ok", newer);
        write_file(mark, "character Probe\nplay-time 200\n");
        when.actime = when.modtime = 2000000;
        CHECK(utime(mark, &when) == 0, "and a second one dated after it");

        CHECK(mmo_launch_export_sweep(exe, 7, count_note, &said) == 2,
              "a later launch adopts every handoff the ones before it kept");
        CHECK(said == 0, "and has nothing to say, because none of them refused");
        CHECK(access(older, F_OK) != 0 && access(newer, F_OK) != 0
                  && access(mark, F_OK) != 0,
              "the images and their markers are gone, so the launch after this"
              " one does not adopt them again");
        CHECK(link_line(path, 0, line, sizeof line) == 0 &&
              strcmp(line, "the game played second") == 0,
              "and the newest marker is the one left standing as the offline"
              " save");
        CHECK(mmo_launch_export_sweep(exe, 7, count_note, &said) == 0
                  && said == 0,
              "with nothing left to sweep, a launch adopts nothing and says"
              " nothing");

        /* The whole reason the image was kept: the disk that refused it is
         * not the disk the next launch meets. */
        write_file(older, "the game a full disk refused");
        snprintf(mark, sizeof mark, "%s.ok", older);
        write_file(mark, "character Probe\nplay-time 300\n");
        CHECK(chmod(sdir, 0500) == 0, "the save folder can be closed to writes");
        CHECK(mmo_launch_export_sweep(exe, 7, count_note, &said) == 0
                  && said == 2,
              "a folder that will not take a write adopts nothing, and says"
              " both that it refused and that the game is still there");
        CHECK(chmod(sdir, 0700) == 0, "and can be opened again");
        said = 0;
        CHECK(mmo_launch_export_sweep(exe, 7, count_note, &said) == 1
                  && said == 0,
              "so the next launch lands it, now there is room");
        CHECK(link_line(path, 0, line, sizeof line) == 0 &&
              strcmp(line, "the game a full disk refused") == 0,
              "and the game nobody could put away is the one PLAY OFFLINE"
              " picks up");
    }

    /*
     * And a session in the middle of playing the file an adopt replaces. Two writers of one
     * save that cannot see each other: the offline session writes it as it goes, and the adopt
     * renames over it and keeps what was there.
     */
    {
        mmo_launch_offline o;
        int said = 0;

        snprintf(path, sizeof path, "%s/save/platinum.sav", dir);
        CHECK(mmo_launch_export_open(exe, 1, 77, &x, err, sizeof err) == 0,
              "a session carries a game home");
        write_file(x.save, "the game carried home mid-session");
        write_file(x.mark, "character Probe\nplay-time 400\n");
        CHECK(mmo_launch_offline_open(exe, 1, "2026-09-09 09:00:00", &o,
                                      err, sizeof err) == 0,
              "and another launcher opens the save it would replace");
        {
            /* Through a buffer the size a caller declares, not the roomy one
             * this file uses: half a sentence is worse than none, and the
             * sweep's own `err` is exactly this wide. */
            char tight[MMO_LAUNCH_TEXT];

            CHECK(mmo_launch_export_adopt(&x, "2026-09-09 09:01:00", 7, line,
                                          sizeof line, tight, sizeof tight)
                          == -1
                      && strstr(tight, "another launcher") != NULL
                      && strstr(tight, "at the next launch") != NULL,
                  "the adopt is refused while that session has it, and the"
                  " whole sentence fits the buffer a caller declares");
        }
        CHECK(link_line(path, 0, line, sizeof line) == 0 &&
              strcmp(line, "the game a full disk refused") == 0,
              "so the game being played is still the one at the name");
        CHECK(mmo_launch_export_settle(&x, -1) == 1
                  && access(x.save, F_OK) == 0 && access(x.mark, F_OK) == 0,
              "and the game carried home is kept for the launch after this"
              " one, marker and all");
        CHECK(mmo_launch_export_sweep(exe, 7, count_note, &said) == 0
                  && said == 1,
              "a sweep is refused the same way, and says so once for the pass"
              " rather than once for every handoff in it");
        CHECK(access(x.save, F_OK) == 0 && access(x.mark, F_OK) == 0,
              "with every marker still beside its image");
        mmo_launch_offline_close(&o);
        CHECK(mmo_launch_export_sweep(exe, 7, count_note, &said) == 1,
              "and the sweep lands it the moment that session lets go: the"
              " claim is the pass's, not the launcher's");
        CHECK(link_line(path, 0, line, sizeof line) == 0 &&
              strcmp(line, "the game carried home mid-session") == 0,
              "so the game that waited is the one PLAY OFFLINE picks up");
    }

    /* A sweep of an install that never named a handoff of its own. */
    {
        char fresh[] = "/tmp/openmmo-sweep-XXXXXX";
        char fexe[512], fdir[512], fsave[512], anchor[512];
        char *at = mkdtemp(fresh);
        int said = 0;

        if (at == NULL) {
            CHECK(0, "a second temporary install root could be made");
            return;
        }
        snprintf(fdir, sizeof fdir, "%s/save", at);
        CHECK(mkdir(fdir, 0700) == 0, "an install with a save folder and"
              " nothing else in it");
        snprintf(fexe, sizeof fexe, "%s/bin/pokeplatinum", at);
        snprintf(fsave, sizeof fsave, "%s/export-7.sav", fdir);
        write_file(fsave, "the only copy there is");
        snprintf(anchor, sizeof anchor, "%s.ok", fsave);
        write_file(anchor, "character Probe\nplay-time 500\n");
        CHECK(mmo_launch_export_sweep(fexe, 7, count_note, &said) == 1
                  && said == 0,
              "a sweep with no sessions/ beside it adopts all the same");
        snprintf(path, sizeof path, "%s/" "platinum.sav", fdir);
        CHECK(link_line(path, 0, line, sizeof line) == 0 &&
              strcmp(line, "the only copy there is") == 0,
              "the game is the offline save");
        CHECK(mmo_launch_offline_list(fdir, stamps,
                                      (int)(sizeof stamps / sizeof stamps[0]))
                  == 0,
              "with nothing kept behind it, because there was nothing to keep");
        snprintf(anchor, sizeof anchor, "%s/sessions", fdir);
        CHECK(count_suffix(anchor, ".export") == 1,
              "and its anchor written down beside it: a save with no record of"
              " where it came from reads as a New Game");
    }
}

/*
 * The evidence behind an offline save: which sessions are gathered, in which order, and what
 * the blob the server reads says about them.
 */
/* The offline copy a session sends as it leaves, which the server reads with
 * ExportImageWire.kt. The fourteen bytes below are the whole of what the two
 * ends agree on for a three-byte image; the Kotlin test pins the same ones. */
static void check_export_blob(void)
{
    static const u8 IMAGE[3] = { 0xaa, 0xbb, 0xcc };
    static const u8 WANT[14] = { 4, 'O', 'M', 'E', 'X', 1, 0,
                                 3, 0, 0, 0, 0xaa, 0xbb, 0xcc };
    mmo_wbuf w;

    printf("the offline copy a session sends as it leaves names itself:\n");
    mmo_wbuf_init(&w);
    CHECK(mmo_export_image_encode(&w, IMAGE, sizeof IMAGE) == 0 &&
              w.len == sizeof WANT && memcmp(w.data, WANT, sizeof WANT) == 0,
          "a length-prefixed OMEX, a version, a U32LE length and the image"
          " verbatim");
    mmo_wbuf_free(&w);
    mmo_wbuf_init(&w);
    CHECK(mmo_export_image_encode(&w, IMAGE, 0) != 0 && w.err,
          "an empty image is refused rather than sent as one");
    mmo_wbuf_free(&w);
}

/* The front door's half of a save that went online. The game retires the
 * report under a new name on the server's word; the front door reads that
 * name, not the report's absence, which a report that never read is too. */
/* The report a whole save crosses in, out to a file and back. */
static void check_report_file(void)
{
    char root[] = "/tmp/openmmo-report-XXXXXX";
    char path[600];
    char *dir;
    mmo_import_mon mon;
    mmo_import_report r;
    mmo_wbuf w;
    mmo_wbuf plain;
    u8 *back = NULL;
    size_t n = 0;
    s32 seconds = 0;
    char sha[65];
    int i;

    printf("a save report written and read back:\n");

    dir = mkdtemp(root);
    if (dir == NULL) {
        CHECK(0, "a temporary folder could be made");
        return;
    }
    snprintf(path, sizeof path, "%s/platinum.sav.report", dir);

    memset(&mon, 0, sizeof mon);
    mon.pid = 305419896;
    mon.dex = 387;
    mon.level = 22;
    mon.xp = 10648;
    for (i = 0; i < MMO_IMPORT_STATS; i++) {
        mon.ivs[i] = (u8)(i + 1);
        mon.evs[i] = (u8)(i * 2);
    }
    mon.nmoves = 1;
    mon.moves[0].move = 33;
    mon.moves[0].pp = 30;
    snprintf(mon.nickname, sizeof mon.nickname, "TURTWIG");
    snprintf(mon.ot_name, sizeof mon.ot_name, "PROBE");
    mon.ot_id = 24680;
    mon.friendship = 70;
    for (i = 0; i < MMO_IMPORT_CONDITIONS; i++)
        mon.cond[i] = (u8)((i + 1) * 10);
    mon.sheen = 60;
    mon.met_location = 16;
    mon.ball = 5004;
    mon.pokerus = 0x24;
    mon.markings = 0x05;

    memset(&r, 0, sizeof r);
    snprintf(r.sha256, sizeof r.sha256,
             "a6f3595e6375180db81a5f63664d73b661c1c8310450a1468de72bde4041bce2");
    r.client_revision = 41;
    r.trainer_id = 24680;
    r.play_seconds = 7265;
    r.position.bank = 1;
    r.position.map = 155;
    /* Row 2 of the engine's spawn table, which is a place the position cannot
     * name: the save is standing in Twinleaf Town, which is no row at all. */
    r.black_out_warp = 2;
    r.monsters = &mon;
    r.nmonsters = 1;

    mmo_wbuf_init(&w);
    CHECK(mmo_import_report_encode(&w, &r) == 0 && w.len > 7,
          "a report with a monster in it encodes");
    CHECK(w.len > 5 && w.data[5] == MMO_IMPORT_VERSION && w.data[6] == 0,
          "and says which version of the format it is");
    CHECK(mmo_import_report_write(path, &r) == 0, "and reaches the disk");
    CHECK(mmo_import_report_read(path, &back, &n) == 0 && n == w.len &&
          memcmp(back, w.data, n) == 0,
          "and comes back off it byte for byte");
    CHECK(mmo_import_report_stamp(back, n, &seconds, sha, NULL) == 0 &&
          seconds == 7265 &&
          strcmp(sha, "a6f3595e6375180db81a5f63664d73b661c1c8310450a1468de72"
                      "bde4041bce2") == 0,
          "and its head reads without decoding the rest of it");

    /* The last of the twenty ribbon bits is the one a mask carried in too few
     * bytes loses, and losing it is silent: the monster arrives having won
     * everything except the contest it is proudest of. */
    mon.ribbons_super = (u64)1 << 19;
    mmo_wbuf_init(&plain);
    CHECK(mmo_import_report_encode(&plain, &r) == 0 && plain.len == w.len,
          "a ribbon at the top of the twenty does not change the length");
    {
        size_t differ = 0;

        for (i = 0; (size_t)i < plain.len; i++) {
            if (plain.data[i] != w.data[i])
                differ++;
        }
        CHECK(differ == 1, "and moves exactly one byte of the report");
    }
    mmo_wbuf_free(&plain);
    mmo_wbuf_free(&w);

    /* A report from a build that writes a different version. Every field after
     * the header may have moved, so there is nothing here to read. */
    {
        FILE *f;

        back[5] = (u8)(MMO_IMPORT_VERSION + 1);
        f = fopen(path, "wb");
        if (f != NULL) {
            fwrite(back, 1, n, f);
            fclose(f);
        }
    }
    free(back);
    back = NULL;
    CHECK(mmo_import_report_read(path, &back, &n) != 0 && back == NULL,
          "a report of a version this build does not write is refused");
    remove(path);
    remove(dir);
}

static void check_import_landed(void)
{
    char root[] = "/tmp/openmmo-landed-XXXXXX";
    char report[600];
    char landed[640];
    char *dir;

    printf("a report the server took is known to have landed:\n");

    dir = mkdtemp(root);
    if (dir == NULL) {
        CHECK(0, "a temporary install root could be made");
        return;
    }
    snprintf(report, sizeof report, "%s/platinum.sav.report", dir);
    CHECK(mmo_launch_import_landed("") == 0, "no report offered, none landed");
    CHECK(mmo_launch_import_landed(report) == 0,
          "a report that was never there has not landed");
    write_file(report, "OMIR");
    CHECK(mmo_launch_import_landed(report) == 0,
          "a report still at its name is one the server has not answered for");
    snprintf(landed, sizeof landed, "%s.landed", report);
    CHECK(rename(report, landed) == 0 && mmo_launch_import_landed(report) == 1,
          "and one set aside under .landed has");

    /* The next offline session writes its own report at that name. The answer
     * still sitting beside it is about the save before this one, so a front
     * door that reads it as this one's tells a player whose second offer the
     * server refused that their game is on the server. */
    {
        mmo_import_report r;

        memset(&r, 0, sizeof r);
        r.client_revision = -1;
        r.play_seconds = 4000;
        snprintf(r.sha256, sizeof r.sha256,
                 "1111111111111111111111111111111111111111111111111111111111111111");
        CHECK(mmo_import_report_write(report, &r) == 0,
              "a second offline session writes a report at the same name");
        CHECK(access(landed, F_OK) != 0,
              "which clears the answer the server gave about the save before"
              " it");
        CHECK(mmo_launch_import_landed(report) == 0,
              "so a second offer the server refuses reads as refused");
    }
}

/* Which side is newer, and what the front door does when it cannot tell. */
static void check_import_offer(void)
{
    char root[] = "/tmp/openmmo-offer-XXXXXX";
    char exe[600], path[700];
    mmo_launch_import offer;
    mmo_import_report r;
    char *dir;

    printf("an offline save is offered only when it is the newer of two:\n");

    dir = mkdtemp(root);
    if (dir == NULL) {
        CHECK(0, "a temporary install root could be made");
        return;
    }
    snprintf(path, sizeof path, "%s/save", dir);
    if (mkdir(path, 0700) != 0) {
        CHECK(0, "a temporary save folder could be made");
        return;
    }
    snprintf(path, sizeof path, "%s/save/sessions", dir);
    CHECK(mkdir(path, 0700) == 0, "an install with a save folder");
    snprintf(exe, sizeof exe, "%s/bin/pokeplatinum", dir);

    memset(&r, 0, sizeof r);
    r.client_revision = -1;
    r.play_seconds = 5000;
    snprintf(r.sha256, sizeof r.sha256,
             "3333333333333333333333333333333333333333333333333333333333333333");
    snprintf(path, sizeof path, "%s/save/platinum.sav.report", dir);
    CHECK(mmo_import_report_write(path, &r) == 0,
          "and an offline save that wrote a report of itself");

    CHECK(mmo_launch_import_offer(exe, 1, &offer) == 1 &&
              offer.server_seconds == -1 && offer.unsure[0] == '\0',
          "with no export behind it the save is a New Game played offline, and"
          " it is offered");

    snprintf(path, sizeof path, "%s/save/sessions/20260101-000000.export", dir);
    write_file(path, "version 1\nplay-time 4000\n");
    CHECK(mmo_launch_import_offer(exe, 1, &offer) == 1 &&
              offer.save_seconds == 5000 && offer.server_seconds == 4000,
          "a save played on since the server handed it over is offered, and"
          " both clocks are said");

    snprintf(path, sizeof path, "%s/save/sessions/20260102-000000.export", dir);
    write_file(path, "version 1\nplay-time 6000\n");
    CHECK(mmo_launch_import_offer(exe, 1, &offer) == 0 && offer.unsure[0] == '\0',
          "one the server is already ahead of is not, and that is the ordinary"
          " no rather than a doubt");

    snprintf(path, sizeof path, "%s/save/sessions/20260103-000000.export", dir);
    write_file(path, "version 1\ncharacter Probe\n");
    CHECK(mmo_launch_import_offer(exe, 1, &offer) == 0 &&
              strstr(offer.unsure, "does not say") != NULL,
          "a marker with no play time in it is a character the server HAS,"
          " whose clock this cannot read: nothing is offered and it says so");

    /* A folder at the marker's name: unopenable, or openable and unreadable,
     * whichever the host does, and not a permission a test has to be able to
     * take away. */
    snprintf(path, sizeof path, "%s/save/sessions/20260104-000000.export", dir);
    CHECK(mkdir(path, 0700) == 0, "a marker that cannot be read at all");
    CHECK(mmo_launch_import_offer(exe, 1, &offer) == 0 &&
              strstr(offer.unsure, "could not be") != NULL,
          "reads as a doubt too, never as the New Game an absent one is");
}

static void check_chain(void)
{
    mmo_launch_offline o;
    char root[] = "/tmp/openmmo-chain-XXXXXX";
    char err[192];
    char path[640];
    char blob[512];
    char line[256];
    char first[128];
    u8 *bytes = NULL;
    size_t n = 0;
    char *dir;

    printf("the sessions behind an offline save are gathered for checking:\n");

    dir = mkdtemp(root);
    if (dir == NULL) {
        CHECK(0, "a temporary install root could be made");
        return;
    }
    snprintf(path, sizeof path, "%s/bin", dir);
    if (mkdir(path, 0700) != 0) {
        CHECK(0, "a temporary bin/ could be made");
        return;
    }
    snprintf(path, sizeof path, "%s/bin/pokeplatinum", dir);

    CHECK(mmo_launch_chain_collect(path, 1, blob, sizeof blob, err, sizeof err)
              == 0 && blob[0] == '\0',
          "an install nobody has played offline has nothing to gather");

    /* Two sessions, the second after the first. Each writes its own record,
     * its own recording and the save the game left, which is what the launcher
     * does around a real run, and the save matters here, because what a
     * chain is gathered by is the hashes of those files. */
    CHECK(mmo_launch_offline_open(path, 1, "2026-09-04 09:00:00", &o,
                                  err, sizeof err) == 0 &&
              mmo_launch_offline_begin(&o, 12, err, sizeof err) == 0,
          "a first session is opened and written down");
    write_file(o.record, "0 keys none\n120 keys A\n");
    write_file(o.save, "the game after one session");
    CHECK(mmo_launch_offline_end(&o, err, sizeof err) == 0,
          "and closed");
    CHECK(link_line(o.link, 6, first, sizeof first) == 0 &&
              strncmp(first, "quit-sha256 ", 12) == 0,
          "and it wrote down the hash of the game it left");
    memmove(first, first + 12, strlen(first + 12) + 1);

    CHECK(mmo_launch_offline_open(path, 1, "2026-09-04 11:00:00", &o,
                                  err, sizeof err) == 0 &&
              mmo_launch_offline_begin(&o, 12, err, sizeof err) == 0,
          "a second one after it");
    CHECK(link_line(o.link, 3, line, sizeof line) == 0 &&
              strncmp(line, "boot-sha256 ", 12) == 0 &&
              strcmp(line + 12, first) == 0,
          "which booted from what the first one left, and says so: that line"
          " is the edge the chain is walked along");
    write_file(o.record, "0 keys none\n60 keys UP\n900 keys none\n");
    write_file(o.save, "the game after two sessions");
    CHECK(mmo_launch_offline_end(&o, err, sizeof err) == 0, "and closed too");

    CHECK(mmo_launch_chain_collect(path, 1, blob, sizeof blob, err, sizeof err)
              == 1,
          "both of them are gathered");
    CHECK(mmo_chain_read(blob, &bytes, &n) == 0,
          "and what lands names itself as a chain of session records");
    if (bytes != NULL) {
        /* head is `u8 4` + OMCH + u16 version; then the count and the anchor. */
        CHECK(bytes[7] == 2 && bytes[8] == 0,
              "two sessions, and the count is where the reader looks for it");
        CHECK(bytes[9] == 4 && bytes[10] == 0 &&
                  memcmp(bytes + 11, "none", 4) == 0,
              "with no export behind them the chain starts from no file at"
              " all, which is a New Game");
        /* The first record is the oldest: a chain is walked forward. */
        CHECK(memcmp(bytes + 19, "version 1\nrevision 12\nrtc 2026-09-04"
                     " 09:00:00\n", 45) == 0,
              "the oldest session is first, record and all, verbatim");
        free(bytes);
        bytes = NULL;
    }

    /*
     * An export marker naming the file the second session booted from: the chain now starts
     * from what the server handed over, and the session before it is somebody else's problem.
     */
    snprintf(path, sizeof path, "%s/save/sessions/20260904-100000.export", dir);
    snprintf(line, sizeof line, "version 1\nrevision 12\nrtc 2026-09-04"
             " 10:00:00\nexport-sha256 %s\n", first);
    write_file(path, line);
    snprintf(path, sizeof path, "%s/bin/pokeplatinum", dir);
    CHECK(mmo_launch_chain_collect(path, 1, blob, sizeof blob, err, sizeof err)
              == 1 && mmo_chain_read(blob, &bytes, &n) == 0,
          "the chain is gathered again");
    if (bytes != NULL) {
        CHECK(bytes[7] == 1 && bytes[8] == 0,
              "only the session played after the export is in it");
        CHECK(bytes[9] == 64 && bytes[10] == 0 &&
                  memcmp(bytes + 11, first, 64) == 0,
              "and it starts from the hash the export was written down with");
        free(bytes);
        bytes = NULL;
    }

    /* A recording the record does not name is not swept in by its stamp. */
    snprintf(path, sizeof path, "%s/save/sessions/20260904-110000.inp", dir);
    CHECK(remove(path) == 0, "the second session's recording is taken away");
    snprintf(path, sizeof path, "%s/bin/pokeplatinum", dir);
    CHECK(mmo_launch_chain_collect(path, 1, blob, sizeof blob, err, sizeof err)
              == 1 && mmo_chain_read(blob, &bytes, &n) == 0,
          "the session is still gathered without it");
    if (bytes != NULL) {
        CHECK(bytes[7] == 1,
              "a session whose recording is gone goes up all the same, so the"
              " gap is the server's to see rather than the client's to hide");
        free(bytes);
        bytes = NULL;
    }

    /*
     * The fork, which is the shape a player reaches first: play offline, go back online once
     * and carry that session out too, then bring the offline run up.
     */
    {
        mmo_launch_export x;
        mmo_import_report r;
        char sess[640], mark[640], rept[700], side[700];

        /* The offline run has written a report of itself, which is what the
         * front door offers the server: a game waiting to go online. */
        memset(&r, 0, sizeof r);
        r.client_revision = 12;
        r.play_seconds = 5000;
        snprintf(r.sha256, sizeof r.sha256,
                 "3333333333333333333333333333333333333333333333333333333333333333");
        snprintf(rept, sizeof rept, "%s/save/platinum.sav.report", dir);
        CHECK(mmo_import_report_write(rept, &r) == 0,
              "the offline run has a report of itself beside it, which is the"
              " box on the front door offering to take it up");

        snprintf(path, sizeof path, "%s/bin/pokeplatinum", dir);
        CHECK(mmo_launch_export_open(path, 1, 99, &x, err, sizeof err) == 0,
              "the player goes back online, and the session names a handoff");
        write_file(x.save, "the game that session played");
        write_file(x.mark, "character Probe\nplay-time 4000\n");
        CHECK(mmo_launch_export_adopt(&x, "2026-09-04 15:00:00", 12, line,
                                      sizeof line, err, sizeof err) == 1,
              "and carries it out offline when it ends, which adopts over the"
              " offline run and writes a second export marker");
        snprintf(sess, sizeof sess,
                 "%s/save/sessions/20260904-150000.export", dir);
        CHECK(access(sess, R_OK) == 0, "the newest marker is that one");

        /* The report went with the image it describes, rather than away. */
        snprintf(side, sizeof side,
                 "%s/save/platinum.sav.20260904-150000.report", dir);
        CHECK(access(rept, F_OK) != 0 && access(side, R_OK) == 0,
              "and the waiting game's report is kept beside the copy of it,"
              " not deleted: what Restore save puts back has to be a save the"
              " front door will still offer");
        {
            mmo_launch_import offer;

            CHECK(mmo_launch_import_offer(path, 1, &offer) == 0,
                  "nothing is offered meanwhile, because the live save is the"
                  " one the server just handed over");
        }

        CHECK(mmo_launch_chain_collect(path, 1, blob, sizeof blob, err,
                                       sizeof err) == 0,
              "and with the session's own game at the live name there is"
              " nothing to gather: the two copies are the same file");

        /* Restore save puts the offline run back. */
        snprintf(mark, sizeof mark, "%s/save", dir);
        CHECK(mmo_launch_offline_restore(mark, "20260904-150000", 
                                         "2026-09-04 16:00:00",
                                         err, sizeof err) == 0,
              "the player puts the offline run back from Restore save");
        snprintf(path, sizeof path, "%s/save/platinum.sav", dir);
        CHECK(link_line(path, 0, line, sizeof line) == 0 &&
                  strcmp(line, "the game after two sessions") == 0,
              "so the offline run is the live save again");
        {
            mmo_launch_import offer;

            snprintf(path, sizeof path, "%s/bin/pokeplatinum", dir);
            CHECK(access(rept, R_OK) == 0 &&
                      mmo_launch_import_offer(path, 1, &offer) == 1 &&
                      offer.save_seconds == 5000 &&
                      offer.server_seconds == 4000,
                  "and the box to carry it up is back with it, in one press:"
                  " the report came back with the image, so the player does"
                  " not have to play the restored game before the front door"
                  " will offer it");
        }
        snprintf(path, sizeof path, "%s/bin/pokeplatinum", dir);
        CHECK(mmo_launch_chain_collect(path, 1, blob, sizeof blob, err,
                                       sizeof err) == 1 &&
                  mmo_chain_read(blob, &bytes, &n) == 0,
              "and its sessions are gathered, which is the whole point: a game"
              " carried out in between is not a reason to stop checking a run"
              " whose records reach the file being offered");
        if (bytes != NULL) {
            CHECK(bytes[7] == 1 && bytes[8] == 0,
                  "the session that produced the file is in it");
            CHECK(bytes[9] == 64 && bytes[10] == 0 &&
                      memcmp(bytes + 11, first, 64) == 0,
                  "anchored at the export that run actually booted from,"
                  " which is the OLDER of the two markers in the folder");
            free(bytes);
            bytes = NULL;
        }
    }

    /* And a save the records here do not reach at all. */
    {
        char save[512], mark[512];

        snprintf(save, sizeof save, "%s/save/platinum.sav", dir);
        snprintf(path, sizeof path, "%s/save/platinum.sav.20260904-120000",
                 dir);
        write_file(path, "a game from somewhere else");
        snprintf(mark, sizeof mark, "%s/save", dir);
        CHECK(mmo_launch_offline_restore(mark, "20260904-120000",
                                         "2026-09-04 17:00:00",
                                         err, sizeof err) == 0,
              "an image nothing here ever produced is put back");
        snprintf(mark, sizeof mark, "%s/save/platinum.sav.restore", dir);
        CHECK(link_line(mark, 2, line, sizeof line) == 0 &&
                  strcmp(line, "at 20260904-170000") == 0 &&
                  link_line(mark, 3, line, sizeof line) == 0 &&
                  strcmp(line, "image 20260904-120000") == 0,
              "the restore is still written down, which is what the bundle"
              " carries to another machine");
        snprintf(path, sizeof path, "%s/bin/pokeplatinum", dir);
        CHECK(mmo_launch_chain_collect(path, 1, blob, sizeof blob, err,
                                       sizeof err) == -1 &&
                  strstr(err, "not the play that produced it") != NULL,
              "and the chain is refused, because no session here leaves that"
              " file");
    }
}

/*
 * One call builds the plan the front door plays and the plan --print-plan prints: the handoff
 * a session may write itself out to, the offline save the box on the front door asked to carry
 * up, and the two command lines around whichever of the two there are.
 */
static void check_session_plan(void)
{
    mmo_launch_settings s;
    mmo_launch_plan p;
    mmo_launch_export x;
    char root[] = "/tmp/openmmo-session-XXXXXX";
    char err[192];
    char exe[512];
    char path[560];
    char report[600];
    const char *v;
    char *dir;
    int said;

    printf("a session launch is built one way, for whoever asks:\n");

    dir = mkdtemp(root);
    if (dir == NULL) {
        CHECK(0, "a temporary install root could be made");
        return;
    }
    snprintf(path, sizeof path, "%s/bin", dir);
    if (mkdir(path, 0700) != 0) {
        CHECK(0, "a temporary bin/ could be made");
        return;
    }
    snprintf(exe, sizeof exe, "%s/bin/pokeplatinum", dir);
    good_settings(&s);

    /* Nothing asked for, and nothing to offer: the plan still names somewhere
     * for Continue Offline to write, because every session has one. */
    said = 0;
    CHECK(mmo_launch_plan_build_session(&s, exe, "/b/openmmo-view", "chan-x",
                                        4321, 0, count_note, count_note, &said,
                                        &x, &p, err, sizeof err) == 0,
          "a session plan is built");
    snprintf(path, sizeof path, "%s/save/export-4321.sav", dir);
    CHECK(strcmp(x.save, path) == 0, "with this launcher's handoff named");
    v = env_of(&p, "OPENMMO_EXPORT");
    CHECK(v != NULL && strcmp(v, x.save) == 0,
          "and the game told where to write it");
    CHECK(env_of(&p, "OPENMMO_IMPORT") == NULL,
          "no save is offered to the server unless the player asked");
    CHECK(said == 0, "and a launch with nothing to say says nothing");

    /* Asked for, with no offline save behind it. The refusal is said, because
     * a player who ticked the box is owed the reason it did nothing. */
    said = 0;
    CHECK(mmo_launch_plan_build_session(&s, exe, "/b/openmmo-view", "chan-x",
                                        4321, 1, count_note, count_note, &said,
                                        &x, &p, err, sizeof err) == 0,
          "asking for a save that is not there still plays");
    CHECK(env_of(&p, "OPENMMO_IMPORT") == NULL, "with nothing offered");
    CHECK(said == 1, "and the player is told why");

    /* Asked for, with one. No session has been recorded and no export has ever
     * been adopted, so this is a New Game played offline: offered, with no
     * chain behind it. */
    {
        mmo_import_report r;

        memset(&r, 0, sizeof r);
        r.client_revision = -1;
        r.play_seconds = 900;
        snprintf(r.sha256, sizeof r.sha256,
                 "3333333333333333333333333333333333333333333333333333333333333333");
        snprintf(report, sizeof report, "%s/save/platinum.sav.report", dir);
        CHECK(mmo_import_report_write(report, &r) == 0,
              "an offline session leaves a report beside its save");
    }
    said = 0;
    CHECK(mmo_launch_plan_build_session(&s, exe, "/b/openmmo-view", "chan-x",
                                        4321, 1, count_note, count_note, &said,
                                        &x, &p, err, sizeof err) == 0,
          "and the plan that carries it up is built the same way");
    v = env_of(&p, "OPENMMO_IMPORT");
    CHECK(v != NULL && strcmp(v, report) == 0,
          "the game is told which report to offer");
    CHECK(env_of(&p, "OPENMMO_EXPORT") != NULL,
          "and still has somewhere to write itself out to");
    CHECK(env_of(&p, "OPENMMO_IMPORT_CHAIN") == NULL,
          "a first offline game has no play behind it to send for checking");
    CHECK(said == 1, "and the player is told the save is going up");

    /* A handoff name that will not clear. The session plays on without one,
     * and takes the offer down with it, because both names ride the same plan.
     * A directory with something in it is a name remove() cannot take away. */
    snprintf(path, sizeof path, "%s/save/export-4321.sav", dir);
    if (mkdir(path, 0700) == 0) {
        char stuck[600];

        snprintf(stuck, sizeof stuck, "%s/keep", path);
        write_file(stuck, "a name that will not clear");
        said = 0;
        CHECK(mmo_launch_plan_build_session(&s, exe, "/b/openmmo-view",
                                            "chan-x", 4321, 1, count_note,
                                            count_note, &said, &x, &p,
                                            err, sizeof err) == 0,
              "a session whose handoff will not clear is still played");
        CHECK(env_of(&p, "OPENMMO_EXPORT") == NULL,
              "with nothing named that an old image could be loaded from");
        CHECK(env_of(&p, "OPENMMO_IMPORT") == NULL,
              "and the offer dropped with it rather than sent on its own");
        CHECK(said == 3,
              "both halves said, so neither goes missing without a word");
        remove(stuck);
        remove(path);
    }
}

/* One install, four saved games. */
static void check_slots(void)
{
    mmo_launch_settings s;
    mmo_launch_offline one, two;
    char root[] = "/tmp/openmmo-slots-XXXXXX";
    char out[MMO_LAUNCH_PATH];
    char err[192];
    char path[640];
    char text[4096];
    char *dir;

    printf("an install holds more than one saved game:\n");

    mmo_launch_slot_dir("/i", 1, out, sizeof out);
    CHECK(strcmp(out, "/i/save") == 0,
          "slot 1 is save/, the folder every install already has");
    mmo_launch_slot_dir("/i", 2, out, sizeof out);
    CHECK(strcmp(out, "/i/save/slot2") == 0, "slot 2 is save/slot2");
    mmo_launch_slot_dir("/i", 0, out, sizeof out);
    CHECK(strcmp(out, "/i/save") == 0, "slot 0 is slot 1, not a refusal");
    mmo_launch_slot_dir("/i", MMO_LAUNCH_SLOTS + 1, out, sizeof out);
    CHECK(strcmp(out, "/i/save") == 0,
          "and so is one past the end: a number nobody can play is the game"
          " everybody has, never a front door that will not open");

    mmo_launch_defaults(&s);
    CHECK(s.slot == 1, "the default is the slot that was always there");
    CHECK(mmo_launch_parse("slot 3\n", &s, err, sizeof err) == 0 && s.slot == 3,
          "a config file names one");
    CHECK(mmo_launch_parse("slot 99\n", &s, err, sizeof err) == 0 && s.slot == 1,
          "and one this build does not have is read as slot 1 rather than"
          " stopping the front door");
    s.slot = 3;
    CHECK(mmo_launch_format(&s, text, sizeof text) > 0 &&
              strstr(text, "\nslot 3\n") != NULL,
          "the file remembers it");
    s.slot = 1;
    CHECK(mmo_launch_format(&s, text, sizeof text) > 0 &&
              strstr(text, "slot ") == NULL,
          "and says nothing at all about the one everybody is on");

    dir = mkdtemp(root);
    if (dir == NULL) {
        CHECK(0, "a temporary install root could be made");
        return;
    }
    snprintf(path, sizeof path, "%s/bin", dir);
    if (mkdir(path, 0700) != 0) {
        CHECK(0, "a temporary bin/ could be made");
        return;
    }
    snprintf(path, sizeof path, "%s/bin/pokeplatinum", dir);

    CHECK(mmo_launch_offline_open(path, 1, "2026-09-06 10:00:00", &one,
                                  err, sizeof err) == 0,
          "a session opens on slot 1");
    /* The whole point: the second lock is a different file, so this is not the
     * refusal a second launcher on one slot gets. */
    CHECK(mmo_launch_offline_open(path, 2, "2026-09-06 10:00:01", &two,
                                  err, sizeof err) == 0,
          "and another opens on slot 2 while it is held");
    snprintf(text, sizeof text, "%s/save/slot2", dir);
    CHECK(strcmp(two.dir, text) == 0, "playing its own folder");
    CHECK(strstr(one.save, "/save/platinum.sav") != NULL &&
              strstr(two.save, "/save/slot2/platinum.sav") != NULL,
          "and its own save file, under it");
    mmo_launch_offline_close(&one);
    mmo_launch_offline_close(&two);

    /* A handoff left in slot 2 is picked up by a sweep run from anywhere: it
     * belongs to the slot whose session wrote it, and a front door that only
     * swept the slot it was about to play would leave it there. */
    snprintf(path, sizeof path, "%s/save/slot2/export-9.sav", dir);
    write_file(path, "an image a session carried out");
    snprintf(path, sizeof path, "%s/save/slot2/export-9.sav.ok", dir);
    write_file(path, "version 1\ncharacter Probe\nplay-time 100\n");
    snprintf(path, sizeof path, "%s/bin/pokeplatinum", dir);
    CHECK(mmo_launch_export_sweep(path, 7, NULL, NULL) == 1,
          "a game carried out of a session on slot 2 is adopted by the sweep");
    snprintf(path, sizeof path, "%s/save/slot2/platinum.sav", dir);
    CHECK(access(path, R_OK) == 0, "into slot 2's own saved game");
    snprintf(path, sizeof path, "%s/save/platinum.sav", dir);
    CHECK(access(path, R_OK) != 0, "and slot 1 is left alone");
}

/* A saved game carried to another machine. */
static void check_bundle(void)
{
    mmo_launch_offline o;
    mmo_launch_import offer;
    mmo_launch_bundle_info info;
    mmo_import_report r;
    char here[] = "/tmp/openmmo-carry-a-XXXXXX";
    char there[] = "/tmp/openmmo-carry-b-XXXXXX";
    char err[192];
    char landed[256];
    char path[640];
    char file[700];
    char blob[512];
    char *a, *b;
    char aexe[560], bexe[560];
    FILE *f;
    int n;

    printf("a saved game is carried to another machine whole:\n");

    a = mkdtemp(here);
    b = mkdtemp(there);
    if (a == NULL || b == NULL) {
        CHECK(0, "two temporary install roots could be made");
        return;
    }
    snprintf(path, sizeof path, "%s/bin", a);
    if (mkdir(path, 0700) != 0) {
        CHECK(0, "a temporary bin/ could be made");
        return;
    }
    snprintf(path, sizeof path, "%s/bin", b);
    if (mkdir(path, 0700) != 0) {
        CHECK(0, "a second temporary bin/ could be made");
        return;
    }
    snprintf(aexe, sizeof aexe, "%s/bin/pokeplatinum", a);
    snprintf(bexe, sizeof bexe, "%s/bin/pokeplatinum", b);
    snprintf(file, sizeof file, "%s/carried.omsb", a);

    CHECK(mmo_launch_bundle_write(aexe, 1, file, err, sizeof err) == -1 &&
              strstr(err, "no saved game") != NULL,
          "an empty slot has nothing to carry, and says so");

    /* An install with a game in it: the save, the report the game wrote of it,
     * the anchor the play started from, and one session since. */
    CHECK(mmo_launch_offline_open(aexe, 1, "2026-09-06 09:00:00", &o,
                                  err, sizeof err) == 0 &&
              mmo_launch_offline_begin(&o, 12, err, sizeof err) == 0,
          "a machine with an offline session on it");
    write_file(o.save, "a backup chip, as far as this program is concerned");
    write_file(o.record, "0 keys none\n120 keys A\n");
    CHECK(mmo_launch_offline_end(&o, err, sizeof err) == 0, "and closed");
    snprintf(path, sizeof path, "%s/save/sessions/20260906-080000.export", a);
    write_file(path,
               "version 1\nrevision 12\nrtc 2026-09-06 08:00:00\n"
               "export-sha256 "
               "abababababababababababababababababababababababababababababababab\n"
               "character Probe\nplay-time 4000\n");
    memset(&r, 0, sizeof r);
    r.client_revision = 12;
    r.play_seconds = 5000;
    snprintf(r.sha256, sizeof r.sha256,
             "3333333333333333333333333333333333333333333333333333333333333333");
    snprintf(path, sizeof path, "%s/save/platinum.sav.report", a);
    CHECK(mmo_import_report_write(path, &r) == 0,
          "with a report of what is in the save beside it");

    CHECK(mmo_launch_bundle_write(aexe, 1, file, err, sizeof err) == 0,
          "the whole slot goes out to one file");
    f = fopen(file, "rb");
    n = 0;
    if (f != NULL) {
        n = (int)fread(blob, 1, 16, f);
        fclose(f);
    }
    CHECK(n >= 7 && (unsigned char)blob[0] == 4 &&
              memcmp(blob + 1, MMO_BUNDLE_MAGIC, 4) == 0,
          "which names itself in its first bytes, like every other blob of"
          " ours");

    CHECK(mmo_launch_bundle_look(file, &info, err, sizeof err) == 0 &&
              strcmp(info.from, "Probe") == 0 && info.sessions == 1 &&
              info.has_report && info.has_anchor,
          "and can be read without unpacking: whose game, how much play, and"
          " whether it can still be checked");

    /* The other machine. Nothing of this game has ever been on it. */
    CHECK(mmo_launch_import_offer(bexe, 1, &offer) == 0,
          "the far machine has nothing to offer the server yet");
    CHECK(mmo_launch_bundle_read(bexe, 1, file, landed, sizeof landed,
                                 err, sizeof err) == 0,
          "the file is brought in there");
    snprintf(path, sizeof path, "%s/save/platinum.sav", b);
    n = 0;
    f = fopen(path, "rb");
    if (f != NULL) {
        n = (int)fread(blob, 1, sizeof blob - 1, f);
        blob[n > 0 ? n : 0] = '\0';
        fclose(f);
    }
    CHECK(n > 0 && strncmp(blob, "a backup chip", 13) == 0,
          "and the game itself landed");
    snprintf(path, sizeof path, "%s/save/platinum.sav.report", b);
    CHECK(access(path, R_OK) == 0,
          "with the report beside it, so the save can go online there without"
          " being played first");
    snprintf(path, sizeof path, "%s/save/sessions/20260906-080000.export", b);
    CHECK(access(path, R_OK) == 0, "the anchor it started from");
    snprintf(path, sizeof path, "%s/save/sessions/20260906-090000.link", b);
    CHECK(access(path, R_OK) == 0, "the session played since");
    snprintf(path, sizeof path, "%s/save/sessions/20260906-090000.inp", b);
    CHECK(access(path, R_OK) == 0, "and what the pad did in it");

    /*
     * The sentence this was built for. With no marker the far front door reads a save with no
     * export beside it as a New Game, true on a machine that has never been online, false
     * about a fortnight of play that arrived in a file.
     */
    CHECK(mmo_launch_import_offer(bexe, 1, &offer) == 1 &&
              offer.from_elsewhere == 1 &&
              strcmp(offer.from, "Probe") == 0,
          "the far machine offers the save, and knows it came from another"
          " one");
    CHECK(offer.save_seconds == 5000 && offer.server_seconds == 4000,
          "and compares it against the copy the server handed over, which is"
          " the anchor that travelled with it");
    CHECK(mmo_launch_chain_collect(bexe, 1, blob, sizeof blob,
                                   err, sizeof err) == 1,
          "the play behind it is gathered there exactly as it would have been"
          " at home: a second machine of the same account is not a second"
          " player");

    /* Bringing one in over a game that is already there. The one being
     * replaced is kept, which is what makes a wrong import undoable. */
    {
        char stamps[MMO_LAUNCH_SAVE_KEEP][MMO_LAUNCH_STAMP];
        int kept;

        snprintf(path, sizeof path, "%s/save", b);
        kept = mmo_launch_offline_list(path, stamps, MMO_LAUNCH_SAVE_KEEP);
        CHECK(mmo_launch_bundle_read(bexe, 1, file, landed, sizeof landed,
                                     err, sizeof err) == 0,
              "the same file again");
        CHECK(mmo_launch_offline_list(path, stamps, MMO_LAUNCH_SAVE_KEEP)
                  == kept + 1,
              "keeps the game it replaced, so a wrong file is one restore to"
              " undo");
        /* And says so in the same breath, naming the game that was here.
         * There is no box asking "are you sure": the sentence plus the kept
         * image IS the undo, and a player who picked the wrong file learns it
         * here rather than from a party that is not theirs. */
        CHECK(strstr(landed, "Restore save") != NULL &&
                  strstr(landed, "Probe") != NULL,
              "and tells the player where the game it replaced went");
    }

    /* Anything that is not one of ours. Refused before the save is touched:
     * the read happens before the lock is taken for exactly this. */
    snprintf(path, sizeof path, "%s/notours.omsb", a);
    write_file(path, "this is not a saved game at all");
    CHECK(mmo_launch_bundle_read(bexe, 2, path, landed, sizeof landed,
                                 err, sizeof err) == -1 &&
              strstr(err, "not a saved game") != NULL,
          "a file that is not one of ours is refused by name");
    snprintf(path, sizeof path, "%s/save/slot2/platinum.sav", b);
    CHECK(access(path, R_OK) != 0, "and nothing was written where it would go");
}

/*
 * The wallet a session carries out to the offline game, which is the OFFLINE one and never the
 * online one.
 */
static void check_export_wallet(void)
{
    mmo_launch_settings s;
    mmo_launch_plan p;
    mmo_launch_export x;
    mmo_import_report r;
    char root[] = "/tmp/openmmo-wallet-XXXXXX";
    char err[192];
    char exe[560];
    char path[640];
    const char *v;
    char *dir;

    printf("a session carries the offline game's own wallet, not its own:\n");

    dir = mkdtemp(root);
    if (dir == NULL) {
        CHECK(0, "a temporary install root could be made");
        return;
    }
    snprintf(path, sizeof path, "%s/bin", dir);
    if (mkdir(path, 0700) != 0) {
        CHECK(0, "a temporary bin/ could be made");
        return;
    }
    snprintf(exe, sizeof exe, "%s/bin/pokeplatinum", dir);
    good_settings(&s);

    /* No offline game in this slot at all: the one about to be written is its
     * first, so it opens a wallet where the cartridge opens one. */
    CHECK(mmo_launch_plan_build_session(&s, exe, "/b/openmmo-view", "chan-x",
                                        4321, 0, NULL, NULL, NULL, &x, &p,
                                        err, sizeof err) == 0,
          "a session plan is built for a slot with no offline game");
    v = env_of(&p, "OPENMMO_EXPORT_MONEY");
    CHECK(v != NULL && atoi(v) == MMO_LAUNCH_FRESH_MONEY,
          "and the game is told to open the wallet a new game opens");

    /* A save with a report beside it: the report's wallet is what that game
     * was last playing with, and it is what the image must keep. */
    memset(&r, 0, sizeof r);
    r.client_revision = -1;
    r.play_seconds = 900;
    r.money = 4242;
    snprintf(r.sha256, sizeof r.sha256,
             "3333333333333333333333333333333333333333333333333333333333333333");
    snprintf(path, sizeof path, "%s/save/platinum.sav", dir);
    write_file(path, "the offline game");
    snprintf(path, sizeof path, "%s/save/platinum.sav.report", dir);
    CHECK(mmo_import_report_write(path, &r) == 0,
          "the offline game wrote a report of itself");
    CHECK(mmo_launch_plan_build_session(&s, exe, "/b/openmmo-view", "chan-x",
                                        4321, 0, NULL, NULL, NULL, &x, &p,
                                        err, sizeof err) == 0,
          "and the next session is planned");
    v = env_of(&p, "OPENMMO_EXPORT_MONEY");
    CHECK(v != NULL && atoi(v) == 4242,
          "the offline game keeps the wallet it was playing with");

    /* A save with no report: the state right after an export was adopted and
     * before anybody played it. Its wallet IS the last export's, so nothing is
     * said and the session writes what it has, the same number by another
     * route, and no guess to overwrite an offline wallet with. */
    snprintf(path, sizeof path, "%s/save/platinum.sav.report", dir);
    CHECK(remove(path) == 0, "a save whose report has been taken");
    CHECK(mmo_launch_plan_build_session(&s, exe, "/b/openmmo-view", "chan-x",
                                        4321, 0, NULL, NULL, NULL, &x, &p,
                                        err, sizeof err) == 0,
          "is planned too");
    CHECK(env_of(&p, "OPENMMO_EXPORT_MONEY") == NULL,
          "and nothing is said about the wallet rather than a number invented"
          " for it");
}

int launcher_tests_run(void)
{
    failures = 0;
    check_defaults();
    check_config();
    check_config_file();
    check_refusals();
    check_plan();
    check_print();
    check_paths();
    check_menu_keys();
    check_offline();
    check_export();
    check_import_offer();
    check_chain();
    check_export_blob();
    check_report_file();
    check_import_landed();
    check_session_plan();
    check_slots();
    check_bundle();
    check_export_wallet();
    return failures;
}
