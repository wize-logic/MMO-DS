/* LoginRequest / LoginResponse codecs for the 2106 stream.
 *
 * Field order mirrors the server's LoginRequestPacketCodec. */
#include "login.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crypto.h"
#include "platform.h"

size_t mmo_login_hwid(u8 out[MMO_SHA256_DIGEST])
{
    char id[128];
    /* Where the id comes from is the host's business (platform.h); how short
     * a string is too short to be one is this protocol's. */
    size_t n = mmo_plat_machine_id(id, sizeof id);

    if (n < 10)
        return 0;
    mmo_sha256(id, n, out);
    return MMO_SHA256_DIGEST;
}

/* The byte the server's Platform enum numbers this host with. A cross-built
 * client is for exactly one of them, so this is settled at compile time,
 * and a Windows build that still said LINUX would be a client lying about
 * itself on every login. */
static u8 login_os(void)
{
    const char *os = mmo_plat_os_name();

    if (strcmp(os, "windows") == 0)
        return MMO_LOGIN_OS_WINDOWS;
    if (strcmp(os, "macos") == 0)
        return MMO_LOGIN_OS_MACOS;
    return MMO_LOGIN_OS_LINUX;
}

/* The revision of the install this client is part of. */
int mmo_login_installation_revision(void)
{
    const char *text = getenv("OPENMMO_REVISION");
    char *end;
    long value;

    if (text == NULL || *text == '\0')
        return MMO_CLIENT_REVISION;
    value = strtol(text, &end, 10);
    if (end == text || value <= 0 || value > 0x7fffffffL)
        return MMO_CLIENT_REVISION;
    return (int)value;
}

/* Everything after the method, which is the same for both of them. */
static void login_write_tail(mmo_wbuf *body)
{
    mmo_put_utf16_nt(body, "en");          /* language */
    mmo_put_s32le(body, MMO_CLIENT_REVISION);  /* clientRevision */
    mmo_put_s32le(body, mmo_login_installation_revision()); /* installationRevision */
    mmo_put_u8(body, login_os());          /* os */
    mmo_put_bytes_u8(body, NULL, 0);       /* hardwareInfoCache (empty) */
}

void mmo_login_write_request(mmo_wbuf *body,
                             const char *username,
                             const char *sha1_hex_pw,
                             int stay_logged_in)
{
    u8 hwid[MMO_SHA256_DIGEST];
    size_t hwidlen = mmo_login_hwid(hwid);

    mmo_put_utf16_nt(body, username);      /* username */
    mmo_put_bool(body, 1);                 /* manualLogin */
    mmo_put_bytes_u8(body, hwid, hwidlen); /* hwid */
    mmo_put_u8(body, 0);                   /* methodTag: 0 = PasswordLogin */
    mmo_put_utf16_nt(body, sha1_hex_pw);   /* password: SHA-1 hex string */
    mmo_put_bool(body, stay_logged_in != 0);   /* stayLoggedIn */
    login_write_tail(body);
}

void mmo_login_write_token_request(mmo_wbuf *body,
                                   const char *username,
                                   const u8 *token, size_t tokenlen)
{
    u8 hwid[MMO_SHA256_DIGEST];
    size_t hwidlen = mmo_login_hwid(hwid);

    if (tokenlen > MMO_LOGIN_TOKEN_MAX)
        tokenlen = MMO_LOGIN_TOKEN_MAX;
    mmo_put_utf16_nt(body, username);      /* username */
    /* Not a manual login: nobody typed anything to get here, and the server
     * logs the two differently. */
    mmo_put_bool(body, 0);                 /* manualLogin */
    mmo_put_bytes_u8(body, hwid, hwidlen); /* hwid */
    mmo_put_u8(body, 1);                   /* methodTag: 1 = TokenLogin */
    mmo_put_bytes_u8(body, token, tokenlen);
    login_write_tail(body);
}

/* ------------------------------------------------------------------ */
/* The remember me token                                               */
/* ------------------------------------------------------------------ */

/* base64, decode only. The token crosses as text because the packet field is
 * a string; nothing here ever has to encode one. */
static int b64_value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static size_t b64_decode(const char *s, u8 *out, size_t cap)
{
    u32 acc = 0;
    int bits = 0;
    size_t n = 0;

    for (; *s != '\0'; s++) {
        int v;

        if (*s == '=' || *s == '\r' || *s == '\n')
            continue;
        v = b64_value(*s);
        if (v < 0)
            return 0;           /* not base64: hand back nothing, not junk */
        acc = (acc << 6) | (u32)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n >= cap)
                return 0;
            out[n++] = (u8)((acc >> bits) & 0xFF);
        }
    }
    return n;
}

size_t mmo_login_read_credentials(const u8 *body, size_t n,
                                  u8 *out, size_t cap)
{
    mmo_rbuf r;
    char name[128], key[512];

    if (body == NULL || out == NULL || cap == 0)
        return 0;
    mmo_rbuf_init(&r, body, n);
    mmo_get_utf16_nt(&r, name, sizeof name);   /* display name, unused here */
    mmo_get_utf16_nt(&r, key, sizeof key);
    if (r.err || key[0] == '\0')
        return 0;
    return b64_decode(key, out, cap);
}


int mmo_login_read_response_state(const u8 *body, size_t n)
{
    if (n < 1)
        return -1;
    return body[0];
}

int mmo_login_explain_refusal(int state, char *buf, size_t n)
{
    const char *why;

    if (!buf || n == 0)
        return -1;
    switch (state) {
    case MMO_LOGIN_INVALID_PASSWORD:
        why = "that name or password was not accepted. "
              "Ask the person running this server for an account.";
        break;
    case MMO_LOGIN_ALREADY_LOGGED_IN:
        why = "that account is already signed in.";
        break;
    case MMO_LOGIN_ACCOUNT_ISSUE:
        why = "that account cannot sign in.";
        break;
    case MMO_LOGIN_SERVER_DOWN:
        why = "the login server is not taking sign-ins.";
        break;
    case MMO_LOGIN_SYSTEM_ERROR:
        why = "the login server reported a system error.";
        break;
    case MMO_LOGIN_INVALID_SAVED_CREDENTIALS:
        why = "saved credentials were not accepted.";
        break;
    case MMO_LOGIN_CLIENT_OUT_OF_DATE:
        why = "this client is out of date. Update it and sign in again; "
              "playing offline needs no update.";
        break;
    default:
        return snprintf(buf, n, "the login server refused (%d).", state);
    }
    return snprintf(buf, n, "%s", why);
}
