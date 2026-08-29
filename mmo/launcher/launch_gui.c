/* The front door as the official login screen. */

#include "launch_gui.h"

#include "launch_plan.h"
#include "token.h"
#include "soundcompose.h"

#include <dirent.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "platform.h"

#include "raylib.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#define WIN_W 1024
#define WIN_H 640

#define BROWSE_MAX 256
#define BROWSE_NAME 256

enum {
    ED_USER = 0,
    ED_PASS,
    ED_ROM,
    ED_ROM_HG,
    ED_ROM_BW,
    ED_N
};

enum {
    VIEW_LOGIN = 0,
    VIEW_SETTINGS,
    VIEW_GRAPHICS,
    VIEW_CONTROLS,
    VIEW_BROWSE
};

static const char *const layout_opts = "smart;stacked;wide;fill";
static const char *const filter_opts = "nearest;linear;scale2x";
static const char *const fit_opts    = "aspect;integer;stretch";
static const char *const button_opts = "normal;start-is-x;l-is-a";
/* The launch_plan soundtrack enum, in its order, spelled the way the
 * cartridges are named on the box (mmo/CARTRIDGES' slot names). */
static const char *const soundtrack_opts = "Platinum;Heart Gold;Black & White";
/* The two 3D resolutions, in step order from MMO_LAUNCH_HD3D_MIN up. The
 * multiplier behind each one is the plan's business, not the panel's. */
static const char *const hd3d_opts   = "SD;HD";
static const char *const pace_opts   = "console rate (60 fps);unlimited";
static const char *const scale_opts  = "auto;1x;2x;3x;4x;5x;6x;7x;8x";
static const char *const viewport_names[] = {
    "auto", "native", "16:9", "16:10", "4:3"
};
#define VIEWPORT_NAMED 5

struct place {
    char label[32];
    char dir[MMO_LAUNCH_PATH];
};

struct browse {
    int  on;
    int  back;                  /* the view to return to when it closes */
    char dir[MMO_LAUNCH_PATH];
    char names[BROWSE_MAX][BROWSE_NAME];
    char *ptr[BROWSE_MAX];
    int  kind[BROWSE_MAX];      /* 0 up, 1 dir, 2 file */
    int  n;
    int  scroll;
    int  active;
    int  focus;
    /* A second click on the row already under the cursor opens it, which is
     * what every other file window on the machine does. */
    double click_at;
    int    click_row;
    struct place places[5];
    int    nplaces;
    /* Whether this folder is one the game could start from, in words. */
    char note[192];
    int  note_ok;
};

static struct browse br;

/* A bitmap face only looks like itself at the size it was baked at. */
#define FONT_CACHE_MAX 8

static char  font_face[2][512];
static Font  font_bake[2][FONT_CACHE_MAX];
static int   font_bake_px[2][FONT_CACHE_MAX];
static int   font_bake_n[2];
static float font_scale = 1.0f;
static int   have_font;

/*
 * Two faces at one nominal size are not one size on the page. DejaVu, which this window was
 * laid out against, puts a capital at 0.64 of the em; Segoe UI, which is what Windows hands
 * us, puts it at 0.53.
 */
#define FACE_CAP 0.64f

static void face_metrics(void)
{
    Font probe;
    float cap;

    font_scale = 1.0f;
    if (!have_font)
        return;
    probe = LoadFontEx(font_face[0], 64, NULL, 0);
    if (probe.texture.id == 0)
        return;
    cap = probe.recs[GetGlyphIndex(probe, 'H')].height / 64.0f;
    UnloadFont(probe);
    if (cap > 0.05f && cap < FACE_CAP) {
        font_scale = FACE_CAP / cap;
        /* A face this far from the layout's is a face we have measured
         * wrong; growing the type past a quarter would break rows before it
         * fixed anything. */
        if (font_scale > 1.25f)
            font_scale = 1.25f;
    }
}

/* The nominal size a caller asks for, in the pixels this face needs to draw
 * it that big. */
static int ui_px(float size)
{
    return (int)(size * font_scale + 0.5f);
}

static Font ui_font(int bold, int px)
{
    Font f;
    int i;

    if (!have_font)
        return GetFontDefault();
    if (bold != 0 && font_face[1][0] == '\0')
        bold = 0;
    for (i = 0; i < font_bake_n[bold]; i++)
        if (font_bake_px[bold][i] == px)
            return font_bake[bold][i];
    if (font_bake_n[bold] >= FONT_CACHE_MAX)
        return font_bake[bold][0];
    f = LoadFontEx(font_face[bold], px, NULL, 0);
    if (f.texture.id == 0)
        return font_bake_n[bold] > 0 ? font_bake[bold][0] : GetFontDefault();
    /* Drawn one atlas texel to one pixel, so point sampling is the sharp
     * choice here; bilinear would only put the blur back. */
    SetTextureFilter(f.texture, TEXTURE_FILTER_POINT);
    i = font_bake_n[bold]++;
    font_bake_px[bold][i] = px;
    font_bake[bold][i] = f;
    return f;
}

/*
 * Every line of text in this window goes through these three, so the atlas that was baked and
 * the size it is drawn at can never come apart, which is the bug this whole arrangement
 * exists to close.
 */
static Vector2 text_dim(int bold, const char *s, float size)
{
    int px = ui_px(size);

    return MeasureTextEx(ui_font(bold, px), s, (float)px, 1.0f);
}

/* Glyphs land on whole pixels or they sample between two of them, which is
 * the other half of a soft line of small type. */
static void text_at(int bold, const char *s, float x, float y, float size,
                    Color c)
{
    int px = ui_px(size);

    DrawTextEx(ui_font(bold, px), s,
               (Vector2){ floorf(x + 0.5f), floorf(y + 0.5f) }, (float)px,
               1.0f, c);
}

/* Over the picture a colour is not enough on its own: a bright sky takes a
 * pale grey line with it. One dark pixel behind the glyphs is what keeps a
 * credit or an error readable wherever the art happens to be light. */
static void text_over_art(int bold, const char *s, float x, float y,
                          float size, Color c)
{
    Color sh = { 0, 0, 0, (unsigned char)(c.a > 200 ? 200 : c.a) };

    text_at(bold, s, x + 1.0f, y + 1.0f, size, sh);
    text_at(bold, s, x, y, size, c);
}

/* The top of the tree, where ".." has nowhere left to go: "/" here, and a
 * drive letter's own root ("C:\\") on Windows. */
static int at_root(const char *dir)
{
    if (dir[0] == '/' && dir[1] == '\0')
        return 1;
    return dir[0] != '\0' && dir[1] == ':' && dir[2] != '\0' && dir[3] == '\0';
}

static void parent_dir(char *dir, size_t cap)
{
    char *slash;

    (void)cap;
    if (at_root(dir))
        return;
    slash = (char *)mmo_plat_last_sep(dir);
    if (slash == NULL)
        return;
    if (slash == dir || (slash == dir + 2 && dir[1] == ':'))
        slash[1] = '\0';
    else
        *slash = '\0';
}

static int join_path(char *out, size_t cap, const char *dir, const char *name)
{
    if (at_root(dir))
        return snprintf(out, cap, "%s%s", dir, name);
    return snprintf(out, cap, "%s%s%s", dir, mmo_plat_sep(), name);
}

static int is_nds(const char *name)
{
    size_t L = strlen(name);

    return L >= 4 && strcasecmp(name + L - 4, ".nds") == 0;
}

/* Folders first and then names, both without regard to case: a list in
 * readdir order is a list nobody can find anything in. */
static int name_before(const char *a, int ka, const char *b, int kb)
{
    if (ka != kb)
        return ka < kb;
    return strcasecmp(a, b) < 0;
}

/*
 * The `rom/` the game program resolves on its own, as a folder. This is the one place a
 * cartridge has to be for a launch to work without naming it, so it is where Browse starts and
 * the first place it offers.
 */
static int game_rom_dir(char *out, size_t cap)
{
    char exe[MMO_LAUNCH_PATH];
    char port[MMO_LAUNCH_PATH], view[MMO_LAUNCH_PATH];
    char rom[MMO_LAUNCH_PATH];
    struct stat st;

    if (mmo_plat_exe_path(exe, sizeof exe) != 0)
        snprintf(exe, sizeof exe, "openmmo-launch");
    mmo_launch_paths(exe, port, sizeof port, view, sizeof view,
                     rom, sizeof rom);
    if (rom[0] == '\0')
        return -1;
    snprintf(out, cap, "%s", rom);
    parent_dir(out, cap);
    return out[0] != '\0' && stat(out, &st) == 0 && S_ISDIR(st.st_mode)
        ? 0 : -1;
}

