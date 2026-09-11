/* The one encoding of a carried save. See save_bundle.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "save_bundle.h"

int mmo_bundle_encode(mmo_wbuf *w, const mmo_bundle *b)
{
    size_t total = 0;
    size_t note;
    int i;

    if (w == NULL || b == NULL)
        return -1;
    if (b->nitems < 0 || b->nitems > MMO_BUNDLE_MAX_ITEMS) {
        fprintf(stderr, "openmmo: %d pieces in one saved game, past %d\n",
                b->nitems, MMO_BUNDLE_MAX_ITEMS);
        w->err = 1;
        return -1;
    }
    for (i = 0; i < b->nitems; i++) {
        if (b->items[i].len > 0 && b->items[i].data == NULL) {
            fprintf(stderr, "openmmo: a piece of the saved game names bytes"
                    " that are not there\n");
            w->err = 1;
            return -1;
        }
        if (strlen(b->items[i].name) >= MMO_BUNDLE_NAME) {
            w->err = 1;
            return -1;
        }
        total += b->items[i].len + MMO_BUNDLE_NAME + 8;
        if (total > MMO_BUNDLE_MAX_BYTES) {
            fprintf(stderr, "openmmo: this saved game is larger than the %u"
                    " bytes one file holds\n", MMO_BUNDLE_MAX_BYTES);
            w->err = 1;
            return -1;
        }
    }

    note = strlen(b->note);
    if (note >= MMO_BUNDLE_NOTE) {
        w->err = 1;
        return -1;
    }
    mmo_put_bytes_u8(w, MMO_BUNDLE_MAGIC, 4);
    mmo_put_u16le(w, MMO_BUNDLE_VERSION);
    mmo_put_u16le(w, (u16)note);
    if (note > 0)
        mmo_put_bytes(w, b->note, note);
    for (i = 0; i < b->nitems; i++) {
        const mmo_bundle_item *it = &b->items[i];

        mmo_put_u8(w, it->kind);
        mmo_put_bytes_u8(w, it->name, strlen(it->name));
        mmo_put_u32le(w, (u32)it->len);
        if (it->len > 0)
            mmo_put_bytes(w, it->data, it->len);
    }
    return w->err ? -1 : 0;
}

void mmo_bundle_free(mmo_bundle *b)
{
    int i;

    if (b == NULL)
        return;
    for (i = 0; i < b->nitems; i++)
        free(b->items[i].data);
    free(b->items);
    b->items = NULL;
    b->nitems = 0;
    b->note[0] = '\0';
}

/* Room for one more section, doubling. -1 leaves what is already read intact
 * for the caller's free. */
static int room(mmo_bundle *b, int *cap)
{
    mmo_bundle_item *grown;
    int want = (*cap == 0) ? 16 : *cap * 2;

    if (b->nitems < *cap)
        return 0;
    if (want > MMO_BUNDLE_MAX_ITEMS + 1)
        want = MMO_BUNDLE_MAX_ITEMS + 1;
    grown = (mmo_bundle_item *)realloc(b->items, (size_t)want * sizeof *grown);
    if (grown == NULL)
        return -1;
    memset(grown + *cap, 0, (size_t)(want - *cap) * sizeof *grown);
    b->items = grown;
    *cap = want;
    return 0;
}

