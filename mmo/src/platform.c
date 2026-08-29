/* The one place this client knows which machine it is on. */

/* dl_iterate_phdr, which is how the POSIX half asks where this program was
 * loaded (crash_image_base). glibc hides it behind this and nothing else in
 * here wants a gnu variant of anything. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include "platform.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char plat_err[256];

const char *mmo_plat_error(void)
{
    return plat_err[0] != '\0' ? plat_err : "no error";
}

/* Copy `src` into `out` and say whether it fitted. */
static int put(char *out, size_t cap, const char *src)
{
    size_t n;

    if (out == NULL || cap == 0 || src == NULL || src[0] == '\0')
        return -1;
    n = strlen(src);
    if (n >= cap)
        return -1;
    memcpy(out, src, n + 1);
    return 0;
}


/* ------------------------------------------------------------------ */
/* A crash, written down                                               */
/* ------------------------------------------------------------------ */

/*
 * Three programs die three ways and a player can only be asked for a folder, so all three
 * leave the same file behind: `logs/crash-<program>-<when>.log`, beside the session trace that
 * led up to it.
 */
static char crash_file[1024];      /* where the report goes; empty until armed */
static char crash_prog[64];
static char crash_exe[512];
static char crash_when[32];        /* wall clock at install, for the header */
static long crash_started;         /* mmo_plat_seconds() at install */
/* Where this program was loaded. */
static unsigned long crash_base;

static unsigned long crash_image_base(void);

static void crash_report(const char *reason, unsigned long code,
                         unsigned long at, void **frames, int depth);
static void crash_emit(const char *buf, size_t n);
static int  crash_arm(void);

/* ------------------------------------------------------------------ */
/* One process, one page                                               */
/* ------------------------------------------------------------------ */
/*
 * platform.h says why this exists. What is below is the whole of it: a name, an allocation,
 * and a count of how many handles are looking at it.
 */
#define SHM_INPROC_MAX 8

static struct {
    char   name[MMO_PLAT_NAME];
    void  *addr;
    size_t size;
    int    refs;      /* live mmo_shm handles pointing at this allocation */
    int    unlinked;  /* the name is gone; the memory is not */
} g_inproc[SHM_INPROC_MAX];

static int g_one_process = -1;    /* -1 until the environment is consulted */

void mmo_shm_one_process(int on)
{
    if (on >= 0) {
        g_one_process = on ? 1 : 0;
        return;
    }
    g_one_process = -1;
    (void)mmo_shm_is_one_process();
}

int mmo_shm_is_one_process(void)
{
    if (g_one_process < 0) {
        const char *e = getenv("OPENMMO_ONE_PROCESS");

#if defined(__ANDROID__)
        /* Not a default so much as the only thing there is: an app is one
         * process and bionic has no POSIX shared memory to offer a second
         * one. The environment can still turn it off, which is a way to
         * watch every page call fail loudly rather than a supported mode. */
        g_one_process = (e != NULL && e[0] == '0') ? 0 : 1;
#else
        g_one_process = (e != NULL && e[0] != '\0' && e[0] != '0') ? 1 : 0;
#endif
    }
    return g_one_process;
}

/* The row this name is published under, or -1. An unlinked row is not found
 * by name, the name is what went away, but the handles already on it keep
 * working, which is the whole point of the two lives. */
static int inproc_find(const char *name)
{
    int i;

    if (name == NULL || name[0] == '\0')
        return -1;
    for (i = 0; i < SHM_INPROC_MAX; i++) {
        if (g_inproc[i].addr != NULL && !g_inproc[i].unlinked &&
            strcmp(g_inproc[i].name, name) == 0)
            return i;
    }
    return -1;
}

/* The row a handle is on, or -1. */
static int inproc_slot(const mmo_shm *m)
{
    if (m == NULL || m->inproc <= 0 || m->inproc > SHM_INPROC_MAX)
        return -1;
    return m->inproc - 1;
}

static void inproc_release(int slot)
{
    if (slot < 0 || g_inproc[slot].addr == NULL)
        return;
    if (--g_inproc[slot].refs > 0)
        return;
    free(g_inproc[slot].addr);
    memset(&g_inproc[slot], 0, sizeof g_inproc[slot]);
}

static int inproc_create(mmo_shm *m, const char *name, size_t size)
{
    int slot = inproc_find(name), i;

    memset(m, 0, sizeof *m);
    m->fd = -1;
    /* Taking over a page of this name that already exists is what the POSIX
     * create does (O_CREAT without O_EXCL), and a relaunched session leans on
     * it. Same memory, one more handle, but only if it is the same size,
     * because a caller that asked for more would write past the end of it. */
    if (slot >= 0) {
        if (g_inproc[slot].size != size) {
            snprintf(plat_err, sizeof plat_err,
                     "one-process page '%s' is %lu bytes, not %lu", name,
                     (unsigned long)g_inproc[slot].size, (unsigned long)size);
            return -1;
        }
        g_inproc[slot].refs++;
    } else {
        for (i = 0; i < SHM_INPROC_MAX; i++) {
            if (g_inproc[i].addr == NULL)
                break;
        }
        if (i == SHM_INPROC_MAX) {
            snprintf(plat_err, sizeof plat_err,
                     "no room for a one-process page named '%s'", name);
            return -1;
        }
        g_inproc[i].addr = calloc(1, size);
        if (g_inproc[i].addr == NULL) {
            snprintf(plat_err, sizeof plat_err,
                     "one-process page '%s' would not allocate", name);
            return -1;
        }
        g_inproc[i].size = size;
        g_inproc[i].refs = 1;
        g_inproc[i].unlinked = 0;
        put(g_inproc[i].name, sizeof g_inproc[i].name, name);
        slot = i;
    }
    m->addr = g_inproc[slot].addr;
    m->size = size;
    m->inproc = slot + 1;
    put(m->name, sizeof m->name, name);
    return 0;
}

#if defined(__ANDROID__)
/* The engine'S own pages, found by NAME from here. */
extern void *pc_view_local_page(const char *name, size_t *size)
    __attribute__((weak));

static int inproc_adopt_engine_page(const char *name)
{
    void *map;
    size_t have = 0;
    int i;

    if (pc_view_local_page == NULL)
        return -1;
    map = pc_view_local_page(name, &have);
    if (map == NULL)
        return -1;
    for (i = 0; i < SHM_INPROC_MAX; i++) {
        if (g_inproc[i].addr == NULL)
            break;
    }
    if (i == SHM_INPROC_MAX)
        return -1;
    g_inproc[i].addr = map;
    g_inproc[i].size = have;
    /* One extra reference, held by nobody: the engine owns this memory and
     * the last close must not free() an mmap the engine is still writing. */
    g_inproc[i].refs = 1;
    g_inproc[i].unlinked = 0;
    put(g_inproc[i].name, sizeof g_inproc[i].name, name);
    return i;
}
#endif

static int inproc_attach(mmo_shm *m, const char *name, size_t size)
{
    int slot = inproc_find(name);

    memset(m, 0, sizeof *m);
    m->fd = -1;
#if defined(__ANDROID__)
    if (slot < 0)
        slot = inproc_adopt_engine_page(name);
#endif
    if (slot < 0)
        return -1;                /* nobody is publishing: a wait, not an error */
    g_inproc[slot].refs++;
    m->addr = g_inproc[slot].addr;
    m->size = size;
    m->inproc = slot + 1;
    put(m->name, sizeof m->name, name);
    return 0;
}

/* ---- the public surface, which is one of the two worlds per call ---- */

static size_t shm_os_object_size(const mmo_shm *m);
static int    shm_os_size_matches(const mmo_shm *m, size_t want);
static int    shm_os_create(mmo_shm *m, const char *name, size_t size);
static int    shm_os_attach(mmo_shm *m, const char *name, size_t size, int writable);
static int    shm_os_exists(const char *name);
static int    shm_os_orphaned(const mmo_shm *m);
static void   shm_os_close(mmo_shm *m);
static void   shm_os_unlink(const char *name);

size_t mmo_shm_object_size(const mmo_shm *m)
{
    int slot = inproc_slot(m);

    if (slot >= 0)
        return g_inproc[slot].size;
    return shm_os_object_size(m);
}

int mmo_shm_size_matches(const mmo_shm *m, size_t want)
{
    if (inproc_slot(m) >= 0)
        return mmo_shm_object_size(m) == want;
    return shm_os_size_matches(m, want);
}

int mmo_shm_create(mmo_shm *m, const char *name, size_t size)
{
    if (mmo_shm_is_one_process())
        return inproc_create(m, name, size);
    return shm_os_create(m, name, size);
}

int mmo_shm_attach(mmo_shm *m, const char *name, size_t size, int writable)
{
    /*
     * `writable` has no meaning here and is not a lie by omission: one process writing its own
     * memory cannot be told not to by a page table it shares with itself.
     */
    (void)writable;
    if (mmo_shm_is_one_process())
        return inproc_attach(m, name, size);
    return shm_os_attach(m, name, size, writable);
}

int mmo_shm_exists(const char *name)
{
    if (mmo_shm_is_one_process())
        return inproc_find(name) >= 0;
    return shm_os_exists(name);
}

int mmo_shm_orphaned(const mmo_shm *m)
{
    int slot = inproc_slot(m);

    if (slot >= 0)
        return g_inproc[slot].unlinked;
    return shm_os_orphaned(m);
}

void mmo_shm_close(mmo_shm *m)
{
    int slot = inproc_slot(m);

    if (slot >= 0) {
        inproc_release(slot);
        memset(m, 0, sizeof *m);
        m->fd = -1;
        return;
    }
    shm_os_close(m);
}

void mmo_shm_unlink(const char *name)
{
    int slot;

    if (!mmo_shm_is_one_process()) {
        shm_os_unlink(name);
        return;
    }
    slot = inproc_find(name);
    if (slot < 0)
        return;
    g_inproc[slot].unlinked = 1;
    /* The name went; the memory goes with the last handle. A publisher that
     * unlinks and then closes is the ordinary path and frees it on the close. */
    if (g_inproc[slot].refs <= 0) {
        free(g_inproc[slot].addr);
        memset(&g_inproc[slot], 0, sizeof g_inproc[slot]);
    }
}

#if defined(_WIN32)

/* ================================================================== */
/* Windows                                                             */
/* ================================================================== */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#include <signal.h>

static void fail(const char *what)
{
    snprintf(plat_err, sizeof plat_err, "%s failed, error %lu", what,
             (unsigned long)GetLastError());
}

/*
 * A POSIX shared-object name becomes a section in the session's own namespace. Local\ rather
 * than Global\ on purpose: two players logged into one machine get a channel each, and neither
 * needs the privilege the global namespace asks for.
 */
static void local_name(const char *name, char *out, size_t cap)
{
    while (*name == '/')
        name++;
    snprintf(out, cap, "Local\\%s", name);
}

/* The section's size, as Windows can tell it: the mapped region rounded up to
 * a page. 0 when there is nothing mapped. */
static size_t shm_os_object_size(const mmo_shm *m)
{
    MEMORY_BASIC_INFORMATION mbi;

    if (m == NULL || m->addr == NULL)
        return 0;
    if (VirtualQuery(m->addr, &mbi, sizeof mbi) == 0)
        return 0;
    return (size_t)mbi.RegionSize;
}

