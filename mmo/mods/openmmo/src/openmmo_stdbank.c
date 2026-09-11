/* Where a ported map's common scripts live. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STDBANK_MAX 8

struct stdbank_row {
    unsigned base;
    int script_member;
    unsigned text_member;
    unsigned count;
};

static struct stdbank_row s_rows[STDBANK_MAX];
static int s_count = -1;

static void stdbank_load(void)
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
        struct stdbank_row r;

        snprintf(path, sizeof path, "%s/%s/.cooked/generated/std_banks.txt",
                 dir, name);
        f = fopen(path, "r");
        if (f == NULL)
            continue;
        while (s_count < STDBANK_MAX
               && fscanf(f, "%u %d %u %u", &r.base, &r.script_member,
                         &r.text_member, &r.count) == 4)
            s_rows[s_count++] = r;
        fclose(f);
    }
    if (s_count > 0)
        printf("openmmo: %d common script bank(s) from the packages\n",
               s_count);
}

/* The patched loader's first question. A hit fills the member pair and the
 * entry index within the member; a miss changes nothing. */
int openmmo_std_bank(unsigned scriptID, int *scriptFile, unsigned *textBank,
                     unsigned *entry)
{
    int i;

    if (s_count < 0)
        stdbank_load();
    for (i = 0; i < s_count; i++) {
        if (scriptID >= s_rows[i].base
            && scriptID < s_rows[i].base + s_rows[i].count) {
            *scriptFile = s_rows[i].script_member;
            *textBank = s_rows[i].text_member;
            *entry = scriptID - s_rows[i].base;
            return 1;
        }
    }
    return 0;
}