/* What this folder means to the game, said in the words the check itself
 * uses so the browse window and a refused launch never disagree. */
static void browse_note(void)
{
    char romfile[MMO_LAUNCH_PATH];

    if (mmo_launch_rom_file(br.dir, romfile, sizeof romfile) == 0) {
        const char *base = mmo_plat_last_sep(romfile);

        snprintf(br.note, sizeof br.note, "%s is here, Use this folder",
                 base != NULL ? base + 1 : romfile);
        br.note_ok = 1;
        return;
    }
    snprintf(br.note, sizeof br.note,
             "no %s here, open a folder, or pick a .nds below",
             MMO_LAUNCH_ROM_NAME);
    br.note_ok = 0;
}

static void browse_load(void)
{
    DIR *d;
    struct dirent *de;
    struct stat st;
    char path[MMO_LAUNCH_PATH];
    int n = 0, i;

    if (!at_root(br.dir)) {
        snprintf(br.names[n], BROWSE_NAME, "[ .. ]");
        br.kind[n++] = 0;
    }
    d = opendir(br.dir);
    if (d != NULL) {
        while ((de = readdir(d)) != NULL && n < BROWSE_MAX) {
            if (de->d_name[0] == '.')
                continue;
            if (join_path(path, sizeof path, br.dir, de->d_name) >= (int)sizeof path)
                continue;
            if (stat(path, &st) != 0)
                continue;
            if (S_ISDIR(st.st_mode)) {
                snprintf(br.names[n], BROWSE_NAME, "%s/", de->d_name);
                br.kind[n++] = 1;
                continue;
            }
            if (!S_ISREG(st.st_mode) || !is_nds(de->d_name))
                continue;
            snprintf(br.names[n], BROWSE_NAME, "%s", de->d_name);
            br.kind[n++] = 2;
        }
        closedir(d);
    }
    /* An insertion sort over at most BROWSE_MAX names, which is a folder
     * listing and not a data structure worth more than this. */
    for (i = 1; i < n; i++) {
        char name[BROWSE_NAME];
        int kind = br.kind[i];
        int j = i - 1;

        snprintf(name, sizeof name, "%s", br.names[i]);
        while (j >= 0 && br.kind[j] != 0 &&
               name_before(name, kind, br.names[j], br.kind[j])) {
            snprintf(br.names[j + 1], BROWSE_NAME, "%s", br.names[j]);
            br.kind[j + 1] = br.kind[j];
            j--;
        }
        snprintf(br.names[j + 1], BROWSE_NAME, "%s", name);
        br.kind[j + 1] = kind;
    }
    for (i = 0; i < n; i++)
        br.ptr[i] = br.names[i];
    br.n = n;
    br.active = -1;
    br.focus = -1;
    br.scroll = 0;
    br.click_row = -1;
    /* If the image this build wants is in here, start on it: folders sort
     * first, so the one file a player came for would otherwise be off the
     * bottom of a long listing. */
    for (i = 0; i < n; i++)
        if (br.kind[i] == 2 && strcasecmp(br.names[i], MMO_LAUNCH_ROM_NAME) == 0)
            br.active = i;
    browse_note();
}

static void browse_go(const char *dir)
{
    snprintf(br.dir, sizeof br.dir, "%s", dir);
    browse_load();
}

/* The two or three folders a player's cartridge is actually in, as buttons:
 * their home, the roms folder this install drops images into, and the engine
 * tree's own build output. Typing any of them is what this replaces. */
static void browse_places(const mmo_launch_settings *s)
{
    struct stat st;
    char buf[MMO_LAUNCH_PATH];
    const char *eng = getenv("ENGINE_DIR");
    int n = 0;

    if (mmo_plat_home(buf, sizeof buf) == 0 && stat(buf, &st) == 0) {
        snprintf(br.places[n].label, sizeof br.places[n].label, "Home");
        snprintf(br.places[n].dir, sizeof br.places[n].dir, "%s", buf);
        n++;
    }
    if (game_rom_dir(buf, sizeof buf) == 0) {
        snprintf(br.places[n].label, sizeof br.places[n].label, "Game rom");
        snprintf(br.places[n].dir, sizeof br.places[n].dir, "%s", buf);
        n++;
    }
    {
        char exe[MMO_LAUNCH_PATH];

        if (mmo_plat_exe_path(exe, sizeof exe) != 0)
            snprintf(exe, sizeof exe, "openmmo-launch");
        mmo_launch_roms_dir(exe, buf, sizeof buf);
    }
    if (buf[0] != '\0' && stat(buf, &st) == 0 && S_ISDIR(st.st_mode)) {
        snprintf(br.places[n].label, sizeof br.places[n].label, "Roms folder");
        snprintf(br.places[n].dir, sizeof br.places[n].dir, "%s", buf);
        n++;
    }
    if (eng != NULL && eng[0] != '\0') {
        snprintf(buf, sizeof buf, "%s/build/rom", eng);
        if (stat(buf, &st) == 0 && S_ISDIR(st.st_mode)) {
            snprintf(br.places[n].label, sizeof br.places[n].label, "Engine build");
            snprintf(br.places[n].dir, sizeof br.places[n].dir, "%s", buf);
            n++;
        }
    }
    if (s->rom[0] != '\0' && n < (int)(sizeof br.places / sizeof br.places[0])) {
        int i, dup = 0;

        snprintf(buf, sizeof buf, "%s", s->rom);
        if (stat(buf, &st) != 0 || !S_ISDIR(st.st_mode))
            parent_dir(buf, sizeof buf);
        /* The chosen ROM usually is the game's own rom/, and two buttons on
         * the same folder is a row that reads as a mistake. */
        for (i = 0; i < n; i++)
            if (strcmp(br.places[i].dir, buf) == 0)
                dup = 1;
        if (!dup && buf[0] != '\0' && stat(buf, &st) == 0) {
            snprintf(br.places[n].label, sizeof br.places[n].label, "Current");
            snprintf(br.places[n].dir, sizeof br.places[n].dir, "%s", buf);
            n++;
        }
    }
    br.nplaces = n;
}

static void browse_open(const mmo_launch_settings *s, int back)
{
    struct stat st;
    char home[MMO_LAUNCH_PATH];
    const char *eng = getenv("ENGINE_DIR");

    br.dir[0] = '\0';
    if (s->rom[0] != '\0') {
        if (stat(s->rom, &st) == 0 && S_ISDIR(st.st_mode))
            snprintf(br.dir, sizeof br.dir, "%s", s->rom);
        else {
            snprintf(br.dir, sizeof br.dir, "%s", s->rom);
            parent_dir(br.dir, sizeof br.dir);
        }
    }
    if (br.dir[0] == '\0' && game_rom_dir(br.dir, sizeof br.dir) != 0)
        br.dir[0] = '\0';
    if (br.dir[0] == '\0' && eng != NULL && eng[0] != '\0') {
        snprintf(br.dir, sizeof br.dir, "%s/build/rom", eng);
        if (stat(br.dir, &st) != 0)
            br.dir[0] = '\0';
    }
    if (br.dir[0] == '\0' && mmo_plat_home(home, sizeof home) == 0)
        snprintf(br.dir, sizeof br.dir, "%s", home);
    if (br.dir[0] == '\0')
        snprintf(br.dir, sizeof br.dir, "%s", mmo_plat_sep());
    br.on = 1;
    br.back = back;
    browse_places(s);
    browse_load();
}

/* Enter a folder, or take a file as the ROM. */
static void browse_pick(mmo_launch_settings *s, int idx)
{
    char path[MMO_LAUNCH_PATH];

    if (idx < 0 || idx >= br.n)
        return;
    if (br.kind[idx] == 0) {
        parent_dir(br.dir, sizeof br.dir);
        browse_load();
        return;
    }
    if (br.kind[idx] == 1) {
        char name[BROWSE_NAME];

        snprintf(name, sizeof name, "%s", br.names[idx]);
        name[strlen(name) - 1] = '\0'; /* drop trailing / */
        if (join_path(path, sizeof path, br.dir, name) < (int)sizeof path)
            browse_go(path);
        return;
    }
    if (join_path(path, sizeof path, br.dir, br.names[idx]) < (int)sizeof path) {
        snprintf(s->rom, sizeof s->rom, "%s", path);
        br.on = 0;
    }
}

/*
 * One line about the ROM: what it is when it works, and what is wrong when it does not. The
 * words are mmo_launch_check's own, so this line, the browse window and a refused launch never
 * say three different things.
 */