static int shm_os_size_matches(const mmo_shm *m, size_t want)
{
    SYSTEM_INFO si;
    size_t have = shm_os_object_size(m), page, rounded;

    if (have == 0)
        return 1;                 /* cannot be told; not a refusal */
    GetSystemInfo(&si);
    page = si.dwPageSize ? (size_t)si.dwPageSize : 4096u;
    rounded = (want + page - 1) & ~(page - 1);
    return have == rounded;
}

static int shm_os_create(mmo_shm *m, const char *name, size_t size)
{
    char local[MMO_PLAT_NAME];
    HANDLE h;
    void *view;

    memset(m, 0, sizeof *m);
    m->fd = -1;
    local_name(name, local, sizeof local);

    h = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                           (DWORD)size, local);
    if (h == NULL) {
        fail("CreateFileMapping");
        return -1;
    }
    /* Mapping the whole section rather than `size` of it: an existing section
     * Smaller than this build expects is exactly the mismatch
     * mmo_shm_size_matches() is for, and a view clipped to our own idea of the
     * size would hide it until the first read past the end. */
    view = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (view == NULL) {
        fail("MapViewOfFile");
        CloseHandle(h);
        return -1;
    }
    m->addr = view;
    m->size = size;
    m->handle = (void *)h;
    put(m->name, sizeof m->name, name);
    return 0;
}

static int shm_os_attach(mmo_shm *m, const char *name, size_t size, int writable)
{
    char local[MMO_PLAT_NAME];
    DWORD access = writable ? FILE_MAP_ALL_ACCESS : FILE_MAP_READ;
    HANDLE h;
    void *view;

    memset(m, 0, sizeof *m);
    m->fd = -1;
    local_name(name, local, sizeof local);

    h = OpenFileMappingA(access, FALSE, local);
    if (h == NULL) {
        fail("OpenFileMapping");
        return -1;
    }
    view = MapViewOfFile(h, access, 0, 0, 0);
    if (view == NULL) {
        fail("MapViewOfFile");
        CloseHandle(h);
        return -1;
    }
    m->addr = view;
    m->size = size;
    m->handle = (void *)h;
    put(m->name, sizeof m->name, name);
    return 0;
}

static int shm_os_exists(const char *name)
{
    char local[MMO_PLAT_NAME];
    HANDLE h;

    local_name(name, local, sizeof local);
    h = OpenFileMappingA(FILE_MAP_READ, FALSE, local);
    if (h == NULL)
        return 0;
    CloseHandle(h);
    return 1;
}

static int shm_os_orphaned(const mmo_shm *m)
{
    if (m == NULL || m->addr == NULL || m->name[0] == '\0')
        return 0;
    return !mmo_shm_exists(m->name);
}

static void shm_os_close(mmo_shm *m)
{
    if (m == NULL)
        return;
    if (m->addr != NULL)
        UnmapViewOfFile(m->addr);
    if (m->handle != NULL)
        CloseHandle((HANDLE)m->handle);
    memset(m, 0, sizeof *m);
    m->fd = -1;
}

static void shm_os_unlink(const char *name)
{
    (void)name;   /* the name went away with the last handle */
}

/* ------------------------------------------------------------------ */
/* Child processes                                                     */
/* ------------------------------------------------------------------ */

struct mmo_proc {
    HANDLE h;
    DWORD  id;
    int    code;
    int    done;
};

/*
 * One argument, quoted the way every Windows crt re-splits it: wrapped when it holds a space
 * or a quote, with interior quotes and the backslashes before them escaped.
 */
static void quote_arg(const char *a, char *out, size_t cap, size_t *used)
{
    size_t n = 0;
    int need = (a[0] == '\0');
    const char *p;

    for (p = a; *p != '\0'; p++)
        if (*p == ' ' || *p == '\t' || *p == '"')
            need = 1;

#define PUT(ch) do { if (n + 1 < cap) out[n] = (ch); n++; } while (0)
    if (!need) {
        for (p = a; *p != '\0'; p++)
            PUT(*p);
        out[n < cap ? n : cap - 1] = '\0';
        *used = n;
        return;
    }
    PUT('"');
    for (p = a; *p != '\0'; p++) {
        size_t slashes = 0;

        while (*p == '\\') { slashes++; p++; }
        if (*p == '\0') {
            /* Backslashes before the closing quote are doubled. */
            for (; slashes > 0; slashes--) { PUT('\\'); PUT('\\'); }
            break;
        }
        if (*p == '"') {
            for (; slashes > 0; slashes--) { PUT('\\'); PUT('\\'); }
            PUT('\\');
            PUT('"');
        } else {
            for (; slashes > 0; slashes--) PUT('\\');
            PUT(*p);
        }
    }
    PUT('"');
#undef PUT
    out[n < cap ? n : cap - 1] = '\0';
    *used = n;
}

/* The parent's environment with `env`'s NAME=VALUE entries laid over it, as
 * the doubly-NUL-terminated block CreateProcess takes. NULL when there is
 * nothing to override, which tells CreateProcess to inherit as-is. */
static char *env_block(char *const env[])
{
    char *parent, *p, *block;
    size_t cap = 4096, len = 0;
    int i;

    if (env == NULL || env[0] == NULL)
        return NULL;
    parent = GetEnvironmentStringsA();
    if (parent == NULL)
        return NULL;

    block = malloc(cap);
    if (block == NULL) {
        FreeEnvironmentStringsA(parent);
        return NULL;
    }

#define APPEND(s, n) do {                                    \
        while (len + (n) + 2 > cap) {                        \
            char *bigger = realloc(block, cap * 2);          \
            if (bigger == NULL) {                            \
                free(block);                                 \
                FreeEnvironmentStringsA(parent);             \
                return NULL;                                 \
            }                                                \
            block = bigger; cap *= 2;                        \
        }                                                    \
        memcpy(block + len, (s), (n));                       \
        len += (n);                                          \
        block[len++] = '\0';                                 \
    } while (0)

    for (p = parent; *p != '\0'; p += strlen(p) + 1) {
        const char *eq = strchr(p, '=');
        size_t namelen = eq != NULL ? (size_t)(eq - p) : strlen(p);
        int shadowed = 0;

        for (i = 0; env[i] != NULL; i++) {
            const char *oeq = strchr(env[i], '=');
            size_t olen = oeq != NULL ? (size_t)(oeq - env[i]) : strlen(env[i]);

            if (olen == namelen && _strnicmp(env[i], p, namelen) == 0) {
                shadowed = 1;
                break;
            }
        }
        if (!shadowed)
            APPEND(p, strlen(p));
    }
    for (i = 0; env[i] != NULL; i++)
        APPEND(env[i], strlen(env[i]));
    block[len++] = '\0';           /* the block's own terminator */
#undef APPEND

    FreeEnvironmentStringsA(parent);
    return block;
}

/* Everything this launcher starts belongs to one job, so a launcher that is
 * killed takes the game and the window with it. The POSIX half does the same
 * thing by hand, in a signal handler, because there is nothing to hold them. */
static HANDLE job_of_children;

void mmo_proc_bind_children(void)
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim;

    if (job_of_children != NULL)
        return;
    job_of_children = CreateJobObjectA(NULL, NULL);
    if (job_of_children == NULL)
        return;
    memset(&lim, 0, sizeof lim);
    lim.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job_of_children,
                                 JobObjectExtendedLimitInformation,
                                 &lim, sizeof lim)) {
        CloseHandle(job_of_children);
        job_of_children = NULL;
    }
}

/* The pe entry point. Everything about this is about being early. */
struct guest_region {
    unsigned long base;
    unsigned long size;
    const char *name;
};

static const struct guest_region guest_regions[] = {
    { 0x01FF8000UL, 0x00008000UL, "ITCM" },
    { 0x02000000UL, 0x00400000UL, "main RAM" },
    { 0x027E0000UL, 0x00020000UL, "shared work" },
    { 0x02A00000UL, 0x00200000UL, "port window" },
    { 0x03000000UL, 0x00010000UL, "WRAM" },
    { 0x037F8000UL, 0x00018000UL, "ARM7 WRAM" },
    { 0x04000000UL, 0x00100000UL, "I/O" },
    { 0x05000000UL, 0x00001000UL, "palette" },
    { 0x07000000UL, 0x00001000UL, "OAM" },
    /*
     * Not in armrec's fixed table, and just as fixed. VRAM is claimed by armrec_vram_init()
     * right after that table, in one piece; the GBA slot is pc_main.c's, because CTRDG_Init
     * probes it at boot whether or not a cartridge is modelled.
     */
    { 0x06000000UL, 0x01000000UL, "VRAM" },
    { 0x08000000UL, 0x02010000UL, "GBA slot" },
};

#define GUEST_N ((int)(sizeof guest_regions / sizeof guest_regions[0]))

static void *guest_held[GUEST_N];
static int guest_entry_held = -1;
static char guest_status[256] = "the entry point did not run";

void __attribute__((noreturn)) openmmo_pe_start(void);
extern void mainCRTStartup(void);

void __attribute__((noreturn)) openmmo_pe_start(void)
{
    SYSTEM_INFO si;
    unsigned long gran;
    char lost[160];
    int i, held = 0, nlost = 0;

    GetSystemInfo(&si);
    gran = (unsigned long)si.dwAllocationGranularity;
    if (gran == 0)
        gran = 0x10000UL;
    lost[0] = '\0';

    for (i = 0; i < GUEST_N; i++) {
        /* A reservation is rounded down to the allocation granularity, so
         * asking for an address inside a granule is asking for the granule,
         * ITCM at 0x01FF8000 is half of one. Round both ends out and ask for
         * what will actually be taken, rather than being handed it. */
        unsigned long lo = guest_regions[i].base & ~(gran - 1);
        unsigned long hi = (guest_regions[i].base + guest_regions[i].size
                            + gran - 1) & ~(gran - 1);

        guest_held[i] = VirtualAlloc((LPVOID)(uintptr_t)lo, (SIZE_T)(hi - lo),
                                     MEM_RESERVE, PAGE_NOACCESS);
        if (guest_held[i] != NULL) {
            held++;
        } else if (nlost < 3) {
            /* Named, because which region was lost is the whole diagnosis: a
             * region taken this early is taken by the loader or by something
             * injected into the process, and no amount of being earlier wins
             * against either. */
            if (nlost > 0)
                lstrcatA(lost, ", ");
            lstrcatA(lost, guest_regions[i].name);
            nlost++;
        }
    }

    guest_entry_held = held;
    if (held == GUEST_N)
        wsprintfA(guest_status, "held all %d guest regions", GUEST_N);
    else
        wsprintfA(guest_status,
                  "held %d of %d; %s was already taken before the runtime"
                  " started (error %lu)", held, GUEST_N, lost,
                  (unsigned long)GetLastError());

    mainCRTStartup();
    /* mainCRTStartup calls exit(); this is for the compiler, not the CPU. */
    for (;;) { }
}

