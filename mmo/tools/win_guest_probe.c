/* Is it this binary, or is it this machine? */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "platform.h"

/* The answer goes to a file, next to the exe and named after it. */
static void to_own_log(void)
{
    char exe[512], log[600], dir[512];

    if (mmo_plat_exe_path(exe, sizeof exe) == 0) {
        snprintf(log, sizeof log, "%s.log", exe);
        if (freopen(log, "a", stdout) != NULL) {
            fprintf(stderr, "guest-probe: writing %s\n", log);
            return;
        }
    }
    /* Beside the exe is where a person will look for it, but a folder is not
     * always writable. Temp always is, and it is where the game's own log
     * already lives, so the fallback lands somewhere familiar. */
    if (mmo_plat_temp_dir(dir, sizeof dir) == 0) {
        snprintf(log, sizeof log, "%s%sopenmmo-guest-probe.log", dir,
                 mmo_plat_sep());
        if (freopen(log, "a", stdout) != NULL)
            fprintf(stderr, "guest-probe: writing %s\n", log);
    }
}

/*
 * Parent mode spawns this same EXE with --child, through the spawn the launcher uses for the
 * game: suspended, regions reserved across the process boundary, then resumed.
 */
static int parent(const char *self)
{
    char log[600], exe[512];
    char *av[3];
    mmo_proc *ch;
    int code = -1, i;

    if (mmo_plat_exe_path(exe, sizeof exe) != 0)
        snprintf(exe, sizeof exe, "%s", self);
    snprintf(log, sizeof log, "%s.log", exe);

    av[0] = exe;
    av[1] = (char *)"--child";
    av[2] = NULL;

    fprintf(stderr, "guest-probe: starting a child with the regions reserved\n");
    ch = mmo_proc_spawn_guest(av, NULL, log);
    if (ch == NULL) {
        fprintf(stderr, "guest-probe: could not start the child: %s\n",
                mmo_plat_error());
        return 1;
    }
    for (i = 0; i < 200 && !mmo_proc_exited(ch, &code); i++)
        mmo_plat_sleep_us(50000);
    fprintf(stderr, "guest-probe: the child answered in %s\n", log);
    return 0;
}

int main(int argc, char **argv)
{
    time_t now = time(NULL);
    char when[64], verdict[192];

    if (argc < 2 || strcmp(argv[1], "--child") != 0)
        return parent(argv[0]);

    to_own_log();
    strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", localtime(&now));
    printf("\n==== guest-probe %s ====\n", when);

    /* Captured at the entry point, long before this line: the status is a
     * static string filled in before the C runtime existed, so redirecting
     * stdout above cannot have lost it. */
    printf("guest-probe: at the entry point, %s\n",
           mmo_plat_guest_hold_status());
    mmo_plat_guest_release();
    printf("guest-probe: after the handover, %s\n",
           mmo_plat_guest_hold_status());
    if (mmo_plat_guest_claim_test(verdict, sizeof verdict) == 0) {
        printf("guest-probe: %s\n", verdict);
        printf("guest-probe: done\n");
        fflush(stdout);
        return 0;
    }
    /* Only the failing run needs the map. A run that claimed everything has
     * nothing to explain, and 70 lines of address space in the log of a
     * success is 70 lines nobody reads. */
    printf("guest-probe: %s\n", verdict);
    mmo_plat_map_report("what is in the console's address range:",
                        0x00010000UL, 0x0B000000UL);
    printf("guest-probe: done\n");
    fflush(stdout);
    return 1;
}
