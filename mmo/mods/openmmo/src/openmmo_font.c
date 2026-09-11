/*
 * Which glyph is drawn for a code point the ROM font does not have one for,
 * and saying so out loud.
 */

#include <nitro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/charcode.h"

#include "charcode.h"
#include "font.h"
#include "pc_video.h"
#include "text.h"

#include "../../../include/charcode.h"
#include "../../../include/endpoint.h"

typedef char openmmo_font_charcode_width_check[sizeof(mmo_charcode) == sizeof(charcode_t) ? 1 : -1];

/* Distinct charcodes remembered before the table stops growing. A name is a
 * handful of code points and a run that substitutes more than 64 distinct ones
 * has a mapping problem, not a font problem; the overflow counter says so
 * without this file owning a heap. */
#define MISSING_SLOTS 64

static struct {
    charcode_t code;
    unsigned long count;
} sMissing[MISSING_SLOTS];

static unsigned sMissingUsed;
/* Every glyph the engine asked for, so a report of "0 substitutions" can be
 * told apart from a hook that is not on the path at all. */
static unsigned long sDrawn;
static unsigned long sMissingTotal;
static unsigned long sMissingOverflow;

/* Every distinct numGlyphs seen. The engine has four fonts (FONT_SYSTEM,
 * FONT_MESSAGE, FONT_SUBSCREEN, FONT_UNOWN) out of separate NARC members, so the
 * drawable range is per font and reporting only the last one asked would be
 * reporting whichever screen happened to draw last. */
#define GLYPH_COUNT_SLOTS 8
static u32 sGlyphCounts[GLYPH_COUNT_SLOTS];
static unsigned sGlyphCountsUsed;

static int sReport = -1;
static int sFatal = -1;
/* Both -1 until the environment is read, both read from the draw path rather
 * than a boot hook, because a mod source has no boot hook of its own. */
static int sOwnColors = -1;
static int sClobber = -1;

/*
 * A variable's value against the word that turns it on, rather than its name:
 * OPENMMO_FONT_REPORT only makes this file say more and is read in every build, while the
 * three below change what is drawn and go through openmmo_dev_env (endpoint.h).
 */
static int EnvFlag(const char *v, const char *want)
{
    return v != NULL && strcmp(v, want) == 0;
}

static void ReportAtExit(void)
{
    unsigned i;

    for (i = 0; i < sGlyphCountsUsed; i++) {
        printf("openmmo: font glyphs %lu\n", (unsigned long)sGlyphCounts[i]);
    }

    printf("openmmo: font %lu glyphs drawn, %lu substitutions over %u distinct code points\n",
        sDrawn, sMissingTotal, sMissingUsed);

    for (i = 0; i < sMissingUsed; i++) {
        printf("openmmo: font missing 0x%04X x%lu\n",
            (unsigned)sMissing[i].code, sMissing[i].count);
    }

    if (sMissingOverflow != 0) {
        printf("openmmo: font missing (%lu more, past %d distinct)\n",
            sMissingOverflow, MISSING_SLOTS);
    }
}

static void NoteGlyphCount(u32 numGlyphs)
{
    unsigned i;

    for (i = 0; i < sGlyphCountsUsed; i++) {
        if (sGlyphCounts[i] == numGlyphs) {
            return;
        }
    }

    if (sGlyphCountsUsed < GLYPH_COUNT_SLOTS) {
        sGlyphCounts[sGlyphCountsUsed++] = numGlyphs;
    }
}

static void NoteMissing(charcode_t c)
{
    unsigned i;

    sMissingTotal++;

    for (i = 0; i < sMissingUsed; i++) {
        if (sMissing[i].code == c) {
            sMissing[i].count++;
            return;
        }
    }

    if (sMissingUsed < MISSING_SLOTS) {
        sMissing[sMissingUsed].code = c;
        sMissing[sMissingUsed].count = 1;
        sMissingUsed++;
    } else {
        sMissingOverflow++;
    }
}

/* The glyph index to draw for charcode `c` in a font of `numGlyphs` glyphs.
 * Called from FontManager_TryLoadGlyph for every glyph the engine draws, so it
 * stays a lookup and an increment. */
charcode_t openmmo_glyph_index(charcode_t c, u32 numGlyphs)
{
    if (sReport < 0) {
        sReport = EnvFlag(getenv("OPENMMO_FONT_REPORT"), "1");
        sFatal = EnvFlag(openmmo_dev_env("OPENMMO_FONT"), "fatal");

        if (sReport) {
            atexit(ReportAtExit);
        }
    }

    NoteGlyphCount(numGlyphs);
    sDrawn++;

    /* The engine's own answer, minus the charcode-0 underflow. */
    if (c != CHAR_NONE && c <= numGlyphs) {
        return c - 1;
    }

    NoteMissing(c);

    if (sFatal) {
        fflush(stdout);
        fprintf(stderr,
            "openmmo: font has no glyph for charcode 0x%04X (%lu glyphs); "
            "OPENMMO_FONT=fatal\n",
            (unsigned)c, (unsigned long)numGlyphs);
        abort();
    }

    return CHAR_QUESTION - 1;
}

/* What a run substituted, for a caller inside the process that wants the number
 * without waiting for exit. */
unsigned long openmmo_font_missing_count(void)
{
    return sMissingTotal;
}

/* --- the colours a decompressed glyph comes back in ---------------------- */

/* Text_DecompressGlyph does not answer 0/1/2. */
void openmmo_glyph_colors(void)
{
    if (sOwnColors < 0) {
        sOwnColors = !EnvFlag(openmmo_dev_env("OPENMMO_GLYPH_COLORS"), "0");
    }

    if (sOwnColors) {
        Text_GenerateFontHalfRowLookupTable(1, 0, 2);
    }
}

