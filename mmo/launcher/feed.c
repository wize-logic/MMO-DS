/* Reading the signed inventory, and holding an install to it. */

#include "feed.h"
#include "platform.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(char *err, size_t cap, const char *msg)
{
    if (err != NULL && cap > 0)
        snprintf(err, cap, "%s", msg);
    return -1;
}

/* ====================================================================== */
/* the scanner                                                             */
/* ====================================================================== */

typedef struct {
    const char *p, *end;
    char        name[64];
    const char *attr;           /* the attribute span of the tag just read */
    size_t      attrlen;
    int         closing;        /* </name> */
    int         self;           /* <name .../> */
} xml;

static int xml_name_char(int c)
{
    return isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.' ||
           c == ':';
}

/*
 * Decode one attribute value or text run into `out`. The five XML predefines and numeric
 * character references are decoded; every other `&name;` is an entity reference, which this
 * scanner has no table for and refuses.
 */
static int xml_text(const char *p, size_t len, char *out, size_t cap)
{
    size_t i, n = 0;

    for (i = 0; i < len; i++) {
        if (p[i] != '&') {
            if (n + 1 >= cap)
                return -1;
            out[n++] = p[i];
            continue;
        }
        {
            const char *semi = memchr(p + i, ';', len - i);
            size_t elen;
            long v = -1;

            if (semi == NULL)
                return -1;
            elen = (size_t)(semi - (p + i)) - 1;
            if (elen == 3 && memcmp(p + i + 1, "amp", 3) == 0) v = '&';
            else if (elen == 2 && memcmp(p + i + 1, "lt", 2) == 0) v = '<';
            else if (elen == 2 && memcmp(p + i + 1, "gt", 2) == 0) v = '>';
            else if (elen == 4 && memcmp(p + i + 1, "quot", 4) == 0) v = '"';
            else if (elen == 4 && memcmp(p + i + 1, "apos", 4) == 0) v = '\'';
            else if (elen >= 2 && p[i + 1] == '#') {
                char num[16];
                size_t nl = elen - 1;

                if (nl >= sizeof num)
                    return -1;
                memcpy(num, p + i + 2, nl);
                num[nl] = '\0';
                v = (num[0] == 'x' || num[0] == 'X')
                        ? strtol(num + 1, NULL, 16) : strtol(num, NULL, 10);
                if (v <= 0 || v > 0x10ffff)
                    return -1;
                if (v > 0x7f)           /* no UTF-8 encoder here, and no feed
                                         * seen needs one; refuse rather than
                                         * emit something approximate */
                    return -1;
            }
            if (v < 0)
                return -1;
            if (n + 1 >= cap)
                return -1;
            out[n++] = (char)v;
            i += elen + 1;
        }
    }
    out[n] = '\0';
    return (int)n;
}

/*
 * Advance to the next element. Returns 1 with the scanner's fields filled, 0 at
 * the end of the document, -1 on anything refused.
 */
