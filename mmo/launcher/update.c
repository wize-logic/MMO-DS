/* Fetch, prove, then install; never the other order. */

#include "update.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "fetch.h"
#include "platform.h"

/* The documents are small and the caps generous: a full inventory of 512
 * entries is under 100 KB. A signature is the key's modulus length. */
#define DOC_CAP (4u << 20)
#define SIG_CAP 4096

static int fail(char *msg, size_t cap, const char *fmt, const char *a,
                const char *b)
{
    if (msg != NULL && cap > 0)
        snprintf(msg, cap, fmt, a, b);
    return -1;
}

int mmo_update_load_key(const char *path, mmo_rsa_pubkey *key,
                        char *msg, size_t msgcap)
{
    char err[192];
    char *pem;
    long pemlen;
    FILE *f;
    int rc;

    f = fopen(path, "rb");
    if (f == NULL)
        return fail(msg, msgcap, "the feed key %.400s cannot be read", path,
                    NULL);
    if (fseek(f, 0, SEEK_END) != 0 || (pemlen = ftell(f)) <= 0 ||
        pemlen > 1 << 16 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return fail(msg, msgcap, "the feed key %.400s is not a key", path,
                    NULL);
    }
    pem = malloc((size_t)pemlen);
    if (pem == NULL || fread(pem, 1, (size_t)pemlen, f) != (size_t)pemlen) {
        fclose(f);
        free(pem);
        return fail(msg, msgcap, "the feed key %.400s cannot be read", path,
                    NULL);
    }
    fclose(f);
    rc = mmo_rsa_pubkey_text(pem, (size_t)pemlen, key, err, sizeof err);
    free(pem);
    if (rc != 0)
        return fail(msg, msgcap, "the feed key %.300s is unusable: %.100s",
                    path, err);
    return 0;
}

/* ---------------------------------------------------------------------- */

/* Every directory of `path` above the last component, made if missing. The
 * names come out of mmo_feed_sanitize, so the separators are '/'. */
static int mkdirs_for(const char *path)
{
    char dir[MMO_LAUNCH_PATH + MMO_FEED_NAME];
    size_t i, n = strlen(path);

    if (n >= sizeof dir)
        return -1;
    memcpy(dir, path, n + 1);
    for (i = 1; i < n; i++) {
        if (dir[i] != '/' && dir[i] != '\\')
            continue;
        dir[i] = '\0';
        mmo_plat_mkdir(dir);        /* exists already is fine */
        dir[i] = path[i];
    }
    return 0;
}

/* `name`, spelled into a URL path: the unreserved characters and '/' ride as
 * themselves, everything else as %xx. */
