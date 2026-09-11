/*
 * The plugin's compositor, run on the host over a fill on disk,
 * printing the composed canvas at chosen ticks so the tool's own compose() can be diffed
 * against it (tests/blackanim_test.sh).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef short s16;
typedef unsigned int u32;
typedef int s32;
#define MAX_MON_SPRITES 4

static const char *s_pkg;

int pc_modfs_member_stat(const char *nitro_path, unsigned index, unsigned *out_size);
int pc_modfs_member_read(const char *nitro_path, unsigned index, void *dest, unsigned offset, unsigned bytesToRead);

#define OPENMMO_BLACKANIM_HARNESS
#include "../mods/openmmo/src/openmmo_blackanim.c"

static FILE *open_member(const char *nitro_path, unsigned index, long *size)
{
    char path[1024];
    FILE *fp;

    snprintf(path, sizeof path, "%s/narc/%s/%u", s_pkg, nitro_path, index);
    fp = fopen(path, "rb");
    if (fp == NULL)
        return NULL;
    fseek(fp, 0, SEEK_END);
    *size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    return fp;
}

int pc_modfs_member_stat(const char *nitro_path, unsigned index, unsigned *out_size)
{
    long size;
    FILE *fp = open_member(nitro_path, index, &size);

    if (fp == NULL)
        return 0;
    fclose(fp);
    *out_size = (unsigned)size;
    return 1;
}

int pc_modfs_member_read(const char *nitro_path, unsigned index, void *dest, unsigned offset, unsigned bytesToRead)
{
    long size;
    FILE *fp = open_member(nitro_path, index, &size);
    unsigned n;

    if (fp == NULL)
        return 0;
    if (offset > (unsigned)size)
        n = 0;
    else if (bytesToRead == 0 || offset + bytesToRead > (unsigned)size)
        n = (unsigned)size - offset;
    else
        n = bytesToRead;
    fseek(fp, offset, SEEK_SET);
    if (n && fread(dest, 1, n, fp) != n) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return 1;
}

int main(int argc, char **argv)
{
    int character, i;
    struct face *f = &s_face[0];

    if (argc < 4) {
        fprintf(stderr, "usage: %s <package> <character> <tick>...\n", argv[0]);
        return 2;
    }
    s_pkg = argv[1];
    character = atoi(argv[2]);
    if (!load(f, character)) {
        printf("no loop\n");
        return 1;
    }
    printf("period %u marks %d bbox %d %d %d %d factor %.4f left %d top %d\n", f->period,
           (int)({ int m = 0; u32 t; for (t = 0; t < f->period; t++) m += f->change[t]; m; }),
           f->x0 - ORIGIN, f->y0 - ORIGIN, f->w, f->h, f->factor, f->left, f->top);
    for (i = 3; i < argc; i++) {
        u32 t = (u32)atoi(argv[i]);
        int x, y;

        compose(f, t);
        printf("tick %u\n", t);
        for (y = 0; y < CANVAS; y++)
            for (x = 0; x < CANVAS; x++)
                if (s_canvas[y * CANVAS + x])
                    printf("%d %d %d\n", x - ORIGIN, y - ORIGIN, s_canvas[y * CANVAS + x]);
    }
    return 0;
}
