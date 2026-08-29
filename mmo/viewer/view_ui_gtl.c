/*
 * See view_ui_gtl.h. The official client's broker window, tab for tab: the strings, the
 * column order and the flows are the official client's (broker.xml + f/ww1 + f/fY1 + f/xX0); what official
 * carries on its own packets rides the .hud page's gtl block and command ring here.
 */

#include "view_ui_gtl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

enum {
    GTAB_MON = 0,   /* Pokemon Listings */
    GTAB_MARKET,    /* Item Market (the quote strip) */
    GTAB_ITEMS,     /* Item Listings */
    GTAB_OWN,       /* Your Listings */
    GTAB_CREATE,    /* Create Listing */
    GTAB_LOG,       /* Trade Log */
    GTAB_N
};

/* The text fields, one buffer each so a value survives losing focus. */
enum {
    GF_NONE = -1,
    GF_SEARCH = 0,  /* the toolbar's species search */
    GF_MIN_PRICE,
    GF_MAX_PRICE,
    GF_MIN_LEVEL,
    GF_MAX_LEVEL,
    GF_SPECIES,     /* the advanced panel's species row */
    GF_CREATE_PRICE,
    GF_CREATE_QTY,
    GF_DLG_QTY,     /* the buy dialog's count */
    GF_DLG_PRICE,   /* the lower-price dialog's new price */
    GF_N
};

#define GTL_FIELD_LEN 16

/* A modal inside the frame. One at a time. */
enum {
    GDLG_NONE = 0,
    GDLG_BUY_MON,    /* confirm a monster purchase */
    GDLG_BUY_ITEM,   /* pick a count, then buy */
    GDLG_BUY_MARKET, /* pick a count of the cheapest asks */
    GDLG_CANCEL,     /* confirm a take-back */
    GDLG_REPRICE,    /* type a lower price */
    GDLG_SELL,       /* confirm the create form's Sell */
};

struct gtl_state {
    int tab;
    int sort[GTAB_N];       /* MMO-sort per tab (0 newest) */
    int page[GTAB_N];
    int adv_open;           /* the advanced panel replaces the table */
    int shiny;              /* -1 --, 0 None, 1 Shiny */
    int nature;             /* -1, else 0..24 */
    int focus;              /* GF_*, or GF_NONE */
    char field[GF_N][GTL_FIELD_LEN];
    /* Create Listing: what the pickers hold. Slot -1 / bag -1 = empty. */
    int create_slot;        /* party slot */
    int create_bag;         /* bag row */
    int picker;             /* 0 none, 1 party, 2 bag */
    int picker_scroll;
    /* The modal. `row` is the page row it acts on. */
    int dlg;
    int dlg_row;
    /* The toast strip: the last result shown and when it landed. */
    uint32_t toast_seq;
    char toast[96];
    uint32_t toast_until;   /* SDL_GetTicks deadline */
    /* Hit rects rebuilt every draw, answered by the next click. */
    struct openmmo_rect r_tab[GTAB_N];
    struct openmmo_rect r_tool[6];      /* per-tab toolbar buttons */
    int tool_n;
    int tool_id[6];
    struct openmmo_rect r_field_search;
    struct openmmo_rect r_buy[OPENMMO_HUD_GTL_ROWS];
    struct openmmo_rect r_price[OPENMMO_HUD_GTL_ROWS];  /* own: reprice */
    struct openmmo_rect r_claim[OPENMMO_HUD_GTL_ROWS];
    struct openmmo_rect r_pager[17];    /* «, 15 numbers, » */
    int pager_page[17];
    int pager_n;
    struct openmmo_rect r_adv_field[GF_N];
    struct openmmo_rect r_adv_shiny, r_adv_nature;
    struct openmmo_rect r_adv_search, r_adv_clear;
    struct openmmo_rect r_create_item, r_create_mon;
    struct openmmo_rect r_create_price, r_create_qty;
    struct openmmo_rect r_create_clear, r_create_sell;
    struct openmmo_rect r_picker_row[10];
    int picker_row_at[10];
    int picker_rows;
    struct openmmo_rect r_dlg_yes, r_dlg_no, r_dlg_all, r_dlg_field;
    int laid;               /* the rects above match the last draw */
};

static struct gtl_state G = {
    .focus = GF_NONE,
    .shiny = -1,
    .nature = -1,
    .create_slot = -1,
    .create_bag = -1,
};

/* Toolbar button ids. */
enum {
    GTOOL_SORT = 0,
    GTOOL_ADV,
    GTOOL_CLEARF,
    GTOOL_REFRESH,
    GTOOL_CLAIM_ALL,
};

static const char *const tab_label[GTAB_N] = {
    "Pokemon Listings", "Item Market", "Item Listings",
    "Your Listings", "Create Listing", "Trade Log",
};

static const char *const sort_label[4] = {
    "Newest", "Oldest", "Lowest Price", "Highest Price",
};

static const char *const nature_name[25] = {
    "Hardy", "Lonely", "Brave", "Adamant", "Naughty",
    "Bold", "Docile", "Relaxed", "Impish", "Lax",
    "Timid", "Hasty", "Serious", "Jolly", "Naive",
    "Modest", "Mild", "Quiet", "Bashful", "Rash",
    "Calm", "Gentle", "Sassy", "Careful", "Quirky",
};

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static void fmt_money(char *dst, size_t cap, unsigned v)
{
    char raw[16];
    int len, i, o = 0;

    snprintf(raw, sizeof raw, "%u", v);
    len = (int)strlen(raw);
    if (dst == NULL || cap < 2)
        return;
    dst[o++] = '$';
    for (i = 0; i < len && o + 1 < (int)cap; i++) {
        dst[o++] = raw[i];
        if ((len - 1 - i) % 3 == 0 && i != len - 1 && o + 1 < (int)cap)
            dst[o++] = ',';
    }
    dst[o] = '\0';
}

/* The official client's elapsed / remaining text (f/bW0): one largest unit. */
static void fmt_span(char *dst, size_t cap, long secs, int past)
{
    const char *unit;
    long n;

    if (secs < 0)
        secs = 0;
    if (secs < 60) {
        snprintf(dst, cap, "%s", past ? "Just now" : "Very soon");
        return;
    }
    if (secs < 3600) { n = secs / 60; unit = "minute"; }
    else if (secs < 86400) { n = secs / 3600; unit = "hour"; }
    else { n = secs / 86400; unit = "day"; }
    if (past)
        snprintf(dst, cap, "%ld %s%s ago", n, unit, n == 1 ? "" : "s");
    else
        snprintf(dst, cap, "%ld %s%s", n, unit, n == 1 ? "" : "s");
}

static void fmt_date(char *dst, size_t cap, uint32_t epoch)
{
    time_t t = (time_t)epoch;
    struct tm tmv;

    if (epoch == 0) {
        snprintf(dst, cap, "----");
        return;
    }
#if defined(_WIN32)
    tmv = *localtime(&t);
#else
    localtime_r(&t, &tmv);
#endif
    strftime(dst, cap, "%b %d, %Y", &tmv);
}

static long field_num(int id)
{
    if (G.field[id][0] == '\0')
        return -1;
    return strtol(G.field[id], NULL, 10);
}

static void field_focus(int id)
{
    G.focus = id;
    if (id != GF_NONE)
        SDL_StartTextInput();
}

static int draw_btn(SDL_Renderer *ren, struct view_ui_gpu *g,
                    const struct openmmo_rect *r, const char *label,
                    int mx, int my, int enabled)
{
    int hot = enabled && view_ui_hit(r, mx, my);
    int tw = view_ui_text_width(g, label);
    int x;

    if (view_ui_th(ren, g, VIEW_UI_TH_BUTTON, r)) {
        if (hot)
            view_ui_th(ren, g, VIEW_UI_TH_BUTTON_HOVER, r);
    } else
        view_ui_panel(ren, r, hot ? VIEW_UI_COL_LINE : VIEW_UI_COL_FIELD,
                      VIEW_UI_COL_LINE);
    x = r->x + (r->w - tw) / 2;
    if (x < r->x + 2)
        x = r->x + 2;
    view_ui_text(ren, g, x, r->y + (r->h - g->px) / 2, label,
                 view_ui_col(enabled ? VIEW_UI_COL_TEXT : VIEW_UI_COL_DIM));
    return hot;
}