static int rom_status(const mmo_launch_settings *s, char *out, size_t cap)
{
    char romfile[MMO_LAUNCH_PATH];
    const char *base;
    struct stat st;

    if (s->rom[0] == '\0') {
        snprintf(out, cap, "No ROM chosen yet");
        return 0;
    }
    if (mmo_launch_rom_file(s->rom, romfile, sizeof romfile) != 0) {
        if (stat(s->rom, &st) == 0 && S_ISDIR(st.st_mode))
            snprintf(out, cap, "No %s in that folder", MMO_LAUNCH_ROM_NAME);
        else
            snprintf(out, cap, "That ROM cannot be read");
        return 0;
    }
    base = mmo_plat_last_sep(romfile);
    snprintf(out, cap, "ROM ready: %s", base != NULL ? base + 1 : romfile);
    return 1;
}

/* The host's own file window first, this one only when the machine has none. */
static int rom_browse(mmo_launch_settings *s, int back)
{
    char start[MMO_LAUNCH_PATH];
    char chosen[MMO_LAUNCH_PATH];
    struct stat st;
    int rc;

    start[0] = '\0';
    if (s->rom[0] != '\0') {
        snprintf(start, sizeof start, "%s", s->rom);
        if (stat(start, &st) != 0 || !S_ISDIR(st.st_mode))
            parent_dir(start, sizeof start);
    }
    rc = mmo_plat_pick_file("Choose a Nintendo DS ROM",
                            start[0] != '\0' ? start : NULL,
                            "Nintendo DS ROM", "*.nds",
                            chosen, sizeof chosen);
    if (rc == 0) {
        snprintf(s->rom, sizeof s->rom, "%s", chosen);
        return back;
    }
    if (rc == 1)
        return back;
    browse_open(s, back);
    return VIEW_BROWSE;
}

/* The two other cartridge slots' Browse. No list-window fallback here:
 * that window is built around the Platinum row, and a box with no file
 * dialog still takes a typed path. */
static void slot_browse(mmo_launch_settings *s, int slot)
{
    char start[MMO_LAUNCH_PATH];
    char chosen[MMO_LAUNCH_PATH];
    char *dst = slot == 1 ? s->rom_hg : s->rom_bw;
    struct stat st;
    int rc;

    start[0] = '\0';
    if (dst[0] != '\0')
        snprintf(start, sizeof start, "%s", dst);
    else if (s->rom[0] != '\0')
        snprintf(start, sizeof start, "%s", s->rom);
    if (start[0] != '\0' && (stat(start, &st) != 0 || !S_ISDIR(st.st_mode)))
        parent_dir(start, sizeof start);
    rc = mmo_plat_pick_file(slot == 1 ? "Choose your Heart Gold cartridge"
                                      : "Choose your Black or White cartridge",
                            start[0] != '\0' ? start : NULL,
                            "Nintendo DS ROM", "*.nds",
                            chosen, sizeof chosen);
    if (rc == 0)
        snprintf(dst, sizeof s->rom_hg, "%s", chosen);
}

static int find_res(char *out, size_t cap, const char *name)
{
    char exe[512];
    const char *suffix[] = {
        "/../res/launcher/",
        "/res/launcher/",
        "/../share/openmmo/launcher/",
        NULL
    };
    int i;

    if (mmo_plat_exe_path(exe, sizeof exe) != 0)
        return -1;
    {
        char *slash = (char *)mmo_plat_last_sep(exe);

        if (slash != NULL)
            *slash = '\0';
    }
    for (i = 0; suffix[i] != NULL; i++) {
        snprintf(out, cap, "%s%s%s", exe, suffix[i], name);
        if (FileExists(out))
            return 0;
    }
    return -1;
}

static Texture2D load_res(const char *name)
{
    char path[768];

    if (find_res(path, sizeof path, name) == 0)
        return LoadTexture(path);
    return (Texture2D){ 0 };
}

static void cover_texture(Texture2D tex, int sw, int sh)
{
    float sx, sy, sc, dw, dh, dx, dy;

    if (tex.id == 0 || tex.width <= 0 || tex.height <= 0)
        return;
    sx = (float)sw / (float)tex.width;
    sy = (float)sh / (float)tex.height;
    sc = sx > sy ? sx : sy;
    dw = (float)tex.width * sc;
    dh = (float)tex.height * sc;
    dx = ((float)sw - dw) * 0.5f;
    dy = ((float)sh - dh) * 0.5f;
    DrawTexturePro(tex,
                   (Rectangle){ 0, 0, (float)tex.width, (float)tex.height },
                   (Rectangle){ dx, dy, dw, dh },
                   (Vector2){ 0, 0 }, 0.0f, WHITE);
}

static void style_login(void)
{
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR, 0x1c2228ff);
    GuiSetStyle(DEFAULT, TEXT_SIZE, ui_px(16.0f));
    GuiSetStyle(DEFAULT, TEXT_SPACING, 1);
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, 0xf2f2f2ff);
    GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED, 0xffffffff);
    GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED, 0xffffffff);
    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, 0x2a333cff);
    GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED, 0x3a4652ff);
    GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED, 0x4a5a68ff);
    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, 0x4a5560ff);
    GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, 0x6a889bff);
    GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, 0x81ddf1ff);
    GuiSetStyle(DEFAULT, BORDER_WIDTH, 1);
    GuiSetStyle(BUTTON, BASE_COLOR_NORMAL, 0x3a4450ff);
    GuiSetStyle(BUTTON, BASE_COLOR_FOCUSED, 0x4a5866ff);
    GuiSetStyle(BUTTON, BASE_COLOR_PRESSED, 0x2a333cff);
    GuiSetStyle(BUTTON, TEXT_COLOR_NORMAL, 0xf2f2f2ff);
    GuiSetStyle(BUTTON, BORDER_COLOR_NORMAL, 0x5a6670ff);
    GuiSetStyle(TEXTBOX, BASE_COLOR_NORMAL, 0x161a20ff);
    GuiSetStyle(TEXTBOX, BASE_COLOR_FOCUSED, 0x161a20ff);
    GuiSetStyle(TEXTBOX, BASE_COLOR_PRESSED, 0x161a20ff);
    GuiSetStyle(TEXTBOX, BORDER_COLOR_FOCUSED, 0x6a889bff);
    GuiSetStyle(CHECKBOX, TEXT_COLOR_NORMAL, 0xf2f2f2ff);
    GuiSetStyle(CHECKBOX, BASE_COLOR_NORMAL, 0x161a20ff);
    GuiSetStyle(CHECKBOX, BASE_COLOR_FOCUSED, 0x2a333cff);
    GuiSetStyle(CHECKBOX, BASE_COLOR_PRESSED, 0x6a889bff);
    GuiSetStyle(CHECKBOX, BORDER_COLOR_NORMAL, 0x6a889bff);
    GuiSetStyle(CHECKBOX, BORDER_COLOR_FOCUSED, 0x81ddf1ff);
    /* raygui's own disabled colours are a pale grey meant for a light theme,
     * and a greyed-out button here came out brighter than a live one. */
    GuiSetStyle(DEFAULT, BASE_COLOR_DISABLED, 0x232a31ff);
    GuiSetStyle(DEFAULT, BORDER_COLOR_DISABLED, 0x3a444eff);
    GuiSetStyle(DEFAULT, TEXT_COLOR_DISABLED, 0x77818bff);
    /* A file list reads down its left edge; raygui centres rows by default,
     * which turns a folder listing into a poem. */
    GuiSetStyle(LISTVIEW, TEXT_ALIGNMENT, TEXT_ALIGN_LEFT);
    GuiSetStyle(LISTVIEW, TEXT_PADDING, 10);
    GuiSetStyle(LISTVIEW, LIST_ITEMS_HEIGHT, 26);
    GuiSetStyle(LISTVIEW, LIST_ITEMS_SPACING, 2);
    /* raygui draws its widgets at TEXT_SIZE and nothing else, so its face is
     * the atlas baked at exactly that. */
    if (have_font)
        GuiSetFont(ui_font(0, GuiGetStyle(DEFAULT, TEXT_SIZE)));
}

static void draw_frame(Rectangle r, const char *title)
{
    DrawRectangleRounded(r, 0.03f, 6, (Color){ 28, 32, 38, 235 });
    DrawRectangleRoundedLinesEx(r, 0.03f, 6, 1.5f, (Color){ 74, 85, 96, 255 });
    if (title != NULL && title[0] != '\0') {
        float size = 18.0f;
        Vector2 ts = text_dim(1, title, size);

        text_at(1, title, r.x + (r.width - ts.x) * 0.5f, r.y + 14.0f,
                size, WHITE);
    }
}

