/* The on-screen keyboard state machine. See osk.h. */

#include "osk.h"

#include "osk_layout.gen.h"

#include <string.h>

#define OSK_LINE_H 16
#define OSK_CELL_W (OSK_WIDTH / OSK_LAYOUT_COLS)   /* 14 */
#define OSK_CELL_H ((OSK_HEIGHT - OSK_LINE_H) / OSK_LAYOUT_ROWS) /* 24 */
#define OSK_GRID_X OSK_ORIGIN_X
#define OSK_GRID_Y (OSK_ORIGIN_Y + OSK_LINE_H)

#define OSK_KEYS_MAX (OSK_LAYOUT_ROWS * OSK_LAYOUT_COLS)

typedef struct {
    unsigned char row, col, ncols;
    unsigned char kind;
    uint16_t      unit;
} osk_key;

static osk_key sKeys[OSK_LAYOUT_PAGES][OSK_KEYS_MAX];
static int     sNKeys[OSK_LAYOUT_PAGES];
static int     sReady;

static int is_control(unsigned cell)
{
    return cell == OSK_CELL_UPPER || cell == OSK_CELL_LOWER
        || cell == OSK_CELL_OTHERS || cell == OSK_CELL_JP
        || cell == OSK_CELL_NUMPAD || cell == OSK_CELL_BACK
        || cell == OSK_CELL_OK;
}

static int kind_of(unsigned cell, uint16_t *unit)
{
    *unit = 0;
    switch (cell) {
    case OSK_CELL_SKIP:
        return -1;
    case OSK_CELL_UPPER:
        *unit = 0;
        return OSK_KEY_PAGE;
    case OSK_CELL_LOWER:
        *unit = 1;
        return OSK_KEY_PAGE;
    case OSK_CELL_OTHERS:
        *unit = 2;
        return OSK_KEY_PAGE;
    case OSK_CELL_JP:
        *unit = 3;
        return OSK_KEY_PAGE;
    case OSK_CELL_NUMPAD:
        *unit = 4;
        return OSK_KEY_PAGE;
    case OSK_CELL_BACK:
        return OSK_KEY_BACKSPACE;
    case OSK_CELL_OK:
        return OSK_KEY_ENTER;
    default:
        if (cell == 0x0020 || cell == 0x3000) {
            *unit = 0x0020;
            return OSK_KEY_SPACE;
        }
        *unit = (uint16_t)cell;
        return OSK_KEY_CHAR;
    }
}

static void build(void)
{
    int page;

    if (sReady)
        return;

    for (page = 0; page < OSK_LAYOUT_PAGES; page++) {
        int row, n = 0;

        for (row = 0; row < OSK_LAYOUT_ROWS; row++) {
            int col = 0;

            while (col < OSK_LAYOUT_COLS) {
                unsigned cell = OSK_LAYOUT[page][row][col];
                uint16_t unit;
                int kind = kind_of(cell, &unit);
                int span = 1;

                if (kind < 0) {
                    col++;
                    continue;
                }
                if (is_control(cell)) {
                    while (col + span < OSK_LAYOUT_COLS
                           && OSK_LAYOUT[page][row][col + span] == cell)
                        span++;
                }
                sKeys[page][n].row = (unsigned char)row;
                sKeys[page][n].col = (unsigned char)col;
                sKeys[page][n].ncols = (unsigned char)span;
                sKeys[page][n].kind = (unsigned char)kind;
                sKeys[page][n].unit = unit;
                n++;
                col += span;
            }
        }
        sNKeys[page] = n;
    }
    sReady = 1;
}

static int page_of(const osk_state *k)
{
    if (k == NULL || k->page < 0 || k->page >= OSK_LAYOUT_PAGES)
        return 0;
    return k->page;
}

static int first_letter(int page)
{
    int i;

    build();
    for (i = 0; i < sNKeys[page]; i++) {
        if (sKeys[page][i].kind == OSK_KEY_CHAR)
            return i;
    }
    return 0;
}

static void clamp_cursor(osk_state *k)
{
    int page = page_of(k);
    int n;

    build();
    n = sNKeys[page];
    if (n <= 0) {
        k->cursor = 0;
        return;
    }
    if (k->cursor < 0)
        k->cursor = 0;
    if (k->cursor >= n)
        k->cursor = n - 1;
}

static size_t cap_of(const osk_state *k)
{
    if (k->max == 0 || k->max > OSK_MAX_TEXT)
        return OSK_MAX_TEXT;
    return k->max;
}

void osk_reset(osk_state *k)
{
    size_t max = k->max;

    memset(k, 0, sizeof *k);
    k->max = (max == 0 || max > OSK_MAX_TEXT) ? OSK_MAX_TEXT : max;
    k->page = 0;
    k->cursor = first_letter(0);
}