static void draw_field_box(SDL_Renderer *ren, struct view_ui_gpu *g,
                           const struct openmmo_rect *r, int id, int pad)
{
    char text[GTL_FIELD_LEN + 2];
    int focused = G.focus == id;

    if (!view_ui_th(ren, g, VIEW_UI_TH_INPUT, r)) {
        SDL_Color f = view_ui_col(VIEW_UI_COL_FIELD);
        SDL_Color l = view_ui_col(focused ? VIEW_UI_COL_ACCENT
                                          : VIEW_UI_COL_LINE);

        view_ui_fill(ren, r->x, r->y, r->w, r->h, f.r, f.g, f.b, 255);
        view_ui_border(ren, r->x, r->y, r->w, r->h, l.r, l.g, l.b);
    }
    snprintf(text, sizeof text, "%s%s", G.field[id], focused ? "_" : "");
    view_ui_text_in(ren, g, r, pad, 0, text, VIEW_UI_COL_TEXT);
}

/* The tab a kind of page serves. */
static int tab_kind(int tab)
{
    switch (tab) {
    case GTAB_MON:    return 0;
    case GTAB_MARKET: return 1;
    case GTAB_ITEMS:  return 1;
    case GTAB_OWN:    return 2;
    default:          return -1;
    }
}

/* Ask the guest for the page this tab shows, filters and all. */
static void gtl_ask(struct view_hud *hud, int tab)
{
    struct openmmo_hud_gtl_ask a;
    int kind = tab_kind(tab);
    long v;

    if (kind < 0) {
        if (tab == GTAB_LOG)
            view_hud_push(hud, OPENMMO_HUD_CMD_GTL, OPENMMO_HUD_GTL_LOG);
        return;
    }
    openmmo_hud_gtl_ask_clear(&a);
    if (tab == GTAB_MON) {
        const char *sp = G.field[GF_SPECIES][0] != '\0'
                             ? G.field[GF_SPECIES] : G.field[GF_SEARCH];

        snprintf(a.species, sizeof a.species, "%s", sp);
        if ((v = field_num(GF_MIN_LEVEL)) >= 0) a.min_level = (int32_t)v;
        if ((v = field_num(GF_MAX_LEVEL)) >= 0) a.max_level = (int32_t)v;
        a.shiny = G.shiny;
        a.nature = G.nature;
    }
    if (tab == GTAB_MON || tab == GTAB_ITEMS) {
        if ((v = field_num(GF_MIN_PRICE)) >= 0) a.min_price = (int32_t)v;
        if ((v = field_num(GF_MAX_PRICE)) >= 0) a.max_price = (int32_t)v;
    }
    if (hud != NULL && hud->page != NULL)
        memcpy((void *)&hud->page->cmd_gtl, &a, sizeof a);
    view_hud_push(hud, OPENMMO_HUD_CMD_GTL,
                  (int32_t)(OPENMMO_HUD_GTL_ASK |
                            ((uint32_t)kind << 8) |
                            ((uint32_t)G.sort[tab] << 12) |
                            ((uint32_t)G.page[tab] << 16)));
}

/* Push a money verb whose wide arguments ride cmd_gtl. */
static void gtl_verb(struct view_hud *hud, unsigned verb, int row,
                     int32_t price, int32_t qty)
{
    if (hud != NULL && hud->page != NULL) {
        struct openmmo_hud_gtl_ask a;

        openmmo_hud_gtl_ask_clear(&a);
        a.price = price;
        a.qty = qty;
        memcpy((void *)&hud->page->cmd_gtl, &a, sizeof a);
    }
    view_hud_push(hud, OPENMMO_HUD_CMD_GTL,
                  (int32_t)(verb | ((uint32_t)(row & 0xFF) << 8)));
}

void view_ui_gtl_open(struct view_hud *hud)
{
    G.tab = GTAB_MON;
    G.dlg = GDLG_NONE;
    G.adv_open = 0;
    G.picker = 0;
    field_focus(GF_NONE);
    gtl_ask(hud, GTAB_MON);
}

int view_ui_gtl_typing(void)
{
    return G.focus != GF_NONE;
}

/* ------------------------------------------------------------------ */
/* The toast strip: the official client's result sentences (kE0's map)              */
/* ------------------------------------------------------------------ */

static void toast_text(const struct openmmo_hud_gtl *g, char *dst, size_t cap)
{
    char money[20];

    switch (g->result_code) {
    case 1:  snprintf(dst, cap, "You have successfully created a new listing."); break;
    case 2:  snprintf(dst, cap, "You have reached the maximum amount of active listings."); break;
    case 4:  snprintf(dst, cap, "You have successfully made a purchase."); break;
    case 5:  snprintf(dst, cap, "The requested listing could not be found."); break;
    case 6:  snprintf(dst, cap, "You cannot purchase your own listing."); break;
    case 7:  snprintf(dst, cap, "The requested listing has been canceled."); break;
    case 8:  snprintf(dst, cap, "The listing has unsettled funds. Please claim first."); break;
    case 9:  snprintf(dst, cap, "You have successfully changed a listing price."); break;
    case 10: snprintf(dst, cap, "You cannot change the price of this listing again yet."); break;
    case 11: snprintf(dst, cap, "You have successfully claimed listing funds/items."); break;
    case 12:
        fmt_money(money, sizeof money, (unsigned)g->result_b);
        snprintf(dst, cap, "Requested Unit Price cannot be less than %s for this item.", money);
        break;
    case 13: snprintf(dst, cap, "You cannot afford it."); break;
    case 15: snprintf(dst, cap, "You have no room for it."); break;
    case 20:
        fmt_money(money, sizeof money, (unsigned)g->result_b);
        snprintf(dst, cap, "Your listing has sold for %s.", money);
        break;
    case 21:
        fmt_money(money, sizeof money, (unsigned)g->result_b);
        snprintf(dst, cap, "%d of your listings have sold for %s.",
                 (int)g->result_a, money);
        break;
    case 22: snprintf(dst, cap, "You cannot lower the price of this listing any further."); break;
    default: snprintf(dst, cap, "The Global Trade Link answered (%d).",
                      (int)g->result_code);
    }
}

/* ------------------------------------------------------------------ */
/* Columns                                                             */
/* ------------------------------------------------------------------ */

struct gtl_col {
    const char *label;
    int w;          /* official px; 0 = take the rest */
};

static const struct gtl_col col_mon[] = {
    { "Pokemon", 0 }, { "Nature", 63 }, { "IVs", 186 },
    { "Price", 92 }, { "Start Date", 84 }, { "End Date", 84 }, { "Buy", 82 },
};
static const struct gtl_col col_items[] = {
    { "Item", 0 }, { "Amount", 60 }, { "Price", 102 },
    { "Start Date", 92 }, { "End Date", 92 }, { "Buy", 96 },
};
static const struct gtl_col col_market[] = {
    { "Item", 0 }, { "Amount", 60 }, { "Price", 102 },
    { "1h %", 60 }, { "24h %", 60 }, { "7d %", 60 }, { "Buy", 96 },
};
static const struct gtl_col col_own[] = {
    { "Item", 0 }, { "Unsold", 60 }, { "Price", 102 },
    { "Unclaimed", 92 }, { "State", 78 }, { "Cancel", 92 }, { "Claim", 92 },
};
static const struct gtl_col col_log[] = {
    { "Sent", 0 }, { "Received", 200 }, { "Type", 100 }, { "Date", 120 },
};

static int tab_cols(int tab, const struct gtl_col **out)
{
    switch (tab) {
    case GTAB_MON:    *out = col_mon;    return (int)(sizeof col_mon / sizeof *col_mon);
    case GTAB_MARKET: *out = col_market; return (int)(sizeof col_market / sizeof *col_market);
    case GTAB_ITEMS:  *out = col_items;  return (int)(sizeof col_items / sizeof *col_items);
    case GTAB_OWN:    *out = col_own;    return (int)(sizeof col_own / sizeof *col_own);
    case GTAB_LOG:    *out = col_log;    return (int)(sizeof col_log / sizeof *col_log);
    default:          *out = NULL;       return 0;
    }
}

/* Lay a row's cells into `cell[]` from the column table. */
static int lay_cells(const struct openmmo_rect *area, int tab, int text_px,
                     struct openmmo_rect *cell, int cap)
{
    const struct gtl_col *cols;
    int n = tab_cols(tab, &cols);
    int i, fixed = 0, x = area->x;

