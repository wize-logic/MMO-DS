/* See view_ui_wins.h. */

#include "view_ui_wins.h"
#include "view_ui_gtl.h"
#include "view_ui_mail.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "status_channel.h"

/* Rows a frame can hold before the list is cut. The longest is the team's
 * 64 members plus its header. */
#define WIN_ROW_N (OPENMMO_HUD_MEMBER_N + 8)

/* One table row as the official client draws one: a label, a value at the right edge, a
 * colour that says what the row is, and, where the row is a player, the
 * name the player menu acts on. */
struct win_row {
    char     left[VIEW_UI_ROW_LEN];
    char     right[32];
    uint32_t rgb;
    char     who[OPENMMO_HUD_NAME];
};

void view_ui_wins_init(struct view_ui_wins *w, struct view_hud *hud)
{
    int i;

    memset(w, 0, sizeof *w);
    w->hud = hud;
    w->dragging = -1;
    w->field.win = -1;
    w->confirm.act = -1;
    for (i = 0; i < VIEW_UI_WIN_N; i++) {
        w->order[i] = i;
        w->w[i].sel = -1;
    }
}

static void raise_win(struct view_ui_wins *w, int id)
{
    int i, at = -1;

    for (i = 0; i < VIEW_UI_WIN_N; i++)
        if (w->order[i] == id)
            at = i;
    if (at <= 0)
        return;
    for (i = at; i > 0; i--)
        w->order[i] = w->order[i - 1];
    w->order[0] = id;
}

static void field_blur(struct view_ui_wins *w)
{
    w->field.win = -1;
    w->field.len = 0;
    w->field.text[0] = '\0';
}

void view_ui_wins_toggle(struct view_ui_wins *w, int id)
{
    if (w == NULL || id < 0 || id >= VIEW_UI_WIN_N)
        return;
    if (w->w[id].open) {
        w->w[id].open = 0;
        if (w->field.win == id)
            field_blur(w);
        w->ctx.open = 0;
        return;
    }
    w->w[id].open = 1;
    w->w[id].scroll = 0;
    w->w[id].sel = -1;
    raise_win(w, id);
    /* The broker draws the guest's held page; opening it asks for the
     * first tab's page so there is one to hold. */
    if (id == VIEW_UI_WIN_GTL)
        view_ui_gtl_open(w->hud);
    /* The mailbox likewise: the table is the guest's held page. */
    if (id == VIEW_UI_WIN_MAIL)
        view_ui_mail_open(w->hud);
}

int view_ui_wins_typing(const struct view_ui_wins *w)
{
    if (w == NULL)
        return 0;
    if (w->w[VIEW_UI_WIN_GTL].open && view_ui_gtl_typing())
        return 1;
    if (w->w[VIEW_UI_WIN_MAIL].open && view_ui_mail_typing())
        return 1;
    return w->field.win >= 0;
}

/* Up while the session is in the world. A frame outliving the session would
 * be a list of people who are not there. */
static int wins_live(const struct view_ui_wins *w)
{
    if (w == NULL || w->hud == NULL || !w->hud->any)
        return 0;
    return w->hud->snap.net.state == OPENMMO_ST_IN_GAME;
}

static int any_open(const struct view_ui_wins *w)
{
    int i;

    for (i = 0; i < VIEW_UI_WIN_N; i++)
        if (w->w[i].open)
            return 1;
    return 0;
}

