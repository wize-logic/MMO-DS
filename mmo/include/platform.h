/* The three things this client asks of the machine underneath. */
#ifndef MMO_PLATFORM_H
#define MMO_PLATFORM_H

#include <stddef.h>
#include <stdio.h>

/* The longest channel name any of this passes around, Local\ prefix and page
 * suffix included. Channels are "openmmo-<pid>" plus a short suffix. */
#define MMO_PLAT_NAME 160

/* ------------------------------------------------------------------ */
/* Shared pages                                                        */
/* ------------------------------------------------------------------ */

/*
 * One mapped page. `fd` is the POSIX descriptor and -1 elsewhere; `handle` is the Windows
 * section handle and NULL elsewhere.
 */
typedef struct {
    void  *addr;                  /* the mapping, NULL when there is none */
    size_t size;                  /* bytes the caller asked for */
    int    fd;                    /* POSIX descriptor, -1 elsewhere */
    void  *handle;                /* Windows section handle, NULL elsewhere */
    char   name[MMO_PLAT_NAME];   /* as the caller spelled it */
    int    inproc;                /* one-process page: slot + 1, else 0 */
} mmo_shm;

#define MMO_SHM_INIT { NULL, 0, -1, NULL, { 0 }, 0 }

/* ------------------------------------------------------------------ */
/* One process, one page                                               */
/* ------------------------------------------------------------------ */

/*
 * The desktop runs three programs meeting in a POSIX shared page. An Android app is one
 * process: there is no /dev/shm, a process is one ABI, and the window is a surface the
 * framework hands this program rather than another program's.
 */
void mmo_shm_one_process(int on);

/* Which way it is set, after the environment has been consulted. */
int mmo_shm_is_one_process(void);

/*
 * Publish: make the page (or take over one of this name that already exists) and map it read-
 * write.
 */
int mmo_shm_create(mmo_shm *m, const char *name, size_t size);

/*
 * Attach to a page somebody else published. `writable` maps it read-write, the window writes
 * keys back into the frame page, the launcher only reads the status one.
 */
int mmo_shm_attach(mmo_shm *m, const char *name, size_t size, int writable);

/*
 * Is the publisher's page the size this build expects? The magic and the version cannot answer
 * it: the frame page's pixel arrays grew once without a version bump, and a window that read
 * the old layout would have found keys and audio sitting inside the picture.
 */
int mmo_shm_size_matches(const mmo_shm *m, size_t want);

/* The publisher's object size, for the sentence a mismatch prints. Rounded up
 * to a page on Windows; 0 when it cannot be told. */
size_t mmo_shm_object_size(const mmo_shm *m);

/* 1 while somebody is publishing under this name. */
int mmo_shm_exists(const char *name);

/*
 * 1 once the name this mapping was attached under is gone, the session ended and the page was
 * taken down, while this mapping is still perfectly valid.
 */
int mmo_shm_orphaned(const mmo_shm *m);

/* Unmap and let go. Safe on a zeroed struct, and safe twice. */
void mmo_shm_close(mmo_shm *m);

/*
 * Take the name down so the next session does not find this one's page. POSIX needs it said;
 * on Windows the name went away with the last handle, which is what mmo_shm_close() already
 * did, so this is where that difference stops.
 */
void mmo_shm_unlink(const char *name);

/* ------------------------------------------------------------------ */
/* Child processes                                                     */
/* ------------------------------------------------------------------ */

/* One started program. Opaque: a pid on POSIX, a process handle on Windows. */
typedef struct mmo_proc mmo_proc;

/*
 * Start argv[0] with `argv`, with `env` (NAME=VALUE, NULL-terminated, may be NULL) laid OVER
 * this process's environment rather than replacing it, the game wants the session's own HOME
 * and loader paths, and a plan that composed the whole environment would be a second thing to
 * get right for no gain.
 */
mmo_proc *mmo_proc_spawn(char *const argv[], char *const env[],
                         const char *logpath);

/* The same, for the one child that identity-maps a console. */
mmo_proc *mmo_proc_spawn_guest(char *const argv[], char *const env[],
                               const char *logpath);

/* Has it finished? Never blocks. */
int mmo_proc_exited(mmo_proc *p, int *code);

/* Ask it to stop, SIGTERM, so whichever half is still up runs its own
 * shutdown. Does not wait. Safe from a signal handler. */
void mmo_proc_stop(mmo_proc *p);