    if (n > cap)
        n = cap;
    for (i = 0; i < n; i++)
        if (cols[i].w > 0)
            fixed += view_ui_scale(cols[i].w, text_px);
    for (i = 0; i < n; i++) {
        int w = cols[i].w > 0 ? view_ui_scale(cols[i].w, text_px)
                              : area->w - fixed;

        if (w < 8)
            w = 8;
        cell[i].x = x;
        cell[i].y = area->y;
        cell[i].w = w;
        cell[i].h = area->h;
        x += w;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* Draw                                                                */
/* ------------------------------------------------------------------ */

/* The guest's page serves this tab. */
static int page_live(const struct openmmo_hud_gtl *g, int tab)
{
    return g->open && tab_kind(tab) >= 0 && (int)g->kind == tab_kind(tab);
}

static void draw_toolbar(SDL_Renderer *ren, struct view_ui_gpu *g,
                         struct view_hud *hud, const struct openmmo_rect *band,
                         int mx, int my)
{
    const struct openmmo_hud_gtl *gs = &hud->snap.gtl;
    int pad = view_ui_scale(6, g->px);
    int h = band->h - 4;
    int x = band->x;
    char label[40];
    struct openmmo_rect r;

    G.tool_n = 0;
    (void)gs;
    if (G.tab == GTAB_CREATE)
        return;
    if (G.tab == GTAB_LOG) {
        r.x = x; r.y = band->y + 2; r.w = view_ui_scale(90, g->px); r.h = h;
        draw_btn(ren, g, &r, "Refresh", mx, my, 1);
        G.r_tool[G.tool_n] = r;
        G.tool_id[G.tool_n++] = GTOOL_REFRESH;
        return;
    }
    if (G.tab == GTAB_OWN) {
        r.x = x; r.y = band->y + 2; r.w = view_ui_scale(120, g->px); r.h = h;
        draw_btn(ren, g, &r, "Claim All", mx, my, 1);
        G.r_tool[G.tool_n] = r;
        G.tool_id[G.tool_n++] = GTOOL_CLAIM_ALL;
        x = r.x + r.w + pad;
    }
    if (G.tab == GTAB_MON) {
        /* The free-text species search, the official client's toolbar edit field. */
        G.r_field_search.x = x;
        G.r_field_search.y = band->y + 2;
        G.r_field_search.w = view_ui_scale(150, g->px);
        G.r_field_search.h = h;
        draw_field_box(ren, g, &G.r_field_search, GF_SEARCH, pad);
        x = G.r_field_search.x + G.r_field_search.w + pad;
    } else
        memset(&G.r_field_search, 0, sizeof G.r_field_search);

    snprintf(label, sizeof label, "Sort: %s", sort_label[G.sort[G.tab] & 3]);
    r.x = x; r.y = band->y + 2; r.w = view_ui_scale(150, g->px); r.h = h;
    draw_btn(ren, g, &r, label, mx, my, 1);
    G.r_tool[G.tool_n] = r;
    G.tool_id[G.tool_n++] = GTOOL_SORT;
    x = r.x + r.w + pad;

    if (G.tab == GTAB_MON || G.tab == GTAB_ITEMS) {
        r.x = x; r.y = band->y + 2; r.w = view_ui_scale(140, g->px); r.h = h;
        draw_btn(ren, g, &r, G.adv_open ? "Close Search" : "Advanced Search",
                 mx, my, 1);
        G.r_tool[G.tool_n] = r;
        G.tool_id[G.tool_n++] = GTOOL_ADV;
        x = r.x + r.w + pad;
    }
    r.x = x; r.y = band->y + 2; r.w = view_ui_scale(90, g->px); r.h = h;
    draw_btn(ren, g, &r, "Refresh", mx, my, 1);
    G.r_tool[G.tool_n] = r;
    G.tool_id[G.tool_n++] = GTOOL_REFRESH;
}

static void draw_header(SDL_Renderer *ren, struct view_ui_gpu *g, int tab,
                        const struct openmmo_rect *band)
{
    struct openmmo_rect cell[8];
    const struct gtl_col *cols;
    int i, n = tab_cols(tab, &cols);

    if (n < 1)
        return;
    n = lay_cells(band, tab, g->px, cell, 8);
    for (i = 0; i < n; i++) {
        if (!view_ui_th(ren, g, VIEW_UI_TH_HEADER, &cell[i]))
            view_ui_panel(ren, &cell[i], VIEW_UI_COL_FIELD, VIEW_UI_COL_LINE);
        view_ui_text_in(ren, g, &cell[i], view_ui_scale(5, g->px),
                        cols[i].w > 0 ? 1 : 0, cols[i].label,
                        VIEW_UI_COL_DIM);
    }
}

/* One IV cell, the official client's markup colours: 31 green, 0 red. */
static void draw_iv(SDL_Renderer *ren, struct view_ui_gpu *g,
                    const struct openmmo_rect *r, unsigned v)
{
    char t[8];
    uint32_t col = v >= 31 ? 0x6FB76Fu : v == 0 ? 0xFF6666u : VIEW_UI_COL_TEXT;

    snprintf(t, sizeof t, "%u", v);
    view_ui_text_in(ren, g, r, 0, 2, t, col);
}

static void row_bg(SDL_Renderer *ren, struct view_ui_gpu *g,
                   const struct openmmo_rect *r, int hot)
{
    if (hot && !view_ui_th(ren, g, VIEW_UI_TH_ROW, r)) {
        SDL_Color b = view_ui_col(VIEW_UI_COL_FIELD);

        view_ui_fill(ren, r->x, r->y, r->w, r->h, b.r, b.g, b.b, 255);
    }
}

static void draw_table(SDL_Renderer *ren, struct view_ui_gpu *g,
                       struct view_hud *hud, const struct openmmo_rect *area,
                       int row_h, int mx, int my)
{
    const struct openmmo_hud_gtl *gs = &hud->snap.gtl;
    struct openmmo_rect row, cell[8];
    char t[64], money[20];
    long now = (long)time(NULL);
    int i, n, c, pad = view_ui_scale(5, g->px);
    int live = page_live(gs, G.tab);
    int rows = G.tab == GTAB_MARKET ? (int)gs->quote_n
             : G.tab == GTAB_LOG    ? (int)gs->log_n
                                    : (int)gs->row_n;

    memset(G.r_buy, 0, sizeof G.r_buy);
    memset(G.r_price, 0, sizeof G.r_price);
    memset(G.r_claim, 0, sizeof G.r_claim);
    if (G.tab != GTAB_LOG && !live) {
        row = *area; row.h = row_h;
        view_ui_text_in(ren, g, &row, pad, 0,
                        gs->open ? "Loading..."
                                 : "Reaching the Global Trade Link...",
                        VIEW_UI_COL_DIM);
        return;
    }
    if (rows == 0) {
        row = *area; row.h = row_h;
        view_ui_text_in(ren, g, &row, pad, 0,
                        G.tab == GTAB_LOG
                            ? "No recent trade activity was found."
                            : "No Entries.",
                        VIEW_UI_COL_DIM);
        return;
    }
    for (i = 0; i < rows && (i + 1) * row_h <= area->h; i++) {
        row.x = area->x;
        row.y = area->y + i * row_h;
        row.w = area->w;
        row.h = row_h;
        n = lay_cells(&row, G.tab, g->px, cell, 8);
        (void)n;
        row_bg(ren, g, &row, view_ui_hit(&row, mx, my));
        c = 0;
        if (G.tab == GTAB_MON) {
            const struct openmmo_hud_gtl_row *r = &gs->row[i];

            snprintf(t, sizeof t, "Lv. %u %s", r->level, r->label);
            view_ui_text_in(ren, g, &cell[c++], pad, 0, t,
                            r->shiny ? 0xF7D060u : VIEW_UI_COL_TEXT);
            view_ui_text_in(ren, g, &cell[c++], pad, 0,
                            r->nature < 25 ? nature_name[r->nature] : "--",
                            VIEW_UI_COL_TEXT);
            {   /* six IV sub-cells inside the IVs column */
                struct openmmo_rect ivr = cell[c];
                int k, w6 = ivr.w / 6;

                for (k = 0; k < 6; k++) {
                    struct openmmo_rect one = ivr;

                    one.x = ivr.x + k * w6;
                    one.w = w6;
                    draw_iv(ren, g, &one, r->iv[k]);
                }
                c++;
            }
            fmt_money(money, sizeof money, r->price);
            view_ui_text_in(ren, g, &cell[c++], pad, 0, money,
                            VIEW_UI_COL_TEXT);
            fmt_span(t, sizeof t, now - (long)r->listed_at, 1);
            view_ui_text_in(ren, g, &cell[c++], pad, 0, t, VIEW_UI_COL_DIM);
            fmt_span(t, sizeof t, (long)r->expires_at - now, 0);
            view_ui_text_in(ren, g, &cell[c++], pad, 0, t, VIEW_UI_COL_DIM);
            G.r_buy[i] = cell[c];
            G.r_buy[i].x += 2; G.r_buy[i].w -= 4;
            G.r_buy[i].y += 2; G.r_buy[i].h -= 4;
            draw_btn(ren, g, &G.r_buy[i], "Buy", mx, my, 1);
        } else if (G.tab == GTAB_ITEMS) {
            const struct openmmo_hud_gtl_row *r = &gs->row[i];

            view_ui_text_in(ren, g, &cell[c++], pad, 0, r->label,
                            VIEW_UI_COL_TEXT);
            snprintf(t, sizeof t, "%u", r->quantity);
            view_ui_text_in(ren, g, &cell[c++], pad, 0, t, VIEW_UI_COL_TEXT);
            fmt_money(money, sizeof money, r->price);
            view_ui_text_in(ren, g, &cell[c++], pad, 0, money,
                            VIEW_UI_COL_TEXT);
            fmt_span(t, sizeof t, now - (long)r->listed_at, 1);
            view_ui_text_in(ren, g, &cell[c++], pad, 0, t, VIEW_UI_COL_DIM);
            fmt_span(t, sizeof t, (long)r->expires_at - now, 0);
            view_ui_text_in(ren, g, &cell[c++], pad, 0, t, VIEW_UI_COL_DIM);
            G.r_buy[i] = cell[c];
            G.r_buy[i].x += 2; G.r_buy[i].w -= 4;
            G.r_buy[i].y += 2; G.r_buy[i].h -= 4;
            draw_btn(ren, g, &G.r_buy[i], "Buy", mx, my, 1);
        } else if (G.tab == GTAB_MARKET) {
            const struct openmmo_hud_gtl_quote *q = &gs->quote[i];

            view_ui_text_in(ren, g, &cell[c++], pad, 0, q->label,
                            VIEW_UI_COL_TEXT);
            view_ui_text_in(ren, g, &cell[c++], pad, 0, "--",
                            VIEW_UI_COL_DIM);
            fmt_money(money, sizeof money, q->price);
            view_ui_text_in(ren, g, &cell[c++], pad, 0, money,
                            VIEW_UI_COL_TEXT);
            view_ui_text_in(ren, g, &cell[c++], pad, 0, "--", VIEW_UI_COL_DIM);
            view_ui_text_in(ren, g, &cell[c++], pad, 0, "--", VIEW_UI_COL_DIM);
            view_ui_text_in(ren, g, &cell[c++], pad, 0, "--", VIEW_UI_COL_DIM);
            G.r_buy[i] = cell[c];
            G.r_buy[i].x += 2; G.r_buy[i].w -= 4;
            G.r_buy[i].y += 2; G.r_buy[i].h -= 4;
            draw_btn(ren, g, &G.r_buy[i], "Buy", mx, my, 1);
        } else if (G.tab == GTAB_OWN) {
            const struct openmmo_hud_gtl_row *r = &gs->row[i];
            int active = r->own_state == 0;
            int claimable = r->own_unclaimed > 0;

            if (r->kind == 0)
                snprintf(t, sizeof t, "Lv. %u %s", r->level, r->label);
            else
                snprintf(t, sizeof t, "%s", r->label);
            view_ui_text_in(ren, g, &cell[c++], pad, 0, t, VIEW_UI_COL_TEXT);
            snprintf(t, sizeof t, "%u", r->own_remaining);
            view_ui_text_in(ren, g, &cell[c++], pad, 0, t, VIEW_UI_COL_TEXT);
            fmt_money(money, sizeof money, r->price);
            G.r_price[i] = cell[c];
            G.r_price[i].x += 2; G.r_price[i].w -= 4;
            G.r_price[i].y += 2; G.r_price[i].h -= 4;
            draw_btn(ren, g, &G.r_price[i], money, mx, my,
                     active && r->own_remaining > 0);
            c++;
            fmt_money(money, sizeof money, r->own_unclaimed * r->price);
            view_ui_text_in(ren, g, &cell[c++], pad, 0, money,
                            claimable ? 0x6FB76Fu : VIEW_UI_COL_DIM);
            view_ui_text_in(ren, g, &cell[c++], pad, 0,
                            active ? "Active" : "Ended",
                            active ? VIEW_UI_COL_TEXT : VIEW_UI_COL_DIM);
            G.r_claim[i] = cell[c + 1];
            {
                struct openmmo_rect cr = cell[c];

                cr.x += 2; cr.w -= 4; cr.y += 2; cr.h -= 4;
                if (active)
                    draw_btn(ren, g, &cr, "Cancel", mx, my, 1);
                else
                    view_ui_text_in(ren, g, &cell[c], pad, 2, "--",
                                    VIEW_UI_COL_DIM);
                G.r_buy[i] = cr; /* the cancel button rides the buy slot */
                if (!active)
                    memset(&G.r_buy[i], 0, sizeof G.r_buy[i]);
            }
            c++;
            G.r_claim[i].x += 2; G.r_claim[i].w -= 4;
            G.r_claim[i].y += 2; G.r_claim[i].h -= 4;
            if (claimable)
                draw_btn(ren, g, &G.r_claim[i], "Claim", mx, my, 1);
            else {
                view_ui_text_in(ren, g, &cell[c], pad, 2, "--",
                                VIEW_UI_COL_DIM);
                memset(&G.r_claim[i], 0, sizeof G.r_claim[i]);
            }
        } else if (G.tab == GTAB_LOG) {
            const struct openmmo_hud_gtl_logrow *r = &gs->log[i];
            int bought = (r->type & 2) != 0;
            char what[48];

            if (r->type & 1)
                snprintf(what, sizeof what, "%ux %s", r->amount, r->label);
            else
                snprintf(what, sizeof what, "Lv. %u %s", r->amount, r->label);
            fmt_money(money, sizeof money, r->total);
            /* Sent | Received, from this chair. */
            view_ui_text_in(ren, g, &cell[0], pad, 0,
                            bought ? money : what, VIEW_UI_COL_TEXT);
            view_ui_text_in(ren, g, &cell[1], pad, 0,
                            bought ? what : money, VIEW_UI_COL_TEXT);
            view_ui_text_in(ren, g, &cell[2], pad, 0, "GTL", VIEW_UI_COL_DIM);
            fmt_date(t, sizeof t, r->epoch);
            view_ui_text_in(ren, g, &cell[3], pad, 0, t, VIEW_UI_COL_DIM);
        }
    }
}

static void draw_pager(SDL_Renderer *ren, struct view_ui_gpu *g,
                       struct view_hud *hud, const struct openmmo_rect *band,
                       int mx, int my)
{
    const struct openmmo_hud_gtl *gs = &hud->snap.gtl;
    int total = page_live(gs, G.tab) ? (int)gs->total : 0;
    int pages = (total + OPENMMO_HUD_GTL_ROWS - 1) / OPENMMO_HUD_GTL_ROWS;
    int page = G.page[G.tab];
    int bw = view_ui_scale(33, g->px), bh = band->h - 2;
    int i, first, count, x;
    char t[80];

    G.pager_n = 0;
    if (G.tab == GTAB_CREATE || G.tab == GTAB_LOG || G.tab == GTAB_MARKET)
        return;
    if (pages < 1)
        pages = 1;
    if (page >= pages)
        page = pages - 1;
    count = pages < 15 ? pages : 15;
    first = page - count / 2;
    if (first < 0)
        first = 0;
    if (first + count > pages)
        first = pages - count;
    x = band->x + (band->w - (count + 2) * (bw + 2)) / 2;
    if (x < band->x)
        x = band->x;

    for (i = -1; i <= count; i++) {
        struct openmmo_rect r;
        int tp;

        r.x = x; r.y = band->y; r.w = bw; r.h = bh;
        x += bw + 2;
        if (i == -1) {
            tp = page - 1;
            draw_btn(ren, g, &r, "<<", mx, my, page > 0);
            if (page <= 0)
                tp = -1;
        } else if (i == count) {
            tp = page + 1;
            draw_btn(ren, g, &r, ">>", mx, my, page < pages - 1);
            if (page >= pages - 1)
                tp = -1;
        } else {
            tp = first + i;
            snprintf(t, sizeof t, "%d", tp + 1);
            draw_btn(ren, g, &r, t, mx, my, tp != page);
            if (tp == page)
                tp = -1;
        }
        if (G.pager_n < 17) {
            G.r_pager[G.pager_n] = r;
            G.pager_page[G.pager_n++] = tp;
        }
    }
    /* The official client's count line (string 8028) under the strip. */
    if (total > 0) {
        int lo = page * OPENMMO_HUD_GTL_ROWS + 1;
        int hi = lo + (int)gs->row_n - 1;

        snprintf(t, sizeof t, "Showing %d to %d of %d listings.", lo, hi,
                 total);
        view_ui_text(ren, g,
                     band->x + (band->w - view_ui_text_width(g, t)) / 2,
                     band->y + bh + 2, t, view_ui_col(VIEW_UI_COL_DIM));
    }
}

/* The advanced panel: the server-cut rows live, the rest muted (an ignored
 * filter drawn as workable would be a lie about what the search did). */
static void draw_adv(SDL_Renderer *ren, struct view_ui_gpu *g,
                     const struct openmmo_rect *area, int mx, int my)
{
    int pad = view_ui_scale(6, g->px);
    int lw = view_ui_scale(120, g->px);
    int fw = view_ui_scale(150, g->px);
    int rh = view_ui_scale(30, g->px);
    int col2 = area->x + area->w / 2;
    int y = area->y + pad;
    struct openmmo_rect r;
    char t[40];

    memset(G.r_adv_field, 0, sizeof G.r_adv_field);

#define ADV_LABEL(X, TXT)                                                     \
    do {                                                                      \
        r.x = (X); r.y = y; r.w = lw; r.h = rh - 4;                           \
        if (!view_ui_th(ren, g, VIEW_UI_TH_HEADER, &r))                       \
            view_ui_panel(ren, &r, VIEW_UI_COL_FIELD, VIEW_UI_COL_LINE);      \
        view_ui_text_in(ren, g, &r, pad, 0, (TXT), VIEW_UI_COL_DIM);          \
    } while (0)
#define ADV_FIELD(X, ID)                                                      \
    do {                                                                      \
        r.x = (X) + lw + 4; r.y = y; r.w = fw; r.h = rh - 4;                  \
        G.r_adv_field[ID] = r;                                                \
        draw_field_box(ren, g, &r, (ID), pad);                                \
    } while (0)

    if (G.tab == GTAB_MON) {
        ADV_LABEL(area->x, "Min Price");  ADV_FIELD(area->x, GF_MIN_PRICE);
        ADV_LABEL(col2, "Max Price");     ADV_FIELD(col2, GF_MAX_PRICE);
        y += rh;
        ADV_LABEL(area->x, "Shiny");
        r.x = area->x + lw + 4; r.y = y; r.w = fw; r.h = rh - 4;
        G.r_adv_shiny = r;
        draw_btn(ren, g, &r,
                 G.shiny < 0 ? "--" : G.shiny ? "Shiny" : "None", mx, my, 1);
        ADV_LABEL(col2, "Nature");
        r.x = col2 + lw + 4; r.y = y; r.w = fw; r.h = rh - 4;
        G.r_adv_nature = r;
        snprintf(t, sizeof t, "%s",
                 G.nature < 0 ? "--" : nature_name[G.nature]);
        draw_btn(ren, g, &r, t, mx, my, 1);
        y += rh;
        ADV_LABEL(area->x, "Min Level");  ADV_FIELD(area->x, GF_MIN_LEVEL);
        ADV_LABEL(col2, "Max Level");     ADV_FIELD(col2, GF_MAX_LEVEL);
        y += rh;
        ADV_LABEL(area->x, "Pokemon");    ADV_FIELD(area->x, GF_SPECIES);
        y += rh;
        view_ui_text(ren, g, area->x, y + 4,
                     "Gender, IVs, EVs, moves and the rest arrive with a "
                     "later update.",
                     view_ui_col(VIEW_UI_COL_DIM));
        y += rh;
    } else {
        ADV_LABEL(area->x, "Min Price");  ADV_FIELD(area->x, GF_MIN_PRICE);
        ADV_LABEL(col2, "Max Price");     ADV_FIELD(col2, GF_MAX_PRICE);
        y += rh;
    }
#undef ADV_LABEL
#undef ADV_FIELD

    r.x = area->x; r.y = y + pad; r.w = view_ui_scale(96, g->px);
    r.h = rh - 4;
    G.r_adv_clear = r;
    draw_btn(ren, g, &r, "Clear", mx, my, 1);
    r.x = area->x + area->w - view_ui_scale(96, g->px);
    G.r_adv_search = r;
    draw_btn(ren, g, &r, "Search", mx, my, 1);
}

/* Create Listing, the official client's left form (fY1): what, the unit price, the fee
 * this window computes with the server's own constants, and Sell. */
static void draw_create(SDL_Renderer *ren, struct view_ui_gpu *g,
                        struct view_hud *hud,
                        const struct openmmo_rect *area, int mx, int my)
{
    const struct openmmo_hud_snap *s = &hud->snap;
    int pad = view_ui_scale(6, g->px);
    int lw = view_ui_scale(160, g->px);
    int fw = view_ui_scale(220, g->px);
    int rh = view_ui_scale(40, g->px);
    int y = area->y + pad;
    long price = field_num(GF_CREATE_PRICE);
    long qty = field_num(GF_CREATE_QTY);
    struct openmmo_rect r;
    char t[64], money[20];
    int is_item = G.create_bag >= 0;
    long fee, total;

    if (qty < 1 || !is_item)
        qty = 1;
    if (is_item && G.create_bag < (int)s->bag_n &&
        qty > (long)s->bag[G.create_bag].count)
        qty = (long)s->bag[G.create_bag].count;
    fee = price > 0 ? (price / 40 < 100 ? 100 : price / 40) : 0;
    if (fee > 50000)
        fee = 50000;
    fee *= qty;
    total = price > 0 ? price * qty : 0;

#define CREATE_LABEL(TXT)                                                     \
    do {                                                                      \
        r.x = area->x; r.y = y; r.w = lw; r.h = rh - 6;                       \
        if (!view_ui_th(ren, g, VIEW_UI_TH_HEADER, &r))                       \
            view_ui_panel(ren, &r, VIEW_UI_COL_FIELD, VIEW_UI_COL_LINE);      \
        view_ui_text_in(ren, g, &r, pad, 0, (TXT), VIEW_UI_COL_DIM);          \
        r.x = area->x + lw + 6; r.w = fw;                                     \
    } while (0)

    CREATE_LABEL("Item");
    G.r_create_item = r;
    if (is_item && G.create_bag < (int)s->bag_n)
        snprintf(t, sizeof t, "%s x%u", s->bag[G.create_bag].label,
                 (unsigned)qty);
    else
        snprintf(t, sizeof t, "--");
    draw_btn(ren, g, &r, t, mx, my, 1);
    y += rh;

    CREATE_LABEL("Pokemon");
    G.r_create_mon = r;
    if (G.create_slot >= 0 && G.create_slot < (int)s->party_n)
        snprintf(t, sizeof t, "Lv. %u %s",
                 (unsigned)s->party[G.create_slot].level,
                 s->party[G.create_slot].name);
    else
        snprintf(t, sizeof t, "--");
    draw_btn(ren, g, &r, t, mx, my, 1);
    y += rh;

    CREATE_LABEL("Requested Unit Price");
    G.r_create_price = r;
    draw_field_box(ren, g, &r, GF_CREATE_PRICE, pad);
    y += rh;

    if (is_item) {
        CREATE_LABEL("Quantity");
        G.r_create_qty = r;
        draw_field_box(ren, g, &r, GF_CREATE_QTY, pad);
        y += rh;
    } else
        memset(&G.r_create_qty, 0, sizeof G.r_create_qty);

    CREATE_LABEL("Listing Fee");
    fmt_money(money, sizeof money, (unsigned)fee);
    view_ui_text_in(ren, g, &r, pad, 0, fee > 0 ? money : "--",
                    VIEW_UI_COL_DIM);
    y += rh;

    CREATE_LABEL("Total Value");
    fmt_money(money, sizeof money, (unsigned)total);
    view_ui_text_in(ren, g, &r, pad, 0, total > 0 ? money : "--",
                    VIEW_UI_COL_DIM);
    y += rh;
#undef CREATE_LABEL

    r.x = area->x; r.y = y + pad; r.w = view_ui_scale(100, g->px);
    r.h = rh - 6;
    G.r_create_clear = r;
    draw_btn(ren, g, &r, "Clear", mx, my, 1);
    r.x = area->x + lw + 6;
    G.r_create_sell = r;
    draw_btn(ren, g, &r, "Sell", mx, my,
             price >= 1 && (is_item || G.create_slot >= 0));

    /* The wallet, for the fee at a glance. */
    fmt_money(money, sizeof money, s->money);
    snprintf(t, sizeof t, "Balance: %s", money);
    view_ui_text(ren, g, area->x, r.y + rh, t, view_ui_col(VIEW_UI_COL_DIM));

    /* The picker, over the form. */
    G.picker_rows = 0;
    if (G.picker != 0) {
        struct openmmo_rect box;
        int i, n = G.picker == 1 ? (int)s->party_n : (int)s->bag_n;
        int prh = view_ui_scale(26, g->px);
        int shown = n - G.picker_scroll;

        if (shown > 10)
            shown = 10;
        box.x = area->x + lw + 6;
        box.y = area->y + pad;
        box.w = fw + view_ui_scale(60, g->px);
        box.h = (shown > 0 ? shown : 1) * prh + 2 * pad;
        if (!view_ui_th(ren, g, VIEW_UI_TH_PANEL, &box))
            view_ui_panel(ren, &box, VIEW_UI_COL_GROUND, VIEW_UI_COL_ACCENT);
        for (i = 0; i < shown; i++) {
            int at = G.picker_scroll + i;

            r.x = box.x + pad; r.y = box.y + pad + i * prh;
            r.w = box.w - 2 * pad; r.h = prh - 2;
            if (G.picker == 1)
                snprintf(t, sizeof t, "Lv. %u %s",
                         (unsigned)s->party[at].level, s->party[at].name);
            else
                snprintf(t, sizeof t, "%s x%u", s->bag[at].label,
                         (unsigned)s->bag[at].count);
            row_bg(ren, g, &r, view_ui_hit(&r, mx, my));
            view_ui_text_in(ren, g, &r, pad, 0, t, VIEW_UI_COL_TEXT);
            G.r_picker_row[i] = r;
            G.picker_row_at[i] = at;
            G.picker_rows = i + 1;
        }
        if (shown <= 0)
            view_ui_text_in(ren, g, &box, pad, 0, "No Entries.",
                            VIEW_UI_COL_DIM);
    }
}

/* The modal over everything. */
static void draw_dialog(SDL_Renderer *ren, struct view_ui_gpu *g,
                        struct view_hud *hud,
                        const struct view_ui_frame_layout *L, int mx, int my)
{
    const struct openmmo_hud_gtl *gs = &hud->snap.gtl;
    struct openmmo_rect box, r;
    char line1[96], line2[96], money[20];
    int pad = view_ui_scale(10, g->px);
    int bh = view_ui_scale(26, g->px);
    long qty = field_num(GF_DLG_QTY);
    int has_field = 0, has_all = 0;

    if (G.dlg == GDLG_NONE)
        return;
    line1[0] = line2[0] = '\0';
    if (qty < 1)
        qty = 1;
    switch (G.dlg) {
    case GDLG_BUY_MON:
        snprintf(line1, sizeof line1,
                 "Are you sure you wish to purchase that listing?");
        if (G.dlg_row < (int)gs->row_n) {
            fmt_money(money, sizeof money, gs->row[G.dlg_row].price);
            snprintf(line2, sizeof line2, "Lv. %u %s  %s",
                     gs->row[G.dlg_row].level, gs->row[G.dlg_row].label,
                     money);
        }
        break;
    case GDLG_BUY_ITEM:
        has_field = 1;
        has_all = 1;
        if (G.dlg_row < (int)gs->row_n) {
            if (qty > (long)gs->row[G.dlg_row].quantity)
                qty = (long)gs->row[G.dlg_row].quantity;
            snprintf(line1, sizeof line1,
                     "How many %s would you like to purchase?",
                     gs->row[G.dlg_row].label);
            fmt_money(money, sizeof money,
                      (unsigned)(qty * gs->row[G.dlg_row].price));
            snprintf(line2, sizeof line2, "Total cost: %s", money);
        }
        break;
    case GDLG_BUY_MARKET:
        has_field = 1;
        if (G.dlg_row < (int)gs->quote_n) {
            snprintf(line1, sizeof line1,
                     "How many %s would you like to purchase?",
                     gs->quote[G.dlg_row].label);
            fmt_money(money, sizeof money,
                      (unsigned)(qty * gs->quote[G.dlg_row].price));
            snprintf(line2, sizeof line2, "At the best asks, about %s.",
                     money);
        }
        break;
    case GDLG_CANCEL:
        snprintf(line1, sizeof line1,
                 "Are you sure you wish to cancel that listing?");
        snprintf(line2, sizeof line2, "Listing fees will not be refunded.");
        break;
    case GDLG_REPRICE:
        has_field = 1;
        snprintf(line1, sizeof line1,
                 "Would you like to lower the price of this listing?");
        snprintf(line2, sizeof line2,
                 "You can only do this once every 10 minutes.");
        break;
    case GDLG_SELL: {
        long price = field_num(GF_CREATE_PRICE);

        fmt_money(money, sizeof money, (unsigned)(price > 0 ? price : 0));
        if (G.create_bag >= 0 && G.create_bag < (int)hud->snap.bag_n)
            snprintf(line1, sizeof line1,
                     "Are you sure you want to sell %s for %s each?",
                     hud->snap.bag[G.create_bag].label, money);
        else if (G.create_slot >= 0 &&
                 G.create_slot < (int)hud->snap.party_n)
            snprintf(line1, sizeof line1,
                     "Are you sure you want to sell %s for %s?",
                     hud->snap.party[G.create_slot].name, money);
        break;
    }
    default:
        break;
    }

    box.w = view_ui_scale(420, g->px);
    box.h = 3 * bh + (has_field ? bh : 0) + 4 * pad;
    box.x = L->box.x + (L->box.w - box.w) / 2;
    box.y = L->box.y + (L->box.h - box.h) / 2;
    if (!view_ui_th(ren, g, VIEW_UI_TH_WARN, &box))
        view_ui_panel(ren, &box, VIEW_UI_COL_GROUND, VIEW_UI_COL_ACCENT);
    view_ui_text(ren, g, box.x + pad, box.y + pad, line1,
                 view_ui_col(VIEW_UI_COL_TEXT));
    view_ui_text(ren, g, box.x + pad, box.y + pad + bh, line2,
                 view_ui_col(VIEW_UI_COL_DIM));
    if (has_field) {
        G.r_dlg_field.x = box.x + pad;
        G.r_dlg_field.y = box.y + pad + 2 * bh;
        G.r_dlg_field.w = view_ui_scale(120, g->px);
        G.r_dlg_field.h = bh - 2;
        draw_field_box(ren, g, &G.r_dlg_field,
                       G.dlg == GDLG_REPRICE ? GF_DLG_PRICE : GF_DLG_QTY,
                       pad / 2);
    } else
        memset(&G.r_dlg_field, 0, sizeof G.r_dlg_field);

    r.y = box.y + box.h - bh - pad;
    r.w = view_ui_scale(96, g->px);
    r.h = bh;
    r.x = box.x + box.w - 2 * (r.w + pad);
    G.r_dlg_yes = r;
    draw_btn(ren, g, &r, "Accept", mx, my, 1);
    r.x = box.x + box.w - (r.w + pad);
    G.r_dlg_no = r;
    draw_btn(ren, g, &r, "Cancel", mx, my, 1);
    if (has_all) {
        r.x = box.x + pad;
        G.r_dlg_all = r;
        draw_btn(ren, g, &r, "All", mx, my, 1);
    } else
        memset(&G.r_dlg_all, 0, sizeof G.r_dlg_all);
}

void view_ui_gtl_draw(SDL_Renderer *ren, struct view_ui_gpu *g,
                      const struct view_ui_frame_layout *L,
                      struct view_hud *hud, int mx, int my)
{
    const struct openmmo_hud_gtl *gs = &hud->snap.gtl;
    struct openmmo_rect area, band;
    int i, pad = L->pad;
    int tab_h = view_ui_scale(26, g->px);
    int tool_h = view_ui_scale(30, g->px);
    int head_h = view_ui_scale(20, g->px);
    int row_h = view_ui_scale(36, g->px);
    int pager_h = view_ui_scale(24, g->px);
    int y;

    /* The body, inside the frame's chrome. */
    area.x = L->box.x + pad;
    area.w = L->box.w - 2 * pad;
    area.y = L->box.y + L->title_h;
    area.h = L->box.y + L->box.h - area.y - pad;
    y = area.y;

    /* Tabs. */
    {
        int gap = 1, tw = (area.w - (GTAB_N - 1) * gap) / GTAB_N, x = area.x;

        for (i = 0; i < GTAB_N; i++) {
            struct openmmo_rect r = { x, y, tw, tab_h };
            int active = i == G.tab;
            int lw2 = view_ui_text_width(g, tab_label[i]);
            int tx = r.x + (r.w - lw2) / 2;

            if (!view_ui_th(ren, g,
                            active ? VIEW_UI_TH_TAB_ACTIVE : VIEW_UI_TH_TAB,
                            &r))
                view_ui_panel(ren, &r,
                              active ? VIEW_UI_COL_LINE : VIEW_UI_COL_FIELD,
                              VIEW_UI_COL_LINE);
            if (tx < r.x + 2)
                tx = r.x + 2;
            view_ui_text(ren, g, tx, r.y + (r.h - g->px) / 2, tab_label[i],
                         view_ui_col(active ? VIEW_UI_COL_TEXT
                                            : VIEW_UI_COL_DIM));
            G.r_tab[i] = r;
            x += tw + gap;
        }
        y += tab_h + 4;
    }

    /* Toolbar. */
    band.x = area.x; band.y = y; band.w = area.w; band.h = tool_h;
    draw_toolbar(ren, g, hud, &band, mx, my);
    if (G.tab != GTAB_CREATE)
        y += tool_h + 2;

    if (G.adv_open && (G.tab == GTAB_MON || G.tab == GTAB_ITEMS)) {
        struct openmmo_rect adv = { area.x, y, area.w,
                                    area.y + area.h - y };

        draw_adv(ren, g, &adv, mx, my);
    } else if (G.tab == GTAB_CREATE) {
        struct openmmo_rect form = { area.x, y, area.w,
                                     area.y + area.h - y };

        draw_create(ren, g, hud, &form, mx, my);
    } else {
        struct openmmo_rect table;

        if (G.tab != GTAB_LOG) {
            band.x = area.x; band.y = y; band.w = area.w; band.h = head_h;
            draw_header(ren, g, G.tab, &band);
            y += head_h;
        } else {
            band.x = area.x; band.y = y; band.w = area.w; band.h = head_h;
            draw_header(ren, g, GTAB_LOG, &band);
            y += head_h;
        }
        table.x = area.x; table.y = y; table.w = area.w;
        table.h = area.y + area.h - y - pager_h - g->px - 6;
        draw_table(ren, g, hud, &table, row_h, mx, my);
        band.x = area.x;
        band.y = area.y + area.h - pager_h - g->px - 2;
        band.w = area.w;
        band.h = pager_h;
        draw_pager(ren, g, hud, &band, mx, my);
    }

    /* The toast strip: one official sentence per 0xAF, a few seconds each. */
    if (gs->result_seq != G.toast_seq) {
        G.toast_seq = gs->result_seq;
        if (gs->result_code != 0) {
            toast_text(gs, G.toast, sizeof G.toast);
            G.toast_until = SDL_GetTicks() + 4000u;
        }
    }
    if (G.toast[0] != '\0' && SDL_GetTicks() < G.toast_until) {
        struct openmmo_rect tr;
        int tw = view_ui_text_width(g, G.toast);

        tr.w = tw + 2 * pad;
        tr.h = g->px + pad;
        tr.x = L->box.x + (L->box.w - tr.w) / 2;
        tr.y = L->box.y + L->box.h - tr.h - 2;
        if (!view_ui_th(ren, g, VIEW_UI_TH_PANEL, &tr))
            view_ui_panel(ren, &tr, VIEW_UI_COL_GROUND, VIEW_UI_COL_ACCENT);
        view_ui_text_in(ren, g, &tr, pad, 0, G.toast, VIEW_UI_COL_TEXT);
    }

    draw_dialog(ren, g, hud, L, mx, my);
    G.laid = 1;
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

static void dlg_close(void)
{
    G.dlg = GDLG_NONE;
    if (G.focus == GF_DLG_QTY || G.focus == GF_DLG_PRICE)
        field_focus(GF_NONE);
}

static void dlg_accept(struct view_hud *hud)
{
    const struct openmmo_hud_gtl *gs = &hud->snap.gtl;
    long qty = field_num(GF_DLG_QTY);

    if (qty < 1)
        qty = 1;
    switch (G.dlg) {
    case GDLG_BUY_MON:
        gtl_verb(hud, OPENMMO_HUD_GTL_BUY, G.dlg_row, -1, 1);
        break;
    case GDLG_BUY_ITEM:
        if (G.dlg_row < (int)gs->row_n &&
            qty > (long)gs->row[G.dlg_row].quantity)
            qty = (long)gs->row[G.dlg_row].quantity;
        gtl_verb(hud, OPENMMO_HUD_GTL_BUY, G.dlg_row, -1, (int32_t)qty);
        break;
    case GDLG_BUY_MARKET:
        if (G.dlg_row < (int)gs->quote_n)
            gtl_verb(hud, OPENMMO_HUD_GTL_MARKET, G.dlg_row,
                     (int32_t)(qty * (long)gs->quote[G.dlg_row].price),
                     (int32_t)qty);
        break;
    case GDLG_CANCEL:
        gtl_verb(hud, OPENMMO_HUD_GTL_BACK, G.dlg_row, -1, 1);
        break;
    case GDLG_REPRICE: {
        long p = field_num(GF_DLG_PRICE);

        if (p >= 1)
            gtl_verb(hud, OPENMMO_HUD_GTL_REPRICE, G.dlg_row, (int32_t)p, 1);
        break;
    }
    case GDLG_SELL: {
        long price = field_num(GF_CREATE_PRICE);
        long n = field_num(GF_CREATE_QTY);

        if (price < 1)
            break;
        if (G.create_bag >= 0)
            gtl_verb(hud, OPENMMO_HUD_GTL_SELL_ITEM, G.create_bag,
                     (int32_t)price, (int32_t)(n < 1 ? 1 : n));
        else if (G.create_slot >= 0)
            gtl_verb(hud, OPENMMO_HUD_GTL_SELL, G.create_slot,
                     (int32_t)price, 1);
        G.create_bag = -1;
        G.create_slot = -1;
        G.field[GF_CREATE_PRICE][0] = '\0';
        G.field[GF_CREATE_QTY][0] = '\0';
        break;
    }
    default:
        break;
    }
    dlg_close();
}

static int dlg_event(const SDL_Event *ev, struct view_hud *hud)
{
    if (G.dlg == GDLG_NONE)
        return 0;
    if (ev->type == SDL_KEYDOWN && !ev->key.repeat) {
        SDL_Keycode k = ev->key.keysym.sym;

        if (k == SDLK_ESCAPE) {
            dlg_close();
            return 1;
        }
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
            dlg_accept(hud);
            return 1;
        }
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN &&
        ev->button.button == SDL_BUTTON_LEFT) {
        int x = ev->button.x, y = ev->button.y;

        if (view_ui_hit(&G.r_dlg_yes, x, y)) {
            dlg_accept(hud);
            return 1;
        }
        if (view_ui_hit(&G.r_dlg_no, x, y)) {
            dlg_close();
            return 1;
        }
        if (G.r_dlg_all.w > 0 && view_ui_hit(&G.r_dlg_all, x, y)) {
            const struct openmmo_hud_gtl *gs = &hud->snap.gtl;

            if (G.dlg == GDLG_BUY_ITEM && G.dlg_row < (int)gs->row_n)
                snprintf(G.field[GF_DLG_QTY], GTL_FIELD_LEN, "%u",
                         gs->row[G.dlg_row].quantity);
            return 1;
        }
        if (G.r_dlg_field.w > 0 && view_ui_hit(&G.r_dlg_field, x, y)) {
            field_focus(G.dlg == GDLG_REPRICE ? GF_DLG_PRICE : GF_DLG_QTY);
            return 1;
        }
        return 1; /* modal */
    }
    return ev->type != SDL_MOUSEMOTION;
}

static int field_key(const SDL_Event *ev)
{
    if (G.focus == GF_NONE)
        return 0;
    if (ev->type == SDL_TEXTINPUT) {
        const char *t = ev->text.text;
        int digits_only = G.focus != GF_SEARCH && G.focus != GF_SPECIES;

        for (; *t != '\0'; t++) {
            unsigned char c = (unsigned char)*t;

            if (c < 32 || c > 126)
                continue;
            if (digits_only && (c < '0' || c > '9'))
                continue;
            {
                int len = (int)strlen(G.field[G.focus]);

                if (len + 1 < GTL_FIELD_LEN) {
                    G.field[G.focus][len] = (char)c;
                    G.field[G.focus][len + 1] = '\0';
                }
            }
        }
        return 1;
    }
    if (ev->type == SDL_KEYDOWN) {
        SDL_Keycode k = ev->key.keysym.sym;
        int len = (int)strlen(G.field[G.focus]);

        if (k == SDLK_BACKSPACE) {
            if (len > 0)
                view_ui_utf8_trunc(G.field[G.focus]);
            return 1;
        }
        if (k == SDLK_ESCAPE) {
            field_focus(GF_NONE);
            return 1;
        }
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER)
            return 0; /* let the caller submit */
        return k >= SDLK_a && k <= SDLK_z;
    }
    return 0;
}

int view_ui_gtl_event(const SDL_Event *ev,
                      const struct view_ui_frame_layout *L,
                      struct view_hud *hud)
{
    const struct openmmo_hud_gtl *gs = &hud->snap.gtl;
    int i, x, y;

