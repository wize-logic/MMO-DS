/*
 * The Pokegear's popup: HeartGold's touch list menu (the map's "Fly
 * / Quit", the skins' "change / QUIT") on this engine's ListMenu, keys and pen both.
 */

#include <string.h>

#include "openmmo_pokegear.h"

#include "constants/graphics.h"
#include "constants/menu.h"
#include "font.h"
#include "heap.h"
#include "list_menu.h"
#include "render_window.h"
#include "string_list.h"
#include "system.h"
#include "touch_screen.h"

#define PGMENU_ROWS_MAX 4
#define PGMENU_FRAME_TILES 9

struct PokegearMenu {
    PokegearAppData *app;
    enum HeapID heapID;
    Window win;
    StringList *choices;
    String *rows[PGMENU_ROWS_MAX];
    ListMenu *menu;
    u8 bgLayer, x, y, width, n, palette;
    u16 baseTile;
    int lastTouch;
};

PokegearMenu *PokegearMenu_Open(PokegearAppData *app, u8 bgLayer, u8 x, u8 y,
                                u8 width, u16 baseTile, u8 palette,
                                MessageLoader *msg, const u16 *entries, int n,
                                enum HeapID heapID)
{
    PokegearMenu *m;
    ListMenuTemplate tmpl;
    int i;

    if (n < 1 || n > PGMENU_ROWS_MAX || msg == NULL)
        return NULL;
    m = Heap_Alloc(heapID, sizeof *m);
    memset(m, 0, sizeof *m);
    m->app = app;
    m->heapID = heapID;
    m->bgLayer = bgLayer;
    m->x = x;
    m->y = y;
    m->n = (u8)n;
    m->palette = palette;
    m->baseTile = baseTile;
    m->choices = StringList_New((u32)n, heapID);
    for (i = 0; i < n; i++) {
        u32 w;

        m->rows[i] = MessageLoader_GetNewString(msg, entries[i]);
        StringList_AddFromString(m->choices, m->rows[i], (u32)i);
        w = Font_CalcStringWidth(FONT_SYSTEM, m->rows[i], 0);
        if (width == 0 && (w + 16 + 7) / 8 > m->width)
            m->width = (u8)((w + 16 + 7) / 8);
    }
    if (width != 0)
        m->width = width;
    if (m->width < 4)
        m->width = 4;

    /* The frame's tiles and palette, then the text's palette beside it; both
     * copied into the device's palette buffer so a card's fade keeps them. */
    LoadStandardWindowGraphics(app->bgConfig, bgLayer, baseTile, palette,
                               STANDARD_WINDOW_SYSTEM, heapID);
    Font_LoadTextPalette(PAL_LOAD_MAIN_BG, PLTT_OFFSET(palette + 1), heapID);
    if (app->plttData != NULL)
        PaletteData_LoadBufferFromHardware(app->plttData, PLTTBUF_MAIN_BG,
                                           PLTT_DEST(palette), PALETTE_SIZE_BYTES * 2);
    Window_Add(app->bgConfig, &m->win, bgLayer, x, y, m->width, (u8)(n * 2),
               (u8)(palette + 1), (u16)(baseTile + PGMENU_FRAME_TILES));
    Window_FillTilemap(&m->win, 15);
    Window_DrawStandardFrame(&m->win, 1, baseTile, palette);

    memset(&tmpl, 0, sizeof tmpl);
    tmpl.choices = m->choices;
    tmpl.window = &m->win;
    tmpl.count = (u16)n;
    tmpl.maxDisplay = (u16)n;
    tmpl.textXOffset = 12;
    tmpl.cursorXOffset = 2;
    tmpl.textColorFg = 1;
    tmpl.textColorBg = 15;
    tmpl.textColorShadow = 2;
    tmpl.fontID = FONT_SYSTEM;
    tmpl.cursorType = 0;
    m->menu = ListMenu_New(&tmpl, 0, 0, heapID);
    Window_CopyToVRAM(&m->win);
    Bg_ScheduleTilemapTransfer(app->bgConfig, bgLayer);
    return m;
}

int PokegearMenu_Input(PokegearMenu *m)
{
    u32 choice;
    int i;

    if (m == NULL || m->menu == NULL)
        return -2;
    /* The pen: a row is sixteen pixels under the frame's top edge. */
    if (gSystem.touchPressed) {
        int left = m->x * 8, top = m->y * 8;
        int right = left + m->width * 8;

        for (i = 0; i < m->n; i++) {
            int rt = top + i * 16, rb = rt + 16;

            if ((int)gSystem.touchX >= left && (int)gSystem.touchX < right
                && (int)gSystem.touchY >= rt && (int)gSystem.touchY < rb) {
                m->lastTouch = 1;
                PokegearApp_PlaySE(PG_SE_DECIDE);
                return i;
            }
        }
    }
    choice = ListMenu_ProcessInput(m->menu);
    if (choice == (u32)MENU_NOTHING_CHOSEN)
        return -1;
    m->lastTouch = 0;
    if (choice == (u32)MENU_CANCEL) {
        PokegearApp_PlaySE(PG_SE_CANCEL);
        return -2;
    }
    PokegearApp_PlaySE(PG_SE_DECIDE);
    return (int)choice;
}

int PokegearMenu_LastInputWasTouch(PokegearMenu *m)
{
    return m != NULL && m->lastTouch;
}

void PokegearMenu_Close(PokegearMenu *m)
{
    int i;

    if (m == NULL)
        return;
    if (m->menu != NULL)
        ListMenu_Free(m->menu, NULL, NULL);
    Window_EraseStandardFrame(&m->win, 1);
    Window_ClearAndCopyToVRAM(&m->win);
    Window_Remove(&m->win);
    Bg_ScheduleTilemapTransfer(m->app->bgConfig, m->bgLayer);
    for (i = 0; i < m->n; i++)
        if (m->rows[i] != NULL)
            String_Free(m->rows[i]);
    if (m->choices != NULL)
        StringList_Free(m->choices);
    Heap_Free(m);
}

/* The row the cursor is on. */
int PokegearMenu_Cursor(PokegearMenu *m)
{
    u16 listPos = 0, cursorPos = 0;

    if (m == NULL || m->menu == NULL)
        return 0;
    ListMenu_GetListAndCursorPos(m->menu, &listPos, &cursorPos);
    return listPos + cursorPos;
}

u8 PokegearMenu_X(PokegearMenu *m) { return m != NULL ? m->x : 0; }
u8 PokegearMenu_Y(PokegearMenu *m) { return m != NULL ? m->y : 0; }
u8 PokegearMenu_Width(PokegearMenu *m) { return m != NULL ? m->width : 0; }