static int is_friend_of(const struct openmmo_hud_snap *s, const char *name)
{
    int i;

    for (i = 0; i < (int)s->friends_n && i < (int)OPENMMO_HUD_FRIEND_N; i++)
        if (strcmp(s->friends[i].name, name) == 0)
            return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* What each frame lists                                               */
/* ------------------------------------------------------------------ */

static int row_put(struct win_row *rows, int at, const char *left,
                   const char *right, uint32_t rgb, const char *who)
{
    if (at >= WIN_ROW_N)
        return at;
    snprintf(rows[at].left, sizeof rows[at].left, "%s",
             left != NULL ? left : "");
    snprintf(rows[at].right, sizeof rows[at].right, "%s",
             right != NULL ? right : "");
    snprintf(rows[at].who, sizeof rows[at].who, "%s",
             who != NULL ? who : "");
    rows[at].rgb = rgb;
    return at + 1;
}

static int rows_friends(const struct openmmo_hud_snap *s, int tab,
                        struct win_row *rows)
{
    int i, n = 0;

    /* The block list is send-only on this wire: openmmo_client_block goes
     * up, nothing comes down to list. The official client's own empty-table line. */
    if (tab == 1)
        return row_put(rows, 0, "No Entries.", "", VIEW_UI_COL_DIM, NULL);
    /* Online first, the way the official client's contacts list sorts, then the rest in
     * the order the server sent them. */
    for (i = 0; i < (int)s->friends_n && i < (int)OPENMMO_HUD_FRIEND_N; i++)
        if (s->friends[i].online)
            n = row_put(rows, n, s->friends[i].name, "Online",
                        VIEW_UI_COL_TEXT, s->friends[i].name);
    for (i = 0; i < (int)s->friends_n && i < (int)OPENMMO_HUD_FRIEND_N; i++)
        if (!s->friends[i].online)
            n = row_put(rows, n, s->friends[i].name, "Offline",
                        VIEW_UI_COL_DIM, s->friends[i].name);
    if (n == 0)
        n = row_put(rows, n, "No Entries.", "", VIEW_UI_COL_DIM, NULL);
    return n;
}

static int rows_team(const struct openmmo_hud_snap *s, struct win_row *rows)
{
    const struct openmmo_hud_guild *g = &s->guild;
    char title[OPENMMO_HUD_NAME + OPENMMO_HUD_TAG + 8];
    char count[32];
    int i, n = 0, online = 0;

    if (g->name[0] == '\0')
        return row_put(rows, 0, "This character is not in a team.", "",
                       VIEW_UI_COL_DIM, NULL);
    if (g->tag[0] != '\0')
        snprintf(title, sizeof title, "[%s] %s", g->tag, g->name);
    else
        snprintf(title, sizeof title, "%s", g->name);
    for (i = 0; i < (int)g->member_n && i < (int)OPENMMO_HUD_MEMBER_N; i++)
        if (g->member[i].online)
            online++;
    /* The official client's own caption (string 2712, its {00}/{01} filled). */
    snprintf(count, sizeof count, "Members: %d/%d", online, (int)g->member_n);
    n = row_put(rows, n, title, count, VIEW_UI_COL_ACCENT, NULL);
    if (g->motd[0] != '\0')
        n = row_put(rows, n, g->motd, "", VIEW_UI_COL_DIM, NULL);
    for (i = 0; i < (int)g->member_n && i < (int)OPENMMO_HUD_MEMBER_N; i++)
        if (g->member[i].online)
            n = row_put(rows, n, g->member[i].name, "Online",
                        VIEW_UI_COL_TEXT, g->member[i].name);
    for (i = 0; i < (int)g->member_n && i < (int)OPENMMO_HUD_MEMBER_N; i++)
        if (!g->member[i].online)
            n = row_put(rows, n, g->member[i].name, "Offline",
                        VIEW_UI_COL_DIM, g->member[i].name);
    return n;
}

static int rows_nearby(const struct openmmo_hud_snap *s, struct win_row *rows)
{
    char where[OPENMMO_HUD_NAME * 2 + 16];
    char tile[32];
    int i, n = 0;

    if (s->map.region[0] != '\0' || s->map.name[0] != '\0') {
        snprintf(where, sizeof where, "%s%s%s", s->map.region,
                 (s->map.region[0] != '\0' && s->map.name[0] != '\0') ? " "
                                                                     : "",
                 s->map.name);
        snprintf(tile, sizeof tile, "%u, %u", (unsigned)s->map.x,
                 (unsigned)s->map.y);
        n = row_put(rows, n, where, tile, VIEW_UI_COL_ACCENT, NULL);
    }
    for (i = 0; i < (int)s->map.peer_n && i < (int)OPENMMO_HUD_PEER_N; i++)
        n = row_put(rows, n, s->map.peer[i], "", VIEW_UI_COL_TEXT,
                    s->map.peer[i]);
    if (s->map.peer_n == 0)
        n = row_put(rows, n, "Nobody else is on this map.", "",
                    VIEW_UI_COL_DIM, NULL);
    return n;
}

static int rows_instance(const struct openmmo_hud_snap *s,
                         struct win_row *rows)
{
    int i, n = 0;

    /* The definition table those ids index is not on the wire (0xD3 / 0xD4
     * carry id, value and count only), so the id is what there is to show.
     * Naming them would be a guess printed as a fact. */
    for (i = 0; i < (int)s->objective_n && i < (int)OPENMMO_HUD_OBJECTIVE_N;
         i++) {
        char label[VIEW_UI_ROW_LEN];
        char value[32];

        snprintf(label, sizeof label, "Objective %d", (int)s->objective[i].id);
        if (s->objective[i].count > 0)
            snprintf(value, sizeof value, "%d  x%d",
                     (int)s->objective[i].value, (int)s->objective[i].count);
        else
            snprintf(value, sizeof value, "%d", (int)s->objective[i].value);
        n = row_put(rows, n, label, value, VIEW_UI_COL_TEXT, NULL);
    }
    if (n == 0)
        n = row_put(rows, n, "No instance is running.", "", VIEW_UI_COL_DIM,
                    NULL);
    return n;
}

static int win_rows(int id, int tab, const struct openmmo_hud_snap *s,
                    struct win_row *rows)
{
    switch (id) {
    case VIEW_UI_WIN_FRIENDS:  return rows_friends(s, tab, rows);
    case VIEW_UI_WIN_TEAM:     return rows_team(s, rows);
    case VIEW_UI_WIN_NEARBY:   return rows_nearby(s, rows);
    case VIEW_UI_WIN_INSTANCE: return rows_instance(s, rows);
    default:                   return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Acting on a pick                                                    */
/* ------------------------------------------------------------------ */

static void ctx_open(struct view_ui_wins *w, const char *who, int x, int y)
{
    if (who == NULL || who[0] == '\0')
        return;
    snprintf(w->ctx.who, sizeof w->ctx.who, "%s", who);
    w->ctx.at_x = x;
    w->ctx.at_y = y;
    w->ctx.is_friend = is_friend_of(&w->hud->snap, who);
    w->ctx.open = 1;
}

static void ctx_pick(struct view_ui_wins *w, int i)
{
    const struct view_ui_player_item *it = view_ui_player_item(i);

    if (it == NULL)
        return;
    if (it->act < 0) {
        /* Copy Name is the window's own verb: the clipboard is the host's,
         * which is the one half the guest cannot reach. */
        SDL_SetClipboardText(w->ctx.who);
    } else if (it->act == OPENMMO_HUD_ACT_TRADE) {
        /* An offer stands in front of the other player for a minute; a
         * question on this side first, the way the challenge asks. */
        w->confirm.act = it->act;
        w->confirm.gtl = 0;
        snprintf(w->confirm.who, sizeof w->confirm.who, "%s", w->ctx.who);
        snprintf(w->confirm.line, sizeof w->confirm.line,
                 "Offer %s a trade?", w->ctx.who);
    } else if (it->confirm && w->ctx.is_friend) {
        w->confirm.act = it->act;
        snprintf(w->confirm.who, sizeof w->confirm.who, "%s", w->ctx.who);
    } else {
        view_hud_push_player(w->hud, it->act, w->ctx.who);
    }
    w->ctx.open = 0;
}

static void field_submit(struct view_ui_wins *w)
{
    if (w->field.win < 0 || w->field.text[0] == '\0')
        return;
    if (w->field.win == VIEW_UI_WIN_FRIENDS && w->field.tab == 0) {
        /* ACT_FRIEND toggles on the guest, so a name already on the list
         * is left alone rather than quietly removed by an Add row. */
        if (!is_friend_of(&w->hud->snap, w->field.text))
            view_hud_push_player(w->hud, OPENMMO_HUD_ACT_FRIEND,
                                 w->field.text);
    } else if (w->field.win == VIEW_UI_WIN_FRIENDS && w->field.tab == 1) {
        view_hud_push_player(w->hud, OPENMMO_HUD_ACT_BLOCK, w->field.text);
    }
    w->field.len = 0;
    w->field.text[0] = '\0';
}

/* ------------------------------------------------------------------ */
/* Draw                                                                */
/* ------------------------------------------------------------------ */

/* The list keeps a counter row when it cannot show everything. */
static int shown_rows(int n, int rows)
{
    if (rows < 1)
        return 0;
    return n > rows ? rows - 1 : (n < rows ? n : rows);
}

static void draw_field(struct view_ui_wins *w, int id, SDL_Renderer *ren,
                       struct view_ui_gpu *g,
                       const struct view_ui_win_body *B, int tab)
{
    const char *label = view_ui_win_action_label(id, tab);
    int focused = w->field.win == id && w->field.tab == tab;
    char text[OPENMMO_HUD_NAME + 2];

    if (label == NULL || B->action.h < 1)
        return;
    view_ui_text_in(ren, g, &B->action_label, 0, 0, label,
                    focused ? VIEW_UI_COL_TEXT : VIEW_UI_COL_DIM);
    if (!view_ui_th(ren, g, VIEW_UI_TH_INPUT, &B->action_field)) {
        SDL_Color f = view_ui_col(VIEW_UI_COL_FIELD);
        SDL_Color l = view_ui_col(VIEW_UI_COL_LINE);

        view_ui_fill(ren, B->action_field.x, B->action_field.y,
                     B->action_field.w, B->action_field.h, f.r, f.g, f.b,
                     255);
        view_ui_border(ren, B->action_field.x, B->action_field.y,
                       B->action_field.w, B->action_field.h, l.r, l.g, l.b);
    }
    snprintf(text, sizeof text, "%s%s", focused ? w->field.text : "",
             focused ? "_" : "");
    view_ui_text_in(ren, g, &B->action_field, B->pad, 0, text,
                    VIEW_UI_COL_TEXT);
}

static void draw_one(struct view_ui_wins *w, int id, SDL_Renderer *ren,
                     struct view_ui_gpu *g, const struct view_ui_frame *f,
                     int mx, int my)
{
    static struct win_row rows[WIN_ROW_N];
    const struct view_ui_win_def *d = view_ui_win_def(id);
    struct view_ui_frame_layout L;
    struct view_ui_win_body B;
    struct openmmo_rect r;
    SDL_Rect saved;
    int n, i, top, y, tab, shown;

    if (d == NULL)
        return;
    view_ui_frame_place(&f->canvas, id, w->w[id].drag_x, w->w[id].drag_y, &L);
    if (!view_ui_font_ready(g, ren, L.text_px))
        return;
    tab = w->w[id].tab;
    view_ui_win_body_place(&L, id, tab, &B);

    n = win_rows(id, tab, &w->hud->snap, rows);
    shown = shown_rows(n, B.rows);
    w->w[id].scroll = view_ui_scroll_clamp(w->w[id].scroll, n, shown);
    top = w->w[id].scroll;

    view_ui_clip_push(ren, &L.box, &saved);
    /* The official client's own chrome, the metallic title band and the dark body are
     * one frame-draggable grid, with the primitive frame behind a run
     * without the art. */
    if (view_ui_th(ren, g, VIEW_UI_TH_FRAME, &L.box)) {
        view_ui_text_in(ren, g, &L.title, 0, 0, d->title, VIEW_UI_COL_TEXT);
        view_ui_th(ren, g, VIEW_UI_TH_CLOSE, &L.close);
    } else {
        view_ui_panel(ren, &L.box, VIEW_UI_COL_GROUND, VIEW_UI_COL_LINE);
        r = L.box;
        r.h = L.title_h;
        view_ui_panel(ren, &r, VIEW_UI_COL_FIELD, VIEW_UI_COL_LINE);
        view_ui_text_in(ren, g, &L.title, 0, 0, d->title, VIEW_UI_COL_TEXT);
        view_ui_panel(ren, &L.close, VIEW_UI_COL_FIELD, VIEW_UI_COL_LINE);
        {   /* the close cross, drawn because there is no art to blit */
            SDL_Color c = view_ui_col(VIEW_UI_COL_DIM);
            int k, m = L.close.w < L.close.h ? L.close.w : L.close.h;

            for (k = 3; k < m - 3; k++) {
                view_ui_fill(ren, L.close.x + k, L.close.y + k, 1, 1,
                             c.r, c.g, c.b, 255);
                view_ui_fill(ren, L.close.x + m - 1 - k, L.close.y + k, 1, 1,
                             c.r, c.g, c.b, 255);
            }
        }
    }

    /* The broker's body is its own module; only the chrome is shared. The
     * mailbox is the second of those. */
    if (id == VIEW_UI_WIN_GTL) {
        view_ui_gtl_draw(ren, g, &L, w->hud, mx, my);
        view_ui_clip_pop(ren, &saved);
        return;
    }
    if (id == VIEW_UI_WIN_MAIL) {
        view_ui_mail_draw(ren, g, &L, w->hud, mx, my);
        view_ui_clip_pop(ren, &saved);
        return;
    }

    /* The social window's two tabs (ui-tab art, the official client's own two titles). */
    for (i = 0; i < B.tab_n; i++) {
        const char *tl = view_ui_win_tab_label(id, i);
        int active = i == tab;
        int tw = view_ui_text_width(g, tl);
        int tx = B.tab[i].x + (B.tab[i].w - tw) / 2;

        if (!view_ui_th(ren, g,
                        active ? VIEW_UI_TH_TAB_ACTIVE : VIEW_UI_TH_TAB,
                        &B.tab[i]))
            view_ui_panel(ren, &B.tab[i],
                          active ? VIEW_UI_COL_LINE : VIEW_UI_COL_FIELD,
                          VIEW_UI_COL_LINE);
        if (tx < B.tab[i].x)
            tx = B.tab[i].x;
        view_ui_text(ren, g, tx, B.tab[i].y + (B.tab[i].h - g->px) / 2, tl,
                     view_ui_col(active ? VIEW_UI_COL_TEXT
                                        : VIEW_UI_COL_DIM));
    }

    /* The column headers, on the official client's header art. */
    if (B.header.h > 0) {
        const char *cl = NULL, *cr = NULL;
        struct openmmo_rect hl = B.header, hr = B.header;

        view_ui_win_columns(id, tab, &cl, &cr);
        hl.w = B.header.w * 3 / 5;
        hr.x = hl.x + hl.w;
        hr.w = B.header.w - hl.w;
        if (!view_ui_th(ren, g, VIEW_UI_TH_HEADER, &hl))
            view_ui_panel(ren, &hl, VIEW_UI_COL_FIELD, VIEW_UI_COL_LINE);
        if (cr != NULL && cr[0] != '\0' &&
            !view_ui_th(ren, g, VIEW_UI_TH_HEADER, &hr))
            view_ui_panel(ren, &hr, VIEW_UI_COL_FIELD, VIEW_UI_COL_LINE);
        view_ui_text_in(ren, g, &hl, B.pad, 0, cl, VIEW_UI_COL_DIM);
        if (cr != NULL)
            view_ui_text_in(ren, g, &hr, B.pad, 1, cr, VIEW_UI_COL_DIM);
    }

    y = B.list.y;
    for (i = top; i < n && i < top + shown; i++) {
        int hot;

        r.x = B.list.x;
        r.y = y;
        r.w = B.list.w;
        r.h = B.row_h;
        hot = rows[i].who[0] != '\0' && view_ui_hit(&r, mx, my);
        /* A row that names a player answers the pointer with the official client's own
         * table-row art; the selected row keeps it. */
        if ((hot || w->w[id].sel == i) &&
            !view_ui_th(ren, g, VIEW_UI_TH_ROW, &r)) {
            SDL_Color b = view_ui_col(VIEW_UI_COL_FIELD);

            view_ui_fill(ren, r.x, r.y, r.w, r.h, b.r, b.g, b.b, 255);
        }
        view_ui_text_in(ren, g, &r, B.pad, 0, rows[i].left, rows[i].rgb);
        view_ui_text_in(ren, g, &r, B.pad, 1, rows[i].right, rows[i].rgb);
        y += B.row_h;
    }
    if (n > B.rows && B.rows > 0) {
        char more[48];

        snprintf(more, sizeof more, "%d-%d of %d", top + 1,
                 top + shown < n ? top + shown : n, n);
        r.x = B.list.x;
        r.y = B.list.y + B.list.h - B.row_h;
        r.w = B.list.w;
        r.h = B.row_h;
        view_ui_text_in(ren, g, &r, B.pad, 1, more, VIEW_UI_COL_DIM);
    }

    draw_field(w, id, ren, g, &B, tab);
    view_ui_clip_pop(ren, &saved);
}

static void draw_ctx(struct view_ui_wins *w, SDL_Renderer *ren,
                     struct view_ui_gpu *g, const struct view_ui_frame *f,
                     int mx, int my)
{
    struct view_ui_menu_layout M;
    SDL_Rect saved;
    int i;

    if (!w->ctx.open)
        return;
    view_ui_player_place(&f->canvas, w->ctx.at_x, w->ctx.at_y,
                         view_ui_measure_gpu, g, w->ctx.is_friend, &M);
    w->ctx.laid = M;
    w->ctx.laid_ok = 1;
    view_ui_clip_push(ren, &M.box, &saved);
    if (!view_ui_th(ren, g, VIEW_UI_TH_PANEL, &M.box))
        view_ui_panel(ren, &M.box, VIEW_UI_COL_GROUND, VIEW_UI_COL_ACCENT);
    for (i = 0; i < M.n; i++) {
        const char *label = view_ui_player_label(i, w->ctx.is_friend);
        int hot = view_ui_hit(&M.row[i], mx, my);
        int tw = view_ui_text_width(g, label);
        int x = M.row[i].x + (M.row[i].w - tw) / 2;

        if (hot && !view_ui_th(ren, g, VIEW_UI_TH_ROW_HOVER, &M.row[i])) {
            SDL_Color c = view_ui_col(VIEW_UI_COL_LINE);

            view_ui_fill(ren, M.row[i].x, M.row[i].y, M.row[i].w, M.row[i].h,
                         c.r, c.g, c.b, 255);
        }
        if (x < M.row[i].x + M.pad)
            x = M.row[i].x + M.pad;
        view_ui_text(ren, g, x, M.row[i].y + (M.row[i].h - g->px) / 2, label,
                     view_ui_col(VIEW_UI_COL_TEXT));
    }
    view_ui_clip_pop(ren, &saved);
}

static void draw_confirm(struct view_ui_wins *w, SDL_Renderer *ren,
                         struct view_ui_gpu *g, const struct view_ui_frame *f,
                         int mx, int my)
{
    struct view_ui_confirm_layout C;
    char text[VIEW_UI_ROW_LEN];
    char wrapped[4][VIEW_UI_ROW_LEN];
    uint32_t tags[4];
    SDL_Rect saved;
    int i, rows, line_h;

    if (w->confirm.act < 0)
        return;
    if (w->confirm.line[0] != '\0')
        snprintf(text, sizeof text, "%s", w->confirm.line);
    else
        view_ui_player_confirm_text(w->confirm.who, text, sizeof text);
    view_ui_confirm_place(&f->canvas, view_ui_text_width(g, text) / 2, &C);
    view_ui_clip_push(ren, &C.box, &saved);
    if (!view_ui_th(ren, g, VIEW_UI_TH_WARN, &C.box))
        view_ui_panel(ren, &C.box, VIEW_UI_COL_GROUND, VIEW_UI_COL_ACCENT);
    line_h = g->px + g->px / 3;
    rows = view_ui_wrap(view_ui_measure_gpu, g, C.text.w, 0, text, wrapped,
                        tags, 0, 4);
    for (i = 0; i < rows && i * line_h + line_h <= C.text.h + line_h; i++)
        view_ui_text(ren, g, C.text.x, C.text.y + i * line_h, wrapped[i],
                     view_ui_col(VIEW_UI_COL_TEXT));
    for (i = 0; i < 2; i++) {
        const struct openmmo_rect *b = i == 0 ? &C.yes : &C.no;
        const char *label = i == 0 ? "Yes" : "No";
        int hot = view_ui_hit(b, mx, my);
        int tw = view_ui_text_width(g, label);

        if (view_ui_th(ren, g, VIEW_UI_TH_BUTTON, b)) {
            if (hot)
                view_ui_th(ren, g, VIEW_UI_TH_BUTTON_HOVER, b);
        } else {
            view_ui_panel(ren, b, hot ? VIEW_UI_COL_LINE : VIEW_UI_COL_FIELD,
                          hot ? VIEW_UI_COL_ACCENT : VIEW_UI_COL_LINE);
        }
        view_ui_text(ren, g, b->x + (b->w - tw) / 2,
                     b->y + (b->h - g->px) / 2, label,
                     view_ui_col(VIEW_UI_COL_TEXT));
    }
    view_ui_clip_pop(ren, &saved);
}

static void wins_draw(void *state, SDL_Renderer *ren, struct view_ui_gpu *g,
                      const struct view_ui_frame *f)
{
    struct view_ui_wins *w = state;
    SDL_BlendMode old;
    int i, want, mx = 0, my = 0, over = 0;

    if (!wins_live(w) || !any_open(w))
        return;

    SDL_GetMouseState(&mx, &my);
    for (i = 0; i < VIEW_UI_WIN_N; i++) {
        struct view_ui_frame_layout L;

        if (!w->w[i].open)
            continue;
        view_ui_frame_place(&f->canvas, i, w->w[i].drag_x, w->w[i].drag_y, &L);
        if (view_ui_hit(&L.box, mx, my))
            over = 1;
    }
    want = (over || w->ctx.open || w->confirm.act >= 0 ||
            w->field.win >= 0) ? 240 : 208;
    if (w->hud->snap.guest_busy)
        want = 64;
    w->alpha = view_ui_fade(w->alpha, want, 12, &w->faded);
    view_ui_alpha((unsigned)w->alpha);

    SDL_GetRenderDrawBlendMode(ren, &old);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    /* Back to front: order[0] is the topmost, so it is drawn last; the
     * player menu and the confirm are over every frame. */
    for (i = VIEW_UI_WIN_N - 1; i >= 0; i--)
        if (w->w[w->order[i]].open)
            draw_one(w, w->order[i], ren, g, f, mx, my);
    draw_ctx(w, ren, g, f, mx, my);
    view_ui_alpha(255);
    draw_confirm(w, ren, g, f, mx, my);
    SDL_SetRenderDrawBlendMode(ren, old);
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

static int confirm_event(struct view_ui_wins *w, const SDL_Event *ev,
                         const struct view_ui_frame *f)
{
    struct view_ui_confirm_layout C;
    char text[VIEW_UI_ROW_LEN];

    if (w->confirm.act < 0)
        return 0;
    if (ev->type == SDL_KEYDOWN && !ev->key.repeat) {
        SDL_Keycode k = ev->key.keysym.sym;

        if (k == SDLK_ESCAPE || k == SDLK_n) {
            w->confirm.act = -1;
            w->confirm.gtl = 0;
            w->confirm.line[0] = '\0';
            return 1;
        }
        if (k == SDLK_y || k == SDLK_RETURN) {
            if (w->confirm.gtl)
                view_hud_push(w->hud, OPENMMO_HUD_CMD_GTL,
                              (int32_t)w->confirm.gtl_arg);
            else
                view_hud_push_player(w->hud, w->confirm.act, w->confirm.who);
            w->confirm.act = -1;
            w->confirm.gtl = 0;
            w->confirm.line[0] = '\0';
            return 1;
        }
        return 1; /* modal: no chord underneath */
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN &&
        ev->button.button == SDL_BUTTON_LEFT) {
        if (w->confirm.line[0] != '\0')
            snprintf(text, sizeof text, "%s", w->confirm.line);
        else
            view_ui_player_confirm_text(w->confirm.who, text, sizeof text);
        view_ui_confirm_place(&f->canvas, 0, &C);
        if (view_ui_hit(&C.yes, ev->button.x, ev->button.y)) {
            if (w->confirm.gtl)
                view_hud_push(w->hud, OPENMMO_HUD_CMD_GTL,
                              (int32_t)w->confirm.gtl_arg);
            else
                view_hud_push_player(w->hud, w->confirm.act, w->confirm.who);
        }
        if (view_ui_hit(&C.yes, ev->button.x, ev->button.y) ||
            view_ui_hit(&C.no, ev->button.x, ev->button.y) ||
            !view_ui_hit(&C.box, ev->button.x, ev->button.y)) {
            w->confirm.act = -1;
            w->confirm.gtl = 0;
            w->confirm.line[0] = '\0';
        }
        return 1;
    }
    return ev->type == SDL_MOUSEBUTTONUP || ev->type == SDL_MOUSEWHEEL;
}

static int ctx_event(struct view_ui_wins *w, const SDL_Event *ev,
                     const struct view_ui_frame *f)
{
    struct view_ui_menu_layout M;

    if (!w->ctx.open)
        return 0;
    if (ev->type == SDL_KEYDOWN && !ev->key.repeat &&
        ev->key.keysym.sym == SDLK_ESCAPE) {
        w->ctx.open = 0;
        return 1;
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN &&
        ev->button.button == SDL_BUTTON_LEFT) {
        int row;

        if (w->ctx.laid_ok)
            M = w->ctx.laid;
        else
            view_ui_player_place(&f->canvas, w->ctx.at_x, w->ctx.at_y, NULL,
                                 NULL, w->ctx.is_friend, &M);
        row = view_ui_menu_hit(&M, ev->button.x, ev->button.y);
        if (row >= 0) {
            ctx_pick(w, row);
            return 1;
        }
        w->ctx.open = 0;
        /* The click that dismissed it still lands below, the way the bar's
         * popup lets a second button swap menus in one press. */
        return 0;
    }
    return 0;
}

static int field_event(struct view_ui_wins *w, const SDL_Event *ev)
{
    if (w->field.win < 0)
        return 0;
    if (ev->type == SDL_TEXTINPUT) {
        const char *t = ev->text.text;

        SDL_StartTextInput();
        for (; *t != '\0'; t++) {
            unsigned char c = (unsigned char)*t;

            if (c < 32 || c > 126)
                continue;
            if (w->field.len + 1 < (int)sizeof w->field.text) {
                w->field.text[w->field.len++] = (char)c;
                w->field.text[w->field.len] = '\0';
            }
        }
        return 1;
    }
    if (ev->type == SDL_KEYDOWN) {
        SDL_Keycode k = ev->key.keysym.sym;

        if (k == SDLK_BACKSPACE) {
            if (w->field.len > 0)
                w->field.len = view_ui_utf8_trunc(w->field.text);
            return 1;
        }
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
            field_submit(w);
            return 1;
        }
        if (k == SDLK_ESCAPE) {
            field_blur(w);
            return 1;
        }
        /* Letters land as SDL_TEXTINPUT; the keydown must not also be a
         * HUD chord while the field is focused. */
        return k >= SDLK_a && k <= SDLK_z;
    }
    return 0;
}

static int wins_event(void *state, const SDL_Event *ev,
                      const struct view_ui_frame *f)
{
    struct view_ui_wins *w = state;
    struct view_ui_frame_layout L;
    struct view_ui_win_body B;
    int i, id, mx = 0, my = 0;

    if (!wins_live(w) || ev == NULL)
        return 0;

    if (confirm_event(w, ev, f))
        return 1;
    if (ctx_event(w, ev, f))
        return 1;
    if (field_event(w, ev))
        return 1;
    if (w->w[VIEW_UI_WIN_GTL].open &&
        (ev->type == SDL_KEYDOWN || ev->type == SDL_TEXTINPUT)) {
        struct view_ui_frame_layout GL;

        view_ui_frame_place(&f->canvas, VIEW_UI_WIN_GTL,
                            w->w[VIEW_UI_WIN_GTL].drag_x,
                            w->w[VIEW_UI_WIN_GTL].drag_y, &GL);
        if (view_ui_gtl_event(ev, &GL, w->hud))
            return 1;
    }
    if (w->w[VIEW_UI_WIN_MAIL].open &&
        (ev->type == SDL_KEYDOWN || ev->type == SDL_TEXTINPUT)) {
        struct view_ui_frame_layout ML;

        view_ui_frame_place(&f->canvas, VIEW_UI_WIN_MAIL,
                            w->w[VIEW_UI_WIN_MAIL].drag_x,
                            w->w[VIEW_UI_WIN_MAIL].drag_y, &ML);
        if (view_ui_mail_event(ev, &ML, w->hud))
            return 1;
    }

    if (ev->type == SDL_MOUSEMOTION && w->dragging >= 0) {
        w->w[w->dragging].drag_x += ev->motion.xrel;
        w->w[w->dragging].drag_y += ev->motion.yrel;
        return 1;
    }
    if (ev->type == SDL_MOUSEBUTTONUP && w->dragging >= 0) {
        w->dragging = -1;
        return 1;
    }
    if (!any_open(w))
        return 0;

    if (ev->type == SDL_MOUSEWHEEL) {
        SDL_GetMouseState(&mx, &my);
        for (i = 0; i < VIEW_UI_WIN_N; i++) {
            id = w->order[i];
            if (!w->w[id].open)
                continue;
            view_ui_frame_place(&f->canvas, id, w->w[id].drag_x,
                                w->w[id].drag_y, &L);
            if (!view_ui_hit(&L.box, mx, my))
                continue;
            w->w[id].scroll -= ev->wheel.y > 0 ? 3 : -3;
            if (w->w[id].scroll < 0)
                w->w[id].scroll = 0;
            return 1;
        }
        return 0;
    }

    if (ev->type == SDL_MOUSEBUTTONDOWN &&
        ev->button.button == SDL_BUTTON_LEFT) {
        for (i = 0; i < VIEW_UI_WIN_N; i++) {
            static struct win_row rows[WIN_ROW_N];
            int hit, tab, t, row;

            id = w->order[i];
            if (!w->w[id].open)
                continue;
            view_ui_frame_place(&f->canvas, id, w->w[id].drag_x,
                                w->w[id].drag_y, &L);
            hit = view_ui_frame_hit(&L, ev->button.x, ev->button.y);
            if (hit == VIEW_UI_FRAME_HIT_NONE &&
                !view_ui_hit(&L.box, ev->button.x, ev->button.y))
                continue;
            raise_win(w, id);
            if (id == VIEW_UI_WIN_GTL || id == VIEW_UI_WIN_MAIL) {
                if (hit == VIEW_UI_FRAME_HIT_CLOSE) {
                    w->w[id].open = 0;
                    return 1;
                }
                if (hit == VIEW_UI_FRAME_HIT_TITLE) {
                    w->dragging = id;
                    return 1;
                }
                if (id == VIEW_UI_WIN_GTL)
                    view_ui_gtl_event(ev, &L, w->hud);
                else
                    view_ui_mail_event(ev, &L, w->hud);
                return 1;
            }
            tab = w->w[id].tab;
            view_ui_win_body_place(&L, id, tab, &B);
            if (w->field.win >= 0 &&
                !view_ui_hit(&B.action_field, ev->button.x, ev->button.y))
                field_blur(w);
            if (hit == VIEW_UI_FRAME_HIT_CLOSE) {
                w->w[id].open = 0;
                return 1;
            }
            t = view_ui_win_tab_hit(&B, ev->button.x, ev->button.y);
            if (t >= 0 && t != tab) {
                w->w[id].tab = t;
                w->w[id].scroll = 0;
                w->w[id].sel = -1;
                return 1;
            }
            if (B.action.h > 0 &&
                view_ui_hit(&B.action_field, ev->button.x, ev->button.y)) {
                w->field.win = id;
                w->field.tab = tab;
                w->field.len = 0;
                w->field.text[0] = '\0';
                SDL_StartTextInput();
                return 1;
            }
            row = view_ui_win_row_hit(&B, ev->button.x, ev->button.y);
            if (row >= 0) {
                int n = win_rows(id, tab, &w->hud->snap, rows);
                int at = w->w[id].scroll + row;

                if (at < n && at < w->w[id].scroll +
                                       shown_rows(n, B.rows)) {
                    w->w[id].sel = at;
                    /* A click on a player's row raises the official client's player
                     * menu at the pointer, the way the social table's
                     * own rows do (f/tJ). */
                    ctx_open(w, rows[at].who, ev->button.x, ev->button.y);
                }
                return 1;
            }
            if (hit == VIEW_UI_FRAME_HIT_TITLE)
                w->dragging = id;
            return 1;
        }
        return 0;
    }

    /* Escape closes the frontmost frame before the window takes it, the way
     * it already cancels a text field first. */
    if (ev->type == SDL_KEYDOWN && !ev->key.repeat && !f->typing &&
        ev->key.keysym.sym == SDLK_ESCAPE) {
        for (i = 0; i < VIEW_UI_WIN_N; i++)
            if (w->w[w->order[i]].open) {
                w->w[w->order[i]].open = 0;
                return 1;
            }
    }
    return 0;
}

static int wins_owns_pointer(void *state, const struct view_ui_frame *f,
                             int mx, int my)
{
    struct view_ui_wins *w = state;
    struct view_ui_frame_layout L;
    int i;

    if (!wins_live(w))
        return 0;
    if (w->dragging >= 0 || w->confirm.act >= 0)
        return 1;
    if (w->ctx.open) {
        struct view_ui_menu_layout M;

        if (w->ctx.laid_ok)
            M = w->ctx.laid;
        else
            view_ui_player_place(&f->canvas, w->ctx.at_x, w->ctx.at_y, NULL,
                                 NULL, w->ctx.is_friend, &M);
        if (view_ui_hit(&M.box, mx, my))
            return 1;
    }
    for (i = 0; i < VIEW_UI_WIN_N; i++) {
        if (!w->w[i].open)
            continue;
        view_ui_frame_place(&f->canvas, i, w->w[i].drag_x, w->w[i].drag_y, &L);
        if (view_ui_hit(&L.box, mx, my))
            return 1;
    }
    return 0;
}

struct view_ui_element view_ui_wins_element(struct view_ui_wins *w)
{
    struct view_ui_element el;

    memset(&el, 0, sizeof el);
    el.name = "wins";
    el.state = w;
    el.draw = wins_draw;
    el.event = wins_event;
    el.owns_pointer = wins_owns_pointer;
    return el;
}
