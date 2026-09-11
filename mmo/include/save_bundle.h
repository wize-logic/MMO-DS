/* A whole offline save folder as one file, so it can travel. */
#ifndef MMO_SAVE_BUNDLE_H
#define MMO_SAVE_BUNDLE_H

#include <stddef.h>

#include "codec.h"
#include "mmo.h"

/* The head every blob of ours carries: `u8 4`, four bytes of magic, a u16
 * version. A reader that does not know the version says so and refuses rather
 * than guessing at the fields behind it. */
#define MMO_BUNDLE_MAGIC   "OMSB"
#define MMO_BUNDLE_VERSION 1

/* What one bundle may hold. */
#define MMO_BUNDLE_MAX_BYTES (48u * 1024u * 1024u)
#define MMO_BUNDLE_MAX_ITEMS 9000
/* `YYYYmmdd-HHMMSS` and a terminator, which is every name a section carries.
 * Kept here rather than taken from launch_plan.h so that this format does not
 * depend on the front door, the same reason offline_import.h states its own
 * sizes. */
#define MMO_BUNDLE_NAME      20
/* The header record, in the same `<key> <value>` lines every record beside a
 * save is written in. A person opening a bundle in a text editor reads the
 * first few hundred bytes and knows what they are holding. */
#define MMO_BUNDLE_NOTE      512

/* What a section is. An unknown kind is skipped rather than refused: a bundle
 * written by a later launcher that grew a seventh kind still puts back the six
 * this one understands. */
enum {
    MMO_BUNDLE_IMAGE   = 1,  /* the backup image itself; exactly one, required */
    MMO_BUNDLE_REPORT  = 2,  /* the OMIR report of what that image holds */
    MMO_BUNDLE_ANCHOR  = 3,  /* sessions/<name>.export, the play's starting point */
    MMO_BUNDLE_LINK    = 4,  /* sessions/<name>.link, one session's record */
    MMO_BUNDLE_INPUT   = 5,  /* sessions/<name>.inp, what the pad did */
    MMO_BUNDLE_RESTORE = 6   /* the marker saying an earlier image was put back */
};

typedef struct {
    u8     kind;
    /* The stamp the piece is filed under, or empty for the pieces there is
     * only ever one of. Checked as a stamp by the reader before it becomes
     * part of a path. */
    char   name[MMO_BUNDLE_NAME];
    u8    *data;
    size_t len;
} mmo_bundle_item;

/*
 * `note` is the header text; `items` is what the folder holds. On decode both are malloc'd and
 * mmo_bundle_free owns them.
 */
typedef struct {
    char             note[MMO_BUNDLE_NOTE];
    mmo_bundle_item *items;
    int              nitems;
} mmo_bundle;

/* Encode into `w`. Returns 0, or -1 with w->err set and a line on stderr. */
int mmo_bundle_encode(mmo_wbuf *w, const mmo_bundle *b);

/*
 * Read a bundle out of `blob`. *out is filled in with malloc'd pieces the caller frees with
 * mmo_bundle_free.
 */
int mmo_bundle_decode(const u8 *blob, size_t n, mmo_bundle *out);

void mmo_bundle_free(mmo_bundle *b);

/*
 * One `<key> <value>` line of the header, as text or as a number. 0 and `out` filled when the
 * line is there, -1 when it is not.
 */
int mmo_bundle_note_text(const mmo_bundle *b, const char *key, char *out,
                         size_t cap);
int mmo_bundle_note_number(const mmo_bundle *b, const char *key, long *out);

#endif /* MMO_SAVE_BUNDLE_H */
