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

#include "mmo_frontdoor_launch.h"
#include "mmo_device_rules.h"

#ifdef __ANDROID__
#include <android/asset_manager.h>
#endif

/* The device's own defaults (mmo_device.c), weak so this file still links
 * into a build that has no probe: then the constants below stand. */
void mmo_device_defaults(struct mmo_device_defaults *out)
    __attribute__((weak));

#define FD_USER_MAX 64
#define FD_PASS_MAX 64

enum { FOCUS_NONE = 0, FOCUS_USER, FOCUS_PASS };

/*
 * The settings, in the desktop file's own keys (fd_settings_load below says
 * which). Held on the door so the settings face edits what LOGIN applies.
 */
struct fd_settings {
    char user[FD_USER_MAX];
    int hd3d;          /* 2 sd, 3 hd, 4 ultra; sd, hd or ultra in the file */
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
    char rom_hg[512];  /* the Heart Gold cartridge, the desktop's rom-hg */
    char rom_bw[512];  /* the Black cartridge, the desktop's rom-bw */
    int touch;         /* 0 off, 1 auto, 2 on, the drawn pad */
    int touch_alpha;   /* 0..100 */
    int touch_size;    /* 60..140, percent */
    int soundtrack;    /* 0 platinum, 1 heartgold, 2 blackwhite */
    /*
     * Whether this install walks into Johto and Kanto: the `hgss` world package, named or not
     * named.
     */
    int world;         /* 0 sinnoh only, 1 johto and kanto as well */
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
    /* The three cartridge slots, each asked every frame the way rom_ok is,
     * and the slot the picker that is up is answering for. */
    int slot_ok[3];
    char slot_why[3][160];
    int rom_slot;                /* the slot the picker that is up answers for */
    int slots_ready;             /* how many of the three are, for the footer */
    char warn[120];              /* LOGIN pressed with a cartridge missing */
    int on_carts;                /* the cartridges face owns the glass */
    int on_restore;              /* the restore face does */
    /* One line under the panel: what the last run left, what this press
     * did, what is being copied or composed right now. */
    char note[240];
    /* The offline save waiting to go online, asked about once a second the
     * way the desktop's face asks, and the box the player ticks. */
    int offered, offer_age, take;
    long offer_save, offer_server;
    /* The stamped saves the restore face lists. */
    char saves[MMO_LAUNCH_SAVE_KEEP][MMO_LAUNCH_STAMP];
    int nsaves;
    int offline;                 /* PLAY OFFLINE was the press */
    /* What a progress line is drawn over while a press is being prepared. */
    int sw, sh;
    const struct fd_rects *R;
    const char *who;
};

/* ------------------------------------------------------------------ */
/* Art and small files                                                 */
/* ------------------------------------------------------------------ */

/*
 * The package's own copy of the art. The desktop finds bg.png and wordmark.png beside its
 * program; an app has no beside, so they travel as assets and the activity hands the manager
 * over before the door opens.
 */
#ifdef __ANDROID__
static AAssetManager *fd_assets;
#endif

void mmo_frontdoor_assets(void *mgr)
{
#ifdef __ANDROID__
    fd_assets = (AAssetManager *)mgr;
#else
    (void)mgr;
#endif
}

static unsigned char *fd_asset_slurp(const char *name, size_t *len)
{
#ifdef __ANDROID__
    char path[128];
    AAsset *a;
    unsigned char *buf;
    off_t n;

    if (fd_assets == NULL)
        return NULL;
    snprintf(path, sizeof path, "launcher/%s", name);
    a = AAssetManager_open(fd_assets, path, AASSET_MODE_BUFFER);
    if (a == NULL)
        return NULL;
    n = AAsset_getLength(a);
    buf = n > 0 ? malloc((size_t)n) : NULL;
    if (buf == NULL || AAsset_read(a, buf, (size_t)n) != n) {
        free(buf);
        AAsset_close(a);
        return NULL;
    }
    AAsset_close(a);
    *len = (size_t)n;
    return buf;
#else
    (void)name;
    (void)len;
    return NULL;
#endif
}

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

    buf = NULL;
    if (dir != NULL) {
        snprintf(path, sizeof path, "%s/%s", dir, name);
        buf = fd_slurp(path, &len);
    }
    if (buf == NULL)
        buf = fd_asset_slurp(name, &len);
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
/* 1 is AUTO, the device's own answer (mmo_device_rules.c) capped by what it
 * measured last time (mmo_calibrate.c); 2..4 are the player's own. */
static const char *const fd_hd3d[4]    = { "auto", "sd", "hd", "ultra" };
/*
 * AUTO is the default and is not a fence-sit: a phone never sends a gamepad keycode and keeps
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
    s->hd3d = 1;               /* auto: the device decides, see fd_hd3d */
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
    s->world = 0;              /* Sinnoh only, until somebody says otherwise */
    /* The two ROWS that are the device'S to default. */
    if (mmo_device_defaults != NULL) {
        struct mmo_device_defaults d;

        mmo_device_defaults(&d);
        s->ui_scale = d.ui_scale < 1 ? 1 : d.ui_scale > 4 ? 4 : d.ui_scale;
    }
}

/* What AUTO means right now: the rules' answer for this device, capped by
 * what it measured in earlier sessions. Asked when the row is shown and
 * when it is applied, never stored, so the file keeps saying `auto` and a
 * later session with better evidence answers differently. */
static int fd_hd3d_auto(void)
{
    struct mmo_device_defaults d;

    if (mmo_device_defaults == NULL)
        return 2;
    mmo_device_defaults(&d);
    return d.hd3d < 2 ? 2 : d.hd3d > 4 ? 4 : d.hd3d;
}

static int fd_hd3d_resolved(const struct fd_settings *s)
{
    return s->hd3d <= 1 ? fd_hd3d_auto() : s->hd3d;
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

    if (strcmp(v, "auto") == 0) return 1;
    if (strcmp(v, "sd") == 0) return 2;
    if (strcmp(v, "hd") == 0) return 3;
    if (strcmp(v, "ultra") == 0) return 4;
    n = atoi(v);
    return n <= 1 ? 1 : n >= 4 ? 4 : n;
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
        /* camera-distance is read as nothing, the desktop's own retirement
         * of it: the field camera stands at 130 percent in every session. */
        else if (strcmp(line, "hd3d") == 0)
            s->hd3d = fd_hd3d_value(sp + 1);
        /* sound is read as nothing for the same reason: the mixer runs at
         * 48,000 with cubic interpolation in every session, and a file
         * written while it was a row still has to start the door. */
        else if (strcmp(line, "rom") == 0)
            snprintf(s->rom, sizeof s->rom, "%s", sp + 1);
        else if (strcmp(line, "rom-hg") == 0)
            snprintf(s->rom_hg, sizeof s->rom_hg, "%s", sp + 1);
        else if (strcmp(line, "rom-bw") == 0)
            snprintf(s->rom_bw, sizeof s->rom_bw, "%s", sp + 1);
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
        else if (strcmp(line, "world") == 0)
            s->world = strcmp(sp + 1, "hgss") == 0;
    }
    fclose(f);
}

