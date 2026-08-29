/* The signed inventory, against two documents somebody else signed. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "feed.h"

#ifndef MMO_REPO_ROOT
#define MMO_REPO_ROOT "."
#endif

#define FEEDS MMO_REPO_ROOT "/launcher/src/test/resources/fixtures/feeds/"
#define KEYS  MMO_REPO_ROOT \
    "/launcher/src/main/kotlin/de/fiereu/openmmo/launcher/client/FeedKeys.kt"

static int failures;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) {                                                             \
            printf("  ok   %s\n", msg);                                         \
        } else {                                                                \
            printf("  FAIL %s\n", msg);                                         \
            failures++;                                                         \
        }                                                                       \
    } while (0)

static char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;

    if (f == NULL)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = malloc((size_t)n + 1);
    if (buf == NULL || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    buf[n] = '\0';
    if (len != NULL)
        *len = (size_t)n;
    return buf;
}

/*
 * The two feed public keys, read out of the Kotlin module that already holds them rather than
 * copied into a fixture of our own.
 */
static int shipped_key(int which, mmo_rsa_pubkey *key, char *err, size_t errcap)
{
    char *text = slurp(KEYS, NULL);
    const char *p;
    int i, rc = -1;

    if (text == NULL) {
        snprintf(err, errcap, "%s is not readable", KEYS);
        return -1;
    }
    p = text;
    for (i = 0; i <= which; i++) {
        p = strstr(p, "\"MII");
        if (p == NULL) {
            snprintf(err, errcap, "no key %d in %s", which, KEYS);
            free(text);
            return -1;
        }
        p++;
    }
    {
        const char *e = strchr(p, '"');

        if (e != NULL)
            rc = mmo_rsa_pubkey_text(p, (size_t)(e - p), key, err, errcap);
        else
            snprintf(err, errcap, "unterminated key literal");
    }
    free(text);
    return rc;
}

/* --- the signature ----------------------------------------------------- */

static void test_verify(void)
{
    mmo_rsa_pubkey live, other;
    char err[192] = "";
    char *doc, *sig;
    size_t doclen, siglen;

    if (shipped_key(0, &live, err, sizeof err) != 0) {
        printf("  FAIL the live feed key parses: %s\n", err);
        failures++;
        return;
    }
    CHECK(live.nbytes == 384, "the live feed key is RSA-3072");
    CHECK(live.e == 65537, "its public exponent is 65537");
    CHECK(shipped_key(1, &other, err, sizeof err) == 0,
          "the second key the client trusts parses too");

    doc = slurp(FEEDS "main_feed.txt", &doclen);
    sig = slurp(FEEDS "main_feed.sig256", &siglen);
    if (doc == NULL || sig == NULL) {
        printf("  FAIL the committed main feed and its signature are readable\n");
        failures++;
        free(doc);
        free(sig);
        return;
    }
    CHECK(siglen == 384, "the detached signature is one RSA-3072 block");
    CHECK(mmo_rsa_verify_sha256(&live, doc, doclen, (const u8 *)sig, siglen) == 1,
          "the operator's main feed verifies under the operator's key");
    CHECK(mmo_rsa_verify_sha256(&other, doc, doclen, (const u8 *)sig, siglen) == 0,
          "it does not verify under the other key the client trusts");

    doc[doclen / 2] = (char)(doc[doclen / 2] ^ 0x01);
    CHECK(mmo_rsa_verify_sha256(&live, doc, doclen, (const u8 *)sig, siglen) == 0,
          "one flipped bit in the document is refused");
    doc[doclen / 2] = (char)(doc[doclen / 2] ^ 0x01);
    sig[0] = (char)(sig[0] ^ 0x80);
    CHECK(mmo_rsa_verify_sha256(&live, doc, doclen, (const u8 *)sig, siglen) == 0,
          "one flipped bit in the signature is refused");
    sig[0] = (char)(sig[0] ^ 0x80);
    CHECK(mmo_rsa_verify_sha256(&live, doc, doclen, (const u8 *)sig, siglen - 1) == 0,
          "a signature that is not the modulus's width is refused");
    CHECK(mmo_rsa_verify_sha256(&live, doc, doclen - 1, (const u8 *)sig, siglen) == 0,
          "a truncated document is refused");
    free(doc);
    free(sig);

    doc = slurp(FEEDS "update_feed.txt", &doclen);
    sig = slurp(FEEDS "update_feed.sig256", &siglen);
    if (doc != NULL && sig != NULL) {
        CHECK(mmo_rsa_verify_sha256(&live, doc, doclen, (const u8 *)sig, siglen) == 1,
              "the operator's update feed verifies too, under the same key");
    } else {
        printf("  FAIL the committed update feed and its signature are readable\n");
        failures++;
    }
    free(doc);
    free(sig);

    CHECK(mmo_rsa_pubkey_text("not base64 at all !!", 20, &other,
                              err, sizeof err) != 0,
          "a key that is not base64 is refused rather than truncated");
    CHECK(mmo_rsa_pubkey_text("MIIBIjANBgkq", 12, &other, err, sizeof err) != 0,
          "a key whose DER is cut short is refused");
}