static int xml_next(xml *x, char *err, size_t errcap)
{
    while (x->p < x->end) {
        const char *lt = memchr(x->p, '<', (size_t)(x->end - x->p));
        const char *gt;
        size_t n = 0;

        if (lt == NULL) {
            x->p = x->end;
            return 0;
        }
        x->p = lt + 1;
        if (x->p >= x->end)
            return fail(err, errcap, "the document ends inside a tag");

        if (*x->p == '?') {         /* <?xml ...?> */
            gt = memchr(x->p, '>', (size_t)(x->end - x->p));
            if (gt == NULL)
                return fail(err, errcap, "unterminated processing instruction");
            x->p = gt + 1;
            continue;
        }
        if (*x->p == '!') {
            if ((size_t)(x->end - x->p) >= 3 && memcmp(x->p, "!--", 3) == 0) {
                const char *e = x->p + 3;

                while (e + 3 <= x->end && memcmp(e, "-->", 3) != 0)
                    e++;
                if (e + 3 > x->end)
                    return fail(err, errcap, "unterminated comment");
                x->p = e + 3;
                continue;
            }
            /* DOCTYPE, entity, CDATA: not modelled, so not accepted. */
            return fail(err, errcap,
                        "the document declares a doctype, entity or CDATA section");
        }
        x->closing = 0;
        if (*x->p == '/') {
            x->closing = 1;
            x->p++;
        }
        while (x->p < x->end && xml_name_char(*x->p)) {
            if (n + 1 < sizeof x->name)
                x->name[n++] = *x->p;
            x->p++;
        }
        x->name[n] = '\0';
        if (n == 0)
            return fail(err, errcap, "a tag has no name");

        /* The attribute span runs to the '>' that is not inside a quoted
         * value, so an attribute holding '>' does not end the tag early. */
        x->attr = x->p;
        {
            int quote = 0;

            while (x->p < x->end) {
                char c = *x->p;

                if (quote != 0) {
                    if (c == quote)
                        quote = 0;
                } else if (c == '"' || c == '\'') {
                    quote = c;
                } else if (c == '>') {
                    break;
                }
                x->p++;
            }
            if (x->p >= x->end)
                return fail(err, errcap, "the document ends inside a tag");
        }
        x->attrlen = (size_t)(x->p - x->attr);
        x->self = x->attrlen > 0 && x->attr[x->attrlen - 1] == '/';
        if (x->self != 0)
            x->attrlen--;
        x->p++;                 /* past '>' */
        return 1;
    }
    return 0;
}

/* One attribute of the tag just read. Returns 1 when present (`out` holds its
 * decoded value), 0 when absent, -1 when its value cannot be decoded. */
static int xml_attr(const xml *x, const char *want, char *out, size_t cap)
{
    const char *p = x->attr, *end = x->attr + x->attrlen;

    while (p < end) {
        const char *nstart;
        size_t nlen;
        char quote;

        while (p < end && isspace((unsigned char)*p))
            p++;
        nstart = p;
        while (p < end && xml_name_char(*p))
            p++;
        nlen = (size_t)(p - nstart);
        if (nlen == 0)
            return 0;
        while (p < end && isspace((unsigned char)*p))
            p++;
        if (p >= end || *p != '=')
            return 0;
        p++;
        while (p < end && isspace((unsigned char)*p))
            p++;
        if (p >= end || (*p != '"' && *p != '\''))
            return 0;
        quote = *p++;
        {
            const char *vstart = p;

            while (p < end && *p != quote)
                p++;
            if (p >= end)
                return 0;
            if (nlen == strlen(want) && memcmp(nstart, want, nlen) == 0)
                return xml_text(vstart, (size_t)(p - vstart), out, cap) < 0
                           ? -1 : 1;
            p++;
        }
    }
    return 0;
}

/* The text content of the element just read, up to its closing tag. */
static int xml_content(xml *x, char *out, size_t cap)
{
    const char *lt;

    if (x->self != 0) {
        out[0] = '\0';
        return 0;
    }
    lt = memchr(x->p, '<', (size_t)(x->end - x->p));
    if (lt == NULL)
        return -1;
    return xml_text(x->p, (size_t)(lt - x->p), out, cap) < 0 ? -1 : 0;
}

static void trim(char *s)
{
    size_t n = strlen(s), i = 0;

    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
    while (s[i] != '\0' && isspace((unsigned char)s[i]))
        i++;
    if (i > 0)
        memmove(s, s + i, n - i + 1);
}

/* ====================================================================== */
/* paths                                                                   */
/* ====================================================================== */

