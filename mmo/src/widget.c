/* Compile a held UI packet into a screen the engine can draw. */

#include "widget.h"

#include <stdio.h>
#include <string.h>

/* The engine's own placements, in tiles on the layer the renderer uses. */
static const mmo_widget_rect MESSAGE_RECT = { 2, 19, 27, 4 };
static const mmo_widget_rect YESNO_RECT = { 25, 13, 6, 4 };
static const mmo_widget_rect LIST_RECT = { 1, 2, 20, 2 * MMO_WIDGET_LIST_VISIBLE };
static const mmo_widget_rect GRID_RECT = { 15, 3, 12, 2 };

void mmo_screen_clear(mmo_screen *s)
{
    if (s == NULL)
        return;
    memset(s, 0, sizeof *s);
}

const char *mmo_widget_kind_name(mmo_widget_kind k)
{
    switch (k) {
    case MMO_WIDGET_MESSAGE:
        return "message";
    case MMO_WIDGET_YESNO:
        return "yesno";
    case MMO_WIDGET_GRID:
        return "grid";
    case MMO_WIDGET_LIST:
        return "list";
    case MMO_WIDGET_FIELD:
        return "field";
    case MMO_WIDGET_NONE:
    default:
        return "none";
    }
}

static void set_text(char *dst, size_t cap, const char *src)
{
    if (src == NULL)
        src = "";
    snprintf(dst, cap, "%s", src);
}

static mmo_widget *add_part(mmo_screen *s, mmo_widget_kind kind)
{
    mmo_widget *w;

    if (s->part_count >= MMO_SCREEN_PARTS_MAX)
        return NULL;
    w = &s->part[s->part_count++];
    memset(w, 0, sizeof *w);
    w->kind = kind;
    w->cols = 1;
    switch (kind) {
    case MMO_WIDGET_MESSAGE:
        w->rect = MESSAGE_RECT;
        break;
    case MMO_WIDGET_YESNO:
        w->rect = YESNO_RECT;
        w->framed = 1;
        w->cancelable = 1;
        break;
    case MMO_WIDGET_GRID:
        w->rect = GRID_RECT;
        w->framed = 1;
        w->cancelable = 1;
        w->loop = 1;
        break;
    case MMO_WIDGET_LIST:
        w->rect = LIST_RECT;
        w->framed = 1;
        w->cancelable = 1;
        w->visible_rows = MMO_WIDGET_LIST_VISIBLE;
        break;
    default:
        break;
    }
    return w;
}

static void add_row(mmo_widget *w, const char *label, s64 value)
{
    mmo_widget_row *r;

    if (w == NULL || w->row_count >= MMO_WIDGET_ROWS_MAX)
        return;
    r = &w->row[w->row_count++];
    memset(r, 0, sizeof *r);
    set_text(r->label, sizeof r->label, label);
    r->value = value;
    r->selectable = 1;
}

/* A grid stops fitting once the rows outnumber what the screen holds;
 * past that the list and its scroll model are the widget. Both are
 * engine constructors, so this is a choice of widget, not of code. */
static mmo_widget_kind choice_kind(int count)
{
    return (count <= MMO_WIDGET_GRID_MAX) ? MMO_WIDGET_GRID : MMO_WIDGET_LIST;
}

/* Height a choice widget needs for the rows it will show. Two tiles a
 * row is what the engine's own menus use. */
static void size_choices(mmo_widget *w)
{
    int shown = mmo_widget_visible(w);

    if (shown < 1)
        shown = 1;
    w->rect.height = 2 * shown;
    if (w->kind == MMO_WIDGET_GRID) {
        /* The engine's short menus sit above the message box rather
         * than over it. */
        w->rect.top = MESSAGE_RECT.top - w->rect.height;
        if (w->rect.top < 0)
            w->rect.top = 0;
    }
}

