/* See the header. */

#include "mmo_frontdoor_launch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cartridge.h"
#include "mmo_update_notice.h"
#include "platform.h"
#include "soundcompose.h"
#include "followcompose.h"
#include "speciescompose.h"

static char s_root[MMO_LAUNCH_PATH];
static char s_dir[MMO_LAUNCH_PATH + 8];
/* A program path for the plan library, which turns one back into the
 * install root, and OPENMMO_ROOT, set below, is what it reads first. */
static char s_exe[MMO_LAUNCH_PATH + 24];
/* The report the last offer named, so the take can be answered. */
static char s_report[MMO_LAUNCH_PATH];

/* Which of the install's saved games this door plays. */
#define FDL_SLOT 1

void fdl_root(const char *external_dir)
{
    if (external_dir == NULL || external_dir[0] == '\0')
        return;
    snprintf(s_root, sizeof s_root, "%s", external_dir);
    mmo_launch_slot_dir(s_root, FDL_SLOT, s_dir, sizeof s_dir);
    snprintf(s_exe, sizeof s_exe, "%s/bin/pokeplatinum", s_root);
    setenv("OPENMMO_ROOT", s_root, 0);
}

const char *fdl_save_dir(void)
{
    return s_dir;
}

static void note_add(char *note, size_t cap, const char *line)
{
    size_t n = strlen(note);

    if (line == NULL || line[0] == '\0')
        return;
    if (n == 0)
        snprintf(note, cap, "%s", line);
    else if (n + 3 < cap)
        snprintf(note + n, cap - n, "; %s", line);
}

/* Where an offline record left open is remembered between two runs of the
 * app: the game ends the process, so the record is closed by the next front
 * door rather than by the same launcher that opened it. */
static void open_record_path(char *out, size_t cap)
{
    snprintf(out, cap, "%s/session-open.txt", s_dir);
}

static void close_open_record(char *note, size_t cap)
{
    char path[MMO_LAUNCH_PATH + 32], line[MMO_LAUNCH_PATH + 32];
    char err[192];
    mmo_launch_offline o;
    FILE *f;

    open_record_path(path, sizeof path);
    f = fopen(path, "r");
    if (f == NULL)
        return;
    memset(&o, 0, sizeof o);
    while (fgets(line, sizeof line, f) != NULL) {
        char *nl = strchr(line, '\n');
        char *sp = strchr(line, ' ');

        if (nl != NULL)
            *nl = '\0';
        if (sp == NULL)
            continue;
        *sp = '\0';
        if (strcmp(line, "stamp") == 0)
            snprintf(o.stamp, sizeof o.stamp, "%s", sp + 1);
        else if (strcmp(line, "dir") == 0)
            snprintf(o.dir, sizeof o.dir, "%s", sp + 1);
        else if (strcmp(line, "save") == 0)
            snprintf(o.save, sizeof o.save, "%s", sp + 1);
        else if (strcmp(line, "record") == 0)
            snprintf(o.record, sizeof o.record, "%s", sp + 1);
        else if (strcmp(line, "link") == 0)
            snprintf(o.link, sizeof o.link, "%s", sp + 1);
    }
    fclose(f);
    remove(path);
    if (o.link[0] == '\0')
        return;
    if (mmo_launch_offline_end(&o, err, sizeof err) != 0)
        note_add(note, cap, err);
}

/* One note buffer, so the sweep below can add a line to the same string
 * every other housekeeping step writes into. */
typedef struct {
    char *note;
    size_t cap;
} note_sink;

static void note_line(void *ud, const char *line)
{
    note_sink *sink = ud;

    note_add(sink->note, sink->cap, line);
}

/* The handoffs a session wrote on its way out, adopted the way the desktop
 * adopts them: the same sweep, so a game kept here is kept for the same
 * reasons and picked up in the same order. */
static void adopt_exports(char *note, size_t cap)
{
    note_sink sink;

    sink.note = note;
    sink.cap = cap;
    if (mmo_launch_export_sweep(s_exe, mmo_update_notice_revision(),
                                note_line, &sink) > 0)
        /* The wording is launcher.c's, and for the reason it gives at
         * length: the offline row opens this game's own character select
         * now, so there is no new game to warn about. */
        note_add(note, cap, "your game is saved for offline play; PLAY"
                 " OFFLINE opens it from the character select, and any"
                 " earlier save is under RESTORE SAVE");
}

/* A report the game set aside on the server's word. Said once: the desktop
 * says it after the session and this door runs once per launch, so the
 * marker goes once it has been read. */