static void fd_settings_save(const struct fd_settings *s)
{
    char path[560], tmp[576];
    FILE *f;

    fd_cfg_path(path, sizeof path);
    if (path[0] == '\0')
        return;
    /* Written beside it and renamed over it. */
    snprintf(tmp, sizeof tmp, "%s.new", path);
    f = fopen(tmp, "w");
    if (f == NULL) {
        /* Said out loud, because this exact failure once ate the settings,
         * the token and the cartridge choice at the same time and left
         * nothing anywhere naming it. */
        fprintf(stderr, "frontdoor: cannot save settings to %s: %s\n",
                tmp, strerror(errno));
        return;
    }
    fprintf(f, "# openmmo settings; delete this file for defaults\n");
    if (s->user[0] != '\0')
        fprintf(f, "user %s\n", s->user);
    fprintf(f, "hd3d %s\n", fd_hd3d[fd_clampi(s->hd3d, 1, 4) - 1]);
    fprintf(f, "viewport %s\n", s->viewport ? "native" : "auto");
    fprintf(f, "filter %s\n", fd_filters[s->filter]);
    fprintf(f, "render-scale %d\n", s->rs);
    fprintf(f, "layout %s\n", fd_layouts[s->layout]);
    /* Named rather than on/off, so the row still says which world the day a
     * second one exists and an older build reading this file gets Sinnoh
     * rather than a region it cannot draw. */
    fprintf(f, "world %s\n", s->world ? "hgss" : "sinnoh");
    fprintf(f, "ui-scale %d\n", s->ui_scale);
    fprintf(f, "music %d\n", s->music);
    fprintf(f, "sfx %d\n", s->sfx);
    fprintf(f, "button-mode %s\n", fd_buttons[s->button]);
    if (s->rom[0] != '\0')
        fprintf(f, "rom %s\n", s->rom);
    if (s->rom_hg[0] != '\0')
        fprintf(f, "rom-hg %s\n", s->rom_hg);
    if (s->rom_bw[0] != '\0')
        fprintf(f, "rom-bw %s\n", s->rom_bw);
    fprintf(f, "touch %s\n", fd_touch[s->touch]);
    fprintf(f, "touch-alpha %d\n", s->touch_alpha);
    fprintf(f, "touch-size %d\n", s->touch_size);
    fprintf(f, "soundtrack %s\n", fd_tracks[s->soundtrack]);
    if (fclose(f) != 0 || rename(tmp, path) != 0) {
        fprintf(stderr, "frontdoor: cannot save settings to %s: %s\n",
                path, strerror(errno));
        remove(tmp);
    }
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

/* Ask for a cartridge for one slot: the system picker when the device has
 * one, the drawn browser when it does not. Every choose-a-rom press funnels
 * through here so the two routes cannot drift. */
struct frontdoor;
static void fd_rom_ask(struct frontdoor *d, int slot);

/* The slot a setting belongs to, both ways. */
static const char *fd_slot_get(const struct fd_settings *s, int slot)
{
    return slot == MMO_LAUNCH_CART_HEARTGOLD ? s->rom_hg
         : slot == MMO_LAUNCH_CART_BLACK     ? s->rom_bw : s->rom;
}

static void fd_slot_set(struct fd_settings *s, int slot, const char *v)
{
    char *dst = slot == MMO_LAUNCH_CART_HEARTGOLD ? s->rom_hg
              : slot == MMO_LAUNCH_CART_BLACK     ? s->rom_bw : s->rom;

    snprintf(dst, sizeof s->rom, "%s", v);
}

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

/* A granted document's header, read off its descriptor: a picked cartridge
 * has no path, and sixteen bytes say what it is without copying it. */
static int fd_uri_slot_ok(const char *uri, int slot, char *why, size_t cap)
{
    unsigned char hdr[16];
    ssize_t n;
    int fd;

    if (!mmo_android_rom_uri_ok(uri)) {
        snprintf(why, cap, "That %s cartridge cannot be read",
                 mmo_launch_cart_name(slot));
        return 0;
    }
    fd = mmo_android_rom_uri_fd(uri);
    if (fd < 0) {
        snprintf(why, cap, "That %s cartridge cannot be read",
                 mmo_launch_cart_name(slot));
        return 0;
    }
    n = read(fd, hdr, sizeof hdr);
    close(fd);
    return fdl_slot_header(slot, hdr, n > 0 ? (size_t)n : 0, why, cap);
}

/* Where the content packages live, and it is made so somebody can put one there. */
static void fd_mods_root(char *out, size_t cap)
{
    const char *ext = getenv("OPENMMO_EXTERNAL_DIR");

    snprintf(out, cap, "%s/mods", ext != NULL && ext[0] != '\0' ? ext : ".");
    if (mkdir(out, 0770) != 0 && errno != EEXIST)
        return;
    /* mkdir's mode is masked by the umask and an older build's folder is
     * already there at 0750, so the mode is said again rather than asked for. */
    chmod(out, 0770);
}

/* The folder a Platinum named by path sits in, for the desktop's own
 * convenience of finding the other two beside it. */
static int fd_platinum_dir(const struct fd_settings *s, char *out, size_t cap)
{
    char file[512];
    char *sep;

    if (strncmp(s->rom, "content://", 10) == 0
        || fd_rom_file(s->rom, file, sizeof file) != 0)
        return -1;
    sep = strrchr(file, '/');
    if (sep == NULL)
        return -1;
    *sep = '\0';
    snprintf(out, cap, "%s", file);
    return 0;
}

/*
 * One cartridge slot's line, the desktop's rom_status per slot: what it is when it works, what
 * is wrong when it does not.
 */
static int fd_slot_status(const struct fd_settings *s, int slot, char *out,
                          size_t cap)
{
    static char last[3][512];
    static char last_why[3][160];
    static int last_ok[3], age[3];
    const char *v = fd_slot_get(s, slot);
    const char *name = mmo_launch_cart_name(slot);
    char file[512], why[160], dir[512];
    const char *base;
    struct stat st;

    if (v[0] == '\0') {
        if (slot != MMO_LAUNCH_CART_PLATINUM
            && fd_platinum_dir(s, dir, sizeof dir) == 0
            && mmo_launch_cart_scan(dir, slot, file, sizeof file)) {
            base = strrchr(file, '/');
            snprintf(out, cap, "%s ready: %s (beside Platinum)", name,
                     base != NULL ? base + 1 : file);
            return 1;
        }
        snprintf(out, cap, "No %s cartridge chosen yet", name);
        return 0;
    }
    if (strncmp(v, "content://", 10) == 0) {
        if (strcmp(last[slot], v) != 0 || ++age[slot] >= 30) {
            snprintf(last[slot], sizeof last[slot], "%s", v);
            last_ok[slot] = fd_uri_slot_ok(v, slot, last_why[slot],
                                           sizeof last_why[slot]);
            age[slot] = 0;
        }
        if (!last_ok[slot]) {
            snprintf(out, cap, "%s", last_why[slot]);
            return 0;
        }
        fd_uri_name(v, file, sizeof file);
        snprintf(out, cap, "%s ready: %s", name, file);
        return 1;
    }
    if (slot == MMO_LAUNCH_CART_PLATINUM) {
        if (fd_rom_file(v, file, sizeof file) != 0) {
            if (stat(v, &st) == 0 && S_ISDIR(st.st_mode))
                snprintf(out, cap, "No pokeplatinum.us.nds in that folder");
            else
                snprintf(out, cap, "That Platinum cartridge cannot be read");
            return 0;
        }
    } else {
        snprintf(file, sizeof file, "%s", v);
    }
    if (!fdl_slot_file(slot, file, why, sizeof why)) {
        snprintf(out, cap, "%s", why);
        return 0;
    }
    base = strrchr(file, '/');
    snprintf(out, cap, "%s ready: %s", name, base != NULL ? base + 1 : file);
    return 1;
}

/* All three slots, into the door: the footer names the first one that is
 * not ready, or says so when they all are. */
static void fd_slots_status(struct frontdoor *d)
{
    int slot, first = -1;

    for (slot = 0; slot < 3; slot++) {
        d->slot_ok[slot] = fd_slot_status(&d->set, slot, d->slot_why[slot],
                                          sizeof d->slot_why[slot]);
        if (!d->slot_ok[slot] && first < 0)
            first = slot;
    }
    d->rom_ok = first < 0;
    d->slots_ready = 0;
    for (slot = 0; slot < 3; slot++)
        d->slots_ready += d->slot_ok[slot] ? 1 : 0;
    /* The count comes first when one is missing: a player who reads "No
     * Platinum cartridge chosen yet" and finds one has done a third of the
     * job, and the line should say so before it names the next slot. */
    if (d->rom_ok)
        snprintf(d->rom_why, sizeof d->rom_why,
                 "Platinum, Heart Gold and Black ready");
    else
        snprintf(d->rom_why, sizeof d->rom_why, "%d of 3 cartridges, %s",
                 d->slots_ready, d->slot_why[first]);
    if (d->rom_ok)
        d->warn[0] = '\0';
}

/*
 * Stream a granted cartridge into the boot cache. The stamp beside it names the URI and size
 * the cache was cut from, so an unchanged source is not copied twice and a swapped one never
 * boots stale.
 */
static int fd_rom_cache(int fd, const char *uri, int slot, char *out,
                        size_t cap)
{
    static const char *const names[3] = { "rom-cache.nds", "rom-cache-hg.nds",
                                          "rom-cache-bw.nds" };
    const char *ext = getenv("OPENMMO_EXTERNAL_DIR");
    char cache[560], stamp[572], tmp[572], want[700], have[700];
    struct stat st;
    FILE *sf, *o;
    char *buf;
    ssize_t got;
    long long copied = 0;
    int n;

    if (ext == NULL || ext[0] == '\0' || fstat(fd, &st) != 0)
        return -1;
    if (slot < 0 || slot > 2)
        slot = 0;
    snprintf(cache, sizeof cache, "%s/%s", ext, names[slot]);
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
        copied += (long long)got;
    }
    free(buf);
    if (fclose(o) != 0 || got < 0) {
        remove(tmp);
        fprintf(stderr, "frontdoor: the cartridge copy failed\n");
        return -1;
    }
    /* Every byte the document said it had, counted before the rename. */
    if (st.st_size > 0 && copied != (long long)st.st_size) {
        remove(tmp);
        fprintf(stderr, "frontdoor: the cartridge copy stopped at %lld of"
                " %lld bytes; keeping the old one\n", copied,
                (long long)st.st_size);
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
    /* Fixed at 130 the way the desktop plan fixes it: a
     * choice here went wrong twice on the desktop, and the device follows. */
    setenv("OPENMMO_CAMERA_DISTANCE", "130", 0);
    snprintf(buf, sizeof buf, "%d", fd_hd3d_resolved(&s));
    setenv("PC_HD3D", buf, 0);
    /*
     * The mixer's output, the same in every session and on every platform: 48,000 a second
     * with cubic interpolation between a channel's samples, which is also the rate this
     * device's AAudio opens at, so the sink's resampler drops out.
     */
    setenv("PC_AUDIO_RATE", "48000", 0);
    setenv("PC_AUDIO_INTERP", "cubic", 0);
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
                && fd_rom_cache(fd, s.rom, MMO_LAUNCH_CART_PLATINUM, romfile,
                                sizeof romfile) == 0) {
                setenv("OPENMMO_ROM", romfile, 0);
            }
            if (fd >= 0)
                close(fd);
        } else if (s.rom[0] != '\0'
                   && fd_rom_file(s.rom, romfile, sizeof romfile) == 0) {
            setenv("OPENMMO_ROM", romfile, 0);
        }
    }

    /* The drawn pad. AUTO sets nothing at all, which is how the shim spells
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
    {
        char root[512], pkg[64], probe[600], list[192];
        FILE *f;

        fd_mods_root(root, sizeof root);
        list[0] = '\0';

        /* A package is a directory with a mod.toml in it (mmo/mods/), so the
         * manifest is what proves one is really there, a bare directory
         * left behind by a half-finished push would otherwise pass. */

        /*
         * A content package names itself, first and for the strongest reason: This app has no
         * MODS row at all.
         */
        {
            static const char *const content[] = { "imports" };
            size_t i;

            for (i = 0; i < sizeof content / sizeof content[0]; i++) {
                snprintf(probe, sizeof probe, "%s/%s/mod.toml", root,
                         content[i]);
                f = fopen(probe, "rb");
                if (f == NULL) {
                    /*
                     * Said, not skipped. What this package holds is every species past 493;
                     * without it the game draws Platinum's own art and refuses the rest, which
                     * looks exactly like a client ignoring the cartridges it was handed, and
                     * has now been reported that way twice.
                     */
                    fprintf(stderr, "frontdoor: no %s package in %s, the"
                            " game plays without it, drawing this cartridge's"
                            " own art and no species past 493\n",
                            content[i], root);
                    continue;
                }
                fclose(f);
                if (list[0] != '\0')
                    snprintf(list + strlen(list), sizeof list - strlen(list),
                             ",%s", content[i]);
                else
                    snprintf(list, sizeof list, "%s", content[i]);
                fprintf(stderr, "frontdoor: content package %s\n", content[i]);
            }
        }

        /* And the world, when the player has asked for it. */
        if (s.world) {
            snprintf(probe, sizeof probe, "%s/hgss/mod.toml", root);
            f = fopen(probe, "rb");
            if (f != NULL) {
                fclose(f);
                if (list[0] != '\0')
                    snprintf(list + strlen(list), sizeof list - strlen(list),
                             ",hgss");
                else
                    snprintf(list, sizeof list, "hgss");
                fprintf(stderr, "frontdoor: Johto and Kanto (hgss)\n");
            } else {
                fprintf(stderr, "frontdoor: Johto and Kanto asked for, but"
                        " %s/hgss is not on this device, Sinnoh is the only"
                        " region this session can walk into\n", root);
            }
        }

        /* The follower package names itself too, because nobody can type it
         * either: it is filled from the player's own cartridge at the press
         * (fd_go) rather than shipped, so unlike a soundtrack there is no
         * setting to read. The desktop plan does the same in plan_build. */
        snprintf(probe, sizeof probe, "%s/followers/mod.toml", root);
        f = fopen(probe, "rb");
        if (f != NULL) {
            fclose(f);
            if (list[0] != '\0')
                snprintf(list + strlen(list), sizeof list - strlen(list),
                         ",followers");
            else
                snprintf(list, sizeof list, "followers");
            fprintf(stderr, "frontdoor: a Pokemon walks behind you"
                    " (followers)\n");
        }

        if (s.soundtrack != 0) {
            snprintf(pkg, sizeof pkg, "sound_%s_%s",
                     fd_track_pkg[s.soundtrack], fd_track_pkg[s.soundtrack]);
            snprintf(probe, sizeof probe, "%s/%s/mod.toml", root, pkg);
            f = fopen(probe, "rb");
            if (f != NULL) {
                fclose(f);
                /* Last, so its one claim over the sound archive wins, which is
                 * the order the desktop plan appends it in too. */
                if (list[0] != '\0')
                    snprintf(list + strlen(list), sizeof list - strlen(list),
                             ",%s", pkg);
                else
                    snprintf(list, sizeof list, "%s", pkg);
                fprintf(stderr, "frontdoor: soundtrack %s (%s)\n",
                        fd_tracks[s.soundtrack], pkg);
            } else {
                /*
                 * Refused BY NAME when it is not there, which is the whole reason this checks
                 * rather than just setting the variable.
                 */
                fprintf(stderr, "frontdoor: soundtrack %s asked for, but %s/%s"
                        " is not on this device, the cartridge's own music"
                        " plays instead\n",
                        fd_tracks[s.soundtrack], root, pkg);
            }
        }

        /* One list, set once. setenv here does not overwrite (the 0), so two
         * calls would silently keep only the first package and drop the
         * other. */
        if (list[0] != '\0') {
            setenv("PC_MODS_DIR", root, 0);
            setenv("PC_MODS", list, 0);
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
    /* The updater's own button on the rail; it writes the label
     * (mmo_update_notice_label), the door only draws it. */
    struct openmmo_rect update_b;
    /* The desktop's second row and its two neighbours: PLAY OFFLINE, the
     * restore button beside it, and the take-my-save-online box above. */
    struct openmmo_rect off_b, restore_b, take_b;
    int saved;
};