/* --- the documents ------------------------------------------------------ */

static mmo_feed_update upd;

static void test_documents(void)
{
    mmo_feed_main m;
    char err[192] = "";
    char *doc;
    size_t doclen;
    const mmo_feed_file *rev = NULL, *linux64 = NULL;
    int i;

    doc = slurp(FEEDS "main_feed.txt", &doclen);
    if (doc == NULL) {
        printf("  FAIL the committed main feed is readable\n");
        failures++;
        return;
    }
    CHECK(mmo_feed_parse_main(doc, doclen, &m, err, sizeof err) == 0,
          "the operator's main feed parses");
    CHECK(strcmp(m.ip, "loginserver.pokemmo.com") == 0,
          "it names the login host, and not the china host beside it");
    CHECK(m.port == 2106, "and the login port");
    CHECK(m.revision == 32763 && m.min_revision == 32763,
          "revision and min_revision are read, and min_ipa_revision is not "
          "mistaken for either");
    free(doc);

    doc = slurp(FEEDS "update_feed.txt", &doclen);
    if (doc == NULL) {
        printf("  FAIL the committed update feed is readable\n");
        failures++;
        return;
    }
    CHECK(mmo_feed_parse_update(doc, doclen, &upd, err, sizeof err) == 0,
          "the operator's update feed parses");
    /* 174 is what `grep -c '<file '` counts in the fixture: every entry the
     * operator published is one this reader kept. */
    CHECK(upd.n == 174, "all 174 file entries are kept");
    CHECK(upd.dropped == 0 && upd.overflow == 0,
          "none was dropped and none overflowed");
    /* The feed carries min_osx_installer_version and no min_launcher_version,
     * so the official updater's fallback is the one that applies. */
    CHECK(upd.min_launcher_version == 60,
          "the installer-version floor is read when the launcher one is absent");

    for (i = 0; i < upd.n; i++) {
        if (strcmp(upd.f[i].name, "revision.txt") == 0)
            rev = &upd.f[i];
        if (strcmp(upd.f[i].name, "bin/linux/x64/PokeMMO") == 0)
            linux64 = &upd.f[i];
    }
    CHECK(rev != NULL && rev->size == 5 &&
          strcmp(rev->sha256,
                 "5e4010e80efaa777fa71a6697d60984a59bb055688d67b205ef7534e1e27e78c")
              == 0,
          "a plain entry keeps its hash and its size");
    CHECK(linux64 != NULL && strcmp(linux64->os, "linux") == 0 &&
          strcmp(linux64->arch, "x64") == 0 && linux64->executable == 1 &&
          linux64->only_if_not_exists == 0,
          "a platform entry keeps its os, its arch and its execute bit");
    CHECK(mmo_feed_declares(&upd, "bin/linux/x64/PokeMMO") == 1,
          "the feed vouches for the binary it lists");
    CHECK(mmo_feed_declares(&upd, "bin\\linux\\x64\\PokeMMO") == 1,
          "and for the same path written the other way round");
    CHECK(mmo_feed_declares(&upd, "bin/linux/x64/PokeMMO-patched") == 0,
          "and not for one it does not");
    free(doc);
}

/* --- what the scanner refuses ------------------------------------------- */

static int refuses(const char *xml)
{
    static mmo_feed_update u;
    char err[192] = "";

    return mmo_feed_parse_update(xml, strlen(xml), &u, err, sizeof err) != 0;
}