int mmo_screen_from_name_choices(const mmo_name_choices *n, mmo_screen *out)
{
    mmo_widget *w;
    char label[MMO_WIDGET_LABEL_MAX];
    int i, count;

    if (n == NULL || out == NULL)
        return 0;
    mmo_screen_clear(out);
    count = n->count;
    if (count < 0)
        count = 0;
    if (count > MMO_UI_NAME_MAX)
        count = MMO_UI_NAME_MAX;
    if (count == 0)
        return 0;

    out->opcode = 0x5A;
    set_text(out->name, sizeof out->name, "names");
    w = add_part(out, choice_kind(count));
    if (w == NULL)
        return 0;
    w->focus = 1;
    /* The kind byte is the packet's own; it is not a title. */
    snprintf(w->text, sizeof w->text, "kind %d flag %d", (int)n->kind, n->flag);
    for (i = 0; i < count; i++) {
        const char *name = n->entry[i].name;

        if (name[0] != '\0')
            set_text(label, sizeof label, name);
        else
            snprintf(label, sizeof label, "#%d", i + 1);
        /* This packet names an entity per row, so the row's value is it. */
        add_row(w, label, n->entry[i].entity_id);
    }
    size_choices(w);
    out->valid = 1;
    return 1;
}

int mmo_screen_from_option_list(const mmo_option_list *o, mmo_screen *out)
{
    mmo_widget *w;
    char label[MMO_WIDGET_LABEL_MAX];
    int i, count;

    if (o == NULL || out == NULL)
        return 0;
    mmo_screen_clear(out);
    count = o->count;
    if (count < 0)
        count = 0;
    if (count > MMO_UI_OPTION_MAX)
        count = MMO_UI_OPTION_MAX;
    if (count == 0)
        return 0;

    out->opcode = 0x5B;
    set_text(out->name, sizeof out->name, "options");
    w = add_part(out, choice_kind(count));
    if (w == NULL)
        return 0;
    w->focus = 1;
    snprintf(w->text, sizeof w->text, "kind %d", (int)o->kind);
    for (i = 0; i < count; i++) {
        const mmo_ui_option *e = &o->entry[i];

        /* No string reaches this row on the wire. Show the numbers it
         * did carry rather than a name nobody sent. */
        /* Parentheses, not brackets: the engine's charmap has no
         * bracket glyph and an unmapped character draws as a question
         * mark, which reads as a value nobody sent. */
        snprintf(label, sizeof label, "#%d %d/%d (%d %d)", i + 1,
                 (int)e->type_id, (int)e->sub_type, (int)e->value[0],
                 (int)e->value[1]);
        add_row(w, label, i);
    }
    size_choices(w);
    out->valid = 1;
    return 1;
}

int mmo_screen_from_list_window(const mmo_list_window *l, mmo_screen *out)
{
    mmo_widget *w;
    char label[MMO_WIDGET_LABEL_MAX];
    int i, count;

    if (l == NULL || out == NULL)
        return 0;
    mmo_screen_clear(out);
    count = l->count;
    if (count < 0)
        count = 0;
    if (count > MMO_UI_ROW_MAX)
        count = MMO_UI_ROW_MAX;
    if (count == 0)
        return 0;

    out->opcode = 0x5C;
    set_text(out->name, sizeof out->name, "list");
    w = add_part(out, MMO_WIDGET_LIST);
    if (w == NULL)
        return 0;
    w->focus = 1;
    snprintf(w->text, sizeof w->text, "window %d hdr %d/%d/%d%s%s",
             (int)l->window_id, (int)l->header[0], (int)l->header[1],
             (int)l->header[2], l->first_page ? " first" : "",
             l->last_page ? " last" : "");
    for (i = 0; i < count; i++) {
        const mmo_ui_row *r = &l->row[i];

        if (r->label[0] != '\0')
            set_text(label, sizeof label, r->label);
        else
            snprintf(label, sizeof label, "#%d type %d", i + 1,
                     (int)r->row_type);
        /* Nothing on the row is established as its identity, so the
         * pick is the position the server sent it in. */
        add_row(w, label, i);
    }
    size_choices(w);
    out->valid = 1;
    return 1;
}

int mmo_screen_from_confirm(const mmo_confirm_prompt *c, mmo_screen *out)
{
    mmo_widget *w;

    if (c == NULL || out == NULL)
        return 0;
    mmo_screen_clear(out);
    if (!c->visible)
        return 0;
    out->opcode = 0xD9;
    set_text(out->name, sizeof out->name, "confirm");
    out->timeout_s = (int)c->response_s;
    /* The official client draws this over the HUD and the packet carries no text, so
     * it is not the field yes/no however much the name suggests one.
     * The message box is the surface that can say what arrived. */
    w = add_part(out, MMO_WIDGET_MESSAGE);
    if (w == NULL)
        return 0;
    snprintf(w->text, sizeof w->text, "request from #%lld  %u s / %u s",
             (long long)c->entity_id, (unsigned)c->request_s,
             (unsigned)c->response_s);
    out->valid = 1;
    return 1;
}