    if (!G.laid)
        return 0;
    if (dlg_event(ev, hud))
        return 1;
    if (field_key(ev)) {
        if (ev->type == SDL_TEXTINPUT || ev->type == SDL_KEYDOWN)
            return 1;
    }
    if (ev->type == SDL_KEYDOWN && !ev->key.repeat &&
        (ev->key.keysym.sym == SDLK_RETURN ||
         ev->key.keysym.sym == SDLK_KP_ENTER) &&
        G.focus != GF_NONE) {
        /* Enter in a toolbar / advanced field runs the search. */
        int was = G.focus;

        field_focus(GF_NONE);
        if (was == GF_SEARCH || (was >= GF_MIN_PRICE && was <= GF_SPECIES)) {
            G.page[G.tab] = 0;
            G.adv_open = 0;
            gtl_ask(hud, G.tab);
        }
        return 1;
    }

    if (ev->type != SDL_MOUSEBUTTONDOWN ||
        ev->button.button != SDL_BUTTON_LEFT)
        return 0;
    x = ev->button.x;
    y = ev->button.y;

    for (i = 0; i < GTAB_N; i++)
        if (view_ui_hit(&G.r_tab[i], x, y)) {
            if (i != G.tab) {
                G.tab = i;
                G.adv_open = 0;
                G.picker = 0;
                field_focus(GF_NONE);
                gtl_ask(hud, i);
            }
            return 1;
        }