int mmo_plat_guest_release(void)
{
    SYSTEM_INFO si;
    unsigned long gran;
    int i, freed = 0;

    GetSystemInfo(&si);
    gran = (unsigned long)si.dwAllocationGranularity;
    if (gran == 0)
        gran = 0x10000UL;

    for (i = 0; i < GUEST_N; i++) {
        /*
         * Whoever reserved it. The entry point above may have, or the parent may have done it
         * through VirtualAllocEx while this process was still suspended, in which case there
         * is no pointer here to free, only an address that both sides compute the same way.
         */
        void *at = guest_held[i] != NULL
                 ? guest_held[i]
                 : (void *)(uintptr_t)(guest_regions[i].base & ~(gran - 1));

        if (VirtualFree(at, 0, MEM_RELEASE))
            freed++;
        guest_held[i] = NULL;
    }

    /* Say what was actually true, now that it can be known. */
    if (freed == GUEST_N) {
        if (guest_entry_held == GUEST_N)
            wsprintfA(guest_status,
                      "the entry point held all %d regions and handed them over",
                      GUEST_N);
        else if (guest_entry_held <= 0)
            wsprintfA(guest_status,
                      "the parent held all %d regions for this process and it"
                      " took them over", GUEST_N);
        else
            wsprintfA(guest_status,
                      "all %d regions handed over, %d held here, the rest by"
                      " the parent", GUEST_N, guest_entry_held);
    } else {
        wsprintfA(guest_status,
                  "only %d of %d regions were held; the rest were never ours"
                  " to hand over", freed, GUEST_N);
    }
    return freed > 0 ? 0 : -1;
}

const char *mmo_plat_guest_hold_status(void)
{
    return guest_status;
}

int mmo_plat_guest_claim_test(char *out, size_t cap)
{
    char lost[192];
    int i, got = 0, nlost = 0;
    void *p[GUEST_N];

    lost[0] = '\0';
    for (i = 0; i < GUEST_N; i++) {
        /* MEM_RESERVE | MEM_COMMIT at the region's own base and size, which is
         * armrec_mem_init()'s call with armrec's arguments. Anything weaker
         * would pass where the engine fails. */
        p[i] = VirtualAlloc((LPVOID)(uintptr_t)guest_regions[i].base,
                            (SIZE_T)guest_regions[i].size,
                            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (p[i] != NULL) {
            got++;
        } else if (nlost < 4) {
            if (nlost > 0)
                lstrcatA(lost, ", ");
            lstrcatA(lost, guest_regions[i].name);
            nlost++;
        }
    }
    for (i = 0; i < GUEST_N; i++)
        if (p[i] != NULL)
            VirtualFree(p[i], 0, MEM_RELEASE);

    if (got == GUEST_N)
        snprintf(out, cap, "claimed all %d regions, the game would boot here",
                 GUEST_N);
    else
        snprintf(out, cap, "claimed %d of %d; %s refused, the game dies here",
                 got, GUEST_N, lost);
    return got == GUEST_N ? 0 : -1;
}

/* RESERVE the console'S addresses inside a child that has not started yet. */
static int reserve_in_child(HANDLE proc, char *note, size_t ncap)
{
    SYSTEM_INFO si;
    unsigned long gran;
    char lost[160];
    int i, held = 0, nlost = 0;

    GetSystemInfo(&si);
    gran = (unsigned long)si.dwAllocationGranularity;
    if (gran == 0)
        gran = 0x10000UL;
    lost[0] = '\0';

    for (i = 0; i < GUEST_N; i++) {
        unsigned long lo = guest_regions[i].base & ~(gran - 1);
        unsigned long hi = (guest_regions[i].base + guest_regions[i].size
                            + gran - 1) & ~(gran - 1);

        if (VirtualAllocEx(proc, (LPVOID)(uintptr_t)lo, (SIZE_T)(hi - lo),
                           MEM_RESERVE, PAGE_NOACCESS) != NULL) {
            held++;
        } else if (nlost < 3) {
            if (nlost > 0)
                lstrcatA(lost, ", ");
            lstrcatA(lost, guest_regions[i].name);
            nlost++;
        }
    }

    if (held == GUEST_N)
        snprintf(note, ncap,
                 "guest: all %d regions reserved in the child before it ran\r\n",
                 GUEST_N);
    else
        snprintf(note, ncap,
                 "guest: %d of %d reserved in the child before it ran;"
                 " %s was already taken at process creation\r\n",
                 held, GUEST_N, lost);
    return held;
}

static mmo_proc *spawn_common(char *const argv[], char *const env[],
                              const char *logpath, int guest)
{
    char cmdline[4096];
    size_t len = 0;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    SECURITY_ATTRIBUTES sa;
    HANDLE log = INVALID_HANDLE_VALUE;
    char *block;
    mmo_proc *p;
    int i;

    for (i = 0; argv[i] != NULL; i++) {
        size_t used = 0;

        if (i > 0 && len + 1 < sizeof cmdline)
            cmdline[len++] = ' ';
        quote_arg(argv[i], cmdline + len, sizeof cmdline - len, &used);
        len += used;
        if (len >= sizeof cmdline) {
            snprintf(plat_err, sizeof plat_err, "the command line is too long");
            return NULL;
        }
    }
    cmdline[len] = '\0';

    memset(&sa, 0, sizeof sa);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;

    if (logpath != NULL) {
        log = CreateFileA(logpath, FILE_APPEND_DATA,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                          OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (log != INVALID_HANDLE_VALUE) {
            time_t now = time(NULL);
            struct tm *tm = localtime(&now);
            char head[96];
            DWORD wrote = 0;

            if (tm != NULL) {
                snprintf(head, sizeof head,
                         "\r\n==== session %04d-%02d-%02d %02d:%02d:%02d ====\r\n",
                         tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                         tm->tm_hour, tm->tm_min, tm->tm_sec);
                WriteFile(log, head, (DWORD)strlen(head), &wrote, NULL);
            }
        }
    }

    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (log != INVALID_HANDLE_VALUE) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = log;
        si.hStdError = log;
    }
    memset(&pi, 0, sizeof pi);

    block = env_block(env);
    /*
     * Suspended when the child needs the console's addresses, so that the reservation below
     * happens before the child has loaded anything that could be sitting on them.
     */
    if (!CreateProcessA(argv[0], cmdline, NULL, NULL, TRUE,
                        guest ? CREATE_SUSPENDED : 0, block, NULL, &si, &pi)) {
        fail("CreateProcess");
        free(block);
        if (log != INVALID_HANDLE_VALUE)
            CloseHandle(log);
        return NULL;
    }
    free(block);

    if (guest) {
        char note[256];
        DWORD wrote = 0;

        reserve_in_child(pi.hProcess, note, sizeof note);
        /* Into the child's own log, because that is where the rest of this
         * boot is written and where anyone reading a failure will look. The
         * parent is a window with no terminal; a printf here goes nowhere. */
        if (log != INVALID_HANDLE_VALUE)
            WriteFile(log, note, (DWORD)strlen(note), &wrote, NULL);
        ResumeThread(pi.hThread);
    }

    if (log != INVALID_HANDLE_VALUE)
        CloseHandle(log);
    CloseHandle(pi.hThread);

    if (job_of_children != NULL)
        AssignProcessToJobObject(job_of_children, pi.hProcess);

    p = calloc(1, sizeof *p);
    if (p == NULL) {
        CloseHandle(pi.hProcess);
        snprintf(plat_err, sizeof plat_err, "out of memory");
        return NULL;
    }
    p->h = pi.hProcess;
    p->id = pi.dwProcessId;
    p->code = -1;
    return p;
}

mmo_proc *mmo_proc_spawn(char *const argv[], char *const env[],
                         const char *logpath)
{
    return spawn_common(argv, env, logpath, 0);
}

mmo_proc *mmo_proc_spawn_guest(char *const argv[], char *const env[],
                               const char *logpath)
{
    return spawn_common(argv, env, logpath, 1);
}

int mmo_proc_exited(mmo_proc *p, int *code)
{
    DWORD st = 0;

    if (p == NULL)
        return 0;
    if (!p->done) {
        if (WaitForSingleObject(p->h, 0) != WAIT_OBJECT_0)
            return 0;
        p->done = 1;
        p->code = GetExitCodeProcess(p->h, &st) ? (int)st : -1;
    }
    if (code != NULL)
        *code = p->code;
    return 1;
}

/*
 * There is no SIGTERM here. A Windows process is asked to stop through its own window, and
 * neither of the two this launcher starts has one it would answer on, the game presents
 * through a page and the window is SDL.
 */
void mmo_proc_stop(mmo_proc *p)
{
    if (p == NULL || p->done)
        return;
    TerminateProcess(p->h, 1);
}

void mmo_proc_wait(mmo_proc *p, int *code)
{
    DWORD st = 0;

    if (p == NULL)
        return;
    if (!p->done) {
        WaitForSingleObject(p->h, INFINITE);
        p->done = 1;
        p->code = GetExitCodeProcess(p->h, &st) ? (int)st : -1;
    }
    if (code != NULL)
        *code = p->code;
}

void mmo_proc_free(mmo_proc *p)
{
    if (p == NULL)
        return;
    if (p->h != NULL)
        CloseHandle(p->h);
    free(p);
}

/* ------------------------------------------------------------------ */
/* Where things are                                                    */
/* ------------------------------------------------------------------ */

int mmo_plat_exe_path(char *out, size_t cap)
{
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)cap);

    if (n == 0 || n >= cap)
        return -1;
    return 0;
}

int mmo_plat_home(char *out, size_t cap)
{
    const char *up = getenv("USERPROFILE");
    const char *drive, *path;
    int n;

    if (up != NULL && up[0] != '\0')
        return put(out, cap, up);
    drive = getenv("HOMEDRIVE");
    path = getenv("HOMEPATH");
    if (drive == NULL || path == NULL)
        return -1;
    n = snprintf(out, cap, "%s%s", drive, path);
    return (n < 0 || (size_t)n >= cap) ? -1 : 0;
}

int mmo_plat_config_home(char *out, size_t cap)
{
    const char *appdata = getenv("APPDATA");

    if (appdata != NULL && appdata[0] != '\0')
        return put(out, cap, appdata);
    return mmo_plat_home(out, cap);
}

int mmo_plat_temp_dir(char *out, size_t cap)
{
    DWORD n = GetTempPathA((DWORD)cap, out);

    if (n == 0 || n >= cap)
        return -1;
    while (n > 1 && (out[n - 1] == '\\' || out[n - 1] == '/'))
        out[--n] = '\0';
    return 0;
}

const char *mmo_plat_sep(void)
{
    return "\\";
}

const char *mmo_plat_last_sep(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *back = strrchr(path, '\\');

    if (back != NULL && (slash == NULL || back > slash))
        return back;
    return slash;
}

const char *mmo_plat_exe_suffix(void)
{
    return ".exe";
}

int mmo_plat_is_executable(const char *path)
{
    DWORD a = GetFileAttributesA(path);

    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

int mmo_plat_mkdir(const char *path)
{
    if (CreateDirectoryA(path, NULL))
        return 0;
    return GetLastError() == ERROR_ALREADY_EXISTS ? 0 : -1;
}

/* MoveFileEx rather than remove-then-rename: it replaces in one step, so a
 * player who loses power between the two still has the old file rather than
 * neither. rename() is the call that cannot do this here (platform.h). */
int mmo_plat_rename_over(const char *from, const char *to)
{
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
}

/* The profile is already the owner's here, so there is no mode to set: this
 * only has to leave an empty file behind for the caller to write. */
int mmo_plat_private_file(const char *path)
{
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);

    if (h == INVALID_HANDLE_VALUE)
        return -1;
    CloseHandle(h);
    return 0;
}

const char *mmo_plat_os_name(void)
{
    return "windows";
}