void osk_set_max(osk_state *k, size_t n)
{
    if (n == 0 || n > OSK_MAX_TEXT)
        n = OSK_MAX_TEXT;
    k->max = n;
    if (k->len > n)
        k->len = n;
    if (k->caret > k->len)
        k->caret = k->len;
}

static void insert_unit(osk_state *k, uint16_t u)
{
    size_t cap = cap_of(k);

    if (k->len >= cap)
        return;
    if (k->caret > k->len)
        k->caret = k->len;
    if (k->caret < k->len)
        memmove(k->text + k->caret + 1, k->text + k->caret,
                (k->len - k->caret) * sizeof k->text[0]);
    k->text[k->caret++] = u;
    k->len++;
}

void osk_insert(osk_state *k, uint16_t u)
{
    insert_unit(k, u);
}

void osk_backspace(osk_state *k)
{
    if (k->caret == 0)
        return;
    if (k->caret < k->len)
        memmove(k->text + k->caret - 1, k->text + k->caret,
                (k->len - k->caret) * sizeof k->text[0]);
    k->caret--;
    k->len--;
}

static void delete_at_caret(osk_state *k)
{
    if (k->caret >= k->len)
        return;
    if (k->caret + 1 < k->len)
        memmove(k->text + k->caret, k->text + k->caret + 1,
                (k->len - k->caret - 1) * sizeof k->text[0]);
    k->len--;
}

void osk_feed(osk_state *k, unsigned kind, unsigned unit)
{
    switch (kind) {
    case 1u: /* UNIT */
        insert_unit(k, (uint16_t)unit);
        break;
    case 2u: /* BACKSPACE */
        osk_backspace(k);
        break;
    case 3u: /* DELETE */
        delete_at_caret(k);
        break;
    case 4u: /* LEFT */
        if (k->caret > 0)
            k->caret--;
        break;
    case 5u: /* RIGHT */
        if (k->caret < k->len)
            k->caret++;
        break;
    case 6u: /* HOME */
        k->caret = 0;
        break;
    case 7u: /* END */
        k->caret = k->len;
        break;
    case 8u: /* COMMIT */
        k->committed = 1;
        break;
    case 9u: /* CANCEL */
        k->cancelled = 1;
        break;
    default:
        break;
    }
}

static int row_keys(int page, int row, int *idxs, int cap)
{
    int i, n = 0;

    for (i = 0; i < sNKeys[page] && n < cap; i++) {
        if (sKeys[page][i].row == row)
            idxs[n++] = i;
    }
    return n;
}

static int nearest_row(int page, int row, int dir)
{
    int tries;

    for (tries = 0; tries < OSK_LAYOUT_ROWS; tries++) {
        int idxs[OSK_LAYOUT_COLS];

        row = (row + dir + OSK_LAYOUT_ROWS) % OSK_LAYOUT_ROWS;
        if (row_keys(page, row, idxs, OSK_LAYOUT_COLS) > 0)
            return row;
    }
    return row;
}

void osk_move(osk_state *k, osk_dir dir)
{
    int page, idxs[OSK_LAYOUT_COLS], n, i, col, row, dest;

    build();
    clamp_cursor(k);
    page = page_of(k);
    row = sKeys[page][k->cursor].row;
    col = sKeys[page][k->cursor].col;
    n = row_keys(page, row, idxs, OSK_LAYOUT_COLS);
    if (n <= 0)
        return;

    switch (dir) {
    case OSK_LEFT:
    case OSK_RIGHT:
        for (i = 0; i < n; i++) {
            if (idxs[i] == k->cursor)
                break;
        }
        if (i >= n)
            i = 0;
        if (dir == OSK_LEFT)
            i = (i - 1 + n) % n;
        else
            i = (i + 1) % n;
        k->cursor = idxs[i];
        break;
    case OSK_UP:
    case OSK_DOWN:
        dest = nearest_row(page, row, dir == OSK_UP ? -1 : 1);
        n = row_keys(page, dest, idxs, OSK_LAYOUT_COLS);
        if (n <= 0)
            break;
        for (i = n - 1; i > 0; i--) {
            if (sKeys[page][idxs[i]].col <= col)
                break;
        }
        k->cursor = idxs[i];
        break;
    }
}

static void set_page(osk_state *k, int page)
{
    int col, row, idxs[OSK_LAYOUT_COLS], n, i;

    build();
    if (page < 0 || page >= OSK_LAYOUT_PAGES)
        return;
    row = 0;
    col = 0;
    if (k->cursor >= 0 && k->cursor < sNKeys[page_of(k)]) {
        row = sKeys[page_of(k)][k->cursor].row;
        col = sKeys[page_of(k)][k->cursor].col;
    }
    k->page = page;
    n = row_keys(page, row, idxs, OSK_LAYOUT_COLS);
    if (n <= 0) {
        k->cursor = first_letter(page);
        return;
    }
    for (i = n - 1; i > 0; i--) {
        if (sKeys[page][idxs[i]].col <= col)
            break;
    }
    k->cursor = idxs[i];
}