/* The cartridges face: three slots, each with its line and a CHOOSE. */
struct fd_crects {
    struct openmmo_rect panel, choose[3], done_b;
    int row_y[3];
};

/* The restore face: the stamped saves, newest first, a RESTORE each. */
struct fd_vrects {
    struct openmmo_rect panel, row[MMO_LAUNCH_SAVE_KEEP],
        put[MMO_LAUNCH_SAVE_KEEP], back_b;
};

/*
 * The settings face: the device-shaped rows of the desktop panel, each a value between < and >
 * at a finger's size, in the order a player meets their effects, the picture first, the
 * window rows after, sound last.
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
    case 0: {
        int v = fd_hd3d_resolved(s);
        const char *name = v >= 4 ? "ULTRA" : v >= 3 ? "HD" : "SD";

        if (s->hd3d <= 1)
            snprintf(out, cap, "AUTO (%s)", name);
        else
            snprintf(out, cap, "%s", name);
        break;
    }
    case 1: snprintf(out, cap, "%s", s->viewport ? "native" : "auto"); break;
    case 2: snprintf(out, cap, "%s", fd_filters[s->filter]); break;
    case 3: snprintf(out, cap, "%dx", s->rs); break;
    case 4: snprintf(out, cap, "%s", fd_layouts[s->layout]); break;
    case 5: snprintf(out, cap, "%dx", s->ui_scale); break;
    case 6: snprintf(out, cap, "%d", s->music); break;
    case 7: snprintf(out, cap, "%d", s->sfx); break;
    /* Explicit, not the default it used to be. The default arm here and in
     * fd_srow_step is the last row, so a row added without a case of its own
     * does not sit inert, it silently edits that one instead. Every row
     * above the last has its own case for exactly that reason. */
    case 8: snprintf(out, cap, "%s", fd_buttons[s->button]); break;
    case 9: snprintf(out, cap, "%s", fd_touch[s->touch]); break;
    case 10: snprintf(out, cap, "%d%%", s->touch_alpha); break;
    case 11: snprintf(out, cap, "%d%%", s->touch_size); break;
    case 12: snprintf(out, cap, "%s", fd_tracks[s->soundtrack]); break;
    default: snprintf(out, cap, "%s",
                      s->world ? "Johto and Kanto" : "Sinnoh only"); break;
    }
}

