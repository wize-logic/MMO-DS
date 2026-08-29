/* Settings, the file they live in, and the two command lines
 * they become. See launch_plan.h for why this is SDL-free. */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cartridge.h"
#include "launch_plan.h"
#include "platform.h"

static const char *const layout_names[MMO_LAYOUT_N] = {
    "smart", "stacked", "wide", "fill"
};
static const char *const filter_names[MMO_FILTER_N] = { "nearest", "linear", "scale2x" };
static const char *const fit_names[MMO_FIT_N]       = { "aspect", "integer", "stretch" };
static const char *const pace_names[MMO_PACE_N]     = { "console", "unlimited" };
/* The two 3D resolutions, in step order from MMO_LAUNCH_HD3D_MIN up. */
static const char *const hd3d_names[MMO_LAUNCH_HD3D_N] = { "sd", "hd" };
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
        fail(err, errcap, line, key, "wants sd or hd");
        return -1;
    }
    *dst = n <= 1 ? MMO_LAUNCH_HD3D_MIN : MMO_LAUNCH_HD3D_MAX;
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
        else if (strcmp(key, "feed") == 0)
            rc = copy_field(s->feed, sizeof s->feed, v, line, key, err, errcap);
        else if (strcmp(key, "feed-key") == 0)
            rc = copy_field(s->feed_key, sizeof s->feed_key, v, line, key,
                            err, errcap);
        else if (strcmp(key, "feed-url") == 0)
            rc = copy_field(s->feed_url, sizeof s->feed_url, v, line, key,
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
    if (s->audio_device[0] != '\0') addf(out, cap, &at, "audio-device %s\n", s->audio_device);
    if (s->feed[0] != '\0')         addf(out, cap, &at, "feed %s\n", s->feed);
    if (s->feed_key[0] != '\0')     addf(out, cap, &at, "feed-key %s\n", s->feed_key);
    if (s->feed_url[0] != '\0')     addf(out, cap, &at, "feed-url %s\n", s->feed_url);
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
    if (fwrite(text, 1, (size_t)n, f) != (size_t)n || fclose(f) != 0) {
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
        return access(out, R_OK) == 0 ? 0 : -1;
    }
    if (!S_ISREG(st.st_mode) || strlen(path) >= cap)
        return -1;
    memcpy(out, path, strlen(path) + 1);
    return access(out, R_OK) == 0 ? 0 : -1;
}

int mmo_launch_check(const mmo_launch_settings *s, char *err, size_t errcap)
{
    char romfile[MMO_LAUNCH_PATH];
    struct stat st;

    if (s->rom[0] == '\0') {
        fail(err, errcap, 0, "no ROM chosen, this build supplies none", NULL);
        return -1;
    }
    if (mmo_launch_rom_file(s->rom, romfile, sizeof romfile) != 0) {
        if (stat(s->rom, &st) == 0 && S_ISDIR(st.st_mode))
            fail(err, errcap, 0, "no pokeplatinum.us.nds in", s->rom);
        else
            fail(err, errcap, 0, "the ROM cannot be read:", s->rom);
        return -1;
    }
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

int mmo_launch_plan_build(const mmo_launch_settings *s, const char *port_exe,
                          const char *view_exe, const char *chan, long pid,
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

    if (mods_dir_live)
        bad |= push_env(p, "PC_MODS_DIR", s->mods_dir);
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
        char modroot[MMO_LAUNCH_PATH];
        char sdat[MMO_LAUNCH_PATH + 96];
        char list[MMO_LAUNCH_TEXT + 32];
        FILE *probe;

        snprintf(pkgname, sizeof pkgname, "sound_%s_%s",
                 sound_slugs[strack], sound_slugs[sfont]);

        if (mods_dir_live) {
            snprintf(modroot, sizeof modroot, "%s", s->mods_dir);
        } else {
            /* No mods-dir row, the packaged install's case. Its packages
             * live in mods/ beside bin/, the same default the front door
             * scripts hand the engine, so look where the game we are about
             * to start actually lives rather than refusing. */
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
        if (s->mods[0] != '\0')
            snprintf(list, sizeof list, "%s,%s", s->mods, pkgname);
        else
            snprintf(list, sizeof list, "%s", pkgname);
        bad |= push_env(p, "PC_MODS", list);
    } else if (s->mods[0] != '\0') {
        bad |= push_env(p, "PC_MODS", s->mods);
    }
    }
    }
    /* A session's party, position and flags are the server's. Writing them
     * into a local file would make the next boot look like a single-player
     * save of a game nobody played here, so every plan runs the port in its own
     * ephemeral mode and there is no longer a row that names a save file. */
    bad |= push_env(p, "PC_SAVE", "none");
    /* Whether is not asked either, only who. The address the game dials is
     * compiled into it (mmo/include/endpoint.h) and a Play that did not join it
     * would be the engine port with our client sitting silent inside it, which
     * is not what this front door is for. */
    bad |= push_env(p, "OPENMMO_SESSION", "1");
    /*
     * Stated even when empty, for the reason spelled out below: a name left out is not "no
     * account", it is whatever the environment already said, and a leftover OPENMMO_PASS from
     * a debugging run would go on answering for the panel.
     */
    bad |= push_env(p, "OPENMMO_USER", s->user);
    bad |= push_env(p, "OPENMMO_PASS", s->pass);
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

    /* The window: the channel first, then the display settings. */
    bad |= push_arg(p->view_argv, &p->view_argc, p, view_exe);
    bad |= push_arg(p->view_argv, &p->view_argc, p, p->chan);
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
