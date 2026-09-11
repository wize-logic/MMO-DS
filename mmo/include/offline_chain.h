/*
 * The session records behind an offline save, in the bytes the server reads
 * them out of.
 */
#ifndef MMO_OFFLINE_CHAIN_H
#define MMO_OFFLINE_CHAIN_H

#include <stddef.h>

#include "codec.h"
#include "mmo.h"

#define MMO_CHAIN_MAGIC   "OMCH"
#define MMO_CHAIN_VERSION 1

/* What one chain may carry. The server holds the same numbers. */
#define MMO_CHAIN_MAX_LINKS 4096
#define MMO_CHAIN_MAX_BYTES (16u * 1024u * 1024u)

/*
 * The third blob on the same channel: the image a session wrote out for offline play, sent as
 * it was written so the server can keep it.
 */
#define MMO_EXPORT_MAGIC   "OMEX"
#define MMO_EXPORT_VERSION 1

/* The Platinum backup chip is 512 KiB; the server refuses anything else. This
 * side only refuses a file no chip could be, so a port that grows the image
 * fails on the far end with a sentence rather than here with a silent no. */
#define MMO_EXPORT_IMAGE_MAX_BYTES (1024u * 1024u)

/* One session: its record verbatim, and the recording it names. */
typedef struct {
    char  *record;       /* the `.link` file's text, NUL-terminated */
    u8    *input;        /* the `.inp` recording, or NULL when there is none */
    size_t input_len;
} mmo_chain_link;

/* `anchor` is the export hash the chain claims to start from, or the word
 * "none", which is a character played offline from a New Game, the absence
 * of a file, which is not something a client could have forged. */
typedef struct {
    char            anchor[65];
    mmo_chain_link *links;
    int             nlinks;
} mmo_chain;

/* Encode into `w`. Returns 0, or -1 with w->err set and a line on stderr. */
int mmo_chain_encode(mmo_wbuf *w, const mmo_chain *c);

/* Read an encoded chain back off disk, checking the magic and the version.
 * *out is malloc'd and holds the whole blob including its head; the caller
 * frees it. Returns 0, or -1 with a line on stderr. */
int mmo_chain_read(const char *path, u8 **out, size_t *n);

/* Free what a collector filled in. Safe on a zeroed struct. */
void mmo_chain_free(mmo_chain *c);

/* Encode a save image as the offline-copy blob. Returns 0, or -1 with w->err
 * set and a line on stderr. */
int mmo_export_image_encode(mmo_wbuf *w, const u8 *image, size_t len);

#endif /* MMO_OFFLINE_CHAIN_H */
