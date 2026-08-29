/* The character list and the creator a person meets. See creator.h. */

#include "creator.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

static int offered_region_count(void)
{
    int n = 0, i, total = mmo_region_count();

    for (i = 0; i < total; i++) {
        const mmo_region *r = mmo_region_at(i);

        if (r && r->offered)
            n++;
    }
    return n;
}

static const mmo_region *offered_region_at(int want)
{
    int seen = 0, i, total = mmo_region_count();

    for (i = 0; i < total; i++) {
        const mmo_region *r = mmo_region_at(i);

        if (!r || !r->offered)
            continue;
        if (seen == want)
            return r;
        seen++;
    }
    return NULL;
}

static int offered_region_index_of(int id)
{
    int seen = 0, i, total = mmo_region_count();

    for (i = 0; i < total; i++) {
        const mmo_region *r = mmo_region_at(i);

        if (!r || !r->offered)
            continue;
        if (r->id == id)
            return seen;
        seen++;
    }
    return 0;
}

static int offered_appear_count(void)
{
    int n = 0, i, total = mmo_appearance_count();

    for (i = 0; i < total; i++) {
        const mmo_appearance *a = mmo_appearance_at(i);

        if (a && a->offered)
            n++;
    }
    return n;
}

static int offered_appear_at(int want)
{
    int seen = 0, i, total = mmo_appearance_count();

    for (i = 0; i < total; i++) {
        const mmo_appearance *a = mmo_appearance_at(i);

        if (!a || !a->offered)
            continue;
        if (seen == want)
            return i;
        seen++;
    }
    return 0;
}

static int offered_appear_index_of(int catalog)
{
    int seen = 0, i, total = mmo_appearance_count();

    for (i = 0; i < total; i++) {
        const mmo_appearance *a = mmo_appearance_at(i);

        if (!a || !a->offered)
            continue;
        if (i == catalog)
            return seen;
        seen++;
    }
    return 0;
}

static int gender_trainer_index(int gender)
{
    int gfx = mmo_appearance_gender_gfx(gender);
    int i, total = mmo_appearance_count();

    for (i = 0; i < total; i++) {
        const mmo_appearance *a = mmo_appearance_at(i);

        if (a && a->offered && a->gfx == gfx)
            return i;
    }
    return 0;
}

static int is_other_trainer(int catalog, int gender)
{
    const mmo_appearance *a = mmo_appearance_at(catalog);
    int other = mmo_appearance_gender_gfx(gender ^ 1);

    return a != NULL && a->gfx == other;
}

static int select_rows(const mmo_creator *c)
{
    return c->list.held + 1;
}

static int step_rows(const mmo_creator *c)
{
    switch (c->step) {
    case MMO_CREATOR_SELECT:
        return select_rows(c);
    case MMO_CREATOR_ACTION:
    case MMO_CREATOR_DELETE:
    case MMO_CREATOR_GENDER:
        return 2;
    case MMO_CREATOR_REGION:
        return offered_region_count();
    case MMO_CREATOR_APPEAR:
        return offered_appear_count();
    default:
        return 0;
    }
}

static void clamp_scroll(mmo_creator *c)
{
    int rows = step_rows(c);
    int vis = MMO_CREATOR_VISIBLE;

    if (c->cursor < 0)
        c->cursor = 0;
    if (rows > 0 && c->cursor >= rows)
        c->cursor = rows - 1;
    if (c->cursor < c->scroll)
        c->scroll = c->cursor;
    if (c->cursor >= c->scroll + vis)
        c->scroll = c->cursor - vis + 1;
    if (c->scroll < 0)
        c->scroll = 0;
}

static void set_step(mmo_creator *c, mmo_creator_step step, int cursor)
{
    c->step = step;
    c->cursor = cursor;
    c->scroll = 0;
    clamp_scroll(c);
}

void mmo_creator_reset(mmo_creator *c)
{
    if (!c)
        return;
    memset(c, 0, sizeof *c);
    c->pick = -1;
    c->acting = -1;
    c->region = mmo_region_default();
    c->appear = gender_trainer_index(0);
    c->submitted = 0;
}

void mmo_creator_set_list(mmo_creator *c, const mmo_character_list *list)
{
    int i, prefer = -1;

    if (!c)
        return;
    if (list)
        c->list = *list;
    else
        memset(&c->list, 0, sizeof c->list);
    c->pick = -1;
    c->acting = -1;
    c->deleting = 0;
    c->creating = 0;
    c->submitted = 0;
    c->refused[0] = '\0';
    if (c->name[0] != '\0') {
        for (i = 0; i < c->list.held; i++) {
            if (strcasecmp(c->list.entry[i].name, c->name) == 0) {
                prefer = i;
                break;
            }
        }
    }
    set_step(c, MMO_CREATOR_SELECT, prefer >= 0 ? prefer : 0);
}

