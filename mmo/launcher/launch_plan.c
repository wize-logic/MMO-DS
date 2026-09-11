/* Settings, the file they live in, and the two command lines
 * they become. See launch_plan.h for why this is SDL-free. */

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cartridge.h"
#include "feed.h"
#include "launch_plan.h"
#include "../include/offline_chain.h"
#include "../include/offline_import.h"
#include "../include/save_bundle.h"
#include "platform.h"

static const char *const layout_names[MMO_LAYOUT_N] = {
    "smart", "stacked", "wide", "fill"
};
static const char *const filter_names[MMO_FILTER_N] = { "nearest", "linear", "scale2x" };
static const char *const fit_names[MMO_FIT_N]       = { "aspect", "integer", "stretch" };
static const char *const pace_names[MMO_PACE_N]     = { "console", "unlimited" };
/* The two 3D resolutions, in step order from MMO_LAUNCH_HD3D_MIN up. */
static const char *const hd3d_names[MMO_LAUNCH_HD3D_N] = { "sd", "hd", "ultra" };
/* The game's own three, in its own order, spelled the way a config file is. */
static const char *const button_names[MMO_BUTTON_N] = { "normal", "start-is-x", "l-is-a" };
static const char *const soundtrack_names[MMO_SOUNDTRACK_N] =
    { "platinum", "heartgold", "blackwhite" };
/* Package names are sound_<tracks>_<font> in these slugs, one composed
 * archive per pair (`make -C mmo soundtrack`). */
static const char *const sound_slugs[MMO_SOUNDTRACK_N] = { "pt", "hg", "bw" };

const char *mmo_launch_layout_name(int layout)
{
    return (layout >= 0 && layout < MMO_LAYOUT_N) ? layout_names[layout] : NULL;
}

const char *mmo_launch_filter_name(int filter)
{
    return (filter >= 0 && filter < MMO_FILTER_N) ? filter_names[filter] : NULL;
}

const char *mmo_launch_fit_name(int fit)
{
    return (fit >= 0 && fit < MMO_FIT_N) ? fit_names[fit] : NULL;
}

const char *mmo_launch_pace_name(int pace)
{
    return (pace >= 0 && pace < MMO_PACE_N) ? pace_names[pace] : NULL;
}

const char *mmo_launch_button_mode_name(int mode)
{
    return (mode >= 0 && mode < MMO_BUTTON_N) ? button_names[mode] : NULL;
}

const char *mmo_launch_hd3d_name(int hd3d)
{
    return (hd3d >= MMO_LAUNCH_HD3D_MIN && hd3d <= MMO_LAUNCH_HD3D_MAX)
               ? hd3d_names[hd3d - MMO_LAUNCH_HD3D_MIN] : NULL;
}

/* The twelve pad buttons, in the DS's own order. */
static const struct { const char *name, *label, *def; }
BIND_PADS[MMO_LAUNCH_PADS] = {
    { "up",     "D-pad Up",            "Up"        },
    { "down",   "D-pad Down",          "Down"      },
    { "left",   "D-pad Left",          "Left"      },
    { "right",  "D-pad Right",         "Right"     },
    { "a",      "A (confirm)",         "Z"         },
    { "b",      "B (cancel)",          "X"         },
    { "x",      "X (start menu)",      "S"         },
    { "y",      "Y (registered item)", "A"         },
    { "l",      "L",                   "Q"         },
    { "r",      "R",                   "W"         },
    { "start",  "START (chat)",        "Return"    },
    { "select", "SELECT (debug)",      "Backspace" },
};

const char *mmo_launch_pad_name(int pad)
{
    return (pad >= 0 && pad < MMO_LAUNCH_PADS) ? BIND_PADS[pad].name : NULL;
}

const char *mmo_launch_pad_label(int pad)
{
    return (pad >= 0 && pad < MMO_LAUNCH_PADS) ? BIND_PADS[pad].label : NULL;
}

const char *mmo_launch_pad_default(int pad)
{
    return (pad >= 0 && pad < MMO_LAUNCH_PADS) ? BIND_PADS[pad].def : NULL;
}

/*
 * One spec item per call: the [start,end) span of the item at `*at`, with `*at` moved past it.
 * Returns 1 with a span, 0 at the end.
 */
static int bind_item(const char *spec, size_t *at, size_t *from, size_t *to)
{
    size_t i = *at;

    while (spec[i] == ',')
        i++;
    if (spec[i] == '\0')
        return 0;
    *from = i;
    while (spec[i] != '\0' && spec[i] != ',')
        i++;
    *to = i;
    *at = i;
    return 1;
}

/* The pad an item names, and where its key begins; -1 for a shape or a name
 * this table does not hold. */
static int bind_item_pad(const char *spec, size_t from, size_t to, size_t *key)
{
    size_t eq, n;
    int i;

    for (eq = from; eq < to && spec[eq] != '='; eq++)
        ;
    if (eq >= to || eq + 1 >= to)
        return -1;              /* no '=', or an empty key */
    n = eq - from;
    for (i = 0; i < MMO_LAUNCH_PADS; i++) {
        if (strlen(BIND_PADS[i].name) == n &&
            strncasecmp(spec + from, BIND_PADS[i].name, n) == 0) {
            *key = eq + 1;
            return i;
        }
    }
    return -1;
}

int mmo_launch_bind_get(const char *spec, int pad, char *key, size_t cap)
{
    size_t at = 0, from, to, k;

    if (pad < 0 || pad >= MMO_LAUNCH_PADS || key == NULL || cap == 0)
        return -1;
    snprintf(key, cap, "%s", BIND_PADS[pad].def);
    if (spec == NULL)
        return 0;
    /* Every match, not the first: the window applies items in order, so the
     * last one to name a pad is the one that holds. */
    while (bind_item(spec, &at, &from, &to)) {
        if (bind_item_pad(spec, from, to, &k) == pad)
            snprintf(key, cap, "%.*s", (int)(to - k), spec + k);
    }
    return 0;
}

int mmo_launch_bind_set(char *spec, size_t cap, int pad, const char *key)
{
    char out[MMO_LAUNCH_BIND];
    char cur[MMO_LAUNCH_TEXT];
    size_t at = 0;
    int i, n;

    if (spec == NULL || pad < 0 || pad >= MMO_LAUNCH_PADS || key == NULL ||
        key[0] == '\0' || strchr(key, ',') != NULL || strchr(key, '=') != NULL)
        return -1;
    /* Rebuilt whole, one item per pad, defaults dropped: the spec never
     * grows with history and an all-defaults map is the empty string. */
    out[0] = '\0';
    for (i = 0; i < MMO_LAUNCH_PADS; i++) {
        if (i == pad)
            snprintf(cur, sizeof cur, "%s", key);
        else
            mmo_launch_bind_get(spec, i, cur, sizeof cur);
        if (strcasecmp(cur, BIND_PADS[i].def) == 0)
            continue;
        at = strlen(out);
        n = snprintf(out + at, sizeof out - at, "%s%s=%s",
                     out[0] != '\0' ? "," : "", BIND_PADS[i].name, cur);
        if (n < 0 || (size_t)n >= sizeof out - at)
            return -1;
    }
    if (strlen(out) >= cap)
        return -1;
    strcpy(spec, out);
    return 0;
}

int mmo_launch_bind_check(const char *spec)
{
    size_t at = 0, from, to, k;

    if (spec == NULL)
        return 0;
    while (bind_item(spec, &at, &from, &to)) {
        if (bind_item_pad(spec, from, to, &k) < 0)
            return -1;
    }
    return 0;
}

/*
 * A mods-dir row that names a folder which is not there any more must not take Play with it:
 * the r740-era installs wrote their own absolute mods path into the config, and deleting such
 * an install left the row behind as a landmine.
 */
static int dir_exists(const char *path)
{
    struct stat st;

    return path[0] != '\0' && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int mods_list_has(const char *list, const char *want);

/* Which packages this session will load, before the two the launcher adds itself. */
void mmo_launch_mods_list(const mmo_launch_settings *s, const char *modroot,
                          char *out, size_t cap)
{
    /*
     * The content package a player can have on the install and can name nowhere: the game
     * reads it and the settings face has no row for it.
     */
    static const char *const content[] = { "imports", "looks" };
    char path[MMO_LAUNCH_PATH + 32];
    char line[512];
    FILE *f;
    size_t at = 0;
    int i;

    out[0] = '\0';
    if (s->mods[0] != '\0') {
        snprintf(out, cap, "%s", s->mods);
        at = strlen(out);
    } else if (modroot != NULL && modroot[0] != '\0') {
        snprintf(path, sizeof path, "%s%sloadorder.txt", modroot,
                 mmo_plat_sep());
        f = fopen(path, "r");
        if (f != NULL) {
            while (fgets(line, sizeof line, f) != NULL) {
                char *hash = strchr(line, '#');
                char *b, *e;

                if (hash != NULL)
                    *hash = '\0';
                b = line;
                while (*b == ' ' || *b == '\t')
                    b++;
                e = b + strlen(b);
                while (e > b && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '
                                 || e[-1] == '\t'))
                    e--;
                *e = '\0';
                if (*b == '\0')
                    continue;
                if (at + strlen(b) + 2 > cap)
                    break;
                if (at > 0)
                    out[at++] = ',';
                memcpy(out + at, b, strlen(b));
                at += strlen(b);
                out[at] = '\0';
            }
            fclose(f);
        }
    }

    /* And what is on the install but in neither of those. */
    if (modroot != NULL && modroot[0] != '\0') {
        for (i = 0; i < (int)(sizeof content / sizeof content[0]); i++) {
            char toml[MMO_LAUNCH_PATH + 64];
            FILE *probe;

            if (mods_list_has(out, content[i]))
                continue;
            snprintf(toml, sizeof toml, "%s%s%s%smod.toml", modroot,
                     mmo_plat_sep(), content[i], mmo_plat_sep());
            probe = fopen(toml, "rb");
            if (probe == NULL)
                continue;
            fclose(probe);
            if (at + strlen(content[i]) + 2 > cap)
                break;
            if (at > 0)
                out[at++] = ',';
            memcpy(out + at, content[i], strlen(content[i]));
            at += strlen(content[i]);
            out[at] = '\0';
        }
    }
}

/* Whether a comma-separated mods list already names this package. */
static int mods_list_has(const char *list, const char *want)
{
    size_t n = strlen(want);
    const char *at = list;

    while (*at != '\0') {
        const char *end = strchr(at, ',');
        size_t len = end != NULL ? (size_t)(end - at) : strlen(at);

        if (len == n && strncmp(at, want, n) == 0)
            return 1;
        if (end == NULL)
            break;
        at = end + 1;
    }
    return 0;
}

static int name_index(const char *const *names, int n, const char *v)
{
    int i;

    for (i = 0; i < n; i++) {
        if (strcmp(names[i], v) == 0)
            return i;
    }
    return -1;
}

void mmo_launch_defaults(mmo_launch_settings *s)
{
    memset(s, 0, sizeof *s);
    /*
     * The window's own defaults, so a config file that says nothing and no config file at all
     * start the same game.
     */
    s->scale        = MMO_LAUNCH_SCALE_AUTO;
    snprintf(s->viewport, sizeof s->viewport, "auto");
    /* Fixed, not chosen: see the constants in launch_plan.h. */
    s->camera       = MMO_LAUNCH_CAMERA;
    s->render_scale = 2;
    s->hd3d         = MMO_LAUNCH_HD3D_MIN;
    s->layout       = MMO_LAYOUT_FILL;
    s->filter       = MMO_FILTER_LINEAR;
    s->fit          = MMO_FIT_ASPECT;
    s->audio        = 1;
    s->discord      = 1;
    s->music        = 100;
    s->sfx          = 100;
    s->slot         = 1;
}

int mmo_launch_config_path(char *out, size_t cap)
{
    char base[MMO_LAUNCH_PATH];
    const char *sep = mmo_plat_sep();
    int n;

    if (mmo_plat_config_home(base, sizeof base) != 0)
        return -1;
    n = snprintf(out, cap, "%s%sopenmmo%slauncher.cfg", base, sep, sep);
    return (n < 0 || (size_t)n >= cap) ? -1 : n;
}

/* ------------------------------------------------------------------ */
/* The config file                                                     */
/* ------------------------------------------------------------------ */

static void fail(char *err, size_t cap, int line, const char *what,
                 const char *detail)
{
    if (err == NULL || cap == 0)
        return;
    if (line > 0)
        snprintf(err, cap, "line %d: %s%s%s", line, what,
                 detail != NULL ? " " : "", detail != NULL ? detail : "");
    else
        snprintf(err, cap, "%s%s%s", what, detail != NULL ? " " : "",
                 detail != NULL ? detail : "");
}

static int copy_field(char *dst, size_t cap, const char *v, int line,
                      const char *key, char *err, size_t errcap)
{
    if (strlen(v) >= cap) {
        fail(err, errcap, line, key, "is too long");
        return -1;
    }
    memcpy(dst, v, strlen(v) + 1);
    return 0;
}

/*
 * A number that is not one is still fatal, because a typo read as a default is the failure
 * this format exists to avoid.
 */
static int int_field(int *dst, const char *v, int lo, int hi, int line,
                     const char *key, char *err, size_t errcap)
{
    char *end;
    long n = strtol(v, &end, 10);

    if (end == v || *end != '\0') {
        fail(err, errcap, line, key, "wants a number");
        return -1;
    }
    if (n < lo)
        n = lo;
    if (n > hi)
        n = hi;
    *dst = (int)n;
    return 0;
}

static int viewport_ok(const char *v)
{
    char *end;
    long n;

    if (v == NULL || v[0] == '\0') return 1;
    if (strcmp(v, "auto") == 0 || strcmp(v, "native") == 0 ||
        strcmp(v, "16:9") == 0 || strcmp(v, "16:10") == 0 ||
        strcmp(v, "4:3") == 0)
        return 1;
    n = strtol(v, &end, 10);
    return end != v && *end == '\0' && n >= 1 && n <= 4096;
}

static int enum_field(int *dst, const char *const *names, int n, const char *v,
                      int line, const char *key, char *err, size_t errcap)
{
    int i = name_index(names, n, v);

    if (i < 0) {
        fail(err, errcap, line, key, "does not name one of the choices");
        return -1;
    }
    *dst = i;
    return 0;
}

/* The resolution row, which is a name now and used to be a number. */
static int hd3d_field(int *dst, const char *v, int line, const char *key,
                      char *err, size_t errcap)
{
    char *end;
    long n;
    int i = name_index(hd3d_names, MMO_LAUNCH_HD3D_N, v);

    if (i >= 0) {
        *dst = MMO_LAUNCH_HD3D_MIN + i;
        return 0;
    }
    n = strtol(v, &end, 10);
    if (end == v || *end != '\0') {
        fail(err, errcap, line, key, "wants sd, hd or ultra");
        return -1;
    }
    /* The old numbers, migrated once: the console's own 1x comes up to sd,
     * 2x and 3x are HD, and anything above is the top step. */
    *dst = n <= 1 ? MMO_LAUNCH_HD3D_MIN
         : (n <= 3 ? MMO_LAUNCH_HD3D_MIN + 1 : MMO_LAUNCH_HD3D_MAX);
    return 0;
}

