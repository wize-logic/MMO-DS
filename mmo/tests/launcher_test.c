/* The front door's arithmetic, without a front door. */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "launch_menu.h"
#include "launch_plan.h"

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

/* A settings block that passes mmo_launch_check: the ROM has to be a file that
 * exists, so the suite names one it knows does, its own source. */
static const char *readable_file(void)
{
    static const char *const candidates[] = {
        MMO_REPO_ROOT "/mmo/tests/launcher_test.c",
        MMO_REPO_ROOT "/mmo/Makefile",
        "/dev/null",
    };
    size_t i;

    for (i = 0; i < sizeof candidates / sizeof candidates[0]; i++) {
        if (access(candidates[i], R_OK) == 0)
            return candidates[i];
    }
    return "/dev/null";
}

static void good_settings(mmo_launch_settings *s)
{
    mmo_launch_defaults(s);
    snprintf(s->rom, sizeof s->rom, "%s", readable_file());
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
    CHECK(mmo_launch_parse("hd3d hd\n", &b, err, sizeof err) == 0 &&
          b.hd3d == MMO_LAUNCH_HD3D_MAX,
          "hd is the front door's top step");
    CHECK(mmo_launch_parse("hd3d sd\n", &b, err, sizeof err) == 0 &&
          b.hd3d == MMO_LAUNCH_HD3D_MIN,
          "and sd is the one below it");
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
          b.hd3d == MMO_LAUNCH_HD3D_MAX,
          "and one at 2x comes up to HD");
    CHECK(mmo_launch_parse("hd3d 3\n", &b, err, sizeof err) == 0 &&
          b.hd3d == MMO_LAUNCH_HD3D_MAX,
          "3x was already the top step and stays there");
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
        char tmp[] = "/tmp/openmmo-romdir-XXXXXX";
        char nds[512], file[MMO_LAUNCH_PATH];
        char *dir = mkdtemp(tmp);
        mmo_launch_plan plan;

        if (dir == NULL) {
            printf("  FAIL no temp directory for a ROM folder\n");
            failures++;
        } else {
            snprintf(nds, sizeof nds, "%s/%s", dir, MMO_LAUNCH_ROM_NAME);
            {
                int fd = open(nds, O_WRONLY | O_CREAT | O_TRUNC, 0644);

                if (fd >= 0)
                    close(fd);
            }
            good_settings(&s);
            snprintf(s.rom, sizeof s.rom, "%s", dir);
            CHECK(mmo_launch_rom_file(s.rom, file, sizeof file) == 0 &&
                  strcmp(file, nds) == 0,
                  "a folder resolves to pokeplatinum.us.nds inside it");
            CHECK(mmo_launch_check(&s, err, sizeof err) == 0,
                  "and that folder is enough to launch");
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
            remove(nds);
            CHECK(mmo_launch_check(&s, err, sizeof err) == -1 &&
                  strstr(err, "pokeplatinum.us.nds") != NULL,
                  "an empty folder names the missing cartridge");
            rmdir(dir);
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
    s.hd3d = MMO_LAUNCH_HD3D_MAX;
    CHECK(mmo_launch_plan_build(&s, "/b/pokeplatinum", "/b/openmmo-view",
                                "chan-0", 0, &p, err, sizeof err) == 0,
          "a plan at HD is built");
    v = env_of(&p, "PC_HD3D");
    CHECK(v != NULL && strcmp(v, "3") == 0,
          "PC_HD3D=3 is passed when the panel asks for HD");
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
    CHECK(v != NULL && strcmp(v, s.rom) == 0, "PC_ROM is the chosen ROM");
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
          s.hd3d == MMO_LAUNCH_HD3D_MAX &&
          strcmp(mmo_launch_menu_value(&m, MMO_LAUNCH_R_HD3D, scratch,
                                       sizeof scratch), "HD") == 0,
          "right on 3D DETAIL is HD");
    mmo_launch_menu_key(&m, MMO_LAUNCH_KEY_LEFT);
    CHECK(s.hd3d == MMO_LAUNCH_HD3D_MIN, "left puts it back");

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
          "enter on ROM DIR opens the file manager, not a typed field");

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
    return failures;
}