int mmo_screen_from_menu_prompt(const mmo_menu_prompt *p, mmo_screen *out)
{
    mmo_widget *w;

    if (p == NULL || out == NULL)
        return 0;
    mmo_screen_clear(out);
    out->opcode = 0xD6;
    set_text(out->name, sizeof out->name, "prompt");
    w = add_part(out, MMO_WIDGET_MESSAGE);
    if (w == NULL)
        return 0;
    if (p->relation_count > 0)
        snprintf(w->text, sizeof w->text, "prompt kind %d type %d %d rows",
                 (int)p->kind, (int)p->prompt_type, p->relation_count);
    else
        snprintf(w->text, sizeof w->text, "prompt kind %d type %d value %d",
                 (int)p->kind, (int)p->prompt_type, (int)p->value);
    out->valid = 1;
    return 1;
}

int mmo_screen_from_ui(const openmmo_ui *ui, mmo_screen *out)
{
    if (ui == NULL || out == NULL)
        return 0;
    mmo_screen_clear(out);
    if (!ui->valid)
        return 0;

    /* The packet that arrived last is the screen, where that packet
     * describes one. The join's two empty pages, its scale byte and the
     * visibility flag describe none, so those fall through to whatever
     * is still held, richest shape first. */
    switch (ui->last_op) {
    case 0x5A:
        if (ui->names_valid && mmo_screen_from_name_choices(&ui->names, out))
            return 1;
        break;
    case 0x5B:
        if (ui->options_valid && mmo_screen_from_option_list(&ui->options, out))
            return 1;
        break;
    case 0x5C:
        if (ui->list_valid && mmo_screen_from_list_window(&ui->list, out))
            return 1;
        break;
    case 0xD9:
        if (ui->confirm_valid && mmo_screen_from_confirm(&ui->confirm, out))
            return 1;
        break;
    case 0xD6:
        if (ui->prompt_open && mmo_screen_from_menu_prompt(&ui->prompt, out))
            return 1;
        break;
    default:
        break;
    }

    if (ui->list_valid && mmo_screen_from_list_window(&ui->list, out))
        return 1;
    if (ui->options_valid && mmo_screen_from_option_list(&ui->options, out))
        return 1;
    if (ui->names_valid && mmo_screen_from_name_choices(&ui->names, out))
        return 1;
    if (ui->confirm_valid && mmo_screen_from_confirm(&ui->confirm, out))
        return 1;
    if (ui->prompt_open && mmo_screen_from_menu_prompt(&ui->prompt, out))
        return 1;

    mmo_screen_clear(out);
    return 0;
}

int mmo_screen_focus(const mmo_screen *s)
{
    int i;

    if (s == NULL)
        return -1;
    for (i = 0; i < s->part_count; i++) {
        if (s->part[i].focus && s->part[i].kind != MMO_WIDGET_MESSAGE)
            return i;
    }
    return -1;
}

int mmo_widget_visible(const mmo_widget *w)
{
    int shown;

    if (w == NULL || w->row_count < 1)
        return 0;
    if (w->kind != MMO_WIDGET_LIST)
        return w->row_count;
    shown = (w->visible_rows > 0) ? w->visible_rows : w->row_count;
    if (shown > w->row_count)
        shown = w->row_count;
    return shown;
}

int mmo_widget_first_visible(const mmo_widget *w, int cursor)
{
    int shown, first;

    shown = mmo_widget_visible(w);
    if (shown < 1 || shown >= w->row_count)
        return 0;
    if (cursor < 0)
        cursor = 0;
    if (cursor > w->row_count - 1)
        cursor = w->row_count - 1;
    /* Keep the cursor as near the middle as the ends allow. */
    first = cursor - shown / 2;
    if (first < 0)
        first = 0;
    if (first > w->row_count - shown)
        first = w->row_count - shown;
    return first;
}

int mmo_widget_pick(const mmo_widget *w, int cursor, s64 *out)
{
    if (w == NULL || out == NULL)
        return 0;
    if (cursor < 0 || cursor >= w->row_count)
        return 0;
    if (!w->row[cursor].selectable)
        return 0;
    *out = w->row[cursor].value;
    return 1;
}