static void fd_srow_step(struct fd_settings *s, int i, int dir)
{
    switch (i) {
    /* Up to the port's own 4x, the desktop's ultra: the port drops pictures
     * rather than pacing when a device cannot keep up, so the row is the
     * player's to try. */
    case 0: s->hd3d = fd_clampi(s->hd3d + dir, 1, 4); break;
    case 1: s->viewport = !s->viewport; break;
    case 2: s->filter = (s->filter + dir + 3) % 3; break;
    case 3: s->rs = fd_clampi(s->rs + dir, 1, 4); break;
    case 4: s->layout = (s->layout + dir + 4) % 4; break;
    case 5: s->ui_scale = fd_clampi(s->ui_scale + dir, 1, 4); break;
    case 6: s->music = fd_clampi(s->music + dir * 10, 0, 100); break;
    case 7: s->sfx = fd_clampi(s->sfx + dir * 10, 0, 100); break;
    case 8: s->button = (s->button + dir + 3) % 3; break;
    case 9: s->touch = (s->touch + dir + 3) % 3; break;
    case 10: s->touch_alpha = fd_clampi(s->touch_alpha + dir * 10, 0, 100);
             break;
    case 11: s->touch_size = fd_clampi(s->touch_size + dir * 10, 60, 140);
             break;
    case 12: s->soundtrack = (s->soundtrack + dir + 3) % 3; break;
    default: s->world = !s->world; break;
    }
}