/*
 * Leave the table the way a screen that printed in some other colour leaves it, under
 * OPENMMO_GLYPH_CLOBBER=1.
 */
void openmmo_glyph_clobber(void)
{
    if (sClobber < 0) {
        sClobber = EnvFlag(openmmo_dev_env("OPENMMO_GLYPH_CLOBBER"), "1");
    }

    if (sClobber) {
        Text_GenerateFontHalfRowLookupTable(3, 15, 4);
    }
}

/* --- host-surface blit --------------------------------------------------- */

extern BOOL Font_HasManager(enum Font font);

#define FONT_DRAW_MAX 80
/* The ROM font's cell. FONT_SYSTEM's maxLetterHeight, measured; host-surface
 * text uses this rather than a second 5x7. */
#define OPENMMO_FONT_CELL_H 16
#define FONT_SHADOW         0x00303038u

int openmmo_font_ready(void)
{
    return Font_HasManager(FONT_SYSTEM);
}

int openmmo_font_cell_h(void)
{
    return OPENMMO_FONT_CELL_H;
}

/* The engine decompresses a glyph into a 16x16 cell of 8x8 4bpp tiles: the tile
 * at (tx,ty) starts at ty*0x40 + tx*0x20, a row is four bytes, and pixel j of a
 * row is nibble j (font_manager.c:165 for the layout, bg_window.c:2023 for the
 * read). Value 0 is transparent, 1 the letter, 2 its shadow. */
static uint32_t GlyphPixel(const TextGlyph *g, int x, int y)
{
    const u8 *tile = g->gfx + (y / 8) * 0x40 + (x / 8) * 0x20;
    u32 row = *(const u32 *)(tile + (y % 8) * 4);

    return (row >> ((x % 8) * 4)) & 0xF;
}

static void openmmo_font_blit(uint32_t *surf, const TextGlyph *g, int x, int y,
                              uint32_t fg)
{
    int gx, gy;

    if (surf == NULL || g == NULL) {
        return;
    }

    for (gy = 0; gy < g->height && gy < OPENMMO_FONT_CELL_H; gy++) {
        for (gx = 0; gx < g->width && gx < OPENMMO_FONT_CELL_H; gx++) {
            uint32_t v = GlyphPixel(g, gx, gy);
            int px = x + gx;
            int py = y + gy;

            if (v == 0 || px < 0 || px >= PC_VIDEO_WIDTH
                || py < 0 || py >= PC_VIDEO_HEIGHT) {
                continue;
            }
            surf[py * PC_VIDEO_WIDTH + px] = (v == 1) ? fg : FONT_SHADOW;
        }
    }
}

static int DrawCodes(uint32_t *surf, int x, int y, const charcode_t *s, int n,
                     uint32_t fg)
{
    int i, cx = x;

    if (surf == NULL || s == NULL || n <= 0 || !openmmo_font_ready()) {
        return x;
    }
    openmmo_glyph_colors();
    for (i = 0; i < n; i++) {
        const TextGlyph *g = Font_TryLoadGlyph(FONT_SYSTEM, s[i]);

        if (cx + g->width >= PC_VIDEO_WIDTH) {
            break;
        }
        openmmo_font_blit(surf, g, cx, y, fg);
        cx += g->width;
    }
    return cx;
}

static int WidthCodes(const charcode_t *s, int n)
{
    int i, w = 0;

    if (s == NULL || n <= 0 || !openmmo_font_ready()) {
        return 0;
    }
    openmmo_glyph_colors();
    for (i = 0; i < n; i++) {
        w += Font_TryLoadGlyph(FONT_SYSTEM, s[i])->width;
    }
    return w;
}

int openmmo_font_draw_utf8(uint32_t *surf, int x, int y, const char *s,
                           uint32_t fg)
{
    mmo_charcode text[FONT_DRAW_MAX + 1];
    mmo_charcode_result r;

    if (s == NULL) {
        return x;
    }
    r = mmo_utf8_to_charcode(s, text, FONT_DRAW_MAX + 1);
    return DrawCodes(surf, x, y, (const charcode_t *)text, (int)r.written, fg);
}

int openmmo_font_width_utf8(const char *s)
{
    mmo_charcode text[FONT_DRAW_MAX + 1];
    mmo_charcode_result r;

    if (s == NULL) {
        return 0;
    }
    r = mmo_utf8_to_charcode(s, text, FONT_DRAW_MAX + 1);
    return WidthCodes((const charcode_t *)text, (int)r.written);
}

/* Copy FONT_SYSTEM's cells for the host panel. gfx is max * 128 bytes of the
 * engine's own 16x16 4bpp layout; advance[i] is glyph i's width. Charcode
 * 1 is index 0. Returns 1 when the manager is up and `max` cells were copied. */
int openmmo_font_export(uint8_t *gfx, uint8_t *advance, unsigned max,
                        uint32_t *out_n)
{
    unsigned i;

    if (gfx == NULL || advance == NULL || max == 0 || !openmmo_font_ready())
        return 0;
    openmmo_glyph_colors();
    for (i = 0; i < max; i++) {
        const TextGlyph *g = Font_TryLoadGlyph(FONT_SYSTEM, (charcode_t)(i + 1));

        memcpy(gfx + i * 128u, g->gfx, 128);
        advance[i] = g->width;
    }
    if (out_n != NULL)
        *out_n = max;
    return 1;
}