/*
 * The MachineGuid the Windows installer wrote: one value, per installation, stable across
 * reboots and reinstalls of everything but Windows itself. That is what /etc/machine-id is, so
 * it is what this hashes.
 */
size_t mmo_plat_machine_id(char *out, size_t cap)
{
    /*
     * advapi32 is opened by hand, for the same reason sockets.c opens ws2_32 that way: a named
     * import is loaded before main(), and this archive is linked into a game whose first act
     * is claiming the console's identity-mapped addresses.
     */
    typedef LONG (WINAPI *open_fn)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
    typedef LONG (WINAPI *query_fn)(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE,
                                    LPDWORD);
    typedef LONG (WINAPI *close_fn)(HKEY);
    open_fn reg_open;
    query_fn reg_query;
    close_fn reg_close;
    HMODULE lib;
    HKEY key;
    DWORD type = 0, len = (DWORD)cap;
    LONG rc;

    if (out == NULL || cap == 0)
        return 0;
    lib = LoadLibraryA("advapi32.dll");
    if (lib == NULL)
        return 0;
    *(FARPROC *)&reg_open = GetProcAddress(lib, "RegOpenKeyExA");
    *(FARPROC *)&reg_query = GetProcAddress(lib, "RegQueryValueExA");
    *(FARPROC *)&reg_close = GetProcAddress(lib, "RegCloseKey");
    if (reg_open == NULL || reg_query == NULL || reg_close == NULL)
        return 0;

    rc = reg_open(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", 0,
                  KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key);
    if (rc != ERROR_SUCCESS)
        return 0;
    rc = reg_query(key, "MachineGuid", NULL, &type, (LPBYTE)out, &len);
    reg_close(key);
    if (rc != ERROR_SUCCESS || type != REG_SZ || len == 0)
        return 0;
    out[len < cap ? len : cap - 1] = '\0';
    while (len > 0 && (unsigned char)out[len - 1] <= ' ')
        out[--len] = '\0';
    return (size_t)len;
}

int mmo_plat_setenv(const char *name, const char *value, int overwrite)
{
    char buf[1024];

    if (!overwrite) {
        const char *had = getenv(name);

        if (had != NULL && had[0] != '\0')
            return 0;
    }
    /* SetEnvironmentVariable moves the process block that CreateProcess reads;
     * _putenv_s moves the crt's copy that getenv reads. This client does both,
     * so a setting made here is seen by our own getenv and by a child. */
    if (!SetEnvironmentVariableA(name, value))
        return -1;
    snprintf(buf, sizeof buf, "%s=%s", name, value);
    return _putenv(buf) == 0 ? 0 : -1;
}

/* RtlGenRandom, which advapi32 exports under its ordinal name SystemFunction036. */
int mmo_plat_random(void *buf, size_t n)
{
    typedef BOOLEAN (WINAPI *rand_fn)(PVOID, ULONG);
    static rand_fn gen;
    static int tried;

    if (!tried) {
        HMODULE lib = LoadLibraryA("advapi32.dll");

        tried = 1;
        if (lib != NULL)
            *(FARPROC *)&gen = GetProcAddress(lib, "SystemFunction036");
    }
    if (gen == NULL)
        return -1;
    return gen(buf, (ULONG)n) ? 0 : -1;
}

void mmo_plat_sleep_us(unsigned us)
{
    Sleep(us < 1000u ? 1u : us / 1000u);
}

unsigned mmo_plat_pid(void)
{
    return (unsigned)GetCurrentProcessId();
}

long mmo_plat_seconds(void)
{
    /*
     * QueryPerformanceCounter rather than GetTickCount64: the 64-bit tick arrived with Vista,
     * and importing it is a loader refusal on XP before main() runs, while the performance
     * counter has been there since Windows 2000.
     */
    static LONGLONG freq;
    LARGE_INTEGER n;

    if (freq == 0) {
        LARGE_INTEGER f;

        QueryPerformanceFrequency(&f);
        freq = f.QuadPart != 0 ? f.QuadPart : 1;
    }
    QueryPerformanceCounter(&n);
    return (long)(n.QuadPart / freq);
}

/* The ebp chain, walked here rather than by ntdll. */
int mmo_plat_backtrace(void **frames, int max)
{
#if defined(__i386__)
    uintptr_t base  = (uintptr_t)__readfsdword(0x04);  /* TIB StackBase  */
    uintptr_t limit = (uintptr_t)__readfsdword(0x08);  /* TIB StackLimit */
    uintptr_t fp = (uintptr_t)__builtin_frame_address(0);
    int n = 0;

    if (max <= 0 || base <= limit)
        return 0;
    while (n < max) {
        uintptr_t next, ret;

        /* Both words have to be inside the stack, and a frame pointer is
         * aligned. This is the whole of the safety: past it, the two reads
         * cannot fault. */
        if (fp < limit || fp > base - 2 * sizeof(uintptr_t) ||
            (fp & (sizeof(uintptr_t) - 1)) != 0)
            break;
        next = ((const uintptr_t *)fp)[0];
        ret  = ((const uintptr_t *)fp)[1];
        if (ret == 0)
            break;
        frames[n++] = (void *)ret;
        /* A chain climbs. Anything else is a frame that was not one, and
         * following it is how a walk loops forever. */
        if (next <= fp)
            break;
        fp = next;
    }
    return n;
#else
    return (int)RtlCaptureStackBackTrace(1, (ULONG)max, frames, NULL);
#endif
}

int mmo_plat_pid_alive(unsigned pid)
{
    HANDLE h;
    DWORD st = 0;
    int alive;

    if (pid == 0)
        return 0;
    h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (h == NULL)
        return 0;
    alive = GetExitCodeProcess(h, &st) && st == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
}

/*
 * MEM_MAPPED says a section is there and nothing about whose it is. GetMappedFileName answers
 * that, in device form (\\Device\\HarddiskVolume3\\...), which is not a dos path but does name
 * the file, and naming it is the point.
 */
static void map_backing(LPVOID base, char *out, size_t cap)
{
    HMODULE lib;
    DWORD (WINAPI *p_get)(HANDLE, LPVOID, LPSTR, DWORD);

    out[0] = '\0';
    lib = LoadLibraryA("psapi.dll");
    if (lib == NULL)
        return;
    *(FARPROC *)&p_get = GetProcAddress(lib, "GetMappedFileNameA");
    if (p_get != NULL && p_get(GetCurrentProcess(), base, out, (DWORD)cap) == 0)
        out[0] = '\0';
    FreeLibrary(lib);
}


void mmo_plat_map_report(const char *why, unsigned long lo, unsigned long hi)
{
    SYSTEM_INFO si;
    MEMORY_BASIC_INFORMATION mbi;
    char file[MAX_PATH];
    unsigned long at;

    GetSystemInfo(&si);
    printf("map-report: %s\n", why != NULL ? why : "address space");
    printf("map-report: page 0x%lX granularity 0x%lX, %08lX..%08lX\n",
           (unsigned long)si.dwPageSize,
           (unsigned long)si.dwAllocationGranularity, lo, hi);

    for (at = lo; at < hi; ) {
        if (VirtualQuery((LPCVOID)(uintptr_t)at, &mbi, sizeof mbi) != sizeof mbi)
            break;
        /* Free space is the answer we hope for and says nothing; every other
         * state is a competitor for the address and is printed. */
        if (mbi.State != MEM_FREE) {
            const char *kind = mbi.Type == MEM_IMAGE   ? "image"
                             : mbi.Type == MEM_MAPPED  ? "mapped"
                             : mbi.Type == MEM_PRIVATE ? "private"
                                                       : "?";

            file[0] = '\0';
            if (mbi.Type == MEM_MAPPED || mbi.Type == MEM_IMAGE)
                map_backing(mbi.AllocationBase, file, sizeof file);
            printf("map-report:   %08lX +%08lX alloc=%08lX %-7s %s%s%s\n",
                   (unsigned long)(uintptr_t)mbi.BaseAddress,
                   (unsigned long)mbi.RegionSize,
                   (unsigned long)(uintptr_t)mbi.AllocationBase,
                   kind,
                   mbi.State == MEM_COMMIT ? "committed" : "reserved",
                   file[0] != '\0' ? "  " : "", file);
        }
        if (mbi.RegionSize == 0)
            break;
        at = (unsigned long)((uintptr_t)mbi.BaseAddress + mbi.RegionSize);
    }
    fflush(stdout);
}

void mmo_plat_unsetenv(const char *name)
{
    char buf[256];

    if (name == NULL || name[0] == '\0')
        return;
    /* msvcrt's way of removing a variable: an empty assignment. */
    snprintf(buf, sizeof buf, "%s=", name);
    _putenv(buf);
}

void mmo_plat_exec_self(void)
{
    char exe[512];
    const char *argv2[2];

    if (mmo_plat_exe_path(exe, sizeof exe) != 0)
        return;
    argv2[0] = exe;
    argv2[1] = NULL;
    /* There is no exec on Windows: spawn the fresh copy and leave. */
    if (_spawnv(_P_NOWAIT, exe, argv2) != -1)
        exit(0);
}


void mmo_plat_stamp(char *out, size_t cap)
{
    SYSTEMTIME t;

    if (out == NULL || cap == 0)
        return;
    GetLocalTime(&t);
    snprintf(out, cap, "%04u-%02u-%02u %02u:%02u:%02u",
             (unsigned)t.wYear, (unsigned)t.wMonth, (unsigned)t.wDay,
             (unsigned)t.wHour, (unsigned)t.wMinute, (unsigned)t.wSecond);
}

/* Appended and closed each time: the file is opened for the first time by the
 * crash that writes it, so a run that ends well leaves nothing behind. */
static void crash_emit(const char *buf, size_t n)
{
    HANDLE h;
    DWORD wrote = 0;

    if (crash_file[0] == '\0')
        return;
    h = CreateFileA(crash_file, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    WriteFile(h, buf, (DWORD)n, &wrote, NULL);
    CloseHandle(h);
}

/* The ones with a name worth printing. Everything else is its own number,
 * which is what a person searching for it has anyway. */
static const char *crash_exception_name(unsigned long code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:      return "access violation";
    case EXCEPTION_STACK_OVERFLOW:        return "stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION:   return "illegal instruction";
    case EXCEPTION_PRIV_INSTRUCTION:      return "privileged instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "integer divide by zero";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return "float divide by zero";
    case EXCEPTION_IN_PAGE_ERROR:         return "in-page error";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "misaligned access";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
    case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "noncontinuable exception";
    default:                              return "exception";
    }
}

static LPTOP_LEVEL_EXCEPTION_FILTER crash_prev;

/*
 * The filter runs on the faulting thread and on its stack, so the frames captured here are the
 * ones that led to the fault with the dispatcher's own on top of them.
 */
static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep)
{
    static volatile LONG inside;
    void *frames[24];
    unsigned long code = 0, at = 0;
    int depth;

    /*
     * The POSIX handler's guard, which this side wanted and did not have. A filter is not the
     * end of the road: it hands the exception on, and a handler further down the chain may
     * resume at the faulting instruction rather than end the process.
     */
    if (inside)
        return EXCEPTION_EXECUTE_HANDLER;
    inside = 1;

    if (ep != NULL && ep->ExceptionRecord != NULL) {
        code = (unsigned long)ep->ExceptionRecord->ExceptionCode;
        at = (unsigned long)(uintptr_t)ep->ExceptionRecord->ExceptionAddress;
    }
    depth = mmo_plat_backtrace(frames, (int)(sizeof frames / sizeof frames[0]));
    crash_report(crash_exception_name(code), code, at, frames, depth);
    if (crash_prev != NULL)
        return crash_prev(ep);
    return EXCEPTION_EXECUTE_HANDLER;
}

