/* A session does not use a player .sav. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../include/platform.h"

void openmmo_save_session_prepare(void)
{
    const char *session;
    const char *save;
    static int done;

    if (done)
        return;
    done = 1;

    session = getenv("OPENMMO_SESSION");
    if (session == NULL || session[0] == '\0' || session[0] == '0')
        return;

    /* Stated here rather than in the plan, so a plan still names no player
     * save and there is one place that decides what a session's card is. */
    {
        const char *exp = getenv("OPENMMO_EXPORT");

        if (exp != NULL && exp[0] != '\0') {
            if (mmo_plat_setenv("PC_SAVE", exp, 1) == 0) {
                printf("openmmo: session is server-sided; an offline copy"
                       " would go to %s\n", exp);
                return;
            }
            fprintf(stderr, "openmmo: cannot point PC_SAVE at %s\n", exp);
        }
    }

    save = getenv("PC_SAVE");
    if (save != NULL && strcmp(save, "none") == 0) {
        printf("openmmo: session is server-sided; no player save file\n");
        return;
    }

    if (save != NULL && save[0] != '\0')
        printf("openmmo: session is server-sided; dropped player save %s\n", save);
    else
        printf("openmmo: session is server-sided; no player save file\n");

    if (mmo_plat_setenv("PC_SAVE", "none", 1) != 0)
        fprintf(stderr, "openmmo: cannot drop PC_SAVE for a session\n");
}
