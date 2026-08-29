/* The saved sign-in, which is what a player has on disk instead of a password. */

#ifndef OPENMMO_TOKEN_H
#define OPENMMO_TOKEN_H

#include <stddef.h>

#include "mmo.h"

/* The wire writes a token with a one-byte length, so this is the ceiling the
 * protocol imposes rather than one chosen here (login.h sends it). */
#define MMO_TOKEN_MAX  255
/* An account name, the same width the client and the front door keep. */
#define MMO_TOKEN_NAME  64

/* Whether two account names are the same one, compared the way the server
 * compares them (without case, ASCII). Published because the front door has
 * to ask it too, and two spellings of that rule would be one too many. */
int mmo_token_name_is(const char *a, const char *b);

/* Where the file lives: `token`, beside launcher.cfg. 0 or -1. */
int mmo_token_path(char *out, size_t cap);

/* Keep `token` as the saved sign-in for the account `name`. Any earlier one
 * is replaced. 0 or -1; a failure leaves no file rather than half of one. */
int mmo_token_save(const char *name, const u8 *token, size_t n);

/* The saved sign-in for `name`, if the one on disk belongs to that account
 * (compared without case, the way the server compares it). Returns its length
 * in bytes, or 0 when there is none to use, which is not an error, it is a
 * player who has not signed in on this device yet. */
size_t mmo_token_load(const char *name, u8 *out, size_t cap);

/* Who the saved sign-in is for, without reading the credential itself.
 * Returns 1 and writes the name, or 0 when there is no saved sign-in. This is
 * what the front door asks so it can say whose session it is about to
 * resume. */
int mmo_token_who(char *out, size_t cap);

/* Forget the saved sign-in on this device. Signing out is the only thing a
 * player can do about a credential they cannot see, so it removes the file
 * rather than emptying it. */
void mmo_token_clear(void);

#endif /* OPENMMO_TOKEN_H */