int mmo_feed_sanitize(const char *entry, char *out, size_t cap)
{
    char buf[MMO_FEED_NAME];
    char *seg[MMO_FEED_NAME / 2];
    size_t n, i, nseg = 0, len = 0;

    if (entry == NULL || entry[0] == '\0')
        return -1;
    n = strlen(entry);
    if (n >= sizeof buf)
        return -1;
    memcpy(buf, entry, n + 1);
    for (i = 0; i < n; i++) {
        if (buf[i] == '\\')
            buf[i] = '/';
    }
    if (buf[0] == '/')
        return -1;
    if (n > 1 && buf[1] == ':')     /* a Windows drive letter, on any host */
        return -1;

    /* A segment ending in '.' or a space is refused before anything is
     * resolved, which is also what turns `..` away: it ends in a dot. */
    {
        char *save = NULL, *tok = strtok_r(buf, "/", &save);

        while (tok != NULL) {
            size_t tl = strlen(tok);

            if (tl > 0 && (tok[tl - 1] == '.' || tok[tl - 1] == ' '))
                return -1;
            if (nseg >= sizeof seg / sizeof seg[0])
                return -1;
            seg[nseg++] = tok;
            tok = strtok_r(NULL, "/", &save);
        }
    }
    if (nseg == 0)                  /* resolves to the root itself */
        return -1;
    for (i = 0; i < nseg; i++) {
        size_t tl = strlen(seg[i]);

        if (len + tl + (i > 0 ? 1u : 0u) + 1 > cap)
            return -1;
        if (i > 0)
            out[len++] = '/';
        memcpy(out + len, seg[i], tl);
        len += tl;
    }
    out[len] = '\0';
    return 0;
}

/* ====================================================================== */
/* the two documents                                                       */
/* ====================================================================== */

int mmo_feed_parse_main(const char *text, size_t len, mmo_feed_main *m,
                        char *err, size_t errcap)
{
    xml x;
    char val[MMO_FEED_HOST];
    int rc, root = 0;

    memset(m, 0, sizeof *m);
    m->revision = -1;
    m->port = -1;
    x.p = text;
    x.end = text + len;
    while ((rc = xml_next(&x, err, errcap)) == 1) {
        if (x.closing != 0)
            continue;
        if (root == 0) {
            if (strcmp(x.name, "main_feed") != 0)
                return fail(err, errcap, "the main feed's root is not main_feed");
            root = 1;
            continue;
        }
        if (strcmp(x.name, "ip") == 0 && m->ip[0] == '\0') {
            if (xml_content(&x, val, sizeof val) == 0) {
                trim(val);
                snprintf(m->ip, sizeof m->ip, "%s", val);
            }
        } else if (strcmp(x.name, "port") == 0 && m->port < 0) {
            if (xml_content(&x, val, sizeof val) == 0) {
                trim(val);
                m->port = (int)strtol(val, NULL, 10);
            }
        } else if (strcmp(x.name, "revision") == 0 && m->revision < 0) {
            if (xml_content(&x, val, sizeof val) == 0) {
                trim(val);
                m->revision = (int)strtol(val, NULL, 10);
            }
        } else if (strcmp(x.name, "min_revision") == 0 && m->min_revision == 0) {
            if (xml_content(&x, val, sizeof val) == 0) {
                trim(val);
                m->min_revision = (int)strtol(val, NULL, 10);
            }
        }
    }
    if (rc < 0)
        return -1;
    if (root == 0)
        return fail(err, errcap, "the main feed has no main_feed element");
    return 0;
}

