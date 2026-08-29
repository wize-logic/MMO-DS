/*
 * See view_ui_mail.h. The official client's mail screen, rebuilt over the .hud page: f/kW0
 * is the box (the ten-row table and its pager), f/rQ0 the screen around it (the reader and the
 * Recipient / Subject / Body form that becomes f/Ap1).
 */

#include "view_ui_mail.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

enum {
    MTAB_INBOX = 0, /* 5827 "Mail" */
    MTAB_SENT,      /* 5848 "Sent Mail" */
    MTAB_SEND,      /* 5834 "Send Mail" */
    MTAB_N
};

enum {
    MF_NONE = -1,
    MF_TO = 0,      /* 5828 "Recipient" */
    MF_SUBJECT,     /* 5829 "Subject" */
    MF_BODY,        /* 5830 "Body" */
    MF_N
};

/* The wire's own limits (game.h): subject 3..40, body 3..2000, and a name is
 * the page's. One more byte each for the terminator. */
#define MAIL_TO_LEN      OPENMMO_HUD_NAME
#define MAIL_SUBJECT_LEN 41
#define MAIL_BODY_LEN    2001
#define MAIL_BODY_ROWS   64

/* The official client prints the box against a hard 250 (string 5839, f/kW0). */
#define MAIL_CAP 250

struct mail_state {
    int tab;
    int page[2];            /* inbox, sent */
    int focus;              /* MF_*, or MF_NONE */
    char to[MAIL_TO_LEN];
    char subject[MAIL_SUBJECT_LEN];
    char body[MAIL_BODY_LEN];
    /* The letter being read. `want` is what was asked for; `open` is set
     * once the guest's snapshot carries that same id back. */
    uint32_t want_lo, want_hi;
    int reading;
    int letter_scroll;
    /* The official client confirms a send with string 5903 before it sends (f/rQ0). */
    int confirm;
    /* The answer strip: one sentence per 0x96, a few seconds each. */
    uint32_t toast_seq;
    char toast[192];
    uint32_t toast_until;
    /* Hit rects rebuilt every draw, answered by the next click. */
    struct openmmo_rect r_tab[MTAB_N];
    struct openmmo_rect r_row[OPENMMO_HUD_MAIL_ROWS];
    struct openmmo_rect r_del[OPENMMO_HUD_MAIL_ROWS];
    int row_n;
    struct openmmo_rect r_pager[10];
    int pager_page[10];
    int pager_n;
    struct openmmo_rect r_field[MF_N];
    struct openmmo_rect r_send, r_clear;
    struct openmmo_rect r_back, r_reply, r_paper, r_letter_del;
    struct openmmo_rect r_yes, r_no;
    int laid;
};

static struct mail_state M = { .focus = MF_NONE };

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

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

static char *field_buf(int id)
{
    switch (id) {
    case MF_TO:      return M.to;
    case MF_SUBJECT: return M.subject;
    case MF_BODY:    return M.body;
    default:         return NULL;
    }
}

static size_t field_cap(int id)
{
    switch (id) {
    case MF_TO:      return MAIL_TO_LEN;
    case MF_SUBJECT: return MAIL_SUBJECT_LEN;
    case MF_BODY:    return MAIL_BODY_LEN;
    default:         return 0;
    }
}

static void field_focus(int id)
{
    M.focus = id;
    if (id != MF_NONE)
        SDL_StartTextInput();
}

/* Which box a tab reads. The send form keeps whichever the player left. */
static int tab_sent(int tab)
{
    return tab == MTAB_SENT;
}

static void mail_ask(struct view_hud *hud, int tab, int page)
{
    int sent = tab_sent(tab);

    if (page < 0)
        page = 0;
    M.page[sent] = page;
    view_hud_push(hud, OPENMMO_HUD_CMD_MAIL,
                  (int32_t)(OPENMMO_HUD_MAIL_ASK |
                            ((uint32_t)(sent ? 1 : 0) << 8) |
                            ((uint32_t)(page & 0xFF) << 16)));
}

static void mail_verb(struct view_hud *hud, unsigned verb, int row)
{
    view_hud_push(hud, OPENMMO_HUD_CMD_MAIL,
                  (int32_t)(verb | ((uint32_t)(row & 0xFF) << 8)));
}