static void say_landed(char *note, size_t cap)
{
    char report[MMO_LAUNCH_PATH + 40], landed[MMO_LAUNCH_PATH + 56];

    snprintf(report, sizeof report, "%s/platinum.sav.report", s_dir);
    if (!mmo_launch_import_landed(report))
        return;
    note_add(note, cap, "your offline save is on the server now; LOGIN"
             " carries on as that character");
    snprintf(landed, sizeof landed, "%s.landed", report);
    remove(landed);
}

void fdl_housekeep(char *note, size_t cap)
{
    if (note == NULL || cap == 0)
        return;
    note[0] = '\0';
    if (s_root[0] == '\0')
        return;
    close_open_record(note, cap);
    adopt_exports(note, cap);
    say_landed(note, cap);
}

/* What a header code is, for the sentence that refuses it. The registry
 * names every cartridge this client knows, so the phone's refusal and the
 * desktop's are the same words about the same image. */
static const char *code_name(const char *code)
{
    const MmoCartridge *c = mmo_cartridge_slot_of(code);

    return c != NULL ? c->name : "another game";
}

int fdl_slot_header(int slot, const unsigned char *hdr, size_t n,
                    char *why, size_t cap)
{
    char code[5];

    if (hdr == NULL || n < 16) {
        snprintf(why, cap, "That %s file is not a cartridge image",
                 mmo_launch_cart_name(slot));
        return 0;
    }
    memcpy(code, hdr + 12, 4);
    code[4] = '\0';
    if (!mmo_launch_cart_is(slot, code)) {
        snprintf(why, cap, "That is %s, not %s", code_name(code),
                 mmo_launch_cart_name(slot));
        return 0;
    }
    return 1;
}

int fdl_slot_file(int slot, const char *path, char *why, size_t cap)
{
    char code[5];

    if (mmo_launch_rom_code(path, code) == NULL) {
        snprintf(why, cap, "That %s cartridge cannot be read",
                 mmo_launch_cart_name(slot));
        return 0;
    }
    if (!mmo_launch_cart_is(slot, code)) {
        snprintf(why, cap, "That is %s, not %s", code_name(code),
                 mmo_launch_cart_name(slot));
        return 0;
    }
    return 1;
}

int fdl_offer(long *save_seconds, long *server_seconds)
{
    mmo_launch_import o;
    int r;

    if (s_root[0] == '\0')
        return 0;
    r = mmo_launch_import_offer(s_exe, FDL_SLOT, &o);
    if (save_seconds != NULL)
        *save_seconds = o.save_seconds;
    if (server_seconds != NULL)
        *server_seconds = o.server_seconds;
    snprintf(s_report, sizeof s_report, "%s", r ? o.report : "");
    return r;
}

static void push_revision(void)
{
    char text[16];
    int rev = mmo_update_notice_revision();

    if (rev <= 0)
        return;
    snprintf(text, sizeof text, "%d", rev);
    setenv("OPENMMO_REVISION", text, 0);
}

int fdl_prepare_online(int take, char *note, size_t cap)
{
    mmo_launch_export x;
    char err[192];

    if (s_root[0] == '\0')
        return -1;
    /*
     * Overwriting, unlike the door's other knobs: the app names a save file and a session
     * before the door is up (mmo_android_main.c, engine_env), and the press is the player's
     * answer to exactly that question.
     */
    setenv("PC_SAVE", "none", 1);
    setenv("OPENMMO_SESSION", "1", 1);
    push_revision();
    /* A name that will not clear is said and played through without an
     * export, the desktop's own rule: the session is what the player
     * pressed for, and Continue Offline can wait for a launch that could
     * tidy up after the last one. */
    if (mmo_launch_export_open(s_exe, FDL_SLOT, (long)getpid(), &x, err,
                               sizeof err) != 0)
        note_add(note, cap, err);
    else
        setenv("OPENMMO_EXPORT", x.save, 0);
    if (take && s_report[0] != '\0') {
        char chain[MMO_LAUNCH_PATH], cerr[MMO_LAUNCH_TEXT];

        setenv("OPENMMO_IMPORT", s_report, 0);
        /* And the sessions behind it, so the server can check the save
         * against the play that produced it. Never a reason not to offer
         * the save: without them it lands the same way and stays marked. */
        if (mmo_launch_chain_collect(s_exe, FDL_SLOT, chain, sizeof chain,
                                     cerr, sizeof cerr) == 1)
            setenv("OPENMMO_IMPORT_CHAIN", chain, 0);
        else if (cerr[0] != '\0')
            note_add(note, cap, cerr);
    }
    return 0;
}