/* abort() and the assertions above it never reach the filter, the crt turns
 * them into a signal and stops. This is the same report from that path. */
static void crash_crt_signal(int sig)
{
    void *frames[24];
    int depth = mmo_plat_backtrace(frames,
                                   (int)(sizeof frames / sizeof frames[0]));

    crash_report(sig == SIGABRT ? "abort" : "signal", (unsigned long)sig, 0,
                 frames, depth);
    signal(sig, SIG_DFL);
    raise(sig);
}

/* The player's own file manager, on the folder we want them to look in. */
int mmo_plat_open_folder(const char *path)
{
    typedef HINSTANCE (WINAPI *shell_exec)(HWND, LPCSTR, LPCSTR, LPCSTR,
                                           LPCSTR, INT);
    shell_exec p_exec;
    HMODULE lib;
    int ok;

    if (path == NULL || path[0] == '\0')
        return -1;
    lib = LoadLibraryA("shell32.dll");
    if (lib == NULL)
        return -1;
    *(FARPROC *)&p_exec = GetProcAddress(lib, "ShellExecuteA");
    ok = p_exec != NULL &&
         (uintptr_t)p_exec(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL) > 32;
    FreeLibrary(lib);
    return ok ? 0 : -1;
}

/* A module handle is the load address on this host. */
static unsigned long crash_image_base(void)
{
    return (unsigned long)(uintptr_t)GetModuleHandleA(NULL);
}

static int crash_arm(void)
{
    crash_prev = SetUnhandledExceptionFilter(crash_filter);
    signal(SIGABRT, crash_crt_signal);
    return 0;
}

#else /* !_WIN32 */

/* ================================================================== */
/* POSIX                                                               */
/* ================================================================== */

#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <link.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void fail(const char *what)
{
    snprintf(plat_err, sizeof plat_err, "%s: %s", what, strerror(errno));
}

static size_t shm_os_object_size(const mmo_shm *m)
{
    struct stat st;

    if (m == NULL || m->fd < 0 || fstat(m->fd, &st) != 0)
        return 0;
    return (size_t)st.st_size;
}

static int shm_os_size_matches(const mmo_shm *m, size_t want)
{
    size_t have = shm_os_object_size(m);

    return have == 0 || have == want;
}

/*
 * bionic has no POSIX SHARED MEMORY, no shm_open, no shm_unlink, and that is not a gap to
 * work around.
 */
#if defined(__ANDROID__)

static int shm_os_create(mmo_shm *m, const char *name, size_t size)
{
    (void)name; (void)size;
    if (m != NULL) { memset(m, 0, sizeof *m); m->fd = -1; }
    snprintf(plat_err, sizeof plat_err,
             "this host has no POSIX shared memory; the page is in-process here");
    return -1;
}

static int shm_os_attach(mmo_shm *m, const char *name, size_t size, int writable)
{
    (void)name; (void)size; (void)writable;
    if (m != NULL) { memset(m, 0, sizeof *m); m->fd = -1; }
    snprintf(plat_err, sizeof plat_err,
             "this host has no POSIX shared memory; the page is in-process here");
    return -1;
}

static int  shm_os_exists(const char *name)     { (void)name; return 0; }
static int  shm_os_orphaned(const mmo_shm *m)   { (void)m;    return 0; }
static void shm_os_unlink(const char *name)     { (void)name; }

static void shm_os_close(mmo_shm *m)
{
    if (m == NULL)
        return;
    if (m->addr != NULL)
        munmap(m->addr, m->size);
    if (m->fd >= 0)
        close(m->fd);
    memset(m, 0, sizeof *m);
    m->fd = -1;
}

#else

static int shm_os_create(mmo_shm *m, const char *name, size_t size)
{
    void *p;
    int fd;

    memset(m, 0, sizeof *m);
    m->fd = -1;

    fd = shm_open(name, O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        fail("shm_open");
        return -1;
    }
    if (ftruncate(fd, (off_t)size) != 0) {
        fail("ftruncate");
        close(fd);
        return -1;
    }
    p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        fail("mmap");
        close(fd);
        return -1;
    }
    m->addr = p;
    m->size = size;
    m->fd = fd;
    put(m->name, sizeof m->name, name);
    return 0;
}

static int shm_os_attach(mmo_shm *m, const char *name, size_t size, int writable)
{
    int prot = writable ? (PROT_READ | PROT_WRITE) : PROT_READ;
    void *p;
    int fd;

    memset(m, 0, sizeof *m);
    m->fd = -1;

    fd = shm_open(name, writable ? O_RDWR : O_RDONLY, 0600);
    if (fd < 0) {
        fail("shm_open");
        return -1;
    }
    p = mmap(NULL, size, prot, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        fail("mmap");
        close(fd);
        return -1;
    }
    m->addr = p;
    m->size = size;
    m->fd = fd;
    put(m->name, sizeof m->name, name);
    return 0;
}

static int shm_os_exists(const char *name)
{
    int fd = shm_open(name, O_RDONLY, 0600);

    if (fd < 0)
        return 0;
    close(fd);
    return 1;
}

static int shm_os_orphaned(const mmo_shm *m)
{
    struct stat st;

    if (m == NULL || m->fd < 0 || fstat(m->fd, &st) != 0)
        return 0;
    return st.st_nlink == 0;
}

static void shm_os_close(mmo_shm *m)
{
    if (m == NULL)
        return;
    if (m->addr != NULL)
        munmap(m->addr, m->size);
    if (m->fd >= 0)
        close(m->fd);
    memset(m, 0, sizeof *m);
    m->fd = -1;
}

static void shm_os_unlink(const char *name)
{
    if (name != NULL && name[0] != '\0')
        shm_unlink(name);
}

#endif /* __ANDROID__ */

/* ------------------------------------------------------------------ */
/* Child processes                                                     */
/* ------------------------------------------------------------------ */

struct mmo_proc {
    pid_t pid;
    int   code;
    int   done;
};

mmo_proc *mmo_proc_spawn(char *const argv[], char *const env[],
                         const char *logpath)
{
    mmo_proc *p = calloc(1, sizeof *p);
    pid_t pid;

    if (p == NULL) {
        snprintf(plat_err, sizeof plat_err, "out of memory");
        return NULL;
    }
    p->code = -1;

    pid = fork();
    if (pid < 0) {
        fail("fork");
        free(p);
        return NULL;
    }
    if (pid == 0) {
        int i;

        if (logpath != NULL) {
            int fd = open(logpath, O_WRONLY | O_CREAT | O_APPEND, 0644);

            if (fd >= 0) {
                time_t now = time(NULL);
                char head[96];
                struct tm tm;

                localtime_r(&now, &tm);
                snprintf(head, sizeof head,
                         "\n==== session %04d-%02d-%02d %02d:%02d:%02d ====\n",
                         tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                         tm.tm_hour, tm.tm_min, tm.tm_sec);
                (void)!write(fd, head, strlen(head));
                dup2(fd, STDOUT_FILENO);
                dup2(fd, STDERR_FILENO);
                if (fd > STDERR_FILENO)
                    close(fd);
            }
        }
        for (i = 0; env != NULL && env[i] != NULL; i++) {
            char *eq = strchr(env[i], '=');

            if (eq == NULL)
                continue;
            *eq = '\0';
            setenv(env[i], eq + 1, 1);
            *eq = '=';
        }
        execv(argv[0], argv);
        fprintf(stderr, "openmmo-launch: cannot run %s: %s\n", argv[0],
                strerror(errno));
        _exit(127);
    }
    p->pid = pid;
    return p;
}


/* One spelling on this host. mmap places from the top of the address space
 * down, so the console's addresses are free when the engine asks for them and
 * there is nothing to defend them from, the Windows half is where the two
 * differ, and this is what keeps that difference out of the caller. */
mmo_proc *mmo_proc_spawn_guest(char *const argv[], char *const env[],
                               const char *logpath)
{
    return mmo_proc_spawn(argv, env, logpath);
}

int mmo_proc_exited(mmo_proc *p, int *code)
{
    int status = 0;

    if (p == NULL)
        return 0;
    if (!p->done) {
        if (waitpid(p->pid, &status, WNOHANG) != p->pid)
            return 0;
        p->done = 1;
        p->code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    if (code != NULL)
        *code = p->code;
    return 1;
}

void mmo_proc_stop(mmo_proc *p)
{
    if (p == NULL || p->done || p->pid <= 0)
        return;
    kill(p->pid, SIGTERM);
}

void mmo_proc_wait(mmo_proc *p, int *code)
{
    int status = 0;

    if (p == NULL)
        return;
    if (!p->done) {
        if (waitpid(p->pid, &status, 0) == p->pid) {
            p->done = 1;
            p->code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        }
    }
    if (code != NULL)
        *code = p->code;
}

void mmo_proc_free(mmo_proc *p)
{
    free(p);
}

void mmo_proc_bind_children(void)
{
    /* Nothing holds a POSIX child but its parent's own hand; the launcher's
     * signal handler is that hand. */
}

/* ------------------------------------------------------------------ */
/* Where things are                                                    */
/* ------------------------------------------------------------------ */

int mmo_plat_exe_path(char *out, size_t cap)
{
    ssize_t n = readlink("/proc/self/exe", out, cap - 1);

    if (n <= 0)
        return -1;
    out[n] = '\0';
    return 0;
}

int mmo_plat_home(char *out, size_t cap)
{
    return put(out, cap, getenv("HOME"));
}

int mmo_plat_config_home(char *out, size_t cap)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    char home[512];
    int n;

    if (xdg != NULL && xdg[0] != '\0')
        return put(out, cap, xdg);
    if (mmo_plat_home(home, sizeof home) != 0)
        return -1;
    n = snprintf(out, cap, "%s/.config", home);
    return (n < 0 || (size_t)n >= cap) ? -1 : 0;
}

int mmo_plat_temp_dir(char *out, size_t cap)
{
    const char *tmp = getenv("TMPDIR");

    return put(out, cap, (tmp != NULL && tmp[0] != '\0') ? tmp : "/tmp");
}

const char *mmo_plat_sep(void)
{
    return "/";
}

const char *mmo_plat_last_sep(const char *path)
{
    return strrchr(path, '/');
}

const char *mmo_plat_exe_suffix(void)
{
    return "";
}

int mmo_plat_is_executable(const char *path)
{
    return access(path, X_OK) == 0;
}

int mmo_plat_mkdir(const char *path)
{
    if (mkdir(path, 0755) == 0 || errno == EEXIST)
        return 0;
    return -1;
}

/* Already what rename() means here, which is the whole reason the other half
 * of this file needed a function to say it. */
int mmo_plat_rename_over(const char *from, const char *to)
{
    return rename(from, to) == 0 ? 0 : -1;
}

/* 0600 at creation rather than a chmod afterwards, so there is no moment when
 * the file exists and anyone else can read it. */
int mmo_plat_private_file(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);

    if (fd < 0)
        return -1;
    /* An existing file keeps the mode it was made with, so say it again for
     * one written by an older build that did not ask. */
    if (fchmod(fd, 0600) != 0) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

