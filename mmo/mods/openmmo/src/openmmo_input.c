/* Who decides what a button means. */

#include <nitro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/game_options.h"

/* The launcher's spelling, which is also the config file's. The engine's own
 * names are numbers in an enum nobody types. */
static const struct {
    const char *name;
    int mode;
} MODES[] = {
    { "normal", OPTIONS_BUTTON_MODE_NORMAL },
    { "start-is-x", OPTIONS_BUTTON_MODE_START_IS_X },
    { "l-is-a", OPTIONS_BUTTON_MODE_L_IS_A },
};

#define MODES_N ((int)(sizeof MODES / sizeof MODES[0]))

const char *openmmo_button_mode_name(int mode)
{
    int i;

    for (i = 0; i < MODES_N; i++) {
        if (MODES[i].mode == mode) {
            return MODES[i].name;
        }
    }

    return "unknown";
}

/*
 * Read once, because it is read on every frame the pad is read on and because a value that
 * changed underneath the game would be a control scheme changing mid-battle.
 */
int openmmo_button_mode(void)
{
    static int mode = -1;
    const char *v;
    int i;

    if (mode >= 0) {
        return mode;
    }

    mode = OPTIONS_BUTTON_MODE_NORMAL;
    v = getenv("OPENMMO_BUTTON_MODE");

    if (v != NULL && v[0] != '\0') {
        for (i = 0; i < MODES_N; i++) {
            if (strcmp(v, MODES[i].name) == 0) {
                mode = MODES[i].mode;
                break;
            }
        }

        if (i == MODES_N) {
            fprintf(stderr,
                    "openmmo: OPENMMO_BUTTON_MODE=%s is not a button mode"
                    " (normal, start-is-x, l-is-a); using normal\n",
                    v);
            fflush(stderr);
        }
    }

    if (getenv("OPENMMO_INPUT_REPORT") != NULL) {
        fprintf(stderr, "openmmo: button mode %s, from the host\n",
                openmmo_button_mode_name(mode));
        fflush(stderr);
    }

    return mode;
}
