/* The save report's one encoding. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "offline_import.h"

static void put_text(mmo_wbuf *w, const char *s)
{
    size_t n = (s != NULL) ? strlen(s) : 0;

    if (n > 0xFFFF) {
        w->err = 1;
        return;
    }
    mmo_put_u16le(w, (u16)n);
    if (n > 0)
        mmo_put_bytes(w, s, n);
}

/* A list header, refusing a count this side is not allowed to send. Returns 0
 * when the count is sane; -1 (with err set) otherwise, so an overlong list is
 * a loud failure here rather than a refusal from the far end. */
static int put_count_u16(mmo_wbuf *w, int n, int cap, const char *what)
{
    if (n < 0 || n > cap) {
        fprintf(stderr, "openmmo: %d %s in one save report, past %d\n",
                n, what, cap);
        w->err = 1;
        return -1;
    }
    mmo_put_u16le(w, (u16)n);
    return 0;
}

static void put_stats(mmo_wbuf *w, const u8 *v)
{
    int i;

    for (i = 0; i < MMO_IMPORT_STATS; i++)
        mmo_put_u8(w, v[i]);
}

static void put_mon(mmo_wbuf *w, const mmo_import_mon *m)
{
    int i;
    int nmoves = m->nmoves;

    if (nmoves < 0)
        nmoves = 0;
    if (nmoves > 4)
        nmoves = 4;

    mmo_put_s32le(w, m->pid);
    mmo_put_u16le(w, m->dex);
    mmo_put_u8(w, m->form);
    mmo_put_u8(w, m->level);
    mmo_put_s32le(w, m->xp);
    put_stats(w, m->ivs);
    put_stats(w, m->evs);
    mmo_put_u8(w, (u8)nmoves);
    for (i = 0; i < nmoves; i++) {
        mmo_put_u16le(w, m->moves[i].move);
        mmo_put_u8(w, m->moves[i].pp);
        mmo_put_u8(w, m->moves[i].pp_ups);
    }
    put_text(w, m->nickname);
    put_text(w, m->ot_name);
    mmo_put_s32le(w, m->ot_id);
    mmo_put_u16le(w, m->ability);
    mmo_put_bool(w, m->hidden_ability);
    mmo_put_u8(w, m->nature);
    mmo_put_bool(w, m->shiny);
    mmo_put_u16le(w, m->held_item);
    mmo_put_u8(w, m->friendship);
    mmo_put_bool(w, m->egg);
    mmo_put_u8(w, m->egg_cycles);
    mmo_put_u8(w, m->box ? 1 : 0);
    mmo_put_u16le(w, m->slot);
    for (i = 0; i < MMO_IMPORT_CONDITIONS; i++)
        mmo_put_u8(w, m->cond[i]);
    mmo_put_u8(w, m->sheen);
    /* Signed on the wire because the server reads it with S64LE, the same way
     * the live monster record's mask crosses. Twenty of the sixty-four bits
     * are ribbons and the rest are the engine's own spare room. */
    mmo_put_s64le(w, (s64)m->ribbons_super);
    mmo_put_u16le(w, m->met_location);
    mmo_put_u16le(w, m->ball);
    mmo_put_u8(w, m->pokerus);
    mmo_put_u8(w, m->markings);
    /* The condition word last, where the live wire carries it too. It arrives
     * here masked to the bits the engine defines; the server masks it again on
     * the way in, because what a report says is never what this side promised
     * it would say. */
    mmo_put_u16le(w, m->status);
}

