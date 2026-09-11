/* The global trade link, drawn with the engine's own windows. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bg_window.h"
#include "constants/heap.h"
#include "constants/menu.h"
#include "field/field_system.h"
#include "field_message.h"
#include "field_system.h"
#include "field_task.h"
#include "font.h"
#include "heap.h"
#include "list_menu.h"
#include "render_window.h"
#include "save_player.h"
#include "sound_playback.h"
#include "string_gf.h"
#include "string_list.h"
#include "text.h"

#include "../../../include/charcode.h"
#include "../../../include/endpoint.h"
#include "../../../include/client.h"
#include "../../../include/game.h"
#include "../../../include/hud_channel.h"
#include "../../../include/idmap.h"

#include "message_util.h"

#define GTL_HEAP       HEAP_ID_FIELD2
#define GTL_EXTRA      4 /* next page, previous page, kind, close */
#define GTL_MENU_ROWS  (MMO_GTL_PAGE_ROWS + GTL_EXTRA)
#define GTL_FRAME_TILE (1024 - (18 + 12) - 9)
#define GTL_FRAME_PAL  11
#define GTL_LIST_TILE  1

enum {
    GTL_ST_PAINT = 0,
    GTL_ST_OPENING,   /* waiting for the 0xDC */
    GTL_ST_ASKING,    /* waiting for the page that answers our request */
    GTL_ST_BROWSE,
    GTL_ST_CONFIRM,   /* one more A buys the held row */
};

typedef struct {
    Window listWin;
    Window msgWin;
    StringList *choices;
    ListMenu *menu;
    String *rowStr[GTL_MENU_ROWS];
    String *msgStr;
    int rowCount;
    int listAdded;
    int msgAdded;
    u8 printer;
    int kind;          /* MMO_GTL_KIND_* being browsed */
    int page;
    int want_request;  /* the request id the next page must echo */
    s64 held;          /* the listing a second A would buy or take back */
    int held_own;
} GtlScreen;

extern int openmmo_hud_windowed(void);
extern const struct openmmo_hud_gtl_ask *openmmo_hud_gtl_args(void);
/* The listing a row verb named, out of the ring slot it rode. */
extern s64 openmmo_hud_cmd_row_id(int32_t arg);

static openmmo_client *s_client;
static GtlScreen *s_live;
static int s_opened;
static int s_pending;

/* The window's GTL frame asked for a page before the session was open; the
 * ask is held until the 0xDC lands. -1 = nothing held. */
static int s_want_kind = -1;
static int s_want_page;
static int s_want_sort;
static mmo_gtl_search s_want_filter;
static int s_open_sent;

static String *utf8_string(enum HeapID heap, const char *s)
{
    mmo_charcode buf[64];
    String *out = String_Init(64, heap);

    if (out == NULL)
        return NULL;
    mmo_utf8_to_charcode(s != NULL ? s : "", buf, 64);
    String_CopyChars(out, (const charcode_t *)buf);
    return out;
}

static void drop_menu(GtlScreen *g)
{
    int i;

    if (g->menu != NULL) {
        ListMenu_Free(g->menu, NULL, NULL);
        g->menu = NULL;
    }
    if (g->choices != NULL) {
        StringList_Free(g->choices);
        g->choices = NULL;
    }
    for (i = 0; i < GTL_MENU_ROWS; i++) {
        if (g->rowStr[i] != NULL)
            String_Free(g->rowStr[i]);
        g->rowStr[i] = NULL;
    }
    g->rowCount = 0;
}

static void gtl_free(GtlScreen *g)
{
    if (g == NULL)
        return;
    drop_menu(g);
    if (g->msgStr != NULL) {
        String_Free(g->msgStr);
        g->msgStr = NULL;
    }
    if (g->listAdded) {
        Window_EraseStandardFrame(&g->listWin, TRUE);
        Window_ClearAndCopyToVRAM(&g->listWin);
        Window_Remove(&g->listWin);
        g->listAdded = 0;
    }
    if (g->msgAdded) {
        Window_EraseMessageBox(&g->msgWin, 0);
        Window_Remove(&g->msgWin);
        g->msgAdded = 0;
    }
    if (s_live == g)
        s_live = NULL;
    Heap_Free(g);
}

