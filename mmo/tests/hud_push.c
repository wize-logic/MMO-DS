/* Push one HUD command into a running fused client's page. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hud_channel.h"

int main(int argc, char **argv)
{
    char name[160];
    struct stat st;
    struct openmmo_hud_shm *h;
    uint32_t kind;
    int32_t arg = 0;
    int fd;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <channel> export|<kind> [arg]\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[2], "export") == 0)
        kind = OPENMMO_HUD_CMD_EXPORT;
    else
        kind = (uint32_t)strtoul(argv[2], NULL, 10);
    if (argc > 3)
        arg = (int32_t)strtol(argv[3], NULL, 10);

    snprintf(name, sizeof name, "/%s%s", argv[1], OPENMMO_HUD_SUFFIX);
    fd = shm_open(name, O_RDWR, 0);
    if (fd < 0) {
        perror(name);
        return 1;
    }
    if (fstat(fd, &st) != 0 || (size_t)st.st_size != sizeof *h) {
        fprintf(stderr, "%s: %ld bytes, not the %zu of this build's page\n",
                name, (long)st.st_size, sizeof *h);
        close(fd);
        return 1;
    }
    h = mmap(NULL, sizeof *h, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (h == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    if (h->magic != OPENMMO_HUD_MAGIC || h->version != OPENMMO_HUD_VERSION) {
        fprintf(stderr, "%s: magic %08x version %u, wanted %08x version %u\n",
                name, h->magic, h->version, OPENMMO_HUD_MAGIC,
                OPENMMO_HUD_VERSION);
        munmap(h, sizeof *h);
        return 1;
    }
    openmmo_hud_push(h, kind, arg);
    printf("pushed command %u arg %d onto %s (head now %u)\n", kind, arg,
           name, h->cmd_head);
    munmap(h, sizeof *h);
    return 0;
}