const char *mmo_plat_os_name(void)
{
#if defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "";
#endif
}

/* A file, its trailing whitespace cut. dbus's copy is where systemd's is
 * absent. */
static size_t read_id_file(const char *path, char *out, size_t cap)
{
    FILE *f = fopen(path, "rb");
    size_t n;

    if (f == NULL)
        return 0;
    n = fread(out, 1, cap - 1, f);
    fclose(f);
    while (n > 0 && (unsigned char)out[n - 1] <= ' ')
        n--;
    out[n] = '\0';
    return n;
}

size_t mmo_plat_machine_id(char *out, size_t cap)
{
    size_t n;

    if (out == NULL || cap < 2)
        return 0;
    n = read_id_file("/etc/machine-id", out, cap);
    if (n == 0)
        n = read_id_file("/var/lib/dbus/machine-id", out, cap);
    return n;
}

int mmo_plat_setenv(const char *name, const char *value, int overwrite)
{
    return setenv(name, value, overwrite);
}

int mmo_plat_random(void *buf, size_t n)
{
    FILE *f = fopen("/dev/urandom", "rb");
    size_t got;

    if (f == NULL)
        return -1;
    got = fread(buf, 1, n, f);
    fclose(f);
    return got == n ? 0 : -1;
}

void mmo_plat_sleep_us(unsigned us)
{
    usleep(us);
}

unsigned mmo_plat_pid(void)
{
    return (unsigned)getpid();
}

long mmo_plat_seconds(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (long)ts.tv_sec;
}

/* One frame deeper than glibc reports, so both hosts agree that frame 0 is
 * whoever called this and not this function itself: RtlCaptureStackBackTrace
 * is asked to skip its own caller, and here the same frame is dropped. */
#if defined(__ANDROID__)

/*
 * bionic has no backtrace(): there is no <execinfo.h> on this libc. What it does have is the
 * unwinder itself, which is what glibc's backtrace() is a wrapper over, so this is the same
 * walk with the wrapper written out.
 */
#include <unwind.h>

struct bt_state {
    void **frames;
    int    max;
    int    n;
    int    skip;     /* this function's own frame, so frame 0 is the caller */
};

static _Unwind_Reason_Code bt_step(struct _Unwind_Context *ctx, void *arg)
{
    struct bt_state *st = arg;
    uintptr_t pc = (uintptr_t)_Unwind_GetIP(ctx);

    if (pc == 0)
        return _URC_END_OF_STACK;
    if (st->skip > 0) {
        st->skip--;
        return _URC_NO_REASON;
    }
    if (st->n >= st->max)
        return _URC_END_OF_STACK;
    st->frames[st->n++] = (void *)pc;
    return _URC_NO_REASON;
}

int mmo_plat_backtrace(void **frames, int max)
{
    struct bt_state st;

    if (frames == NULL || max <= 0)
        return 0;
    st.frames = frames;
    st.max = max;
    st.n = 0;
    st.skip = 1;              /* drop this function, as the glibc half does */
    _Unwind_Backtrace(bt_step, &st);
    return st.n;
}

#else

int mmo_plat_backtrace(void **frames, int max)
{
    void *raw[64];
    int n, i;

    if (max <= 0)
        return 0;
    if (max > (int)(sizeof raw / sizeof raw[0]) - 1)
        max = (int)(sizeof raw / sizeof raw[0]) - 1;
    n = backtrace(raw, max + 1);
    if (n <= 1)
        return 0;
    for (i = 1; i < n; i++)
        frames[i - 1] = raw[i];
    return n - 1;
}

#endif /* __ANDROID__ */

int mmo_plat_pid_alive(unsigned pid)
{
    if (pid == 0)
        return 0;
    return kill((pid_t)pid, 0) == 0 || errno == EPERM;
}

/*
 * The same question, asked of /proc/self/maps, which already answers it in the form the
 * Windows half has to assemble: a range, what it is, and the file behind it.
 */
/* Nothing to hold. mmap places from the top of the address space down, so the
 * console's addresses are free when the engine asks for them and have been on
 * every Linux boot this client has ever had. Saying so costs two functions and
 * keeps the caller free of the question. */
int mmo_plat_guest_release(void)
{
    return 0;
}

const char *mmo_plat_guest_hold_status(void)
{
    return "not needed on this host";
}