int mmo_launch_parse(const char *text, mmo_launch_settings *s,
                     char *err, size_t errcap)
{
    const char *p = text;
    int line = 0;

    while (*p != '\0') {
        char buf[MMO_LAUNCH_PATH * 2];
        char key[64];
        const char *nl = strchr(p, '\n');
        size_t len = (nl != NULL) ? (size_t)(nl - p) : strlen(p);
        const char *v;
        size_t klen;
        int rc = 0;

        line++;
        if (len >= sizeof buf) {
            fail(err, errcap, line, "is longer than this file allows", NULL);
            return -1;
        }
        memcpy(buf, p, len);
        buf[len] = '\0';
        p += len + (nl != NULL ? 1 : 0);

        /* Trailing whitespace only: a value may hold spaces (a path does), so
         * the split is at the first space and nothing else is trimmed. */
        while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '\t' ||
                           buf[len - 1] == '\r'))
            buf[--len] = '\0';
        if (buf[0] == '\0' || buf[0] == '#')
            continue;

        klen = strcspn(buf, " \t");
        if (klen >= sizeof key) {
            fail(err, errcap, line, "has an implausibly long key", NULL);
            return -1;
        }
        memcpy(key, buf, klen);
        key[klen] = '\0';
        v = buf + klen;
        while (*v == ' ' || *v == '\t')
            v++;

        /* Retired, and read as nothing rather than refused. */
        if (strcmp(key, "server") == 0)
            rc = 0;
        else if (strcmp(key, "gameport") == 0)
            rc = 0;
        else if (strcmp(key, "session") == 0)
            rc = 0;
        else if (strcmp(key, "save") == 0)
            rc = 0;
        else if (strcmp(key, "character") == 0)
            rc = 0;
        else if (strcmp(key, "world") == 0)
            rc = 0;
        else if (strcmp(key, "bind") == 0) {
            rc = copy_field(s->bind, sizeof s->bind, v, line, key, err,
                            errcap);
            if (rc == 0 && mmo_launch_bind_check(s->bind) != 0) {
                fail(err, errcap, line, key,
                     "wants PAD=KEY[,PAD=KEY...]; PAD is a, b, x, y, l, r, "
                     "start, select, up, down, left or right");
                rc = -1;
            }
        }
        else if (strcmp(key, "user") == 0)
            rc = copy_field(s->user, sizeof s->user, v, line, key, err, errcap);
        /*
         * Retired, and dropped rather than read. This file used to hold the account password
         * as plain text, which put it in every backup that covers the profile and in front of
         * anyone who reads the folder.
         */
        else if (strcmp(key, "pass") == 0)
            rc = 0;
        else if (strcmp(key, "rom") == 0)
            rc = copy_field(s->rom, sizeof s->rom, v, line, key, err, errcap);
        /* Read and clamped rather than checked. */
        else if (strcmp(key, "slot") == 0) {
            long n = strtol(v, NULL, 10);

            s->slot = (n >= 1 && n <= MMO_LAUNCH_SLOTS) ? (int)n : 1;
            rc = 0;
        }
        else if (strcmp(key, "feed") == 0)
            rc = copy_field(s->feed, sizeof s->feed, v, line, key, err, errcap);
        else if (strcmp(key, "feed-key") == 0)
            rc = copy_field(s->feed_key, sizeof s->feed_key, v, line, key,
                            err, errcap);
        else if (strcmp(key, "feed-url") == 0)
            rc = copy_field(s->feed_url, sizeof s->feed_url, v, line, key,
                            err, errcap);
        else if (strcmp(key, "feed-ca") == 0)
            rc = copy_field(s->feed_ca, sizeof s->feed_ca, v, line, key,
                            err, errcap);
        else if (strcmp(key, "audio-device") == 0)
            rc = copy_field(s->audio_device, sizeof s->audio_device, v, line,
                            key, err, errcap);
        else if (strcmp(key, "scale") == 0) {
            if (strcmp(v, "auto") == 0)
                s->scale = MMO_LAUNCH_SCALE_AUTO;
            else
                rc = int_field(&s->scale, v, 1, MMO_LAUNCH_SCALE_MAX, line, key,
                               err, errcap);
        } else if (strcmp(key, "viewport") == 0) {
            rc = copy_field(s->viewport, sizeof s->viewport, v, line, key,
                            err, errcap);
            if (rc == 0 && !viewport_ok(s->viewport)) {
                fail(err, errcap, line, key,
                     "wants auto, native, 16:9, 16:10, 4:3 or a width");
                rc = -1;
            }
        }
        /* Read and ignored: the distance is not a setting any more, and a
         * config written while it was one still has to start the front door.
         * The next save writes the file without the row. */
        else if (strcmp(key, "camera-distance") == 0)
            rc = 0;
        else if (strcmp(key, "render-scale") == 0)
            rc = int_field(&s->render_scale, v, 1, MMO_LAUNCH_RS_MAX, line, key, err, errcap);
        else if (strcmp(key, "hd3d") == 0)
            rc = hd3d_field(&s->hd3d, v, line, key, err, errcap);
        else if (strcmp(key, "fullscreen") == 0)
            rc = int_field(&s->fullscreen, v, 0, 1, line, key, err, errcap);
        else if (strcmp(key, "audio") == 0)
            rc = int_field(&s->audio, v, 0, 1, line, key, err, errcap);
        else if (strcmp(key, "discord") == 0)
            rc = int_field(&s->discord, v, 0, 1, line, key, err, errcap);
        else if (strcmp(key, "music") == 0)
            rc = int_field(&s->music, v, 0, 100, line, key, err, errcap);
        else if (strcmp(key, "sfx") == 0)
            rc = int_field(&s->sfx, v, 0, 100, line, key, err, errcap);
        /*
         * Read and ignored: the mixer's output is not a setting any more (launch_plan.h), and
         * a config written while it was one still has to start the front door. The next save
         * writes the file without the row.
         */
        else if (strcmp(key, "sound") == 0)
            rc = 0;
        else if (strcmp(key, "layout") == 0)
            rc = enum_field(&s->layout, layout_names, MMO_LAYOUT_N, v, line, key, err, errcap);
        else if (strcmp(key, "filter") == 0)
            rc = enum_field(&s->filter, filter_names, MMO_FILTER_N, v, line, key, err, errcap);
        else if (strcmp(key, "fit") == 0)
            rc = enum_field(&s->fit, fit_names, MMO_FIT_N, v, line, key, err, errcap);
        else if (strcmp(key, "pace") == 0)
            rc = enum_field(&s->pace, pace_names, MMO_PACE_N, v, line, key, err, errcap);
        else if (strcmp(key, "button-mode") == 0)
            rc = enum_field(&s->button_mode, button_names, MMO_BUTTON_N, v, line,
                            key, err, errcap);
        else if (strcmp(key, "soundtrack") == 0)
            rc = enum_field(&s->soundtrack, soundtrack_names, MMO_SOUNDTRACK_N,
                            v, line, key, err, errcap);
        /*
         * Read and ignored while the font pass is unfinished: pinning a font away from its own
         * soundtrack does not play correctly yet, so the setting is held at follow and the row
         * is accepted only so that a config written while the choice existed still starts the
         * front door.
         */
        else if (strcmp(key, "soundfont") == 0)
            rc = 0;
        else if (strcmp(key, "rom-hg") == 0)
            rc = copy_field(s->rom_hg, sizeof s->rom_hg, v, line, key,
                            err, errcap);
        else if (strcmp(key, "rom-bw") == 0)
            rc = copy_field(s->rom_bw, sizeof s->rom_bw, v, line, key,
                            err, errcap);
        else if (strcmp(key, "mods") == 0)
            rc = copy_field(s->mods, sizeof s->mods, v, line, key, err, errcap);
        else if (strcmp(key, "mods-dir") == 0)
            rc = copy_field(s->mods_dir, sizeof s->mods_dir, v, line, key,
                            err, errcap);
        else if (strcmp(key, "theme") == 0)
            rc = copy_field(s->theme, sizeof s->theme, v, line, key, err,
                            errcap);
        else {
            fail(err, errcap, line, "unknown setting", key);
            return -1;
        }
        if (rc != 0)
            return -1;
    }
    return 0;
}

/* Append `fmt` to a bounded buffer, tracking overflow in one place. */
static int addf(char *out, size_t cap, int *at, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (*at < 0)
        return -1;
    va_start(ap, fmt);
    n = vsnprintf(out + *at, cap - (size_t)*at, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap - (size_t)*at) {
        *at = -1;
        return -1;
    }
    *at += n;
    return 0;
}

int mmo_launch_format(const mmo_launch_settings *s, char *out, size_t cap)
{
    int at = 0;

    if (cap == 0)
        return -1;
    out[0] = '\0';
    addf(out, cap, &at, "# openmmo launcher settings; delete this file for defaults\n");
    if (s->user[0] != '\0')         addf(out, cap, &at, "user %s\n", s->user);
    /* The password is deliberately not here. It lives in this process for as
     * long as the session does and never on disk; the server already issues a
     * remember me token for the job this row was doing badly. */
    if (s->rom[0] != '\0')          addf(out, cap, &at, "rom %s\n", s->rom);
    if (s->slot > 1)                addf(out, cap, &at, "slot %d\n", s->slot);
    if (s->audio_device[0] != '\0') addf(out, cap, &at, "audio-device %s\n", s->audio_device);
    if (s->feed[0] != '\0')         addf(out, cap, &at, "feed %s\n", s->feed);
    if (s->feed_key[0] != '\0')     addf(out, cap, &at, "feed-key %s\n", s->feed_key);
    if (s->feed_url[0] != '\0')     addf(out, cap, &at, "feed-url %s\n", s->feed_url);
    if (s->feed_ca[0] != '\0')      addf(out, cap, &at, "feed-ca %s\n", s->feed_ca);
    if (s->bind[0] != '\0')         addf(out, cap, &at, "bind %s\n", s->bind);
    if (s->scale == MMO_LAUNCH_SCALE_AUTO)
        addf(out, cap, &at, "scale auto\n");
    else
        addf(out, cap, &at, "scale %d\n", s->scale);
    addf(out, cap, &at, "viewport %s\n",
         s->viewport[0] != '\0' ? s->viewport : "auto");
    addf(out, cap, &at, "render-scale %d\n", s->render_scale);
    addf(out, cap, &at, "hd3d %s\n", hd3d_names[s->hd3d - MMO_LAUNCH_HD3D_MIN]);
    addf(out, cap, &at, "layout %s\n", layout_names[s->layout]);
    addf(out, cap, &at, "filter %s\n", filter_names[s->filter]);
    addf(out, cap, &at, "fit %s\n", fit_names[s->fit]);
    addf(out, cap, &at, "pace %s\n", pace_names[s->pace]);
    addf(out, cap, &at, "button-mode %s\n", button_names[s->button_mode]);
    addf(out, cap, &at, "fullscreen %d\n", s->fullscreen);
    addf(out, cap, &at, "audio %d\n", s->audio);
    addf(out, cap, &at, "discord %d\n", s->discord);
    addf(out, cap, &at, "music %d\n", s->music);
    addf(out, cap, &at, "sfx %d\n", s->sfx);
    addf(out, cap, &at, "soundtrack %s\n", soundtrack_names[s->soundtrack]);
    if (s->rom_hg[0] != '\0')      addf(out, cap, &at, "rom-hg %s\n", s->rom_hg);
    if (s->rom_bw[0] != '\0')      addf(out, cap, &at, "rom-bw %s\n", s->rom_bw);
    if (s->mods[0] != '\0')         addf(out, cap, &at, "mods %s\n", s->mods);
    if (s->mods_dir[0] != '\0')     addf(out, cap, &at, "mods-dir %s\n", s->mods_dir);
    if (s->theme[0] != '\0')        addf(out, cap, &at, "theme %s\n", s->theme);
    return at;
}

/* Whether a `pass` row is still in the file. Line-anchored, so a password or a
 * path that merely contains the word is not mistaken for one. */
static int cfg_holds_secret(const char *text)
{
    const char *p = text;

    while (*p != '\0') {
        while (*p == ' ' || *p == '\t')
            p++;
        if (strncmp(p, "pass", 4) == 0 &&
            (p[4] == ' ' || p[4] == '\t' || p[4] == '\n' || p[4] == '\0'))
            return 1;
        while (*p != '\0' && *p != '\n')
            p++;
        if (*p == '\n')
            p++;
    }
    return 0;
}

int mmo_launch_load(const char *path, mmo_launch_settings *s,
                    char *err, size_t errcap)
{
    static char text[16384];
    FILE *f = fopen(path, "r");
    size_t n;
    int rc;

    if (f == NULL)
        return 1;
    n = fread(text, 1, sizeof text - 1, f);
    if (!feof(f)) {
        fclose(f);
        fail(err, errcap, 0, "the settings file is implausibly large", NULL);
        return -1;
    }
    fclose(f);
    text[n] = '\0';
    rc = mmo_launch_parse(text, s, err, errcap);
    /* Say so when the file still holds the password row, so the caller can
     * write it back out without one. Left to the caller because this function
     * is also how the tests and --print-plan read a file they must not touch. */
    if (rc == 0 && cfg_holds_secret(text))
        return 2;
    return rc;
}

/* Create every missing parent of `path` (the file). The first save has to
 * make $XDG_CONFIG_HOME/openmmo; without that the ROM a player just set is
 * gone when the window closes. */
static int mkdir_parents(const char *path)
{
    char dir[MMO_LAUNCH_PATH];
    size_t i, n;

    if (path == NULL || path[0] == '\0')
        return -1;
    n = strlen(path);
    if (n >= sizeof dir)
        return -1;
    memcpy(dir, path, n + 1);
    for (i = 1; i < n; i++) {
        /* Both separators, because a Windows path can hold either and a
         * component this loop skips is a directory nobody makes. */
        if (dir[i] != '/' && dir[i] != '\\')
            continue;
        {
            char was = dir[i];

            dir[i] = '\0';
            if (mmo_plat_mkdir(dir) != 0) {
                dir[i] = was;
                return -1;
            }
            dir[i] = was;
        }
    }
    return 0;
}

/* Written to a sibling temp file and renamed, so a launcher killed mid-save
 * leaves the previous settings rather than half of the new ones. */
