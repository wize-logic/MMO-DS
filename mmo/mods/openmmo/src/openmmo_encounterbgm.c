/* What a ported trainer is heard as. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The carried trainers of one region pair: 390 today, and the ceiling is the
 * defeated-flag block's (mods/openmmo/patches/include/vars_flags.h), so a
 * package that fits there fits here. */
#define ENCOUNTERBGM_MAX 768

struct encounterbgm_row {
    unsigned short trainer;
    unsigned short seq;
};

static struct encounterbgm_row s_rows[ENCOUNTERBGM_MAX];
static int s_count = -1;

static void encounterbgm_load(void)
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
        unsigned trainer, seq;

        snprintf(path, sizeof path,
                 "%s/%s/.cooked/generated/encounter_bgm.txt", dir, name);
        f = fopen(path, "r");
        if (f == NULL)
            continue;
        while (s_count < ENCOUNTERBGM_MAX
               && fscanf(f, "%u %u", &trainer, &seq) == 2) {
            s_rows[s_count].trainer = (unsigned short)trainer;
            s_rows[s_count].seq = (unsigned short)seq;
            s_count++;
        }
        fclose(f);
    }
    if (s_count > 0)
        printf("openmmo: %d ported trainer(s) are heard as their own class\n",
               s_count);
}

/* The track this trainer's own cartridge announces them with, or 0 for
 * anybody the package says nothing about, and 0 is the caller's signal to
 * keep the answer it already had. */
unsigned int openmmo_encounter_bgm(int trainerID)
{
    int i;

    if (s_count < 0)
        encounterbgm_load();
    for (i = 0; i < s_count; i++) {
        if (s_rows[i].trainer == (unsigned short)trainerID)
            return s_rows[i].seq;
    }
    return 0;
}
