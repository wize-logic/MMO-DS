/* Where a log goes, and what is left behind by a crash. */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>

#include "platform.h"

static int failures;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) {                                                             \
            printf("  ok   %s\n", msg);                                         \
        } else {                                                                \
            printf("  FAIL %s\n", msg);                                         \
            failures++;                                                         \
        }                                                                       \
    } while (0)

/* Stands in for the engine's own fault handler: it exits with a status of its
 * choosing, which is what makes "did ours defer to it" answerable. */
static void pretend_engine(int sig, siginfo_t *si, void *ctx)
{
    (void)sig;
    (void)si;
    (void)ctx;
    _exit(42);
}

/* A directory of this suite's own, removed and remade, so a run says nothing
 * about the run before it. */
static void scratch(char *out, size_t cap)
{
    char base[512];

    if (mmo_plat_temp_dir(base, sizeof base) != 0)
        snprintf(base, sizeof base, "/tmp");
    snprintf(out, cap, "%s/openmmo-logdir-test-%u", base, mmo_plat_pid());
    mmo_plat_mkdir(out);
}

static int count_matching(const char *dir, const char *prefix)
{
    DIR *d = opendir(dir);
    struct dirent *de;
    int n = 0;

    if (d == NULL)
        return -1;
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, prefix, strlen(prefix)) == 0)
            n++;
    }
    closedir(d);
    return n;
}

static void remove_matching(const char *dir, const char *prefix)
{
    char path[1024 + 256];
    DIR *d = opendir(dir);
    struct dirent *de;

    if (d == NULL)
        return;
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, prefix, strlen(prefix)) == 0) {
            snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
            remove(path);
        }
    }
    closedir(d);
}

/*
 * $OPENMMO_LOGS is what the launcher hands its two children, so that three programs in three
 * directories cannot each resolve a folder of their own.
 */
static void test_told_where(const char *want)
{
    char got[1024], path[1024];
    struct stat st;

    CHECK(mmo_plat_log_dir(got, sizeof got) == 0 && strcmp(got, want) == 0,
          "$OPENMMO_LOGS is where the logs go");
    CHECK(stat(want, &st) == 0 && S_ISDIR(st.st_mode),
          "and the folder is made by the asking, not by the installer");
    CHECK(mmo_plat_log_path("openmmo-client.log", path, sizeof path) == 0 &&
          strncmp(path, want, strlen(want)) == 0 &&
          strcmp(path + strlen(path) - 18, "openmmo-client.log") == 0,
          "a named log is that folder plus the name");
}

/* The stamp is in the file name and the file name is what the pruning sorts
 * on, so the digits have to be widest-first and the separators have to go. */
static void test_stamp(void)
{
    char stamp[32];

    mmo_plat_stamp(stamp, sizeof stamp);
    CHECK(strlen(stamp) == 19 && stamp[4] == '-' && stamp[7] == '-' &&
          stamp[10] == ' ' && stamp[13] == ':' && stamp[16] == ':' &&
          stamp[0] == '2',
          "the wall clock reads YYYY-MM-DD HH:MM:SS and is not 1970");
}

/* The crash itself. */
static void test_crash_leaves_a_report(const char *dir)
{
    char path[1024 + 256], line[4096];
    pid_t kid;
    int status = 0, saw_reason = 0, saw_binary = 0, saw_frames = 0;
    int saw_base = 0;
    DIR *d;
    struct dirent *de;
    FILE *f;

    remove_matching(dir, "crash-");
    fflush(stdout);                 /* the child must not inherit a buffer it
                                     * will die without flushing */
    kid = fork();
    if (kid == 0) {
        mmo_plat_crash_install("suite");
        *(volatile int *)0 = 1;
        _exit(0);
    }
    CHECK(kid > 0 && waitpid(kid, &status, 0) == kid &&
          WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV,
          "a crash still kills the process the way it would have");
    CHECK(count_matching(dir, "crash-suite-") == 1,
          "and leaves exactly one report behind");

    path[0] = '\0';
    d = opendir(dir);
    if (d != NULL) {
        while ((de = readdir(d)) != NULL) {
            if (strncmp(de->d_name, "crash-suite-", 12) == 0)
                snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        }
        closedir(d);
    }
    f = path[0] != '\0' ? fopen(path, "rb") : NULL;
    while (f != NULL && fgets(line, sizeof line, f) != NULL) {
        if (strstr(line, "reason") != NULL && strstr(line, "SIGSEGV") != NULL)
            saw_reason = 1;
        if (strstr(line, "binary") != NULL && strchr(line, '/') != NULL)
            saw_binary = 1;
        if (strstr(line, "frames") != NULL && strstr(line, "0x") != NULL)
            saw_frames = 1;
        if (strstr(line, "base") != NULL && strstr(line, "0x") != NULL)
            saw_base = 1;
    }
    if (f != NULL)
        fclose(f);
    CHECK(saw_reason, "the report names the signal");
    CHECK(saw_binary, "and the binary to resolve the addresses against");
    CHECK(saw_frames, "and the return addresses above the fault");
    /* The frames have the load address taken off them and the fault address
     * does not, so without this the reader cannot tell which space either
     * number is in. It is 0 here and it is not on Windows. */
    CHECK(saw_base, "and where the program loaded, to read the two together");
}

