/* A name drawn over a player standing in the world. */

#include <nitro.h>
#include <nnsys.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/charcode.h"

#include "charcode.h"
#include "font.h"
#include "map_object.h"
#include "render_text.h"

#include "pc_video.h"

/* The client's own charcode bridge. The engine header of the same name won the
 * include above (a mod's sources compile with the game's include path), so ours
 * is reached by path, the same seam openmmo_name.c documents. */
#include "../../../include/endpoint.h"
#include "../../../include/charcode.h"
#include "../../../include/entity.h"

typedef char openmmo_label_charcode_width_check[sizeof(mmo_charcode) == sizeof(charcode_t) ? 1 : -1];

/* Glyphs kept per label. A official name is at most sixteen and the prefix a few
 * more; past that the label is cut, because a nameplate wider than the screen
 * is not a nameplate. */
#define LABEL_MAX 24

/* World units the label's anchor sits above the sprite's own origin. */
#define LABEL_LIFT_DEFAULT 56

/* Font_HasManager, added by patches/src/font.c.patch: whether a font has a
 * manager behind it. Font_TryLoadGlyph dereferences one unconditionally, which
 * is fine for a caller that is part of a screen and a null dereference for
 * anything drawn outside one. */
extern BOOL Font_HasManager(enum Font font);

/* The palette indices a decompressed glyph comes back in are the last text
 * printer's, not fixed ones. openmmo_font.c says why this has to be set before
 * a glyph is read from outside the printer system; without it a plate drawn
 * after the lobby printed a card was painted entirely in the shadow colour. */
extern void openmmo_glyph_colors(void);
extern int openmmo_font_draw_utf8(uint32_t *surf, int x, int y, const char *s,
                                  uint32_t fg);
extern int openmmo_font_width_utf8(const char *s);
/* Whether a window is attached and reading the hud page. */
extern int openmmo_hud_windowed(void);

static struct {
    int used;
    int width;   /* pixels, measured once the font is loadable; -1 = not yet */
    char utf8[LABEL_MAX + 1]; /* the string we painted, for the report */
    charcode_t text[LABEL_MAX + 1];
} sLabels[OPENMMO_ENTITY_NETID_CEIL];

static int sEnabled = -1; /* -1 = not yet read from the environment */
static int sLift;
/* 0 off, 1 once a second, 2 every frame. Two is what pairs a projection with the
 * rasterizer's own polygon list for that frame (PC_DUMP_POLYS), which is the
 * only check that says the projection agrees with the picture. */
static int sReport;
/* The port's own frame number, so a report line and a polygon dump name the
 * same instant. */
static uint64_t sFrame;

/* This frame'S plates, for the window to draw. */
#define PLATE_MAX 16

static struct {
    int  x, y;
    char name[LABEL_MAX + 1];
} sPlates[PLATE_MAX];
static int sPlateN;

/* Cleared at the top of each frame by openmmo_label_frame(). */
int openmmo_label_plate_count(void)
{
    return sPlateN;
}

int openmmo_label_plate_at(int i, int *x, int *y, const char **name)
{
    if (i < 0 || i >= sPlateN)
        return 0;
    if (x != NULL)
        *x = sPlates[i].x;
    if (y != NULL)
        *y = sPlates[i].y;
    if (name != NULL)
        *name = sPlates[i].name;
    return 1;
}

/* Counters a check can read instead of a screenshot. */
static unsigned long sDrawn;    /* labels painted */
static unsigned long sOffScreen; /* labels whose anchor projected off screen */

/* One line at exit under OPENMMO_LABEL_REPORT, so a headless run can be asked
 * whether anything was drawn at all, a label that is never painted and a label
 * that is painted off screen look the same in a frame dump. */
static void ReportAtExit(void)
{
    printf("openmmo: labels %lu drawn, %lu projected off screen\n",
        sDrawn, sOffScreen);
    fflush(stdout);
}