int mmo_plat_guest_claim_test(char *out, size_t cap)
{
    /* MAP_FIXED_NOREPLACE, which is armrec's own call on this host: it fails
     * rather than moving the mapping somewhere else, so a refusal here is the
     * same refusal the engine would get. */
    int i, got = 0, n = 0, nlost = 0;
    void *p[11];
    char lost[192];
    static const struct { unsigned long base, size; const char *name; } r[11] = {
        { 0x01FF8000UL, 0x00008000UL, "ITCM" },
        { 0x02000000UL, 0x00400000UL, "main RAM" },
        { 0x027E0000UL, 0x00020000UL, "shared work" },
        { 0x02A00000UL, 0x00200000UL, "port window" },
        { 0x03000000UL, 0x00010000UL, "WRAM" },
        { 0x037F8000UL, 0x00018000UL, "ARM7 WRAM" },
        { 0x04000000UL, 0x00100000UL, "I/O" },
        { 0x05000000UL, 0x00001000UL, "palette" },
        { 0x07000000UL, 0x00001000UL, "OAM" },
        { 0x06000000UL, 0x01000000UL, "VRAM" },
        { 0x08000000UL, 0x02010000UL, "GBA slot" },
    };

    lost[0] = '\0';

    n = (int)(sizeof r / sizeof r[0]);
    for (i = 0; i < n; i++) {
        p[i] = mmap((void *)(uintptr_t)r[i].base, (size_t)r[i].size,
                    PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (p[i] == (void *)(uintptr_t)r[i].base) {
            got++;
            continue;
        }
        if (p[i] != MAP_FAILED)
            munmap(p[i], (size_t)r[i].size);
        if (nlost < 4) {
            if (nlost > 0)
                strncat(lost, ", ", sizeof lost - strlen(lost) - 1);
            strncat(lost, r[i].name, sizeof lost - strlen(lost) - 1);
            nlost++;
        }
    }
    for (i = 0; i < n; i++)
        if (p[i] == (void *)(uintptr_t)r[i].base)
            munmap(p[i], (size_t)r[i].size);

    /*
     * Named, not counted. A -m32 ELF of this program loads at 0x08048000, which is inside the
     * GBA slot, so a probe built for this host reports that one region lost to its own image,
     * true, expected, and completely misleading as a bare number.
     */
    if (got == n)
        snprintf(out, cap, "claimed all %d regions", n);
    else
        snprintf(out, cap, "claimed %d of %d; %s refused", got, n, lost);
    return got == n ? 0 : -1;
}

void mmo_plat_map_report(const char *why, unsigned long lo, unsigned long hi)
{
    FILE *f;
    char line[512];

    printf("map-report: %s\n", why != NULL ? why : "address space");
    printf("map-report: page %ld, %08lX..%08lX\n", sysconf(_SC_PAGESIZE), lo, hi);

    f = fopen("/proc/self/maps", "r");
    if (f == NULL) {
        printf("map-report:   (no /proc/self/maps)\n");
        fflush(stdout);
        return;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        unsigned long a = 0, b = 0;

        if (sscanf(line, "%lx-%lx", &a, &b) != 2)
            continue;
        if (b <= lo || a >= hi)
            continue;
        line[strcspn(line, "\n")] = '\0';
        printf("map-report:   %s\n", line);
    }
    fclose(f);
    fflush(stdout);
}

void mmo_plat_unsetenv(const char *name)
{
    if (name != NULL && name[0] != '\0')
        unsetenv(name);
}

void mmo_plat_exec_self(void)
{
    char exe[512];
    char *argv2[2];

    if (mmo_plat_exe_path(exe, sizeof exe) != 0)
        return;
    argv2[0] = exe;
    argv2[1] = NULL;
    /* exec keeps the environment and skips atexit, so the shm pages are
     * neither unlinked nor recreated, shm_open(O_CREAT) without O_EXCL
     * reuses the same objects and an attached window rides through into
     * the fresh boot's frames. */
    execv(exe, argv2);
}


void mmo_plat_stamp(char *out, size_t cap)
{
    struct timespec ts;
    struct tm tm;

    if (out == NULL || cap == 0)
        return;
    out[0] = '\0';
    /* CLOCK_REALTIME rather than time(): the DS network library the fused
     * binary links defines a time() of its own that answers 0 for the whole
     * run (platform.h says so about mmo_plat_seconds), and a crash report
     * stamped 1970 is a crash report nobody can line up with a session. */
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        return;
    if (localtime_r(&ts.tv_sec, &tm) == NULL)
        return;
    snprintf(out, cap, "%04d-%02d-%02d %02d:%02d:%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
}

/* open/write/close and nothing else: every call here is on the list of what a
 * signal handler may do, which stdio is not. */
static void crash_emit(const char *buf, size_t n)
{
    int fd;

    if (crash_file[0] == '\0')
        return;
    fd = open(crash_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
        return;
    while (n > 0) {
        ssize_t wrote = write(fd, buf, n);

        if (wrote <= 0)
            break;
        buf += wrote;
        n -= (size_t)wrote;
    }
    close(fd);
}

static const char *crash_signal_name(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (bad address)";
    case SIGBUS:  return "SIGBUS (bad access)";
    case SIGILL:  return "SIGILL (illegal instruction)";
    case SIGFPE:  return "SIGFPE (arithmetic)";
    case SIGABRT: return "SIGABRT (abort or a failed assertion)";
    default:      return "signal";
    }
}

/* What was catching these before us, one slot per signal below. */
static struct { int sig; struct sigaction sa; } crash_prev_sa[5];
static int crash_prev_n;

/* What held `sig` before we did, or NULL. */
static const struct sigaction *crash_prev_for(int sig)
{
    int i;

    for (i = 0; i < crash_prev_n; i++) {
        if (crash_prev_sa[i].sig == sig)
            return &crash_prev_sa[i].sa;
    }
    return NULL;
}

static void crash_signal(int sig, siginfo_t *si, void *ctx)
{
    static volatile sig_atomic_t inside;
    void *frames[24];
    int depth;
    /* A fault inside the handler would loop here forever. One report is what
     * the folder is for. */
    if (inside)
        _exit(128 + sig);
    inside = 1;

    depth = mmo_plat_backtrace(frames, (int)(sizeof frames / sizeof frames[0]));
    crash_report(crash_signal_name(sig), (unsigned long)sig,
                 (unsigned long)(uintptr_t)(si != NULL ? si->si_addr : NULL),
                 frames, depth);

    /* Whoever had this signal before we did, if it was anyone: the engine's
     * fault handler prints the guest diagnosis and exits, and that line is
     * worth as much as this file is. */
    {
        const struct sigaction *prev = crash_prev_for(sig);

        if (prev != NULL && prev->sa_handler != SIG_DFL &&
            prev->sa_handler != SIG_IGN) {
            if ((prev->sa_flags & SA_SIGINFO) != 0) {
                if (prev->sa_sigaction != NULL &&
                    prev->sa_sigaction != crash_signal)
                    prev->sa_sigaction(sig, si, ctx);
            } else if (prev->sa_handler != NULL) {
                prev->sa_handler(sig);
            }
        }
    }

    /*
     * Die the way it would have without us: the default disposition, so the exit status, the
     * core and anything watching the process all still say what happened.
     */
    {
        sigset_t only;

        signal(sig, SIG_DFL);
        sigemptyset(&only);
        sigaddset(&only, sig);
        sigprocmask(SIG_UNBLOCK, &only, NULL);
        raise(sig);
    }
    _exit(128 + sig);
}

/*
 * The same, through whatever this desktop calls its file manager. Two forks, so the launcher
 * is never left with a child to reap: the middle process exits at once and init inherits the
 * one that matters.
 */
int mmo_plat_open_folder(const char *path)
{
    pid_t mid;

    if (path == NULL || path[0] == '\0')
        return -1;
    mid = fork();
    if (mid < 0)
        return -1;
    if (mid == 0) {
        if (fork() == 0) {
            execlp("xdg-open", "xdg-open", path, (char *)NULL);
            execlp("open", "open", path, (char *)NULL);   /* the other desktop */
            _exit(127);
        }
        _exit(0);
    }
    waitpid(mid, NULL, 0);
    return 0;
}

/* The first object the loader lists is the program itself, and its dlpi_addr
 * is what was added to every address in it. Zero for a -no-pie build, which is
 * the game. */
static int crash_first_object(struct dl_phdr_info *info, size_t size, void *ud)
{
    (void)size;
    *(unsigned long *)ud = (unsigned long)info->dlpi_addr;
    return 1;                              /* stop: the first one is ours */
}

static unsigned long crash_image_base(void)
{
    unsigned long base = 0;

    dl_iterate_phdr(crash_first_object, &base);
    return base;
}

static int crash_arm(void)
{
    /* A stack overflow is the one crash a handler on the overflowed stack
     * cannot report, and the engine recurses through the field's tasks. 64K
     * of BSS is the price of hearing about it. SIGSTKSZ is not a constant on
     * current glibc, so this does not ask for it. */
    static char altstack[65536];
    static const int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
    struct sigaction sa;
    stack_t ss;
    size_t i;

    ss.ss_sp = altstack;
    ss.ss_size = sizeof altstack;
    ss.ss_flags = 0;
    sigaltstack(&ss, NULL);

    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = crash_signal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (i = 0; i < sizeof sigs / sizeof sigs[0]; i++) {
        struct sigaction old;
        int slot;

        memset(&old, 0, sizeof old);
        if (sigaction(sigs[i], &sa, &old) != 0)
            continue;
        /* Ours already, from an earlier arming: keeping it would make the
         * chain above call this function again, and it would lose the handler
         * this slot is actually for. */
        if ((old.sa_flags & SA_SIGINFO) != 0 && old.sa_sigaction == crash_signal)
            continue;
        for (slot = 0; slot < crash_prev_n; slot++) {
            if (crash_prev_sa[slot].sig == sigs[i])
                break;
        }
        if (slot == crash_prev_n &&
            crash_prev_n < (int)(sizeof crash_prev_sa / sizeof crash_prev_sa[0]))
            crash_prev_sa[crash_prev_n++].sig = sigs[i];
        if (slot < (int)(sizeof crash_prev_sa / sizeof crash_prev_sa[0]))
            crash_prev_sa[slot].sa = old;
    }
    return 0;
}

#endif /* _WIN32 */

/* ------------------------------------------------------------------ */
/* Where the logs go                                                   */
/* ------------------------------------------------------------------ */

/*
 * Both questions at once: does the directory exist, and does it take a file we create? A
 * release unpacked under Program Files answers yes to the first and no to the second, and only
 * the second one matters to a log.
 */
static int dir_takes_a_file(const char *dir)
{
    char probe[1024];
    FILE *f;

    if (dir[0] == '\0' || mmo_plat_mkdir(dir) != 0)
        return 0;
    snprintf(probe, sizeof probe, "%s%s.writable", dir, mmo_plat_sep());
    f = fopen(probe, "ab");
    if (f == NULL)
        return 0;
    fclose(f);
    remove(probe);
    return 1;
}

/*
 * The install root, from this program's own path. The launcher and the window live in `bin/`;
 * the game is in `bin/` in a release and in `fused/` in a built tree.
 */
static void log_install_root(char *out, size_t cap)
{
    char exe[512];
    char *slash;

    out[0] = '\0';
    if (mmo_plat_exe_path(exe, sizeof exe) != 0)
        return;
    slash = (char *)mmo_plat_last_sep(exe);
    if (slash == NULL)
        return;
    *slash = '\0';
    slash = (char *)mmo_plat_last_sep(exe);
    if (slash != NULL &&
        (strcmp(slash + 1, "bin") == 0 || strcmp(slash + 1, "fused") == 0))
        *slash = '\0';
    put(out, cap, exe);
}

int mmo_plat_log_dir(char *out, size_t cap)
{
    static char cached[1200];
    char dir[1200], root[1024];
    const char *e;

    if (cached[0] != '\0')
        return put(out, cap, cached);

    /* Told outright. The suite points a run at a scratch directory this way,
     * and so does a player whose install is somewhere read-only. */
    e = getenv("OPENMMO_LOGS");
    if (e != NULL && e[0] != '\0') {
        snprintf(dir, sizeof dir, "%s", e);
        if (dir_takes_a_file(dir))
            goto found;
    }

    /* Beside the install, which is the whole point: a player who is asked for
     * a log opens the folder the game was unpacked into and it is there. */
    log_install_root(root, sizeof root);
    if (root[0] != '\0') {
        snprintf(dir, sizeof dir, "%s%slogs", root, mmo_plat_sep());
        if (dir_takes_a_file(dir))
            goto found;
    }

    /* An install under Program Files cannot be written to. A log the player
     * has to be talked through finding still beats one that never opens. */
    if (mmo_plat_config_home(root, sizeof root) == 0) {
        snprintf(dir, sizeof dir, "%s%sopenmmo", root, mmo_plat_sep());
        mmo_plat_mkdir(dir);
        snprintf(dir, sizeof dir, "%s%sopenmmo%slogs", root, mmo_plat_sep(),
                 mmo_plat_sep());
        if (dir_takes_a_file(dir))
            goto found;
    }

    /* Where every one of these used to go. */
    if (mmo_plat_temp_dir(root, sizeof root) == 0) {
        snprintf(dir, sizeof dir, "%s", root);
        if (dir_takes_a_file(dir))
            goto found;
    }
    return -1;

found:
    if (put(cached, sizeof cached, dir) != 0)
        return -1;
    return put(out, cap, cached);
}

int mmo_plat_log_path(const char *name, char *out, size_t cap)
{
    char dir[1200];

    if (name == NULL || name[0] == '\0')
        return -1;
    if (mmo_plat_log_dir(dir, sizeof dir) != 0)
        return -1;
    if (strlen(dir) + strlen(name) + 2 > cap)
        return -1;
    snprintf(out, cap, "%s%s%s", dir, mmo_plat_sep(), name);
    return 0;
}

/* ------------------------------------------------------------------ */
/* What a crash report says                                            */
/* ------------------------------------------------------------------ */

/*
 * Assembled by hand into one buffer and written with one call, because this runs inside a
 * signal handler on one host and inside an exception filter on the other, and snprintf is
 * allowed in neither.
 */
static size_t crash_str(char *buf, size_t cap, size_t at, const char *s)
{
    if (s == NULL)
        s = "";
    while (*s != '\0' && at + 1 < cap)
        buf[at++] = *s++;
    return at;
}

static size_t crash_num(char *buf, size_t cap, size_t at, unsigned long v,
                        int hex, int pad)
{
    char digits[24];
    int n = 0;

    do {
        unsigned long d = hex ? (v & 0xf) : (v % 10);

        digits[n++] = (char)(d < 10 ? '0' + d : 'a' + (d - 10));
        v = hex ? (v >> 4) : (v / 10);
    } while (v != 0 && n < (int)sizeof digits);
    while (n < pad && n < (int)sizeof digits)
        digits[n++] = '0';
    if (hex)
        at = crash_str(buf, cap, at, "0x");
    while (n > 0 && at + 1 < cap)
        buf[at++] = digits[--n];
    return at;
}

static void crash_report(const char *reason, unsigned long code,
                         unsigned long at, void **frames, int depth)
{
    char buf[2048];
    size_t n = 0;
    int i;

    n = crash_str(buf, sizeof buf, n, "==== openmmo crash ====\n program  ");
    n = crash_str(buf, sizeof buf, n, crash_prog);
    n = crash_str(buf, sizeof buf, n, " (pid ");
    n = crash_num(buf, sizeof buf, n, (unsigned long)mmo_plat_pid(), 0, 0);
    n = crash_str(buf, sizeof buf, n, ")\n started  ");
    n = crash_str(buf, sizeof buf, n, crash_when);
    n = crash_str(buf, sizeof buf, n, "\n ran for  ");
    n = crash_num(buf, sizeof buf, n,
                  (unsigned long)(mmo_plat_seconds() - crash_started), 0, 0);
    n = crash_str(buf, sizeof buf, n, "s\n reason   ");
    n = crash_str(buf, sizeof buf, n, reason);
    n = crash_str(buf, sizeof buf, n, " (");
    n = crash_num(buf, sizeof buf, n, code, 1, 0);
    n = crash_str(buf, sizeof buf, n, ") at ");
    n = crash_num(buf, sizeof buf, n, at, 1, 8);
    n = crash_str(buf, sizeof buf, n, "\n binary   ");
    n = crash_str(buf, sizeof buf, n, crash_exe);
    /*
     * Where it loaded, because the two numbers below it are not in the same space and a reader
     * cannot tell by looking. The fault address is raw and the frames have this taken off
     * them, which agrees only while this is 0.
     */
    n = crash_str(buf, sizeof buf, n, "\n base     ");
    n = crash_num(buf, sizeof buf, n, crash_base, 1, 8);
    n = crash_str(buf, sizeof buf, n, "\n frames  ");
    for (i = 0; i < depth; i++) {
        unsigned long f = (unsigned long)(uintptr_t)frames[i];

        n = crash_str(buf, sizeof buf, n, " ");
        n = crash_num(buf, sizeof buf, n, f - crash_base, 1, 8);
    }
    /* The one line that turns the addresses above into places in the source.
     * The shipped binaries keep their symbol table, mmo/package.sh strips
     * debugging sections and never the symbols, so this answers even for a
     * release a player is running. */
    n = crash_str(buf, sizeof buf, n, "\n resolve  addr2line -e <binary> <frames>\n"
                                      " send     this file and the other logs "
                                      "in this folder\n\n");
    if (n >= sizeof buf)
        n = sizeof buf - 1;
    crash_emit(buf, n);
}

/* Old reports, dropped oldest first. */
static void crash_keep_newest(int keep)
{
    static char names[64][96];
    char dir[1200], path[1200 + sizeof names[0] + 8];
    DIR *d;
    struct dirent *de;
    size_t dirlen;
    int n = 0, i, j;

    if (mmo_plat_log_dir(dir, sizeof dir) != 0)
        return;
    d = opendir(dir);
    if (d == NULL)
        return;
    while ((de = readdir(d)) != NULL && n < (int)(sizeof names / sizeof names[0])) {
        size_t len = strlen(de->d_name);

        if (strncmp(de->d_name, "crash-", 6) == 0 && len < sizeof names[0])
            memcpy(names[n++], de->d_name, len + 1);
    }
    closedir(d);
    dirlen = strlen(dir);
    if (n <= keep)
        return;
    for (i = 1; i < n; i++) {
        char hold[sizeof names[0]];

        memcpy(hold, names[i], sizeof hold);
        for (j = i; j > 0 && strcmp(names[j - 1], hold) > 0; j--)
            memcpy(names[j], names[j - 1], sizeof hold);
        memcpy(names[j], hold, sizeof hold);
    }
    for (i = 0; i < n - keep; i++) {
        memcpy(path, dir, dirlen);
        path[dirlen] = mmo_plat_sep()[0];
        memcpy(path + dirlen + 1, names[i], strlen(names[i]) + 1);
        remove(path);
    }
}

int mmo_plat_crash_install(const char *program)
{
    char name[160], compact[24];
    void *warm[4];
    size_t i, at = 0;

    /*
     * Called again with the path already resolved means one thing: something else has taken
     * these signals since (the engine installs its own in main(), after the constructor the
     * game arms from).
     */
    if (crash_file[0] != '\0')
        return crash_arm();
    if (program == NULL || program[0] == '\0')
        program = "openmmo";
    put(crash_prog, sizeof crash_prog, program);
    if (mmo_plat_exe_path(crash_exe, sizeof crash_exe) != 0)
        put(crash_exe, sizeof crash_exe, "unknown");
    mmo_plat_stamp(crash_when, sizeof crash_when);

    /* The stamp again, as a file name: digits only, with the date and the time
     * kept apart. Names in this form sort into the order they happened, which
     * is what the pruning above leans on. */
    for (i = 0; crash_when[i] != '\0' && at + 1 < sizeof compact; i++) {
        if (crash_when[i] >= '0' && crash_when[i] <= '9')
            compact[at++] = crash_when[i];
        else if (crash_when[i] == ' ')
            compact[at++] = '-';
    }
    compact[at] = '\0';
    snprintf(name, sizeof name, "crash-%s-%s-%u.log", crash_prog, compact,
             mmo_plat_pid());
    if (mmo_plat_log_path(name, crash_file, sizeof crash_file) != 0) {
        crash_file[0] = '\0';
        return -1;
    }
    crash_started = mmo_plat_seconds();
    crash_base = crash_image_base();

    /* Taken once now, where it is allowed to be slow: glibc resolves the
     * unwinder lazily, and a handler is the worst place in the program to be
     * loading a library. */
    mmo_plat_backtrace(warm, (int)(sizeof warm / sizeof warm[0]));
    crash_keep_newest(10);
    return crash_arm();
}

/* ------------------------------------------------------------------ */
/* The UI face, which is a list on both hosts                          */
/* ------------------------------------------------------------------ */

/*
 * Both windows draw their own text, the launcher through raylib, the game window through
 * freetype, and both used to name one Debian path. A player on any other machine got the
 * built-in fallback glyphs and no explanation.
 */
int mmo_plat_ui_font(int bold, char *out, size_t cap)
{
    static const char *const posix_face[2] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
    };
    static const char *const win_face[2] = { "segoeui.ttf", "seguisb.ttf" };
    static const char *const win_alt[2] = { "arial.ttf", "arialbd.ttf" };
    const char *beside[2] = { "DejaVuSans.ttf", "DejaVuSans-Bold.ttf" };
    char path[1024];
    char exe[512];
    FILE *f;
    int i = bold ? 1 : 0;

    /* Named outright. An Android app has no directory beside the program and
     * no distribution fonts at all, so the frontend points this at a face it
     * carries; on the desktops it is simply an override that wins. */
    {
        const char *env = getenv("OPENMMO_UI_FONT");

        if (env != NULL && env[0] != '\0') {
            f = fopen(env, "rb");
            if (f != NULL) {
                fclose(f);
                return put(out, cap, env);
            }
        }
    }

    /* Beside the program. */
    if (mmo_plat_exe_path(exe, sizeof exe) == 0) {
        char *slash = (char *)mmo_plat_last_sep(exe);

        if (slash != NULL) {
            *slash = '\0';
            snprintf(path, sizeof path, "%s%s%s", exe, mmo_plat_sep(),
                     beside[i]);
            f = fopen(path, "rb");
            if (f != NULL) {
                fclose(f);
                return put(out, cap, path);
            }
        }
    }

    f = fopen(posix_face[i], "rb");
    if (f != NULL) {
        fclose(f);
        return put(out, cap, posix_face[i]);
    }

#if defined(_WIN32)
    {
        const char *root = getenv("SystemRoot");
        int k;

        if (root == NULL || root[0] == '\0')
            root = "C:\\Windows";
        for (k = 0; k < 2; k++) {
            snprintf(path, sizeof path, "%s\\Fonts\\%s", root,
                     k == 0 ? win_face[i] : win_alt[i]);
            f = fopen(path, "rb");
            if (f != NULL) {
                fclose(f);
                return put(out, cap, path);
            }
        }
    }
#else
    (void)win_face;
    (void)win_alt;
#endif
    return -1;
}

/* ------------------------------------------------------------------ */
/* Choosing a file, which is a window on one host and a child on the   */
/* other                                                               */
/* ------------------------------------------------------------------ */

#if defined(_WIN32)

#include <commdlg.h>

int mmo_plat_pick_file(const char *title, const char *start_dir,
                       const char *filter_name, const char *filter_glob,
                       char *out, size_t cap)
{
    OPENFILENAMEA ofn;
    char chosen[1024];
    char filter[256];
    size_t at = 0;

    if (out == NULL || cap == 0)
        return -1;
    chosen[0] = '\0';

    /* The filter is one buffer of NUL-separated pairs ending in a second NUL,
     * so it cannot be built with printf. */
    if (filter_name != NULL && filter_glob != NULL) {
        int n = snprintf(filter + at, sizeof filter - at, "%s (%s)",
                         filter_name, filter_glob);

        if (n < 0 || (size_t)n >= sizeof filter - at)
            return -1;
        at += (size_t)n + 1;
        n = snprintf(filter + at, sizeof filter - at, "%s", filter_glob);
        if (n < 0 || (size_t)n >= sizeof filter - at)
            return -1;
        at += (size_t)n + 1;
    }
    at += (size_t)snprintf(filter + at, sizeof filter - at,
                           "All files (*.*)") + 1;
    at += (size_t)snprintf(filter + at, sizeof filter - at, "*.*") + 1;
    if (at >= sizeof filter)
        return -1;
    filter[at] = '\0';

    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = chosen;
    ofn.nMaxFile = (DWORD)sizeof chosen;
    ofn.lpstrTitle = title;
    ofn.lpstrInitialDir = (start_dir != NULL && start_dir[0] != '\0')
        ? start_dir : NULL;
    /* NOCHANGEDIR because this process finds the game beside itself by a
     * relative path, and a dialog that moved the working directory would
     * quietly break the next launch. */
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR
        | OFN_HIDEREADONLY;
    if (!GetOpenFileNameA(&ofn))
        return 1;
    return put(out, cap, chosen) == 0 ? 0 : -1;
}

#else /* !_WIN32 */

/* Is there such a program on PATH? execvp would answer too, but only after
 * the fork, and a caller has to know before it gives up its own window
 * whether anything is going to appear. */
static int have_prog(const char *name)
{
    const char *path = getenv("PATH");
    char buf[1024];

    if (path == NULL || path[0] == '\0')
        return 0;
    while (*path != '\0') {
        const char *end = strchr(path, ':');
        size_t len = end != NULL ? (size_t)(end - path) : strlen(path);

        if (len > 0 && len + strlen(name) + 2 < sizeof buf) {
            memcpy(buf, path, len);
            buf[len] = '/';
            memcpy(buf + len + 1, name, strlen(name) + 1);
            if (access(buf, X_OK) == 0)
                return 1;
        }
        if (end == NULL)
            break;
        path = end + 1;
    }
    return 0;
}

/* Run one picker and read the path off its stdout. Its exit status is the
 * answer: zenity and kdialog both say 0 for a choice and 1 for a cancel. */
static int run_picker(char *const argv[], char *out, size_t cap)
{
    int fds[2];
    pid_t pid;
    char buf[1024];
    size_t at = 0;
    int status = 0;

    if (pipe(fds) != 0)
        return -1;
    pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        close(fds[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);
    for (;;) {
        ssize_t n = read(fds[0], buf + at, sizeof buf - 1 - at);

        if (n <= 0)
            break;
        at += (size_t)n;
        if (at >= sizeof buf - 1)
            break;
    }
    buf[at] = '\0';
    close(fds[0]);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
    if (!WIFEXITED(status) || WEXITSTATUS(status) == 127)
        return -1;
    while (at > 0 && (buf[at - 1] == '\n' || buf[at - 1] == '\r'))
        buf[--at] = '\0';
    if (WEXITSTATUS(status) != 0 || buf[0] == '\0')
        return 1;
    return put(out, cap, buf) == 0 ? 0 : -1;
}

int mmo_plat_pick_file(const char *title, const char *start_dir,
                       const char *filter_name, const char *filter_glob,
                       char *out, size_t cap)
{
    /* zenity is gnome's, qarma and matedialog are its argument-compatible
     * clones, and kdialog is KDE's with its own spelling. */
    static const char *const zenity_like[] = {
        "zenity", "qarma", "matedialog", "yad", NULL
    };
    char here[1024];
    char filter[256];
    char kfilter[128];
    int i;

    if (out == NULL || cap == 0)
        return -1;
    if (title == NULL || title[0] == '\0')
        title = "Choose a file";

    /* zenity opens in the folder only when the name ends in a separator. */
    if (start_dir != NULL && start_dir[0] != '\0')
        snprintf(here, sizeof here, "%s/", start_dir);
    else
        here[0] = '\0';
    if (filter_name != NULL && filter_glob != NULL) {
        snprintf(filter, sizeof filter, "%s | %s", filter_name, filter_glob);
        snprintf(kfilter, sizeof kfilter, "%s|%s", filter_glob, filter_name);
    } else {
        snprintf(filter, sizeof filter, "All files | *");
        snprintf(kfilter, sizeof kfilter, "*|All files");
    }

    for (i = 0; zenity_like[i] != NULL; i++) {
        char *argv[12];
        int n = 0;

        if (!have_prog(zenity_like[i]))
            continue;
        argv[n++] = (char *)zenity_like[i];
        argv[n++] = (char *)"--file-selection";
        argv[n++] = (char *)"--title";
        argv[n++] = (char *)title;
        if (here[0] != '\0') {
            argv[n++] = (char *)"--filename";
            argv[n++] = here;
        }
        argv[n++] = (char *)"--file-filter";
        argv[n++] = filter;
        argv[n++] = (char *)"--file-filter";
        argv[n++] = (char *)"All files | *";
        argv[n] = NULL;
        {
            int rc = run_picker(argv, out, cap);

            if (rc >= 0)
                return rc;
        }
    }
    if (have_prog("kdialog")) {
        char *argv[7];
        int n = 0;

        argv[n++] = (char *)"kdialog";
        argv[n++] = (char *)"--title";
        argv[n++] = (char *)title;
        argv[n++] = (char *)"--getopenfilename";
        argv[n++] = here[0] != '\0' ? here : (char *)".";
        argv[n++] = kfilter;
        argv[n] = NULL;
        {
            int rc = run_picker(argv, out, cap);

            if (rc >= 0)
                return rc;
        }
    }
    return -1;
}

#endif /* _WIN32 */
