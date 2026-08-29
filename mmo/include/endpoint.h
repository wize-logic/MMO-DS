/* The one server this client talks to. */
#ifndef MMO_ENDPOINT_H
#define MMO_ENDPOINT_H

#include <stddef.h>

#include "mmo.h"
#include "obfstr.h"

/* A buffer big enough for any address this client can be built with. */
#define OPENMMO_ENDPOINT_HOST_MAX OBFSTR_MAX

/* The login server's address, into `buf`, NUL-terminated; returns buf. */
const char *openmmo_endpoint_host(char *buf, size_t cap);

/* The login server's TCP port. */
u16 openmmo_endpoint_login_port(void);

/* The game server's TCP port, or 0 to take the one login advertises. The host
 * for that connection is never ours: it comes out of the handoff. */
u16 openmmo_endpoint_game_port(void);

/* Wipe a buffer openmmo_endpoint_host() filled, once the address is no longer
 * needed. Same call as obfstr_wipe, named for this use so the pairing reads. */
void openmmo_endpoint_forget(char *buf, size_t cap);

/* 1 in a release build: the address above is the only one this program can
 * reach. 0 in a build from this tree, where the suite and a local server need
 * to point it somewhere else and the environment is allowed to. Programs that
 * say where they are going should say it through this rather than guess. */
int openmmo_endpoint_is_pinned(void);

#endif /* MMO_ENDPOINT_H */