static void test_refusals(void)
{
    static mmo_feed_update u;
    char err[192] = "";
    const char *one =
        "<update_feed><file name=\"a.txt\" "
        "sha256=\"0000000000000000000000000000000000000000000000000000000000000000\" "
        "size=\"1\"/></update_feed>";

    CHECK(refuses("<!DOCTYPE update_feed SYSTEM \"x.dtd\"><update_feed/>"),
          "a doctype declaration is refused, not ignored");
    CHECK(refuses("<update_feed><![CDATA[<file/>]]></update_feed>"),
          "a CDATA section is refused");
    CHECK(refuses("<update_feed><file name=\"&evil;\" sha256=\"x\" size=\"1\"/>"
                  "</update_feed>"),
          "an entity reference the scanner has no table for is refused");
    CHECK(refuses("<update_feed><file name=\"a.txt\" size=\"1\""),
          "a document that ends inside a tag is refused");
    CHECK(refuses("<main_feed/>"),
          "a document whose root is the wrong element is refused");
    CHECK(refuses("<update_feed></update_feed>"),
          "a feed that lists no usable file is refused rather than trusted");

    CHECK(mmo_feed_parse_update(one, strlen(one), &u, err, sizeof err) == 0 &&
          u.n == 1,
          "the same shape with one good entry is accepted");

    /* The four rules the official updater drops an entry on, each on its own.
     * `option_name` is the one no live feed in hand exercises: the entry below
     * is laid out to that updater's own reader, which skips an entry carrying
     * one before it reads anything else about it. */
    {
        const char *cases[] = {
            "<update_feed><file option_name=\"extra_maps\" name=\"a.txt\" "
            "sha256=\"0000000000000000000000000000000000000000000000000000000000000000\" "
            "size=\"1\"/></update_feed>",
            "<update_feed><file name=\"../escape\" "
            "sha256=\"0000000000000000000000000000000000000000000000000000000000000000\" "
            "size=\"1\"/></update_feed>",
            "<update_feed><file name=\"a.txt\" sha256=\"\" size=\"1\"/></update_feed>",
            "<update_feed><file name=\"a.txt\" "
            "sha256=\"0000000000000000000000000000000000000000000000000000000000000000\" "
            "size=\"0\"/></update_feed>",
            "<update_feed><file name=\"a.txt\" "
            "sha256=\"0000000000000000000000000000000000000000000000000000000000000000\" "
            "size=\"twelve\"/></update_feed>",
        };
        const char *why[] = {
            "an entry naming an optional component is skipped",
            "an entry whose name escapes the install root is skipped",
            "an entry with no hash is skipped",
            "an entry with a non-positive size is skipped",
            "an entry whose size is not a number is skipped",
        };
        size_t c;

        for (c = 0; c < sizeof cases / sizeof cases[0]; c++) {
            int rc = mmo_feed_parse_update(cases[c], strlen(cases[c]), &u,
                                           err, sizeof err);

            /* Dropping every entry leaves an empty feed, which is itself a
             * refusal, so the check is that it dropped one and kept none. */
            CHECK(rc != 0 && u.dropped == 1 && u.n == 0, why[c]);
        }
    }
}

static void test_sanitize(void)
{
    char out[MMO_FEED_NAME];

    CHECK(mmo_feed_sanitize("data/maps/1.pm3d", out, sizeof out) == 0 &&
          strcmp(out, "data/maps/1.pm3d") == 0, "a plain relative path passes");
    CHECK(mmo_feed_sanitize("bin\\linux\\x64\\PokeMMO", out, sizeof out) == 0 &&
          strcmp(out, "bin/linux/x64/PokeMMO") == 0,
          "backslashes become slashes");
    CHECK(mmo_feed_sanitize("a//b", out, sizeof out) == 0 &&
          strcmp(out, "a/b") == 0, "an empty segment collapses");
    CHECK(mmo_feed_sanitize("/etc/passwd", out, sizeof out) != 0,
          "an absolute path is refused");
    CHECK(mmo_feed_sanitize("C:/windows/system32", out, sizeof out) != 0,
          "a drive letter is refused on any host");
    CHECK(mmo_feed_sanitize("../../etc/passwd", out, sizeof out) != 0,
          "a path that climbs out of the root is refused");
    CHECK(mmo_feed_sanitize("a/../b", out, sizeof out) != 0,
          "and so is one that climbs and comes back");
    CHECK(mmo_feed_sanitize("trailing./x", out, sizeof out) != 0,
          "a segment ending in a dot is refused");
    CHECK(mmo_feed_sanitize("trailing /x", out, sizeof out) != 0,
          "a segment ending in a space is refused");
    CHECK(mmo_feed_sanitize("", out, sizeof out) != 0, "an empty name is refused");
    CHECK(mmo_feed_sanitize(".", out, sizeof out) != 0,
          "and one that resolves to the root itself");
}

