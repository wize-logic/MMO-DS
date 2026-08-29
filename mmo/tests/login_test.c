/* The LoginRequest encoder against the official client's bytes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "client.h"
#include "crypto.h"
#include "login.h"
#include "token.h"
#include "platform.h"

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

static size_t unhex(const char *hex, u8 *out, size_t cap)
{
    size_t n = 0;
    for (const char *p = hex; p[0] && p[1] && n < cap; p += 2) {
        unsigned byte;
        sscanf(p, "%2x", &byte);
        out[n++] = (u8)byte;
    }
    return n;
}

/* The official LoginRequest for user "test" / password "test", up to the hwid:
 * the username and manualLogin. The capture clears manualLogin where the client
 * sets it, so that one byte is the client's own value, not the official client's. */
static const char *LOGIN_REQ_HEAD_HEX = "7400650073007400000001";

/* The same capture from the method tag on: PasswordLogin(SHA-1 hex of "test"),
 * stayLoggedIn clear, language "en", both revisions the official client, os linux and an
 * empty hardwareInfoCache. */
static const char *LOGIN_REQ_TAIL_HEX =
    "0061003900340061003800660065003500630063006200310039006200610036"
    "0031006300340063003000380037003300640033003900310065003900380037"
    "003900380032006600620062006400330000000065006e000000aa7c0000aa7c"
    "00000100";

static void test_login_request(void)
{
    printf("login request (captures, user/pass \"test\"):\n");

    /* expect = head || u8 hwid length || hwid || tail. */
    u8 expect[256];
    size_t elen = unhex(LOGIN_REQ_HEAD_HEX, expect, sizeof expect);
    CHECK(elen == 11, "the pinned head is the username and manualLogin");

    u8 hwid[MMO_SHA256_DIGEST];
    size_t hwidlen = mmo_login_hwid(hwid);
    CHECK(hwidlen == 0 || hwidlen == MMO_SHA256_DIGEST,
          "the hwid is a SHA-256 digest, or absent on a host with no machine id");
    expect[elen++] = (u8)hwidlen;
    memcpy(expect + elen, hwid, hwidlen);
    elen += hwidlen;
    elen += unhex(LOGIN_REQ_TAIL_HEX, expect + elen, sizeof expect - elen);
    CHECK(hwidlen == 0 || elen == 144, "the expected body is the official client's 144 bytes");

    char pwhex[41];
    mmo_sha1_hex("test", 4, pwhex);

    mmo_wbuf w;
    mmo_wbuf_init(&w);
    /* The capture was taken of a login that did not ask to be remembered, so
     * this asks the same to stay comparable with it. */
    mmo_login_write_request(&w, "test", pwhex, 0);

    CHECK(!w.err, "encoder reported no error");
    CHECK(w.len == elen, "encoded length matches the expected body");
    CHECK(w.len == elen && memcmp(w.data, expect, elen) == 0,
          "encoded LoginRequest matches official byte for byte around the hwid");

    /*
     * Asking to be remembered is one byte, and it has to be the stayLoggedIn byte rather than
     * anything else moving. This is what earns the client a token, so getting it wrong is a
     * password typed every session.
     */
    {
        mmo_wbuf stay;
        size_t i, diff = 0, at = 0;

        mmo_wbuf_init(&stay);
        mmo_login_write_request(&stay, "test", pwhex, 1);
        CHECK(!stay.err && stay.len == w.len,
              "asking to be remembered does not change the length");
        for (i = 0; i < stay.len && i < w.len; i++) {
            if (stay.data[i] != w.data[i]) {
                diff++;
                at = i;
            }
        }
        CHECK(diff == 1, "exactly one byte changes");
        CHECK(diff == 1 && w.data[at] == 0 && stay.data[at] == 1,
              "and it is the stayLoggedIn bool, false to true");
        mmo_wbuf_free(&stay);
    }
    mmo_wbuf_free(&w);
}