    for (i = 0; i < G.tool_n; i++)
        if (view_ui_hit(&G.r_tool[i], x, y)) {
            switch (G.tool_id[i]) {
            case GTOOL_SORT:
                G.sort[G.tab] = (G.sort[G.tab] + 1) & 3;
                G.page[G.tab] = 0;
                gtl_ask(hud, G.tab);
                break;
            case GTOOL_ADV:
                G.adv_open = !G.adv_open;
                break;
            case GTOOL_REFRESH:
                gtl_ask(hud, G.tab);
                break;
            case GTOOL_CLAIM_ALL:
                gtl_verb(hud, OPENMMO_HUD_GTL_CLAIM, 255, -1, 1);
                break;
            default:
                break;
            }
            return 1;
        }

    if (G.r_field_search.w > 0 && view_ui_hit(&G.r_field_search, x, y)) {
        field_focus(GF_SEARCH);
        return 1;
    }

    if (G.adv_open && (G.tab == GTAB_MON || G.tab == GTAB_ITEMS)) {
        for (i = 0; i < GF_N; i++)
            if (G.r_adv_field[i].w > 0 && view_ui_hit(&G.r_adv_field[i], x, y)) {
                field_focus(i);
                return 1;
            }
        if (G.tab == GTAB_MON && view_ui_hit(&G.r_adv_shiny, x, y)) {
            G.shiny = G.shiny >= 1 ? -1 : G.shiny + 1;
            return 1;
        }
        if (G.tab == GTAB_MON && view_ui_hit(&G.r_adv_nature, x, y)) {
            G.nature = G.nature >= 24 ? -1 : G.nature + 1;
            return 1;
        }
        if (view_ui_hit(&G.r_adv_clear, x, y)) {
            for (i = GF_MIN_PRICE; i <= GF_SPECIES; i++)
                G.field[i][0] = '\0';
            G.shiny = -1;
            G.nature = -1;
            return 1;
        }
        if (view_ui_hit(&G.r_adv_search, x, y)) {
            G.adv_open = 0;
            G.page[G.tab] = 0;
            field_focus(GF_NONE);
            gtl_ask(hud, G.tab);
            return 1;
        }
        return view_ui_hit(&L->box, x, y);
    }