static void url_escape(const char *name, char *out, size_t cap)
{
    static const char keep[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                               "abcdefghijklmnopqrstuvwxyz"
                               "0123456789-._~/";
    size_t at = 0;

    for (; *name != '\0' && at + 4 < cap; name++) {
        if (strchr(keep, *name) != NULL)
            out[at++] = *name;
        else
            at += (size_t)snprintf(out + at, cap - at, "%%%02X",
                                   (unsigned char)*name);
    }
    out[at] = '\0';
}

/* One document (or signature) off the server, into fresh memory. */
static void *fetch_doc(const char *host, const char *port, const char *base,
                       const char *name, size_t cap, long *len,
                       char *msg, size_t msgcap)
{
    char path[MMO_FETCH_PATH], err[MMO_FETCH_ERR];
    void *buf = malloc(cap);
    long n;

    if (buf == NULL) {
        fail(msg, msgcap, "out of memory", NULL, NULL);
        return NULL;
    }
    snprintf(path, sizeof path, "%s/%s", base, name);
    n = mmo_fetch_buf(host, port, path, buf, cap, err, sizeof err);
    if (n < 0) {
        free(buf);
        fail(msg, msgcap, "%.100s cannot be fetched: %.350s", name, err);
        return NULL;
    }
    *len = n;
    return buf;
}

/* The four feed documents, verified and parsed, still in memory so a caller
 * can write what it verified. */
struct feed_docs {
    char *main_txt, *main_sig, *upd_txt, *upd_sig;
    long  main_len, main_siglen, upd_len, upd_siglen;
};

static void docs_free(struct feed_docs *d)
{
    free(d->main_txt);
    free(d->main_sig);
    free(d->upd_txt);
    free(d->upd_sig);
    memset(d, 0, sizeof *d);
}

static int docs_fetch(const char *host, const char *port, const char *base,
                      const mmo_rsa_pubkey *key, struct feed_docs *d,
                      mmo_feed_main *m, mmo_feed_update *u,
                      char *msg, size_t msgcap)
{
    char err[192];

    memset(d, 0, sizeof *d);
    d->main_txt = fetch_doc(host, port, base, "main_feed.txt", DOC_CAP,
                            &d->main_len, msg, msgcap);
    if (d->main_txt == NULL)
        return -1;
    d->main_sig = fetch_doc(host, port, base, "main_feed.sig256", SIG_CAP,
                            &d->main_siglen, msg, msgcap);
    if (d->main_sig == NULL)
        goto bad;
    d->upd_txt = fetch_doc(host, port, base, "update_feed.txt", DOC_CAP,
                           &d->upd_len, msg, msgcap);
    if (d->upd_txt == NULL)
        goto bad;
    d->upd_sig = fetch_doc(host, port, base, "update_feed.sig256", SIG_CAP,
                           &d->upd_siglen, msg, msgcap);
    if (d->upd_sig == NULL)
        goto bad;

    /* Both signatures before either parse, the same rule mmo_feed_load
     * keeps: an unverified document never reaches a reader. */
    if (mmo_rsa_verify_sha256(key, (const u8 *)d->main_txt,
                              (size_t)d->main_len, (const u8 *)d->main_sig,
                              (size_t)d->main_siglen) == 0 ||
        mmo_rsa_verify_sha256(key, (const u8 *)d->upd_txt,
                              (size_t)d->upd_len, (const u8 *)d->upd_sig,
                              (size_t)d->upd_siglen) == 0) {
        fail(msg, msgcap, "the server's feed is not signed by a key this "
                          "launcher trusts", NULL, NULL);
        goto bad;
    }
    if (mmo_feed_parse_main(d->main_txt, (size_t)d->main_len, m, err,
                            sizeof err) != 0 ||
        mmo_feed_parse_update(d->upd_txt, (size_t)d->upd_len, u, err,
                              sizeof err) != 0) {
        fail(msg, msgcap, "the server's feed cannot be read: %.400s", err,
             NULL);
        goto bad;
    }
    if (u->overflow != 0) {
        fail(msg, msgcap, "the server's feed lists more files than this "
                          "launcher can hold", NULL, NULL);
        goto bad;
    }
    if (u->min_launcher_version > MMO_FEED_LAUNCHER_VERSION) {
        fail(msg, msgcap, "this update needs a newer launcher than this one",
             NULL, NULL);
        goto bad;
    }
    return 0;
bad:
    docs_free(d);
    return -1;
}

/*
 * The published revision, and nothing else fetched to learn it. update.h says why an app asks
 * this question rather than the one above it.
 */
int mmo_update_latest_revision(const char *url, const mmo_rsa_pubkey *key,
                               int *revision, char *msg, size_t msgcap)
{
    mmo_feed_main main_feed;
    char host[MMO_FETCH_HOST], port[16], base[MMO_FETCH_PATH];
    char err[MMO_FETCH_ERR];
    char *txt = NULL, *sig = NULL;
    long txtlen = 0, siglen = 0;
    int rc = -1;

    if (revision != NULL)
        *revision = 0;
    if (msg != NULL && msgcap > 0)
        msg[0] = '\0';
    if (mmo_fetch_split(url, host, sizeof host, port, sizeof port,
                        base, sizeof base, err, sizeof err) != 0)
        return fail(msg, msgcap, "%.400s", err, NULL);

    txt = fetch_doc(host, port, base, "main_feed.txt", DOC_CAP, &txtlen,
                    msg, msgcap);
    if (txt == NULL)
        goto done;
    sig = fetch_doc(host, port, base, "main_feed.sig256", SIG_CAP, &siglen,
                    msg, msgcap);
    if (sig == NULL)
        goto done;
    /* The signature first, exactly as docs_fetch and mmo_feed_load do it: a
     * document nobody vouched for never reaches the parser. */
    if (mmo_rsa_verify_sha256(key, (const u8 *)txt, (size_t)txtlen,
                              (const u8 *)sig, (size_t)siglen) == 0) {
        fail(msg, msgcap, "the server's feed is not signed by a key this "
                          "client trusts", NULL, NULL);
        goto done;
    }
    if (mmo_feed_parse_main(txt, (size_t)txtlen, &main_feed, err,
                            sizeof err) != 0) {
        fail(msg, msgcap, "the server's feed cannot be read: %.400s", err,
             NULL);
        goto done;
    }
    /* A channel with no revision cannot answer the question, and saying so is
     * the only honest outcome: reading it as 0 would announce that every
     * install ever made is newer than what the operator publishes. */
    if (main_feed.revision <= 0) {
        fail(msg, msgcap, "the server's feed names no revision", NULL, NULL);
        goto done;
    }
    if (revision != NULL)
        *revision = main_feed.revision;
    if (msg != NULL && msgcap > 0)
        snprintf(msg, msgcap, "the channel publishes r%d", main_feed.revision);
    rc = 0;
done:
    free(txt);
    free(sig);
    return rc;
}

/* One download's progress, spoken at every whole percent and no oftener:
 * the listener redraws a window per call, and a call per packet would turn
 * a 60 Hz vsync into the download's clock. */
struct dl_tick {
    void (*tick)(void *ud, const char *line);
    void *ud;
    const char *name;
    int at, of, lastpct;
};

static void dl_tick(void *ud, long got, long total)
{
    struct dl_tick *t = ud;
    char line[MMO_FEED_NAME + 96];
    int pct = total > 0 ? (int)(got * 100 / total) : 0;

    if (t->tick == NULL || pct == t->lastpct)
        return;
    t->lastpct = pct;
    snprintf(line, sizeof line, "downloading %d of %d: %s, %d%%",
             t->at, t->of, t->name, pct);
    t->tick(t->ud, line);
}

static int write_whole(const char *path, const void *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    int ok;

    if (f == NULL)
        return -1;
    ok = fwrite(data, 1, n, f) == n;
    return (fclose(f) == 0 && ok) ? 0 : -1;
}

int mmo_update_run(const char *url, const char *feed_dir,
                   const mmo_rsa_pubkey *key, const char *root,
                   const char *self_rel,
                   void (*note)(void *ud, const char *line),
                   void (*tick)(void *ud, const char *line), void *ud,
                   int *self_updated, char *msg, size_t msgcap)
{
    /* ~190 KB of inventory: static, never a stack frame. Single-threaded,
     * like everything the launcher does before a game exists. */
    static mmo_feed_update upd;
    static int want[MMO_FEED_MAX_FILES];
    mmo_feed_main main_feed;
    struct feed_docs docs;
    char host[MMO_FETCH_HOST], port[16], base[MMO_FETCH_PATH];
    char path[MMO_LAUNCH_PATH + MMO_FEED_NAME];
    char stage[MMO_LAUNCH_PATH + MMO_FEED_NAME];
    char forcefile[MMO_LAUNCH_PATH + 16];
    char line[MMO_FEED_NAME + 128], err[MMO_FETCH_ERR], have[65];
    long got;
    int i, w, wants = 0, done, force;

    if (self_updated != NULL)
        *self_updated = 0;
    if (msg != NULL && msgcap > 0)
        msg[0] = '\0';

    /* A launcher swapped by an earlier update leaves its old image beside the
     * new binary; on Windows it could not be deleted while it ran. Now it is
     * nobody's, so it goes, quietly, because there usually is none. */
    if (self_rel != NULL && self_rel[0] != '\0') {
        snprintf(path, sizeof path, "%s/%s.old", root, self_rel);
        remove(path);
    }

    /*
     * `.forceupdate` beside the install, the official updater's own trigger, turns this run
     * into a full repair: the hash shortcut below is skipped, so every file the feed vouches
     * for is fetched and proven again.
     */
    snprintf(forcefile, sizeof forcefile, "%s/.forceupdate", root);
    force = access(forcefile, F_OK) == 0;

    if (mmo_fetch_split(url, host, sizeof host, port, sizeof port,
                        base, sizeof base, err, sizeof err) != 0)
        return fail(msg, msgcap, "%.400s", err, NULL);
    if (docs_fetch(host, port, base, key, &docs, &main_feed, &upd, msg,
                   msgcap) != 0)
        return -1;

    if (force && note != NULL)
        note(ud, ".forceupdate is beside the install, every file will be "
                 "fetched again");

    /* What this install is missing or holding wrong, by the official rules the
     * gate also lives by; a file the feed vouches for and the disk already
     * matches is not downloaded again, unless this is a forced repair. */
    for (i = 0; i < upd.n; i++) {
        const mmo_feed_file *f = &upd.f[i];
        int exists;

        snprintf(path, sizeof path, "%s/%s", root, f->name);
        exists = mmo_feed_hash_file(path, have) == 0;
        if (!mmo_feed_should_download(f, exists, mmo_feed_os(),
                                      mmo_feed_arch()))
            continue;
        if (!force && exists && strcasecmp(have, f->sha256) == 0)
            continue;
        want[wants++] = i;
    }

    if (wants == 0)
        goto install_feed;

    /* Every download proves itself in staging before any file of the install
     * is touched. The staged names are flat, f0, f1, …, so staging never
     * has to mirror the install's tree to then throw it away. */
    snprintf(stage, sizeof stage, "%s/update.staging", root);
    mmo_plat_mkdir(stage);
    for (w = 0; w < wants; w++) {
        const mmo_feed_file *f = &upd.f[want[w]];
        char esc[3 * MMO_FEED_NAME], fileurl[MMO_FETCH_PATH + sizeof esc + 80];
        struct dl_tick t = { tick, ud, f->name, w + 1, wants, -1 };

        if (note != NULL) {
            snprintf(line, sizeof line, "downloading %d of %d: %s (%ld KB)",
                     w + 1, wants, f->name,
                     (f->size + 1023) / 1024);
            note(ud, line);
        }
        url_escape(f->name, esc, sizeof esc);
        snprintf(fileurl, sizeof fileurl, "%s/files/%s?%s", base, esc,
                 f->sha256);
        snprintf(stage, sizeof stage, "%s/update.staging/f%d", root, w);
        got = mmo_fetch_file(host, port, fileurl, stage, f->size,
                             dl_tick, &t, err, sizeof err);
        if (got < 0) {
            docs_free(&docs);
            return fail(msg, msgcap, "%.200s cannot be fetched: %.250s",
                        f->name, err);
        }
        if (got != f->size || mmo_feed_hash_file(stage, have) != 0 ||
            strcasecmp(have, f->sha256) != 0) {
            docs_free(&docs);
            return fail(msg, msgcap, "%.300s downloaded wrong: the bytes are "
                                     "not the ones the feed signed", f->name,
                        NULL);
        }
    }

    /* Only now is the install written. The launcher's own binary steps aside
     * by rename, the running image survives on both hosts, and everything
     * else is replaced outright. */
    for (w = 0; w < wants; w++) {
        const mmo_feed_file *f = &upd.f[want[w]];
        int is_self = self_rel != NULL && self_rel[0] != '\0' &&
                      strcmp(f->name, self_rel) == 0;

        snprintf(stage, sizeof stage, "%s/update.staging/f%d", root, w);
        snprintf(path, sizeof path, "%s/%s", root, f->name);
        mkdirs_for(path);
        if (is_self) {
            char old[sizeof path + 4];

            snprintf(old, sizeof old, "%s.old", path);
            remove(old);
            rename(path, old);
        } else {
            remove(path);
        }
        if (rename(stage, path) != 0) {
            docs_free(&docs);
            return fail(msg, msgcap, "%.300s cannot be replaced, the "
                                     "install may need a repair", f->name,
                        NULL);
        }
#ifndef _WIN32
        if (f->executable)
            chmod(path, 0755);
#endif
        if (is_self && self_updated != NULL)
            *self_updated = 1;
    }
    snprintf(stage, sizeof stage, "%s/update.staging", root);
    rmdir(stage);

install_feed:
    /* The verified documents become the local feed, so the gate that runs
     * next holds the install to exactly what was fetched. Written after the
     * files: a crash mid-install leaves the old feed refusing the mix, which
     * is the refusal a player can act on. */
    done = 0;
    mkdirs_for(feed_dir);
    mmo_plat_mkdir(feed_dir);           /* exists already is fine */
    snprintf(path, sizeof path, "%s/main_feed.txt", feed_dir);
    done |= write_whole(path, docs.main_txt, (size_t)docs.main_len);
    snprintf(path, sizeof path, "%s/main_feed.sig256", feed_dir);
    done |= write_whole(path, docs.main_sig, (size_t)docs.main_siglen);
    snprintf(path, sizeof path, "%s/update_feed.txt", feed_dir);
    done |= write_whole(path, docs.upd_txt, (size_t)docs.upd_len);
    snprintf(path, sizeof path, "%s/update_feed.sig256", feed_dir);
    done |= write_whole(path, docs.upd_sig, (size_t)docs.upd_siglen);
    docs_free(&docs);
    if (done != 0)
        return fail(msg, msgcap, "the feed cannot be written into %.300s",
                    feed_dir, NULL);

    /* The trigger is spent only now, with the repair proven and installed. */
    if (force)
        remove(forcefile);

    if (wants == 0)
        snprintf(msg, msgcap, "the install is current at revision %d",
                 main_feed.revision);
    else
        snprintf(msg, msgcap, "updated %d file%s to revision %d%s", wants,
                 wants == 1 ? "" : "s", main_feed.revision,
                 (self_updated != NULL && *self_updated)
                     ? ", the launcher itself among them, for its next start"
                     : "");
    return 0;
}