static void say(FieldSystem *fs, GtlScreen *g, const char *text)
{
    const Options *options = SaveData_GetOptions(fs->saveData);

    if (g->msgStr != NULL)
        String_Free(g->msgStr);
    g->msgStr = utf8_string(GTL_HEAP, text);
    if (g->msgStr == NULL)
        return;
    if (!g->msgAdded) {
        FieldMessage_AddWindow(fs->bgConfig, &g->msgWin, BG_LAYER_MAIN_3);
        FieldMessage_DrawWindow(&g->msgWin, options);
        g->msgAdded = 1;
    }
    g->printer = FieldMessage_Print(&g->msgWin, g->msgStr, options, 1);
}

static const char *kind_name(int kind)
{
    return kind == MMO_GTL_KIND_ITEM  ? "items"
         : kind == MMO_GTL_KIND_OWN   ? "yours"
                                      : "monsters";
}

/* Ask the shelf for the page this screen is on; the store's request id
 * moving is what says the answer arrived. */
static int ask(GtlScreen *g)
{
    const openmmo_gtl *store = openmmo_client_gtl(s_client);

    g->want_request = (store->next_request + 1) & 0x7F;
    return openmmo_client_gtl_search(s_client, g->kind, MMO_GTL_SORT_NEWEST, g->page, NULL) == 0;
}

static int paint_rows(FieldSystem *fs, GtlScreen *g)
{
    const openmmo_gtl *store = openmmo_client_gtl(s_client);
    ListMenuTemplate tmpl;
    char line[64];
    int i, n, rows;

    drop_menu(g);
    rows = store->count;
    n = 0;
    g->choices = StringList_New((u32)(rows + GTL_EXTRA), GTL_HEAP);
    if (g->choices == NULL)
        return 0;
    for (i = 0; i < rows; i++) {
        const openmmo_gtl_row *r = &store->row[i];

        if (r->have_mon) {
            char monstr[40];

            if (r->mon.nickname[0] != '\0')
                snprintf(monstr, sizeof monstr, "%s", r->mon.nickname);
            else
                snprintf(monstr, sizeof monstr, "No.%u", r->mon.dex_id);
            snprintf(line, sizeof line, "%s Lv%d  $%d",
                     monstr, r->mon.level, (int)r->price);
        } else {
            snprintf(line, sizeof line, "Item %u x%d  $%d",
                     r->item_id, r->quantity, (int)r->price);
        }
        g->rowStr[n] = utf8_string(GTL_HEAP, line);
        if (g->rowStr[n] == NULL)
            return 0;
        StringList_AddFromString(g->choices, g->rowStr[n], (u32)i);
        n++;
    }
    snprintf(line, sizeof line, "Next page (%d of %d shown)",
             rows, (int)store->total);
    g->rowStr[n] = utf8_string(GTL_HEAP, line);
    StringList_AddFromString(g->choices, g->rowStr[n], (u32)(MMO_GTL_PAGE_ROWS + 0));
    n++;
    g->rowStr[n] = utf8_string(GTL_HEAP, "Previous page");
    StringList_AddFromString(g->choices, g->rowStr[n], (u32)(MMO_GTL_PAGE_ROWS + 1));
    n++;
    snprintf(line, sizeof line, "Showing %s; switch", kind_name(g->kind));
    g->rowStr[n] = utf8_string(GTL_HEAP, line);
    StringList_AddFromString(g->choices, g->rowStr[n], (u32)(MMO_GTL_PAGE_ROWS + 2));
    n++;
    g->rowStr[n] = utf8_string(GTL_HEAP, "Close");
    StringList_AddFromString(g->choices, g->rowStr[n], (u32)(MMO_GTL_PAGE_ROWS + 3));
    n++;
    g->rowCount = n;

    if (!g->listAdded) {
        Window_Add(fs->bgConfig, &g->listWin, BG_LAYER_MAIN_3,
                   1, 1, 26, 22, 13, GTL_LIST_TILE);
        g->listAdded = 1;
        LoadStandardWindowGraphics(fs->bgConfig, BG_LAYER_MAIN_3,
                                   GTL_FRAME_TILE, GTL_FRAME_PAL,
                                   STANDARD_WINDOW_SYSTEM, GTL_HEAP);
        Window_DrawStandardFrame(&g->listWin, 1, GTL_FRAME_TILE, GTL_FRAME_PAL);
    }
    Window_FillTilemap(&g->listWin, 15);

    memset(&tmpl, 0, sizeof tmpl);
    tmpl.choices = g->choices;
    tmpl.window = &g->listWin;
    tmpl.count = (u16)n;
    tmpl.maxDisplay = (u16)(n < 11 ? n : 11);
    tmpl.textXOffset = 8;
    tmpl.textColorFg = 1;
    tmpl.textColorBg = 15;
    tmpl.textColorShadow = 2;
    tmpl.fontID = FONT_SYSTEM;
    tmpl.cursorType = 0;
    g->menu = ListMenu_New(&tmpl, 0, 0, GTL_HEAP);
    if (g->menu == NULL)
        return 0;
    Window_CopyToVRAM(&g->listWin);
    return 1;
}