    if (G.tab == GTAB_CREATE) {
        if (G.picker != 0) {
            for (i = 0; i < G.picker_rows; i++)
                if (view_ui_hit(&G.r_picker_row[i], x, y)) {
                    if (G.picker == 1) {
                        G.create_slot = G.picker_row_at[i];
                        G.create_bag = -1;
                    } else {
                        G.create_bag = G.picker_row_at[i];
                        G.create_slot = -1;
                    }
                    G.picker = 0;
                    return 1;
                }
            G.picker = 0;
            return 1;
        }
        if (view_ui_hit(&G.r_create_item, x, y)) {
            G.picker = 2;
            G.picker_scroll = 0;
            return 1;
        }
        if (view_ui_hit(&G.r_create_mon, x, y)) {
            G.picker = 1;
            G.picker_scroll = 0;
            return 1;
        }
        if (view_ui_hit(&G.r_create_price, x, y)) {
            field_focus(GF_CREATE_PRICE);
            return 1;
        }
        if (G.r_create_qty.w > 0 && view_ui_hit(&G.r_create_qty, x, y)) {
            field_focus(GF_CREATE_QTY);
            return 1;
        }
        if (view_ui_hit(&G.r_create_clear, x, y)) {
            G.create_slot = -1;
            G.create_bag = -1;
            G.field[GF_CREATE_PRICE][0] = '\0';
            G.field[GF_CREATE_QTY][0] = '\0';
            return 1;
        }
        if (view_ui_hit(&G.r_create_sell, x, y)) {
            long price = field_num(GF_CREATE_PRICE);

            if (price >= 1 && (G.create_bag >= 0 || G.create_slot >= 0)) {
                G.dlg = GDLG_SELL;
                G.dlg_row = 0;
            }
            return 1;
        }
        return view_ui_hit(&L->box, x, y);
    }