int mmo_creator_needs_entry(const mmo_creator *c)
{
    return c && c->step == MMO_CREATOR_NAME;
}

static void move_cursor(mmo_creator *c, int delta)
{
    int rows = step_rows(c);

    if (rows <= 0)
        return;
    c->cursor += delta;
    if (c->cursor < 0)
        c->cursor = rows - 1;
    else if (c->cursor >= rows)
        c->cursor = 0;
    clamp_scroll(c);
}

void mmo_creator_move(mmo_creator *c, mmo_creator_dir dir)
{
    if (!c)
        return;
    if (c->step == MMO_CREATOR_HIDDEN || c->step == MMO_CREATOR_WAIT
        || c->step == MMO_CREATOR_NAME)
        return;

    if (c->step == MMO_CREATOR_ACTION || c->step == MMO_CREATOR_DELETE) {
        if (dir == MMO_CREATOR_UP)
            move_cursor(c, -1);
        else if (dir == MMO_CREATOR_DOWN)
            move_cursor(c, 1);
        return;
    }

    if (c->step == MMO_CREATOR_GENDER) {
        if (dir == MMO_CREATOR_LEFT || dir == MMO_CREATOR_RIGHT
            || dir == MMO_CREATOR_UP || dir == MMO_CREATOR_DOWN)
            c->cursor ^= 1;
        c->gender = c->cursor & 1;
        if (is_other_trainer(c->appear, c->gender))
            c->appear = gender_trainer_index(c->gender);
        return;
    }

    if (dir == MMO_CREATOR_UP)
        move_cursor(c, -1);
    else if (dir == MMO_CREATOR_DOWN)
        move_cursor(c, 1);
}

int mmo_creator_confirm(mmo_creator *c)
{
    const mmo_region *r;

    if (!c)
        return 0;

    switch (c->step) {
    case MMO_CREATOR_SELECT:
        c->refused[0] = '\0';
        if (c->cursor < c->list.held) {
            /* Not the pick itself: a character is also the one thing on this
             * screen that can be destroyed, so the row opens a menu and PLAY
             * is the first thing on it. */
            c->acting = c->cursor;
            c->creating = 0;
            set_step(c, MMO_CREATOR_ACTION, 0);
            return 1;
        }
        c->pick = -1;
        c->acting = -1;
        c->creating = 1;
        c->name[0] = '\0';
        c->gender = 0;
        c->region = mmo_region_default();
        c->appear = gender_trainer_index(0);
        set_step(c, MMO_CREATOR_NAME, 0);
        return 1;

    case MMO_CREATOR_ACTION:
        if (c->acting < 0 || c->acting >= c->list.held) {
            set_step(c, MMO_CREATOR_SELECT, c->cursor);
            return 0;
        }
        if (c->cursor == 0) {
            c->pick = c->acting;
            return 1;
        }
        set_step(c, MMO_CREATOR_DELETE, 0);
        return 1;

    case MMO_CREATOR_DELETE:
        if (c->acting < 0 || c->acting >= c->list.held) {
            set_step(c, MMO_CREATOR_SELECT, 0);
            return 0;
        }
        if (c->cursor == 0) {
            set_step(c, MMO_CREATOR_ACTION, 0);
            return 1;
        }
        c->deleting = 1;
        return 1;

    case MMO_CREATOR_NAME:
        if (c->name[0] == '\0')
            return 0;
        set_step(c, MMO_CREATOR_GENDER, c->gender & 1);
        return 1;

    case MMO_CREATOR_GENDER:
        c->gender = c->cursor & 1;
        if (is_other_trainer(c->appear, c->gender))
            c->appear = gender_trainer_index(c->gender);
        set_step(c, MMO_CREATOR_REGION, offered_region_index_of(c->region));
        return 1;

    case MMO_CREATOR_REGION:
        r = offered_region_at(c->cursor);
        if (!r || !r->selectable)
            return 0;
        c->region = r->id;
        set_step(c, MMO_CREATOR_APPEAR, offered_appear_index_of(c->appear));
        return 1;

    case MMO_CREATOR_APPEAR:
        c->appear = offered_appear_at(c->cursor);
        c->submitted = 1;
        return 1;

    default:
        return 0;
    }
}

