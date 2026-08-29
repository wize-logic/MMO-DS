/* The signed inventory the front door checks an install against. */

#ifndef OPENMMO_FEED_H
#define OPENMMO_FEED_H

#include <stddef.h>

#include "mmo.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- the key, and the signature over a document ------------------------ */

/* 4096-bit is the ceiling; the feeds in hand are 3072-bit. Limbs are 32 bits
 * and little-endian, so the modulus arrives big-endian and is reversed once. */
#define MMO_RSA_MAX_BYTES 512
#define MMO_RSA_MAX_LIMBS (MMO_RSA_MAX_BYTES / 4)

typedef struct {
    u32 n[MMO_RSA_MAX_LIMBS];   /* modulus, little-endian limbs */
    u32 r2[MMO_RSA_MAX_LIMBS];  /* 2^(64*limbs) mod n, for Montgomery entry */
    u32 n0inv;                  /* -n[0]^-1 mod 2^32 */
    int limbs;
    int nbytes;                 /* modulus length in bytes: a signature's length */
    u32 e;                      /* public exponent; 65537 in every key seen */
} mmo_rsa_pubkey;

/*
 * A DER SubjectPublicKeyInfo, i.e. the body of a `-----BEGIN PUBLIC KEY-----` block. Only
 * rsaEncryption is accepted; anything else is refused by name rather than misread as RSA.
 */
int mmo_rsa_pubkey_der(const u8 *der, size_t len, mmo_rsa_pubkey *key,
                       char *err, size_t errcap);

/*
 * The same key as text: a PEM block, or a bare base64 SPKI literal with no armour at all.
 * Whitespace is skipped; any other character outside the base64 alphabet is an error, so a
 * truncated paste fails rather than decoding to a shorter key.
 */
int mmo_rsa_pubkey_text(const char *text, size_t len, mmo_rsa_pubkey *key,
                        char *err, size_t errcap);

/*
 * RSASSA-PKCS1-v1_5 verify with SHA-256, which is what `SHA256withRSA` names. Returns 1 when
 * the signature is the key's over exactly these bytes, 0 otherwise.
 */
int mmo_rsa_verify_sha256(const mmo_rsa_pubkey *key,
                          const void *msg, size_t msglen,
                          const u8 *sig, size_t siglen);

/* SHA-256 of a file, as 64 lowercase hex bytes plus a NUL. Returns 0, or -1 if
 * the file cannot be read (which the caller reports as missing). */
int mmo_feed_hash_file(const char *path, char out[65]);

/* --- the two documents ------------------------------------------------- */

#define MMO_FEED_NAME      256
#define MMO_FEED_HOST      128
#define MMO_FEED_TAG        16
#define MMO_FEED_MAX_FILES 512

/*
 * One `<file>` of the update feed, after the four rules below have accepted it. `name` is the
 * sanitized relative path; `sha256` is kept as written because official compares it case-
 * insensitively and a feed may hold either case.
 */
typedef struct {
    char name[MMO_FEED_NAME];
    char sha256[65];
    char os[MMO_FEED_TAG];      /* empty means "any" */
    char arch[MMO_FEED_TAG];    /* empty means "any" */
    long size;
    int  executable;
    int  only_if_not_exists;
} mmo_feed_file;

/*
 * The parsed update feed. `dropped` counts entries the rules skipped, which is normal, an
 * optional component or a foreign platform, and is reported rather than hidden.
 */
typedef struct {
    mmo_feed_file f[MMO_FEED_MAX_FILES];
    int n;
    int dropped;
    int overflow;
    int min_launcher_version;
} mmo_feed_update;

typedef struct {
    char ip[MMO_FEED_HOST];
    int  port;
    int  revision;
    int  min_revision;
} mmo_feed_main;

/* This launcher's own version, compared against the feed's min_launcher_version
 * exactly as the official launcher compares its own. It goes up when a feed
 * change would break an older front door. */
#define MMO_FEED_LAUNCHER_VERSION 1

/*
 * Parse. Both refuse the document outright rather than skipping what they do not model: a
 * DOCTYPE, an entity or CDATA section, an unterminated tag, or an `&name;` that is not one of
 * the five XML predefines.
 */
int mmo_feed_parse_main(const char *text, size_t len, mmo_feed_main *m,
                        char *err, size_t errcap);
int mmo_feed_parse_update(const char *text, size_t len, mmo_feed_update *u,
                          char *err, size_t errcap);

/*
 * A feed-supplied path, made safe to resolve under an install root, or refused. Backslashes
 * become slashes; an absolute path, a drive letter, a segment ending in `.` or a space, and
 * anything that normalises to the root itself or outside it are all refused.
 */
int mmo_feed_sanitize(const char *entry, char *out, size_t cap);

/*
 * Whether this entry is one this install should be holding, given whether the file is already
 * there.
 */
int mmo_feed_should_download(const mmo_feed_file *f, int exists,
                             const char *os, const char *arch);

/* Whether the feed vouches for this relative path. `only_if_not_exists`
 * entries deliberately do not count: they are never byte-checked, so they
 * cannot carry a promise about the file's contents. */
int mmo_feed_declares(const mmo_feed_update *u, const char *relpath);

/*
 * A repair, a full re-check rather than a plain update, is due when the install cannot say
 * what revision it is, or says one below the feed's floor.
 */
int mmo_feed_needs_repair(int revision, int min_revision);

/* What `revision.txt` in the install root says, or -1 when it is absent, is
 * not a file, or does not hold an integer. */
int mmo_feed_installed_revision(const char *root);

/* --- the verdict a player is shown ------------------------------------- */

enum {
    MMO_FEED_OK = 0,        /* every file the feed vouches for is present and right */
    MMO_FEED_STALE,         /* files are missing or have the wrong contents */
    MMO_FEED_UNTRUSTED,     /* the feed does not vouch for what we would run */
    MMO_FEED_LAUNCHER_OLD,  /* the feed needs a newer front door than this one */
    MMO_FEED_BAD            /* no signature, no parse: the feed itself is unusable */
};

#define MMO_FEED_MESSAGE 512

typedef struct {
    int  verdict;
    int  checked;               /* entries in scope for this install */
    int  missing;
    int  mismatched;
    int  repair;                /* the revision floor says re-check everything */
    char first_bad[MMO_FEED_NAME];
    char message[MMO_FEED_MESSAGE];  /* one line, addressed to a player */
} mmo_feed_report;

/*
 * Read, verify and parse both documents out of `dir`, the four names above, under `key`.
 * Returns 0 with `m` and `u` filled, or -1 with a player-readable line in `r->message` and
 * `r->verdict` set to MMO_FEED_BAD.
 */
int mmo_feed_load(const char *dir, const mmo_rsa_pubkey *key,
                  mmo_feed_main *m, mmo_feed_update *u, mmo_feed_report *r);

/* Hold the install at `root` to the feed, and decide whether the game may start. */
int mmo_feed_check(const char *root, const mmo_feed_main *m,
                   const mmo_feed_update *u, const char *os, const char *arch,
                   const char *launch_target, mmo_feed_report *r);

/* The names this platform goes by in a feed, "linux", "x64" and so on, or
 * "" where this build cannot say, which reads as "any" and so checks more
 * rather than less. */
const char *mmo_feed_os(void);
const char *mmo_feed_arch(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_FEED_H */