static BOOL gtl_task(FieldTask *task)
{
    FieldSystem *fs = FieldTask_GetFieldSystem(task);
    GtlScreen *g = FieldTask_GetEnv(task);
    const openmmo_gtl *store = openmmo_client_gtl(s_client);
    u32 choice;

    switch (task->state) {
    case GTL_ST_PAINT:
        say(fs, g, "Reaching the Global Trade Link...");
        if (!store->session_open) {
            if (openmmo_client_gtl_open(s_client) != 0) {
                printf("openmmo: gtl open failed\n");
                gtl_free(g);
                return TRUE;
            }
            task->state = GTL_ST_OPENING;
            break;
        }
        if (!ask(g)) {
            gtl_free(g);
            return TRUE;
        }
        task->state = GTL_ST_ASKING;
        break;

    case GTL_ST_OPENING:
        if (store->session_open) {
            if (!ask(g)) {
                gtl_free(g);
                return TRUE;
            }
            task->state = GTL_ST_ASKING;
        }
        break;

    case GTL_ST_ASKING:
        if (store->request_id != g->want_request)
            break;
        if (!paint_rows(fs, g)) {
            printf("openmmo: gtl screen would not allocate\n");
            gtl_free(g);
            return TRUE;
        }
        say(fs, g, g->kind == MMO_GTL_KIND_OWN
                       ? "A takes a listing back. B closes."
                       : "A picks, A again buys. B closes.");
        task->state = GTL_ST_BROWSE;
        break;

    case GTL_ST_BROWSE:
        choice = ListMenu_ProcessInput(g->menu);
        if (choice == (u32)MENU_NOTHING_CHOSEN)
            break;
        Sound_PlayEffect(SE_CONFIRM_sseq_3);
        if (choice == (u32)MENU_CANCEL) {
            printf("openmmo: gtl closed\n");
            gtl_free(g);
            return TRUE;
        }
        if (choice == (u32)(MMO_GTL_PAGE_ROWS + 0)) {
            if ((g->page + 1) * MMO_GTL_PAGE_ROWS < (int)store->total)
                g->page++;
            if (ask(g))
                task->state = GTL_ST_ASKING;
            break;
        }
        if (choice == (u32)(MMO_GTL_PAGE_ROWS + 1)) {
            if (g->page > 0)
                g->page--;
            if (ask(g))
                task->state = GTL_ST_ASKING;
            break;
        }
        if (choice == (u32)(MMO_GTL_PAGE_ROWS + 2)) {
            g->kind = g->kind == MMO_GTL_KIND_POKEMON ? MMO_GTL_KIND_ITEM
                      : g->kind == MMO_GTL_KIND_ITEM  ? MMO_GTL_KIND_OWN
                                                      : MMO_GTL_KIND_POKEMON;
            g->page = 0;
            if (ask(g))
                task->state = GTL_ST_ASKING;
            break;
        }
        if (choice == (u32)(MMO_GTL_PAGE_ROWS + 3)) {
            printf("openmmo: gtl closed\n");
            gtl_free(g);
            return TRUE;
        }
        if ((int)choice < store->count) {
            const openmmo_gtl_row *r = &store->row[choice];
            char line[64];

            g->held = r->listing_id;
            g->held_own = g->kind == MMO_GTL_KIND_OWN;
            if (g->held_own)
                snprintf(line, sizeof line,
                         "A again takes #%lld back. B keeps it up.",
                         (long long)g->held);
            else
                snprintf(line, sizeof line, "A again buys it for $%d.",
                         (int)r->price);
            say(fs, g, line);
            task->state = GTL_ST_CONFIRM;
        }
        break;

    case GTL_ST_CONFIRM:
        choice = ListMenu_ProcessInput(g->menu);
        if (choice == (u32)MENU_NOTHING_CHOSEN)
            break;
        Sound_PlayEffect(SE_CONFIRM_sseq_3);
        if (choice == (u32)MENU_CANCEL || (s64)choice != g->held) {
            /* Anything but a second A on the same row backs out. */
            say(fs, g, "A picks, A again buys. B closes.");
            task->state = GTL_ST_BROWSE;
            break;
        }
        if (g->held_own) {
            printf("openmmo: gtl takes back #%lld\n", (long long)g->held);
            openmmo_client_gtl_cancel(s_client, g->held);
        } else {
            printf("openmmo: gtl buys #%lld\n", (long long)g->held);
            openmmo_client_gtl_buy(s_client, g->held, 1);
        }
        /* The answer is a notice plus resends; the page is re-asked so the
         * shelf shown is the shelf as it now stands. */
        if (ask(g))
            task->state = GTL_ST_ASKING;
        else
            task->state = GTL_ST_BROWSE;
        break;
    }
    return FALSE;
}