int mmo_creator_back(mmo_creator *c)
{
    if (!c)
        return 0;
    switch (c->step) {
    case MMO_CREATOR_ACTION:
        set_step(c, MMO_CREATOR_SELECT, c->acting >= 0 ? c->acting : 0);
        c->acting = -1;
        return 1;
    case MMO_CREATOR_DELETE:
        set_step(c, MMO_CREATOR_ACTION, 0);
        return 1;
    case MMO_CREATOR_NAME:
        set_step(c, MMO_CREATOR_SELECT, c->list.held);
        c->creating = 0;
        return 1;
    case MMO_CREATOR_GENDER:
        set_step(c, MMO_CREATOR_NAME, 0);
        return 1;
    case MMO_CREATOR_REGION:
        set_step(c, MMO_CREATOR_GENDER, c->gender & 1);
        return 1;
    case MMO_CREATOR_APPEAR:
        set_step(c, MMO_CREATOR_REGION, offered_region_index_of(c->region));
        return 1;
    default:
        return 0;
    }
}

int mmo_creator_set_name(mmo_creator *c, const char *latin1)
{
    size_t n;

    if (!c || !latin1 || c->step != MMO_CREATOR_NAME)
        return 0;
    n = strlen(latin1);
    if (n == 0 || n > MMO_CHAR_NAME_MAX)
        return 0;
    memcpy(c->name, latin1, n);
    c->name[n] = '\0';
    set_step(c, MMO_CREATOR_GENDER, c->gender & 1);
    return 1;
}

void mmo_creator_begin_wait(mmo_creator *c)
{
    if (!c)
        return;
    c->step = MMO_CREATOR_WAIT;
    c->cursor = 0;
    c->scroll = 0;
}

void mmo_creator_refuse(mmo_creator *c, const char *why)
{
    size_t n;

    if (!c || !why || why[0] == '\0')
        return;
    n = strlen(why);
    if (n >= sizeof c->refused)
        n = sizeof c->refused - 1;
    memcpy(c->refused, why, n);
    c->refused[n] = '\0';
    /* The reason is read on the list the refusal put the player back on, and
     * not on the name step: the lobby hands that step straight to the engine's
     * naming screen, which has nowhere to print a sentence of ours. The cursor
     * sits on NEW CHARACTER, which is where the answer to it is. */
    set_step(c, MMO_CREATOR_SELECT, c->list.held);
}

int mmo_creator_has_pick(const mmo_creator *c)
{
    return c && c->pick >= 0 && c->pick < c->list.held;
}

int mmo_creator_pick_index(const mmo_creator *c)
{
    return mmo_creator_has_pick(c) ? c->pick : -1;
}

int mmo_creator_has_delete(const mmo_creator *c)
{
    return c && c->deleting && c->acting >= 0 && c->acting < c->list.held;
}

int mmo_creator_delete_index(const mmo_creator *c)
{
    return mmo_creator_has_delete(c) ? c->acting : -1;
}

int mmo_creator_ready(const mmo_creator *c)
{
    if (!c || !c->creating || c->name[0] == '\0')
        return 0;
    if (c->gender != 0 && c->gender != 1)
        return 0;
    if (!mmo_region_is_selectable(c->region))
        return 0;
    if (mmo_appearance_body_type(c->appear) < 0)
        return 0;
    return c->submitted;
}

int mmo_creator_fill_create(const mmo_creator *c, mmo_create_character *out)
{
    if (!c || !out || !mmo_creator_ready(c))
        return -1;
    memset(out, 0, sizeof *out);
    out->name = c->name;
    out->gender = c->gender;
    out->starting_region = c->region;
    out->appearance.region_selection_index = c->region;
    if (mmo_appearance_apply_body(&out->appearance, c->appear) != 0)
        return -1;
    return 0;
}

const char *mmo_creator_title(const mmo_creator *c)
{
    if (!c)
        return "";
    switch (c->step) {
    case MMO_CREATOR_SELECT:
        return "CHARACTER SELECTION";
    case MMO_CREATOR_ACTION:
        return "CHARACTER";
    case MMO_CREATOR_DELETE:
        return "DELETE CHARACTER";
    case MMO_CREATOR_NAME:
        return "CHARACTER NAME";
    case MMO_CREATOR_GENDER:
        return "GENDER";
    case MMO_CREATOR_REGION:
        return "STARTING REGION";
    case MMO_CREATOR_APPEAR:
        return "APPEARANCE";
    case MMO_CREATOR_WAIT:
        return "PLEASE WAIT";
    default:
        return "";
    }
}