/* Wait for it to end. *code as above; NULL if the status is not wanted. */
void mmo_proc_wait(mmo_proc *p, int *code);

/* Let go of the handle without waiting. */
void mmo_proc_free(mmo_proc *p);

/*
 * Tie every process started after this call to the life of this one, so a launcher that is
 * killed does not leave a game running with no window and no menu.
 */
void mmo_proc_bind_children(void);

/* ------------------------------------------------------------------ */
/* Where things are                                                    */
/* ------------------------------------------------------------------ */

/* This executable's full path. 0 on success, -1 when it cannot be told. */
int mmo_plat_exe_path(char *out, size_t cap);

/* Drop one variable from this process's environment. */
void mmo_plat_unsetenv(const char *name);

/* Become a fresh boot of this same program, the guest's logout. */
void mmo_plat_exec_self(void);

/* The player's home directory: $HOME, or the profile directory on Windows. */
int mmo_plat_home(char *out, size_t cap);

/* Where a program's own settings belong: $XDG_CONFIG_HOME or ~/.config,
 * %APPDATA% on Windows. No trailing separator. */
int mmo_plat_config_home(char *out, size_t cap);

/* Where a scratch file belongs: $TMPDIR or /tmp, %TEMP% on Windows. No
 * trailing separator. */
int mmo_plat_temp_dir(char *out, size_t cap);

/*
 * Where every log this client writes belongs: `logs/` beside the install, so a player who is
 * asked for one opens the folder they unpacked the game into and finds all of it in one place.
 */
int mmo_plat_log_dir(char *out, size_t cap);

/* The same, with one file name joined onto it. */
int mmo_plat_log_path(const char *name, char *out, size_t cap);

/*
 * The local wall clock as `YYYY-MM-DD HH:MM:SS`. Not time(): the fused binary links the DS
 * network library, whose own time() answers 0 for the whole run (see mmo_plat_seconds below),
 * so anything in the game that dated a file with it would date it 1970.
 */
void mmo_plat_stamp(char *out, size_t cap);

/*
 * Seconds since 1970-01-01 00:00:00 utc, from the operating system's clock. Not time(), for
 * the reason mmo_plat_stamp gives: in the fused binary that name is the DS network library's
 * and answers 0 for the whole run.
 */
long long mmo_plat_unix_time(void);

/*
 * Leave a crash report in the logs folder rather than dying silently: a signal on one host, an
 * unhandled structured exception on the other, and one file either way naming the reason, the
 * faulting address and the return addresses above it.
 */
int mmo_plat_crash_install(const char *program);

/*
 * Show `path` to the player in whatever this host calls its file manager, how the front door
 * hands over the logs folder rather than reciting a path at someone who then has to type it. 0
 * when something was opened.
 */
int mmo_plat_open_folder(const char *path);

/* The path separator this host writes, as a string: "/" or "\\". Every path
 * this client builds is read back by the same host, so one is enough. */
const char *mmo_plat_sep(void);

/* The last separator in `path`, or NULL. Reading a path apart is not the same
 * as writing one: Windows hands a program an argv[0] with backslashes in it
 * and accepts forward slashes everywhere else, so both count when splitting. */
const char *mmo_plat_last_sep(const char *path);

/* What a program is called here: "" or ".exe". The three programs find each
 * other by name beside the one that is running, so the name has to be the
 * host's. */
const char *mmo_plat_exe_suffix(void);

/*
 * Is there a program at `path` this host would run? Not access(X_OK): Windows has no execute
 * bit and rejects that mode outright, so asking it that way says "no" about every program on
 * the machine.
 */
int mmo_plat_is_executable(const char *path);

/* mkdir, with the mode POSIX wants and Windows has no use for. 0 on success,
 * and 0 when the directory is already there. */
int mmo_plat_mkdir(const char *path);

/* Rename `from` onto `to`, replacing `to` if it is already there. 0 or -1. */
int mmo_plat_rename_over(const char *from, const char *to);

/* Push everything written to `f` down to the disk itself. 0 or -1. */
int mmo_plat_fsync(FILE *f);

/* One holder at a time for a file two programs would otherwise both write. */
typedef struct mmo_plat_lock mmo_plat_lock;

int mmo_plat_lock_take(const char *path, mmo_plat_lock **out);
void mmo_plat_lock_drop(mmo_plat_lock *l);

/*
 * Create or truncate `path` so that only this user can read it, before anything is written to
 * it. 0 or -1.
 */