static void mail_send(struct view_hud *hud)
{
    struct openmmo_hud_mail_send m;

    memset(&m, 0, sizeof m);
    snprintf(m.to, sizeof m.to, "%s", M.to);
    snprintf(m.subject, sizeof m.subject, "%s", M.subject);
    snprintf(m.body, sizeof m.body, "%s", M.body);
    if (hud != NULL && hud->page != NULL)
        memcpy((void *)&hud->page->cmd_mail, &m, sizeof m);
    view_hud_push(hud, OPENMMO_HUD_CMD_MAIL, OPENMMO_HUD_MAIL_SEND);
}

/* The official client's own answer sentences: string 5800 + the result byte (f/oP1 prints
 * nV0.Id1(sj1.jp1), and sj1's table is jp1 = 5800 + Y90 for every code). */
static const char *result_line(int code)
{
    switch (code) {
    case 0:  return "Your mail has been sent successfully.";
    case 1:  return "You cannot send mail to that user as they have blocked you.";
    case 2:  return "You cannot send mail to that user as their mailbox is full.";
    case 4:  return "The requested recipient could not be found.";
    case 5:  return "The subject must be between 3-40 characters.";
    case 6:  return "The body must be between 3-2000 characters.";
    case 8:  return "The requested attached item could not be found.";
    case 15: return "You cannot send mail to yourself.";
    case 18: return "You cannot send mail as your mailbox is full.";
    case 19: return "The requested recipient has not been online recently.";
    case 20: return "The requested mail was not found.";
    default: return "An unknown error occurred.";
    }
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

static void draw_field(SDL_Renderer *ren, struct view_ui_gpu *g,
                       const struct openmmo_rect *r, int id, int pad)
{
    char text[MAIL_SUBJECT_LEN + 2];
    const char *src = field_buf(id);
    int focused = M.focus == id;

    if (!view_ui_th(ren, g, VIEW_UI_TH_INPUT, r)) {
        SDL_Color f = view_ui_col(VIEW_UI_COL_FIELD);
        SDL_Color l = view_ui_col(focused ? VIEW_UI_COL_ACCENT
                                          : VIEW_UI_COL_LINE);

        view_ui_fill(ren, r->x, r->y, r->w, r->h, f.r, f.g, f.b, 255);
        view_ui_border(ren, r->x, r->y, r->w, r->h, l.r, l.g, l.b);
    }
    snprintf(text, sizeof text, "%s%s", src != NULL ? src : "",
             focused ? "_" : "");
    view_ui_text_in(ren, g, r, pad, 0, text, VIEW_UI_COL_TEXT);
}

/* Wrap `s` into the box and draw it, `skip` rows down. Returns how many rows
 * the whole text needed, so a caller can bound its own scroll. */
static int draw_wrapped(SDL_Renderer *ren, struct view_ui_gpu *g,
                        const struct openmmo_rect *box, const char *s,
                        int skip, int caret)
{
    char rows[MAIL_BODY_ROWS][VIEW_UI_ROW_LEN];
    uint32_t tags[MAIL_BODY_ROWS];
    int n = 0, i, line_h = g->px + 3, drawn;
    const char *p = s != NULL ? s : "";
    SDL_Rect saved;

    /* A typed body keeps its own newlines: wrap each of them apart. */
    while (n < MAIL_BODY_ROWS) {
        const char *nl = strchr(p, '\n');
        char one[VIEW_UI_ROW_LEN * 4];
        size_t len = nl != NULL ? (size_t)(nl - p) : strlen(p);

        if (len >= sizeof one)
            len = sizeof one - 1;
        memcpy(one, p, len);
        one[len] = '\0';
        n = view_ui_wrap(view_ui_measure_gpu, g, box->w - 4, 0, one, rows,
                         tags, n, MAIL_BODY_ROWS);
        if (nl == NULL)
            break;
        p = nl + 1;
    }
    if (caret && n < MAIL_BODY_ROWS) {
        size_t at = strlen(rows[n > 0 ? n - 1 : 0]);

        if (n > 0 && at + 2 < VIEW_UI_ROW_LEN) {
            rows[n - 1][at] = '_';
            rows[n - 1][at + 1] = '\0';
        } else if (n == 0) {
            snprintf(rows[n++], VIEW_UI_ROW_LEN, "_");
        }
    }
    view_ui_clip_push(ren, box, &saved);
    drawn = 0;
    for (i = skip; i < n; i++) {
        int y = box->y + 2 + drawn * line_h;

        if (y + line_h > box->y + box->h)
            break;
        view_ui_text(ren, g, box->x + 2, y, rows[i],
                     view_ui_col(VIEW_UI_COL_TEXT));
        drawn++;
    }
    view_ui_clip_pop(ren, &saved);
    return n;
}

/* ------------------------------------------------------------------ */
/* Open                                                                */
/* ------------------------------------------------------------------ */

void view_ui_mail_open(struct view_hud *hud)
{
    M.tab = MTAB_INBOX;
    M.reading = 0;
    M.confirm = 0;
    M.letter_scroll = 0;
    field_focus(MF_NONE);
    mail_ask(hud, MTAB_INBOX, 0);
}

/* ------------------------------------------------------------------ */
/* Draw                                                                */
/* ------------------------------------------------------------------ */

static void draw_table(SDL_Renderer *ren, struct view_ui_gpu *g,
                       const struct openmmo_hud_mail *ms,
                       struct openmmo_rect area, int mx, int my)
{
    int pad = 4, head_h = view_ui_scale(20, g->px);
    int row_h = view_ui_scale(22, g->px);
    int i, y = area.y;
    int del_w = view_ui_text_width(g, "Delete") + 12;
    int date_w = view_ui_text_width(g, "Sep 00, 0000") + 12;
    int who_w = (area.w - del_w - date_w) / 3;
    struct openmmo_rect head = { area.x, y, area.w, head_h };

    if (!view_ui_th(ren, g, VIEW_UI_TH_HEADER, &head))
        view_ui_panel(ren, &head, VIEW_UI_COL_FIELD, VIEW_UI_COL_LINE);
    {
        struct openmmo_rect c = { area.x, y, who_w, head_h };

        view_ui_text_in(ren, g, &c, pad, 0,
                        ms->listed_sent ? "Recipient" : "Sender",
                        VIEW_UI_COL_DIM);
        c.x += who_w;
        c.w = area.w - who_w - date_w - del_w;
        view_ui_text_in(ren, g, &c, pad, 0, "Subject", VIEW_UI_COL_DIM);
        c.x += c.w;
        c.w = date_w;
        view_ui_text_in(ren, g, &c, pad, 0, "Date", VIEW_UI_COL_DIM);
    }
    y += head_h;

    M.row_n = 0;
    for (i = 0; i < (int)ms->row_n && i < OPENMMO_HUD_MAIL_ROWS; i++) {
        const struct openmmo_hud_mail_row *r = &ms->row[i];
        struct openmmo_rect rr = { area.x, y, area.w, row_h };
        struct openmmo_rect c;
        char date[24];
        int hot;

        if (y + row_h > area.y + area.h)
            break;
        hot = view_ui_hit(&rr, mx, my);
        if (hot) {
            if (!view_ui_th(ren, g, VIEW_UI_TH_ROW_HOVER, &rr))
                view_ui_panel(ren, &rr, VIEW_UI_COL_LINE, VIEW_UI_COL_LINE);
        } else if (!view_ui_th(ren, g, VIEW_UI_TH_ROW, &rr))
            view_ui_panel(ren, &rr, VIEW_UI_COL_GROUND, VIEW_UI_COL_LINE);

        c = rr;
        c.w = who_w;
        view_ui_text_in(ren, g, &c, pad, 0, r->who,
                        r->unread ? VIEW_UI_COL_ACCENT : VIEW_UI_COL_TEXT);
        c.x += who_w;
        c.w = area.w - who_w - date_w - del_w;
        view_ui_text_in(ren, g, &c, pad, 0, r->subject,
                        r->unread ? VIEW_UI_COL_ACCENT : VIEW_UI_COL_TEXT);
        c.x += c.w;
        c.w = date_w;
        fmt_date(date, sizeof date, r->epoch);
        view_ui_text_in(ren, g, &c, pad, 0, date, VIEW_UI_COL_DIM);

        M.r_row[i] = rr;
        M.r_del[i].x = area.x + area.w - del_w;
        M.r_del[i].y = y + 1;
        M.r_del[i].w = del_w - 2;
        M.r_del[i].h = row_h - 2;
        draw_btn(ren, g, &M.r_del[i], "Delete", mx, my, 1);
        M.row_n++;
        y += row_h;
    }
    if (ms->row_n == 0) {
        struct openmmo_rect e = { area.x, y, area.w, row_h };

        view_ui_text_in(ren, g, &e, pad, 0, "No mail.", VIEW_UI_COL_DIM);
    }
}

static void draw_pager(SDL_Renderer *ren, struct view_ui_gpu *g,
                       const struct openmmo_hud_mail *ms,
                       struct openmmo_rect band, int mx, int my)
{
    unsigned total = ms->listed_sent ? ms->sent : ms->inbox;
    int pages = (int)((total + OPENMMO_HUD_MAIL_ROWS - 1) /
                      OPENMMO_HUD_MAIL_ROWS);
    int here = (int)ms->page;
    int first, i, x = band.x, bw = view_ui_scale(28, g->px);

    M.pager_n = 0;
    if (pages < 1)
        pages = 1;
    /* The official client's own pager shows eight numbers around the page it is on. */
    first = here - 3;
    if (first + 8 > pages)
        first = pages - 8;
    if (first < 0)
        first = 0;

    {
        struct openmmo_rect r = { x, band.y, bw, band.h };

        draw_btn(ren, g, &r, "<<", mx, my, here > 0);
        M.r_pager[M.pager_n] = r;
        M.pager_page[M.pager_n++] = here - 1;
        x += bw + 2;
    }
    for (i = first; i < pages && i < first + 8 && M.pager_n < 9; i++) {
        struct openmmo_rect r = { x, band.y, bw, band.h };
        char n[12];

        snprintf(n, sizeof n, "%d", i + 1);
        if (i == here) {
            if (!view_ui_th(ren, g, VIEW_UI_TH_TAB_ACTIVE, &r))
                view_ui_panel(ren, &r, VIEW_UI_COL_LINE, VIEW_UI_COL_LINE);
            view_ui_text_in(ren, g, &r, 0, 0, n, VIEW_UI_COL_TEXT);
        } else
            draw_btn(ren, g, &r, n, mx, my, 1);
        M.r_pager[M.pager_n] = r;
        M.pager_page[M.pager_n++] = i;
        x += bw + 2;
    }
    {
        struct openmmo_rect r = { x, band.y, bw, band.h };

        draw_btn(ren, g, &r, ">>", mx, my, here + 1 < pages);
        M.r_pager[M.pager_n] = r;
        M.pager_page[M.pager_n++] = here + 1;
    }
}

static void draw_letter(SDL_Renderer *ren, struct view_ui_gpu *g,
                        const struct openmmo_hud_mail *ms,
                        struct openmmo_rect area, int mx, int my)
{
    int line_h = g->px + 6, pad = 4;
    int btn_h = view_ui_scale(24, g->px);
    int btn_w = view_ui_scale(84, g->px);
    struct openmmo_rect r = { area.x, area.y, area.w, line_h };
    struct openmmo_rect body;
    char date[24];
    char head[OPENMMO_HUD_NAME + 32];
    int rows, fit;

    view_ui_text_in(ren, g, &r, pad, 0, ms->open_subject, VIEW_UI_COL_TEXT);
    r.y += line_h;
    fmt_date(date, sizeof date, ms->open_epoch);
    snprintf(head, sizeof head, "%s %s   %s",
             ms->open_sent ? "To" : "From", ms->open_who, date);
    view_ui_text_in(ren, g, &r, pad, 0, head, VIEW_UI_COL_DIM);
    r.y += line_h;

    body.x = area.x;
    body.y = r.y;
    body.w = area.w;
    body.h = area.y + area.h - btn_h - 4 - body.y;
    if (body.h < line_h)
        body.h = line_h;
    if (!view_ui_th(ren, g, VIEW_UI_TH_ROW, &body))
        view_ui_panel(ren, &body, VIEW_UI_COL_GROUND, VIEW_UI_COL_LINE);
    rows = draw_wrapped(ren, g, &body, ms->open_body, M.letter_scroll, 0);
    fit = body.h / (g->px + 3);
    if (M.letter_scroll > rows - fit)
        M.letter_scroll = rows - fit;
    if (M.letter_scroll < 0)
        M.letter_scroll = 0;

    M.r_back.x = area.x;
    M.r_back.y = area.y + area.h - btn_h;
    M.r_back.w = btn_w;
    M.r_back.h = btn_h;
    draw_btn(ren, g, &M.r_back, "Back", mx, my, 1);

    M.r_reply = M.r_back;
    M.r_reply.x += btn_w + 4;
    draw_btn(ren, g, &M.r_reply, "Reply", mx, my, !ms->open_sent);

    M.r_paper = M.r_reply;
    M.r_paper.x += btn_w + 4;
    M.r_paper.w = view_ui_scale(110, g->px);
    draw_btn(ren, g, &M.r_paper, "Read Mail", mx, my, 1);

    M.r_letter_del.x = area.x + area.w - btn_w;
    M.r_letter_del.y = M.r_back.y;
    M.r_letter_del.w = btn_w;
    M.r_letter_del.h = btn_h;
    draw_btn(ren, g, &M.r_letter_del, "Delete", mx, my, 1);
}

static void draw_form(SDL_Renderer *ren, struct view_ui_gpu *g,
                      struct openmmo_rect area, int mx, int my)
{
    int line_h = view_ui_scale(24, g->px);
    int lab_w = view_ui_text_width(g, "Recipient") + 12;
    int btn_h = view_ui_scale(26, g->px);
    int btn_w = view_ui_scale(96, g->px);
    struct openmmo_rect lab = { area.x, area.y, lab_w, line_h };
    struct openmmo_rect fld = { area.x + lab_w, area.y,
                                area.w - lab_w, line_h };
    struct openmmo_rect body;

    view_ui_text_in(ren, g, &lab, 0, 0, "Recipient", VIEW_UI_COL_DIM);
    draw_field(ren, g, &fld, MF_TO, 4);
    M.r_field[MF_TO] = fld;

    lab.y += line_h + 4;
    fld.y += line_h + 4;
    view_ui_text_in(ren, g, &lab, 0, 0, "Subject", VIEW_UI_COL_DIM);
    draw_field(ren, g, &fld, MF_SUBJECT, 4);
    M.r_field[MF_SUBJECT] = fld;

    lab.y += line_h + 4;
    view_ui_text_in(ren, g, &lab, 0, 0, "Body", VIEW_UI_COL_DIM);
    body.x = area.x;
    body.y = lab.y + line_h;
    body.w = area.w;
    body.h = area.y + area.h - btn_h - 6 - body.y;
    if (body.h < line_h)
        body.h = line_h;
    if (!view_ui_th(ren, g, VIEW_UI_TH_INPUT, &body)) {
        SDL_Color f = view_ui_col(VIEW_UI_COL_FIELD);
        SDL_Color l = view_ui_col(M.focus == MF_BODY ? VIEW_UI_COL_ACCENT
                                                     : VIEW_UI_COL_LINE);

        view_ui_fill(ren, body.x, body.y, body.w, body.h, f.r, f.g, f.b, 255);
        view_ui_border(ren, body.x, body.y, body.w, body.h, l.r, l.g, l.b);
    }
    draw_wrapped(ren, g, &body, M.body, 0, M.focus == MF_BODY);
    M.r_field[MF_BODY] = body;

    M.r_clear.x = area.x;
    M.r_clear.y = area.y + area.h - btn_h;
    M.r_clear.w = btn_w;
    M.r_clear.h = btn_h;
    draw_btn(ren, g, &M.r_clear, "Clear", mx, my, 1);

    M.r_send.x = area.x + area.w - btn_w;
    M.r_send.y = M.r_clear.y;
    M.r_send.w = btn_w;
    M.r_send.h = btn_h;
    draw_btn(ren, g, &M.r_send, "Send Mail", mx, my,
             M.to[0] != '\0' && strlen(M.subject) >= 3 &&
             strlen(M.body) >= 3);
}

/* The official client asks before it sends (string 5903). */
static void draw_confirm(SDL_Renderer *ren, struct view_ui_gpu *g,
                         const struct view_ui_frame_layout *L, int mx, int my)
{
    static const char *const lines[] = {
        "Mail is a one-way trade. It is not recommended",
        "for buying or selling assets. Lost or stolen items",
        "will not be recovered by staff.",
        "",
        "Continue sending mail?",
    };
    int line_h = g->px + 6;
    int w = L->box.w * 3 / 4, h = line_h * 7 + view_ui_scale(34, g->px);
    struct openmmo_rect box, r;
    int btn_w = view_ui_scale(80, g->px), btn_h = view_ui_scale(26, g->px);
    size_t i;

    box.w = w;
    box.h = h;
    box.x = L->box.x + (L->box.w - w) / 2;
    box.y = L->box.y + (L->box.h - h) / 2;
    if (!view_ui_th(ren, g, VIEW_UI_TH_WARN, &box))
        view_ui_panel(ren, &box, VIEW_UI_COL_GROUND, VIEW_UI_COL_ACCENT);
    r.x = box.x + 8;
    r.w = box.w - 16;
    r.h = line_h;
    r.y = box.y + 8;
    for (i = 0; i < sizeof lines / sizeof lines[0]; i++) {
        view_ui_text_in(ren, g, &r, 0, 0, lines[i], VIEW_UI_COL_TEXT);
        r.y += line_h;
    }
    M.r_yes.x = box.x + box.w / 2 - btn_w - 6;
    M.r_yes.y = box.y + box.h - btn_h - 8;
    M.r_yes.w = btn_w;
    M.r_yes.h = btn_h;
    draw_btn(ren, g, &M.r_yes, "Yes", mx, my, 1);
    M.r_no = M.r_yes;
    M.r_no.x = box.x + box.w / 2 + 6;
    draw_btn(ren, g, &M.r_no, "No", mx, my, 1);
}

void view_ui_mail_draw(SDL_Renderer *ren, struct view_ui_gpu *g,
                       const struct view_ui_frame_layout *L,
                       struct view_hud *hud, int mx, int my)
{
    const struct openmmo_hud_mail *ms = &hud->snap.mail;
    struct openmmo_rect area;
    int pad = L->pad, i;
    int tab_h = view_ui_scale(26, g->px);
    int pager_h = view_ui_scale(24, g->px);
    int line_h = g->px + 6;
    int y;

    area.x = L->box.x + pad;
    area.w = L->box.w - 2 * pad;
    area.y = L->box.y + L->title_h;
    area.h = L->box.y + L->box.h - area.y - pad;
    y = area.y;

    /* Tabs: the official client's 5827 / 5848 / 5834. */
    {
        static const char *const tab_label[MTAB_N] = {
            "Mail", "Sent Mail", "Send Mail"
        };
        int gap = 1, tw = (area.w - (MTAB_N - 1) * gap) / MTAB_N, x = area.x;

        for (i = 0; i < MTAB_N; i++) {
            struct openmmo_rect r = { x, y, tw, tab_h };
            int active = i == M.tab;
            int lw = view_ui_text_width(g, tab_label[i]);
            int tx = r.x + (r.w - lw) / 2;

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
            M.r_tab[i] = r;
            x += tw + gap;
        }
        y += tab_h + 4;
    }

    /* The letter the guest answered with, once it is the one asked for. */
    if (M.reading &&
        (ms->open_id_lo != M.want_lo || ms->open_id_hi != M.want_hi))
        M.reading = 2; /* asked, still waiting */
    else if (M.reading)
        M.reading = 1;

    if (M.tab == MTAB_SEND) {
        struct openmmo_rect form = { area.x, y, area.w,
                                     area.y + area.h - y };

        draw_form(ren, g, form, mx, my);
        M.row_n = 0;
        M.pager_n = 0;
    } else if (M.reading == 1) {
        struct openmmo_rect one = { area.x, y, area.w,
                                    area.y + area.h - y };

        draw_letter(ren, g, ms, one, mx, my);
        M.row_n = 0;
        M.pager_n = 0;
    } else {
        struct openmmo_rect list, band, count;

        list.x = area.x;
        list.y = y;
        list.w = area.w;
        list.h = area.y + area.h - y - pager_h - line_h - 6;
        if (list.h < 0)
            list.h = 0;
        draw_table(ren, g, ms, list, mx, my);
        band.x = area.x;
        band.y = area.y + area.h - pager_h - line_h - 2;
        band.w = area.w;
        band.h = pager_h;
        draw_pager(ren, g, ms, band, mx, my);
        count.x = area.x;
        count.y = area.y + area.h - line_h;
        count.w = area.w;
        count.h = line_h;
        {
            char line[64];

            /* The official client's 5839, and 5825 beside it when anything is unread. */
            if (ms->unread > 0)
                snprintf(line, sizeof line, "Mailbox: %u/%d   %u Unread Mail.",
                         ms->inbox, MAIL_CAP, ms->unread);
            else
                snprintf(line, sizeof line, "Mailbox: %u/%d",
                         ms->inbox, MAIL_CAP);
            view_ui_text_in(ren, g, &count, 4, 0,
                            M.reading == 2 ? "Opening..." : line,
                            VIEW_UI_COL_DIM);
        }
    }

    /* One official sentence per 0x96, a few seconds each. */
    if (ms->result_seq != M.toast_seq) {
        M.toast_seq = ms->result_seq;
        if (ms->result_seq != 0) {
            snprintf(M.toast, sizeof M.toast, "%s",
                     result_line(ms->result_code));
            M.toast_until = SDL_GetTicks() + 4000u;
            if (ms->result_code == 0) {
                M.to[0] = '\0';
                M.subject[0] = '\0';
                M.body[0] = '\0';
            }
        }
    }
    if (M.toast[0] != '\0' && SDL_GetTicks() < M.toast_until) {
        struct openmmo_rect t;
        int tw = view_ui_text_width(g, M.toast);

        t.w = tw + 16;
        if (t.w > area.w)
            t.w = area.w;
        t.h = line_h + 6;
        t.x = area.x + (area.w - t.w) / 2;
        t.y = area.y + area.h - t.h;
        if (!view_ui_th(ren, g, VIEW_UI_TH_PANEL, &t))
            view_ui_panel(ren, &t, VIEW_UI_COL_GROUND, VIEW_UI_COL_ACCENT);
        view_ui_text_in(ren, g, &t, 8, 0, M.toast, VIEW_UI_COL_TEXT);
    }

    if (M.confirm)
        draw_confirm(ren, g, L, mx, my);
    M.laid = 1;
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

static int field_key(const SDL_Event *ev)
{
    char *buf;
    size_t cap;

    if (M.focus == MF_NONE)
        return 0;
    buf = field_buf(M.focus);
    cap = field_cap(M.focus);
    if (buf == NULL || cap == 0)
        return 0;
    if (ev->type == SDL_TEXTINPUT) {
        const char *t = ev->text.text;

        for (; *t != '\0'; t++) {
            unsigned char c = (unsigned char)*t;
            size_t len;

            if (c < 32 || c > 126)
                continue;
            len = strlen(buf);
            if (len + 1 < cap) {
                buf[len] = (char)c;
                buf[len + 1] = '\0';
            }
        }
        return 1;
    }
    if (ev->type == SDL_KEYDOWN) {
        SDL_Keycode k = ev->key.keysym.sym;
        size_t len = strlen(buf);

        if (k == SDLK_BACKSPACE) {
            if (len > 0)
                view_ui_utf8_trunc(buf);
            return 1;
        }
        if (k == SDLK_ESCAPE) {
            field_focus(MF_NONE);
            return 1;
        }
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
            /* The body is a text area; the two single lines move on. */
            if (M.focus == MF_BODY) {
                if (len + 1 < cap) {
                    buf[len] = '\n';
                    buf[len + 1] = '\0';
                }
            } else
                field_focus(M.focus == MF_TO ? MF_SUBJECT : MF_BODY);
            return 1;
        }
        if (k == SDLK_TAB) {
            field_focus(M.focus + 1 >= MF_N ? MF_TO : M.focus + 1);
            return 1;
        }
        return k >= SDLK_a && k <= SDLK_z;
    }
    return 0;
}

int view_ui_mail_typing(void)
{
    return M.focus != MF_NONE;
}

int view_ui_mail_event(const SDL_Event *ev,
                       const struct view_ui_frame_layout *L,
                       struct view_hud *hud)
{
    const struct openmmo_hud_mail *ms = &hud->snap.mail;
    int i, x, y;

    if (!M.laid)
        return 0;

    if (M.confirm) {
        if (ev->type == SDL_MOUSEBUTTONDOWN &&
            ev->button.button == SDL_BUTTON_LEFT) {
            x = ev->button.x;
            y = ev->button.y;
            if (view_ui_hit(&M.r_yes, x, y)) {
                M.confirm = 0;
                mail_send(hud);
                return 1;
            }
            if (view_ui_hit(&M.r_no, x, y)) {
                M.confirm = 0;
                return 1;
            }
            return view_ui_hit(&L->box, x, y);
        }
        if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_ESCAPE) {
            M.confirm = 0;
            return 1;
        }
        return 0;
    }

    if (field_key(ev)) {
        if (ev->type == SDL_TEXTINPUT || ev->type == SDL_KEYDOWN)
            return 1;
    }

    if (ev->type == SDL_MOUSEWHEEL && M.reading == 1) {
        M.letter_scroll -= ev->wheel.y;
        if (M.letter_scroll < 0)
            M.letter_scroll = 0;
        return 1;
    }

    if (ev->type != SDL_MOUSEBUTTONDOWN ||
        ev->button.button != SDL_BUTTON_LEFT)
        return 0;
    x = ev->button.x;
    y = ev->button.y;

    for (i = 0; i < MTAB_N; i++)
        if (view_ui_hit(&M.r_tab[i], x, y)) {
            if (i != M.tab) {
                M.tab = i;
                M.reading = 0;
                field_focus(MF_NONE);
                if (i != MTAB_SEND)
                    mail_ask(hud, i, M.page[tab_sent(i)]);
            }
            return 1;
        }

    if (M.tab == MTAB_SEND) {
        for (i = 0; i < MF_N; i++)
            if (M.r_field[i].w > 0 && view_ui_hit(&M.r_field[i], x, y)) {
                field_focus(i);
                return 1;
            }
        if (view_ui_hit(&M.r_clear, x, y)) {
            M.to[0] = '\0';
            M.subject[0] = '\0';
            M.body[0] = '\0';
            field_focus(MF_NONE);
            return 1;
        }
        if (view_ui_hit(&M.r_send, x, y)) {
            if (M.to[0] != '\0' && strlen(M.subject) >= 3 &&
                strlen(M.body) >= 3) {
                field_focus(MF_NONE);
                M.confirm = 1;
            }
            return 1;
        }
        return view_ui_hit(&L->box, x, y);
    }

    if (M.reading == 1) {
        if (view_ui_hit(&M.r_back, x, y)) {
            M.reading = 0;
            M.letter_scroll = 0;
            return 1;
        }
        if (!ms->open_sent && view_ui_hit(&M.r_reply, x, y)) {
            /* The official client's Reply: the sender, and its subject under "Re:". */
            snprintf(M.to, sizeof M.to, "%s", ms->open_who);
            {
                /* The official client's 5845. The wire caps a subject at 40, so one
                 * already at the cap loses its tail to the prefix. */
                size_t n = strlen(ms->open_subject);

                if (n > sizeof M.subject - 5)
                    n = sizeof M.subject - 5;
                memcpy(M.subject, "Re: ", 4);
                memcpy(M.subject + 4, ms->open_subject, n);
                M.subject[4 + n] = '\0';
            }
            M.body[0] = '\0';
            M.tab = MTAB_SEND;
            M.reading = 0;
            field_focus(MF_BODY);
            return 1;
        }
        if (view_ui_hit(&M.r_paper, x, y)) {
            /* The engine's own stationery, on the row this letter came from. */
            for (i = 0; i < (int)ms->row_n; i++)
                if (ms->row[i].id_lo == ms->open_id_lo &&
                    ms->row[i].id_hi == ms->open_id_hi) {
                    mail_verb(hud, OPENMMO_HUD_MAIL_PAPER, i);
                    break;
                }
            return 1;
        }
        if (view_ui_hit(&M.r_letter_del, x, y)) {
            for (i = 0; i < (int)ms->row_n; i++)
                if (ms->row[i].id_lo == ms->open_id_lo &&
                    ms->row[i].id_hi == ms->open_id_hi) {
                    mail_verb(hud, OPENMMO_HUD_MAIL_DELETE, i);
                    break;
                }
            M.reading = 0;
            return 1;
        }
        return view_ui_hit(&L->box, x, y);
    }

    for (i = 0; i < M.row_n; i++) {
        if (view_ui_hit(&M.r_del[i], x, y)) {
            mail_verb(hud, OPENMMO_HUD_MAIL_DELETE, i);
            return 1;
        }
        if (view_ui_hit(&M.r_row[i], x, y)) {
            M.want_lo = ms->row[i].id_lo;
            M.want_hi = ms->row[i].id_hi;
            M.reading = 2;
            M.letter_scroll = 0;
            mail_verb(hud, OPENMMO_HUD_MAIL_READ, i);
            return 1;
        }
    }

    for (i = 0; i < M.pager_n; i++)
        if (view_ui_hit(&M.r_pager[i], x, y)) {
            int want = M.pager_page[i];
            unsigned total = ms->listed_sent ? ms->sent : ms->inbox;
            int pages = (int)((total + OPENMMO_HUD_MAIL_ROWS - 1) /
                              OPENMMO_HUD_MAIL_ROWS);

            if (pages < 1)
                pages = 1;
            if (want >= 0 && want < pages)
                mail_ask(hud, M.tab, want);
            return 1;
        }

    return view_ui_hit(&L->box, x, y);
}