static void ReadEnv(void)
{
    const char *e;

    e = openmmo_dev_env("OPENMMO_LABELS");
    sEnabled = !(e != NULL && e[0] == '0' && e[1] == '\0');

    e = openmmo_dev_env("OPENMMO_LABEL_LIFT");
    sLift = (e != NULL && e[0] != '\0') ? atoi(e) : LABEL_LIFT_DEFAULT;

    e = getenv("OPENMMO_LABEL_REPORT");
    sReport = (e != NULL && e[0] != '\0') ? atoi(e) : 0;
    if (sReport) {
        atexit(ReportAtExit);
    }
}

/* --- the store ----------------------------------------------------------- */

/*
 * A name as the wire carried it: UTF-8, which is what the codec decodes a server UTF-16 name
 * into (codec.c, mmo_get_utf16_nt), and what the charcode bridge takes directly, so nothing
 * here reinterprets a byte, and an unmapped glyph is counted rather than silent.
 */
void openmmo_label_set(int slot, const char *name)
{
    mmo_charcode_result r;
    size_t n;

    if (slot < 0 || slot >= OPENMMO_ENTITY_NETID_CEIL) {
        return;
    }
    /* A missing name is still a person. "?" is the same visible fallback the
     * charcode bridge uses for an unmapped glyph, it cannot be mistaken for
     * a real name the server sent. */
    if (name == NULL || name[0] == '\0') {
        name = "?";
    }

    n = strlen(name);
    if (n > LABEL_MAX) {
        n = LABEL_MAX;
        while (n > 0 && ((unsigned char)name[n] & 0xC0u) == 0x80u) {
            n--;
        }
    }
    memcpy(sLabels[slot].utf8, name, n);
    sLabels[slot].utf8[n] = '\0';

    r = mmo_utf8_to_charcode(sLabels[slot].utf8,
        (mmo_charcode *)sLabels[slot].text, LABEL_MAX + 1);
    sLabels[slot].used = r.written > 0;
    sLabels[slot].width = -1;
}

const char *openmmo_label_text(int slot)
{
    if (slot < 0 || slot >= OPENMMO_ENTITY_NETID_CEIL || !sLabels[slot].used)
        return "";
    return sLabels[slot].utf8;
}

void openmmo_label_clear(int slot)
{
    if (slot < 0 || slot >= OPENMMO_ENTITY_NETID_CEIL) {
        return;
    }

    sLabels[slot].used = 0;
    sLabels[slot].width = -1;
    sLabels[slot].utf8[0] = '\0';
    sLabels[slot].text[0] = CHAR_EOS;
}

void openmmo_label_clear_all(void)
{
    int i;

    for (i = 0; i < OPENMMO_ENTITY_NETID_CEIL; i++) {
        openmmo_label_clear(i);
    }
}

/* --- the projection ------------------------------------------------------ */

/* The world point the field draws a map object's sprite at, before the label's
 * own lift. Composed from the engine's public accessors; the terms and their
 * order are ov5_021ECC20.c:144 and the +6.0 z is ov5_021ECE40.c:1442. */
void openmmo_label_sprite_pos(const MapObject *obj, VecFx32 *out)
{
    VecFx32 pos, jump, spr, terrain;

    MapObject_GetPosPtr(obj, &pos);
    MapObject_GetSpriteJumpOffset(obj, &jump);
    MapObject_GetSpritePosOffset(obj, &spr);
    MapObject_GetSpriteTerrainOffset(obj, &terrain);

    out->x = pos.x + jump.x + spr.x + terrain.x;
    out->y = pos.y + jump.y + spr.y + terrain.y;
    out->z = pos.z + jump.z + spr.z + terrain.z + (FX32_ONE * 6);
}

/* The engine's own projection, answered with the matrices the frame was drawn
 * with. Returns 0 when the point is inside the viewport, -1 when it is not; the
 * pixel is written either way, because a label whose anchor is one pixel off the
 * top still has glyphs on screen. */
int openmmo_label_project(const VecFx32 *world, int *sx, int *sy)
{
    return NNS_G3dWorldPosToScrPos(world, sx, sy);
}

/* --- the paint ----------------------------------------------------------- */

