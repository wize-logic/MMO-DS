#ifndef OPENMMO_CREATOR_H
#define OPENMMO_CREATOR_H
/* The character list and the creator a person meets. */

#include "appearance.h"
#include "entry.h"
#include "game.h"
#include "region.h"

#include <stddef.h>

#define MMO_CREATOR_VISIBLE 8
#define MMO_CREATOR_ROW     40

typedef enum {
    MMO_CREATOR_HIDDEN = 0,
    MMO_CREATOR_SELECT,
    MMO_CREATOR_ACTION,   /* what to do with the row that was chosen */
    MMO_CREATOR_DELETE,   /* keep it or delete it, and nothing else */
    MMO_CREATOR_NAME,
    MMO_CREATOR_GENDER,
    MMO_CREATOR_REGION,
    MMO_CREATOR_APPEAR,
    MMO_CREATOR_WAIT
} mmo_creator_step;

typedef enum {
    MMO_CREATOR_UP = 0,
    MMO_CREATOR_DOWN,
    MMO_CREATOR_LEFT,
    MMO_CREATOR_RIGHT
} mmo_creator_dir;

typedef struct {
    char        text[MMO_CREATOR_ROW];
    int         selected;
    int         greyed;
    const char *reason;
} mmo_creator_row;

typedef struct {
    mmo_creator_step  step;
    mmo_character_list list;
    int               cursor;
    int               scroll;
    char              name[MMO_CHAR_NAME_MAX + 1];
    int               gender;     /* 0 boy, 1 girl */
    int               region;     /* wire region id */
    int               appear;     /* catalog index, offered */
    int               pick;       /* list index of an existing row, or -1 */
    int               creating;   /* 1 once NEW CHARACTER was confirmed */
    int               submitted;  /* 1 once A on the body finished the creator */
    int               acting;     /* list index the action menu is about, or -1 */
    int               deleting;   /* 1 once the delete confirmation was taken */
    char              refused[64]; /* why the last create came back, or empty */
} mmo_creator;

void mmo_creator_reset(mmo_creator *c);

/* Seat the account's list and open the select step. An empty list is a
 * new player's first screen: one row, NEW CHARACTER. A later call (the
 * list that comes back after a create) stays on select and prefers the
 * name that was just submitted. */
void mmo_creator_set_list(mmo_creator *c, const mmo_character_list *list);

/* The name step is the only one that types. The caller opens the field
 * with mmo_entry_open(..., MMO_ENTRY_NAME) and writes the committed
 * line back with mmo_creator_set_name. */
int mmo_creator_needs_entry(const mmo_creator *c);

void mmo_creator_move(mmo_creator *c, mmo_creator_dir dir);

/* A. On select, an existing row becomes the pick; NEW CHARACTER starts
 * the creator. On a greyed region, nothing happens. Returns 1 when the
 * step changed or a pick/submit became ready. */
int mmo_creator_confirm(mmo_creator *c);

/* B. Back one step. On select, stays there. */
int mmo_creator_back(mmo_creator *c);

/* Commit the typed name (Latin-1, at most 32). Empty or too long is
 * refused and the step stays NAME. */
int mmo_creator_set_name(mmo_creator *c, const char *latin1);

/* Hide the screens (a pick or a create has gone on the wire). */
void mmo_creator_begin_wait(mmo_creator *c);

/* The create that was submitted was refused, with [why] a sentence a player reads. */
void mmo_creator_refuse(mmo_creator *c, const char *why);

/* 1 when an existing list row has been chosen. */
int mmo_creator_has_pick(const mmo_creator *c);
int mmo_creator_pick_index(const mmo_creator *c);

/* 1 when a row's delete was confirmed, and which row it was. */
int mmo_creator_has_delete(const mmo_creator *c);
int mmo_creator_delete_index(const mmo_creator *c);

/* 1 when the four creator steps are filled and A on the body was hit. */
int mmo_creator_ready(const mmo_creator *c);

/* Fill a CreateCharacter request from the four steps. `out->name` points
 * at this object's name buffer. Returns 0, or -1 if not ready. */
int mmo_creator_fill_create(const mmo_creator *c, mmo_create_character *out);

const char *mmo_creator_title(const mmo_creator *c);
const char *mmo_creator_hint(const mmo_creator *c);

int mmo_creator_visible_count(const mmo_creator *c);
int mmo_creator_get_row(const mmo_creator *c, int vis, mmo_creator_row *out);

/* The whole step, not the visible window: every row of the current step,
 * addressed absolutely. A scrolling list widget owns its own window and
 * wants the full list; the visible pair above stays for the suite. */
int mmo_creator_row_count(const mmo_creator *c);
int mmo_creator_row_at(const mmo_creator *c, int idx, mmo_creator_row *out);

#endif /* OPENMMO_CREATOR_H */