static const char *const fd_srow_label[FD_SROWS] = {
    "3D resolution", "Viewport", "Filter", "Render scale", "Layout",
    "UI scale (next start)", "Music", "SFX", "Button mode",
    "Touch controls", "Pad opacity", "Pad size", "Soundtrack", "World"
};

static void fd_rom_ask(struct frontdoor *d, int slot)
{
    d->rom_slot = slot;
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
    snprintf(line, sizeof line, "CHOOSE YOUR %s CARTRIDGE (%d OF 3)",
             mmo_launch_cart_name(d->rom_slot), d->rom_slot + 1);
    for (i = 0; line[i] != '\0'; i++)
        if (line[i] >= 'a' && line[i] <= 'z')
            line[i] = (char)(line[i] - 'a' + 'A');
    fd_text(d, R->panel.x + 14, R->panel.y + 10, line, 0xF2F2F2u);

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

/*
 * The picture, covering the window the way the desktop door covers its own, on every face; the
 * flat launcher ground where the art is not to be had. Cover, not stretch: crop the art to the
 * window's own shape.
 */
static void fd_draw_bg(struct frontdoor *d, int sw, int sh)
{
    SDL_SetRenderDrawColor(d->ren, 0x1c, 0x22, 0x28, 255);
    SDL_RenderClear(d->ren);
    if (d->bg != NULL) {
        SDL_Rect dst = { 0, 0, sw, sh };
        SDL_Rect src;
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
}

static void fd_draw_settings(struct frontdoor *d, int sw, int sh,
                             const struct fd_srects *S)
{
    int i;

    fd_draw_bg(d, sw, sh);
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

    /* The desktop's own arithmetic: a row for PLAY OFFLINE, and one more
     * for the offer while a save is waiting to go online. */
    wh += 44;
    if (d->offered)
        wh += 42;
    if (ww > sw - 40)
        ww = sw - 40;
    col_w = (ww - pad * 2 - gap) / 2;
    if (d->focus != FOCUS_NONE) {
        /* The keyboard owns the bottom of the glass; the frame yields to
         * it, and the window is already laid out in the rows above it. */
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
    o->off_b = (struct openmmo_rect){ wx + pad, wy + wh - 40, col_w, 30 };
    o->restore_b = (struct openmmo_rect){ wx + pad + col_w + gap, wy + wh - 40,
                                          col_w, 30 };
    o->take_b = (struct openmmo_rect){ wx + pad, wy + wh - 40 - 36, 20, 20 };
    /* The right rail, stacked from the bottom the way the desktop's is. */
    o->exit_b = (struct openmmo_rect){ sw - 208, sh - 34, 200, 26 };
    o->settings_b = (struct openmmo_rect){ sw - 208, sh - 66, 200, 26 };
    o->rom_b = (struct openmmo_rect){ sw - 208, sh - 98, 200, 26 };
    o->update_b = (struct openmmo_rect){ sw - 208, sh - 130, 200, 26 };
}

static void fd_clayout(int sw, int sh, struct fd_crects *o)
{
    int ww = 480, wh = 52 + 3 * 70 + 56;
    int wx, wy, i;

    if (ww > sw - 40)
        ww = sw - 40;
    wx = (sw - ww) / 2;
    wy = (sh - wh) / 2;
    if (wy < 4)
        wy = 4;
    o->panel = (struct openmmo_rect){ wx, wy, ww, wh };
    for (i = 0; i < 3; i++) {
        o->row_y[i] = wy + 46 + i * 70;
        o->choose[i] = (struct openmmo_rect){ wx + ww - 16 - 120,
                                              o->row_y[i] + 4, 120, 32 };
    }
    o->done_b = (struct openmmo_rect){ wx + ww - 16 - 120, wy + wh - 44,
                                       120, 32 };
}

static void fd_vlayout(int sw, int sh, int n, struct fd_vrects *o)
{
    int ww = 460, pitch = 36, wh = 52 + (n > 0 ? n : 1) * pitch + 76;
    int wx, wy, i;

    if (ww > sw - 40)
        ww = sw - 40;
    if (wh > sh - 8) {
        pitch = (sh - 8 - 52 - 76) / (n > 0 ? n : 1);
        if (pitch < 22)
            pitch = 22;
        wh = 52 + (n > 0 ? n : 1) * pitch + 76;
    }
    wx = (sw - ww) / 2;
    wy = (sh - wh) / 2;
    if (wy < 4)
        wy = 4;
    o->panel = (struct openmmo_rect){ wx, wy, ww, wh };
    for (i = 0; i < MMO_LAUNCH_SAVE_KEEP; i++) {
        int ry = wy + 46 + i * pitch;

        o->row[i] = (struct openmmo_rect){ wx + 16, ry, ww - 32, pitch - 4 };
        o->put[i] = (struct openmmo_rect){ wx + ww - 16 - 110, ry + 1, 110,
                                           pitch - 6 };
    }
    o->back_b = (struct openmmo_rect){ wx + ww - 16 - 120, wy + wh - 44,
                                       120, 32 };
}

/* `YYYYmmdd-HHMMSS` the way a person reads a clock. */
static void fd_stamp_text(const char *stamp, char *out, size_t cap)
{
    if (strlen(stamp) < 15) {
        snprintf(out, cap, "%s", stamp);
        return;
    }
    snprintf(out, cap, "%.4s-%.2s-%.2s %.2s:%.2s", stamp, stamp + 4,
             stamp + 6, stamp + 9, stamp + 11);
}

static void fd_draw_carts(struct frontdoor *d, int sw, int sh,
                          const struct fd_crects *C)
{
    int i;

    fd_draw_bg(d, sw, sh);
    fd_frame(d, &C->panel, "CARTRIDGES");
    for (i = 0; i < 3; i++) {
        fd_text(d, C->panel.x + 16, C->row_y[i] + 2, mmo_launch_cart_name(i),
                0xF2F2F2u);
        fd_text(d, C->panel.x + 16, C->row_y[i] + 24, d->slot_why[i],
                d->slot_ok[i] ? 0x96DE96u : 0xF0B096u);
        fd_button(d, &C->choose[i], "CHOOSE...");
    }
    /* Above the DONE row, not beside it: on the same line the sentence ran
     * under the button. */
    fd_text(d, C->panel.x + 16, C->done_b.y - 26,
            "Your own backups of all three; Play needs every one.", 0x929AA2u);
    fd_button(d, &C->done_b, "DONE");
    SDL_RenderPresent(d->ren);
}

static void fd_draw_saves(struct frontdoor *d, int sw, int sh,
                          const struct fd_vrects *V)
{
    int i;

    fd_draw_bg(d, sw, sh);
    fd_frame(d, &V->panel, "RESTORE SAVE");
    if (d->nsaves == 0)
        fd_text(d, V->row[0].x, V->row[0].y + 5, "No earlier save is kept yet.",
                0x929AA2u);
    for (i = 0; i < d->nsaves; i++) {
        char when[40];

        fd_stamp_text(d->saves[i], when, sizeof when);
        fd_text(d, V->row[i].x, V->row[i].y + 5, when, 0xF2F2F2u);
        fd_button(d, &V->put[i], "RESTORE");
    }
    fd_text(d, V->panel.x + 16, V->back_b.y + 8,
            "The save being replaced is kept first.", 0x929AA2u);
    fd_button(d, &V->back_b, "BACK");
    SDL_RenderPresent(d->ren);
}

static void fd_draw(struct frontdoor *d, int sw, int sh, const char *who,
                    const struct fd_rects *R)
{
    char masked[FD_PASS_MAX], line[MMO_TOKEN_NAME + 120];
    size_t i;

    fd_draw_bg(d, sw, sh);

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

    /* LOGIN is LOGIN. It used to turn into CHOOSE PLATINUM... */
    fd_button(d, &R->login_b, "LOGIN");
    if (d->warn[0] != '\0' && !d->rom_ok)
        fd_text(d, R->panel.x + 20, R->login_b.y + R->login_b.h + 10,
                d->warn, 0xF0B096u);

    /* The offer, in the desktop's words: the offline save is newer than the
     * copy the server was last given, and the box is the player's answer. */
    if (d->offered) {
        char line[120];

        view_ui_fill(d->ren, R->take_b.x, R->take_b.y, R->take_b.w,
                     R->take_b.h, 0x16, 0x1a, 0x20, 255);
        view_ui_border(d->ren, R->take_b.x, R->take_b.y, R->take_b.w,
                       R->take_b.h, 0x6a, 0x88, 0x9b);
        if (d->take)
            view_ui_fill(d->ren, R->take_b.x + 4, R->take_b.y + 4,
                         R->take_b.w - 8, R->take_b.h - 8, 0x81, 0xdd, 0xf1,
                         255);
        if (d->offer_server < 0)
            snprintf(line, sizeof line,
                     "Take my offline save online (%ld:%02ld played)",
                     d->offer_save / 3600, (d->offer_save / 60) % 60);
        else
            snprintf(line, sizeof line,
                     "Take my offline save online (%ld:%02ld played,"
                     " %ld:%02ld newer)",
                     d->offer_save / 3600, (d->offer_save / 60) % 60,
                     (d->offer_save - d->offer_server) / 3600,
                     ((d->offer_save - d->offer_server) / 60) % 60);
        fd_text(d, R->take_b.x + R->take_b.w + 8, R->take_b.y + 1, line,
                0xF2F2F2u);
    }
    fd_button(d, &R->off_b, "PLAY OFFLINE");
    if (d->nsaves > 0)
        fd_button(d, &R->restore_b, "RESTORE SAVE...");

    /* And its footer, in the desktop's colours: the cartridges' one line,
     * because a first run cannot start without them and a player should
     * never have to guess which is missing. Green when the game could start.
     * Above it, what the last run left behind or what this press is doing. */
    if (d->note[0] != '\0') {
        snprintf(line, sizeof line, "%.100s", d->note);
        fd_text(d, 12, sh - (d->rom_ok ? 48 : 68), line, 0x81DDF1u);
    }
    /* Not one cartridge but three, said outright while any is missing: a
     * footer that only names the next slot reads as if Platinum alone were
     * the whole price, and a player finds out otherwise one picker later. */
    if (!d->rom_ok)
        fd_text(d, 12, sh - 48,
                "Play needs your own backups of all three cartridges:"
                " Platinum, Heart Gold and Black.",
                0xF2F2F2u);
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
    fd_button(d, &R->rom_b, "CARTRIDGES");
    fd_button(d, &R->exit_b, "EXIT");
    /* Check for updates, and then whatever the check, the download or the
     * install is doing: the updater writes the label. A build with no
     * channel compiled in (every working tree) has no button at all. */
    if (mmo_update_notice_available()) {
        char label[48];

        mmo_update_notice_label(label, sizeof label);
        fd_button(d, &R->update_b, label);
    }

    /* Last, so it sits over the door rather than under it, and only here:
     * the settings and picker faces present themselves and are not handling
     * its taps. It draws nothing until there is something to answer, and on
     * most runs it never draws at all. */
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

/* The file a cartridge slot resolves to for a reader that wants a path: a
 * picked document is cut into the app's own directory, stamped so an
 * unchanged source is copied once; a path is itself, or the file beside
 * Platinum the desktop would have found. 0 with the path, or -1. */
static int fd_slot_cache(const struct fd_settings *s, int slot, char *out,
                         size_t cap)
{
    const char *v = fd_slot_get(s, slot);
    char dir[512];

    if (v[0] == '\0') {
        if (slot != MMO_LAUNCH_CART_PLATINUM
            && fd_platinum_dir(s, dir, sizeof dir) == 0
            && mmo_launch_cart_scan(dir, slot, out, cap))
            return 0;
        return -1;
    }
    if (strncmp(v, "content://", 10) == 0) {
        int fd = mmo_android_rom_uri_fd(v), rc;

        if (fd < 0)
            return -1;
        rc = fd_rom_cache(fd, v, slot, out, cap);
        close(fd);
        return rc;
    }
    if (slot == MMO_LAUNCH_CART_PLATINUM)
        return fd_rom_file(v, out, cap);
    snprintf(out, cap, "%s", v);
    return access(out, R_OK) == 0 ? 0 : -1;
}

/* A line of progress, drawn at once: the copies and the compose below run
 * on this thread for seconds, and a face that says what it is doing is not
 * a face that hung. */
static void fd_progress(void *ud, const char *line)
{
    struct frontdoor *d = ud;

    snprintf(d->note, sizeof d->note, "%s", line);
    if (d->R != NULL)
        fd_draw(d, d->sw, d->sh, d->who, d->R);
}

/* The press. */
static int fd_go(struct frontdoor *d)
{
    char paths[3][512], err[192], probe[600], root[512];
    int cached = 0;
    FILE *f;

    d->note[0] = '\0';
    fd_mods_root(root, sizeof root);
    snprintf(probe, sizeof probe, "%s/sound_%s_%s/mod.toml", root,
             fd_track_pkg[d->set.soundtrack],
             fd_track_pkg[d->set.soundtrack]);
    f = d->set.soundtrack != 0 ? fopen(probe, "rb") : NULL;
    if (f != NULL)
        fclose(f);
    if (d->set.soundtrack != 0 && f == NULL) {
        int slot;

        for (slot = 0; slot < 3; slot++) {
            char line[160];

            snprintf(line, sizeof line, "Copying your %s cartridge into the"
                     " app...", mmo_launch_cart_name(slot));
            fd_progress(d, line);
            if (fd_slot_cache(&d->set, slot, paths[slot],
                              sizeof paths[slot]) != 0) {
                snprintf(d->note, sizeof d->note, "The %s cartridge could not"
                         " be copied, so the %s soundtrack cannot be composed",
                         mmo_launch_cart_name(slot),
                         fd_tracks[d->set.soundtrack]);
                return -1;
            }
        }
        fd_progress(d, "Composing the soundtrack from your cartridges...");
        if (fdl_compose(d->set.soundtrack, paths[0], paths[1], paths[2],
                        fd_progress, d, err, sizeof err) != 0) {
            snprintf(d->note, sizeof d->note, "%s", err);
            return -1;
        }
        d->note[0] = '\0';
        cached = 1;
    }

    /* And the POKEMON that walks behind the player, on its own gate. */
    /* Asked every press, not only when the folder is empty. */
    {
        char hg[512];
        const char *from = NULL;

        if (cached) {
            from = paths[MMO_LAUNCH_CART_HEARTGOLD];
        } else if (fd_slot_cache(&d->set, MMO_LAUNCH_CART_HEARTGOLD, hg,
                                 sizeof hg) == 0) {
            from = hg;
        }
        if (from == NULL) {
            fprintf(stderr, "frontdoor: no Heart Gold cartridge, so nothing"
                    " walks behind the player; the game starts without one\n");
        } else if (fdl_compose_followers(from, d->set.world, fd_progress, d,
                                         err, sizeof err) < 1) {
            fprintf(stderr, "frontdoor: the follower package was not filled"
                    " (%s); the game starts without one\n", err);
        }
        d->note[0] = '\0';
    }

    /*
     * And the POKEMON the other two CARTRIDGES add, on a gate of its own for the same reason
     * the follower has one: it is filled whatever the music is set to, and the door has
     * already made the player choose all three cartridges, so both images this needs are in
     * hand.
     */
    {
        char pt[512], bw[512];
        const char *from_pt = NULL, *from_bw = NULL;

        if (cached) {
            from_pt = paths[MMO_LAUNCH_CART_PLATINUM];
            from_bw = paths[MMO_LAUNCH_CART_BLACK];
        } else {
            if (fd_slot_cache(&d->set, MMO_LAUNCH_CART_PLATINUM, pt,
                              sizeof pt) == 0)
                from_pt = pt;
            if (fd_slot_cache(&d->set, MMO_LAUNCH_CART_BLACK, bw,
                              sizeof bw) == 0)
                from_bw = bw;
        }
        if (from_pt == NULL || from_bw == NULL) {
            fprintf(stderr, "frontdoor: no %s cartridge, so the Pokemon it adds"
                    " are not filled; the game starts on Platinum's own\n",
                    from_bw == NULL ? "Black or White" : "Platinum");
        } else if (fdl_compose_species(from_pt, from_bw, d->set.world,
                                       fd_progress, d, err, sizeof err) < 1) {
            fprintf(stderr, "frontdoor: the imported species were not filled"
                    " (%s); the game starts on Platinum's own\n", err);
        }
        d->note[0] = '\0';
    }

    if (d->offline)
        return fdl_prepare_offline(d->note, sizeof d->note);
    return fdl_prepare_online(d->take, d->note, sizeof d->note);
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
    /* The plan library's install root is the app's own directory, and the
     * last run's leftovers are dealt with before the face is up: the record
     * an offline session left open, the image a session wrote on its way
     * out, the report the server took. Each is one sentence on the face. */
    fdl_root(getenv("OPENMMO_EXTERNAL_DIR"));
    /* And the packages folder, before anything asks for it: made at the door
     * rather than at the first Play so a device somebody wants to put a
     * package on has somewhere to put it from the moment the app is installed
     * (fd_mods_root). */
    {
        char root[512];

        fd_mods_root(root, sizeof root);
    }
    fdl_housekeep(d.note, sizeof d.note);
    d.nsaves = fdl_saves(d.saves, MMO_LAUNCH_SAVE_KEEP);
    d.offered = fdl_offer(&d.offer_save, &d.offer_server);
    win = SDL_CreateWindow("openmmo", 0, 0, 0, 0, 0);
    d.ren = SDL_CreateRenderer(win, -1, 0);
    if (d.ren == NULL)
        return 0;               /* no screen to ask on: token or env decide */
    /*
     * Read again, now that the renderer has named the GL driver: a software rasteriser caps
     * the device's hd3d default, and the first read above ran before there was a context to
     * ask.
     */
    {
        int scale = d.set.ui_scale;

        fd_settings_load(&d.set);
        d.set.ui_scale = scale;
    }
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

    /*
     * No auto-opened browser: the desktop never does that either. With no cartridge chosen the
     * big button reads CHOOSE PLATINUM...
     */

    /*
     * Ask the channel whether this build is behind, on a thread of its own so a slow answer
     * costs the door nothing.
     */
    mmo_update_notice_ask();

    while (!submitted && !want_exit) {
        SDL_Event ev;
        struct fd_rects R;
        struct fd_srects S;
        struct fd_crects C;
        struct fd_vrects V;
        const char *saved_who = NULL;
        int saved;

        SDL_GetRendererOutputSize(d.ren, &sw, &sh);
        /* The installer's verdict arrives as a file; this is the read. */
        mmo_update_notice_tick();
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
                    fd_slot_set(&d.set, d.rom_slot, uri);
                    fd_settings_save(&d.set);
                }
            }
        }
        fd_slots_status(&d);
        /* The offer only changes when a game ends, so once a second is
         * plenty and every frame would be a file read per frame. */
        if (++d.offer_age >= 60) {
            d.offer_age = 0;
            d.offered = fdl_offer(&d.offer_save, &d.offer_server);
        }
        saved = mmo_token_who(who, sizeof who) && d.user[0] != '\0'
                && mmo_token_name_is(who, d.user);
        if (saved)
            saved_who = who;
        fd_layout(&d, sw, sh, saved, &R);
        fd_slayout(sw, sh, &S);
        fd_clayout(sw, sh, &C);
        fd_vlayout(sw, sh, d.nsaves, &V);

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
                                char path[FD_ROM_PATH];

                                snprintf(path, sizeof path, "%s/%s",
                                         d.rom.dir, d.rom.e[at].name);
                                fd_slot_set(&d.set, d.rom_slot, path);
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

        if (d.on_carts) {
            while (SDL_PollEvent(&ev)) {
                int i;

                if (ev.type == SDL_QUIT) {
                    want_exit = 1;
                } else if (ev.type == SDL_KEYDOWN
                           && ev.key.keysym.sym == SDLK_ESCAPE) {
                    d.on_carts = 0;
                } else if (ev.type == SDL_MOUSEBUTTONDOWN) {
                    if (fd_hit(&C.done_b, ev.button.x, ev.button.y))
                        d.on_carts = 0;
                    for (i = 0; i < 3; i++)
                        if (fd_hit(&C.choose[i], ev.button.x, ev.button.y))
                            fd_rom_ask(&d, i);
                }
            }
            /* The system picker is another activity over this one, so the
             * face keeps drawing under it; the drawn browser, when that is
             * the route, owns the glass from the next frame. */
            if (!d.on_rom)
                fd_draw_carts(&d, sw, sh, &C);
            continue;
        }

        if (d.on_restore) {
            while (SDL_PollEvent(&ev)) {
                int i;

                if (ev.type == SDL_QUIT) {
                    want_exit = 1;
                } else if (ev.type == SDL_KEYDOWN
                           && ev.key.keysym.sym == SDLK_ESCAPE) {
                    d.on_restore = 0;
                } else if (ev.type == SDL_MOUSEBUTTONDOWN) {
                    if (fd_hit(&V.back_b, ev.button.x, ev.button.y))
                        d.on_restore = 0;
                    for (i = 0; i < d.nsaves; i++) {
                        if (!fd_hit(&V.put[i], ev.button.x, ev.button.y))
                            continue;
                        d.note[0] = '\0';
                        fdl_restore(d.saves[i], d.note, sizeof d.note);
                        d.nsaves = fdl_saves(d.saves, MMO_LAUNCH_SAVE_KEEP);
                        d.offer_age = 60;
                        d.on_restore = 0;
                        break;
                    }
                }
            }
            fd_draw_saves(&d, sw, sh, &V);
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
                    /* No CARTRIDGES, no LOGIN, the desktop's rule: the
                     * press that cannot start the game says why, under the
                     * button, and opens nothing. */
                    if (d.rom_ok) {
                        d.offline = 0;
                        submitted = 1;
                    } else {
                        fd_focus(&d, FOCUS_NONE);
                        snprintf(d.warn, sizeof d.warn, "Choose your three"
                                 " cartridges first (CARTRIDGES button).");
                    }
                } else if (fd_hit(&R.off_b, ev.button.x, ev.button.y)) {
                    /* The second row. Every cartridge still, and no account
                     * at all: the game is the port with its own save. */
                    fd_focus(&d, FOCUS_NONE);
                    if (d.rom_ok) {
                        d.offline = 1;
                        submitted = 1;
                    } else {
                        snprintf(d.warn, sizeof d.warn, "Choose your three"
                                 " cartridges first (CARTRIDGES button).");
                    }
                } else if (d.nsaves > 0
                           && fd_hit(&R.restore_b, ev.button.x,
                                     ev.button.y)) {
                    fd_focus(&d, FOCUS_NONE);
                    d.on_restore = 1;
                } else if (d.offered
                           && fd_hit(&R.take_b, ev.button.x, ev.button.y)) {
                    d.take = !d.take;
                } else if (saved
                           && fd_hit(&R.out_b, ev.button.x, ev.button.y)) {
                    mmo_token_clear();
                } else if (fd_hit(&R.settings_b, ev.button.x, ev.button.y)) {
                    fd_focus(&d, FOCUS_NONE);
                    d.on_settings = 1;
                } else if (fd_hit(&R.rom_b, ev.button.x, ev.button.y)) {
                    fd_focus(&d, FOCUS_NONE);
                    d.on_carts = 1;
                } else if (fd_hit(&R.exit_b, ev.button.x, ev.button.y)) {
                    want_exit = 1;
                } else if (mmo_update_notice_available()
                           && fd_hit(&R.update_b, ev.button.x,
                                     ev.button.y)) {
                    fd_focus(&d, FOCUS_NONE);
                    mmo_update_notice_press();
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
                            d.offline = 0;
                            submitted = 1;
                        } else {
                            fd_focus(&d, FOCUS_NONE);
                            snprintf(d.warn, sizeof d.warn, "Choose your three"
                                 " cartridges first (CARTRIDGES button).");
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

        /* An empty name cannot be a session; the press just keeps the form.
         * Offline needs none. */
        if (submitted && !d.offline && d.user[0] == '\0') {
            submitted = 0;
            fd_focus(&d, FOCUS_USER);
        }

        fd_draw(&d, sw, sh, saved_who, &R);

        /* The press itself, with the face still up to say what it is doing.
         * A press that cannot go leaves the note saying why and the face
         * standing, which is the desktop's answer too. */
        if (submitted) {
            fd_focus(&d, FOCUS_NONE);
            d.sw = sw;
            d.sh = sh;
            d.who = saved_who;
            d.R = &R;
            if (fd_go(&d) != 0)
                submitted = 0;
            d.R = NULL;
        }
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
    if (d.offline)
        return 2;
    snprintf(user, ucap, "%s", d.user);
    snprintf(pass, pcap, "%s", d.pass);
    return 0;
}
