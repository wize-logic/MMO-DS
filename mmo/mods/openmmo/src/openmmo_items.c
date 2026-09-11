/* What the engine asks about an item this game did not ship with. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ITEMS_FILL_MAX 128

struct fill_row {
    unsigned item;
    unsigned data;
    unsigned ncgr;
    unsigned nclr;
};

static struct fill_row s_rows[ITEMS_FILL_MAX];
static int s_count = -1;
static unsigned s_max;

static void load(void)
{
    const char *dir = getenv("PC_MODS_DIR");
    const char *mods = getenv("PC_MODS");
    char list[256];
    char *name, *save = NULL;

    s_count = 0;
    s_max = 0;
    if (dir == NULL || mods == NULL)
        return;
    snprintf(list, sizeof list, "%s", mods);
    for (name = strtok_r(list, ",", &save); name != NULL;
         name = strtok_r(NULL, ",", &save)) {
        char path[512];
        char line[128];
        FILE *f;

        snprintf(path, sizeof path, "%s/%s/.cooked/generated/items_fill.txt",
                 dir, name);
        f = fopen(path, "r");
        if (f == NULL)
            continue;
        while (s_count < ITEMS_FILL_MAX && fgets(line, sizeof line, f) != NULL) {
            struct fill_row r;

            if (line[0] == '#')
                continue;
            if (sscanf(line, "%u %u %u %u", &r.item, &r.data, &r.ncgr, &r.nclr) != 4)
                continue;
            s_rows[s_count++] = r;
            if (r.item > s_max)
                s_max = r.item;
        }
        fclose(f);
    }
    if (s_count > 0)
        printf("openmmo: %d item(s) filled past this game's table, to %u\n",
               s_count, s_max);
}

/* The engine's own enum ItemFileType, by value: data 0, icon 1, palette 2. */
int openmmo_item_archive(unsigned item, int type, unsigned *member)
{
    int i;

    if (s_count < 0)
        load();
    for (i = 0; i < s_count; i++) {
        if (s_rows[i].item != item)
            continue;
        switch (type) {
        case 0: *member = s_rows[i].data; return 1;
        case 1: *member = s_rows[i].ncgr; return 1;
        case 2: *member = s_rows[i].nclr; return 1;
        default: return 0;
        }
    }
    return 0;
}

/* The highest item id a fill serves, or 0 without one. */
unsigned openmmo_item_max(void)
{
    if (s_count < 0)
        load();
    return s_max;
}
