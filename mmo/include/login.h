/* The login server's application packets (2106 stream). */
#ifndef MMO_LOGIN_H
#define MMO_LOGIN_H

#include <stddef.h>

#include "mmo.h"
#include "codec.h"
#include "crypto.h"

/* Application opcodes on the login protocol (LoginProtocol). */
#define MMO_LOGIN_OP_REQUEST  0x11
#define MMO_LOGIN_OP_RESPONSE 0x01
/* s2c, and it arrives before the response it belongs to: the remember me
 * token, as a base64 string beside the display name. */
#define MMO_LOGIN_OP_CREDENTIALS 0x07

/* The token is written with a one-byte length, so this is the ceiling the
 * wire imposes rather than one we chose. */
#define MMO_LOGIN_TOKEN_MAX 255

/* LoginState ids the client acts on; the full table lives server-side. */
#define MMO_LOGIN_AUTHED                      0x00
#define MMO_LOGIN_SYSTEM_ERROR                0x01
#define MMO_LOGIN_INVALID_PASSWORD            0x02
#define MMO_LOGIN_ALREADY_LOGGED_IN           0x07
#define MMO_LOGIN_SERVER_DOWN                 0x08
#define MMO_LOGIN_ACCOUNT_ISSUE               0x09
#define MMO_LOGIN_INVALID_SAVED_CREDENTIALS   0x1e

/* The protocol revision this client speaks, sent as both the client and the
 * installation revision. the official client is the revision of the client the wire format
 * here is taken from.
 */
#define MMO_CLIENT_REVISION 31914

/* Platform ids carried by the LoginRequest's `os` byte:
 * WINDOWS 0, LINUX 1, MAC 2, IOS 3, ANDROID 4, UNKNOWN 0xFF. The one this
 * build sends is the host it was compiled for, which is what the server's own
 * Platform enum is numbered for. */
#define MMO_LOGIN_OS_WINDOWS 0
#define MMO_LOGIN_OS_LINUX   1
#define MMO_LOGIN_OS_MACOS   2

/*
 * SHA-256 of this machine's id, the value the `hwid` field carries. The id is whatever this
 * host calls one, /etc/machine-id (or dbus's copy) on Linux, the installer's MachineGuid on
 * Windows, and must be at least 10 characters.
 */
size_t mmo_login_hwid(u8 out[MMO_SHA256_DIGEST]);

/*
 * Write the LoginRequest body (no opcode) for a password login into `body`: the username,
 * manualLogin=true, this machine's hwid, PasswordLogin(sha1_hex_pw, stayLoggedIn), language
 * "en", the client and installation revisions, the os byte and an empty hardwareInfoCache.
 */
void mmo_login_write_request(mmo_wbuf *body,
                             const char *username,
                             const char *sha1_hex_pw,
                             int stay_logged_in);

/*
 * The same request with a remember me token in place of the password, which is what a second
 * session sends.
 */
void mmo_login_write_token_request(mmo_wbuf *body,
                                   const char *username,
                                   const u8 *token, size_t tokenlen);

/* Read a SentCredentials body (opcode already stripped) and decode the token out of it. */
size_t mmo_login_read_credentials(const u8 *body, size_t n,
                                  u8 *out, size_t cap);

/* Where the token is kept between sessions, and who it belongs to, is
 * Both halves of the client link that, this protocol half is only
 * what crosses the wire. */

/* Read the state byte of a LoginResponse body (opcode already stripped).
 * Returns the LoginState id, or -1 if the body is empty. */
int mmo_login_read_response_state(const u8 *body, size_t n);

/*
 * Write the sentence a player is told when LoginResponse is not AUTHED. Returns the number of
 * characters written (not counting the NUL), or -1 if `buf` is unusable.
 */
int mmo_login_explain_refusal(int state, char *buf, size_t n);

#endif /* MMO_LOGIN_H */