void openmmo_gtl_attach(openmmo_client *c)
{
    const char *env = openmmo_dev_env("OPENMMO_GTL");

    s_client = c;
    s_live = NULL;
    s_opened = 0;
    /* OPENMMO_GTL=1 opens the screen once the field is idle, so a headless
     * boot can measure it without a window pressing the button. */
    s_pending = env != NULL && env[0] != '\0' && env[0] != '0';
    if (s_pending)
        printf("openmmo: gtl armed\n");
}

void openmmo_gtl_mark_pending(void)
{
    s_pending = 1;
}

/* A species name typed into the window's search box, resolved against the
 * engine's own name bank: exact case-insensitive first, then the first
 * prefix hit (the official client's autocomplete matches startsWith). Answers the server
 * dex id, or 0 for a name no species carries. */
static void string_lower_utf8(const String *src, char *dst, size_t cap)
{
    size_t i;

    if (dst == NULL || cap == 0)
        return;
    dst[0] = '\0';
    if (src == NULL)
        return;
    mmo_charcode_to_utf8(String_GetData(src), dst, cap);
    /* Only the ASCII letters fold, which is the whole of every species name;
     * a byte above 0x7F is part of a sequence and is left exactly as it is. */
    for (i = 0; dst[i] != '\0'; i++) {
        if (dst[i] >= 'A' && dst[i] <= 'Z')
            dst[i] = (char)(dst[i] + 32);
    }
}

static u16 species_by_name(const char *text)
{
    char want[24], have[24];
    u16 engine_species, hit = 0;
    size_t i, want_len;

    if (text == NULL || text[0] == '\0')
        return 0;
    for (i = 0; i + 1 < sizeof want && text[i] != '\0'; i++)
        want[i] = (char)((text[i] >= 'A' && text[i] <= 'Z')
                             ? text[i] + 32 : text[i]);
    want[i] = '\0';
    want_len = i;
    for (engine_species = 1; engine_species <= MMO_SPECIES_MAX;
         engine_species++) {
        String *str = MessageUtil_SpeciesName(engine_species, HEAP_ID_SYSTEM);
        int exact;

        if (str == NULL)
            continue;
        string_lower_utf8(str, have, sizeof have);
        String_Free(str);
        exact = strcmp(have, want) == 0;
        if (exact || (hit == 0 && strncmp(have, want, want_len) == 0)) {
            const char *why;
            u16 dex = mmo_id_species_to_server(engine_species, &why);

            if (dex != 0)
                hit = dex;
            if (exact)
                break;
        }
    }
    return hit;
}