const char *mmo_creator_hint(const mmo_creator *c)
{
    const mmo_region *r;

    if (!c)
        return "";
    switch (c->step) {
    case MMO_CREATOR_SELECT:
        if (c->refused[0])
            return c->refused;
        return c->list.held ? "A TO CHOOSE   NEW CHARACTER TO CREATE" : "A  NEW CHARACTER";
    case MMO_CREATOR_ACTION:
        return "A TO CHOOSE   B GOES BACK";
    case MMO_CREATOR_DELETE:
        return "A DELETED CHARACTER DOES NOT COME BACK";
    case MMO_CREATOR_NAME:
        return "TYPE A NAME, THEN ENTER";
    case MMO_CREATOR_GENDER:
        return "LEFT OR RIGHT, THEN A";
    case MMO_CREATOR_REGION:
        r = offered_region_at(c->cursor);
        if (r && !r->selectable && r->reason)
            return r->reason;
        return "ONLY SINNOH CAN BE PLAYED";
    case MMO_CREATOR_APPEAR:
        return "A  CREATE CHARACTER";
    case MMO_CREATOR_WAIT:
        return "THE SERVER IS ANSWERING";
    default:
        return "";
    }
}

int mmo_creator_visible_count(const mmo_creator *c)
{
    int rows, left;

    if (!c)
        return 0;
    if (c->step == MMO_CREATOR_NAME || c->step == MMO_CREATOR_WAIT
        || c->step == MMO_CREATOR_HIDDEN)
        return 0;
    rows = step_rows(c);
    left = rows - c->scroll;
    if (left < 0)
        return 0;
    if (left > MMO_CREATOR_VISIBLE)
        return MMO_CREATOR_VISIBLE;
    return left;
}

static void pretty_name(const char *in, char *out, size_t cap)
{
    size_t i;

    if (cap == 0)
        return;
    for (i = 0; in[i] != '\0' && i + 1 < cap; i++)
        out[i] = (in[i] == '_') ? ' ' : in[i];
    out[i] = '\0';
}

int mmo_creator_row_count(const mmo_creator *c)
{
    if (!c || c->step == MMO_CREATOR_NAME || c->step == MMO_CREATOR_WAIT
        || c->step == MMO_CREATOR_HIDDEN)
        return 0;
    return step_rows(c);
}

int mmo_creator_row_at(const mmo_creator *c, int idx, mmo_creator_row *out)
{
    const mmo_region *r;
    const mmo_appearance *a;
    const mmo_character *ch;
    char look[24];

    if (!c || !out || idx < 0 || idx >= mmo_creator_row_count(c))
        return 0;
    memset(out, 0, sizeof *out);
    out->selected = (idx == c->cursor);

    switch (c->step) {
    case MMO_CREATOR_SELECT:
        if (idx < c->list.held) {
            ch = &c->list.entry[idx];
            snprintf(out->text, sizeof out->text, "%s  %s  %s",
                     ch->name[0] ? ch->name : "?",
                     ch->gender == 1 ? "F" : (ch->gender == 0 ? "M" : "?"),
                     mmo_region_name(ch->region));
        } else {
            snprintf(out->text, sizeof out->text, "NEW CHARACTER");
        }
        return 1;

    case MMO_CREATOR_ACTION:
        if (c->acting < 0 || c->acting >= c->list.held)
            return 0;
        ch = &c->list.entry[c->acting];
        snprintf(out->text, sizeof out->text, "%s %s",
                 idx ? "DELETE" : "PLAY AS",
                 ch->name[0] ? ch->name : "?");
        return 1;

    case MMO_CREATOR_DELETE:
        if (c->acting < 0 || c->acting >= c->list.held)
            return 0;
        ch = &c->list.entry[c->acting];
        snprintf(out->text, sizeof out->text, "%s %s",
                 idx ? "DELETE" : "KEEP",
                 ch->name[0] ? ch->name : "?");
        return 1;

    case MMO_CREATOR_GENDER:
        snprintf(out->text, sizeof out->text, "%s", idx ? "FEMALE" : "MALE");
        return 1;

    case MMO_CREATOR_REGION:
        r = offered_region_at(idx);
        if (!r)
            return 0;
        snprintf(out->text, sizeof out->text, "%s", r->name);
        if (!r->selectable) {
            out->greyed = 1;
            out->reason = r->reason;
        }
        return 1;

    case MMO_CREATOR_APPEAR:
        a = mmo_appearance_at(offered_appear_at(idx));
        if (!a)
            return 0;
        pretty_name(a->name, look, sizeof look);
        snprintf(out->text, sizeof out->text, "%s", look);
        return 1;

    default:
        return 0;
    }
}

int mmo_creator_get_row(const mmo_creator *c, int vis, mmo_creator_row *out)
{
    if (!c || vis < 0 || vis >= mmo_creator_visible_count(c))
        return 0;
    return mmo_creator_row_at(c, c->scroll + vis, out);
}