int fdl_prepare_offline(char *note, size_t cap)
{
    mmo_launch_offline o;
    char err[192], path[MMO_LAUNCH_PATH + 32];
    FILE *f;

    if (s_root[0] == '\0')
        return -1;
    if (mmo_launch_offline_open(s_exe, FDL_SLOT, NULL, &o, err,
                                sizeof err) != 0) {
        note_add(note, cap, err);
        return -1;
    }
    if (mmo_launch_offline_begin(&o, mmo_update_notice_revision(), err,
                                 sizeof err) != 0) {
        note_add(note, cap, err);
        /* The open above claimed the save for a session that is not going to start. */
        mmo_launch_offline_close(&o);
        return -1;
    }
    /* Overwriting, for the reason fdl_prepare_online gives. */
    setenv("PC_SAVE", o.save, 1);
    setenv("PC_RTC", o.rtc, 1);
    setenv("PC_RECORD_INPUT", o.record, 1);
    setenv("OPENMMO_SESSION", "0", 1);
    push_revision();
    /* Remembered for the next door, which closes it (fdl_housekeep). */
    open_record_path(path, sizeof path);
    f = fopen(path, "w");
    if (f != NULL) {
        fprintf(f, "stamp %s\ndir %s\nsave %s\nrecord %s\nlink %s\n",
                o.stamp, o.dir, o.save, o.record, o.link);
        fclose(f);
    }
    return 0;
}

int fdl_saves(char out[][MMO_LAUNCH_STAMP], int max)
{
    if (s_root[0] == '\0')
        return 0;
    return mmo_launch_offline_list(s_dir, out, max);
}

int fdl_restore(const char *stamp, char *note, size_t cap)
{
    char err[192], line[96];

    if (s_root[0] == '\0' || stamp == NULL || stamp[0] == '\0')
        return -1;
    if (mmo_launch_offline_restore(s_dir, stamp, NULL, err, sizeof err) != 0) {
        note_add(note, cap, err);
        return -1;
    }
    snprintf(line, sizeof line, "restored the saved game from %s", stamp);
    note_add(note, cap, line);
    return 0;
}

int fdl_compose(int track, const char *pt, const char *hg, const char *bw,
                void (*note)(void *ud, const char *line), void *ud,
                char *err, size_t errcap)
{
    mmo_launch_settings s;

    if (s_root[0] == '\0') {
        snprintf(err, errcap, "the app has no directory of its own to"
                 " compose into");
        return -1;
    }
    mmo_launch_defaults(&s);
    snprintf(s.rom, sizeof s.rom, "%s", pt != NULL ? pt : "");
    snprintf(s.rom_hg, sizeof s.rom_hg, "%s", hg != NULL ? hg : "");
    snprintf(s.rom_bw, sizeof s.rom_bw, "%s", bw != NULL ? bw : "");
    s.soundtrack = track;
    s.soundfont = MMO_SOUNDFONT_FOLLOW;
    return mmo_soundcompose_ensure(&s, s_exe, note, ud, err, errcap);
}

/*
 * And the POKEMON A black cartridge adds, out of the two the door already made the player
 * choose, into the same mods folder.
 */
int fdl_compose_species(const char *pt, const char *bw, int world,
                        void (*note)(void *ud, const char *line), void *ud,
                        char *err, size_t errcap)
{
    mmo_launch_settings s;

    if (s_root[0] == '\0') {
        snprintf(err, errcap, "the app has no directory of its own to"
                 " fill into");
        return -1;
    }
    mmo_launch_defaults(&s);
    snprintf(s.rom, sizeof s.rom, "%s", pt != NULL ? pt : "");
    snprintf(s.rom_bw, sizeof s.rom_bw, "%s", bw != NULL ? bw : "");
    if (world)
        snprintf(s.mods, sizeof s.mods, "hgss");
    return mmo_speciescompose_ensure(&s, s_exe, note, ud, err, errcap);
}

/*
 * The Pokemon that walks behind the player, out of the same Heart Gold the soundtrack reads,
 * into the same mods folder.
 */
int fdl_compose_followers(const char *hg, int world,
                          void (*note)(void *ud, const char *line), void *ud,
                          char *err, size_t errcap)
{
    mmo_launch_settings s;

    if (s_root[0] == '\0') {
        snprintf(err, errcap, "the app has no directory of its own to"
                 " fill into");
        return -1;
    }
    mmo_launch_defaults(&s);
    snprintf(s.rom_hg, sizeof s.rom_hg, "%s", hg != NULL ? hg : "");
    /* The mods row the app does not have, filled in for the one package that needs it. */
    if (world)
        snprintf(s.mods, sizeof s.mods, "hgss");
    return mmo_followcompose_ensure(&s, s_exe, note, ud, err, errcap);
}