int mmo_feed_parse_update(const char *text, size_t len, mmo_feed_update *u,
                          char *err, size_t errcap)
{
    xml x;
    char val[MMO_FEED_NAME];
    int rc, root = 0;

    memset(u, 0, sizeof *u);
    x.p = text;
    x.end = text + len;
    while ((rc = xml_next(&x, err, errcap)) == 1) {
        mmo_feed_file f;

        if (x.closing != 0)
            continue;
        if (root == 0) {
            if (strcmp(x.name, "update_feed") != 0)
                return fail(err, errcap,
                            "the update feed's root is not update_feed");
            root = 1;
            /* The official updater reads min_launcher_version, and falls back to
             * min_osx_installer_version only when the first is absent or below
             * 1. Both are carried here for the same reason: an operator who
             * raises the floor on one platform's installer raised it. */
            if (xml_attr(&x, "min_launcher_version", val, sizeof val) == 1)
                u->min_launcher_version = (int)strtol(val, NULL, 10);
            if (u->min_launcher_version < 1 &&
                xml_attr(&x, "min_osx_installer_version", val, sizeof val) == 1)
                u->min_launcher_version = (int)strtol(val, NULL, 10);
            continue;
        }
        if (strcmp(x.name, "file") != 0)
            continue;

        memset(&f, 0, sizeof f);
        /* An entry naming an optional component is not part of the install and
         * is skipped whole, before anything else about it is read. */
        if (xml_attr(&x, "option_name", val, sizeof val) == 1 && val[0] != '\0') {
            u->dropped++;
            continue;
        }
        if (xml_attr(&x, "name", val, sizeof val) != 1 ||
            mmo_feed_sanitize(val, f.name, sizeof f.name) != 0) {
            u->dropped++;
            continue;
        }
        if (xml_attr(&x, "sha256", val, sizeof val) != 1 || val[0] == '\0' ||
            strlen(val) != 64 || strspn(val, "0123456789abcdefABCDEF") != 64) {
            /* Stricter than official, which only checks for a non-empty string:
             * a hash that is not a hash can never match a file, so accepting
             * one only defers the failure to somewhere less clear. */
            u->dropped++;
            continue;
        }
        memcpy(f.sha256, val, 64);
        f.sha256[64] = '\0';
        if (xml_attr(&x, "size", val, sizeof val) != 1) {
            u->dropped++;
            continue;
        }
        {
            char *endp = NULL;

            f.size = strtol(val, &endp, 10);
            if (endp == val || *endp != '\0' || f.size <= 0) {
                u->dropped++;
                continue;
            }
        }
        /* A platform tag longer than any platform name drops the entry rather
         * than being cut down to one it does not mean. */
        if (xml_attr(&x, "os", val, sizeof val) == 1) {
            if (strlen(val) >= sizeof f.os) {
                u->dropped++;
                continue;
            }
            strcpy(f.os, val);
        }
        if (xml_attr(&x, "arch", val, sizeof val) == 1) {
            if (strlen(val) >= sizeof f.arch) {
                u->dropped++;
                continue;
            }
            strcpy(f.arch, val);
        }
        if (xml_attr(&x, "executable", val, sizeof val) == 1)
            f.executable = strcmp(val, "true") == 0;
        if (xml_attr(&x, "only_if_not_exists", val, sizeof val) == 1)
            f.only_if_not_exists = strcmp(val, "true") == 0;

        if (u->n >= MMO_FEED_MAX_FILES) {
            u->overflow++;
            continue;
        }
        u->f[u->n++] = f;
    }
    if (rc < 0)
        return -1;
    if (root == 0)
        return fail(err, errcap, "the update feed has no update_feed element");
    if (u->overflow > 0)
        return fail(err, errcap,
                    "the update feed lists more files than this launcher can hold");
    if (u->n == 0)
        return fail(err, errcap, "the update feed lists no usable files");
    return 0;
}

/* ====================================================================== */
/* what the feed says about this install                                   */
/* ====================================================================== */

int mmo_feed_should_download(const mmo_feed_file *f, int exists,
                             const char *os, const char *arch)
{
    if (f->only_if_not_exists != 0 && exists != 0)
        return 0;
    if (exists != 0)
        return 1;
    if (f->os[0] != '\0' && os != NULL && os[0] != '\0' &&
        strcmp(f->os, os) != 0)
        return 0;
    if (f->arch[0] != '\0' && arch != NULL && arch[0] != '\0' &&
        strcmp(f->arch, arch) != 0)
        return 0;
    return 1;
}