int mmo_plat_private_file(const char *path);

/* Set a variable in this process's environment, leaving an existing one alone
 * unless `overwrite`. What a child is given is mmo_proc_spawn()'s `env`; this
 * is for the few settings a program has to make for itself before it opens its
 * own window. */
int mmo_plat_setenv(const char *name, const char *value, int overwrite);

/*
 * What this host is called, in the spelling the update feed and the login stream both use:
 * "linux", "windows", "macos", or "" where it cannot be named. Decided at compile time, a
 * cross-built binary is for exactly one of them.
 */
const char *mmo_plat_os_name(void);

/*
 * This installation's own id, which the login stream hashes into its `hwid`. /etc/machine-id
 * (or dbus's copy) on Linux; the MachineGuid the Windows installer wrote, which is that idea's
 * opposite number there.
 */
size_t mmo_plat_machine_id(char *out, size_t cap);

/*
 * A face for the two windows' own text: DejaVu where a distribution ships it, the system UI
 * face on Windows. `bold` picks the heavier one.
 */
int mmo_plat_ui_font(int bold, char *out, size_t cap);

/*
 * The host's own "open a file" window, where the host has one. `title` names it, `start_dir`
 * is where it opens, and `filter_name`/`filter_glob` are the one kind it offers ("DS ROM",
 * "*.nds"); any of them may be NULL.
 */
int mmo_plat_pick_file(const char *title, const char *start_dir,
                       const char *filter_name, const char *filter_glob,
                       char *out, size_t cap);

/* The same window with the other question: where should this be written. */
int mmo_plat_pick_save_file(const char *title, const char *start_dir,
                            const char *suggest, const char *filter_name,
                            const char *filter_glob, char *out, size_t cap);

/*
 * Fill `buf` with entropy the operating system stands behind. 0 on success, -1 when the
 * machine will not supply it, the caller then fails loudly rather than proceeding with a
 * predictable key.
 */
int mmo_plat_random(void *buf, size_t n);

/* Sleep, in microseconds. */
void mmo_plat_sleep_us(unsigned us);

/*
 * This process's id, as the shared pages stamp it. It goes through here rather than being
 * called directly because on Windows a call with no prototype gets the cdecl symbol while the
 * import library exports the stdcall one, which is a link error if you are lucky.
 */
unsigned mmo_plat_pid(void);

/*
 * Seconds from an unspecified fixed point, counting forward and never backward. For pacing
 * something against the wall clock, how long since the last one, not what the date is.
 */
long mmo_plat_seconds(void);

/*
 * The same clock in nanoseconds, for the things a second is far too coarse for, a frame is
 * sixteen milliseconds and a display's refresh has to be predicted to a fraction of one.
 */
long long mmo_plat_mono_ns(void);

/*
 * The display'S own clock, and the reason it is in the platform layer rather than in the
 * window is that the two ends are in different files and, on the desktop, in different
 * Processes.
 */
void mmo_plat_vsync_mark(void);
int mmo_plat_vsync_next(long long *when_ns, long long *period_ns);

/*
 * ...And across two processes, which is the desktop and is where the cure above was doing
 * nothing at all.
 */
void mmo_plat_vsync_publish(const char *channel);
void mmo_plat_vsync_unpublish(void);

/* 1 while that id still belongs to a live process, how a page's publisher or
 * reader is noticed to have gone away without saying so. */
int mmo_plat_pid_alive(unsigned pid);

/*
 * The return addresses above the caller, for a trap that has to say where it was reached from.
 * Frame 0 is whoever called this.
 */
int mmo_plat_backtrace(void **frames, int max);

/* The last failure in this file, as text. Never NULL. */
const char *mmo_plat_error(void);

/*
 * Every reservation the process holds between two addresses, printed to stdout, and where the
 * host can say so, what backs each one.
 */
void mmo_plat_map_report(const char *why, unsigned long lo, unsigned long hi);

/*
 * The guest's address space, held from before the process has a C runtime and handed over the
 * moment the engine wants it.
 */
int mmo_plat_guest_release(void);

/* What the entry point managed, as text, for the line that reports it. */
const char *mmo_plat_guest_hold_status(void);

/*
 * Claim the guest's regions the way the engine will, say how it went, and let them go again.
 * Call it after mmo_plat_guest_release().
 */
int mmo_plat_guest_claim_test(char *out, size_t cap);

#endif /* MMO_PLATFORM_H */
