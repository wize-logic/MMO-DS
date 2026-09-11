/*
 * The two answers a build gives about itself, in a file every program here can
 * link.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "endpoint.h"

/* Written by the Makefile into $(build)/gen. Deliberately not defaulted here:
 * a build that cannot say whether it is a release should not silently pick. */
#include "endpoint_pin.h"

int openmmo_dev_features(void)
{
#if OPENMMO_PIN_DEV_FEATURES
    return 1;
#else
    return 0;
#endif
}

const char *openmmo_dev_env(const char *name)
{
    const char *v = getenv(name);

    if (openmmo_dev_features())
        return v;

    /* Once per run, and only for a door somebody actually tried: the notice
     * must not read out the list of them to a player who set none. */
    if (v != NULL && v[0] != '\0') {
        static int said;

        if (!said) {
            said = 1;
            printf("openmmo: %s is a development build's setting and this"
                   " build ignores it\n", name);
            fflush(stdout);
        }
    }
    return NULL;
}
