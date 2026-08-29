/* The launcher's login face, inside the app. */

#include "SDL.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mmo_update_notice.h"
#include "platform.h"
#include "token.h"
#include "view_ui.h"
#include "view_ui_draw.h"
#include "view_ui_theme.h"

#define FD_USER_MAX 64
#define FD_PASS_MAX 64

enum { FOCUS_NONE = 0, FOCUS_USER, FOCUS_PASS };

/*
 * The settings, in the desktop file's own keys (fd_settings_load below says
 * which). Held on the door so the settings face edits what LOGIN applies.
 */
struct fd_settings {
    char user[FD_USER_MAX];
    int camera;        /* 100..130, step 5 */
    int hd3d;          /* 2 sd, 3 hd; sd or hd in the file */
    int viewport;      /* 0 auto, 1 native */
    int filter;        /* 0 nearest, 1 linear, 2 scale2x */
    int rs;            /* 1..4 */
    int layout;        /* 0 smart, 1 stacked, 2 wide, 3 fill */
    int ui_scale;      /* 1..4, the shim's own */
    int music, sfx;    /* 0..100 */
    int button;        /* 0 normal, 1 start-is-x, 2 l-is-a */
    char rom[512];     /* the cartridge, as a PATH to the player's own file,
                         the desktop's `rom` key, never a copy. A copy
                          squirrelled into the app's hidden directory kept
                          sessions booting after the visible file was
                          deleted, and nothing could explain why. */
    int touch;         /* 0 off, 1 auto, 2 on, the drawn pad */
    int touch_alpha;   /* 0..100 */
    int touch_size;    /* 60..140, percent */
    int soundtrack;    /* 0 platinum, 1 heartgold, 2 blackwhite */
};

/* The CARTRIDGE picker. */
#define FD_ROM_MAX   256         /* entries listed in one directory */
#define FD_ROM_NAME  128
#define FD_ROM_PATH  512

struct fd_rom_entry {
    char name[FD_ROM_NAME];
    int  is_dir;
    long long size;
};

struct fd_rom {
    char dir[FD_ROM_PATH];
    struct fd_rom_entry e[FD_ROM_MAX];
    int n;
    int scroll;
    int truncated;               /* the directory held more than we list */
    int denied;                  /* the read failed: no All files access */
    char note[192];              /* what just happened, for the player */
};

struct frontdoor {
    SDL_Renderer *ren;
    struct view_ui_gpu gpu;
    SDL_Texture *bg, *mark;
    int bg_w, bg_h, mark_w, mark_h;
    char user[FD_USER_MAX];
    char pass[FD_PASS_MAX];
    int focus;
    int remember;
    int on_settings;
    int rom_ok;                  /* fd_rom_status, re-asked every frame so a
                                    file deleted behind the door flips the
                                    face the moment it happens */
    char rom_why[220];
    int rom_wait;                /* the system picker is up; poll for its
                                    answer */
    int on_rom;                  /* the drawn fallback browser owns the face */
    struct fd_rom rom;
    struct fd_settings set;
};

/* ------------------------------------------------------------------ */
/* Art and small files                                                 */
/* ------------------------------------------------------------------ */