int mmo_bundle_decode(const u8 *blob, size_t n, mmo_bundle *out)
{
    mmo_rbuf r;
    unsigned version;
    size_t note;
    int cap = 0;

    if (out == NULL)
        return -1;
    memset(out, 0, sizeof *out);
    if (blob == NULL || n < 9) {
        fprintf(stderr, "openmmo: that file is too short to be a saved game\n");
        return -1;
    }
    if (blob[0] != 4 || memcmp(blob + 1, MMO_BUNDLE_MAGIC, 4) != 0) {
        fprintf(stderr, "openmmo: that file is not a saved game this launcher"
                " wrote\n");
        return -1;
    }
    version = (unsigned)blob[5] | ((unsigned)blob[6] << 8);
    if (version != MMO_BUNDLE_VERSION) {
        fprintf(stderr, "openmmo: that saved game is version %u and this"
                " launcher reads %d\n", version, MMO_BUNDLE_VERSION);
        return -1;
    }
    if (n > MMO_BUNDLE_MAX_BYTES) {
        fprintf(stderr, "openmmo: that saved game is larger than one this"
                " launcher writes\n");
        return -1;
    }

    mmo_rbuf_init(&r, blob, n);
    r.pos = 7;
    note = mmo_get_u16le(&r);
    if (note >= MMO_BUNDLE_NOTE || mmo_rbuf_remaining(&r) < note) {
        fprintf(stderr, "openmmo: that saved game's first lines run off the"
                " end of the file\n");
        return -1;
    }
    mmo_get_bytes(&r, out->note, note);
    out->note[note] = '\0';

    while (mmo_rbuf_remaining(&r) > 0) {
        mmo_bundle_item *it;
        u8   kind;
        char name[MMO_BUNDLE_NAME];
        size_t namelen;
        u32  len;

        kind = mmo_get_u8(&r);
        memset(name, 0, sizeof name);
        namelen = mmo_get_bytes_u8(&r, name, sizeof name - 1);
        len = mmo_get_u32le(&r);
        if (r.err || namelen >= sizeof name) {
            fprintf(stderr, "openmmo: that saved game ends in the middle of a"
                    " piece of itself\n");
            mmo_bundle_free(out);
            return -1;
        }
        if (len > mmo_rbuf_remaining(&r)) {
            fprintf(stderr, "openmmo: a piece of that saved game says it is %u"
                    " bytes and the file has fewer\n", (unsigned)len);
            mmo_bundle_free(out);
            return -1;
        }
        if (out->nitems >= MMO_BUNDLE_MAX_ITEMS || room(out, &cap) != 0) {
            fprintf(stderr, "openmmo: that saved game holds more pieces than"
                    " one this launcher writes\n");
            mmo_bundle_free(out);
            return -1;
        }
        it = &out->items[out->nitems];
        it->kind = kind;
        snprintf(it->name, sizeof it->name, "%s", name);
        it->len = len;
        it->data = NULL;
        if (len > 0) {
            /* One past the end and NUL, so a record can be read as a string
             * without the caller copying it again. */
            it->data = (u8 *)malloc((size_t)len + 1u);
            if (it->data == NULL) {
                mmo_bundle_free(out);
                return -1;
            }
            mmo_get_bytes(&r, it->data, len);
            it->data[len] = '\0';
            if (r.err) {
                free(it->data);
                it->data = NULL;
                mmo_bundle_free(out);
                return -1;
            }
        }
        out->nitems++;
    }
    return 0;
}

/* The value of `<key> <value>` in the header, verbatim to the newline. */
static const char *note_line(const mmo_bundle *b, const char *key, size_t *len)
{
    size_t klen = strlen(key);
    const char *p;

    if (b == NULL)
        return NULL;
    for (p = b->note; *p != '\0'; ) {
        const char *eol = strchr(p, '\n');
        size_t line = (eol != NULL) ? (size_t)(eol - p) : strlen(p);

        if (line > klen && strncmp(p, key, klen) == 0 && p[klen] == ' ') {
            *len = line - klen - 1;
            return p + klen + 1;
        }
        if (eol == NULL)
            break;
        p = eol + 1;
    }
    return NULL;
}

int mmo_bundle_note_text(const mmo_bundle *b, const char *key, char *out,
                         size_t cap)
{
    const char *v;
    size_t len = 0;

    if (out == NULL || cap == 0)
        return -1;
    out[0] = '\0';
    v = note_line(b, key, &len);
    if (v == NULL)
        return -1;
    if (len >= cap)
        len = cap - 1;
    memcpy(out, v, len);
    out[len] = '\0';
    return 0;
}

int mmo_bundle_note_number(const mmo_bundle *b, const char *key, long *out)
{
    char text[64];

    if (mmo_bundle_note_text(b, key, text, sizeof text) != 0)
        return -1;
    if (text[0] == '\0')
        return -1;
    if (out != NULL)
        *out = strtol(text, NULL, 10);
    return 0;
}
