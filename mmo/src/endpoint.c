/* The compiled-in server address, and the only place it exists. */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "endpoint.h"
#include "obfstr.h"

/* Written by the Makefile into $(build)/gen. Deliberately not defaulted here:
 * a build that cannot say which server it is for should not silently pick. */
#include "endpoint_pin.h"

/* One site, one key. Named, because OBFSTR_KEY reads __TIME__ and the array
 * and the decode below have to be given the same one. */
#define PIN_KEY OBFSTR_KEY(1u)

OBFSTR_FITS(pin_host_fits, OPENMMO_PIN_HOST);

static const unsigned char pin_host[OBFSTR_MAX] = {
    OBFSTR_BYTES(OPENMMO_PIN_HOST, PIN_KEY)
};

/* The low half of the same key, so the two ports are not the only plain
 * numbers in the neighbourhood of the address. */
#define PIN_PORT_MASK ((unsigned)(PIN_KEY) & 0xFFFFu)

static const u16 pin_login_port =
    (u16)(((unsigned)OPENMMO_PIN_LOGIN_PORT) ^ PIN_PORT_MASK);
static const u16 pin_game_port =
    (u16)(((unsigned)OPENMMO_PIN_GAME_PORT) ^ PIN_PORT_MASK);

#if OPENMMO_PIN_SETTABLE
/*
 * A build from this tree. OPENMMO_SERVER is HOST or HOST:PORT and replaces the pinned address
 * and (when it carries one) the login port; OPENMMO_GAMEPORT replaces the game port.
 */
static const char *env_server(void)
{
    const char *s = getenv("OPENMMO_SERVER");

    return (s != NULL && s[0] != '\0') ? s : NULL;
}

/* The ":PORT" of an OPENMMO_SERVER, or 0. Split on the LAST colon, which is
 * the same split openmmo_boot.c used to do and has the same limit: a bare IPv6
 * literal is cut at its last group. It is a development seam pointed at a
 * server on this machine, not an address a player types. */
static u16 env_server_port(void)
{
    const char *s = env_server();
    const char *colon;

    if (s == NULL)
        return 0;
    colon = strrchr(s, ':');
    if (colon == NULL || colon[1] == '\0')
        return 0;
    return (u16)strtoul(colon + 1, NULL, 10);
}
#endif /* OPENMMO_PIN_SETTABLE */

const char *openmmo_endpoint_host(char *buf, size_t cap)
{
    if (buf == NULL || cap == 0)
        return buf;

#if OPENMMO_PIN_SETTABLE
    {
        const char *s = env_server();

        if (s != NULL) {
            const char *colon = strrchr(s, ':');
            size_t n = (colon != NULL) ? (size_t)(colon - s) : strlen(s);

            if (n >= cap)
                n = cap - 1;
            memcpy(buf, s, n);
            buf[n] = '\0';
            return buf;
        }
    }
#endif

    return obfstr_decode(pin_host, PIN_KEY, buf, cap);
}

u16 openmmo_endpoint_login_port(void)
{
#if OPENMMO_PIN_SETTABLE
    {
        u16 p = env_server_port();

        if (p != 0)
            return p;
    }
#endif
    return (u16)(((unsigned)pin_login_port) ^ PIN_PORT_MASK);
}

u16 openmmo_endpoint_game_port(void)
{
#if OPENMMO_PIN_SETTABLE
    {
        const char *g = getenv("OPENMMO_GAMEPORT");

        if (g != NULL && g[0] != '\0')
            return (u16)strtoul(g, NULL, 10);
    }
#endif
    return (u16)(((unsigned)pin_game_port) ^ PIN_PORT_MASK);
}

void openmmo_endpoint_forget(char *buf, size_t cap)
{
    obfstr_wipe(buf, cap);
}

int openmmo_endpoint_is_pinned(void)
{
#if OPENMMO_PIN_SETTABLE
    return 0;
#else
    return 1;
#endif
}