/* Password field: official editfield passwordChar is 35 ('#'). Enter submits. */
static int password_box(Rectangle r, char *pass, int cap, bool *edit)
{
    Vector2 mouse = GetMousePosition();
    char shown[128];
    int n, ch;
    Color border;

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        *edit = CheckCollisionPointRec(mouse, r);
    if (*edit) {
        while ((ch = GetCharPressed()) > 0) {
            int len = (int)strlen(pass);

            if (len + 1 < cap && ch >= 32 && ch < 127) {
                pass[len] = (char)ch;
                pass[len + 1] = '\0';
            }
        }
        if (IsKeyPressed(KEY_BACKSPACE)) {
            int len = (int)strlen(pass);

            if (len > 0)
                pass[len - 1] = '\0';
        }
        if (IsKeyPressed(KEY_ENTER))
            return 1;
        if (IsKeyPressed(KEY_TAB))
            *edit = false;
    }

    n = (int)strlen(pass);
    if (n > (int)sizeof shown - 1)
        n = (int)sizeof shown - 1;
    memset(shown, '#', (size_t)n);
    shown[n] = '\0';

    border = *edit ? (Color){ 106, 136, 155, 255 } : (Color){ 74, 85, 96, 255 };
    DrawRectangleRec(r, (Color){ 22, 26, 32, 255 });
    DrawRectangleLinesEx(r, 1.0f, border);
    text_at(0, shown, r.x + 8.0f, r.y + (r.height - 16.0f) * 0.5f,
            16.0f, WHITE);
    if (*edit && (((int)(GetTime() * 2.0)) & 1)) {
        Vector2 tw = text_dim(0, shown, 16.0f);

        DrawRectangle((int)(r.x + 8.0f + tw.x), (int)(r.y + 6.0f),
                      1, (int)(r.height - 12.0f), WHITE);
    }
    return 0;
}

void launch_gui_hide_for_session(void)
{
    if (IsWindowReady())
        SetWindowState(FLAG_WINDOW_HIDDEN);
}

static int do_play(struct launch_gui_host *host, int *playing)
{
    if (host->play == NULL || *playing)
        return 0;
    *playing = 1;
    EndDrawing();
    /* Stay mapped until the game window exists. Hiding first leaves WSLg
     * with nothing to hand keyboard focus to, and the new X11 window
     * never sees a key. */
    host->play(host->ctx);
    ClearWindowState(FLAG_WINDOW_HIDDEN);
    SetWindowFocused();
    *playing = 0;
    return 1;
}

static void label_left(Rectangle r, const char *s)
{
    GuiLabel(r, s);
}

/*
 * A text box that says what it is for while it is empty. The words are the keyboard menu's own
 * (launch_menu.c's value()), so a row means the same thing in both front doors.
 */
static int hint_box(Rectangle r, char *text, int cap, bool *edit,
                    const char *hint)
{
    int toggled = GuiTextBox(r, text, cap, *edit);

    if (toggled)
        *edit = !*edit;
    if (text[0] == '\0' && !*edit) {
        text_at(0, hint, r.x + 8.0f, r.y + (r.height - 14.0f) * 0.5f,
                14.0f, (Color){ 138, 146, 154, 255 });
    }
    return toggled;
}

/* The official login face: logo, login-window, right-rail Settings / Exit. */
/* Whose saved sign-in is on this device, or NULL for none. */
static const char *saved_signin(int forget)
{
    static char who[MMO_TOKEN_NAME];
    static double checked;
    static int present;
    double now = GetTime();

    if (forget) {
        mmo_token_clear();
        present = 0;
        who[0] = '\0';
        checked = now;
        return NULL;
    }
    if (checked == 0.0 || now - checked > 0.25) {
        checked = now;
        present = mmo_token_who(who, sizeof who);
    }
    return present ? who : NULL;
}