/*
 * The game is the case this exists for and the case that nearly did not work: the engine
 * installs fault handlers of its own in main(), after the constructor this client arms from,
 * and its handler is the one that names the guest region and decides the exit status.
 */
static void test_chains_to_what_it_displaced(const char *dir)
{
    pid_t kid;
    int status = 0;

    remove_matching(dir, "crash-");
    fflush(stdout);
    kid = fork();
    if (kid == 0) {
        struct sigaction sa;

        memset(&sa, 0, sizeof sa);
        sa.sa_sigaction = pretend_engine;
        sa.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &sa, NULL);
        mmo_plat_crash_install("chain");
        *(volatile int *)0 = 1;
        _exit(0);
    }
    CHECK(kid > 0 && waitpid(kid, &status, 0) == kid &&
          WIFEXITED(status) && WEXITSTATUS(status) == 42,
          "the handler that was there still runs, and still decides how the "
          "process dies");
    CHECK(count_matching(dir, "crash-chain-") == 1,
          "and the report was written before it");
}

/*
 * A division by zero is not a crash on this hardware: the DS has no divide instruction, so the
 * cartridge's runtime answers it, and in the fused game the engine's SIGFPE handler repairs
 * the fault and lets the frame carry on.
 */
static void test_leaves_a_taken_sigfpe_alone(const char *dir)
{
    pid_t kid;
    int status = 0;

    remove_matching(dir, "crash-");
    fflush(stdout);
    kid = fork();
    if (kid == 0) {
        struct sigaction sa;

        memset(&sa, 0, sizeof sa);
        sa.sa_sigaction = pretend_engine;
        sa.sa_flags = SA_SIGINFO;
        sigaction(SIGFPE, &sa, NULL);
        mmo_plat_crash_install("fpe");
        raise(SIGFPE);
        _exit(0);
    }
    CHECK(kid > 0 && waitpid(kid, &status, 0) == kid &&
          WIFEXITED(status) && WEXITSTATUS(status) == 42,
          "a SIGFPE handler that was already there keeps the signal");
    CHECK(count_matching(dir, "crash-fpe-") == 0,
          "and no report is written about a fault it was going to repair");
}

/* The other half: with nobody holding it, a SIGFPE is a real one and gets the
 * same report and the same death as any other fault. */
static void test_takes_an_untaken_sigfpe(const char *dir)
{
    pid_t kid;
    int status = 0;

    remove_matching(dir, "crash-");
    fflush(stdout);
    kid = fork();
    if (kid == 0) {
        mmo_plat_crash_install("fpe");
        raise(SIGFPE);
        _exit(0);
    }
    CHECK(kid > 0 && waitpid(kid, &status, 0) == kid &&
          WIFSIGNALED(status) && WTERMSIG(status) == SIGFPE,
          "a SIGFPE nobody else wanted still kills the process");
    CHECK(count_matching(dir, "crash-fpe-") == 1,
          "and still leaves a report behind");
}

int logdir_tests_run(void)
{
    char dir[1024];
    const char *had = getenv("OPENMMO_LOGS");
    char keep[1024];

    printf("logdir:\n");
    failures = 0;
    keep[0] = '\0';
    if (had != NULL)
        snprintf(keep, sizeof keep, "%s", had);

    scratch(dir, sizeof dir);
    mmo_plat_setenv("OPENMMO_LOGS", dir, 1);
    test_told_where(dir);
    test_stamp();
    test_crash_leaves_a_report(dir);
    test_chains_to_what_it_displaced(dir);
    test_leaves_a_taken_sigfpe_alone(dir);
    test_takes_an_untaken_sigfpe(dir);

    remove_matching(dir, "crash-");
    remove_matching(dir, ".writable");
    rmdir(dir);
    if (keep[0] != '\0')
        mmo_plat_setenv("OPENMMO_LOGS", keep, 1);
    else
        mmo_plat_unsetenv("OPENMMO_LOGS");
    return failures;
}