static void test_should_download(void)
{
    mmo_feed_file f;

    memset(&f, 0, sizeof f);
    CHECK(mmo_feed_should_download(&f, 0, "linux", "x64") == 1,
          "an absent file with no platform is wanted");
    CHECK(mmo_feed_should_download(&f, 1, "linux", "x64") == 1,
          "and a present one is still checked");
    f.only_if_not_exists = 1;
    CHECK(mmo_feed_should_download(&f, 1, "linux", "x64") == 0,
          "an only_if_not_exists file that is already there is left alone");
    CHECK(mmo_feed_should_download(&f, 0, "linux", "x64") == 1,
          "and fetched when it is not");
    memset(&f, 0, sizeof f);
    snprintf(f.os, sizeof f.os, "windows");
    CHECK(mmo_feed_should_download(&f, 0, "linux", "x64") == 0,
          "an absent file for another os is not wanted");
    CHECK(mmo_feed_should_download(&f, 1, "linux", "x64") == 1,
          "but one left on disk by another os is still checked, the official "
          "updater's own ordering");
    snprintf(f.os, sizeof f.os, "linux");
    snprintf(f.arch, sizeof f.arch, "arm64");
    CHECK(mmo_feed_should_download(&f, 0, "linux", "x64") == 0,
          "an absent file for another arch is not wanted");
    CHECK(mmo_feed_should_download(&f, 0, "linux", "") == 1,
          "and a host that cannot name its arch checks more, not less");
}

/*
 * The other half of the same rule: what this build calls its own arch. The filter above is
 * only as good as the word handed to it, and there is exactly one right answer per target,
 * so this is an assertion and not a survey.
 */
static void test_this_arch(void)
{
    const char *a = mmo_feed_arch();

#if defined(__x86_64__) || defined(_M_X64)
    CHECK(strcmp(a, "x64") == 0, "this build names its own arch: x64");
#elif defined(__aarch64__)
    CHECK(strcmp(a, "arm64") == 0, "this build names its own arch: arm64");
#elif defined(__arm__)
    CHECK(strcmp(a, "arm") == 0,
          "this build names its own arch: arm (32-bit, not arm64)");
#elif defined(__i386__)
    CHECK(a[0] == '\0',
          "i386 has no name in a feed, so it claims none and checks more");
#else
    CHECK(a[0] == '\0', "an arch with no feed name claims none");
#endif
}

static void test_repair(void)
{
    /* The official updater's own line is
     *     revision <= 0 ? true : minRevision > 0 && revision >= minRevision
     * which repairs at or above the floor. This is the corrected sense, and
     * the middle two checks are exactly where the two disagree. */
    CHECK(mmo_feed_needs_repair(-1, 32763) == 1,
          "an install that cannot say its revision is repaired");
    CHECK(mmo_feed_needs_repair(32700, 32763) == 1,
          "an install below the floor is repaired");
    CHECK(mmo_feed_needs_repair(32763, 32763) == 0,
          "an install at the floor is not");
    CHECK(mmo_feed_needs_repair(32800, 32763) == 0,
          "nor one above it");
    CHECK(mmo_feed_needs_repair(32700, 0) == 0,
          "and a feed with no floor asks for no repair");
}

/* --- the verdict over a real directory ---------------------------------- */

static int write_file(const char *root, const char *rel, const char *body)
{
    char path[512];
    FILE *f;

    snprintf(path, sizeof path, "%s/%s", root, rel);
    f = fopen(path, "wb");
    if (f == NULL)
        return -1;
    fputs(body, f);
    fclose(f);
    return 0;
}

