/* Paint the chat window, headless. */

#include <nitro.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "constants/charcode.h"

#include "charcode.h"
#include "font.h"
#include "render_text.h"

#include "pc_video.h"

#include "../../../include/charcode.h"
#include "../../../include/chatwin.h"
#include "../../../include/osk.h"

typedef char openmmo_chat_charcode_width_check[sizeof(mmo_charcode) == sizeof(charcode_t) ? 1 : -1];

extern BOOL Font_HasManager(enum Font font);

/* Which palette index a decompressed glyph calls the letter is the last
 * text printer's choice, not a constant. openmmo_font.c holds the reason
 * this has to be set before reading one from outside a printer. */
extern void openmmo_glyph_colors(void);
extern int openmmo_font_draw_latin1(uint32_t *surf, int x, int y, const char *s,
                                    uint32_t fg);

#define LINE_H 14
#define PAD_X  4

static void FillRect(uint32_t *surf, int x, int y, int w, int h, uint32_t color)
{
    int px, py;

    for (py = y; py < y + h; py++) {
        if (py < 0 || py >= PC_VIDEO_HEIGHT)
            continue;
        for (px = x; px < x + w; px++) {
            if (px < 0 || px >= PC_VIDEO_WIDTH)
                continue;
            surf[py * PC_VIDEO_WIDTH + px] = color;
        }
    }
}

static int DrawLatin1(uint32_t *surf, int x, int y, const char *s, uint32_t fg)
{
    if (s == NULL || !Font_HasManager(FONT_SYSTEM))
        return x;
    return openmmo_font_draw_latin1(surf, x, y, s, fg);
}

void openmmo_chat_draw(const mmo_chatwin *w, const mmo_chatwin_line *log, int n,
                       const osk_state *osk)
{
    uint32_t *surf;
    mmo_chatwin_row row;
    int i, vis, y, panel_y, upper;

    (void)osk;
    if (w == NULL || !mmo_chatwin_visible(w))
        return;

    /* Walking, the log takes the lower screen; composing, it moves to the
     * upper strip and draw_osk owns the lower one. */
    upper = mmo_chatwin_composing(w);
    surf = pc_video_surface(upper ? pc_video_upper_engine()
                                  : (pc_video_upper_engine() == PC_VIDEO_MAIN
                                         ? PC_VIDEO_SUB
                                         : PC_VIDEO_MAIN));
    if (surf == NULL)
        return;

    vis = mmo_chatwin_visible_count(w, n);
    panel_y = PC_VIDEO_HEIGHT - vis * LINE_H - 16;
    if (panel_y < 0)
        panel_y = 0;
    FillRect(surf, 0, panel_y, PC_VIDEO_WIDTH, PC_VIDEO_HEIGHT - panel_y,
             0x00101820u);

    y = panel_y + 2;
    for (i = 0; i < vis; i++) {
        if (!mmo_chatwin_get_row(w, log, n, i, &row))
            continue;
        DrawLatin1(surf, PAD_X, y, row.text, mmo_chatwin_color(row.type));
        y += LINE_H;
    }

    DrawLatin1(surf, PAD_X, PC_VIDEO_HEIGHT - 14,
               upper ? "ENTER SENDS  START CLOSES" : "START TO TALK",
               0x00A0A0A8u);
}
