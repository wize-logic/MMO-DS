/* Which of this game's own props a carried one is. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROPEQUIV_MAX 128

struct propequiv_row {
    int carried;
    int native;
};

static struct propequiv_row s_rows[PROPEQUIV_MAX];
static int s_count = -1;

static void propequiv_load(void)
{
    const char *dir = getenv("PC_MODS_DIR");
    const char *mods = getenv("PC_MODS");
    char list[256];
    char *name, *save = NULL;

    s_count = 0;
    if (dir == NULL || mods == NULL)
        return;
    snprintf(list, sizeof list, "%s", mods);
    for (name = strtok_r(list, ",", &save); name != NULL;
         name = strtok_r(NULL, ",", &save)) {
        char path[512];
        FILE *f;
        int carried, native;

        snprintf(path, sizeof path, "%s/%s/.cooked/generated/prop_equiv.txt",
                 dir, name);
        f = fopen(path, "r");
        if (f == NULL)
            continue;
        while (s_count < PROPEQUIV_MAX
               && fscanf(f, "%d %d %*s", &carried, &native) == 2) {
            s_rows[s_count].carried = carried;
            s_rows[s_count].native = native;
            s_count++;
        }
        fclose(f);
    }
    if (s_count > 0)
        printf("openmmo: %d carried prop(s) stand for this game's own\n",
               s_count);
}

/* What a loaded prop's model id means to a search: itself, or the native
 * id it stands for. */
int openmmo_prop_equiv(int modelID)
{
    int i;

    if (s_count < 0)
        propequiv_load();
    for (i = 0; i < s_count; i++) {
        if (s_rows[i].carried == modelID)
            return s_rows[i].native;
    }
    return modelID;
}