/* The other way in: a token instead of a password. */
static void test_token_request(void)
{
    static const u8 tok[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01 };
    mmo_wbuf w;
    size_t at;

    printf("token login request:\n");
    mmo_wbuf_init(&w);
    mmo_login_write_token_request(&w, "test", tok, sizeof tok);
    CHECK(!w.err, "the token request encodes");

    /* username "test" is 5 UTF-16 units including the terminator, then the
     * manualLogin bool. */
    at = 5 * 2;
    CHECK(w.len > at && w.data[at] == 0,
          "a token login is not a manual login: nobody typed anything");
    at += 1 + 1 + (size_t)w.data[at + 1];   /* bool, then the u8-prefixed hwid */
    CHECK(w.len > at && w.data[at] == 1, "the method tag says token, not password");
    at++;
    CHECK(w.len > at && w.data[at] == sizeof tok, "the token is length prefixed");
    CHECK(w.len > at + sizeof tok &&
          memcmp(w.data + at + 1, tok, sizeof tok) == 0,
          "and it is the token that was handed in");
    mmo_wbuf_free(&w);
}

/* The token arrives as base64 in a string field, and has to come back out. */
static void test_credentials_decode(void)
{
    /* username "a", then key "3q2+7wE=" which is DE AD BE EF 01. */
    static const u8 body[] = {
        'a', 0x00, 0x00, 0x00,
        '3', 0x00, 'q', 0x00, '2', 0x00, '+', 0x00, '7', 0x00, 'w', 0x00,
        'E', 0x00, '=', 0x00, 0x00, 0x00
    };
    static const u8 want[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01 };
    u8 out[MMO_LOGIN_TOKEN_MAX];
    size_t n;

    printf("credentials packet:\n");
    n = mmo_login_read_credentials(body, sizeof body, out, sizeof out);
    CHECK(n == sizeof want, "the base64 key decodes to the token's length");
    CHECK(n == sizeof want && memcmp(out, want, n) == 0,
          "and to the token's bytes, padding and all");

    /* The server sends the same packet with an empty key to say it has none,
     * which is not a malformed packet. */
    {
        static const u8 empty[] = { 'a', 0x00, 0x00, 0x00, 0x00, 0x00 };

        CHECK(mmo_login_read_credentials(empty, sizeof empty, out,
                                         sizeof out) == 0,
              "an empty key is no token rather than an error");
    }
}

/*
 * The token on disk. $XDG_CONFIG_HOME is moved somewhere disposable first, because this writes
 * a real file and the one it would otherwise write is the developer's own saved sign-in.
 */
static void test_token_store(void)
{
    static const u8 tok[] = { 0x01, 0x02, 0x03, 0x04 };
    char tmpl[] = "/tmp/openmmo-token-XXXXXX";
    char *dir = mkdtemp(tmpl);
    char path[1024], sub[1200], who[MMO_TOKEN_NAME];
    const char *had = getenv("XDG_CONFIG_HOME");
    char keep[1024];
    u8 out[MMO_TOKEN_MAX];
    struct stat st;

    printf("token store:\n");
    if (dir == NULL) {
        CHECK(0, "a temporary config home was made");
        return;
    }
    /* Held to put back, not just unset: the machine running the suite may
     * have one of its own, and a later test that asks for the config home
     * must see what it would have seen. */
    keep[0] = '\0';
    if (had != NULL)
        snprintf(keep, sizeof keep, "%s", had);
    mmo_plat_setenv("XDG_CONFIG_HOME", dir, 1);

    CHECK(mmo_token_load("lucas", out, sizeof out) == 0,
          "no saved sign-in yet is none, not a failure");
    CHECK(mmo_token_who(who, sizeof who) == 0,
          "and nobody to name on the front door");
    CHECK(mmo_token_save("lucas", tok, sizeof tok) == 0,
          "the saved sign-in is written");
    CHECK(mmo_token_path(path, sizeof path) == 0 &&
          strstr(path, "openmmo") != NULL && strstr(path, "token") != NULL,
          "and it goes to a file of its own, not into launcher.cfg");
    /* It is a credential, so nobody else on the machine may read it. */
    CHECK(stat(path, &st) == 0 && (st.st_mode & 0077) == 0,
          "and only its owner can read it");
    CHECK(mmo_token_load("lucas", out, sizeof out) == sizeof tok &&
          memcmp(out, tok, sizeof tok) == 0,
          "it reads back byte for byte");
    CHECK(mmo_token_who(who, sizeof who) == 1 && strcmp(who, "lucas") == 0,
          "and the front door can say whose session it is about to resume");
    /* The server compares account names without case, so this has to agree:
     * a player who typed their name with a capital still gets their own
     * sign-in rather than being told to find their password. */
    CHECK(mmo_token_load("Lucas", out, sizeof out) == sizeof tok,
          "the name is matched without case, the way the server matches it");
    /*
     * The whole reason the name is in the file. Sending one account's token under another's
     * name is a refusal the server would have to issue, and the player would be told their
     * saved sign-in expired when the truth is that it was never theirs to use.
     */
    CHECK(mmo_token_load("barry", out, sizeof out) == 0,
          "another account's saved sign-in is not offered to this one");
    CHECK(mmo_token_who(who, sizeof who) == 1 && strcmp(who, "lucas") == 0,
          "and asking did not disturb it");

    mmo_token_clear();
    CHECK(mmo_token_load("lucas", out, sizeof out) == 0,
          "signing out leaves nothing to sign in with");
    CHECK(mmo_token_who(who, sizeof who) == 0, "and nobody to name");

    /* A name with a newline in it would forge the split the file is parsed
     * on, and the reader would hand back the front of a credential as an
     * account name. */
    CHECK(mmo_token_save("lu\ncas", tok, sizeof tok) == -1,
          "a name that could forge the file's own shape is refused");
    CHECK(mmo_token_who(who, sizeof who) == 0,
          "and nothing was written by the attempt");

    if (keep[0] != '\0')
        mmo_plat_setenv("XDG_CONFIG_HOME", keep, 1);
    else
        mmo_plat_unsetenv("XDG_CONFIG_HOME");
    /* token_save made <dir>/openmmo on the way, so the directory is not empty
     * and a bare rmdir(dir) was quietly leaving both behind in /tmp. */
    snprintf(sub, sizeof sub, "%s/openmmo", dir);
    rmdir(sub);
    rmdir(dir);
}