int mmo_feed_declares(const mmo_feed_update *u, const char *relpath)
{
    char want[MMO_FEED_NAME];
    size_t i, n;
    int j;

    if (relpath == NULL)
        return 0;
    n = strlen(relpath);
    if (n >= sizeof want)
        return 0;
    memcpy(want, relpath, n + 1);
    for (i = 0; i < n; i++) {
        if (want[i] == '\\')
            want[i] = '/';
    }
    for (j = 0; j < u->n; j++) {
        if (u->f[j].only_if_not_exists == 0 &&
            strcmp(u->f[j].name, want) == 0)
            return 1;
    }
    return 0;
}

int mmo_feed_needs_repair(int revision, int min_revision)
{
    if (revision <= 0)
        return 1;
    return min_revision > 0 && revision < min_revision;
}

int mmo_feed_installed_revision(const char *root)
{
    char path[MMO_FEED_NAME * 2], line[64];
    FILE *f;
    long v;
    char *endp = NULL;

    snprintf(path, sizeof path, "%s/revision.txt", root);
    f = fopen(path, "rb");
    if (f == NULL)
        return -1;
    if (fgets(line, sizeof line, f) == NULL) {
        fclose(f);
        return -1;
    }
    fclose(f);
    trim(line);
    v = strtol(line, &endp, 10);
    if (endp == line || *endp != '\0' || v < 0 || v > 0x7fffffff)
        return -1;
    return (int)v;
}

/* One spelling of the host's name, shared with the login stream: platform.h
 * decides it and this is the feed's word for the same thing. */
const char *mmo_feed_os(void)
{
    return mmo_plat_os_name();
}

const char *mmo_feed_arch(void)
{
#if defined(__x86_64__) || defined(_M_X64)
    return "x64";
#elif defined(__aarch64__)
    return "arm64";
#elif defined(__arm__)
    /*
     * 32-bit ARM, which is what both handhelds run and not a fallback for "arm64 did not
     * match": an armhf binary and an aarch64 one share no file, so a build that answered
     * "arm64" here would fetch the half of a release it cannot execute.
     */
    return "arm";
#elif defined(__i386__)
    /* The game is 32-bit and the front door may be built the same way for the
     * suite. A feed has no name for i386, and guessing "x64" would check the
     * wrong files, so this says nothing and every entry stays in scope. */
    return "";
#else
    return "";
#endif
}

/* ====================================================================== */
/* loading, and the verdict                                                */
/* ====================================================================== */

static char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;

    if (f == NULL)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0 || n > (1 << 24)) {
        fclose(f);
        return NULL;
    }
    buf = malloc((size_t)n + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    buf[n] = '\0';
    *len = (size_t)n;
    return buf;
}

static int bad(mmo_feed_report *r, const char *fmt, const char *a)
{
    r->verdict = MMO_FEED_BAD;
    snprintf(r->message, sizeof r->message, fmt, a);
    return -1;
}

/* One document and its detached signature, verified before it is parsed. */
static int load_signed(const char *dir, const char *stem,
                       const mmo_rsa_pubkey *key, char **text, size_t *len,
                       mmo_feed_report *r)
{
    char path[MMO_FEED_NAME * 2];
    char *doc, *sig;
    size_t doclen, siglen;
    int ok;

    snprintf(path, sizeof path, "%s/%s.txt", dir, stem);
    doc = slurp(path, &doclen);
    if (doc == NULL)
        return bad(r, "the update feed is not readable at %.400s", path);
    snprintf(path, sizeof path, "%s/%s.sig256", dir, stem);
    sig = slurp(path, &siglen);
    if (sig == NULL) {
        free(doc);
        return bad(r, "the update feed is not signed: %.400s is missing", path);
    }
    ok = mmo_rsa_verify_sha256(key, doc, doclen, (const u8 *)sig, siglen);
    free(sig);
    if (ok == 0) {
        free(doc);
        return bad(r, "the %.64s feed is not signed by a key this launcher trusts",
                   stem);
    }
    *text = doc;
    *len = doclen;
    return 0;
}