static int launch_gui_logingui(struct launch_gui_host *host, Texture2D wordmark,
                               bool *edit, bool *remember, int *playing)
{
    mmo_launch_settings *s = host->set;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    float ww, wh, wx, wy;
    float logo_w, logo_h, logo_x, logo_y;
    Rectangle panel, user_l, pass_l, user_b, pass_b, login_b, rem_b;
    float col_w, gap, pad, top;
    int submitted = 0;
    int browse = 0;
    char rom_why[220];
    int rom_ok = rom_status(s, rom_why, sizeof rom_why);
    /* Only when it is this account's: a token minted for somebody else is not
     * a sign-in this player is about to get, so announcing one would be a
     * promise the server would then refuse. */
    const char *who = saved_signin(0);
    int saved = who != NULL && s->user[0] != '\0' && mmo_token_name_is(who, s->user);

    /* Wordmark: official logo widget is 484x143, centered, lifted above mid. */
    if (wordmark.id != 0) {
        logo_w = (float)wordmark.width;
        logo_h = (float)wordmark.height;
        if (logo_w > (float)sw * 0.48f) {
            float sc = (float)sw * 0.48f / logo_w;

            logo_w *= sc;
            logo_h *= sc;
        }
        logo_x = ((float)sw - logo_w) * 0.5f;
        logo_y = (float)sh * 0.16f;
        DrawTexturePro(wordmark,
                       (Rectangle){ 0, 0, (float)wordmark.width, (float)wordmark.height },
                       (Rectangle){ logo_x, logo_y, logo_w, logo_h },
                       (Vector2){ 0, 0 }, 0.0f, WHITE);
    } else {
        const char *t = "OPENMMO";
        Vector2 ts = text_dim(1, t, 42.0f);

        logo_y = (float)sh * 0.18f;
        logo_h = 56.0f;
        text_over_art(1, t, ((float)sw - ts.x) * 0.5f, logo_y, 42.0f, WHITE);
    }

    /* login-window: min 300x150, dialog padding 60,20,20,20, two columns. */
    ww = 440.0f;
    wh = saved ? 268.0f : 214.0f;
    if (ww > (float)sw - 40.0f)
        ww = (float)sw - 40.0f;
    wx = ((float)sw - ww) * 0.5f;
    wy = logo_y + logo_h + 18.0f;
    if (wy + wh > (float)sh - 56.0f)
        wy = (float)sh - wh - 56.0f;
    if (wy < 8.0f)
        wy = 8.0f;
    panel = (Rectangle){ wx, wy, ww, wh };
    draw_frame(panel, "LOGIN");

    pad = 20.0f;
    top = 52.0f;
    gap = 16.0f;
    col_w = (ww - pad * 2.0f - gap) * 0.5f;
    user_l = (Rectangle){ wx + pad, wy + top, col_w, 20.0f };
    pass_l = (Rectangle){ wx + pad + col_w + gap, wy + top, col_w, 20.0f };
    user_b = (Rectangle){ wx + pad, wy + top + 22.0f, col_w, 28.0f };
    pass_b = (Rectangle){ wx + pad + col_w + gap, wy + top + 22.0f, col_w, 28.0f };
    rem_b  = (Rectangle){ wx + pad, wy + top + 70.0f, 20.0f, 20.0f };
    login_b = (Rectangle){ wx + pad + col_w + gap, wy + top + 56.0f, col_w, 50.0f };

    GuiLabel(user_l, "Username:");
    GuiLabel(pass_l, "Password:");
    hint_box(user_b, s->user, (int)sizeof s->user, &edit[ED_USER],
             "account name");
    if (saved) {
        /*
         * The form is gone, not ignored. A box that still takes a password while a saved sign-
         * in is being used is the confusing part: either it does nothing, or it quietly
         * overrides the thing the panel below says is happening.
         */
        edit[ED_PASS] = false;
        s->pass[0] = '\0';
        text_at(0, "not needed", pass_b.x + 2.0f,
                pass_b.y + (pass_b.height - 15.0f) * 0.5f, 15.0f,
                (Color){ 146, 154, 162, 255 });
        /* Nothing to tab to, so the name field's Enter is the press. */
        if (edit[ED_USER] && IsKeyPressed(KEY_ENTER)) {
            edit[ED_USER] = false;
            submitted = 1;
        }
        if (edit[ED_USER] && IsKeyPressed(KEY_TAB))
            edit[ED_USER] = false;
    } else {
        if (edit[ED_USER] && IsKeyPressed(KEY_TAB)) {
            edit[ED_USER] = false;
            edit[ED_PASS] = true;
        }
        if (edit[ED_USER] && IsKeyPressed(KEY_ENTER)) {
            edit[ED_USER] = false;
            edit[ED_PASS] = true;
        }
        if (password_box(pass_b, s->pass, (int)sizeof s->pass, &edit[ED_PASS]))
            submitted = 1;
    }
    /* The name only. The password used to be written beside it as plain text
     * and is not written anywhere now, so the box must not promise otherwise. */
    GuiCheckBox(rem_b, "Remember My Name", remember);
    /* Nothing can start without a game to start, so the press that would
     * have failed opens the picker instead: LOGIN with no ROM chosen is
     * choosing a ROM, and Enter in the password field does the same. */
    if (GuiButton(login_b, rom_ok ? "LOGIN" : "CHOOSE ROM..."))
        submitted = 1;
    if (submitted && !rom_ok)
        return rom_browse(s, VIEW_LOGIN);

    /*
     * Why this is here at all. With a saved sign-in the player presses LOGIN on an empty
     * password box and is let straight in, and a login window that does that without a word
     * reads as broken authentication rather than as a session being resumed.
     */
    if (saved) {
        Rectangle note = { wx + pad, wy + top + 112.0f,
                           ww - pad * 2.0f, 50.0f };
        Rectangle out_b = { note.x + note.width - 86.0f, note.y + 13.0f,
                            78.0f, 24.0f };
        char line[MMO_TOKEN_NAME + 40];

        DrawRectangleRounded(note, 0.18f, 6, (Color){ 26, 34, 30, 255 });
        DrawRectangleRoundedLinesEx(note, 0.18f, 6, 1.0f,
                                    (Color){ 58, 92, 74, 255 });
        /* Capped: a long account name would otherwise run under the Sign out
         * button rather than stopping before it. */
        snprintf(line, sizeof line, "Signed in as %.24s", who);
        text_at(0, line, note.x + 12.0f, note.y + 9.0f, 15.0f,
                (Color){ 150, 222, 150, 255 });
        text_at(0, "No password is stored on this device.",
                note.x + 12.0f, note.y + 27.0f, 14.0f,
                (Color){ 146, 154, 162, 255 });
        /* No sentence needed underneath: the green block goes, the panel
         * closes up and the box turns into an ordinary empty one, which is
         * the whole of what signing out means. */
        if (GuiButton(out_b, "Sign out"))
            saved_signin(1);
    }

    /* Right rail: official CP1 buttons are 200x26, stacked from the bottom. */
    {
        float bw = 200.0f, bh = 26.0f;
        float bx = (float)sw - bw - 8.0f;
        float by = (float)sh - (bh * 4.0f + 20.0f);

        DrawRectangleRounded((Rectangle){ bx - 6.0f, by - 6.0f, bw + 12.0f, bh * 4.0f + 24.0f },
                             0.12f, 4, (Color){ 20, 24, 28, 200 });
        if (GuiButton((Rectangle){ bx, by, bw, bh }, "Settings"))
            return VIEW_SETTINGS;
        if (GuiButton((Rectangle){ bx, by + bh + 4.0f, bw, bh }, "Graphics"))
            return VIEW_GRAPHICS;
        if (GuiButton((Rectangle){ bx, by + (bh + 4.0f) * 2.0f, bw, bh }, "Controls"))
            return VIEW_CONTROLS;
        if (GuiButton((Rectangle){ bx, by + (bh + 4.0f) * 3.0f, bw, bh }, "Exit"))
            return -1;
    }

    {
        const char *line = host->status != NULL ? host->status : "";
        Color c = (host->status_bad != NULL && *host->status_bad)
            ? (Color){ 240, 153, 153, 255 }
            : (Color){ 206, 206, 206, 255 };
        float y = panel.y + panel.height + 10.0f;

        if (host->conn != NULL && host->conn[0] != '\0') {
            Color cc = {
                (unsigned char)((host->conn_colour >> 16) & 0xFF),
                (unsigned char)((host->conn_colour >> 8) & 0xFF),
                (unsigned char)(host->conn_colour & 0xFF), 255
            };

            text_over_art(0, host->conn, panel.x, y, 15.0f, cc);
            y += 19.0f;
        }
        if (line[0] != '\0')
            text_over_art(0, line, panel.x, y, 15.0f, c);
    }

    /* The footer is the ROM, because a first run cannot start without one and
     * a path in small grey type at the bottom of the window is not a thing
     * anybody reads. Green when the game could start; a button when it could
     * not, so choosing one never means finding Settings first. */
    {
        const char *why = rom_why;
        int ok = rom_ok;
        float bh = 28.0f;
        float by = (float)sh - bh - 10.0f;
        Vector2 ts = text_dim(0, why, 15.0f);
        float pill = ts.x + 24.0f + (ok ? 0.0f : 132.0f);

        DrawRectangleRounded((Rectangle){ 10.0f, by - 4.0f, pill, bh + 8.0f },
                             0.35f, 6, (Color){ 20, 24, 28, 200 });
        text_at(0, why, 22.0f, by + (bh - ts.y) * 0.5f, 15.0f,
                ok ? (Color){ 150, 222, 150, 255 }
                   : (Color){ 240, 176, 150, 255 });
        if (!ok && GuiButton((Rectangle){ ts.x + 30.0f, by, 120.0f, bh },
                             "Choose ROM..."))
            browse = 1;
        {
            const char *cr = "Albert Bierstadt, Rocky Mountain Landscape, "
                             "1870. Public domain.";

            text_over_art(0, cr, 12.0f, by - 26.0f, 15.0f,
                          (Color){ 232, 232, 232, 255 });
        }
    }

    if (browse)
        return rom_browse(s, VIEW_LOGIN);
    if (submitted)
        do_play(host, playing);
    return VIEW_LOGIN;
}

static int draw_settings(struct launch_gui_host *host, bool *edit)
{
    mmo_launch_settings *s = host->set;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    float ww = 640.0f, wh = 348.0f;
    float wx, wy, y, lx, fx, fw;
    Rectangle panel;

    if (ww > (float)sw - 24.0f)
        ww = (float)sw - 24.0f;
    if (wh > (float)sh - 24.0f)
        wh = (float)sh - 24.0f;
    wx = ((float)sw - ww) * 0.5f;
    wy = ((float)sh - wh) * 0.5f;
    panel = (Rectangle){ wx, wy, ww, wh };
    draw_frame(panel, "Settings");

    lx = wx + 24.0f;
    fx = wx + 150.0f;
    fw = ww - 174.0f;
    y = wy + 52.0f;

    /*
     * No address, no port and no on/off: which server this client belongs to is compiled into
     * it, and going there is what Play means. The row that used to ask, and the save-file row
     * that only meant anything with it answered "single player", are both gone.
     */
    label_left((Rectangle){ lx, y, 120, 26 }, "Platinum");
    hint_box((Rectangle){ fx, y, fw - 100, 26 }, s->rom, (int)sizeof s->rom,
             &edit[ED_ROM], "(a folder with " MMO_LAUNCH_ROM_NAME ")");
    if (GuiButton((Rectangle){ fx + fw - 96, y, 96, 26 }, "Browse..."))
        return rom_browse(s, VIEW_SETTINGS);
    y += 24;
    {
        char why[220];
        int ok = rom_status(s, why, sizeof why);

        text_at(0, why, fx + 2.0f, y, 15.0f,
                ok ? (Color){ 150, 222, 150, 255 }
                   : (Color){ 236, 170, 150, 255 });
    }
    y += 22;
    label_left((Rectangle){ lx, y, 120, 26 }, "Heart Gold");
    hint_box((Rectangle){ fx, y, fw - 100, 26 }, s->rom_hg,
             (int)sizeof s->rom_hg, &edit[ED_ROM_HG], "(optional)");
    if (GuiButton((Rectangle){ fx + fw - 96, y, 96, 26 }, "Browse..."))
        slot_browse(s, 1);
    y += 24;
    {
        char why[160];
        int ok = mmo_sound_slot_status(s, 1, NULL, 0, why, sizeof why);

        text_at(0, why, fx + 2.0f, y, 15.0f,
                ok ? (Color){ 150, 222, 150, 255 }
                   : (Color){ 200, 200, 170, 255 });
    }
    y += 22;
    label_left((Rectangle){ lx, y, 120, 26 }, "Black / White");
    hint_box((Rectangle){ fx, y, fw - 100, 26 }, s->rom_bw,
             (int)sizeof s->rom_bw, &edit[ED_ROM_BW], "(optional)");
    if (GuiButton((Rectangle){ fx + fw - 96, y, 96, 26 }, "Browse..."))
        slot_browse(s, 2);
    y += 24;
    {
        char why[160];
        int ok = mmo_sound_slot_status(s, 2, NULL, 0, why, sizeof why);

        text_at(0, why, fx + 2.0f, y, 15.0f,
                ok ? (Color){ 150, 222, 150, 255 }
                   : (Color){ 200, 200, 170, 255 });
    }
    y += 22;
    {
        float half = (fw - 12.0f) * 0.5f;
        bool au = s->audio != 0;
        bool dc = s->discord != 0;

        label_left((Rectangle){ lx, y, 120, 26 }, "Buttons");
        GuiComboBox((Rectangle){ fx, y, half, 26 }, button_opts, &s->button_mode);
        GuiCheckBox((Rectangle){ fx + half + 12.0f, y + 2, 22, 22 }, "Sound", &au);
        /* Beside Sound rather than on a row of its own: this panel's last row
         * sits 30 pixels above the buttons along the bottom. */
        GuiCheckBox((Rectangle){ fx + half + 122.0f, y + 2, 22, 22 }, "Discord", &dc);
        s->audio = au;
        s->discord = dc;
    }
    y += 30;
    /*
     * Whose sound Sinnoh plays, the composed packages behind mmo/SOUNDTRACKS and
     * mmo/SOUNDFONTS. The choice is checked at Play, which refuses by name if the named
     * package has not been composed into the install's mods folder.
     */
    label_left((Rectangle){ lx, y, 120, 26 }, "Soundtrack");
    GuiComboBox((Rectangle){ fx, y, fw, 26 }, soundtrack_opts, &s->soundtrack);

    if (GuiButton((Rectangle){ wx + 24.0f, wy + wh - 44.0f, 120, 28 }, "Graphics"))
        return VIEW_GRAPHICS;
    if (GuiButton((Rectangle){ wx + 152.0f, wy + wh - 44.0f, 120, 28 }, "Controls"))
        return VIEW_CONTROLS;
    /* The folder every log in this client is written into, opened in the
     * player's own file manager. Being asked for a log should not be being
     * asked to find a path: this is the whole of what "send me your logs"
     * costs a player now. */
    if (GuiButton((Rectangle){ wx + 280.0f, wy + wh - 44.0f, 120, 28 }, "Logs")) {
        char logs[MMO_LAUNCH_PATH];

        if (mmo_plat_log_dir(logs, sizeof logs) == 0)
            mmo_plat_open_folder(logs);
    }
    if (GuiButton((Rectangle){ wx + ww - 120.0f, wy + wh - 44.0f, 96, 28 }, "Back"))
        return VIEW_LOGIN;
    return VIEW_SETTINGS;
}

