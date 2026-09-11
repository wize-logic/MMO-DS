/* The session chain's one encoding. See offline_chain.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "offline_chain.h"

int mmo_chain_encode(mmo_wbuf *w, const mmo_chain *c)
{
    size_t total = 0;
    int i;

    if (w == NULL || c == NULL)
        return -1;
    if (c->nlinks < 0 || c->nlinks > MMO_CHAIN_MAX_LINKS) {
        fprintf(stderr, "openmmo: %d sessions in one chain, past %d\n",
                c->nlinks, MMO_CHAIN_MAX_LINKS);
        w->err = 1;
        return -1;
    }

    for (i = 0; i < c->nlinks; i++) {
        size_t rec = (c->links[i].record != NULL)
                         ? strlen(c->links[i].record) : 0;

        total += rec + c->links[i].input_len;
        if (total > MMO_CHAIN_MAX_BYTES) {
            fprintf(stderr, "openmmo: this run is longer than the %u bytes of"
                    " session records the server will look at\n",
                    MMO_CHAIN_MAX_BYTES);
            w->err = 1;
            return -1;
        }
    }

    mmo_put_bytes_u8(w, MMO_CHAIN_MAGIC, 4);
    mmo_put_u16le(w, MMO_CHAIN_VERSION);
    mmo_put_u16le(w, (u16)c->nlinks);
    {
        size_t n = strlen(c->anchor);

        if (n > 0xFFFF) {
            w->err = 1;
            return -1;
        }
        mmo_put_u16le(w, (u16)n);
        if (n > 0)
            mmo_put_bytes(w, c->anchor, n);
    }
    for (i = 0; i < c->nlinks; i++) {
        const mmo_chain_link *l = &c->links[i];
        size_t rec = (l->record != NULL) ? strlen(l->record) : 0;

        mmo_put_u32le(w, (u32)rec);
        if (rec > 0)
            mmo_put_bytes(w, l->record, rec);
        mmo_put_u32le(w, (u32)l->input_len);
        if (l->input_len > 0)
            mmo_put_bytes(w, l->input, l->input_len);
    }
    return w->err ? -1 : 0;
}

/* The magic and version at the head of a blob, or -1 with a line. */
static int chain_head(const u8 *blob, size_t n)
{
    unsigned version;

    if (blob == NULL || n < 7) {
        fprintf(stderr, "openmmo: that file is too short to be a session"
                " chain\n");
        return -1;
    }
    if (blob[0] != 4 || memcmp(blob + 1, MMO_CHAIN_MAGIC, 4) != 0) {
        fprintf(stderr, "openmmo: that file is not a session chain\n");
        return -1;
    }
    version = (unsigned)blob[5] | ((unsigned)blob[6] << 8);
    if (version != MMO_CHAIN_VERSION) {
        fprintf(stderr, "openmmo: that session chain is version %u and this"
                " game writes %d\n", version, MMO_CHAIN_VERSION);
        return -1;
    }
    return 0;
}

int mmo_chain_read(const char *path, u8 **out, size_t *n)
{
    FILE *f;
    long size;
    u8 *buf;
    size_t got;

    if (out == NULL || n == NULL)
        return -1;
    *out = NULL;
    *n = 0;
    if (path == NULL || path[0] == '\0')
        return -1;
    f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "openmmo: cannot read the session chain at %s\n", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "openmmo: cannot measure the session chain at %s\n",
                path);
        fclose(f);
        return -1;
    }
    if ((unsigned long)size > (unsigned long)MMO_CHAIN_MAX_BYTES + 65536ul) {
        fprintf(stderr, "openmmo: the session chain at %s is larger than one"
                " this game writes\n", path);
        fclose(f);
        return -1;
    }
    buf = (u8 *)malloc((size_t)size + 1u);
    if (buf == NULL) {
        fclose(f);
        return -1;
    }
    got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) {
        fprintf(stderr, "openmmo: the session chain at %s ended early\n", path);
        free(buf);
        return -1;
    }
    if (chain_head(buf, got) != 0) {
        free(buf);
        return -1;
    }
    *out = buf;
    *n = got;
    return 0;
}

void mmo_chain_free(mmo_chain *c)
{
    int i;

    if (c == NULL || c->links == NULL) {
        if (c != NULL)
            c->nlinks = 0;
        return;
    }
    for (i = 0; i < c->nlinks; i++) {
        free(c->links[i].record);
        free(c->links[i].input);
    }
    free(c->links);
    c->links = NULL;
    c->nlinks = 0;
}

int mmo_export_image_encode(mmo_wbuf *w, const u8 *image, size_t len)
{
    if (w == NULL)
        return -1;
    if (image == NULL || len == 0) {
        fprintf(stderr, "openmmo: there is no offline copy to send\n");
        w->err = 1;
        return -1;
    }
    if (len > MMO_EXPORT_IMAGE_MAX_BYTES) {
        fprintf(stderr, "openmmo: the offline copy is %zu bytes, which no"
                " backup chip is\n", len);
        w->err = 1;
        return -1;
    }
    mmo_put_bytes_u8(w, MMO_EXPORT_MAGIC, 4);
    mmo_put_u16le(w, MMO_EXPORT_VERSION);
    mmo_put_u32le(w, (u32)len);
    mmo_put_bytes(w, image, len);
    return w->err ? -1 : 0;
}