    /* Table buttons. */
    for (i = 0; i < (int)OPENMMO_HUD_GTL_ROWS; i++) {
        if (G.r_buy[i].w > 0 && view_ui_hit(&G.r_buy[i], x, y)) {
            G.dlg_row = i;
            G.field[GF_DLG_QTY][0] = '\0';
            if (G.tab == GTAB_MON)
                G.dlg = GDLG_BUY_MON;
            else if (G.tab == GTAB_ITEMS)
                G.dlg = i < (int)gs->row_n && gs->row[i].quantity > 1
                            ? GDLG_BUY_ITEM : GDLG_BUY_MON;
            else if (G.tab == GTAB_MARKET) {
                G.dlg = GDLG_BUY_MARKET;
                snprintf(G.field[GF_DLG_QTY], GTL_FIELD_LEN, "1");
            } else if (G.tab == GTAB_OWN)
                G.dlg = GDLG_CANCEL;
            if (G.dlg == GDLG_BUY_ITEM)
                snprintf(G.field[GF_DLG_QTY], GTL_FIELD_LEN, "1");
            return 1;
        }
        if (G.r_claim[i].w > 0 && view_ui_hit(&G.r_claim[i], x, y)) {
            gtl_verb(hud, OPENMMO_HUD_GTL_CLAIM, i, -1, 1);
            return 1;
        }
        if (G.r_price[i].w > 0 && view_ui_hit(&G.r_price[i], x, y) &&
            G.tab == GTAB_OWN) {
            G.dlg = GDLG_REPRICE;
            G.dlg_row = i;
            G.field[GF_DLG_PRICE][0] = '\0';
            field_focus(GF_DLG_PRICE);
            return 1;
        }
    }

    for (i = 0; i < G.pager_n; i++)
        if (view_ui_hit(&G.r_pager[i], x, y)) {
            if (G.pager_page[i] >= 0) {
                G.page[G.tab] = G.pager_page[i];
                gtl_ask(hud, G.tab);
            }
            return 1;
        }

    return view_ui_hit(&L->box, x, y);
}