int mmo_feed_load(const char *dir, const mmo_rsa_pubkey *key,
                  mmo_feed_main *m, mmo_feed_update *u, mmo_feed_report *r)
{
    char *main_txt = NULL, *upd_txt = NULL, err[192];
    size_t main_len = 0, upd_len = 0;
    int rc = 0;

    memset(r, 0, sizeof *r);
    /* Both signatures first, then either parse: a document that has not been
     * verified must never reach a reader. */
    if (load_signed(dir, "main_feed", key, &main_txt, &main_len, r) != 0)
        return -1;
    if (load_signed(dir, "update_feed", key, &upd_txt, &upd_len, r) != 0) {
        free(main_txt);
        return -1;
    }
    if (mmo_feed_parse_main(main_txt, main_len, m, err, sizeof err) != 0)
        rc = bad(r, "the main feed cannot be read: %.400s", err);
    else if (mmo_feed_parse_update(upd_txt, upd_len, u, err, sizeof err) != 0)
        rc = bad(r, "the update feed cannot be read: %.400s", err);
    free(main_txt);
    free(upd_txt);
    return rc;
}

int mmo_feed_check(const char *root, const mmo_feed_main *m,
                   const mmo_feed_update *u, const char *os, const char *arch,
                   const char *launch_target, mmo_feed_report *r)
{
    char path[MMO_FEED_NAME * 2], have[65];
    int i, revision;

    memset(r, 0, sizeof *r);
    if (u->min_launcher_version > MMO_FEED_LAUNCHER_VERSION) {
        r->verdict = MMO_FEED_LAUNCHER_OLD;
        snprintf(r->message, sizeof r->message,
                 "this launcher is version %d and the server now needs %d "
                 "-- install a newer OpenMMO before playing",
                 MMO_FEED_LAUNCHER_VERSION, u->min_launcher_version);
        return r->verdict;
    }
    for (i = 0; i < u->n; i++) {
        const mmo_feed_file *f = &u->f[i];
        int exists;

        snprintf(path, sizeof path, "%s/%s", root, f->name);
        exists = mmo_feed_hash_file(path, have) == 0;
        if (mmo_feed_should_download(f, exists, os, arch) == 0)
            continue;
        r->checked++;
        if (exists == 0) {
            r->missing++;
        } else if (strcasecmp(have, f->sha256) != 0) {
            r->mismatched++;
        } else {
            continue;
        }
        if (r->first_bad[0] == '\0')
            snprintf(r->first_bad, sizeof r->first_bad, "%s", f->name);
    }
    revision = mmo_feed_installed_revision(root);
    r->repair = mmo_feed_needs_repair(revision, m != NULL ? m->min_revision : 0);

    if (r->missing > 0 || r->mismatched > 0) {
        r->verdict = MMO_FEED_STALE;
        snprintf(r->message, sizeof r->message,
                 "this copy of the game is %s: %d of %d files are missing or "
                 "changed, starting with %s, update before playing",
                 r->repair != 0 ? "out of date" : "damaged",
                 r->missing + r->mismatched, r->checked, r->first_bad);
        return r->verdict;
    }
    /* Only a launch target that is actually there is held to the feed. One that
     * is absent is a broken install, which the loop above has already said. */
    if (launch_target != NULL && launch_target[0] != '\0') {
        snprintf(path, sizeof path, "%s/%s", root, launch_target);
        if (mmo_feed_hash_file(path, have) == 0 &&
            mmo_feed_declares(u, launch_target) == 0) {
            r->verdict = MMO_FEED_UNTRUSTED;
            snprintf(r->first_bad, sizeof r->first_bad, "%s", launch_target);
            snprintf(r->message, sizeof r->message,
                     "the update feed does not vouch for %s, refusing to "
                     "start it", launch_target);
            return r->verdict;
        }
    }
    r->verdict = MMO_FEED_OK;
    snprintf(r->message, sizeof r->message,
             "the install matches the feed (%d files checked)", r->checked);
    return r->verdict;
}
