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