static void test_response_state(void)
{
    printf("login response state:\n");

    /* AUTHED body = state 0x00 then an empty UTF-16 string (00 00). */
    const u8 authed[] = { 0x00, 0x00, 0x00 };
    CHECK(mmo_login_read_response_state(authed, sizeof authed) == MMO_LOGIN_AUTHED,
          "AUTHED body decodes to state 0x00");

    /* INVALID_PASSWORD body = state 0x02, no trailer. */
    const u8 bad[] = { MMO_LOGIN_INVALID_PASSWORD };
    CHECK(mmo_login_read_response_state(bad, sizeof bad) == MMO_LOGIN_INVALID_PASSWORD,
          "INVALID_PASSWORD body decodes to state 0x02");

    CHECK(mmo_login_read_response_state(NULL, 0) == -1,
          "empty body is rejected");
}

/* How much room a failure message has when it reaches a player. */
#define MESSAGE_ROOM ((int)sizeof(((openmmo_event *)0)->message))

static void test_refusal_sentence(void)
{
    char why[MESSAGE_ROOM];
    int n;

    printf("login refusal sentence:\n");

    CHECK(mmo_login_explain_refusal(MMO_LOGIN_INVALID_PASSWORD, NULL, 16) == -1,
          "a missing buffer is refused");

    n = mmo_login_explain_refusal(MMO_LOGIN_INVALID_PASSWORD, why, sizeof why);
    CHECK(n > 0 && n < MESSAGE_ROOM, "INVALID_PASSWORD fits in the session message");
    CHECK(strstr(why, "not accepted") != NULL && strstr(why, "account") != NULL,
          "INVALID_PASSWORD names the miss and how to get an account");

    n = mmo_login_explain_refusal(MMO_LOGIN_ALREADY_LOGGED_IN, why, sizeof why);
    CHECK(n > 0 && n < MESSAGE_ROOM && strstr(why, "already signed in") != NULL,
          "ALREADY_LOGGED_IN is named");

    n = mmo_login_explain_refusal(99, why, sizeof why);
    CHECK(n > 0 && n < MESSAGE_ROOM && strstr(why, "99") != NULL,
          "an unnamed state is reported by number rather than as 'rejected'");
}

int login_tests_run(void)
{
    failures = 0;
    test_login_request();
    test_token_request();
    test_credentials_decode();
    test_token_store();
    test_response_state();
    test_refusal_sentence();

    if (failures)
        printf("login: %d check(s) FAILED\n", failures);
    else
        printf("login: all checks passed\n");
    return failures;
}
