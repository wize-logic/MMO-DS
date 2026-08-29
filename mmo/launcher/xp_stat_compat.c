/* _stat64i32 for the prebuilt raylib, on the C library Windows XP ships. */

#if defined(_WIN32)

#include <sys/stat.h>

int stat64i32(const char *path, struct _stat64i32 *out)
{
    struct _stat64 s;

    if (_stat64(path, &s) != 0) {
        return -1;
    }
    out->st_dev   = s.st_dev;
    out->st_ino   = s.st_ino;
    out->st_mode  = s.st_mode;
    out->st_nlink = s.st_nlink;
    out->st_uid   = s.st_uid;
    out->st_gid   = s.st_gid;
    out->st_rdev  = s.st_rdev;
    out->st_size  = (_off_t)s.st_size;
    out->st_atime = s.st_atime;
    out->st_mtime = s.st_mtime;
    out->st_ctime = s.st_ctime;
    return 0;
}

#else

typedef int openmmo_xp_stat_compat_is_windows_only;

#endif