static unsigned char *fd_slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long n;

    if (f == NULL)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) <= 0
        || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = malloc((size_t)n);
    if (buf == NULL || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static SDL_Texture *fd_texture(SDL_Renderer *ren, const char *dir,
                               const char *name, int *w, int *h)
{
    char path[1024];
    unsigned char *buf, *rgba;
    size_t len = 0;
    SDL_Texture *t;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    buf = fd_slurp(path, &len);
    if (buf == NULL)
        return NULL;
    rgba = view_ui_png(buf, len, w, h);
    free(buf);
    if (rgba == NULL)
        return NULL;
    t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888,
                          SDL_TEXTUREACCESS_STATIC, *w, *h);
    if (t != NULL) {
        SDL_UpdateTexture(t, NULL, rgba, *w * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    }
    free(rgba);
    return t;
}

/*
 * launcher.cfg beside the token, in the DESKTOP FILE'S OWN KEYS, so either file reads as the
 * other: user, camera-distance, hd3d, viewport, filter, render-scale, layout, music, sfx,
 * button-mode, plus `ui-scale`, which only the device has.
 */
static const char *const fd_layouts[4] = { "smart", "stacked", "wide",
                                           "fill" };
static const char *const fd_filters[3] = { "nearest", "linear", "scale2x" };
static const char *const fd_buttons[3] = { "normal", "start-is-x", "l-is-a" };
static const char *const fd_hd3d[2]    = { "sd", "hd" };
/*
 * Auto is the default and is not a fence-sit: a phone never sends a gamepad keycode and keeps
 * the drawn pad, a handheld with sticks sends one on its first press and drops it.
 */
static const char *const fd_touch[3]   = { "off", "auto", "on" };
/* The launcher's own three, in launch_plan.c's order and spelling, because
 * the package name is composed from them and the two files have to agree. */
static const char *const fd_tracks[3]  = { "platinum", "heartgold",
                                           "blackwhite" };
static const char *const fd_track_pkg[3] = { "pt", "hg", "bw" };

static void fd_settings_defaults(struct fd_settings *s)
{
    memset(s, 0, sizeof *s);
    s->camera = 130;
    s->hd3d = 2;
    s->touch = 1;              /* auto: the device decides, see fd_touch */
    s->touch_alpha = 100;
    s->touch_size = 100;
    s->soundtrack = 0;         /* platinum, the game's own */
    s->viewport = 0;
    s->filter = 1;
    s->rs = 2;
    s->layout = 3;
    s->ui_scale = 2;
    s->music = 100;
    s->sfx = 100;
    s->button = 0;
}

static void fd_cfg_path(char *out, size_t cap)
{
    char base[512];

    out[0] = '\0';
    if (mmo_plat_config_home(base, sizeof base) != 0)
        return;
    snprintf(out, cap, "%s%sopenmmo", base, mmo_plat_sep());
    mmo_plat_mkdir(out);
    snprintf(out, cap, "%s%sopenmmo%slauncher.cfg", base, mmo_plat_sep(),
             mmo_plat_sep());
}

static int fd_name_index(const char *const names[], int n, const char *v,
                         int fallback)
{
    int i;

    for (i = 0; i < n; i++) {
        if (strcmp(names[i], v) == 0)
            return i;
    }
    return fallback;
}

static int fd_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/*
 * The resolution row, which is a name now and used to be a number. A file from any build up to
 * this one says 1, 2 or 3; the old numbers move up a step, 1 to sd and anything above it to
 * hd, and the next save writes the name.
 */
static int fd_hd3d_value(const char *v)
{
    int n;

    if (strcmp(v, "sd") == 0) return 2;
    if (strcmp(v, "hd") == 0) return 3;
    n = atoi(v);
    return n <= 1 ? 2 : 3;
}

static void fd_settings_load(struct fd_settings *s)
{
    char path[560], line[256];
    FILE *f;

    fd_settings_defaults(s);
    fd_cfg_path(path, sizeof path);
    if (path[0] == '\0' || (f = fopen(path, "r")) == NULL)
        return;
    while (fgets(line, sizeof line, f) != NULL) {
        char *nl = strchr(line, '\n');
        char *sp;

        if (nl != NULL)
            *nl = '\0';
        if (line[0] == '#' || (sp = strchr(line, ' ')) == NULL)
            continue;
        *sp = '\0';
        if (strcmp(line, "user") == 0)
            snprintf(s->user, sizeof s->user, "%s", sp + 1);
        else if (strcmp(line, "camera-distance") == 0)
            s->camera = fd_clampi(atoi(sp + 1), 100, 130);
        else if (strcmp(line, "hd3d") == 0)
            s->hd3d = fd_hd3d_value(sp + 1);
        else if (strcmp(line, "rom") == 0)
            snprintf(s->rom, sizeof s->rom, "%s", sp + 1);
        else if (strcmp(line, "touch") == 0)
            s->touch = fd_name_index(fd_touch, 3, sp + 1, 1);
        else if (strcmp(line, "touch-alpha") == 0)
            s->touch_alpha = fd_clampi(atoi(sp + 1), 0, 100);
        else if (strcmp(line, "touch-size") == 0)
            s->touch_size = fd_clampi(atoi(sp + 1), 60, 140);
        else if (strcmp(line, "soundtrack") == 0)
            s->soundtrack = fd_name_index(fd_tracks, 3, sp + 1, 0);
        else if (strcmp(line, "viewport") == 0)
            s->viewport = strcmp(sp + 1, "native") == 0;
        else if (strcmp(line, "filter") == 0)
            s->filter = fd_name_index(fd_filters, 3, sp + 1, 1);
        else if (strcmp(line, "render-scale") == 0)
            s->rs = fd_clampi(atoi(sp + 1), 1, 4);
        else if (strcmp(line, "layout") == 0)
            s->layout = fd_name_index(fd_layouts, 4, sp + 1, 3);
        else if (strcmp(line, "ui-scale") == 0)
            s->ui_scale = fd_clampi(atoi(sp + 1), 1, 4);
        else if (strcmp(line, "music") == 0)
            s->music = fd_clampi(atoi(sp + 1), 0, 100);
        else if (strcmp(line, "sfx") == 0)
            s->sfx = fd_clampi(atoi(sp + 1), 0, 100);
        else if (strcmp(line, "button-mode") == 0)
            s->button = fd_name_index(fd_buttons, 3, sp + 1, 0);
    }
    fclose(f);
}

static void fd_settings_save(const struct fd_settings *s)
{
    char path[560];
    FILE *f;

    fd_cfg_path(path, sizeof path);
    if (path[0] == '\0')
        return;
    f = fopen(path, "w");
    if (f == NULL) {
        /* Said out loud, because this exact failure once ate the settings,
         * the token and the cartridge choice at the same time and left
         * nothing anywhere naming it. */
        fprintf(stderr, "frontdoor: cannot save settings to %s: %s\n",
                path, strerror(errno));
        return;
    }
    fprintf(f, "# openmmo settings; delete this file for defaults\n");
    if (s->user[0] != '\0')
        fprintf(f, "user %s\n", s->user);
    fprintf(f, "camera-distance %d\n", s->camera);
    fprintf(f, "hd3d %s\n", fd_hd3d[s->hd3d - 2]);
    fprintf(f, "viewport %s\n", s->viewport ? "native" : "auto");
    fprintf(f, "filter %s\n", fd_filters[s->filter]);
    fprintf(f, "render-scale %d\n", s->rs);
    fprintf(f, "layout %s\n", fd_layouts[s->layout]);
    fprintf(f, "ui-scale %d\n", s->ui_scale);
    fprintf(f, "music %d\n", s->music);
    fprintf(f, "sfx %d\n", s->sfx);
    fprintf(f, "button-mode %s\n", fd_buttons[s->button]);
    if (s->rom[0] != '\0')
        fprintf(f, "rom %s\n", s->rom);
    fprintf(f, "touch %s\n", fd_touch[s->touch]);
    fprintf(f, "touch-alpha %d\n", s->touch_alpha);
    fprintf(f, "touch-size %d\n", s->touch_size);
    fprintf(f, "soundtrack %s\n", fd_tracks[s->soundtrack]);
    fclose(f);
}

/*
 * The half of launch_plan this device keeps: the settings become the game's environment and
 * the window's arguments, exactly the rows the desktop plan pushes.
 */
/* The system document picker's C side, exported by mmo_android_main.c,
 * which owns the activity and the JavaVM. Plain externs: both objects are
 * always in the one shared library together. */
int mmo_android_rom_pick(void);
int mmo_android_rom_picked(char *out, size_t cap);
int mmo_android_rom_uri_ok(const char *uri);
int mmo_android_rom_uri_fd(const char *uri);

/* Ask for a cartridge: the system picker when the device has one, the drawn
 * browser when it does not. Every choose-a-rom press funnels through here so
 * the two routes cannot drift. */
struct frontdoor;
static void fd_rom_ask(struct frontdoor *d);

/* content:// has no path for stat to see, so its name for the footer is dug
 * out of the URI itself: percent-decode, then take what follows the last
 * separator. document/primary%3ADownload%2Fpokeplatinum.us.nds ends in
 * exactly the name the player knows the file by. */
static void fd_uri_name(const char *uri, char *out, size_t cap)
{
    char dec[512];
    size_t i = 0, o = 0;
    const char *base;

    while (uri[i] != '\0' && o + 1 < sizeof dec) {
        if (uri[i] == '%' && uri[i + 1] != '\0' && uri[i + 2] != '\0') {
            char hex[3] = { uri[i + 1], uri[i + 2], '\0' };

            dec[o++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else {
            dec[o++] = uri[i++];
        }
    }
    dec[o] = '\0';
    base = strrchr(dec, '/');
    if (base == NULL)
        base = strrchr(dec, ':');
    snprintf(out, cap, "%s", base != NULL ? base + 1 : dec);
}

/*
 * The desktop launcher's own pair, ported word for word: which file a rom setting names (a
 * path may be the file or the folder that holds it), and the one line about it, what it is
 * when it works, what is wrong when it does not.
 */
static int fd_rom_file(const char *path, char *out, size_t cap)
{
    struct stat st;
    int n;

    if (path == NULL || path[0] == '\0' || out == NULL || cap == 0)
        return -1;
    if (stat(path, &st) != 0)
        return -1;
    if (S_ISDIR(st.st_mode)) {
        n = snprintf(out, cap, "%s/pokeplatinum.us.nds", path);
        if (n < 0 || (size_t)n >= cap)
            return -1;
        return access(out, R_OK) == 0 ? 0 : -1;
    }
    if (!S_ISREG(st.st_mode) || strlen(path) >= cap)
        return -1;
    memcpy(out, path, strlen(path) + 1);
    return access(out, R_OK) == 0 ? 0 : -1;
}

static int fd_rom_status(const struct fd_settings *s, char *out, size_t cap)
{
    char romfile[512];
    const char *base;
    struct stat st;

    if (s->rom[0] == '\0') {
        snprintf(out, cap, "No ROM chosen yet");
        return 0;
    }
    if (strncmp(s->rom, "content://", 10) == 0) {
        /* A picked document. The probe is a binder call, so it is cached and
         * re-asked about once a second rather than every frame, still fast
         * enough that a deleted file flips the face while you watch. */
        static char last[512];
        static int last_ok, age;

        if (strcmp(last, s->rom) != 0 || ++age >= 30) {
            snprintf(last, sizeof last, "%s", s->rom);
            last_ok = mmo_android_rom_uri_ok(s->rom);
            age = 0;
        }
        if (!last_ok) {
            snprintf(out, cap, "That ROM cannot be read");
            return 0;
        }
        fd_uri_name(s->rom, romfile, sizeof romfile);
        snprintf(out, cap, "ROM ready: %s", romfile);
        return 1;
    }
    if (fd_rom_file(s->rom, romfile, sizeof romfile) != 0) {
        if (stat(s->rom, &st) == 0 && S_ISDIR(st.st_mode))
            snprintf(out, cap, "No pokeplatinum.us.nds in that folder");
        else
            snprintf(out, cap, "That ROM cannot be read");
        return 0;
    }
    base = strrchr(romfile, '/');
    snprintf(out, cap, "ROM ready: %s", base != NULL ? base + 1 : romfile);
    return 1;
}

/*
 * Stream a granted cartridge into the boot cache. The stamp beside it names the URI and size
 * the cache was cut from, so an unchanged source is not copied twice and a swapped one never
 * boots stale.
 */
static int fd_rom_cache(int fd, const char *uri, char *out, size_t cap)
{
    const char *ext = getenv("OPENMMO_EXTERNAL_DIR");
    char cache[560], stamp[572], tmp[572], want[700], have[700];
    struct stat st;
    FILE *sf, *o;
    char *buf;
    ssize_t got;
    int n;

    if (ext == NULL || ext[0] == '\0' || fstat(fd, &st) != 0)
        return -1;
    snprintf(cache, sizeof cache, "%s/rom-cache.nds", ext);
    snprintf(stamp, sizeof stamp, "%s.stamp", cache);
    n = snprintf(want, sizeof want, "%s %lld", uri, (long long)st.st_size);
    if (n < 0 || (size_t)n >= sizeof want)
        return -1;

    sf = fopen(stamp, "rb");
    if (sf != NULL) {
        size_t hn = fread(have, 1, sizeof have - 1, sf);

        fclose(sf);
        have[hn] = '\0';
        if (strcmp(have, want) == 0 && access(cache, R_OK) == 0) {
            snprintf(out, cap, "%s", cache);
            return 0;           /* same document, same size: boot the copy */
        }
    }

    fprintf(stderr, "frontdoor: caching the cartridge (%lld MB)\n",
            (long long)st.st_size / (1024 * 1024));
    snprintf(tmp, sizeof tmp, "%s.part", cache);
    o = fopen(tmp, "wb");
    buf = o != NULL ? malloc(1u << 20) : NULL;
    if (buf == NULL) {
        if (o != NULL) {
            fclose(o);
            remove(tmp);
        }
        return -1;
    }
    lseek(fd, 0, SEEK_SET);
    while ((got = read(fd, buf, 1u << 20)) > 0) {
        if (fwrite(buf, 1, (size_t)got, o) != (size_t)got) {
            got = -1;
            break;
        }
    }
    free(buf);
    if (fclose(o) != 0 || got < 0) {
        remove(tmp);
        fprintf(stderr, "frontdoor: the cartridge copy failed\n");
        return -1;
    }
    remove(cache);
    if (rename(tmp, cache) != 0) {
        remove(tmp);
        return -1;
    }
    remove(stamp);
    sf = fopen(stamp, "wb");
    if (sf != NULL) {
        fwrite(want, 1, strlen(want), sf);
        fclose(sf);
    }
    snprintf(out, cap, "%s", cache);
    return 0;
}

void mmo_frontdoor_apply(char *view_args, size_t cap)
{
    struct fd_settings s;
    char buf[16];
    int rs;

    fd_settings_load(&s);
    snprintf(buf, sizeof buf, "%d", s.camera);
    setenv("OPENMMO_CAMERA_DISTANCE", buf, 0);
    snprintf(buf, sizeof buf, "%d", s.hd3d);
    setenv("PC_HD3D", buf, 0);
    setenv("PC_ASPECT", s.viewport ? "native" : "auto", 0);
    snprintf(buf, sizeof buf, "%d", s.music);
    setenv("OPENMMO_MUSIC", buf, 0);
    snprintf(buf, sizeof buf, "%d", s.sfx);
    setenv("OPENMMO_SFX", buf, 0);
    setenv("OPENMMO_BUTTON_MODE", fd_buttons[s.button], 0);
    snprintf(buf, sizeof buf, "%d", s.ui_scale);
    setenv("OPENMMO_UI_SCALE", buf, 0);

    /* The cartridge the player chose, resolved the way the desktop resolves
     * it (a folder means the file inside it), handed to the boot as
     * OPENMMO_ROM. No overwrite: openmmo.env naming one is a bench run and
     * wins, exactly as every knob here does. */
    {
        char romfile[512];

        if (strncmp(s.rom, "content://", 10) == 0) {
            /* The URI is the truth, A cache serves the boot. */
            int fd = mmo_android_rom_uri_fd(s.rom);

            fprintf(stderr, "frontdoor: cartridge fd %d for %.90s\n",
                    fd, s.rom);
            if (fd >= 0
                && fd_rom_cache(fd, s.rom, romfile, sizeof romfile) == 0) {
                setenv("OPENMMO_ROM", romfile, 0);
            }
            if (fd >= 0)
                close(fd);
        } else if (s.rom[0] != '\0'
                   && fd_rom_file(s.rom, romfile, sizeof romfile) == 0) {
            setenv("OPENMMO_ROM", romfile, 0);
        }
    }

    /* The drawn pad. Auto sets nothing at all, which is how the shim spells
     * it (sdlshim_android.h): a variable that is absent lets the pad decide
     * from whether a real gamepad ever speaks, and one that is present takes
     * that decision away. So off and on are written and auto is not. */
    if (s.touch != 1)
        setenv("OPENMMO_TOUCH_PAD", s.touch == 2 ? "1" : "0", 0);
    snprintf(buf, sizeof buf, "%d", s.touch_alpha);
    setenv("OPENMMO_TOUCH_ALPHA", buf, 0);
    snprintf(buf, sizeof buf, "%d", s.touch_size);
    setenv("OPENMMO_TOUCH_SIZE", buf, 0);

    /*
     * The soundtrack is a mod package, exactly as it is on the desktop
     * (launcher/launch_plan.c): the composed archive's one claim replaces the sound archive,
     * so choosing it is choosing a package name rather than setting a volume.
     */
    if (s.soundtrack != 0) {
        const char *ext = getenv("OPENMMO_EXTERNAL_DIR");
        char root[512], pkg[64], probe[600];
        FILE *f;

        snprintf(pkg, sizeof pkg, "sound_%s_%s", fd_track_pkg[s.soundtrack],
                 fd_track_pkg[s.soundtrack]);
        snprintf(root, sizeof root, "%s/mods", ext != NULL ? ext : ".");
        /* A package is a directory with a mod.toml in it (mmo/mods/), so the
         * manifest is what proves one is really there, a bare directory
         * left behind by a half-finished push would otherwise pass. */
        snprintf(probe, sizeof probe, "%s/%s/mod.toml", root, pkg);
        f = fopen(probe, "rb");
        if (f != NULL) {
            fclose(f);
            setenv("PC_MODS_DIR", root, 0);
            setenv("PC_MODS", pkg, 0);
            fprintf(stderr, "frontdoor: soundtrack %s (%s)\n",
                    fd_tracks[s.soundtrack], pkg);
        } else {
            fprintf(stderr, "frontdoor: soundtrack %s asked for, but %s/%s"
                    " is not on this device, the cartridge's own music"
                    " plays instead\n",
                    fd_tracks[s.soundtrack], root, pkg);
        }
    }

    /* The desktop plan's own rule: both resolutions hand over a frame that
     * already carries the engine's sub-pixels and the window's CPU prescale
     * would re-replicate them; scale2x is the one filter with an opinion and
     * keeps its scale. */
    rs = (s.filter != 2) ? 1 : s.rs;
    if (view_args != NULL)
        snprintf(view_args, cap, "--layout %s --filter %s --render-scale %d",
                 fd_layouts[s.layout], fd_filters[s.filter], rs);
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

static void fd_text(struct frontdoor *d, int x, int y, const char *s,
                    uint32_t rgb)
{
    view_ui_text(d->ren, &d->gpu, x, y, s, view_ui_col(rgb));
}

static void fd_text_centred(struct frontdoor *d, const struct openmmo_rect *r,
                            const char *s, uint32_t rgb)
{
    int w = view_ui_text_width(&d->gpu, s);

    view_ui_text(d->ren, &d->gpu, r->x + (r->w - w) / 2,
                 r->y + (r->h - d->gpu.px) / 2, s, view_ui_col(rgb));
}

/* The launcher's draw_frame: a dark rounded panel with a centred title. */
static void fd_frame(struct frontdoor *d, const struct openmmo_rect *r,
                     const char *title)
{
    view_ui_fill(d->ren, r->x, r->y, r->w, r->h, 28, 32, 38, 235);
    view_ui_border(d->ren, r->x, r->y, r->w, r->h, 74, 85, 96);
    if (title != NULL) {
        int w = view_ui_text_width(&d->gpu, title);

        fd_text(d, r->x + (r->w - w) / 2, r->y + 10, title, 0xFFFFFFu);
    }
}

/* A field, the TEXTBOX style: 0x161a20 ground, focus turns the border to
 * the launcher's own focus blue, and the caret blinks at the tail. */
static void fd_field(struct frontdoor *d, const struct openmmo_rect *r,
                     const char *shown, int focused, const char *hint)
{
    char line[FD_PASS_MAX + 2];
    SDL_Rect saved;

    view_ui_fill(d->ren, r->x, r->y, r->w, r->h, 0x16, 0x1a, 0x20, 255);
    if (focused)
        view_ui_border(d->ren, r->x, r->y, r->w, r->h, 0x6a, 0x88, 0x9b);
    else
        view_ui_border(d->ren, r->x, r->y, r->w, r->h, 0x4a, 0x55, 0x60);
    if (shown[0] == '\0' && !focused) {
        view_ui_text_in(d->ren, &d->gpu, r, 8, 0, hint, 0x77818Bu);
        return;
    }
    snprintf(line, sizeof line, "%s%s", shown,
             focused && (SDL_GetTicks64() / 500) % 2 == 0 ? "_" : "");
    /* A long mask stays inside its box; the tail is the part being typed,
     * so it is the tail that stays in view. */
    view_ui_clip_push(d->ren, r, &saved);
    if (view_ui_text_width(&d->gpu, line) > r->w - 16) {
        int x = r->x + r->w - 8 - view_ui_text_width(&d->gpu, line);

        view_ui_text(d->ren, &d->gpu, x, r->y + (r->h - d->gpu.px) / 2, line,
                     view_ui_col(0xF2F2F2u));
    } else {
        view_ui_text_in(d->ren, &d->gpu, r, 8, 0, line, 0xF2F2F2u);
    }
    view_ui_clip_pop(d->ren, &saved);
}

static void fd_button(struct frontdoor *d, const struct openmmo_rect *r,
                      const char *label)
{
    view_ui_fill(d->ren, r->x, r->y, r->w, r->h, 0x3a, 0x44, 0x50, 255);
    view_ui_border(d->ren, r->x, r->y, r->w, r->h, 0x5a, 0x66, 0x70);
    fd_text_centred(d, r, label, 0xF2F2F2u);
}

static int fd_hit(const struct openmmo_rect *r, int x, int y)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

/* ------------------------------------------------------------------ */
/* The cartridge picker                                                */
/* ------------------------------------------------------------------ */

/* Where a person's own files are. /sdcard is the symlink everyone knows;
 * the emulated path is what it points at and what survives an odd ROM. */
static const char *const kRomRoots[] = {
    "/storage/emulated/0", "/sdcard", "/storage/self/primary", NULL
};

static int fd_rom_is_nds(const char *name)
{
    size_t n = strlen(name);

    return n > 4 && strcasecmp(name + n - 4, ".nds") == 0;
}

static int fd_rom_cmp(const void *a, const void *b)
{
    const struct fd_rom_entry *x = a, *y = b;

    if (x->is_dir != y->is_dir)
        return y->is_dir - x->is_dir;      /* directories first */
    return strcasecmp(x->name, y->name);
}

/*
 * List one directory: sub-directories and .nds files, nothing else. A player hunting for a
 * cartridge does not want to scroll past their photos, and a list that shows only what can be
 * chosen cannot be mis-tapped into.
 */
static void fd_rom_read(struct fd_rom *r)
{
    DIR *dp;
    struct dirent *de;

    r->n = 0;
    r->scroll = 0;
    r->truncated = 0;
    r->denied = 0;
    dp = opendir(r->dir);
    if (dp == NULL) {
        /* EACCES here is the whole permission story: the directory exists and
         * the app may not look at it. Anything else is a path that is gone. */
        r->denied = (errno == EACCES || errno == EPERM);
        snprintf(r->note, sizeof r->note, "%s: %s", r->dir, strerror(errno));
        return;
    }
    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        char full[FD_ROM_PATH + FD_ROM_NAME + 2];
        int isdir;

        if (de->d_name[0] == '.')
            continue;                      /* . .. and the dotfiles */
        if (r->n >= FD_ROM_MAX) {
            r->truncated = 1;
            break;
        }
        snprintf(full, sizeof full, "%s/%s", r->dir, de->d_name);
        if (stat(full, &st) != 0)
            continue;
        isdir = S_ISDIR(st.st_mode);
        if (!isdir && !fd_rom_is_nds(de->d_name))
            continue;
        snprintf(r->e[r->n].name, sizeof r->e[r->n].name, "%s", de->d_name);
        r->e[r->n].is_dir = isdir;
        r->e[r->n].size = isdir ? 0 : (long long)st.st_size;
        r->n++;
    }
    closedir(dp);
    qsort(r->e, (size_t)r->n, sizeof r->e[0], fd_rom_cmp);
}

static void fd_rom_open(struct fd_rom *r, const char *dir)
{
    snprintf(r->dir, sizeof r->dir, "%s", dir);
    fd_rom_read(r);
}

/* Start where a cartridge is most likely to be, and fall back to whatever
 * root can actually be opened. */
static void fd_rom_start(struct fd_rom *r)
{
    static const char *const kFirst[] = { "/Download", "/Downloads", "", NULL };
    int i, j;

    r->note[0] = '\0';
    for (i = 0; kRomRoots[i] != NULL; i++) {
        for (j = 0; kFirst[j] != NULL; j++) {
            char cand[FD_ROM_PATH];

            snprintf(cand, sizeof cand, "%s%s", kRomRoots[i], kFirst[j]);
            fd_rom_open(r, cand);
            if (!r->denied && r->dir[0] != '\0' && r->n >= 0
                && access(cand, R_OK) == 0)
                return;
        }
    }
    /* Nothing opened: leave the last attempt's note and denied flag, which is
     * what the face turns into the Settings instruction. */
}

static void fd_rom_up(struct fd_rom *r)
{
    char *slash = strrchr(r->dir, '/');

    if (slash == NULL || slash == r->dir)
        return;
    *slash = '\0';
    fd_rom_read(r);
}


/* ------------------------------------------------------------------ */
/* The face                                                            */
/* ------------------------------------------------------------------ */

struct fd_rects {
    struct openmmo_rect panel, user_b, pass_b, rem_b, login_b, out_b, exit_b;
    struct openmmo_rect settings_b, rom_b;
    int saved;
};

/*
 * The settings face: the device-shaped rows of the desktop panel, each a value between < and >
 * at a finger's size, in the order a player meets their effects, the camera first because it
 * is the one whose absence was noticed, the window rows after, sound last.
 */
#define FD_SROWS 14

struct fd_srects {
    struct openmmo_rect panel, row[FD_SROWS], dec[FD_SROWS], inc[FD_SROWS];
    struct openmmo_rect done_b;
};

/* The pitch is derived, not written down. */
static void fd_slayout(int sw, int sh, struct fd_srects *o)
{
    int ww = 460, pitch = 34, rowh, step, wh;
    int avail = sh - 8;
    int wx, wy, i;

    if (52 + FD_SROWS * pitch + 56 > avail)
        pitch = (avail - 52 - 56) / (FD_SROWS > 0 ? FD_SROWS : 1);
    if (pitch > 34)
        pitch = 34;
    /* No floor, and that is deliberate. */
    if (pitch < 1)
        pitch = 1;
    rowh = pitch - 4;
    /*
     * The STEPPER has the floor, not the pitch, because it is the only thing here that has to
     * stay finger-sized to work rather than merely to look right.
     */
    step = rowh - 2;
    if (step < 8)
        step = 8;
    wh = 52 + FD_SROWS * pitch + 56;
    wx = (sw - ww) / 2;
    wy = (sh - wh) / 2;

    if (wy < 4)
        wy = 4;
    o->panel = (struct openmmo_rect){ wx, wy, ww, wh };
    for (i = 0; i < FD_SROWS; i++) {
        int ry = wy + 46 + i * pitch;

        o->row[i] = (struct openmmo_rect){ wx + 16, ry, ww - 32, rowh };
        o->dec[i] = (struct openmmo_rect){ wx + ww - 16 - 176, ry + 1, step,
                                           step };
        o->inc[i] = (struct openmmo_rect){ wx + ww - 16 - step, ry + 1, step,
                                           step };
    }
    o->done_b = (struct openmmo_rect){ wx + ww - 16 - 120,
                                       wy + wh - 44, 120, 32 };
}

static void fd_srow_value(const struct fd_settings *s, int i, char *out,
                          size_t cap)
{
    switch (i) {
    case 0: snprintf(out, cap, "%d%%", s->camera); break;
    case 1: snprintf(out, cap, "%s", s->hd3d >= 3 ? "HD" : "SD"); break;
    case 2: snprintf(out, cap, "%s", s->viewport ? "native" : "auto"); break;
    case 3: snprintf(out, cap, "%s", fd_filters[s->filter]); break;
    case 4: snprintf(out, cap, "%dx", s->rs); break;
    case 5: snprintf(out, cap, "%s", fd_layouts[s->layout]); break;
    case 6: snprintf(out, cap, "%dx", s->ui_scale); break;
    case 7: snprintf(out, cap, "%d", s->music); break;
    case 8: snprintf(out, cap, "%d", s->sfx); break;
    /* Explicit, not the default it used to be. The default arm here and in
     * fd_srow_step is the button mode, so a row added without a case of its
     * own does not sit inert, it silently edits the buttons instead. */
    case 9: snprintf(out, cap, "%s", fd_buttons[s->button]); break;
    case 10: snprintf(out, cap, "%s", fd_touch[s->touch]); break;
    case 11: snprintf(out, cap, "%d%%", s->touch_alpha); break;
    case 12: snprintf(out, cap, "%d%%", s->touch_size); break;
    default: snprintf(out, cap, "%s", fd_tracks[s->soundtrack]); break;
    }
}

static void fd_srow_step(struct fd_settings *s, int i, int dir)
{
    switch (i) {
    case 0: s->camera = fd_clampi(s->camera + dir * 5, 100, 130); break;
    case 1: s->hd3d = fd_clampi(s->hd3d + dir, 2, 3); break;
    case 2: s->viewport = !s->viewport; break;
    case 3: s->filter = (s->filter + dir + 3) % 3; break;
    case 4: s->rs = fd_clampi(s->rs + dir, 1, 4); break;
    case 5: s->layout = (s->layout + dir + 4) % 4; break;
    case 6: s->ui_scale = fd_clampi(s->ui_scale + dir, 1, 4); break;
    case 7: s->music = fd_clampi(s->music + dir * 10, 0, 100); break;
    case 8: s->sfx = fd_clampi(s->sfx + dir * 10, 0, 100); break;
    case 9: s->button = (s->button + dir + 3) % 3; break;
    case 10: s->touch = (s->touch + dir + 3) % 3; break;
    case 11: s->touch_alpha = fd_clampi(s->touch_alpha + dir * 10, 0, 100);
             break;
    case 12: s->touch_size = fd_clampi(s->touch_size + dir * 10, 60, 140);
             break;
    default: s->soundtrack = (s->soundtrack + dir + 3) % 3; break;
    }
}

static const char *const fd_srow_label[FD_SROWS] = {
    "Camera distance", "3D resolution", "Viewport", "Filter", "Render scale",
    "Layout", "UI scale (next start)", "Music", "SFX", "Button mode",
    "Touch controls", "Pad opacity", "Pad size", "Soundtrack"
};

static void fd_rom_ask(struct frontdoor *d)
{
    if (mmo_android_rom_pick() == 0) {
        d->rom_wait = 1;
        return;
    }
    /* No picker on this device: the drawn browser is the answer it was
     * before the picker existed. */
    fd_rom_start(&d->rom);
    d->on_rom = 1;
}

#define FD_ROM_ROWS 9            /* rows on screen; the list scrolls */

struct fd_rrects {
    struct openmmo_rect panel, row[FD_ROM_ROWS];
    struct openmmo_rect up_b, cancel_b, down_b, pgup_b;
};

static void fd_rlayout(int sw, int sh, struct fd_rrects *o)
{
    int ww = sw - 80, wh = sh - 56;
    int wx, wy, pitch, i;

    if (ww > 620) ww = 620;
    if (wh > 460) wh = 460;
    if (ww < 240) ww = 240;
    if (wh < 180) wh = 180;
    wx = (sw - ww) / 2;
    wy = (sh - wh) / 2;
    if (wy < 4) wy = 4;
    o->panel = (struct openmmo_rect){ wx, wy, ww, wh };
    /* Derived, for the reason fd_slayout's is: the list must fit whatever
     * face it is given, and its rows are what has to stay finger-sized. */
    pitch = (wh - 96) / FD_ROM_ROWS;
    if (pitch > 34) pitch = 34;
    if (pitch < 18) pitch = 18;
    for (i = 0; i < FD_ROM_ROWS; i++)
        o->row[i] = (struct openmmo_rect){ wx + 12, wy + 56 + i * pitch,
                                           ww - 24, pitch - 3 };
    o->pgup_b   = (struct openmmo_rect){ wx + 12, wy + wh - 40, 90, 30 };
    o->down_b   = (struct openmmo_rect){ wx + 110, wy + wh - 40, 90, 30 };
    o->up_b     = (struct openmmo_rect){ wx + ww - 12 - 190, wy + wh - 40,
                                         90, 30 };
    o->cancel_b = (struct openmmo_rect){ wx + ww - 12 - 92, wy + wh - 40,
                                         92, 30 };
}

static void fd_draw_rom(struct frontdoor *d, int sw, int sh,
                        const struct fd_rrects *R)
{
    const struct fd_rom *r = &d->rom;
    char line[256];
    int i;

    SDL_SetRenderDrawColor(d->ren, 0x1c, 0x22, 0x28, 255);
    SDL_RenderClear(d->ren);
    view_ui_fill(d->ren, R->panel.x, R->panel.y, R->panel.w, R->panel.h,
                 22, 26, 32, 255);
    view_ui_border(d->ren, R->panel.x, R->panel.y, R->panel.w, R->panel.h,
                   74, 85, 96);
    fd_text(d, R->panel.x + 14, R->panel.y + 10, "CHOOSE YOUR CARTRIDGE",
            0xF2F2F2u);

    if (r->denied) {
        /*
         * The one message that has to be exact. Without All files access this app cannot see a
         * single file the player owns, and there is no way to ask for it from here, the
         * request is an Intent and this client has no Java side.
         */
        fd_text(d, R->panel.x + 14, R->panel.y + 40,
                "Android is not letting OpenMMO read your files yet.",
                0xF2F2F2u);
        fd_text(d, R->panel.x + 14, R->panel.y + 68,
                "Settings > Apps > OpenMMO > Permissions,", 0x96DE96u);
        fd_text(d, R->panel.x + 14, R->panel.y + 90,
                "then turn on 'All files access'.", 0x96DE96u);
        fd_text(d, R->panel.x + 14, R->panel.y + 120,
                "Come back here afterwards and press Retry.", 0xA0A0A8u);
        fd_text(d, R->panel.x + 14, R->panel.y + 148, r->note, 0x929AA2u);
        fd_button(d, &R->up_b, "Retry");
        fd_button(d, &R->cancel_b, "Back");
        return;
    }

    snprintf(line, sizeof line, "%.90s", r->dir);
    fd_text(d, R->panel.x + 14, R->panel.y + 32, line, 0x81DDF1u);

    for (i = 0; i < FD_ROM_ROWS; i++) {
        int at = r->scroll + i;

        if (at >= r->n)
            break;
        if (r->e[at].is_dir) {
            snprintf(line, sizeof line, "[ %.60s ]", r->e[at].name);
            fd_text(d, R->row[i].x + 6, R->row[i].y + 2, line, 0xF2F2F2u);
        } else {
            snprintf(line, sizeof line, "%.52s   %lld MB", r->e[at].name,
                     r->e[at].size / (1024 * 1024));
            fd_text(d, R->row[i].x + 6, R->row[i].y + 2, line, 0x96DE96u);
        }
    }
    if (r->n == 0)
        fd_text(d, R->panel.x + 14, R->panel.y + 60,
                "No folders and no .nds files here.", 0xA0A0A8u);
    if (r->note[0] != '\0')
        fd_text(d, R->panel.x + 14, R->panel.y + R->panel.h - 62, r->note,
                0x96DE96u);
    if (r->truncated)
        fd_text(d, R->panel.x + 14, R->panel.y + R->panel.h - 62,
                "(only the first 256 entries are listed)", 0xA0A0A8u);

    fd_button(d, &R->pgup_b, "Page up");
    fd_button(d, &R->down_b, "Page down");
    fd_button(d, &R->up_b, "Up");
    fd_button(d, &R->cancel_b, "Back");
}

static void fd_draw_settings(struct frontdoor *d, int sw, int sh,
                             const struct fd_srects *S)
{
    int i;

    SDL_SetRenderDrawColor(d->ren, 0x1c, 0x22, 0x28, 255);
    SDL_RenderClear(d->ren);
    if (d->bg != NULL) {
        SDL_Rect dst = { 0, 0, sw, sh };

        SDL_RenderCopy(d->ren, d->bg, NULL, &dst);
    }
    fd_frame(d, &S->panel, "SETTINGS");
    for (i = 0; i < FD_SROWS; i++) {
        char val[24];

        fd_text(d, S->row[i].x, S->row[i].y + 5, fd_srow_label[i],
                0xF2F2F2u);
        fd_button(d, &S->dec[i], "<");
        fd_button(d, &S->inc[i], ">");
        fd_srow_value(&d->set, i, val, sizeof val);
        {
            struct openmmo_rect mid = { S->dec[i].x + S->dec[i].w,
                                        S->row[i].y, S->inc[i].x
                                            - (S->dec[i].x + S->dec[i].w),
                                        S->row[i].h };

            fd_text_centred(d, &mid, val, 0x81DDF1u);
        }
    }
    fd_button(d, &S->done_b, "DONE");
    SDL_RenderPresent(d->ren);
}

static void fd_layout(struct frontdoor *d, int sw, int sh, int saved,
                      struct fd_rects *o)
{
    int ww = 440, wh = saved ? 268 : 214;
    int pad = 20, top = 52, gap = 16, col_w;
    int wx, wy;

    if (ww > sw - 40)
        ww = sw - 40;
    col_w = (ww - pad * 2 - gap) / 2;
    if (d->focus != FOCUS_NONE) {
        /* The keyboard owns the bottom-right; the frame yields to it. */
        wx = 16;
        wy = 30;
    } else {
        wx = (sw - ww) / 2;
        wy = sh * 16 / 100 + sh * 14 / 100 + 18;
        if (wy + wh > sh - 56)
            wy = sh - wh - 56;
    }
    o->saved = saved;
    o->panel = (struct openmmo_rect){ wx, wy, ww, wh };
    o->user_b = (struct openmmo_rect){ wx + pad, wy + top + 22, col_w, 28 };
    o->pass_b = (struct openmmo_rect){ wx + pad + col_w + gap, wy + top + 22,
                                       col_w, 28 };
    o->rem_b = (struct openmmo_rect){ wx + pad, wy + top + 70, 20, 20 };
    o->login_b = (struct openmmo_rect){ wx + pad + col_w + gap, wy + top + 56,
                                        col_w, 50 };
    o->out_b = (struct openmmo_rect){ wx + ww - pad - 86,
                                      wy + top + 112 + 13, 86, 24 };
    /* The right rail, stacked from the bottom the way the desktop's is. */
    o->exit_b = (struct openmmo_rect){ sw - 208, sh - 34, 200, 26 };
    o->settings_b = (struct openmmo_rect){ sw - 208, sh - 66, 200, 26 };
    o->rom_b = (struct openmmo_rect){ sw - 208, sh - 98, 200, 26 };
}

static void fd_draw(struct frontdoor *d, int sw, int sh, const char *who,
                    const struct fd_rects *R)
{
    char masked[FD_PASS_MAX], line[MMO_TOKEN_NAME + 40];
    size_t i;

    /* The picture, covering the window the way the desktop door covers
     * its own; the flat launcher ground where the art is not on the
     * device. */
    SDL_SetRenderDrawColor(d->ren, 0x1c, 0x22, 0x28, 255);
    SDL_RenderClear(d->ren);
    if (d->bg != NULL) {
        SDL_Rect dst = { 0, 0, sw, sh };
        SDL_Rect src;
        /* Cover, not stretch: crop the art to the window's own shape. */
        int vw = d->bg_w, vh = d->bg_h;

        if (vw * sh > vh * sw)
            vw = vh * sw / sh;
        else
            vh = vw * sh / sw;
        src.x = (d->bg_w - vw) / 2;
        src.y = (d->bg_h - vh) / 2;
        src.w = vw;
        src.h = vh;
        SDL_RenderCopy(d->ren, d->bg, &src, &dst);
    }

    if (d->focus == FOCUS_NONE) {
        if (d->mark != NULL) {
            int mw = d->mark_w, mh = d->mark_h;
            SDL_Rect dst;

            if (mw > sw * 48 / 100) {
                mh = mh * (sw * 48 / 100) / mw;
                mw = sw * 48 / 100;
            }
            dst.x = (sw - mw) / 2;
            dst.y = sh * 16 / 100;
            dst.w = mw;
            dst.h = mh;
            SDL_RenderCopy(d->ren, d->mark, NULL, &dst);
        } else {
            int w = view_ui_text_width(&d->gpu, "OPENMMO");

            fd_text(d, (sw - w) / 2, sh * 16 / 100, "OPENMMO", 0xFFFFFFu);
        }
    }

    fd_frame(d, &R->panel, "LOGIN");
    fd_text(d, R->user_b.x, R->user_b.y - 20, "Username:", 0xF2F2F2u);
    fd_text(d, R->pass_b.x, R->pass_b.y - 20, "Password:", 0xF2F2F2u);
    fd_field(d, &R->user_b, d->user, d->focus == FOCUS_USER, "account name");
    if (R->saved) {
        fd_text(d, R->pass_b.x + 2, R->pass_b.y + 5, "not needed", 0x929AA2u);
    } else {
        /* The official client's editfield masks with '#' (passwordChar 35). */
        for (i = 0; d->pass[i] != '\0' && i < sizeof masked - 1; i++)
            masked[i] = '#';
        masked[i] = '\0';
        fd_field(d, &R->pass_b, masked, d->focus == FOCUS_PASS, "password");
    }

    view_ui_fill(d->ren, R->rem_b.x, R->rem_b.y, R->rem_b.w, R->rem_b.h,
                 0x16, 0x1a, 0x20, 255);
    view_ui_border(d->ren, R->rem_b.x, R->rem_b.y, R->rem_b.w, R->rem_b.h,
                   0x6a, 0x88, 0x9b);
    if (d->remember)
        view_ui_fill(d->ren, R->rem_b.x + 4, R->rem_b.y + 4, R->rem_b.w - 8,
                     R->rem_b.h - 8, 0x81, 0xdd, 0xf1, 255);
    fd_text(d, R->rem_b.x + R->rem_b.w + 8, R->rem_b.y + 1,
            "Remember My Name", 0xF2F2F2u);

    /* The desktop's own move: the big button is the rom flow until a rom is
     * ready, so choosing one never means finding the right small button
     * first. */
    fd_button(d, &R->login_b, d->rom_ok ? "LOGIN" : "CHOOSE ROM...");

    /* And its footer, in the desktop's colours: the ROM's one line, because
     * a first run cannot start without one and a player should never have to
     * guess which of these is missing. Green when the game could start. */
    fd_text(d, 12, sh - 26, d->rom_why,
            d->rom_ok ? 0x96DE96u : 0xF0B096u);

    if (R->saved) {
        struct openmmo_rect note = { R->panel.x + 20, R->panel.y + 52 + 112,
                                     R->panel.w - 40, 50 };

        view_ui_fill(d->ren, note.x, note.y, note.w, note.h, 26, 34, 30, 255);
        view_ui_border(d->ren, note.x, note.y, note.w, note.h, 58, 92, 74);
        snprintf(line, sizeof line, "Signed in as %.24s", who);
        fd_text(d, note.x + 12, note.y + 6, line, 0x96DE96u);
        fd_text(d, note.x + 12, note.y + 26,
                "No password is stored on this device.", 0x929AA2u);
        fd_button(d, &R->out_b, "Sign out");
    }

    fd_button(d, &R->settings_b, "SETTINGS");
    fd_button(d, &R->rom_b, "CARTRIDGE");
    fd_button(d, &R->exit_b, "EXIT");

    /* Last, so it sits over the door rather than under it, and only here:
     * the settings and picker faces present themselves and are not handling
     * its taps. It draws nothing until the channel has answered, and on most
     * runs it never draws at all. */
    mmo_update_notice_draw(d->ren, &d->gpu, sw, sh);
    SDL_RenderPresent(d->ren);
}

/* ------------------------------------------------------------------ */
/* The loop                                                            */
/* ------------------------------------------------------------------ */

static void fd_focus(struct frontdoor *d, int what)
{
    if (d->focus == what)
        return;
    d->focus = what;
    if (what == FOCUS_NONE)
        SDL_StopTextInput();
    else
        SDL_StartTextInput();
}

static void fd_type(char *buf, size_t cap, const char *text)
{
    size_t at = strlen(buf), i;

    for (i = 0; text[i] != '\0'; i++) {
        unsigned char c = (unsigned char)text[i];

        /* An account name and a password are ASCII on the wire; the odd
         * multi-byte press from the keyboard is dropped, not mangled. */
        if (c >= 32 && c < 127 && at + 1 < cap)
            buf[at++] = (char)c;
    }
    buf[at] = '\0';
}

int mmo_frontdoor_run(const char *art_dir, char *user, size_t ucap,
                      char *pass, size_t pcap)
{
    struct frontdoor d;
    SDL_Window *win;
    char who[MMO_TOKEN_NAME];
    int sw = 0, sh = 0, submitted = 0, want_exit = 0;

    memset(&d, 0, sizeof d);
    /* The renderer reads OPENMMO_UI_SCALE as it comes up, so the setting
     * must be in the environment before it does, which is why the file is
     * read here and not after the window exists. */
    fd_settings_load(&d.set);
    {
        char buf[8];

        snprintf(buf, sizeof buf, "%d", d.set.ui_scale);
        setenv("OPENMMO_UI_SCALE", buf, 0);
    }
    win = SDL_CreateWindow("openmmo", 0, 0, 0, 0, 0);
    d.ren = SDL_CreateRenderer(win, -1, 0);
    if (d.ren == NULL)
        return 0;               /* no screen to ask on: token or env decide */
    view_ui_gpu_init(&d.gpu);
    if (!view_ui_font_ready(&d.gpu, d.ren, 16))
        fprintf(stderr, "front door: no face to draw text with\n");
    if (art_dir != NULL) {
        d.bg = fd_texture(d.ren, art_dir, "bg.png", &d.bg_w, &d.bg_h);
        d.mark = fd_texture(d.ren, art_dir, "wordmark.png", &d.mark_w,
                            &d.mark_h);
    }
    snprintf(d.user, sizeof d.user, "%s", d.set.user);
    d.remember = d.user[0] != '\0';

    /* No auto-opened browser: the desktop never does that either. With no
     * rom chosen the big button reads CHOOSE ROM... and the footer says why,
     * which is the whole path. Force-firing a picker Intent at a player who
     * has not touched anything yet is how an app feels broken. */

    /* Ask the channel whether this build is behind, on a thread of its own
     * so a slow answer costs the door nothing. The panel appears if and when
     * one arrives (mmo_update_notice.h); an app cannot patch itself, so the
     * whole of what it can do is say so and offer the download. */
    mmo_update_notice_ask();

    while (!submitted && !want_exit) {
        SDL_Event ev;
        struct fd_rects R;
        struct fd_srects S;
        const char *saved_who = NULL;
        int saved;

        SDL_GetRendererOutputSize(d.ren, &sw, &sh);
        /* Asked fresh every frame, the way the desktop's rom_status is: a
         * cartridge deleted while this face is up flips the button and the
         * footer the moment it happens, instead of at the next launch. */
        if (d.rom_wait) {
            char uri[512];

            if (mmo_android_rom_picked(uri, sizeof uri)) {
                d.rom_wait = 0;
                /* Empty is the player backing out of the picker, which is a
                 * decision, not an error. */
                if (uri[0] != '\0') {
                    snprintf(d.set.rom, sizeof d.set.rom, "%s", uri);
                    fd_settings_save(&d.set);
                }
            }
        }
        d.rom_ok = fd_rom_status(&d.set, d.rom_why, sizeof d.rom_why);
        saved = mmo_token_who(who, sizeof who) && d.user[0] != '\0'
                && mmo_token_name_is(who, d.user);
        if (saved)
            saved_who = who;
        fd_layout(&d, sw, sh, saved, &R);
        fd_slayout(sw, sh, &S);

        if (d.on_rom) {
            struct fd_rrects RR;

            fd_rlayout(sw, sh, &RR);
            while (SDL_PollEvent(&ev)) {
                int i;

                if (ev.type == SDL_QUIT) {
                    want_exit = 1;
                } else if (ev.type == SDL_KEYDOWN
                           && ev.key.keysym.sym == SDLK_ESCAPE) {
                    d.on_rom = 0;
                } else if (ev.type == SDL_MOUSEBUTTONDOWN) {
                    if (fd_hit(&RR.cancel_b, ev.button.x, ev.button.y)) {
                        d.on_rom = 0;
                    } else if (fd_hit(&RR.up_b, ev.button.x, ev.button.y)) {
                        /* The same button is Retry while the read is denied:
                         * re-reading is the retry, and one button that always
                         * means "look again" is easier than two that differ
                         * by a state the player cannot see. */
                        if (d.rom.denied)
                            fd_rom_start(&d.rom);
                        else
                            fd_rom_up(&d.rom);
                    } else if (!d.rom.denied
                               && fd_hit(&RR.down_b, ev.button.x,
                                         ev.button.y)) {
                        if (d.rom.scroll + FD_ROM_ROWS < d.rom.n)
                            d.rom.scroll += FD_ROM_ROWS;
                    } else if (!d.rom.denied
                               && fd_hit(&RR.pgup_b, ev.button.x,
                                         ev.button.y)) {
                        d.rom.scroll -= FD_ROM_ROWS;
                        if (d.rom.scroll < 0)
                            d.rom.scroll = 0;
                    } else if (!d.rom.denied) {
                        for (i = 0; i < FD_ROM_ROWS; i++) {
                            int at = d.rom.scroll + i;

                            if (at >= d.rom.n
                                || !fd_hit(&RR.row[i], ev.button.x,
                                           ev.button.y))
                                continue;
                            if (d.rom.e[at].is_dir) {
                                char next[FD_ROM_PATH];

                                snprintf(next, sizeof next, "%s/%s",
                                         d.rom.dir, d.rom.e[at].name);
                                fd_rom_open(&d.rom, next);
                            } else {
                                /*
                                 * The desktop's pick, exactly: the PATH is the setting, saved
                                 * on the spot, and the browser closes onto the login face
                                 * whose footer now answers for it. No copy: what the player
                                 * deletes is gone, and the face says so the next frame.
                                 */
                                snprintf(d.set.rom, sizeof d.set.rom,
                                         "%s/%s", d.rom.dir,
                                         d.rom.e[at].name);
                                fd_settings_save(&d.set);
                                d.on_rom = 0;
                            }
                            break;
                        }
                    }
                }
            }
            fd_draw_rom(&d, sw, sh, &RR);
            SDL_RenderPresent(d.ren);
            continue;
        }

        if (d.on_settings) {
            while (SDL_PollEvent(&ev)) {
                int i;

                if (ev.type == SDL_QUIT) {
                    want_exit = 1;
                } else if (ev.type == SDL_MOUSEBUTTONDOWN) {
                    if (fd_hit(&S.done_b, ev.button.x, ev.button.y)) {
                        d.on_settings = 0;
                        d.set.user[0] = '\0';
                        if (d.remember)
                            snprintf(d.set.user, sizeof d.set.user, "%s",
                                     d.user);
                        fd_settings_save(&d.set);
                    }
                    for (i = 0; i < FD_SROWS; i++) {
                        if (fd_hit(&S.dec[i], ev.button.x, ev.button.y))
                            fd_srow_step(&d.set, i, -1);
                        else if (fd_hit(&S.inc[i], ev.button.x, ev.button.y))
                            fd_srow_step(&d.set, i, +1);
                    }
                } else if (ev.type == SDL_KEYDOWN
                           && ev.key.keysym.sym == SDLK_ESCAPE) {
                    d.on_settings = 0;
                }
            }
            fd_draw_settings(&d, sw, sh, &S);
            continue;
        }

        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                want_exit = 1;
                break;
            case SDL_MOUSEBUTTONDOWN:
                /*
                 * The update panel is modal while it is up: it takes every tap, including the
                 * ones that miss its two buttons. A door that let a press through to LOGIN
                 * from behind a panel the player cannot see past would be pressing a button
                 * they did not aim at.
                 */
                if (mmo_update_notice_tap(ev.button.x, ev.button.y, sw, sh))
                    break;
                if (fd_hit(&R.user_b, ev.button.x, ev.button.y)) {
                    fd_focus(&d, FOCUS_USER);
                } else if (!saved
                           && fd_hit(&R.pass_b, ev.button.x, ev.button.y)) {
                    fd_focus(&d, FOCUS_PASS);
                } else if (fd_hit(&R.rem_b, ev.button.x, ev.button.y)) {
                    d.remember = !d.remember;
                } else if (fd_hit(&R.login_b, ev.button.x, ev.button.y)) {
                    /* No ROM, no LOGIN, the desktop's rule: the press that
                     * cannot start the game opens the browser instead, which
                     * is what the button said it would do. */
                    if (d.rom_ok) {
                        submitted = 1;
                    } else {
                        fd_focus(&d, FOCUS_NONE);
                        fd_rom_ask(&d);
                    }
                } else if (saved
                           && fd_hit(&R.out_b, ev.button.x, ev.button.y)) {
                    mmo_token_clear();
                } else if (fd_hit(&R.settings_b, ev.button.x, ev.button.y)) {
                    fd_focus(&d, FOCUS_NONE);
                    d.on_settings = 1;
                } else if (fd_hit(&R.rom_b, ev.button.x, ev.button.y)) {
                    fd_focus(&d, FOCUS_NONE);
                    fd_rom_ask(&d);
                } else if (fd_hit(&R.exit_b, ev.button.x, ev.button.y)) {
                    want_exit = 1;
                } else {
                    fd_focus(&d, FOCUS_NONE);
                }
                break;
            case SDL_TEXTINPUT:
                if (d.focus == FOCUS_USER)
                    fd_type(d.user, sizeof d.user, ev.text.text);
                else if (d.focus == FOCUS_PASS)
                    fd_type(d.pass, sizeof d.pass, ev.text.text);
                break;
            case SDL_KEYDOWN:
                if (ev.key.keysym.sym == SDLK_BACKSPACE) {
                    char *buf = d.focus == FOCUS_USER ? d.user
                                : d.focus == FOCUS_PASS ? d.pass : NULL;

                    if (buf != NULL && buf[0] != '\0')
                        buf[strlen(buf) - 1] = '\0';
                } else if (ev.key.keysym.sym == SDLK_RETURN) {
                    /* The keyboard's send: the name field hands over to the
                     * password; the password (or a saved sign-in) is the
                     * press. */
                    if (d.focus == FOCUS_USER && !saved)
                        fd_focus(&d, FOCUS_PASS);
                    else if (d.focus != FOCUS_NONE) {
                        if (d.rom_ok) {
                            submitted = 1;
                        } else {
                            fd_focus(&d, FOCUS_NONE);
                            fd_rom_ask(&d);
                        }
                    }
                } else if (ev.key.keysym.sym == SDLK_ESCAPE) {
                    fd_focus(&d, FOCUS_NONE);
                }
                break;
            default:
                break;
            }
        }

        /* An empty name cannot be a session; the press just keeps the form. */
        if (submitted && d.user[0] == '\0') {
            submitted = 0;
            fd_focus(&d, FOCUS_USER);
        }

        fd_draw(&d, sw, sh, saved_who, &R);
    }

    fd_focus(&d, FOCUS_NONE);
    d.set.user[0] = '\0';
    if (d.remember)
        snprintf(d.set.user, sizeof d.set.user, "%s", d.user);
    fd_settings_save(&d.set);
    if (d.bg != NULL)
        SDL_DestroyTexture(d.bg);
    if (d.mark != NULL)
        SDL_DestroyTexture(d.mark);
    view_ui_gpu_free(&d.gpu);
    if (want_exit)
        return 1;
    snprintf(user, ucap, "%s", d.user);
    snprintf(pass, pcap, "%s", d.pass);
    return 0;
}