/* One command from the window's GTL frame (OPENMMO_HUD_CMD_GTL). */
void openmmo_gtl_window_cmd(unsigned arg)
{
    const openmmo_gtl *g;
    const struct openmmo_hud_gtl_ask *wide = openmmo_hud_gtl_args();
    unsigned verb = arg & 0xFFu;
    int row = (int)((arg >> 8) & 0xFFu);
    s64 id = openmmo_hud_cmd_row_id((int32_t)arg);
    s32 price = wide != NULL && wide->price > 0 ? wide->price : 0;
    int qty = wide != NULL && wide->qty > 0 ? wide->qty : 1;

    if (s_client == NULL)
        return;
    g = openmmo_client_gtl(s_client);
    switch (verb) {
    case OPENMMO_HUD_GTL_ASK: {
        s_want_kind = (int)((arg >> 8) & 0xFu);
        s_want_sort = (int)((arg >> 12) & 0xFu);
        s_want_page = (int)((arg >> 16) & 0xFFu);
        memset(&s_want_filter, 0, sizeof s_want_filter);
        s_want_filter.min_level = -1;
        s_want_filter.max_level = -1;
        s_want_filter.shiny = -1;
        s_want_filter.nature = -1;
        s_want_filter.min_price = -1;
        s_want_filter.max_price = -1;
        if (wide != NULL) {
            struct openmmo_hud_gtl_ask a;

            memcpy(&a, wide, sizeof a);
            a.species[sizeof a.species - 1] = '\0';
            s_want_filter.species = species_by_name(a.species);
            if (a.species[0] != '\0' && s_want_filter.species == 0)
                printf("openmmo: gtl species '%s' not found\n", a.species);
            s_want_filter.min_level = a.min_level;
            s_want_filter.max_level = a.max_level;
            s_want_filter.shiny = a.shiny;
            s_want_filter.nature = a.nature;
            s_want_filter.min_price = a.min_price;
            s_want_filter.max_price = a.max_price;
        }
        printf("openmmo: gtl window asks kind %d sort %d page %d\n",
               s_want_kind, s_want_sort, s_want_page);
        return;
    }
    case OPENMMO_HUD_GTL_BUY:
        if (id == 0) {
            printf("openmmo: gtl row %d named no listing\n", row);
            return;
        }
        printf("openmmo: gtl window buys %d of #%lld\n", qty, (long long)id);
        openmmo_client_gtl_buy(s_client, id, qty);
        break;
    case OPENMMO_HUD_GTL_BACK:
        if (id == 0) {
            printf("openmmo: gtl row %d named no listing\n", row);
            return;
        }
        printf("openmmo: gtl window takes back #%lld\n", (long long)id);
        openmmo_client_gtl_cancel(s_client, id);
        break;
    case OPENMMO_HUD_GTL_SELL: {
        const openmmo_party *p = openmmo_client_party(s_client);

        if (row < 0 || row >= p->count) {
            printf("openmmo: gtl sell: no party slot %d\n", row);
            return;
        }
        if (openmmo_client_gtl_list_mon(s_client, p->mon[row].id, price) != 0)
            printf("openmmo: gtl sell at $%d refused\n", (int)price);
        else
            printf("openmmo: gtl sells slot %d for $%d\n", row, (int)price);
        return;
    }
    case OPENMMO_HUD_GTL_SELL_ITEM: {
        const openmmo_bag *b = openmmo_client_bag(s_client);

        if (b == NULL || !b->valid || row < 0 || row >= b->count) {
            printf("openmmo: gtl sell: no bag row %d\n", row);
            return;
        }
        if (openmmo_client_gtl_list_item(s_client, b->stack[row].item_id,
                                         qty, price) != 0)
            printf("openmmo: gtl sell item %u refused\n",
                   (unsigned)b->stack[row].item_id);
        else
            printf("openmmo: gtl sells item %u x%d for $%d\n",
                   (unsigned)b->stack[row].item_id, qty, (int)price);
        return;
    }
    case OPENMMO_HUD_GTL_CLAIM: {
        s64 ids[MMO_GTL_PAGE_ROWS];
        int i, n = 0;

        if (row == 255) {
            for (i = 0; i < g->count && n < MMO_GTL_PAGE_ROWS; i++)
                if (g->row[i].own_unclaimed > 0)
                    ids[n++] = g->row[i].listing_id;
        } else if (id != 0)
            ids[n++] = id;
        if (n < 1) {
            printf("openmmo: gtl claim with nothing unclaimed\n");
            return;
        }
        printf("openmmo: gtl window claims %d listing(s)\n", n);
        openmmo_client_gtl_claim(s_client, ids, n);
        break;
    }
    case OPENMMO_HUD_GTL_REPRICE:
        if (id == 0) {
            printf("openmmo: gtl row %d named no listing\n", row);
            return;
        }
        printf("openmmo: gtl window reprices #%lld to $%d\n", (long long)id,
               (int)price);
        openmmo_client_gtl_price(s_client, id, price);
        break;
    case OPENMMO_HUD_GTL_LOG:
        if (openmmo_client_gtl_log(s_client) != 0)
            printf("openmmo: gtl log ask refused\n");
        return;
    case OPENMMO_HUD_GTL_MARKET: {
        /* The quote strip names an item, not a listing: the server picks the
         * cheapest asks itself. A budget of nothing falls back to the strip's
         * own quote for that item, wherever it now sits. */
        u16 item = (u16)id;
        s32 budget = price;

        if (item == 0) {
            printf("openmmo: gtl quote %d named no item\n", row);
            return;
        }
        if (budget <= 0) {
            int q;

            for (q = 0; q < g->quote_count; q++)
                if (g->quote[q].item_id == item) {
                    budget = g->quote[q].price * qty;
                    break;
                }
        }
        printf("openmmo: gtl window market-buys %d of item %u\n", qty,
               (unsigned)item);
        openmmo_client_gtl_market(s_client, item, qty, budget);
        break;
    }
    default:
        printf("openmmo: gtl verb %u unknown\n", verb);
        return;
    }
    /* The mutating verbs re-ask the page the window is showing, so the
     * shelf drawn is the shelf as it now stands. */
    s_want_kind = g->kind;
    s_want_sort = 0;
    s_want_page = g->page;
    memset(&s_want_filter, 0, sizeof s_want_filter);
    s_want_filter.min_level = -1;
    s_want_filter.max_level = -1;
    s_want_filter.shiny = -1;
    s_want_filter.nature = -1;
    s_want_filter.min_price = -1;
    s_want_filter.max_price = -1;
}