/* The window before a frame exists: everything the plan turns into --scale,
 * --layout, PC_ASPECT and PC_HD3D, on its own panel behind one button. */
static int draw_graphics(struct launch_gui_host *host, int from_settings)
{
    mmo_launch_settings *s = host->set;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    float ww = 560.0f, wh = 456.0f;
    float wx, wy, y, lx, fx, fw, half;
    int rs, hd;
    Rectangle panel;

    if (ww > (float)sw - 24.0f)
        ww = (float)sw - 24.0f;
    if (wh > (float)sh - 24.0f)
        wh = (float)sh - 24.0f;
    wx = ((float)sw - ww) * 0.5f;
    wy = ((float)sh - wh) * 0.5f;
    panel = (Rectangle){ wx, wy, ww, wh };
    draw_frame(panel, "Graphics");

    lx = wx + 24.0f;
    fx = wx + 170.0f;
    fw = ww - 194.0f;
    half = (fw - 12.0f) * 0.5f;
    y = wy + 52.0f;
    rs = s->render_scale;
    hd = s->hd3d - MMO_LAUNCH_HD3D_MIN;

    /* Scale as a combo so auto reads as a word, not a zero. Auto is 0 and
     * the sizes are 1..MAX, so the value is the combo index. */
    label_left((Rectangle){ lx, y, 140, 26 }, "Window scale");
    GuiComboBox((Rectangle){ fx, y, fw, 26 }, scale_opts, &s->scale);
    y += 32;

    /* Viewport: the named shapes, plus a hand-typed width from the config
     * shown as its own entry so opening this panel cannot erase it. */
    {
        char opts[MMO_LAUNCH_VIEWPORT + 40];
        int vp = 0, named = 0, i;

        for (i = 0; i < VIEWPORT_NAMED; i++)
            if (strcmp(s->viewport, viewport_names[i]) == 0) {
                vp = i;
                named = 1;
            }
        if (s->viewport[0] == '\0')
            named = 1;
        if (!named) {
            snprintf(opts, sizeof opts, "auto;native;16:9;16:10;4:3;%s",
                     s->viewport);
            vp = VIEWPORT_NAMED;
        } else
            snprintf(opts, sizeof opts, "auto;native;16:9;16:10;4:3");
        label_left((Rectangle){ lx, y, 140, 26 }, "Viewport");
        GuiComboBox((Rectangle){ fx, y, fw, 26 }, opts, &vp);
        if (vp < VIEWPORT_NAMED)
            snprintf(s->viewport, sizeof s->viewport, "%s", viewport_names[vp]);
    }
    y += 32;

    /* The official client's per-map distance is 100; farther out shows more world. */
    y += 32;

    label_left((Rectangle){ lx, y, 140, 26 }, "Render scale");
    GuiSpinner((Rectangle){ fx, y, half, 26 }, NULL, &rs, 1,
               MMO_LAUNCH_RS_MAX, false);
    s->render_scale = rs;
    y += 32;
    label_left((Rectangle){ lx, y, 140, 26 }, "3D detail");
    GuiComboBox((Rectangle){ fx, y, fw, 26 }, hd3d_opts, &hd);
    s->hd3d = MMO_LAUNCH_HD3D_MIN + hd;
    y += 32;
    label_left((Rectangle){ lx, y, 140, 26 }, "Frame pacing");
    GuiComboBox((Rectangle){ fx, y, fw, 26 }, pace_opts, &s->pace);
    y += 32;

    label_left((Rectangle){ lx, y, 140, 26 }, "Layout");
    GuiComboBox((Rectangle){ fx, y, fw, 26 }, layout_opts, &s->layout);
    y += 32;
    label_left((Rectangle){ lx, y, 140, 26 }, "Filter");
    GuiComboBox((Rectangle){ fx, y, fw, 26 }, filter_opts, &s->filter);
    y += 32;
    label_left((Rectangle){ lx, y, 140, 26 }, "Fit");
    GuiComboBox((Rectangle){ fx, y, fw, 26 }, fit_opts, &s->fit);
    y += 32;
    {
        bool fs = s->fullscreen != 0;

        GuiCheckBox((Rectangle){ fx, y, 22, 22 }, "Fullscreen", &fs);
        s->fullscreen = fs;
    }

    if (GuiButton((Rectangle){ wx + ww - 120.0f, wy + wh - 44.0f, 96, 28 }, "Back"))
        return from_settings ? VIEW_SETTINGS : VIEW_LOGIN;
    return VIEW_GRAPHICS;
}

/* ------------------------------------------------------------------ */
/* Controls: the window's key map, rebound from the front door         */
/* ------------------------------------------------------------------ */

/*
 * raylib key -> the SDL scancode name the window's --bind resolves with
 * SDL_GetScancodeFromName.
 */
static const struct { int rl; const char *sdl; } RL_SDL[] = {
    { KEY_SPACE, "Space" }, { KEY_ENTER, "Return" }, { KEY_TAB, "Tab" },
    { KEY_BACKSPACE, "Backspace" },
    { KEY_UP, "Up" }, { KEY_DOWN, "Down" },
    { KEY_LEFT, "Left" }, { KEY_RIGHT, "Right" },
    { KEY_INSERT, "Insert" }, { KEY_DELETE, "Delete" },
    { KEY_HOME, "Home" }, { KEY_END, "End" },
    { KEY_PAGE_UP, "PageUp" }, { KEY_PAGE_DOWN, "PageDown" },
    { KEY_CAPS_LOCK, "CapsLock" },
    { KEY_LEFT_SHIFT, "Left Shift" }, { KEY_RIGHT_SHIFT, "Right Shift" },
    { KEY_LEFT_CONTROL, "Left Ctrl" }, { KEY_RIGHT_CONTROL, "Right Ctrl" },
    { KEY_LEFT_ALT, "Left Alt" }, { KEY_RIGHT_ALT, "Right Alt" },
    { KEY_APOSTROPHE, "'" }, { KEY_COMMA, "," }, { KEY_MINUS, "-" },
    { KEY_PERIOD, "." }, { KEY_SLASH, "/" }, { KEY_SEMICOLON, ";" },
    { KEY_EQUAL, "=" }, { KEY_LEFT_BRACKET, "[" },
    { KEY_BACKSLASH, "\\" }, { KEY_RIGHT_BRACKET, "]" }, { KEY_GRAVE, "`" },
    { KEY_KP_DECIMAL, "Keypad ." }, { KEY_KP_DIVIDE, "Keypad /" },
    { KEY_KP_MULTIPLY, "Keypad *" }, { KEY_KP_SUBTRACT, "Keypad -" },
    { KEY_KP_ADD, "Keypad +" }, { KEY_KP_ENTER, "Keypad Enter" },
};