static void FillRect(uint32_t *surf, int x, int y, int w, int h, uint32_t color)
{
    int px, py;

    for (py = y; py < y + h; py++) {
        if (py < 0 || py >= PC_VIDEO_HEIGHT) {
            continue;
        }
        for (px = x; px < x + w; px++) {
            if (px < 0 || px >= PC_VIDEO_WIDTH) {
                continue;
            }
            surf[py * PC_VIDEO_WIDTH + px] = color;
        }
    }
}

/* Measured once per label, not per frame: a name does not change and each
 * measurement is a glyph load the substitution counter would otherwise count
 * twice. */
static int LabelWidth(int slot)
{
    int w = openmmo_font_width_utf8(sLabels[slot].utf8);

    sLabels[slot].width = w;
    return w;
}

/* Paint one slot's label above the object it belongs to. Silently does nothing
 * when there is no label, no font, no surface or no live 3D transform. */
void openmmo_label_draw_for(int slot, const MapObject *obj)
{
    VecFx32 world;
    uint32_t *surf;
    int sx, sy, w, x, y, onScreen;

    if (sEnabled < 0) {
        ReadEnv();
    }
    if (!sEnabled || obj == NULL) {
        return;
    }
    if (slot < 0 || slot >= OPENMMO_ENTITY_NETID_CEIL || !sLabels[slot].used) {
        return;
    }
    if (!Font_HasManager(FONT_SYSTEM)) {
        return;
    }
    openmmo_glyph_colors();

    surf = pc_video_surface(pc_video_upper_engine());
    if (surf == NULL) {
        return;
    }

    openmmo_label_sprite_pos(obj, &world);
    world.y += (fx32)sLift * FX32_ONE;

    onScreen = openmmo_label_project(&world, &sx, &sy) == 0;
    if (!onScreen) {
        sOffScreen++;
    }

    w = sLabels[slot].width >= 0 ? sLabels[slot].width : LabelWidth(slot);

    /* Centred on the anchor and sitting just above it. */
    x = sx - w / 2;
    y = sy - 14;

    if (sReport >= 2 || (sReport == 1 && (sFrame % 60) == 0)) {
        printf("openmmo: frame %llu label slot %d \"%s\" world (%d,%d,%d) ->"
               " screen (%d,%d)%s w=%d\n",
            (unsigned long long)sFrame, slot, sLabels[slot].utf8,
            (int)(world.x >> FX32_SHIFT), (int)(world.y >> FX32_SHIFT),
            (int)(world.z >> FX32_SHIFT), sx, sy, onScreen ? "" : " off-screen", w);
        fflush(stdout);
    }

    /* Nothing of the label would land on screen; skip the glyph loads too, so a
     * crowd behind the camera costs a projection each and nothing more. */
    if (x + w < 0 || x >= PC_VIDEO_WIDTH || y + 12 < 0 || y >= PC_VIDEO_HEIGHT) {
        return;
    }

    if (sPlateN < PLATE_MAX) {
        sPlates[sPlateN].x = sx;
        sPlates[sPlateN].y = sy;
        snprintf(sPlates[sPlateN].name, sizeof sPlates[sPlateN].name, "%s",
                 sLabels[slot].utf8);
        sPlateN++;
    }

    /* A window of our own is drawing these, in its own pixels. */
    if (openmmo_hud_windowed()) {
        sDrawn++;
        return;
    }

    FillRect(surf, x - 2, y - 1, w + 4, 14, 0x00101018u);
    openmmo_font_draw_utf8(surf, x, y, sLabels[slot].utf8, 0x00F8F8F8u);

    sDrawn++;
}

/* The port's frame number for the frame about to be labelled. Taken from the
 * renderer rather than counted here so a report line and the polygon dump for
 * the same frame can be lined up. */
void openmmo_label_frame(uint64_t frame)
{
    if (sEnabled < 0) {
        ReadEnv();
    }

    sFrame = frame;
    sPlateN = 0;
}

/* The same frame number, and last frame's plates kept. */
void openmmo_label_hold(uint64_t frame)
{
    if (sEnabled < 0) {
        ReadEnv();
    }

    sFrame = frame;
}