int mmo_import_report_encode(mmo_wbuf *w, const mmo_import_report *r)
{
    int i;

    mmo_put_bytes_u8(w, MMO_IMPORT_MAGIC, 4);
    mmo_put_u16le(w, MMO_IMPORT_VERSION);
    put_text(w, r->sha256);
    mmo_put_s32le(w, r->client_revision);
    mmo_put_s32le(w, r->trainer_id);
    mmo_put_s32le(w, r->money);
    mmo_put_s32le(w, r->badges);
    mmo_put_s32le(w, r->play_seconds);

    mmo_put_u8(w, r->position.bank);
    mmo_put_u16le(w, r->position.map);
    mmo_put_s16le(w, r->position.x);
    mmo_put_s16le(w, r->position.y);
    mmo_put_u16le(w, r->black_out_warp);

    if (put_count_u16(w, r->nmonsters, MMO_IMPORT_MAX_MONSTERS, "monsters") != 0)
        return -1;
    for (i = 0; i < r->nmonsters; i++)
        put_mon(w, &r->monsters[i]);

    if (put_count_u16(w, r->nbag, MMO_IMPORT_MAX_BAG, "bag lines") != 0)
        return -1;
    for (i = 0; i < r->nbag; i++) {
        mmo_put_u16le(w, r->bag[i].item);
        mmo_put_u16le(w, r->bag[i].quantity);
    }

    if (put_count_u16(w, r->ndex_seen, MMO_IMPORT_MAX_DEX, "seen species") != 0)
        return -1;
    for (i = 0; i < r->ndex_seen; i++)
        mmo_put_u16le(w, r->dex_seen[i]);

    if (put_count_u16(w, r->ndex_caught, MMO_IMPORT_MAX_DEX, "caught species") != 0)
        return -1;
    for (i = 0; i < r->ndex_caught; i++)
        mmo_put_u16le(w, r->dex_caught[i]);

    if (put_count_u16(w, r->nflags, MMO_IMPORT_MAX_FLAGS, "story flags") != 0)
        return -1;
    for (i = 0; i < r->nflags; i++)
        mmo_put_u16le(w, r->flags[i]);

    if (put_count_u16(w, r->nvars, MMO_IMPORT_MAX_VARS, "story variables") != 0)
        return -1;
    for (i = 0; i < r->nvars; i++) {
        mmo_put_u16le(w, r->vars[i].id);
        mmo_put_s16le(w, r->vars[i].value);
    }

    if (r->nblocks < 0 || r->nblocks > MMO_IMPORT_MAX_BLOCKS) {
        fprintf(stderr, "openmmo: %d save blocks in one report, past %d\n",
                r->nblocks, MMO_IMPORT_MAX_BLOCKS);
        w->err = 1;
        return -1;
    }
    mmo_put_u8(w, (u8)r->nblocks);
    for (i = 0; i < r->nblocks; i++) {
        if (r->blocks[i].len > 0xFFFF) {
            fprintf(stderr, "openmmo: save block %u is %zu bytes, past 65535\n",
                    (unsigned)r->blocks[i].id, r->blocks[i].len);
            w->err = 1;
            return -1;
        }
        mmo_put_u8(w, r->blocks[i].id);
        mmo_put_u16le(w, (u16)r->blocks[i].len);
        if (r->blocks[i].len > 0)
            mmo_put_bytes(w, r->blocks[i].data, r->blocks[i].len);
    }

    return w->err ? -1 : 0;
}