void osk_activate(osk_state *k)
{
    const osk_key *key;
    int page;

    build();
    clamp_cursor(k);
    page = page_of(k);
    if (sNKeys[page] <= 0)
        return;
    key = &sKeys[page][k->cursor];
    switch ((osk_keykind)key->kind) {
    case OSK_KEY_CHAR:
        insert_unit(k, key->unit);
        break;
    case OSK_KEY_SPACE:
        insert_unit(k, 0x0020);
        break;
    case OSK_KEY_BACKSPACE:
        osk_backspace(k);
        break;
    case OSK_KEY_ENTER:
        k->committed = 1;
        break;
    case OSK_KEY_PAGE:
        set_page(k, (int)key->unit);
        break;
    }
}

int osk_pen(osk_state *k, int px, int py)
{
    int i, n;

    build();
    n = sNKeys[page_of(k)];
    for (i = 0; i < n; i++) {
        osk_key_view v;

        if (!osk_get_key(k, i, &v))
            continue;
        if (px >= v.x && px < v.x + v.w && py >= v.y && py < v.y + v.h) {
            k->cursor = i;
            osk_activate(k);
            return 1;
        }
    }
    return 0;
}

int osk_committed(const osk_state *k)
{
    return k->committed;
}

int osk_cancelled(const osk_state *k)
{
    return k->cancelled;
}

size_t osk_caret(const osk_state *k)
{
    return k->caret;
}

int osk_page(const osk_state *k)
{
    return page_of(k);
}

size_t osk_text_len(const osk_state *k)
{
    return k->len;
}

size_t osk_text_utf16le(const osk_state *k, uint8_t *dst, size_t dst_cap)
{
    size_t i;

    for (i = 0; i < k->len; i++) {
        size_t b = i * 2;
        if (b + 1 < dst_cap) {
            dst[b]     = (uint8_t)(k->text[i] & 0xFF);
            dst[b + 1] = (uint8_t)(k->text[i] >> 8);
        }
    }
    return k->len * 2;
}

int osk_key_count(const osk_state *k)
{
    build();
    return sNKeys[page_of(k)];
}

static void put_label(char *dst, size_t cap, const char *s)
{
    size_t n = 0;

    if (cap == 0)
        return;
    if (s == NULL)
        s = "";
    while (s[n] != '\0' && n + 1 < cap) {
        dst[n] = s[n];
        n++;
    }
    dst[n] = '\0';
}

static void put_unit(char *dst, size_t cap, uint16_t u)
{
    if (cap == 0)
        return;
    if (u < 0x80 && cap >= 2) {
        dst[0] = (char)u;
        dst[1] = '\0';
        return;
    }
    if (u < 0x800 && cap >= 3) {
        dst[0] = (char)(0xC0 | (u >> 6));
        dst[1] = (char)(0x80 | (u & 0x3F));
        dst[2] = '\0';
        return;
    }
    if (cap >= 4) {
        dst[0] = (char)(0xE0 | (u >> 12));
        dst[1] = (char)(0x80 | ((u >> 6) & 0x3F));
        dst[2] = (char)(0x80 | (u & 0x3F));
        dst[3] = '\0';
        return;
    }
    dst[0] = '?';
    if (cap > 1)
        dst[1] = '\0';
}

int osk_get_key(const osk_state *k, int i, osk_key_view *out)
{
    const osk_key *key;
    int page;

    if (out == NULL)
        return 0;
    memset(out, 0, sizeof *out);
    build();
    page = page_of(k);
    if (i < 0 || i >= sNKeys[page])
        return 0;
    key = &sKeys[page][i];
    out->x = OSK_GRID_X + key->col * OSK_CELL_W;
    out->y = OSK_GRID_Y + key->row * OSK_CELL_H;
    out->w = key->ncols * OSK_CELL_W - 1;
    out->h = OSK_CELL_H - 1;
    out->kind = (osk_keykind)key->kind;
    out->unit = key->unit;
    out->selected = (k != NULL && i == k->cursor);
    switch (out->kind) {
    case OSK_KEY_PAGE:
        if (out->unit == 0)
            put_label(out->label, sizeof out->label, "ABC");
        else if (out->unit == 1)
            put_label(out->label, sizeof out->label, "abc");
        else if (out->unit == 2)
            put_label(out->label, sizeof out->label, "?!");
        else if (out->unit == 3)
            put_label(out->label, sizeof out->label, "JP");
        else
            put_label(out->label, sizeof out->label, "123");
        break;
    case OSK_KEY_BACKSPACE:
        put_label(out->label, sizeof out->label, "DEL");
        break;
    case OSK_KEY_ENTER:
        put_label(out->label, sizeof out->label, "OK");
        break;
    case OSK_KEY_SPACE:
        put_label(out->label, sizeof out->label, " ");
        break;
    case OSK_KEY_CHAR:
        put_unit(out->label, sizeof out->label, out->unit);
        break;
    }
    return 1;
}