static const char *rl_key_name(int k)
{
    static char buf[16];
    int i;

    if (k >= KEY_A && k <= KEY_Z) {
        snprintf(buf, sizeof buf, "%c", 'A' + (k - KEY_A));
        return buf;
    }
    if (k >= KEY_ZERO && k <= KEY_NINE) {
        snprintf(buf, sizeof buf, "%c", '0' + (k - KEY_ZERO));
        return buf;
    }
    if (k >= KEY_F1 && k <= KEY_F12) {
        snprintf(buf, sizeof buf, "F%d", 1 + (k - KEY_F1));
        return buf;
    }
    if (k >= KEY_KP_0 && k <= KEY_KP_9) {
        snprintf(buf, sizeof buf, "Keypad %d", k - KEY_KP_0);
        return buf;
    }
    for (i = 0; i < (int)(sizeof RL_SDL / sizeof RL_SDL[0]); i++)
        if (RL_SDL[i].rl == k)
            return RL_SDL[i].sdl;
    return NULL;
}

static int  ctl_wait = -1;      /* the pad listening for its key, -1 none */
static char ctl_note[96];

/* The login art, at file scope so a frame pumped mid-update draws the same
 * face the menu does. Loaded and unloaded by launch_gui_run. */
static Texture2D gui_bg;

/*
 * One frame of "the launcher is doing something", drawn from inside the blocking update:
 * do_play runs the Play callback outside Begin/EndDrawing, so pumping a whole frame here is
 * legal, and it is what keeps the window repainting (and, on Windows, out of "Not
 * Responding") while a download runs.
 */
void launch_gui_progress(const char *line)
{
    static double last;
    double now;
    int sw, sh;
    Rectangle panel;

    if (!IsWindowReady())
        return;
    now = GetTime();
    if (now - last < 0.05)
        return;
    last = now;
    sw = GetScreenWidth();
    sh = GetScreenHeight();
    BeginDrawing();
    if (gui_bg.id != 0)
        cover_texture(gui_bg, sw, sh);
    else
        ClearBackground((Color){ 18, 28, 36, 255 });
    DrawRectangle(0, 0, sw, sh, (Color){ 0, 0, 0, 70 });
    panel = (Rectangle){ ((float)sw - 560.0f) * 0.5f,
                         ((float)sh - 128.0f) * 0.5f, 560.0f, 128.0f };
    draw_frame(panel, "Updating");
    text_at(0, line, panel.x + 24.0f, panel.y + 56.0f, 15.0f,
            (Color){ 224, 224, 224, 255 });
    text_at(0, "the game starts when this finishes",
            panel.x + 24.0f, panel.y + 84.0f, 15.0f,
            (Color){ 150, 160, 170, 255 });
    EndDrawing();
}

static int draw_controls(struct launch_gui_host *host, int from_settings)
{
    mmo_launch_settings *s = host->set;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    float ww = 640.0f, wh = 336.0f;
    float wx, wy, y0, colw;
    Rectangle panel;
    int i;

    if (ww > (float)sw - 24.0f)
        ww = (float)sw - 24.0f;
    if (wh > (float)sh - 24.0f)
        wh = (float)sh - 24.0f;
    wx = ((float)sw - ww) * 0.5f;
    wy = ((float)sh - wh) * 0.5f;
    panel = (Rectangle){ wx, wy, ww, wh };
    draw_frame(panel, "Controls");

    /* Two columns of six, the DS order reading down. Each row is the button,
     * what the field does with it, and the key it answers to; the key is the
     * control, so clicking it is how it changes. */
    y0 = wy + 52.0f;
    colw = (ww - 60.0f) * 0.5f;
    for (i = 0; i < MMO_LAUNCH_PADS; i++) {
        float cx = wx + 24.0f + (float)(i / 6) * (colw + 12.0f);
        float cy = y0 + (float)(i % 6) * 34.0f;
        char cur[MMO_LAUNCH_TEXT], shown[MMO_LAUNCH_TEXT + 12];
        int dup = 0, j;

        mmo_launch_bind_get(s->bind, i, cur, sizeof cur);
        for (j = 0; j < MMO_LAUNCH_PADS && !dup; j++) {
            char other[MMO_LAUNCH_TEXT];

            if (j != i &&
                mmo_launch_bind_get(s->bind, j, other, sizeof other) == 0 &&
                strcasecmp(other, cur) == 0)
                dup = 1;
        }
        label_left((Rectangle){ cx, cy, colw - 134.0f, 26 },
                   mmo_launch_pad_label(i));
        if (ctl_wait == i)
            snprintf(shown, sizeof shown, "press a key...");
        else
            snprintf(shown, sizeof shown, dup ? "%s (shared)" : "%s", cur);
        if (GuiButton((Rectangle){ cx + colw - 130.0f, cy, 130, 26 }, shown)) {
            ctl_wait = (ctl_wait == i) ? -1 : i;
            ctl_note[0] = '\0';
        }
    }

    /* The listening row takes the next key. Esc backs out; a key this table
     * cannot name is refused with a line, never bound to a guess. */
    if (ctl_wait >= 0) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            ctl_wait = -1;
        } else {
            int k = GetKeyPressed();

            if (k > 0) {
                const char *name = rl_key_name(k);

                if (name == NULL) {
                    snprintf(ctl_note, sizeof ctl_note,
                             "that key has no name here, try another");
                } else if (mmo_launch_bind_set(s->bind, sizeof s->bind,
                                               ctl_wait, name) != 0) {
                    snprintf(ctl_note, sizeof ctl_note,
                             "that key cannot be bound");
                } else {
                    ctl_note[0] = '\0';
                    ctl_wait = -1;
                }
            }
        }
    }

    {
        char line[160];
        Color c = { 206, 206, 206, 255 };

        if (ctl_wait >= 0)
            snprintf(line, sizeof line, "press the key for %s (Esc cancels)",
                     mmo_launch_pad_label(ctl_wait));
        else if (ctl_note[0] != '\0')
            snprintf(line, sizeof line, "%s", ctl_note);
        else
            snprintf(line, sizeof line,
                     "click a key to change it; two rows sharing a key both fire");
        if (ctl_note[0] != '\0' && ctl_wait < 0)
            c = (Color){ 236, 170, 150, 255 };
        text_at(0, line, wx + 24.0f, y0 + 6.0f * 34.0f + 6.0f, 15.0f, c);
    }

    /* Defaults erases the row, because empty is the default map, there is
     * no second copy to restore from. */
    if (GuiButton((Rectangle){ wx + 24.0f, wy + wh - 44.0f, 120, 28 },
                  "Defaults")) {
        s->bind[0] = '\0';
        ctl_wait = -1;
        ctl_note[0] = '\0';
    }
    if (GuiButton((Rectangle){ wx + ww - 120.0f, wy + wh - 44.0f, 96, 28 },
                  "Back")) {
        ctl_wait = -1;
        ctl_note[0] = '\0';
        return from_settings ? VIEW_SETTINGS : VIEW_LOGIN;
    }
    return VIEW_CONTROLS;
}