int mmo_import_report_write(const char *path, const mmo_import_report *r)
{
    mmo_wbuf w;
    char tmp[1024];
    char answered[sizeof tmp + 16];
    FILE *f;
    size_t wrote;
    int rc = -1;

    if (path == NULL || path[0] == '\0')
        return -1;
    if ((size_t)snprintf(tmp, sizeof tmp, "%s.tmp", path) >= sizeof tmp) {
        fprintf(stderr, "openmmo: the save report path is too long: %s\n", path);
        return -1;
    }

    mmo_wbuf_init(&w);
    if (mmo_import_report_encode(&w, r) != 0) {
        fprintf(stderr, "openmmo: the save report could not be built\n");
        mmo_wbuf_free(&w);
        return -1;
    }

    f = fopen(tmp, "wb");
    if (f == NULL) {
        fprintf(stderr, "openmmo: cannot write the save report at %s\n", tmp);
        mmo_wbuf_free(&w);
        return -1;
    }
    wrote = fwrite(w.data, 1, w.len, f);
    if (fclose(f) != 0 || wrote != w.len) {
        fprintf(stderr, "openmmo: the save report did not reach %s\n", tmp);
        remove(tmp);
        mmo_wbuf_free(&w);
        return -1;
    }
    /* Rename over the old one: the report either is the last save whole or is
     * the one before it, and never a half of either. */
    if (rename(tmp, path) != 0) {
        fprintf(stderr, "openmmo: cannot put the save report at %s\n", path);
        remove(tmp);
    } else {
        /*
         * A report the server took is set aside under `<path>.landed`, and that name is what
         * the front door reads to say whether the save it offered was taken.
         */
        snprintf(answered, sizeof answered, "%s.landed", path);
        if (remove(answered) != 0 && errno != ENOENT)
            fprintf(stderr, "openmmo: the answer about the last save could not"
                    " be cleared at %s\n", answered);
        rc = 0;
    }
    mmo_wbuf_free(&w);
    return rc;
}

/* The magic and version at the head of a blob, or -1 with a line. On success
 * *body is the offset just past the version. */
static int report_head(const u8 *report, size_t n, size_t *body)
{
    if (report == NULL || n < 7) {
        fprintf(stderr, "openmmo: that file is too short to be a save report\n");
        return -1;
    }
    if (report[0] != 4 || memcmp(report + 1, MMO_IMPORT_MAGIC, 4) != 0) {
        fprintf(stderr, "openmmo: that file is not a save report\n");
        return -1;
    }
    {
        unsigned version = (unsigned)report[5] | ((unsigned)report[6] << 8);

        if (version != MMO_IMPORT_VERSION) {
            fprintf(stderr, "openmmo: that save report is version %u and this"
                    " game writes %d\n", version, MMO_IMPORT_VERSION);
            return -1;
        }
    }
    *body = 7;
    return 0;
}

int mmo_import_report_read(const char *path, u8 **out, size_t *n)
{
    FILE *f;
    long size;
    u8 *buf;
    size_t got;
    size_t body;

    *out = NULL;
    *n = 0;
    f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "openmmo: cannot read the save report at %s\n", path);
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "openmmo: cannot measure the save report at %s\n", path);
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
        fprintf(stderr, "openmmo: the save report at %s ended early\n", path);
        free(buf);
        return -1;
    }
    if (report_head(buf, got, &body) != 0) {
        free(buf);
        return -1;
    }
    *out = buf;
    *n = got;
    return 0;
}

int mmo_import_report_stamp(const u8 *report, size_t n,
                            s32 *play_seconds, char sha256[65], s32 *money)
{
    mmo_rbuf r;
    size_t body;
    u16 shalen;
    char hex[65];
    int i;

    if (report_head(report, n, &body) != 0)
        return -1;
    mmo_rbuf_init(&r, report + body, n - body);
    shalen = mmo_get_u16le(&r);
    if (r.err || shalen >= sizeof hex) {
        fprintf(stderr, "openmmo: that save report's hash is %u bytes\n",
                (unsigned)shalen);
        return -1;
    }
    for (i = 0; i < (int)shalen; i++)
        hex[i] = (char)mmo_get_u8(&r);
    hex[shalen] = '\0';
    (void)mmo_get_s32le(&r); /* client revision */
    (void)mmo_get_s32le(&r); /* trainer id */
    {
        s32 wallet = mmo_get_s32le(&r);

        if (money != NULL)
            *money = wallet;
    }
    (void)mmo_get_s32le(&r); /* badges */
    {
        s32 seconds = mmo_get_s32le(&r);

        if (r.err) {
            fprintf(stderr, "openmmo: that save report has no header\n");
            return -1;
        }
        if (play_seconds != NULL)
            *play_seconds = seconds;
    }
    if (sha256 != NULL)
        memcpy(sha256, hex, shalen + 1u);
    return 0;
}