/* Send what the window asked for, once the session can carry it. Called
 * every frame the client is pumped. */
void openmmo_gtl_tick(void)
{
    const openmmo_gtl *g;

    if (s_client == NULL || s_want_kind < 0)
        return;
    g = openmmo_client_gtl(s_client);
    if (!g->session_open) {
        if (!s_open_sent && openmmo_client_gtl_open(s_client) == 0)
            s_open_sent = 1;
        return;
    }
    if (openmmo_client_gtl_search(s_client, s_want_kind, s_want_sort,
                                  s_want_page, &s_want_filter) != 0)
        printf("openmmo: gtl window search failed\n");
    s_want_kind = -1;
    s_open_sent = 0;
}

/* The chat line's own three verbs. Returns 1 when the line was one of
 * them, handled here and owed nothing upstream. */
int openmmo_gtl_local_command(const char *line)
{
    if (line == NULL || s_client == NULL)
        return 0;
    if (strcmp(line, "/gtl") == 0) {
        if (openmmo_hud_windowed()) {
            /* The window has its own GTL frame; the engine list is the
             * windowless guest's face and would draw under the panel. */
            printf("openmmo: gtl is the window's frame here\n");
            return 1;
        }
        s_pending = 1;
        printf("openmmo: gtl asked for\n");
        return 1;
    }
    if (strncmp(line, "/sell ", 6) == 0) {
        const openmmo_party *p = openmmo_client_party(s_client);
        long price = strtol(line + 6, NULL, 10);

        if (p->count < 1) {
            printf("openmmo: /sell with an empty party\n");
            return 1;
        }
        if (openmmo_client_gtl_list_mon(s_client, p->mon[0].id,
                                        (s32)price) != 0)
            printf("openmmo: /sell price %ld refused\n", price);
        else
            printf("openmmo: /sell lead for $%ld\n", price);
        return 1;
    }
    if (strncmp(line, "/sellitem ", 10) == 0) {
        char *end = NULL;
        long item = strtol(line + 10, &end, 10);
        long qty = end != NULL ? strtol(end, &end, 10) : 0;
        long price = end != NULL ? strtol(end, &end, 10) : 0;

        if (openmmo_client_gtl_list_item(s_client, (u16)item, (s32)qty,
                                         (s32)price) != 0)
            printf("openmmo: /sellitem %ld x%ld $%ld refused\n",
                   item, qty, price);
        else
            printf("openmmo: /sellitem %ld x%ld for $%ld\n", item, qty, price);
        return 1;
    }
    return 0;
}

int openmmo_gtl_try_open(FieldSystem *fs)
{
    GtlScreen *g;

    if (fs == NULL || s_client == NULL)
        return 0;
    if (!s_pending || s_opened || s_live != NULL)
        return 0;
    if (fs->task != NULL || !FieldSystem_IsRunningFieldMap(fs)
        || FieldSystem_HasChildProcess(fs))
        return 0;

    g = Heap_Alloc(GTL_HEAP, sizeof(GtlScreen));
    if (g == NULL) {
        printf("openmmo: gtl screen would not allocate\n");
        s_pending = 0;
        return 0;
    }
    memset(g, 0, sizeof(*g));
    g->kind = MMO_GTL_KIND_POKEMON;
    s_live = g;
    FieldSystem_CreateTask(fs, gtl_task, g);
    s_opened = 1;
    s_pending = 0;
    printf("openmmo: gtl opened\n");
    return 1;
}