static int draw_browse(mmo_launch_settings *s)
{
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    float ww = (float)sw - 120.0f, wh = (float)sh - 80.0f;
    float wx = 60.0f, wy = 40.0f;
    Rectangle panel, box;
    float y;
    int i, opened = -1;

    if (ww < 480.0f)
        ww = (float)sw - 16.0f, wx = 8.0f;
    if (wh < 300.0f)
        wh = (float)sh - 16.0f, wy = 8.0f;
    panel = (Rectangle){ wx, wy, ww, wh };
    draw_frame(panel, "Choose your ROM");

    y = wy + 48.0f;

    /* Where it is, in one line that ellipsises from the front: the end of a
     * path is the part that tells a person where they are. */
    {
        const char *shown = br.dir;
        char cut[MMO_LAUNCH_PATH + 4];
        float room = ww - 40.0f;

        if (text_dim(0, shown, 15.0f).x > room) {
            size_t L = strlen(shown);
            size_t drop = 0;

            while (drop < L) {
                snprintf(cut, sizeof cut, "...%s", shown + drop);
                if (text_dim(0, cut, 15.0f).x <= room)
                    break;
                drop++;
            }
            shown = cut;
        }
        text_at(0, shown, wx + 20.0f, y, 15.0f,
                (Color){ 224, 224, 224, 255 });
    }
    y += 24.0f;

    /* The folders a cartridge is actually in, one press each. */
    {
        float bx = wx + 20.0f;

        for (i = 0; i < br.nplaces; i++) {
            float bwid = text_dim(0, br.places[i].label, 15.0f).x + 24.0f;

            if (bx + bwid > wx + ww - 20.0f)
                break;
            if (GuiButton((Rectangle){ bx, y, bwid, 26 }, br.places[i].label))
                browse_go(br.places[i].dir);
            bx += bwid + 8.0f;
        }
    }
    y += 34.0f;

    box = (Rectangle){ wx + 20.0f, y, ww - 40.0f, wh - (y - wy) - 100.0f };
    {
        int before = br.active;

        GuiListViewEx(box, br.ptr, br.n, &br.scroll, &br.active, &br.focus);

        /* Double-click is what a file window does. raygui reports only which
         * row is active and clears it when the row already selected is
         * pressed again, which is exactly the press a double-click is made
         * of, so the row from before the call is the one that was hit. */
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
            CheckCollisionPointRec(GetMousePosition(), box)) {
            int row = br.active >= 0 ? br.active : before;
            double now = GetTime();

            if (row >= 0) {
                if (row == br.click_row && now - br.click_at < 0.5)
                    opened = row;
                br.click_row = row;
                br.click_at = now;
                br.active = row;
            }
        }
    }

    /* And the keys, so the whole window works without the mouse. */
    if (IsKeyPressed(KEY_DOWN) && br.n > 0)
        br.active = br.active + 1 >= br.n ? br.n - 1 : br.active + 1;
    if (IsKeyPressed(KEY_UP) && br.n > 0)
        br.active = br.active <= 0 ? 0 : br.active - 1;
    if (IsKeyPressed(KEY_ENTER) && br.active >= 0)
        opened = br.active;
    if (IsKeyPressed(KEY_BACKSPACE))
        parent_dir(br.dir, sizeof br.dir), browse_load();
    if (IsKeyPressed(KEY_ESCAPE))
        br.on = 0;
    /* Keep the chosen row on screen when the keys moved it. The row pitch is
     * the list's own, and raygui throws the scroll back to the top if it is
     * ever past the last full page. */
    if (br.active >= 0) {
        int pitch = GuiGetStyle(LISTVIEW, LIST_ITEMS_HEIGHT)
            + GuiGetStyle(LISTVIEW, LIST_ITEMS_SPACING);
        int rows = pitch > 0 ? (int)box.height / pitch : 1;

        if (rows < 1)
            rows = 1;
        if (rows > br.n)
            rows = br.n;
        if (br.active < br.scroll)
            br.scroll = br.active;
        else if (br.active >= br.scroll + rows)
            br.scroll = br.active - rows + 1;
        if (br.scroll > br.n - rows)
            br.scroll = br.n - rows;
        if (br.scroll < 0)
            br.scroll = 0;
    }

    /* Whether this folder is one the game could start from. */
    {
        const char *keys = "Enter opens   Backspace goes up   Esc closes";
        Vector2 ks = text_dim(0, keys, 15.0f);

        text_at(0, br.note, wx + 20.0f, wy + wh - 62.0f, 15.0f,
                br.note_ok ? (Color){ 150, 222, 150, 255 }
                           : (Color){ 214, 200, 150, 255 });
        text_at(0, keys, wx + ww - 20.0f - ks.x, wy + wh - 61.0f, 15.0f,
                (Color){ 170, 178, 186, 255 });
    }

    {
        float by = wy + wh - 40.0f;
        int can_folder = br.note_ok;
        int can_file = br.active >= 0 && br.active < br.n && br.kind[br.active] == 2;

        if (!can_folder)
            GuiDisable();
        if (GuiButton((Rectangle){ wx + 20.0f, by, 150, 28 }, "Use this folder")) {
            snprintf(s->rom, sizeof s->rom, "%s", br.dir);
            br.on = 0;
        }
        if (!can_folder)
            GuiEnable();
        if (!can_file)
            GuiDisable();
        if (GuiButton((Rectangle){ wx + 178.0f, by, 130, 28 }, "Use this file"))
            browse_pick(s, br.active);
        if (!can_file)
            GuiEnable();
        if (GuiButton((Rectangle){ wx + ww - 120.0f, by, 100, 28 }, "Cancel"))
            br.on = 0;
    }

    if (opened >= 0)
        browse_pick(s, opened);
    return br.on ? VIEW_BROWSE : br.back;
}

int launch_gui_run(struct launch_gui_host *host)
{
    mmo_launch_settings *s;
    bool edit[ED_N] = { 0 };
    bool remember;
    int  playing = 0;
    int view = VIEW_LOGIN;
    int gfx_from_settings = 0;
    int ctl_from_settings = 0;
    int shot_left = 0;
    const char *shot_path;
    Texture2D wordmark;

    if (host == NULL || host->set == NULL)
        return 1;
    s = host->set;
    remember = s->pass[0] != '\0' || s->user[0] != '\0';

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(WIN_W, WIN_H, "OpenMMO");
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    /* The face is whatever this machine has (platform.h): DejaVu where a
     * distribution ships it, the system UI face on Windows. Without one the
     * built-in raylib font draws, which is legible and plain. */
    have_font = 0;
    font_bake_n[0] = font_bake_n[1] = 0;
    font_face[0][0] = font_face[1][0] = '\0';
    if (mmo_plat_ui_font(0, font_face[0], sizeof font_face[0]) == 0) {
        if (mmo_plat_ui_font(1, font_face[1], sizeof font_face[1]) != 0)
            snprintf(font_face[1], sizeof font_face[1], "%s", font_face[0]);
        have_font = 1;
        /* One bake up front says whether the face on this machine loads at
         * all; style_login() below is what hands raygui its own. */
        face_metrics();
        ui_font(0, ui_px(16.0f));
        if (font_bake_n[0] == 0)
            have_font = 0;
    }
    style_login();
    gui_bg = load_res("bg.png");
    wordmark = load_res("wordmark.png");
    if (gui_bg.id != 0)
        SetTextureFilter(gui_bg, TEXTURE_FILTER_BILINEAR);
    if (wordmark.id != 0)
        SetTextureFilter(wordmark, TEXTURE_FILTER_BILINEAR);
    shot_path = getenv("OPENMMO_LAUNCH_SHOT");
    if (shot_path != NULL && shot_path[0] != '\0') {
        const char *shot_view = getenv("OPENMMO_LAUNCH_SHOT_VIEW");

        shot_left = 10;
        if (shot_view != NULL) {
            if (strcmp(shot_view, "settings") == 0)
                view = VIEW_SETTINGS;
            else if (strcmp(shot_view, "graphics") == 0)
                view = VIEW_GRAPHICS;
            else if (strcmp(shot_view, "controls") == 0)
                view = VIEW_CONTROLS;
            else if (strcmp(shot_view, "browse") == 0) {
                browse_open(s, VIEW_LOGIN);
                view = VIEW_BROWSE;
            }
        }
    }

    while (!WindowShouldClose()) {
        int next;
        if (br.on)
            view = VIEW_BROWSE;

        BeginDrawing();
        if (gui_bg.id != 0)
            cover_texture(gui_bg, GetScreenWidth(), GetScreenHeight());
        else
            ClearBackground((Color){ 18, 28, 36, 255 });
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                      (Color){ 0, 0, 0, 70 });

        if (view == VIEW_BROWSE)
            next = draw_browse(s);
        else if (view == VIEW_SETTINGS)
            next = draw_settings(host, edit);
        else if (view == VIEW_GRAPHICS)
            next = draw_graphics(host, gfx_from_settings);
        else if (view == VIEW_CONTROLS)
            next = draw_controls(host, ctl_from_settings);
        else
            next = launch_gui_logingui(host, wordmark, edit, &remember, &playing);

        if (next == VIEW_GRAPHICS && view != VIEW_GRAPHICS)
            gfx_from_settings = view == VIEW_SETTINGS;
        if (next == VIEW_CONTROLS && view != VIEW_CONTROLS)
            ctl_from_settings = view == VIEW_SETTINGS;

        EndDrawing();
        if (shot_left > 0 && --shot_left == 0) {
            TakeScreenshot(shot_path);
            break;
        }
        if (next < 0)
            break;
        view = next;
    }

    if (!remember)
        s->pass[0] = '\0';

    if (gui_bg.id != 0)
        UnloadTexture(gui_bg);
    if (wordmark.id != 0)
        UnloadTexture(wordmark);
    {
        int b, i;

        for (b = 0; b < 2; b++)
            for (i = 0; i < font_bake_n[b]; i++)
                UnloadFont(font_bake[b][i]);
        font_bake_n[0] = font_bake_n[1] = 0;
    }
    CloseWindow();
    return 0;
}