int mmo_launch_save(const char *path, const mmo_launch_settings *s)
{
    static char text[16384];
    char tmp[MMO_LAUNCH_PATH + 8];
    FILE *f;
    int n = mmo_launch_format(s, text, sizeof text);

    if (n < 0)
        return -1;
    if ((size_t)snprintf(tmp, sizeof tmp, "%s.new", path) >= sizeof tmp)
        return -1;
    if (mkdir_parents(path) != 0)
        return -1;
    f = fopen(tmp, "w");
    if (f == NULL)
        return -1;
    /* Down to the disk before the rename, not just out of this program: the
     * rename orders the NAME against the old file, never the bytes. */
    if (fwrite(text, 1, (size_t)n, f) != (size_t)n || mmo_plat_fsync(f) != 0) {
        fclose(f);
        remove(tmp);
        return -1;
    }
    if (fclose(f) != 0) {
        remove(tmp);
        return -1;
    }
    /*
     * Not rename(): the settings file is written every time the player closes the front door
     * or presses Play, and on Windows rename() refuses once the destination exists.
     */
    if (mmo_plat_rename_over(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Whether it can start                                                */
/* ------------------------------------------------------------------ */

int mmo_launch_rom_file(const char *path, char *out, size_t cap)
{
    struct stat st;
    int n;

    if (path == NULL || path[0] == '\0' || out == NULL || cap == 0)
        return -1;
    if (stat(path, &st) != 0)
        return -1;
    if (S_ISDIR(st.st_mode)) {
        n = snprintf(out, cap, "%s/%s", path, MMO_LAUNCH_ROM_NAME);
        if (n < 0 || (size_t)n >= cap)
            return -1;
        if (access(out, R_OK) == 0)
            return 0;
        /* Under any other name: a backup is called whatever the dumper
         * called it, and the header says which game it is. */
        return mmo_launch_cart_scan(path, MMO_LAUNCH_CART_PLATINUM, out, cap)
               ? 0 : -1;
    }
    if (!S_ISREG(st.st_mode) || strlen(path) >= cap)
        return -1;
    memcpy(out, path, strlen(path) + 1);
    return access(out, R_OK) == 0 ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* The three cartridges                                                */
/* ------------------------------------------------------------------ */

static char s_roms_fallback[MMO_LAUNCH_PATH];

void mmo_launch_roms_fallback(const char *dir)
{
    snprintf(s_roms_fallback, sizeof s_roms_fallback, "%s",
             dir != NULL ? dir : "");
}

const char *mmo_launch_roms_fallback_dir(void)
{
    return s_roms_fallback;
}

const char *mmo_launch_cart_name(int slot)
{
    switch (slot) {
    case MMO_LAUNCH_CART_PLATINUM:  return "Platinum";
    case MMO_LAUNCH_CART_HEARTGOLD: return "Heart Gold";
    case MMO_LAUNCH_CART_BLACK:     return "Black";
    default:                        return "?";
    }
}

const char *mmo_launch_rom_code(const char *path, char *code)
{
    FILE *f;

    code[0] = '\0';
    if (path == NULL || path[0] == '\0')
        return NULL;
    f = fopen(path, "rb");
    if (f == NULL)
        return NULL;
    if (fseek(f, 0x0C, SEEK_SET) != 0 || fread(code, 1, 4, f) != 4) {
        fclose(f);
        code[0] = '\0';
        return NULL;
    }
    fclose(f);
    code[4] = '\0';
    return code;
}

/*
 * The registry slots each front-door slot takes. mmo/CARTRIDGES is the only list of cartridges
 * this client knows, and the front door reads it instead of carrying a second copy of the
 * header codes, so a build added there is a build the door accepts the same day.
 */
static const char *const CART_SLOTS[3][2] = {
    { "platinum",  NULL },
    { "heartgold", "soulsilver" },
    { "black",     "white" }
};

int mmo_launch_cart_is(int slot, const char *code)
{
    const MmoCartridge *c;
    int i;

    if (code == NULL || strlen(code) < 4)
        return 0;
    if (slot < MMO_LAUNCH_CART_PLATINUM || slot > MMO_LAUNCH_CART_BLACK)
        return 0;
    if (slot == MMO_LAUNCH_CART_PLATINUM) {
        /* The image the game runs on is one build and no other, because the
         * engine is that build's own decompilation. The registry calls it the
         * host, and there is exactly one. */
        c = mmo_cartridge_by_code(code);
        return c != NULL && c->status == MMO_CART_HOST;
    }
    /* The other two are asked for as proof the player owns the game; nothing
     * is read out of them at the front door, so a build the registry has not
     * opened still counts as owning it. Hence the slot, not the exact row:
     * the fourth character is the language and the first three are the game. */
    c = mmo_cartridge_slot_of(code);
    if (c == NULL)
        return 0;
    for (i = 0; i < 2; i++)
        if (CART_SLOTS[slot][i] != NULL &&
            strcmp(c->slot, CART_SLOTS[slot][i]) == 0)
            return 1;
    return 0;
}

int mmo_launch_cart_scan(const char *dir, int slot, char *out, size_t cap)
{
    DIR *d;
    struct dirent *ent;
    int hit = 0;

    if (dir == NULL || dir[0] == '\0')
        return 0;
    d = opendir(dir);
    if (d == NULL)
        return 0;
    while (!hit && (ent = readdir(d)) != NULL) {
        size_t n = strlen(ent->d_name);
        char path[MMO_LAUNCH_PATH];
        char code[5];

        if (n < 5 || strncasecmp(ent->d_name + n - 4, ".nds", 4) != 0)
            continue;
        if (snprintf(path, sizeof path, "%s%s%s", dir, mmo_plat_sep(),
                     ent->d_name) >= (int)sizeof path)
            continue;
        if (mmo_launch_rom_code(path, code) != NULL
            && mmo_launch_cart_is(slot, code)) {
            snprintf(out, cap, "%s", path);
            hit = 1;
        }
    }
    closedir(d);
    return hit;
}

/* What a header code is, by the registry's own name, for a refusal. */
static const char *cart_name_of(const char *code)
{
    const MmoCartridge *c = mmo_cartridge_by_code(code);

    if (c == NULL)
        c = mmo_cartridge_slot_of(code);
    return c != NULL ? c->name : code;
}

int mmo_launch_roms_resolve(const mmo_launch_settings *s, mmo_launch_roms *out,
                            char *err, size_t errcap)
{
    char code[5];
    char dir[MMO_LAUNCH_PATH];
    char what[MMO_LAUNCH_PATH + 96];
    const char *sep;
    struct stat st;
    int missing[3] = { 0, 0, 0 };
    int slot;

    memset(out, 0, sizeof *out);
    if (s->rom[0] == '\0') {
        fail(err, errcap, 0, "no ROM chosen, this build supplies none", NULL);
        return -1;
    }
    if (mmo_launch_rom_file(s->rom, out->platinum, sizeof out->platinum) != 0) {
        if (stat(s->rom, &st) == 0 && S_ISDIR(st.st_mode))
            fail(err, errcap, 0, "no Platinum backup in", s->rom);
        else
            fail(err, errcap, 0, "the ROM cannot be read:", s->rom);
        return -1;
    }
    if (mmo_launch_rom_code(out->platinum, code) == NULL) {
        fail(err, errcap, 0, "not a cartridge image (no header to read):",
             out->platinum);
        return -1;
    }
    if (!mmo_launch_cart_is(MMO_LAUNCH_CART_PLATINUM, code)) {
        snprintf(what, sizeof what, "that is %s, not Platinum:",
                 cart_name_of(code));
        fail(err, errcap, 0, what, out->platinum);
        return -1;
    }

    /* The other two: named outright, found beside Platinum, or in the
     * working tree's own roms/ folder. */
    sep = mmo_plat_last_sep(out->platinum);
    if (sep == NULL)
        snprintf(dir, sizeof dir, ".");
    else
        snprintf(dir, sizeof dir, "%.*s", (int)(sep - out->platinum),
                 out->platinum);
    for (slot = MMO_LAUNCH_CART_HEARTGOLD; slot <= MMO_LAUNCH_CART_BLACK; slot++) {
        const char *named = slot == MMO_LAUNCH_CART_HEARTGOLD ? s->rom_hg
                                                              : s->rom_bw;
        char *dst = slot == MMO_LAUNCH_CART_HEARTGOLD ? out->heartgold
                                                      : out->black;
        size_t cap = sizeof out->heartgold;

        if (named[0] != '\0') {
            if (mmo_launch_rom_code(named, code) == NULL) {
                snprintf(what, sizeof what, "the %s cartridge cannot be read:",
                         mmo_launch_cart_name(slot));
                fail(err, errcap, 0, what, named);
                return -1;
            }
            if (!mmo_launch_cart_is(slot, code)) {
                snprintf(what, sizeof what, "that is %s, not %s:",
                         cart_name_of(code), mmo_launch_cart_name(slot));
                fail(err, errcap, 0, what, named);
                return -1;
            }
            snprintf(dst, cap, "%s", named);
            continue;
        }
        if (mmo_launch_cart_scan(dir, slot, dst, cap))
            continue;
        if (mmo_launch_cart_scan(s_roms_fallback, slot, dst, cap))
            continue;
        missing[slot] = 1;
    }
    if (missing[MMO_LAUNCH_CART_HEARTGOLD] || missing[MMO_LAUNCH_CART_BLACK]) {
        const char *base = sep != NULL ? sep + 1 : out->platinum;

        snprintf(what, sizeof what,
                 "no %s backup beside %s, put all three cartridges in one folder",
                 missing[MMO_LAUNCH_CART_HEARTGOLD] && missing[MMO_LAUNCH_CART_BLACK]
                     ? "Heart Gold or Black"
                     : missing[MMO_LAUNCH_CART_HEARTGOLD] ? "Heart Gold" : "Black",
                 base);
        fail(err, errcap, 0, what, NULL);
        return -1;
    }
    return 0;
}

int mmo_launch_check(const mmo_launch_settings *s, char *err, size_t errcap)
{
    mmo_launch_roms roms;

    if (mmo_launch_roms_resolve(s, &roms, err, errcap) != 0)
        return -1;
    if (s->pass[0] != '\0' && s->user[0] == '\0') {
        fail(err, errcap, 0, "a password with no account name", NULL);
        return -1;
    }
    /* A feed with no key is a feed nobody vouched for, and a key with no feed
     * is a check that will never run. Either alone is a settings mistake that
     * reads as protection, so neither is allowed to stand. */
    if (s->feed[0] != '\0' && s->feed_key[0] == '\0') {
        fail(err, errcap, 0, "an update feed with no key to verify it", NULL);
        return -1;
    }
    if (s->feed_key[0] != '\0' && s->feed[0] == '\0') {
        fail(err, errcap, 0, "a feed key with no feed to check", NULL);
        return -1;
    }
    /* A URL with nowhere verified to land, or no key to verify with, is a
     * fetch nobody would then hold the install to. */
    if (s->feed_url[0] != '\0' &&
        (s->feed[0] == '\0' || s->feed_key[0] == '\0')) {
        fail(err, errcap, 0, "a feed URL needs both feed and feed-key set",
             NULL);
        return -1;
    }
    /* A certificate authority for a channel nothing here fetches from is a
     * setting that reads as trust while doing nothing. */
    if (s->feed_ca[0] != '\0' && s->feed_url[0] == '\0') {
        fail(err, errcap, 0, "a feed-ca needs a feed-url to use it for", NULL);
        return -1;
    }
    if (s->scale < MMO_LAUNCH_SCALE_AUTO || s->scale > MMO_LAUNCH_SCALE_MAX ||
        s->render_scale < 1 || s->render_scale > MMO_LAUNCH_RS_MAX ||
        s->hd3d < MMO_LAUNCH_HD3D_MIN || s->hd3d > MMO_LAUNCH_HD3D_MAX ||
        !viewport_ok(s->viewport) ||
        mmo_launch_layout_name(s->layout) == NULL ||
        mmo_launch_filter_name(s->filter) == NULL ||
        mmo_launch_fit_name(s->fit) == NULL ||
        mmo_launch_pace_name(s->pace) == NULL ||
        mmo_launch_button_mode_name(s->button_mode) == NULL) {
        fail(err, errcap, 0, "a display setting is out of range", NULL);
        return -1;
    }
    /* The window refuses this pair on sight; refusing it here says so where the
     * player can still change it. */
    if (s->filter == MMO_FILTER_SCALE2X && s->render_scale < 2) {
        fail(err, errcap, 0, "scale2x wants a render scale of 2 or more", NULL);
        return -1;
    }
    if (mmo_launch_bind_check(s->bind) != 0) {
        fail(err, errcap, 0, "the bind row is not PAD=KEY[,PAD=KEY...]", NULL);
        return -1;
    }
    return 0;
}

void mmo_launch_chan(char *out, size_t cap, long pid)
{
    snprintf(out, cap, "openmmo-%ld", pid);
}

/* ------------------------------------------------------------------ */
/* Where the three files are                                           */
/* ------------------------------------------------------------------ */

/* argv[0] up to its last slash, "." for a bare name, which is what a program
 * started off $PATH gives us and the one case none of this can resolve. */
static void exe_dir(const char *argv0, char *out, size_t cap)
{
    const char *slash = mmo_plat_last_sep(argv0);

    if (slash == NULL)
        snprintf(out, cap, ".");
    else
        snprintf(out, cap, "%.*s", (int)(slash - argv0), argv0);
}

/* `dir/rest`, and whether it is there in the way `mode` asks. */
static int probe(char *out, size_t cap, const char *dir, const char *rest,
                 int mode)
{
    snprintf(out, cap, "%s/%s", dir, rest);
    return access(out, mode) == 0;
}

/* The same, for one of the three programs: the host's own name for it, and the
 * host's own idea of what it means for a file to be runnable. */
static int probe_exe(char *out, size_t cap, const char *dir, const char *rest)
{
    snprintf(out, cap, "%s/%s%s", dir, rest, mmo_plat_exe_suffix());
    return mmo_plat_is_executable(out);
}

void mmo_launch_roms_dir(const char *argv0, char *roms, size_t rcap)
{
    char dir[MMO_LAUNCH_PATH - 32];
    const char *e = getenv("OPENMMO_ROMS");

    if (e != NULL && e[0] != '\0') {
        snprintf(roms, rcap, "%s", e);
        return;
    }
    exe_dir(argv0, dir, sizeof dir);
    if (!probe(roms, rcap, dir, "../../roms", R_OK) &&
        !probe(roms, rcap, dir, "../roms", R_OK))
        roms[0] = '\0';
}

void mmo_launch_paths(const char *argv0, char *port, size_t pcap,
                      char *view, size_t vcap, char *rom, size_t rcap)
{
    /* Short of a whole path on purpose: the directory plus the longest name
     * appended below still fits what the caller gave us. */
    char dir[MMO_LAUNCH_PATH - 32];
    const char *e;

    exe_dir(argv0, dir, sizeof dir);

    e = getenv("OPENMMO_PORT");
    if (e != NULL && e[0] != '\0') {
        snprintf(port, pcap, "%s", e);
    } else if (!probe_exe(port, pcap, dir, "fused/pokeplatinum") &&
               !probe_exe(port, pcap, dir, "pokeplatinum")) {
        /* Neither layout has a game in it. That is an unbuilt tree far more
         * often than a broken release, so leave the built tree's path in
         * place: it is the one whose absence has a `make` to fix it. */
        probe_exe(port, pcap, dir, "fused/pokeplatinum");
    }

    e = getenv("OPENMMO_VIEWER");
    if (e != NULL && e[0] != '\0')
        snprintf(view, vcap, "%s", e);
    else
        probe_exe(view, vcap, dir, "openmmo-view");

    /*
     * The cartridge lives where the game looks for it, not where the launcher happens to sit:
     * `../rom` from the game program, which is the `rom/` beside `bin/` in a release and the
     * one beside the fused binary in a built tree.
     */
    {
        char gdir[MMO_LAUNCH_PATH - 32];
        char up[MMO_LAUNCH_PATH - 32];

        /* Two steps up from the game program rather than one plus `..`, so
         * the path this hands the player to look at is the folder they were
         * told to fill and not a walk through `fused/..`. */
        exe_dir(port, gdir, sizeof gdir);
        exe_dir(gdir, up, sizeof up);
        if (!probe(rom, rcap, up, "rom/" MMO_LAUNCH_ROM_NAME, R_OK)) {
            exe_dir(dir, up, sizeof up);
            if (!probe(rom, rcap, up, "rom/" MMO_LAUNCH_ROM_NAME, R_OK))
                rom[0] = '\0';
        }
    }
}

/* ------------------------------------------------------------------ */
/* The two command lines                                               */
/* ------------------------------------------------------------------ */

/* Copy `s` into the plan's arena and return the copy, or NULL if it is full. */
static char *arena_str(mmo_launch_plan *p, const char *s)
{
    size_t n = strlen(s) + 1;
    char *at;

    if (p->used + n > sizeof p->arena)
        return NULL;
    at = p->arena + p->used;
    memcpy(at, s, n);
    p->used += n;
    return at;
}

static char *arena_fmt(mmo_launch_plan *p, const char *fmt, ...)
{
    char buf[MMO_LAUNCH_PATH * 2];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof buf)
        return NULL;
    return arena_str(p, buf);
}

/* Each of these appends one entry and reports whether the plan is still whole;
 * the caller ORs the results and checks once, so a full arena or a full vector
 * is one error rather than fifteen branches. */
static int push_arg(char **vec, int *n, mmo_launch_plan *p, const char *s)
{
    char *copy;

    if (*n >= MMO_LAUNCH_MAX_ARGV - 1)
        return -1;
    copy = arena_str(p, s);
    if (copy == NULL)
        return -1;
    vec[(*n)++] = copy;
    return 0;
}

static int push_argf(char **vec, int *n, mmo_launch_plan *p, const char *fmt, int v)
{
    char buf[32];

    snprintf(buf, sizeof buf, fmt, v);
    return push_arg(vec, n, p, buf);
}

static int push_env(mmo_launch_plan *p, const char *name, const char *value)
{
    char *entry;

    if (p->envc >= MMO_LAUNCH_MAX_ENV - 1)
        return -1;
    entry = arena_fmt(p, "%s=%s", name, value);
    if (entry == NULL)
        return -1;
    p->env[p->envc++] = entry;
    return 0;
}

/* The offline game's own wallet, as text, or -1 when there is nothing to say.
 * Defined with the rest of the save folder's readers, below. */
static int offline_wallet(const mmo_launch_settings *s, const char *port_exe,
                          char *out, size_t cap);

static int plan_build(const mmo_launch_settings *s, const char *port_exe,
                      const char *view_exe, const char *chan, long pid,
                      const mmo_launch_offline *off,
                      const mmo_launch_export *exp,
                      mmo_launch_plan *p, char *err, size_t errcap)
{
    int bad = 0;

    memset(p, 0, sizeof *p);
    if (mmo_launch_check(s, err, errcap) != 0)
        return -1;
    if (port_exe == NULL || port_exe[0] == '\0' ||
        view_exe == NULL || view_exe[0] == '\0') {
        fail(err, errcap, 0, "the plan needs both programs' paths", NULL);
        return -1;
    }
    if (chan != NULL && chan[0] != '\0') {
        if (strlen(chan) >= sizeof p->chan) {
            fail(err, errcap, 0, "the channel name is too long", NULL);
            return -1;
        }
        snprintf(p->chan, sizeof p->chan, "%s", chan);
    } else {
        mmo_launch_chan(p->chan, sizeof p->chan, pid);
    }

    /* The port: no arguments at all, everything through the environment.
     * PC_ROM is always the file; the setting may have been a folder. */
    {
        char romfile[MMO_LAUNCH_PATH];

        if (mmo_launch_rom_file(s->rom, romfile, sizeof romfile) != 0) {
            fail(err, errcap, 0, "the ROM cannot be read:", s->rom);
            return -1;
        }
        bad |= push_arg(p->port_argv, &p->port_argc, p, port_exe);
        bad |= push_env(p, "PC_VIEW", p->chan);
        bad |= push_env(p, "PC_ROM", romfile);
    }
    /*
     * Runtime content: the engine reads these at pc_modfs_boot. An empty mods list is omitted,
     * not sent as PC_MODS=, because an empty value wins over loadorder.txt and would silence a
     * folder that had one.
     */
    {
        int mods_dir_live = dir_exists(s->mods_dir);
        char modroot[MMO_LAUNCH_PATH];
        char base[MMO_LAUNCH_TEXT + 32];
        int named_a_package = 0;

    if (mods_dir_live)
        bad |= push_env(p, "PC_MODS_DIR", s->mods_dir);

    /*
     * Where this install keeps its packages, resolved once because two of them are looked for
     * below.
     */
    if (mods_dir_live) {
        snprintf(modroot, sizeof modroot, "%s", s->mods_dir);
    } else {
        const char *slash = mmo_plat_last_sep(port_exe);
        char bindir[MMO_LAUNCH_PATH];
        const char *slash2 = NULL;

        if (slash != NULL) {
            snprintf(bindir, sizeof bindir, "%.*s",
                     (int)(slash - port_exe), port_exe);
            slash2 = mmo_plat_last_sep(bindir);
        }
        if (slash2 != NULL)
            snprintf(modroot, sizeof modroot, "%.*s%smods",
                     (int)(slash2 - bindir), bindir, mmo_plat_sep());
        else
            snprintf(modroot, sizeof modroot, "mods");
    }

    /* The follower package names itself. */
    mmo_launch_mods_list(s, modroot, base, sizeof base);
    if (!mods_list_has(base, "followers")) {
        char toml[MMO_LAUNCH_PATH + 32];
        FILE *probe;

        snprintf(toml, sizeof toml, "%s/followers/mod.toml", modroot);
        probe = fopen(toml, "rb");
        if (probe != NULL) {
            fclose(probe);
            if (base[0] != '\0')
                snprintf(base + strlen(base), sizeof base - strlen(base),
                         ",followers");
            else
                snprintf(base, sizeof base, "followers");
            named_a_package = 1;
        }
    }

    /*
     * The soundtrack rides the same door: a composed package whose one claim replaces the
     * sound archive.
     */
    /* Which pair's package: the font follows the soundtrack unless pinned,
     * and platinum-with-platinum is the stock archive and asks for nothing. */
    {
    int strack = s->soundtrack;
    int sfont = (s->soundfont == MMO_SOUNDFONT_FOLLOW)
                ? strack : s->soundfont - 1;

    if (strack != MMO_SOUNDTRACK_PLATINUM || sfont != MMO_SOUNDTRACK_PLATINUM) {
        char pkgname[40];
        char sdat[MMO_LAUNCH_PATH + 96];
        char list[MMO_LAUNCH_TEXT + 128];
        FILE *probe;

        snprintf(pkgname, sizeof pkgname, "sound_%s_%s",
                 sound_slugs[strack], sound_slugs[sfont]);
        snprintf(sdat, sizeof sdat,
                 "%s/%s/replace/data/sound/pl_sound_data.sdat",
                 modroot, pkgname);
        probe = fopen(sdat, "rb");
        if (probe == NULL) {
            fail(err, errcap, 0, "that soundtrack is not composed yet, "
                 "`make -C mmo soundtrack` builds it from your own cartridge; "
                 "missing:", sdat);
            return -1;
        }
        fclose(probe);
        if (!mods_dir_live)
            bad |= push_env(p, "PC_MODS_DIR", modroot);
        if (base[0] != '\0')
            snprintf(list, sizeof list, "%s,%s", base, pkgname);
        else
            snprintf(list, sizeof list, "%s", pkgname);
        bad |= push_env(p, "PC_MODS", list);
    } else if (base[0] != '\0') {
        if (!mods_dir_live && named_a_package)
            bad |= push_env(p, "PC_MODS_DIR", modroot);
        bad |= push_env(p, "PC_MODS", base);
    }
    }
    }
    /* A session's party, position and flags are the server's. */
    if (off != NULL) {
        /* Which row was pressed, said out loud rather than inferred. */
        bad |= push_env(p, "OPENMMO_OFFLINE", "1");
        bad |= push_env(p, "PC_SAVE", off->save);
        /* The host's own clock, read once at the press. The port's is a fixed
         * epoch advanced by its frame counter and nothing in it reads the
         * host, so without this an offline game plays every session through
         * the same morning and never sees a night. */
        bad |= push_env(p, "PC_RTC", off->rtc);
        bad |= push_env(p, "PC_RECORD_INPUT", off->record);
    } else {
        bad |= push_env(p, "PC_SAVE", "none");
        /* Where "Continue Offline" writes, and the only reason a session has a
         * file name at all. The game swaps PC_SAVE for this one itself once it
         * knows there is a session behind it, so the name above stays the true
         * statement of what the front door asked for: no player save. */
        if (exp != NULL && exp->save[0] != '\0') {
            char wallet[24];

            bad |= push_env(p, "OPENMMO_EXPORT", exp->save);
            /* And the wallet that game is to keep, which is the OFFLINE one. */
            if (offline_wallet(s, port_exe, wallet, sizeof wallet) == 0)
                bad |= push_env(p, "OPENMMO_EXPORT_MONEY", wallet);
        }
        /* The other direction: a save the player answered yes to bringing
         * online. The game offers it once it has a character, and the server
         * decides. Stated only when there is one, an empty name would be a
         * session told to offer nothing, which is what leaving it out says. */
        if (exp != NULL && exp->import[0] != '\0')
            bad |= push_env(p, "OPENMMO_IMPORT", exp->import);
        /*
         * And the play behind it, when there is any to show. Named separately from the report
         * because it goes up separately: the save lands first and on its own, and the records
         * are offered for the import that landed.
         */
        if (exp != NULL && exp->chain[0] != '\0')
            bad |= push_env(p, "OPENMMO_IMPORT_CHAIN", exp->chain);
    }
    /*
     * Whether is not asked either, only who. The address the game dials is compiled into it
     * (mmo/include/endpoint.h) and a Play that did not join it would be the engine port with
     * our client sitting silent inside it, which is not what this front door is for.
     */
    bad |= push_env(p, "OPENMMO_SESSION", off != NULL ? "0" : "1");
    /*
     * What revision.txt says, so a save report the game writes can name the build that played
     * the session. Not written at all in a tree nobody published: the report's own reader
     * takes an absent one as "unknown", and 0 is a real revision.
     */
    {
        char root[MMO_LAUNCH_PATH];
        int revision;

        mmo_launch_install_root(port_exe, root, sizeof root);
        revision = mmo_feed_installed_revision(root);
        if (revision > 0) {
            char text[16];

            snprintf(text, sizeof text, "%d", revision);
            bad |= push_env(p, "OPENMMO_REVISION", text);
        }
    }
    /*
     * Stated even when empty, for the reason spelled out below: a name left out is not "no
     * account", it is whatever the environment already said, and a leftover OPENMMO_PASS from
     * a debugging run would go on answering for the panel.
     */
    bad |= push_env(p, "OPENMMO_USER", off != NULL ? "" : s->user);
    bad |= push_env(p, "OPENMMO_PASS", off != NULL ? "" : s->pass);
    /* Who is not asked either, only that there is a session. The character
     * list inside the game is the one place a row is chosen, made or deleted,
     * so the front door names no row and the picker always opens. */
    /*
     * A plan always starts a window, and the window draws the session's state itself, at its
     * own resolution and outside the game's pixels.
     */
    bad |= push_env(p, "OPENMMO_HUD", "0");

    /* Every setting the panel owns is stated, including the one that matches the default. */
    bad |= push_env(p, "OPENMMO_BUTTON_MODE", button_names[s->button_mode]);
    {
        char buf[8];

        /* 100 is the scale the mixer holds with nothing set. */
        snprintf(buf, sizeof buf, "%d", s->music);
        bad |= push_env(p, "OPENMMO_MUSIC", buf);
        snprintf(buf, sizeof buf, "%d", s->sfx);
        bad |= push_env(p, "OPENMMO_SFX", buf);
    }
    /* Stated either way, for the reason above: a name left out is not the
     * default, it is whatever the environment already said, and a panel that
     * turned presence off could not take it back. */
    bad |= push_env(p, "OPENMMO_DISCORD", s->discord ? "1" : "0");

    /* The viewport the window reports into is what the port renders. auto is
     * the default and is stated so a leftover PC_ASPECT in the environment
     * cannot win. native is the one value that means "leave the port alone". */
    if (s->viewport[0] == '\0' || strcmp(s->viewport, "auto") == 0)
        bad |= push_env(p, "PC_ASPECT", "auto");
    else if (strcmp(s->viewport, "native") != 0)
        bad |= push_env(p, "PC_ASPECT", s->viewport);
    {
        char buf[8];

        snprintf(buf, sizeof buf, "%d", s->hd3d);
        bad |= push_env(p, "PC_HD3D", buf);
        /* The official client's own per-map distance is 100 and this stands the field
         * camera farther out than that, everywhere, for everybody. */
        snprintf(buf, sizeof buf, "%d", s->camera);
        bad |= push_env(p, "OPENMMO_CAMERA_DISTANCE", buf);
    }
    bad |= push_env(p, "PC_PACE", s->pace == MMO_PACE_UNLIMITED ? "0" : "1");
    /*
     * The mixer's output, the same in every session and stated for the reason above: 48,000 a
     * second with cubic interpolation between a channel's samples, the stream a device plays
     * as it is.
     */
    bad |= push_env(p, "PC_AUDIO_RATE", "48000");
    bad |= push_env(p, "PC_AUDIO_INTERP", "cubic");

    /* The window: the channel first, then the display settings. */
    bad |= push_arg(p->view_argv, &p->view_argc, p, view_exe);
    bad |= push_arg(p->view_argv, &p->view_argc, p, p->chan);
    /* Every screen that would need a server goes, and the chat box with it.
     * The bar keeps the four that are the game's own. */
    if (off != NULL)
        bad |= push_arg(p->view_argv, &p->view_argc, p, "--offline");
    bad |= push_arg(p->view_argv, &p->view_argc, p, "--scale");
    if (s->scale == MMO_LAUNCH_SCALE_AUTO)
        bad |= push_arg(p->view_argv, &p->view_argc, p, "auto");
    else
        bad |= push_argf(p->view_argv, &p->view_argc, p, "%d", s->scale);
    bad |= push_arg(p->view_argv, &p->view_argc, p, "--layout");
    bad |= push_arg(p->view_argv, &p->view_argc, p, layout_names[s->layout]);
    bad |= push_arg(p->view_argv, &p->view_argc, p, "--render-scale");
    /*
     * Both resolutions draw the page's frame with the engine's sub-pixels already in it, so
     * the window's CPU prescale would only re-replicate them: milliseconds a frame and a
     * texture upload four times the size, for the picture the GPU's own sampling of the un-
     * prescaled frame already produces.
     */
    if (s->filter != MMO_FILTER_SCALE2X)
        bad |= push_arg(p->view_argv, &p->view_argc, p, "1");
    else
        bad |= push_argf(p->view_argv, &p->view_argc, p, "%d", s->render_scale);
    bad |= push_arg(p->view_argv, &p->view_argc, p, "--filter");
    bad |= push_arg(p->view_argv, &p->view_argc, p, filter_names[s->filter]);
    if (s->fit == MMO_FIT_INTEGER)
        bad |= push_arg(p->view_argv, &p->view_argc, p, "--integer");
    else if (s->fit == MMO_FIT_STRETCH)
        bad |= push_arg(p->view_argv, &p->view_argc, p, "--stretch");
    if (s->fullscreen)
        bad |= push_arg(p->view_argv, &p->view_argc, p, "--fullscreen");
    if (!s->audio) {
        bad |= push_arg(p->view_argv, &p->view_argc, p, "--no-audio");
    } else if (s->audio_device[0] != '\0') {
        bad |= push_arg(p->view_argv, &p->view_argc, p, "--audio-device");
        bad |= push_arg(p->view_argv, &p->view_argc, p, s->audio_device);
    }
    if (s->theme[0] != '\0') {
        bad |= push_arg(p->view_argv, &p->view_argc, p, "--theme");
        bad |= push_arg(p->view_argv, &p->view_argc, p, s->theme);
    }
    /* The player's rebinds, verbatim, in the window's own syntax: the map is
     * still the window's, this is the row it is handed. */
    if (s->bind[0] != '\0') {
        bad |= push_arg(p->view_argv, &p->view_argc, p, "--bind");
        bad |= push_arg(p->view_argv, &p->view_argc, p, s->bind);
    }

    if (bad != 0) {
        fail(err, errcap, 0, "the launch plan does not fit its own buffers", NULL);
        memset(p, 0, sizeof *p);
        return -1;
    }
    p->port_argv[p->port_argc] = NULL;
    p->view_argv[p->view_argc] = NULL;
    p->env[p->envc] = NULL;
    return 0;
}

int mmo_launch_plan_build(const mmo_launch_settings *s, const char *port_exe,
                          const char *view_exe, const char *chan, long pid,
                          mmo_launch_plan *p, char *err, size_t errcap)
{
    return plan_build(s, port_exe, view_exe, chan, pid, NULL, NULL, p, err,
                      errcap);
}

int mmo_launch_plan_build_offline(const mmo_launch_settings *s,
                                  const char *port_exe, const char *view_exe,
                                  const char *chan, long pid,
                                  const mmo_launch_offline *o,
                                  mmo_launch_plan *p, char *err, size_t errcap)
{
    if (o == NULL || o->save[0] == '\0' || o->rtc[0] == '\0' ||
        o->record[0] == '\0') {
        memset(p, 0, sizeof *p);
        fail(err, errcap, 0, "the offline row has no save folder to play in",
             NULL);
        return -1;
    }
    return plan_build(s, port_exe, view_exe, chan, pid, o, NULL, p, err,
                      errcap);
}

int mmo_launch_plan_build_export(const mmo_launch_settings *s,
                                 const char *port_exe, const char *view_exe,
                                 const char *chan, long pid,
                                 const mmo_launch_export *x,
                                 mmo_launch_plan *p, char *err, size_t errcap)
{
    if (x == NULL || x->save[0] == '\0') {
        memset(p, 0, sizeof *p);
        fail(err, errcap, 0, "the session has nowhere to write an export to",
             NULL);
        return -1;
    }
    return plan_build(s, port_exe, view_exe, chan, pid, NULL, x, p, err,
                      errcap);
}

void mmo_launch_plan_print(const mmo_launch_plan *p, FILE *out)
{
    int i;

    fprintf(out, "chan %s\n", p->chan);
    for (i = 0; i < p->envc; i++) {
        if (strncmp(p->env[i], "OPENMMO_PASS=", 13) == 0)
            fprintf(out, "env OPENMMO_PASS=<hidden>\n");
        else
            fprintf(out, "env %s\n", p->env[i]);
    }
    fprintf(out, "port");
    for (i = 0; i < p->port_argc; i++)
        fprintf(out, " %s", p->port_argv[i]);
    fprintf(out, "\nview");
    for (i = 0; i < p->view_argc; i++)
        fprintf(out, " %s", p->view_argv[i]);
    fprintf(out, "\n");
}

/* ------------------------------------------------------------------ */
/* Playing with no server                                              */
/* ------------------------------------------------------------------ */

/* The one name the engine's backup chip is given offline. The stamped copies
 * are this plus a dot and the stamp, so a folder listing sorts them under the
 * live one and a player can see at a glance which is which. */
#define SAVE_NAME "platinum.sav"

/*
 * The marker beside a saved game that was carried in from a file rather than played here
 * (mmo_launch_bundle_read).
 */
#define ELSEWHERE_NAME SAVE_NAME ".elsewhere"

/*
 * What is kept beside an image rather than inside it, and so has to travel with it whenever
 * the image is set aside or put back.
 */
#define REPORT_EXT ".report"
#define LANDED_EXT ".report.landed"

/* What the record says it is. It is first in the file so a reader that knows
 * nothing else about the format can still say "not mine" rather than guess:
 * an unknown version is UNVERIFIABLE, never a refusal. */
#define LINK_VERSION 1

/* The lock beside that save, named from the folder it lives in. */
static void save_lock_path(const char *dir, char *out, size_t cap)
{
    snprintf(out, cap, "%s/" SAVE_NAME ".lock", dir);
}

void mmo_launch_mods_root(const mmo_launch_settings *s, const char *port_exe,
                          char *out, size_t cap)
{
    struct stat st;

    if (s->mods_dir[0] != '\0' && stat(s->mods_dir, &st) == 0
        && S_ISDIR(st.st_mode)) {
        snprintf(out, cap, "%s", s->mods_dir);
        return;
    }
    {
        const char *slash = mmo_plat_last_sep(port_exe);
        char bindir[MMO_LAUNCH_PATH];
        const char *slash2 = NULL;

        if (slash != NULL) {
            snprintf(bindir, sizeof bindir, "%.*s",
                     (int)(slash - port_exe), port_exe);
            slash2 = mmo_plat_last_sep(bindir);
        }
        if (slash2 != NULL)
            snprintf(out, cap, "%.*s%smods",
                     (int)(slash2 - bindir), bindir, mmo_plat_sep());
        else
            snprintf(out, cap, "mods");
    }
}

void mmo_launch_install_root(const char *port_exe, char *out, size_t cap)
{
    const char *e = getenv("OPENMMO_ROOT");
    char dir[MMO_LAUNCH_PATH];
    const char *last;

    if (e != NULL && e[0] != '\0') {
        snprintf(out, cap, "%s", e);
        return;
    }
    exe_dir(port_exe, dir, sizeof dir);
    last = mmo_plat_last_sep(dir);
    last = (last != NULL) ? last + 1 : dir;
    /* `bin` in an unpacked release, `fused` in a built tree: the game sits one
     * below the root in both, and this is the same walk the soundtrack package
     * takes to find mods/, so save/ and mods/ can never end up in different
     * folders. Anywhere else the folder holding the game is the root. */
    if (strcmp(last, "bin") == 0 || strcmp(last, "fused") == 0)
        exe_dir(dir, out, cap);
    else
        snprintf(out, cap, "%s", dir);
}

void mmo_launch_slot_dir(const char *root, int slot, char *out, size_t cap)
{
    if (out == NULL || cap == 0)
        return;
    if (root == NULL)
        root = "";
    /* Clamped, never refused: launch_plan.h says why, and the two callers a
     * bad number could reach are the front door starting and a test. */
    if (slot <= 1 || slot > MMO_LAUNCH_SLOTS)
        snprintf(out, cap, "%s/save", root);
    else
        snprintf(out, cap, "%s/save/slot%d", root, slot);
}

void mmo_launch_save_dir(const char *port_exe, int slot, char *out, size_t cap)
{
    char root[MMO_LAUNCH_PATH];

    mmo_launch_install_root(port_exe, root, sizeof root);
    mmo_launch_slot_dir(root, slot, out, cap);
}

/* `YYYY-MM-DD HH:MM:SS` as `YYYYmmdd-HHMMSS`. -1 for anything that is not
 * that, because a stamp is the name of a file and the clock a replay would
 * be given, and a malformed one would quietly become both. */
static int stamp_compact(const char *wall, char *out, size_t cap)
{
    static const char digits[] = "0123456789";
    static const char *const shape = "dddd-dd-dd dd:dd:dd";
    size_t i, n = 0;

    if (wall == NULL || cap < MMO_LAUNCH_STAMP)
        return -1;
    for (i = 0; shape[i] != '\0'; i++) {
        if (shape[i] == 'd') {
            if (strchr(digits, wall[i]) == NULL || wall[i] == '\0')
                return -1;
        } else if (wall[i] != shape[i]) {
            return -1;
        }
    }
    if (wall[i] != '\0')
        return -1;
    for (i = 0; i < 19; i++) {
        if (i == 10)
            out[n++] = '-';
        if (shape[i] == 'd')
            out[n++] = wall[i];
    }
    out[n] = '\0';
    return 0;
}

/* The reverse test, over a name we are about to trust: fifteen characters,
 * eight digits, a dash, six digits. A folder is a place other programs write
 * too, so nothing in it is read back on its name alone. */
static int stamp_ok(const char *s)
{
    int i;

    if (s == NULL || strlen(s) != 15 || s[8] != '-')
        return 0;
    for (i = 0; i < 15; i++) {
        if (i == 8)
            continue;
        if (s[i] < '0' || s[i] > '9')
            return 0;
    }
    return 1;
}

/* Bytes, through a buffer, into a sibling temp file that is renamed over the
 * destination, so a copy interrupted half way leaves the destination as it
 * was rather than half of something. 0, or -1 with errno still set. */
static int copy_file(const char *from, const char *to)
{
    char tmp[MMO_LAUNCH_PATH + 8];
    char buf[32768];
    FILE *in, *out;
    size_t n;
    int bad = 0;

    in = fopen(from, "rb");
    if (in == NULL)
        return -1;
    snprintf(tmp, sizeof tmp, "%s.tmp", to);
    out = fopen(tmp, "wb");
    if (out == NULL) {
        fclose(in);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            bad = 1;
            break;
        }
    }
    if (ferror(in))
        bad = 1;
    fclose(in);
    /* The copy is a backup of a save file, so it is worth nothing at all until
     * it is on the disk rather than in the host's cache. */
    if (!bad && mmo_plat_fsync(out) != 0)
        bad = 1;
    if (fclose(out) != 0)
        bad = 1;
    if (bad || mmo_plat_rename_over(tmp, to) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}

/* The sha256 of a file, or the word `none` where there is no file. An absent
 * image is not a failure: the first offline session ever played starts from
 * no file at all, which is exactly what a New Game is. */
static void hash_or_none(const char *path, char out[65])
{
    if (path == NULL || mmo_feed_hash_file(path, out) != 0)
        snprintf(out, 65, "none");
}

/*
 * The frame the game stopped on, from the file it drops beside the save, and then that file is
 * gone. Reading it is the only chance, it is taken at both ends of a session so a stale one
 * can never be read as this session's.
 */
static int end_frame_take(const char *save, unsigned long long *out)
{
    char path[MMO_LAUNCH_PATH + 16];
    unsigned long long frames;
    FILE *f;
    int got = 0;

    if (save == NULL || save[0] == '\0')
        return -1;
    if ((size_t)snprintf(path, sizeof path, "%s.frames", save) >= sizeof path)
        return -1;
    f = fopen(path, "rb");
    if (f != NULL) {
        got = fscanf(f, "%llu", &frames) == 1;
        fclose(f);
    }
    remove(path);
    if (!got)
        return -1;
    if (out != NULL)
        *out = frames;
    return 0;
}

int mmo_launch_offline_open(const char *port_exe, int slot, const char *wall,
                            mmo_launch_offline *o, char *err, size_t errcap)
{
    char now[MMO_LAUNCH_TEXT];
    char lock[MMO_LAUNCH_PATH + 24];

    if (o == NULL)
        return -1;
    memset(o, 0, sizeof *o);
    if (port_exe == NULL || port_exe[0] == '\0') {
        fail(err, errcap, 0, "the offline row needs the game's own path", NULL);
        return -1;
    }
    if (wall == NULL || wall[0] == '\0') {
        mmo_plat_stamp(now, sizeof now);
        wall = now;
    }
    if (stamp_compact(wall, o->stamp, sizeof o->stamp) != 0) {
        fail(err, errcap, 0, "this host's clock did not read as a date:", wall);
        return -1;
    }
    snprintf(o->rtc, sizeof o->rtc, "%s", wall);
    mmo_launch_save_dir(port_exe, slot, o->dir, sizeof o->dir);
    snprintf(o->save, sizeof o->save, "%s/" SAVE_NAME, o->dir);
    snprintf(o->record, sizeof o->record, "%s/sessions/%s.inp", o->dir,
             o->stamp);
    snprintf(o->link, sizeof o->link, "%s/sessions/%s.link", o->dir, o->stamp);
    /* The deeper of the two makes both. */
    if (mkdir_parents(o->record) != 0) {
        fail(err, errcap, 0, "the save folder could not be made:", o->dir);
        return -1;
    }
    /* And the save is this session's until it ends. */
    save_lock_path(o->dir, lock, sizeof lock);
    switch (mmo_plat_lock_take(lock, &o->lock)) {
    case 0:
        break;
    case 1:
        fail(err, errcap, 0, "another launcher already has this saved game"
             " open; close that game before starting this one", NULL);
        return -1;
    default:
        fail(err, errcap, 0, "this saved game could not be claimed for the"
             " session:", lock);
        return -1;
    }
    return 0;
}

void mmo_launch_offline_close(mmo_launch_offline *o)
{
    if (o == NULL || o->lock == NULL)
        return;
    mmo_plat_lock_drop(o->lock);
    o->lock = NULL;
}

int mmo_launch_offline_begin(const mmo_launch_offline *o, int revision,
                             char *err, size_t errcap)
{
    char boot[65];
    FILE *f;

    if (o == NULL || o->link[0] == '\0') {
        fail(err, errcap, 0, "there is no session to write down", NULL);
        return -1;
    }
    hash_or_none(o->save, boot);
    f = fopen(o->link, "wb");
    if (f == NULL) {
        fail(err, errcap, 0, "the session record could not be written:",
             o->link);
        return -1;
    }
    fprintf(f, "version %d\n", LINK_VERSION);
    /* What revision.txt says, or nothing at all in a tree nobody published.
     * An install that cannot name itself is written down as not naming
     * itself rather than as revision 0, which is a real revision. */
    if (revision > 0)
        fprintf(f, "revision %d\n", revision);
    else
        fprintf(f, "revision unknown\n");
    fprintf(f, "rtc %s\n", o->rtc);
    fprintf(f, "boot-sha256 %s\n", boot);
    fprintf(f, "recording sessions/%s.inp\n", o->stamp);
    /* The game writes this one at the end. An old one left by the session
     * before would otherwise be read as this session's, and an end frame is
     * exactly the field a reader has no way to sanity-check. */
    end_frame_take(o->save, NULL);
    if (fclose(f) != 0) {
        fail(err, errcap, 0, "the session record could not be written:",
             o->link);
        return -1;
    }
    return 0;
}

/* Drop every stamped image past the newest `keep`. */
/*
 * The pair kept beside an image, the report the front door offers, and the server's answer
 * that an earlier one was taken, moved with the image rather than left behind.
 */
static void side_keep(const char *live, const char *stamp)
{
    char from[MMO_LAUNCH_PATH + 32];
    char to[MMO_LAUNCH_PATH + MMO_LAUNCH_STAMP + 32];
    int i;

    for (i = 0; i < 2; i++) {
        const char *ext = (i == 0) ? REPORT_EXT : LANDED_EXT;

        snprintf(from, sizeof from, "%s%s", live, ext);
        snprintf(to, sizeof to, "%s.%s%s", live, stamp, ext);
        if (access(from, R_OK) != 0) {
            remove(to);
            continue;
        }
        if (mmo_plat_rename_over(from, to) != 0 && copy_file(from, to) != 0)
            continue;
        remove(from);
    }
}

static void side_put(const char *live, const char *stamp)
{
    char from[MMO_LAUNCH_PATH + MMO_LAUNCH_STAMP + 32];
    char to[MMO_LAUNCH_PATH + 32];
    int i;

    for (i = 0; i < 2; i++) {
        const char *ext = (i == 0) ? REPORT_EXT : LANDED_EXT;

        snprintf(from, sizeof from, "%s.%s%s", live, stamp, ext);
        snprintf(to, sizeof to, "%s%s", live, ext);
        if (access(from, R_OK) != 0 || copy_file(from, to) != 0)
            remove(to);
    }
}

static void prune_images(const char *dir, int keep)
{
    char stamps[64][MMO_LAUNCH_STAMP];
    char path[MMO_LAUNCH_PATH + MMO_LAUNCH_STAMP + 16];
    int n, i;

    n = mmo_launch_offline_list(dir, stamps, (int)(sizeof stamps / sizeof stamps[0]));
    for (i = keep; i < n; i++) {
        snprintf(path, sizeof path, "%s/" SAVE_NAME ".%s", dir, stamps[i]);
        remove(path);
        /* And what was kept beside it. A report outliving the image it
         * describes is a file nothing can ever put back, which is the same
         * leak the live names get cleared for. */
        snprintf(path, sizeof path, "%s/" SAVE_NAME ".%s" REPORT_EXT, dir,
                 stamps[i]);
        remove(path);
        snprintf(path, sizeof path, "%s/" SAVE_NAME ".%s" LANDED_EXT, dir,
                 stamps[i]);
        remove(path);
    }
}

static int offline_end_record(const mmo_launch_offline *o, char *err,
                              size_t errcap)
{
    char quit[65], rec[65];
    char kept[MMO_LAUNCH_PATH + MMO_LAUNCH_STAMP + 16];
    unsigned long long frames = 0;
    int framed;
    FILE *f;

    if (o == NULL || o->link[0] == '\0') {
        fail(err, errcap, 0, "there is no session to write down", NULL);
        return -1;
    }
    hash_or_none(o->record, rec);
    hash_or_none(o->save, quit);
    framed = end_frame_take(o->save, &frames) == 0;
    f = fopen(o->link, "ab");
    if (f == NULL) {
        fail(err, errcap, 0, "the session record could not be finished:",
             o->link);
        return -1;
    }
    fprintf(f, "recording-sha256 %s\n", rec);
    fprintf(f, "quit-sha256 %s\n", quit);
    /* Last, and only when the game left one. The lines before it are this
     * program's own reading of files it can see; this one is the game's word
     * about a counter nothing outside the process can reach. */
    if (framed)
        fprintf(f, "end-frame %llu\n", frames);
    if (fclose(f) != 0) {
        fail(err, errcap, 0, "the session record could not be finished:",
             o->link);
        return -1;
    }
    /* The image this session left, kept under its own stamp. */
    if (strcmp(quit, "none") != 0) {
        snprintf(kept, sizeof kept, "%s.%s", o->save, o->stamp);
        if (copy_file(o->save, kept) == 0)
            prune_images(o->dir, MMO_LAUNCH_SAVE_KEEP);
    }
    return 0;
}

int mmo_launch_offline_end(mmo_launch_offline *o, char *err, size_t errcap)
{
    int rc = offline_end_record(o, err, errcap);

    /*
     * Last, and whatever the record did: the backup above is the reason the lock is still held
     * this far, the game has exited, but the image it left is being copied and a second
     * launcher starting mid-copy would keep half of it.
     */
    mmo_launch_offline_close(o);
    return rc;
}

int mmo_launch_offline_list(const char *dir, char out[][MMO_LAUNCH_STAMP],
                            int max)
{
    const char *prefix = SAVE_NAME ".";
    size_t plen = strlen(prefix);
    DIR *d;
    struct dirent *e;
    int n = 0, i, j;

    if (dir == NULL || out == NULL || max <= 0)
        return 0;
    d = opendir(dir);
    if (d == NULL)
        return 0;
    while ((e = readdir(d)) != NULL && n < max) {
        if (strncmp(e->d_name, prefix, plen) != 0)
            continue;
        if (!stamp_ok(e->d_name + plen))
            continue;
        snprintf(out[n], MMO_LAUNCH_STAMP, "%s", e->d_name + plen);
        n++;
    }
    closedir(d);
    /* Newest first. The stamp is fixed-width and big-endian by construction,
     * so its own order is the clock's; a readdir order is nobody's. */
    for (i = 1; i < n; i++) {
        char hold[MMO_LAUNCH_STAMP];

        snprintf(hold, sizeof hold, "%s", out[i]);
        for (j = i; j > 0 && strcmp(out[j - 1], hold) < 0; j--)
            snprintf(out[j], MMO_LAUNCH_STAMP, "%s", out[j - 1]);
        snprintf(out[j], MMO_LAUNCH_STAMP, "%s", hold);
    }
    return n;
}

/*
 * A restore with the save already claimed for it. The keep-aside, the marker and the image
 * going back are three writes of one file and they are one act: anything else that wants the
 * save waits for all three or gets none of them.
 */
static int restore_locked(const char *dir, const char *stamp, const char *wall,
                          char *err, size_t errcap)
{
    char from[MMO_LAUNCH_PATH + MMO_LAUNCH_STAMP + 16];
    char live[MMO_LAUNCH_PATH + 16];
    char kept[MMO_LAUNCH_PATH + MMO_LAUNCH_STAMP + 16];
    char mark[MMO_LAUNCH_PATH + 24];
    char now[MMO_LAUNCH_TEXT];
    char aside[MMO_LAUNCH_STAMP];
    FILE *f;
    int wrote = 0;

    snprintf(from, sizeof from, "%s/" SAVE_NAME ".%s", dir, stamp);
    snprintf(live, sizeof live, "%s/" SAVE_NAME, dir);
    if (access(from, R_OK) != 0) {
        fail(err, errcap, 0, "that saved game is not there any more:", from);
        return -1;
    }
    /* Keep the one being replaced first, so restoring the wrong one is itself
     * one restore to undo. Only then is the live file written. */
    if (wall == NULL || wall[0] == '\0') {
        mmo_plat_stamp(now, sizeof now);
        wall = now;
    }
    if (stamp_compact(wall, aside, sizeof aside) != 0) {
        fail(err, errcap, 0, "this host's clock did not read as a date:", wall);
        return -1;
    }
    if (access(live, R_OK) == 0) {
        snprintf(kept, sizeof kept, "%s.%s", live, aside);
        /* Not when the two are the same file: restoring the copy that was
         * just set aside would otherwise overwrite it with itself. */
        if (strcmp(kept, from) != 0 && copy_file(live, kept) != 0) {
            fail(err, errcap, 0,
                 "the game being replaced could not be kept, so nothing was"
                 " replaced:", kept);
            return -1;
        }
        if (strcmp(kept, from) != 0)
            side_keep(live, aside);
    }
    /* Written before the SAVE moves, and the restore is refused if it will not write. */
    snprintf(mark, sizeof mark, "%s.restore", live);
    f = fopen(mark, "wb");
    if (f != NULL) {
        fprintf(f, "version %d\n", LINK_VERSION);
        fprintf(f, "rtc %s\n", wall);
        fprintf(f, "at %s\n", aside);
        fprintf(f, "image %s\n", stamp);
        wrote = mmo_plat_fsync(f) == 0;
        if (fclose(f) != 0)
            wrote = 0;
    }
    if (!wrote) {
        fail(err, errcap, 0, "putting an earlier game back could not be"
             " written down, so nothing was replaced:", mark);
        return -1;
    }
    if (copy_file(from, live) != 0) {
        fail(err, errcap, 0, "that saved game could not be restored:", from);
        return -1;
    }
    /*
     * And what was kept beside the image being put back, put back with it: the report is what
     * makes this save one the front door can offer, and the answer beside it is what stops it
     * being offered twice.
     */
    side_put(live, stamp);
    /* The note about where the game at this name came from goes with the game
     * it was about. The image being put back is one of this machine's own
     * kept copies, and the marker beside it named another machine. */
    {
        char elsewhere[MMO_LAUNCH_PATH + 24];

        snprintf(elsewhere, sizeof elsewhere, "%s/" ELSEWHERE_NAME, dir);
        remove(elsewhere);
    }
    prune_images(dir, MMO_LAUNCH_SAVE_KEEP);
    return 0;
}

int mmo_launch_offline_restore(const char *dir, const char *stamp,
                               const char *wall, char *err, size_t errcap)
{
    char lock[MMO_LAUNCH_PATH + 24];
    mmo_plat_lock *held = NULL;
    int rc;

    /* Asked before the save is claimed. A row that is going to refuse the
     * name it was handed must not take the save off a session about to start,
     * nor make a lock file beside a folder that is not one. */
    if (dir == NULL || dir[0] == '\0' || !stamp_ok(stamp)) {
        fail(err, errcap, 0, "that is not a saved game this folder keeps",
             stamp);
        return -1;
    }
    save_lock_path(dir, lock, sizeof lock);
    switch (mmo_plat_lock_take(lock, &held)) {
    case 0:
        break;
    case 1:
        /*
         * An offline session is playing this file. Putting an earlier game back underneath it
         * would leave that session writing out a game it never loaded, and the copy kept on
         * the way past would be of neither.
         */
        fail(err, errcap, 0, "another launcher has this saved game open; close"
             " that game and put the earlier one back after it", NULL);
        return -1;
    default:
        fail(err, errcap, 0, "this saved game could not be claimed for the"
             " restore:", lock);
        return -1;
    }
    rc = restore_locked(dir, stamp, wall, err, errcap);
    mmo_plat_lock_drop(held);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Carrying a session out to the offline row                           */
/* ------------------------------------------------------------------ */

/* Gone, as far as anything that would read it back is concerned. remove() on
 * a name that was never there succeeds nothing and fails nothing, so the
 * question asked here is the one that matters: can the game still open it. */
static int cleared(const char *path)
{
    remove(path);
    return access(path, F_OK) != 0;
}

int mmo_launch_export_open(const char *port_exe, int slot, long pid,
                           mmo_launch_export *x, char *err, size_t errcap)
{
    char tmp[MMO_LAUNCH_PATH + 8];
    char seed[MMO_LAUNCH_PATH];

    if (x == NULL)
        return -1;
    memset(x, 0, sizeof *x);
    if (port_exe == NULL || port_exe[0] == '\0') {
        fail(err, errcap, 0, "an export needs the game's own path", NULL);
        return -1;
    }
    /* The slot being played, so a game carried out of a session lands in the
     * folder the player will next press PLAY OFFLINE on. */
    mmo_launch_save_dir(port_exe, slot, x->dir, sizeof x->dir);
    snprintf(x->save, sizeof x->save, "%s/export-%ld.sav", x->dir, pid);
    snprintf(x->mark, sizeof x->mark, "%s.ok", x->save);
    /* sessions/ as well as save/, because the anchor lands beside the session
     * records and adopting is not the moment to discover the folder is not
     * there. The deeper of the two makes both. */
    snprintf(seed, sizeof seed, "%s/sessions/x", x->dir);
    if (mkdir_parents(seed) != 0) {
        fail(err, errcap, 0, "the save folder could not be made:", x->dir);
        memset(x, 0, sizeof *x);
        return -1;
    }
    /*
     * The HANDOFF must not exist when the game starts, and that is checked rather than
     * assumed.
     */
    snprintf(tmp, sizeof tmp, "%s.tmp", x->save);
    if (!cleared(x->save) || !cleared(tmp) || !cleared(x->mark)) {
        fail(err, errcap, 0, "an old export is in the way and would be played"
             " as a save:", x->save);
        memset(x, 0, sizeof *x);
        return -1;
    }
    return 0;
}

/* `character <name>` out of the marker, printable characters only and no
 * longer than a name can be. The marker is written by the game and read here
 * into a record other programs parse, so a newline in it would forge a field. */
/*
 * One `<key> <integer>` line of a record. 0 and `*out` when the line is there, 1 when the
 * record has no such line, -1 when the record could not be opened at all.
 */
static int record_number(const char *path, const char *key, long *out)
{
    char line[256];
    size_t klen = strlen(key);
    int got = 1;
    FILE *f = fopen(path, "rb");

    if (f == NULL)
        return -1;
    while (fgets(line, sizeof line, f) != NULL) {
        if (strncmp(line, key, klen) != 0 || line[klen] != ' ')
            continue;
        if (out != NULL)
            *out = strtol(line + klen + 1, NULL, 10);
        got = 0;
        break;
    }
    /* A read that stopped on an error read a record nobody can vouch for: the
     * line wanted may be the one the error is sitting on. */
    if (ferror(f))
        got = -1;
    fclose(f);
    return got;
}

static void marker_character(const char *path, char *out, size_t cap)
{
    char line[256];
    const char *at;
    size_t n = 0;
    FILE *f;

    out[0] = '\0';
    f = fopen(path, "rb");
    if (f == NULL)
        return;
    while (fgets(line, sizeof line, f) != NULL) {
        if (strncmp(line, "character ", 10) != 0)
            continue;
        for (at = line + 10; *at != '\0' && n + 1 < cap && n < 24; at++) {
            if (*at >= 0x20 && *at < 0x7F)
                out[n++] = *at;
        }
        out[n] = '\0';
        break;
    }
    fclose(f);
}

/* The newest `sessions/<stamp>.export` in `dir`, or an empty string. The stamp
 * is fixed-width and big-endian by construction, so its own order is the
 * clock's. */
static void newest_export(const char *dir, char *out, size_t cap)
{
    char sessions[MMO_LAUNCH_PATH + 16];
    char best[MMO_LAUNCH_STAMP];
    DIR *d;
    struct dirent *e;

    out[0] = '\0';
    best[0] = '\0';
    snprintf(sessions, sizeof sessions, "%s/sessions", dir);
    d = opendir(sessions);
    if (d == NULL)
        return;
    while ((e = readdir(d)) != NULL) {
        char stamp[MMO_LAUNCH_STAMP];
        const char *dot = strrchr(e->d_name, '.');

        if (dot == NULL || strcmp(dot, ".export") != 0)
            continue;
        if ((size_t)(dot - e->d_name) >= sizeof stamp)
            continue;
        memcpy(stamp, e->d_name, (size_t)(dot - e->d_name));
        stamp[dot - e->d_name] = '\0';
        if (!stamp_ok(stamp))
            continue;
        if (best[0] == '\0' || strcmp(stamp, best) > 0)
            snprintf(best, sizeof best, "%s", stamp);
    }
    closedir(d);
    if (best[0] != '\0')
        snprintf(out, cap, "%s/%s.export", sessions, best);
}

/*
 * What the offline game has in its pocket, for a session that is about to write itself out
 * over that game.
 */
static int offline_wallet(const mmo_launch_settings *s, const char *port_exe,
                          char *out, size_t cap)
{
    char dir[MMO_LAUNCH_PATH + 8];
    char path[MMO_LAUNCH_PATH + 32];
    u8 *report = NULL;
    size_t len = 0;
    s32 money = 0;

    if (out == NULL || cap == 0 || s == NULL)
        return -1;
    mmo_launch_save_dir(port_exe, s->slot, dir, sizeof dir);
    snprintf(path, sizeof path, "%s/" SAVE_NAME ".report", dir);
    if (mmo_import_report_read(path, &report, &len) == 0) {
        int ok = mmo_import_report_stamp(report, len, NULL, NULL, &money) == 0;

        free(report);
        if (ok) {
            snprintf(out, cap, "%ld", (long)(money < 0 ? 0 : money));
            return 0;
        }
        return -1;
    }
    free(report);
    snprintf(path, sizeof path, "%s/" SAVE_NAME, dir);
    if (access(path, R_OK) == 0)
        return -1;
    snprintf(out, cap, "%d", MMO_LAUNCH_FRESH_MONEY);
    return 0;
}

int mmo_launch_import_offer(const char *port_exe, int slot,
                            mmo_launch_import *out)
{
    char dir[MMO_LAUNCH_PATH + 8];
    char anchor[MMO_LAUNCH_PATH + MMO_LAUNCH_STAMP + 32];
    char foreign[MMO_LAUNCH_PATH + 32];
    u8 *report = NULL;
    size_t len = 0;
    s32 seconds = 0;

    if (out == NULL)
        return 0;
    memset(out, 0, sizeof *out);
    out->server_seconds = -1;
    mmo_launch_save_dir(port_exe, slot, dir, sizeof dir);
    snprintf(out->report, sizeof out->report, "%s/" SAVE_NAME ".report", dir);
    if (mmo_import_report_read(out->report, &report, &len) != 0) {
        out->report[0] = '\0';
        return 0;
    }
    if (mmo_import_report_stamp(report, len, &seconds, NULL, NULL) != 0) {
        free(report);
        out->report[0] = '\0';
        return 0;
    }
    free(report);
    out->save_seconds = seconds;

    /*
     * Read before the anchor, and independently of it: a bundle carries the anchor it left on,
     * so a save that came from elsewhere usually has one and is compared against it exactly as
     * a save played here would be.
     */
    snprintf(foreign, sizeof foreign, "%s/" ELSEWHERE_NAME, dir);
    if (access(foreign, R_OK) == 0) {
        out->from_elsewhere = 1;
        marker_character(foreign, out->from, sizeof out->from);
    }

    newest_export(dir, anchor, sizeof anchor);
    if (anchor[0] != '\0') {
        long played = -1;

        switch (record_number(anchor, "play-time", &played)) {
        case 0:
            out->server_seconds = played;
            break;
        case 1:
            snprintf(out->unsure, sizeof out->unsure,
                     "the record of the game this save came from does not say"
                     " how long it had been played, so this front door cannot"
                     " tell whether the offline save is the newer of the two;"
                     " it is not being offered");
            return 0;
        default:
            snprintf(out->unsure, sizeof out->unsure,
                     "the record of the game this save came from could not be"
                     " read, so this front door cannot tell whether the"
                     " offline save is the newer of the two; it is not being"
                     " offered");
            return 0;
        }
    }

    /* Nothing came from a session: this save is a New Game played offline and
     * there is no server copy of it to be older or newer than. Offer it. Only
     * a missing marker says that, an unreadable one was refused above. */
    if (out->server_seconds < 0)
        return 1;
    /* The save has been played since the server handed it over. Offer it, and
     * say by how much. Equal is not newer: pressing play straight after
     * "continue offline" must not propose replacing a character with itself. */
    return out->save_seconds > out->server_seconds ? 1 : 0;
}

int mmo_launch_import_landed(const char *report)
{
    char landed[MMO_LAUNCH_PATH + 16];

    if (report == NULL || report[0] == '\0')
        return 0;
    if ((size_t)snprintf(landed, sizeof landed, "%s.landed", report) >=
        sizeof landed)
        return 0;
    return access(landed, F_OK) == 0;
}

/* One `<key> <value>` line of a record as text, or an empty string. The value
 * is taken verbatim up to the newline; nothing here interprets it. */
static void record_text(const char *path, const char *key, char *out,
                        size_t cap)
{
    char line[512];
    size_t klen = strlen(key);
    FILE *f;

    out[0] = '\0';
    f = fopen(path, "rb");
    if (f == NULL)
        return;
    while (fgets(line, sizeof line, f) != NULL) {
        size_t n;

        if (strncmp(line, key, klen) != 0 || line[klen] != ' ')
            continue;
        snprintf(out, cap, "%s", line + klen + 1);
        n = strlen(out);
        while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r'))
            out[--n] = '\0';
        break;
    }
    fclose(f);
}

/* A whole file into a NUL-terminated buffer, refusing one past `cap`. Returns
 * the byte count, or -1. `*out` is malloc'd on success. */
static long slurp(const char *path, u8 **out, size_t cap)
{
    FILE *f;
    long size;
    u8 *buf;
    size_t got;

    *out = NULL;
    f = fopen(path, "rb");
    if (f == NULL)
        return -1;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0 || (unsigned long)size > cap) {
        fclose(f);
        return -1;
    }
    buf = (u8 *)malloc((size_t)size + 1u);
    if (buf == NULL) {
        fclose(f);
        return -1;
    }
    got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) {
        free(buf);
        return -1;
    }
    buf[got] = '\0';
    *out = buf;
    return (long)got;
}

/* The stamps of the `sessions/<stamp>.link` records in `dir`, oldest first and
 * only those after `since` (an empty string takes them all). */
/* One session record, reduced to the two hashes the walk below follows and
 * the stamp that names it. `boot` is "none" for a session that started from no
 * file at all; `quit` is "none" for one that left no save, which a crash does
 * and which no walk can pass through. */
typedef struct {
    char stamp[MMO_LAUNCH_STAMP];
    char boot[65];
    char quit[65];
} chain_step;

/* Every `sessions/<stamp>.link` in the folder, newest first. Unreadable
 * records are carried with their hashes as "none": a record that cannot be
 * read is one the walk will not pass through, which is the same answer as one
 * that does not join up, and the sentence the caller writes is the same. */
static int session_steps(const char *dir, chain_step *out, int max)
{
    char sessions[MMO_LAUNCH_PATH + 16];
    char path[MMO_LAUNCH_PATH + MMO_LAUNCH_STAMP + 32];
    DIR *d;
    struct dirent *e;
    int n = 0, i, j;

    snprintf(sessions, sizeof sessions, "%s/sessions", dir);
    d = opendir(sessions);
    if (d == NULL)
        return 0;
    while ((e = readdir(d)) != NULL && n < max) {
        char stamp[MMO_LAUNCH_STAMP];
        const char *dot = strrchr(e->d_name, '.');

        if (dot == NULL || strcmp(dot, ".link") != 0)
            continue;
        if ((size_t)(dot - e->d_name) >= sizeof stamp)
            continue;
        memcpy(stamp, e->d_name, (size_t)(dot - e->d_name));
        stamp[dot - e->d_name] = '\0';
        if (!stamp_ok(stamp))
            continue;
        snprintf(out[n].stamp, sizeof out[n].stamp, "%s", stamp);
        snprintf(path, sizeof path, "%s/%s", sessions, e->d_name);
        record_text(path, "boot-sha256", out[n].boot, sizeof out[n].boot);
        record_text(path, "quit-sha256", out[n].quit, sizeof out[n].quit);
        if (out[n].boot[0] == '\0')
            snprintf(out[n].boot, sizeof out[n].boot, "none");
        if (out[n].quit[0] == '\0')
            snprintf(out[n].quit, sizeof out[n].quit, "none");
        n++;
    }
    closedir(d);
    /* Newest first. The stamp is fixed-width and big-endian by construction,
     * so its own order is the clock's. */
    for (i = 1; i < n; i++) {
        chain_step hold = out[i];

        for (j = i; j > 0 && strcmp(out[j - 1].stamp, hold.stamp) < 0; j--)
            out[j] = out[j - 1];
        out[j] = hold;
    }
    return n;
}

/* 1 when this folder holds an export marker whose image hashed to `hash`. The
 * markers are the exports this front door adopted, so the hash in one is the
 * hash of a file the server handed over and kept a copy of. */
static int export_with_hash(const char *dir, const char *hash, char *out,
                            size_t cap)
{
    char sessions[MMO_LAUNCH_PATH + 16];
    char path[MMO_LAUNCH_PATH + MMO_LAUNCH_STAMP + 32];
    char mine[MMO_LAUNCH_TEXT];
    DIR *d;
    struct dirent *e;
    int found = 0;

    if (out != NULL && cap > 0)
        out[0] = '\0';
    if (hash == NULL || strlen(hash) != 64)
        return 0;
    snprintf(sessions, sizeof sessions, "%s/sessions", dir);
    d = opendir(sessions);
    if (d == NULL)
        return 0;
    while (!found && (e = readdir(d)) != NULL) {
        const char *dot = strrchr(e->d_name, '.');

        if (dot == NULL || strcmp(dot, ".export") != 0)
            continue;
        snprintf(path, sizeof path, "%s/%s", sessions, e->d_name);
        record_text(path, "export-sha256", mine, sizeof mine);
        found = strcmp(mine, hash) == 0;
        if (found && out != NULL && cap > 0)
            snprintf(out, cap, "%s", path);
    }
    closedir(d);
    return found;
}

/*
 * The sessions that actually produced the save being offered, newest first, and the hash the
 * oldest of them booted from.
 */
/* The walk runs backwards; a chain is read forwards. */
static void reverse_stamps(char stamps[][MMO_LAUNCH_STAMP], int n)
{
    int i;

    for (i = 0; i < n / 2; i++) {
        char hold[MMO_LAUNCH_STAMP];

        snprintf(hold, sizeof hold, "%s", stamps[i]);
        snprintf(stamps[i], MMO_LAUNCH_STAMP, "%s", stamps[n - 1 - i]);
        snprintf(stamps[n - 1 - i], MMO_LAUNCH_STAMP, "%s", hold);
    }
}

static int chain_walk(const char *dir, const char *from, chain_step *steps,
                      int nsteps, char out[][MMO_LAUNCH_STAMP], int max,
                      char anchor[65])
{
    char cur[65];
    int n = 0;

    snprintf(cur, sizeof cur, "%s", from);
    for (;;) {
        int pick = -1, i;

        if (strcmp(cur, "none") == 0 ||
            export_with_hash(dir, cur, NULL, 0)) {
            snprintf(anchor, 65, "%s", cur);
            return n;
        }
        for (i = 0; i < nsteps; i++) {
            if (strcmp(steps[i].quit, cur) != 0)
                continue;
            /* Strictly older than the step it feeds, so the walk always moves
             * backwards through the folder and cannot revisit a record. */
            if (n > 0 && strcmp(steps[i].stamp, out[n - 1]) >= 0)
                continue;
            /* The newest that qualifies: two sessions that left the same file
             * are the same evidence, and the nearer one is the shorter walk. */
            if (pick < 0 || strcmp(steps[i].stamp, steps[pick].stamp) > 0)
                pick = i;
        }
        if (pick < 0 || n >= max)
            return -1;
        snprintf(out[n], MMO_LAUNCH_STAMP, "%s", steps[pick].stamp);
        n++;
        snprintf(cur, sizeof cur, "%s", steps[pick].boot);
    }
}

int mmo_launch_chain_collect(const char *port_exe, int slot, char *out,
                             size_t outcap, char *err, size_t errcap)
{
    char dir[MMO_LAUNCH_PATH + 8];
    char want[65], found[65];
    char path[MMO_LAUNCH_PATH + MMO_LAUNCH_STAMP + 32];
    char tmp[MMO_LAUNCH_PATH + MMO_LAUNCH_STAMP + 32];
    char (*stamps)[MMO_LAUNCH_STAMP];
    chain_step *steps;
    mmo_chain chain;
    mmo_wbuf w;
    FILE *f;
    size_t wrote;
    int n, nsteps, i, bad, rc = -1;

    if (out != NULL && outcap > 0)
        out[0] = '\0';
    if (port_exe == NULL || port_exe[0] == '\0' || out == NULL)
        return -1;
    memset(&chain, 0, sizeof chain);
    mmo_launch_save_dir(port_exe, slot, dir, sizeof dir);

    /*
     * Where the chain starts and which sessions are in it, walked back by hash from the file
     * that is about to be offered (chain_walk above). The file itself is the one thing here
     * that is not somebody's word about a file, so it is what the walk starts from.
     */
    snprintf(path, sizeof path, "%s/" SAVE_NAME, dir);
    hash_or_none(path, want);
    if (strcmp(want, "none") == 0)
        return 0;   /* no saved game in this slot: nothing to gather */

    steps = (chain_step *)calloc((size_t)MMO_CHAIN_MAX_LINKS, sizeof *steps);
    stamps = (char (*)[MMO_LAUNCH_STAMP])
        malloc((size_t)MMO_CHAIN_MAX_LINKS * MMO_LAUNCH_STAMP);
    if (steps == NULL || stamps == NULL) {
        free(steps);
        free(stamps);
        return -1;
    }
    nsteps = session_steps(dir, steps, MMO_CHAIN_MAX_LINKS);
    n = chain_walk(dir, want, steps, nsteps, stamps, MMO_CHAIN_MAX_LINKS,
                   found);
    free(steps);
    if (n < 0) {
        /* The sessions in this folder do not reach the file being offered. */
        free(stamps);
        fail(err, errcap, 0, "the sessions written down beside this saved game"
             " are not the play that produced it, so there is nothing to check"
             " it against; the save still comes online, unchecked", NULL);
        return -1;
    }
    if (n == 0) {
        /* The file is an export this front door adopted: the server's copy and
         * this one are the same thing and nothing has been played offline
         * since. Not an error, and nothing to offer. */
        free(stamps);
        return 0;
    }
    snprintf(chain.anchor, sizeof chain.anchor, "%s", found);
    reverse_stamps(stamps, n);

    chain.links = (mmo_chain_link *)calloc((size_t)n, sizeof *chain.links);
    if (chain.links == NULL) {
        free(stamps);
        return -1;
    }
    chain.nlinks = n;

    for (i = 0; i < n; i++) {
        u8 *text = NULL;
        long len;

        snprintf(path, sizeof path, "%s/sessions/%s.link", dir, stamps[i]);
        len = slurp(path, &text, MMO_CHAIN_MAX_BYTES);
        if (len < 0) {
            fail(err, errcap, 0, "a session record could not be read:", path);
            goto done;
        }
        chain.links[i].record = (char *)text;
        /* The recording the record names, not one guessed from the stamp: if
         * the two ever disagree the record is what the server reads, so it is
         * what this follows. A session that left none goes up without one and
         * the server says so; leaving it out would hide a gap in the chain. */
        {
            char named[MMO_LAUNCH_TEXT];

            record_text(path, "recording", named, sizeof named);
            if (named[0] != '\0' && strstr(named, "..") == NULL &&
                named[0] != '/') {
                u8 *input = NULL;

                snprintf(tmp, sizeof tmp, "%s/%s", dir, named);
                len = slurp(tmp, &input, MMO_CHAIN_MAX_BYTES);
                if (len > 0) {
                    chain.links[i].input = input;
                    chain.links[i].input_len = (size_t)len;
                } else {
                    free(input);
                }
            }
        }
    }

    mmo_wbuf_init(&w);
    if (mmo_chain_encode(&w, &chain) != 0) {
        fail(err, errcap, 0, "this run is too long to send for checking; the"
             " save still comes online", NULL);
        mmo_wbuf_free(&w);
        goto done;
    }
    snprintf(path, sizeof path, "%s/" SAVE_NAME ".chain", dir);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (f == NULL) {
        fail(err, errcap, 0, "the session records could not be gathered at:",
             tmp);
        mmo_wbuf_free(&w);
        goto done;
    }
    wrote = fwrite(w.data, 1, w.len, f);
    bad = mmo_plat_fsync(f) != 0;
    if (fclose(f) != 0 || bad || wrote != w.len) {
        fail(err, errcap, 0, "the session records did not reach:", tmp);
        remove(tmp);
        mmo_wbuf_free(&w);
        goto done;
    }
    mmo_wbuf_free(&w);
    if (mmo_plat_rename_over(tmp, path) != 0 && copy_file(tmp, path) != 0) {
        fail(err, errcap, 0, "the session records could not be put at:", path);
        remove(tmp);
        goto done;
    }
    remove(tmp);
    snprintf(out, outcap, "%s", path);
    rc = 1;

done:
    free(stamps);
    mmo_chain_free(&chain);
    return rc;
}

int mmo_launch_plan_build_session(const mmo_launch_settings *s,
                                  const char *port_exe, const char *view_exe,
                                  const char *chan, long pid,
                                  int take_save_online,
                                  void (*note)(void *ud, const char *line),
                                  void (*warn)(void *ud, const char *line),
                                  void *ud, mmo_launch_export *x,
                                  mmo_launch_plan *p, char *err, size_t errcap)
{
    /* A name that will not clear is said and played through without an export
     * rather than played with one that might be an old image: the session is
     * what the player pressed Play for, and Continue Offline can wait for a
     * launch that could tidy up after the last one. */
    if (mmo_launch_export_open(port_exe, s->slot, pid, x, err, errcap) != 0) {
        if (warn != NULL)
            warn(ud, err);
        memset(x, 0, sizeof *x);
    }
    /* The offline save, when it is newer than the copy the server was last handed. */
    if (take_save_online) {
        mmo_launch_import offer;

        if (mmo_launch_import_offer(port_exe, s->slot, &offer)) {
            char chain[MMO_LAUNCH_PATH];
            /* Cleared first: a collect that found nothing to gather leaves
             * this alone, and the test below reads it either way. */
            char cerr[MMO_LAUNCH_TEXT] = "";

            snprintf(x->import, sizeof x->import, "%s", offer.report);
            if (note != NULL)
                note(ud, "your offline save goes online with this session");
            /* And the sessions behind it, so the server can check the save
             * against the play that produced it. Never a reason not to offer
             * the save: without them it lands the same way and stays marked,
             * which is where every imported save starts. */
            if (mmo_launch_chain_collect(port_exe, s->slot, chain,
                                         sizeof chain, cerr,
                                         sizeof cerr) == 1)
                snprintf(x->chain, sizeof x->chain, "%s", chain);
            else if (cerr[0] != '\0' && warn != NULL)
                warn(ud, cerr);
        } else if (warn != NULL) {
            /* Two different noes. One is "the server already has this", which
             * is the ordinary answer; the other is this front door declining
             * to guess which side is newer, and a player owed the difference
             * between them is owed the sentence that says which it was. */
            warn(ud, offer.unsure[0] != '\0' ? offer.unsure
                     : "there is no offline save newer than the one this"
                       " character was last given, so none is being offered");
        }
    }
    /* Both names ride the same plan, so a handoff that could not be named
     * takes the offer down with it. Said rather than dropped quietly: a player
     * who answered the box is owed the difference between "your save is going
     * up" and nothing happening. */
    if (x->save[0] == '\0' && x->import[0] != '\0') {
        if (warn != NULL)
            warn(ud, "your offline save is not going online with this session:"
                     " it has nowhere to write itself out to");
        x->import[0] = '\0';
        x->chain[0] = '\0';
    }
    if (x->save[0] != '\0')
        return mmo_launch_plan_build_export(s, port_exe, view_exe, chan, pid,
                                            x, p, err, errcap);
    return mmo_launch_plan_build(s, port_exe, view_exe, chan, pid, p, err,
                                 errcap);
}

/* The adopt itself, with the live save already claimed. */
static int export_adopt_locked(const mmo_launch_export *x, const char *wall,
                               int revision, char *stamp, size_t stampcap,
                               char *err, size_t errcap)
{
    char live[MMO_LAUNCH_PATH + 16];
    char stale[MMO_LAUNCH_PATH + 32];
    char kept[MMO_LAUNCH_PATH + MMO_LAUNCH_STAMP + 16];
    char anchor[MMO_LAUNCH_PATH + MMO_LAUNCH_STAMP + 32];
    char now[MMO_LAUNCH_TEXT];
    char who[MMO_LAUNCH_TEXT];
    char mine[MMO_LAUNCH_STAMP];
    char hash[65];
    long played;
    FILE *f;

    if (stamp != NULL && stampcap > 0)
        stamp[0] = '\0';
    if (x == NULL || x->save[0] == '\0')
        return 0;
    /* No marker, nothing asked for. An image with no marker is what a script
     * that saved inside the session left behind, and it is not an export. */
    if (access(x->mark, R_OK) != 0)
        return 0;
    if (access(x->save, R_OK) != 0) {
        fail(err, errcap, 0, "the game asked to carry on offline but left no"
             " save image at:", x->save);
        return -1;
    }
    if (wall == NULL || wall[0] == '\0') {
        mmo_plat_stamp(now, sizeof now);
        wall = now;
    }
    if (stamp_compact(wall, mine, sizeof mine) != 0) {
        fail(err, errcap, 0, "this host's clock did not read as a date:", wall);
        return -1;
    }
    marker_character(x->mark, who, sizeof who);
    /* What the game said its clock read when it wrote the image. This is the
     * server side of "which side is newer": the moment the two copies of this
     * character were last the same thing. */
    if (record_number(x->mark, "play-time", &played) != 0)
        played = -1;
    snprintf(live, sizeof live, "%s/" SAVE_NAME, x->dir);
    /* Keep what is being replaced first. A player who has been playing offline
     * and then brings a session home has one restore to undo this, the same
     * way the restore row itself does. */
    if (access(live, R_OK) == 0) {
        snprintf(kept, sizeof kept, "%s.%s", live, mine);
        if (strcmp(kept, live) != 0 && copy_file(live, kept) != 0) {
            fail(err, errcap, 0, "the saved game being replaced could not be"
                 " kept, so nothing was replaced:", kept);
            return -1;
        }
    }
    if (mmo_plat_rename_over(x->save, live) != 0 &&
        copy_file(x->save, live) != 0) {
        fail(err, errcap, 0, "the exported game could not be put in place:",
             live);
        return -1;
    }
    /*
     * The report an offline session wrote is about the image that has just been replaced,
     * that file's hash and that session's play time, neither of which describes what is now at
     * this name.
     */
    side_keep(live, mine);
    /* And the note that an earlier game was put back, for the same reason: it
     * is about a file that is no longer at this name. Left standing it would
     * refuse the chain behind every session played from here on. */
    snprintf(stale, sizeof stale, "%s.restore", live);
    remove(stale);
    /* And the note that the game at this name came from another machine. The
     * game at this name is now one this server just handed over. */
    snprintf(stale, sizeof stale, "%s/" ELSEWHERE_NAME, x->dir);
    remove(stale);
    prune_images(x->dir, MMO_LAUNCH_SAVE_KEEP);
    /* The anchor: what an offline session booting from this image started
     * from. Its hash is the file's own, read back after it landed rather than
     * taken from the handoff, so what is written down is what is on disk. */
    hash_or_none(live, hash);
    snprintf(anchor, sizeof anchor, "%s/sessions/%s.export", x->dir, mine);
    f = fopen(anchor, "wb");
    if (f == NULL) {
        fail(err, errcap, 0, "the export could not be written down:", anchor);
        return -1;
    }
    fprintf(f, "version %d\n", LINK_VERSION);
    if (revision > 0)
        fprintf(f, "revision %d\n", revision);
    else
        fprintf(f, "revision unknown\n");
    fprintf(f, "rtc %s\n", wall);
    fprintf(f, "export-sha256 %s\n", hash);
    if (who[0] != '\0')
        fprintf(f, "character %s\n", who);
    if (played >= 0)
        fprintf(f, "play-time %ld\n", played);
    if (fclose(f) != 0) {
        fail(err, errcap, 0, "the export could not be written down:", anchor);
        return -1;
    }
    if (stamp != NULL && stampcap > 0)
        snprintf(stamp, stampcap, "%s", mine);
    return 1;
}

int mmo_launch_export_adopt(const mmo_launch_export *x, const char *wall,
                            int revision, char *stamp, size_t stampcap,
                            char *err, size_t errcap)
{
    char lock[MMO_LAUNCH_PATH + 24];
    mmo_plat_lock *held = NULL;
    int rc;

    if (stamp != NULL && stampcap > 0)
        stamp[0] = '\0';
    /*
     * Asked at all before the save is claimed. Most sessions carry nothing out, and a row that
     * is going to do nothing must not refuse itself against a game somebody is in the middle
     * of playing, nor take the save away from one about to start.
     */
    if (x == NULL || x->save[0] == '\0')
        return 0;
    if (access(x->mark, R_OK) != 0)
        return 0;
    /* Said rather than assumed: the folder is where both the save and the
     * lock beside it are named from, and an export with an image and no
     * folder would otherwise claim a file at the root of the disk. */
    if (x->dir[0] == '\0') {
        fail(err, errcap, 0, "the game asked to carry on offline but named no"
             " folder to put the save in", NULL);
        return -1;
    }
    save_lock_path(x->dir, lock, sizeof lock);
    switch (mmo_plat_lock_take(lock, &held)) {
    case 0:
        break;
    case 1:
        /*
         * An offline session is playing this file. Replacing it underneath would leave that
         * session writing over a game it never loaded, and both halves keeping backups of the
         * other's.
         */
        fail(err, errcap, 0, "another launcher has this saved game open; close"
             " that game and the game you just played is picked up at the next"
             " launch", NULL);
        return -1;
    default:
        fail(err, errcap, 0, "this saved game could not be claimed for the"
             " export:", lock);
        return -1;
    }
    rc = export_adopt_locked(x, wall, revision, stamp, stampcap, err, errcap);
    mmo_plat_lock_drop(held);
    return rc;
}

void mmo_launch_export_close(const mmo_launch_export *x)
{
    char tmp[MMO_LAUNCH_PATH + 8];

    if (x == NULL || x->save[0] == '\0')
        return;
    snprintf(tmp, sizeof tmp, "%s.tmp", x->save);
    remove(x->mark);
    remove(tmp);
    remove(x->save);
}

int mmo_launch_export_settle(const mmo_launch_export *x, int adopted)
{
    /*
     * A refused adopt that left the image where the game wrote it is the one case the pair
     * does not go.
     */
    if (x != NULL && adopted < 0 && x->save[0] != '\0'
        && access(x->save, R_OK) == 0)
        return 1;
    mmo_launch_export_close(x);
    return 0;
}

/*
 * Eight handoffs in one pass. A ninth is not lost, only late: the marker is
 * still beside its image and the next launch is another sweep.
 */
#define EXPORT_SWEEP_MAX 8

/* One slot's handoffs, with that slot's own save claimed for the pass. */
static int sweep_dir(const char *dir, int revision,
                     void (*note)(void *ud, const char *line), void *ud)
{
    struct {
        char name[64];
        long long when;
    } marks[EXPORT_SWEEP_MAX], hold;
    char err[MMO_LAUNCH_TEXT];
    char stamp[MMO_LAUNCH_STAMP];
    char lock[MMO_LAUNCH_PATH + 24];
    mmo_plat_lock *held = NULL;
    DIR *d;
    struct dirent *e;
    int n = 0, i, j, landed = 0;

    /* Names first, all of them: the adopt renames and the settle removes, and
     * a directory being read is not one to change under the reader. */
    d = opendir(dir);
    if (d == NULL)
        return 0;
    while ((e = readdir(d)) != NULL && n < EXPORT_SWEEP_MAX) {
        char path[MMO_LAUNCH_PATH + 72];
        struct stat st;
        size_t len = strlen(e->d_name);

        /* export-<something>.sav.ok, with the something there: the marker is
         * what says an image is a handoff, and the image is its name less the
         * three characters that make it one. */
        if (strncmp(e->d_name, "export-", 7) != 0 || len < 15
            || strcmp(e->d_name + len - 7, ".sav.ok") != 0
            || len >= sizeof marks[0].name)
            continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        snprintf(marks[n].name, sizeof marks[n].name, "%s", e->d_name);
        marks[n].when = stat(path, &st) == 0 ? (long long)st.st_mtime : 0;
        n++;
    }
    closedir(d);
    /*
     * Oldest first, by when the game wrote the marker. The last one adopted is the one play
     * OFFLINE ends up with, so the order has to be the clock's: the name carries a process id,
     * which is nobody's order, and readdir carries none at all.
     */
    for (i = 1; i < n; i++) {
        hold = marks[i];
        for (j = i; j > 0 && (marks[j - 1].when > hold.when
                              || (marks[j - 1].when == hold.when
                                  && strcmp(marks[j - 1].name, hold.name) > 0));
             j--)
            marks[j] = marks[j - 1];
        marks[j] = hold;
    }
    /*
     * sessions/, before anything moves. The adopt puts the image in place and only then writes
     * the anchor beside it, so a missing folder is an adopt that refuses with the image
     * already gone, a save with no record of where it came from, and nothing said.
     */
    if (n > 0) {
        char seed[MMO_LAUNCH_PATH + 24];

        snprintf(seed, sizeof seed, "%s/sessions/x", dir);
        if (mkdir_parents(seed) != 0) {
            if (note != NULL) {
                char line[MMO_LAUNCH_PATH + 96];

                snprintf(line, sizeof line, "a game played earlier is waiting"
                         " in %s, and the folder to write it down in could"
                         " not be made", dir);
                note(ud, line);
            }
            return 0;
        }
        /* And the live save, claimed once for the whole pass rather than once per handoff. */
        save_lock_path(dir, lock, sizeof lock);
        if (mmo_plat_lock_take(lock, &held) != 0) {
            /* Nothing has moved and nothing is thrown away: the markers are
             * still beside their images and the next launch sweeps again. */
            if (note != NULL) {
                char line[MMO_LAUNCH_PATH + 160];

                snprintf(line, sizeof line, "a game played earlier is waiting"
                         " in %s, and this saved game is open in another"
                         " launcher; close that game and it is picked up at"
                         " the next launch", dir);
                note(ud, line);
            }
            return 0;
        }
    }
    for (i = 0; i < n; i++) {
        mmo_launch_export x;
        int rc;

        memset(&x, 0, sizeof x);
        snprintf(x.dir, sizeof x.dir, "%s", dir);
        snprintf(x.mark, sizeof x.mark, "%s/%s", dir, marks[i].name);
        snprintf(x.save, sizeof x.save, "%.*s", (int)(strlen(x.mark) - 3),
                 x.mark);
        err[0] = '\0';
        rc = export_adopt_locked(&x, NULL, revision, stamp, sizeof stamp,
                                 err, sizeof err);
        if (rc > 0)
            landed++;
        else if (rc < 0 && note != NULL)
            note(ud, err);
        /* Kept again, and that is the whole point of keeping it: the marker is
         * what this sweep looks for and it stays beside the image, so the
         * launch after this one asks once more. */
        if (mmo_launch_export_settle(&x, rc) && note != NULL) {
            char line[MMO_LAUNCH_PATH + 96];

            snprintf(line, sizeof line, "a game played earlier is still at %s,"
                     " and nothing has been thrown away", x.save);
            note(ud, line);
        }
    }
    mmo_plat_lock_drop(held);
    return landed;
}

int mmo_launch_export_sweep(const char *port_exe, int revision,
                            void (*note)(void *ud, const char *line), void *ud)
{
    char root[MMO_LAUNCH_PATH];
    char dir[MMO_LAUNCH_PATH + 8];
    int slot, landed = 0;

    if (port_exe == NULL || port_exe[0] == '\0')
        return 0;
    mmo_launch_install_root(port_exe, root, sizeof root);
    /* Every slot, and each one on its own lock. */
    for (slot = 1; slot <= MMO_LAUNCH_SLOTS; slot++) {
        mmo_launch_slot_dir(root, slot, dir, sizeof dir);
        landed += sweep_dir(dir, revision, note, ud);
    }
    return landed;
}

/* ------------------------------------------------------------------ */
/* Carrying a saved game to another machine                            */
/* ------------------------------------------------------------------ */

/*
 * Bytes to a file, through a sibling temp that is renamed over it, so a write interrupted half
 * way leaves the destination as it was. The same arrangement copy_file above uses, over a
 * buffer instead of a second file.
 */
static int write_bytes(const char *path, const void *data, size_t len)
{
    char tmp[MMO_LAUNCH_PATH + 8];
    FILE *f;
    int bad = 0;

    if ((size_t)snprintf(tmp, sizeof tmp, "%s.tmp", path) >= sizeof tmp)
        return -1;
    f = fopen(tmp, "wb");
    if (f == NULL)
        return -1;
    if (len > 0 && fwrite(data, 1, len, f) != len)
        bad = 1;
    /* A saved game is worth nothing in the host's cache. */
    if (!bad && mmo_plat_fsync(f) != 0)
        bad = 1;
    if (fclose(f) != 0)
        bad = 1;
    if (bad || mmo_plat_rename_over(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}

/* Add one piece to a bundle being built. The bytes are the caller's: nothing
 * here copies or frees them, and mmo_bundle_encode only reads. */
static int bundle_add(mmo_bundle *b, int *cap, u8 kind, const char *name,
                      u8 *data, size_t len)
{
    mmo_bundle_item *grown;

    if (b->nitems >= *cap) {
        int want = (*cap == 0) ? 32 : *cap * 2;

        grown = (mmo_bundle_item *)realloc(b->items, (size_t)want * sizeof *grown);
        if (grown == NULL)
            return -1;
        memset(grown + *cap, 0, (size_t)(want - *cap) * sizeof *grown);
        b->items = grown;
        *cap = want;
    }
    b->items[b->nitems].kind = kind;
    snprintf(b->items[b->nitems].name, MMO_BUNDLE_NAME, "%s",
             name != NULL ? name : "");
    b->items[b->nitems].data = data;
    b->items[b->nitems].len = len;
    b->nitems++;
    return 0;
}

/* Free what bundle_add was given, which the bundle does not own. */
static void bundle_drop_items(mmo_bundle *b)
{
    int i;

    for (i = 0; i < b->nitems; i++)
        free(b->items[i].data);
    free(b->items);
    b->items = NULL;
    b->nitems = 0;
}

/* The stamp of `sessions/<stamp>.export`, or an empty string. */
static void export_stamp(const char *anchor, char *out, size_t cap)
{
    const char *base = mmo_plat_last_sep(anchor);
    const char *dot;

    out[0] = '\0';
    base = (base != NULL) ? base + 1 : anchor;
    dot = strrchr(base, '.');
    if (dot == NULL || (size_t)(dot - base) >= cap)
        return;
    memcpy(out, base, (size_t)(dot - base));
    out[dot - base] = '\0';
    if (!stamp_ok(out))
        out[0] = '\0';
}

/*
 * The bundle's own first lines. Everything in them is for a person reading the file, or for
 * the row that asks "replace this saved game with that one?" before it does; nothing in the
 * unpacking below is decided on any of it.
 */
static void bundle_note(char *out, size_t cap, const char *anchor,
                        int revision, long played)
{
    char wall[MMO_LAUNCH_TEXT];
    char who[MMO_LAUNCH_TEXT];
    char hash[MMO_LAUNCH_TEXT];
    int at = 0;

    out[0] = '\0';
    mmo_plat_stamp(wall, sizeof wall);
    addf(out, cap, &at, "version %d\n", LINK_VERSION);
    if (revision > 0)
        addf(out, cap, &at, "revision %d\n", revision);
    else
        addf(out, cap, &at, "revision unknown\n");
    addf(out, cap, &at, "rtc %s\n", wall);
    addf(out, cap, &at, "written %ld\n", mmo_plat_seconds());
    if (played >= 0)
        addf(out, cap, &at, "play-time %ld\n", played);
    who[0] = '\0';
    hash[0] = '\0';
    if (anchor[0] != '\0') {
        marker_character(anchor, who, sizeof who);
        record_text(anchor, "export-sha256", hash, sizeof hash);
    }
    if (who[0] != '\0')
        addf(out, cap, &at, "character %s\n", who);
    addf(out, cap, &at, "anchor %s\n", hash[0] != '\0' ? hash : "none");
}

int mmo_launch_bundle_write(const char *port_exe, int slot, const char *path,
                            char *err, size_t errcap)
{
    char dir[MMO_LAUNCH_PATH + 8];
    char root[MMO_LAUNCH_PATH];
    char live[MMO_LAUNCH_PATH + 24];
    char lock[MMO_LAUNCH_PATH + 24];
    char anchor[MMO_LAUNCH_PATH + MMO_LAUNCH_STAMP + 32];
    char want[65], found[65];
    char file[MMO_LAUNCH_PATH + MMO_LAUNCH_STAMP + 32];
    char (*stamps)[MMO_LAUNCH_STAMP] = NULL;
    chain_step *steps;
    mmo_plat_lock *held = NULL;
    mmo_bundle b;
    mmo_wbuf w;
    long played = -1;
    int cap = 0, n = 0, i, rc = -1;
    u8 *bytes;
    long len;

    if (port_exe == NULL || port_exe[0] == '\0' || path == NULL
        || path[0] == '\0') {
        fail(err, errcap, 0, "there is nowhere to write the saved game", NULL);
        return -1;
    }
    memset(&b, 0, sizeof b);
    mmo_launch_install_root(port_exe, root, sizeof root);
    mmo_launch_slot_dir(root, slot, dir, sizeof dir);
    snprintf(live, sizeof live, "%s/" SAVE_NAME, dir);
    /* Asked before the save is claimed, so an empty slot refuses without
     * taking the file off a session about to start. */
    if (access(live, R_OK) != 0) {
        fail(err, errcap, 0, "there is no saved game in this slot to carry:",
             live);
        return -1;
    }
    save_lock_path(dir, lock, sizeof lock);
    switch (mmo_plat_lock_take(lock, &held)) {
    case 0:
        break;
    case 1:
        /* Half of a game somebody is in the middle of writing is not a saved
         * game. Nothing is written and nothing is thrown away. */
        fail(err, errcap, 0, "another launcher has this saved game open; close"
             " that game and carry it out after it", NULL);
        return -1;
    default:
        fail(err, errcap, 0, "this saved game could not be claimed to be"
             " carried out:", lock);
        return -1;
    }

    len = slurp(live, &bytes, MMO_EXPORT_IMAGE_MAX_BYTES);
    if (len <= 0) {
        fail(err, errcap, 0, "this saved game could not be read:", live);
        goto done;
    }
    if (bundle_add(&b, &cap, MMO_BUNDLE_IMAGE, "", bytes, (size_t)len) != 0) {
        free(bytes);
        goto done;
    }

    /* What the game wrote about what is in that file. Carrying it means the
     * machine this lands on can offer the save to the server on its first
     * launch, rather than only after it has been played there once. */
    snprintf(file, sizeof file, "%s.report", live);
    len = slurp(file, &bytes, MMO_BUNDLE_MAX_BYTES);
    if (len > 0) {
        u8 *report = NULL;
        size_t rlen = 0;
        s32 seconds = 0;

        if (bundle_add(&b, &cap, MMO_BUNDLE_REPORT, "", bytes,
                       (size_t)len) != 0) {
            free(bytes);
            goto done;
        }
        /*
         * Its play time, read the way the front door reads it, the number the box on the far
         * machine compares against the server's.
         */
        if (mmo_import_report_read(file, &report, &rlen) == 0) {
            if (mmo_import_report_stamp(report, rlen, &seconds, NULL, NULL) == 0)
                played = seconds;
            free(report);
        }
    } else {
        free(bytes);
    }

    /*
     * The anchor, and the sessions that reach this save from it: the evidence for the play
     * behind the file being carried. Without them the save still travels and still comes
     * online, it simply stays marked, which is where every import starts.
     */
    steps = (chain_step *)calloc((size_t)MMO_CHAIN_MAX_LINKS, sizeof *steps);
    stamps = (char (*)[MMO_LAUNCH_STAMP])
        malloc((size_t)MMO_CHAIN_MAX_LINKS * MMO_LAUNCH_STAMP);
    if (steps == NULL || stamps == NULL) {
        free(steps);
        goto done;
    }
    hash_or_none(live, want);
    n = chain_walk(dir, want, steps, session_steps(dir, steps,
                                                   MMO_CHAIN_MAX_LINKS),
                   stamps, MMO_CHAIN_MAX_LINKS, found);
    free(steps);
    if (n < 0)
        n = 0;      /* the save travels without evidence rather than with the
                     * wrong evidence */
    else
        reverse_stamps(stamps, n);
    /*
     * Two markers can matter, and they answer different questions, so both travel whenever
     * they are not the same file:
     */
    {
        char marks[2][MMO_LAUNCH_PATH + MMO_LAUNCH_STAMP + 32];
        int nmarks = 0, m;

        if (n > 0 && export_with_hash(dir, found, anchor, sizeof anchor))
            snprintf(marks[nmarks++], sizeof marks[0], "%s", anchor);
        newest_export(dir, anchor, sizeof anchor);
        if (anchor[0] != '\0' && (nmarks == 0 || strcmp(marks[0], anchor) != 0))
            snprintf(marks[nmarks++], sizeof marks[0], "%s", anchor);
        for (m = 0; m < nmarks; m++) {
            char stamp[MMO_LAUNCH_STAMP];

            export_stamp(marks[m], stamp, sizeof stamp);
            len = slurp(marks[m], &bytes, MMO_BUNDLE_MAX_BYTES);
            if (len > 0 && stamp[0] != '\0') {
                if (bundle_add(&b, &cap, MMO_BUNDLE_ANCHOR, stamp, bytes,
                               (size_t)len) != 0) {
                    free(bytes);
                    goto done;
                }
            } else {
                free(bytes);
            }
        }
    }
    for (i = 0; i < n; i++) {
        snprintf(file, sizeof file, "%s/sessions/%s.link", dir, stamps[i]);
        len = slurp(file, &bytes, MMO_BUNDLE_MAX_BYTES);
        if (len <= 0) {
            free(bytes);
            continue;
        }
        if (bundle_add(&b, &cap, MMO_BUNDLE_LINK, stamps[i], bytes,
                       (size_t)len) != 0) {
            free(bytes);
            goto done;
        }
        snprintf(file, sizeof file, "%s/sessions/%s.inp", dir, stamps[i]);
        len = slurp(file, &bytes, MMO_CHAIN_MAX_BYTES);
        if (len <= 0) {
            /* A session that left no recording travels without one, exactly as
             * it goes up to the server without one. Leaving the record out
             * instead would hide the gap. */
            free(bytes);
            continue;
        }
        if (bundle_add(&b, &cap, MMO_BUNDLE_INPUT, stamps[i], bytes,
                       (size_t)len) != 0) {
            free(bytes);
            goto done;
        }
    }
    /* And the note that an earlier game was put back here. It travels because
     * it is the reason the sessions after it are not the play that produced
     * this save, and a machine that did not know that would offer the chain
     * as though they were. */
    snprintf(file, sizeof file, "%s.restore", live);
    len = slurp(file, &bytes, MMO_BUNDLE_MAX_BYTES);
    if (len > 0) {
        if (bundle_add(&b, &cap, MMO_BUNDLE_RESTORE, "", bytes,
                       (size_t)len) != 0) {
            free(bytes);
            goto done;
        }
    } else {
        free(bytes);
    }

    bundle_note(b.note, sizeof b.note, anchor,
                mmo_feed_installed_revision(root), played);
    mmo_wbuf_init(&w);
    if (mmo_bundle_encode(&w, &b) != 0) {
        fail(err, errcap, 0, "this saved game is too large to carry in one"
             " file", NULL);
        mmo_wbuf_free(&w);
        goto done;
    }
    if (write_bytes(path, w.data, w.len) != 0) {
        fail(err, errcap, 0, "the saved game could not be written to:", path);
        mmo_wbuf_free(&w);
        goto done;
    }
    mmo_wbuf_free(&w);
    rc = 0;

done:
    free(stamps);
    bundle_drop_items(&b);
    mmo_plat_lock_drop(held);
    return rc;
}

/* The one piece of a bundle there is only ever one of, or NULL. */
static const mmo_bundle_item *bundle_one(const mmo_bundle *b, u8 kind)
{
    int i;

    for (i = 0; i < b->nitems; i++) {
        if (b->items[i].kind == kind)
            return &b->items[i];
    }
    return NULL;
}

/* A whole bundle file, read and decoded. 0, or -1 with a sentence. */
static int bundle_open(const char *path, mmo_bundle *out, char *err,
                       size_t errcap)
{
    u8 *blob = NULL;
    long len;

    memset(out, 0, sizeof *out);
    if (path == NULL || path[0] == '\0') {
        fail(err, errcap, 0, "no saved game was chosen", NULL);
        return -1;
    }
    len = slurp(path, &blob, MMO_BUNDLE_MAX_BYTES);
    if (len <= 0) {
        free(blob);
        fail(err, errcap, 0, "that file could not be read, or is larger than a"
             " saved game this launcher writes:", path);
        return -1;
    }
    if (mmo_bundle_decode(blob, (size_t)len, out) != 0) {
        free(blob);
        fail(err, errcap, 0, "that is not a saved game this launcher wrote:",
             path);
        return -1;
    }
    free(blob);
    return 0;
}

int mmo_launch_bundle_look(const char *path, mmo_launch_bundle_info *out,
                           char *err, size_t errcap)
{
    mmo_bundle b;
    int i;

    if (out == NULL)
        return -1;
    memset(out, 0, sizeof *out);
    out->play_seconds = -1;
    out->written = -1;
    if (bundle_open(path, &b, err, errcap) != 0)
        return -1;
    if (bundle_one(&b, MMO_BUNDLE_IMAGE) == NULL) {
        mmo_bundle_free(&b);
        fail(err, errcap, 0, "that file carries no saved game in it:", path);
        return -1;
    }
    mmo_bundle_note_text(&b, "character", out->from, sizeof out->from);
    (void)mmo_bundle_note_number(&b, "play-time", &out->play_seconds);
    (void)mmo_bundle_note_number(&b, "written", &out->written);
    out->has_report = bundle_one(&b, MMO_BUNDLE_REPORT) != NULL;
    out->has_anchor = bundle_one(&b, MMO_BUNDLE_ANCHOR) != NULL;
    for (i = 0; i < b.nitems; i++) {
        if (b.items[i].kind == MMO_BUNDLE_LINK)
            out->sessions++;
    }
    mmo_bundle_free(&b);
    return 0;
}

/* The slot's own session records, gone. */
static void sessions_clear(const char *dir)
{
    char sessions[MMO_LAUNCH_PATH + 32];
    /* A directory entry is up to 255 bytes whatever this expects of it, and
     * the compiler is right to say so: sized for one rather than for the
     * twenty-odd characters the names it acts on actually are. */
    char path[MMO_LAUNCH_PATH + 300];
    DIR *d;
    struct dirent *e;

    snprintf(sessions, sizeof sessions, "%s/sessions", dir);
    d = opendir(sessions);
    if (d == NULL)
        return;
    while ((e = readdir(d)) != NULL) {
        char stamp[MMO_LAUNCH_STAMP];
        const char *dot = strrchr(e->d_name, '.');

        if (dot == NULL)
            continue;
        if (strcmp(dot, ".link") != 0 && strcmp(dot, ".inp") != 0
            && strcmp(dot, ".export") != 0)
            continue;
        if ((size_t)(dot - e->d_name) >= sizeof stamp)
            continue;
        memcpy(stamp, e->d_name, (size_t)(dot - e->d_name));
        stamp[dot - e->d_name] = '\0';
        /* Only the names this program writes. A folder is a place other
         * programs put things too. */
        if (!stamp_ok(stamp))
            continue;
        snprintf(path, sizeof path, "%s/%s", sessions, e->d_name);
        remove(path);
    }
    closedir(d);
}

/* The import, with the slot's save already claimed for it. */
static int bundle_read_locked(const mmo_bundle *b, const char *dir,
                              const char *wall, char *landed, size_t landedcap,
                              char *err, size_t errcap)
{
    const mmo_bundle_item *image = bundle_one(b, MMO_BUNDLE_IMAGE);
    const mmo_bundle_item *item;
    char live[MMO_LAUNCH_PATH + 24];
    char path[MMO_LAUNCH_PATH + MMO_LAUNCH_STAMP + 32];
    char aside[MMO_LAUNCH_STAMP];
    char now[MMO_LAUNCH_TEXT];
    char who[MMO_LAUNCH_TEXT];
    char anchor[MMO_LAUNCH_TEXT];
    char replaced[MMO_LAUNCH_TEXT];
    int i, sessions = 0, missing = 0, had_game;
    FILE *f;

    snprintf(live, sizeof live, "%s/" SAVE_NAME, dir);
    if (image == NULL || image->len == 0) {
        fail(err, errcap, 0, "that file carries no saved game in it", NULL);
        return -1;
    }
    /* A backup chip is half a megabyte. Refusing anything no chip could be is
     * this side's whole opinion on the contents: what the save means is the
     * server's door, and what it is worth offline is the engine's. */
    if (image->len > MMO_EXPORT_IMAGE_MAX_BYTES) {
        fail(err, errcap, 0, "the game in that file is larger than any backup"
             " chip", NULL);
        return -1;
    }
    /* Every name checked before anything moves. A stamp that is not one would
     * otherwise be found out half way through, with the save already replaced.
     */
    for (i = 0; i < b->nitems; i++) {
        const mmo_bundle_item *it = &b->items[i];

        if (it->kind != MMO_BUNDLE_ANCHOR && it->kind != MMO_BUNDLE_LINK
            && it->kind != MMO_BUNDLE_INPUT)
            continue;
        if (!stamp_ok(it->name)) {
            fail(err, errcap, 0, "a session in that file is filed under a name"
                 " that is not a date:", it->name);
            return -1;
        }
    }
    if (wall == NULL || wall[0] == '\0') {
        mmo_plat_stamp(now, sizeof now);
        wall = now;
    }
    if (stamp_compact(wall, aside, sizeof aside) != 0) {
        fail(err, errcap, 0, "this host's clock did not read as a date:", wall);
        return -1;
    }

    /* Whose game is about to be replaced, read before it is. */
    replaced[0] = '\0';
    had_game = access(live, R_OK) == 0;
    if (had_game) {
        char mark[MMO_LAUNCH_PATH + MMO_LAUNCH_STAMP + 32];

        newest_export(dir, mark, sizeof mark);
        if (mark[0] != '\0')
            marker_character(mark, replaced, sizeof replaced);
        if (replaced[0] == '\0') {
            snprintf(mark, sizeof mark, "%s/" ELSEWHERE_NAME, dir);
            if (access(mark, R_OK) == 0)
                marker_character(mark, replaced, sizeof replaced);
        }
    }

    /* The game being replaced, kept first and under its own stamp, so an
     * import of the wrong file is one press of "restore an earlier save" to
     * undo. Exactly what a restore does, for exactly that reason. */
    if (access(live, R_OK) == 0) {
        snprintf(path, sizeof path, "%s.%s", live, aside);
        if (copy_file(live, path) != 0) {
            fail(err, errcap, 0, "the game being replaced could not be kept, so"
                 " nothing was replaced:", path);
            return -1;
        }
    }
    if (write_bytes(live, image->data, image->len) != 0) {
        fail(err, errcap, 0, "the saved game could not be written to:", live);
        return -1;
    }

    /* From here the save is the arriving one, so the folder around it is made
     * to agree with it rather than left describing the game that was here. */
    sessions_clear(dir);
    snprintf(path, sizeof path, "%s.report", live);
    remove(path);
    item = bundle_one(b, MMO_BUNDLE_REPORT);
    if (item != NULL && item->len > 0)
        (void)write_bytes(path, item->data, item->len);
    /* The word that an earlier report was taken by the server. It was taken
     * about a game that is no longer at this name. */
    snprintf(path, sizeof path, "%s.report.landed", live);
    remove(path);
    snprintf(path, sizeof path, "%s.chain", live);
    remove(path);
    snprintf(path, sizeof path, "%s.restore", live);
    remove(path);
    item = bundle_one(b, MMO_BUNDLE_RESTORE);
    if (item != NULL && item->len > 0)
        (void)write_bytes(path, item->data, item->len);

    for (i = 0; i < b->nitems; i++) {
        const mmo_bundle_item *it = &b->items[i];
        const char *ext = NULL;

        switch (it->kind) {
        case MMO_BUNDLE_ANCHOR: ext = "export"; break;
        case MMO_BUNDLE_LINK:   ext = "link";   break;
        case MMO_BUNDLE_INPUT:  ext = "inp";    break;
        default: continue;
        }
        snprintf(path, sizeof path, "%s/sessions/%s.%s", dir, it->name, ext);
        if (write_bytes(path, it->data, it->len) != 0) {
            /*
             * Not fatal, and not swallowed either. The save has landed; what is missing is a
             * piece of the evidence behind it, and a save with less evidence than it could
             * have had is one that stays marked, which is where every import starts anyway.
             */
            missing++;
            continue;
        }
        if (it->kind == MMO_BUNDLE_LINK)
            sessions++;
    }

    /* And the marker that says where this game came from, which is the whole
     * of what the front door needs to stop calling it a New Game. */
    who[0] = '\0';
    anchor[0] = '\0';
    mmo_bundle_note_text(b, "character", who, sizeof who);
    mmo_bundle_note_text(b, "anchor", anchor, sizeof anchor);
    snprintf(path, sizeof path, "%s/" ELSEWHERE_NAME, dir);
    f = fopen(path, "wb");
    if (f != NULL) {
        fprintf(f, "version %d\n", LINK_VERSION);
        fprintf(f, "rtc %s\n", wall);
        if (who[0] != '\0')
            fprintf(f, "character %s\n", who);
        fprintf(f, "anchor %s\n", anchor[0] != '\0' ? anchor : "none");
        (void)mmo_plat_fsync(f);
        fclose(f);
    }
    prune_images(dir, MMO_LAUNCH_SAVE_KEEP);

    if (landed != NULL && landedcap > 0) {
        char note[MMO_LAUNCH_TEXT];
        char gone[MMO_LAUNCH_TEXT + 64];

        note[0] = '\0';
        if (who[0] != '\0')
            snprintf(note, sizeof note, " (%.24s)", who);
        /* Where the game that was here went, in the same breath. It is under
         * Restore save, and saying so is the whole of the undo. */
        gone[0] = '\0';
        if (replaced[0] != '\0')
            snprintf(gone, sizeof gone, "; the game it replaced (%.24s) is"
                     " under Restore save", replaced);
        else if (had_game)
            snprintf(gone, sizeof gone, "; the game it replaced is under"
                     " Restore save");
        snprintf(landed, landedcap,
                 "the saved game%s is this slot now, with %d offline session%s"
                 " behind it%s%s%s", note, sessions, sessions == 1 ? "" : "s",
                 bundle_one(b, MMO_BUNDLE_ANCHOR) != NULL
                     ? " and the game it started from named"
                     : "; its play cannot be checked, so what it brings online"
                       " stays marked",
                 missing > 0
                     ? ", and some of that play could not be written down"
                       " here, so less of it can be checked than arrived"
                     : "",
                 gone);
    }
    return 0;
}

int mmo_launch_bundle_read(const char *port_exe, int slot, const char *path,
                           char *landed, size_t landedcap,
                           char *err, size_t errcap)
{
    char dir[MMO_LAUNCH_PATH + 8];
    char seed[MMO_LAUNCH_PATH + 24];
    char lock[MMO_LAUNCH_PATH + 24];
    mmo_plat_lock *held = NULL;
    mmo_bundle b;
    int rc;

    if (landed != NULL && landedcap > 0)
        landed[0] = '\0';
    if (port_exe == NULL || port_exe[0] == '\0') {
        fail(err, errcap, 0, "an import needs the game's own path", NULL);
        return -1;
    }
    /* Read and understood before the save is claimed: a file that is not one
     * of ours must not take the save off a session about to start. */
    if (bundle_open(path, &b, err, errcap) != 0)
        return -1;
    mmo_launch_save_dir(port_exe, slot, dir, sizeof dir);
    /* The folder before the lock, because taking one creates the file it opens
     * and a slot nobody has played yet has no folder for it to be created in.
     * The deeper of the two names makes both, which is what the offline row
     * does for the same reason. */
    snprintf(seed, sizeof seed, "%s/sessions/x", dir);
    if (mkdir_parents(seed) != 0) {
        mmo_bundle_free(&b);
        fail(err, errcap, 0, "the save folder could not be made:", dir);
        return -1;
    }
    save_lock_path(dir, lock, sizeof lock);
    switch (mmo_plat_lock_take(lock, &held)) {
    case 0:
        break;
    case 1:
        mmo_bundle_free(&b);
        fail(err, errcap, 0, "another launcher has this saved game open; close"
             " that game and bring the other one in after it", NULL);
        return -1;
    default:
        mmo_bundle_free(&b);
        fail(err, errcap, 0, "this saved game could not be claimed for the"
             " import:", lock);
        return -1;
    }
    rc = bundle_read_locked(&b, dir, NULL, landed, landedcap, err, errcap);
    mmo_plat_lock_drop(held);
    mmo_bundle_free(&b);
    return rc;
}