static void test_check(void)
{
    /* sha256("hello\n") and sha256("goodbye\n"), computed independently with
     * sha256sum; the feed below is written around the first. */
    static const char HELLO[] =
        "5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03";
    char root[] = "/tmp/openmmo-feed-XXXXXX";
    char doc[1024];
    static mmo_feed_update u;
    mmo_feed_main m;
    mmo_feed_report r;
    char err[192] = "";

    if (mkdtemp(root) == NULL) {
        printf("  FAIL a scratch install directory can be made\n");
        failures++;
        return;
    }
    snprintf(doc, sizeof doc,
             "<update_feed><file name=\"openmmo\" sha256=\"%s\" size=\"6\"/>"
             "</update_feed>", HELLO);
    if (mmo_feed_parse_update(doc, strlen(doc), &u, err, sizeof err) != 0) {
        printf("  FAIL the scratch feed parses: %s\n", err);
        failures++;
        return;
    }
    memset(&m, 0, sizeof m);
    m.min_revision = 100;

    mmo_feed_check(root, &m, &u, "linux", "x64", "openmmo", &r);
    CHECK(r.verdict == MMO_FEED_STALE && r.missing == 1 &&
          strcmp(r.first_bad, "openmmo") == 0,
          "an install missing the file the feed lists is stale");
    CHECK(strstr(r.message, "openmmo") != NULL &&
          strstr(r.message, "update before playing") != NULL,
          "and says which file, in a line a player can act on");

    write_file(root, "openmmo", "goodbye\n");
    mmo_feed_check(root, &m, &u, "linux", "x64", "openmmo", &r);
    CHECK(r.verdict == MMO_FEED_STALE && r.mismatched == 1,
          "an install whose file has the wrong contents is stale too");
    CHECK(r.repair == 1,
          "and with no revision.txt the feed's floor asks for a repair");

    write_file(root, "openmmo", "hello\n");
    write_file(root, "revision.txt", "120\n");
    mmo_feed_check(root, &m, &u, "linux", "x64", "openmmo", &r);
    CHECK(r.verdict == MMO_FEED_OK && r.checked == 1 && r.repair == 0,
          "an install that matches the feed is allowed to start");
    CHECK(mmo_feed_installed_revision(root) == 120,
          "and revision.txt is read past its newline");

    write_file(root, "other", "hello\n");
    mmo_feed_check(root, &m, &u, "linux", "x64", "other", &r);
    CHECK(r.verdict == MMO_FEED_UNTRUSTED &&
          strstr(r.message, "does not vouch for other") != NULL,
          "a binary the feed does not list is refused by name");

    u.min_launcher_version = MMO_FEED_LAUNCHER_VERSION + 1;
    mmo_feed_check(root, &m, &u, "linux", "x64", "openmmo", &r);
    CHECK(r.verdict == MMO_FEED_LAUNCHER_OLD,
          "a feed that needs a newer front door stops this one first");
    u.min_launcher_version = 0;

    {
        char path[512];

        snprintf(path, sizeof path, "%s/openmmo", root);
        unlink(path);
        snprintf(path, sizeof path, "%s/other", root);
        unlink(path);
        snprintf(path, sizeof path, "%s/revision.txt", root);
        unlink(path);
        rmdir(root);
    }
}

/* An unsigned or wrongly signed directory never reaches a parser. */
static void test_load(void)
{
    mmo_rsa_pubkey live;
    mmo_feed_main m;
    static mmo_feed_update u;
    mmo_feed_report r;
    char err[192] = "";

    if (shipped_key(0, &live, err, sizeof err) != 0)
        return;
    CHECK(mmo_feed_load(FEEDS, &live, &m, &u, &r) == 0 &&
          m.revision == 32763 && u.n == 174,
          "both documents verify and parse straight out of the fixture tree");
    if (shipped_key(1, &live, err, sizeof err) == 0) {
        CHECK(mmo_feed_load(FEEDS, &live, &m, &u, &r) != 0 &&
              r.verdict == MMO_FEED_BAD &&
              strstr(r.message, "not signed by a key") != NULL,
              "and under the wrong key neither is parsed at all");
    }
    CHECK(mmo_feed_load("/nonexistent-feed-dir", &live, &m, &u, &r) != 0 &&
          r.verdict == MMO_FEED_BAD,
          "a feed directory that is not there is a refusal with a reason");
}

int feed_tests_run(void)
{
    failures = 0;
    test_verify();
    test_documents();
    test_refusals();
    test_sanitize();
    test_should_download();
    test_this_arch();
    test_repair();
    test_check();
    test_load();

    if (failures)
        printf("feed: %d check(s) FAILED\n", failures);
    else
        printf("feed: all checks passed\n");
    return failures;
}
