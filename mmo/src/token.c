/*
 * The saved sign-in on disk. See token.h for what it is and why it is not a row in
 * launcher.cfg.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "token.h"
#include "platform.h"

/* The name comparison the server does (`equals(username, ignoreCase = true)`),
 * written out because strcasecmp is not the same name on every host this
 * builds for. ASCII only, which is what an account name is. */
int mmo_token_name_is(const char *a, const char *b)
{
    size_t i;

    if (a == NULL || b == NULL)
        return 0;
    for (i = 0; a[i] != '\0' && b[i] != '\0'; i++) {
        int ca = a[i], cb = b[i];

        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb)
            return 0;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int mmo_token_path(char *out, size_t cap)
{
    char base[1024];
    const char *sep = mmo_plat_sep();
    int n;

    if (out == NULL || cap == 0)
        return -1;
    if (mmo_plat_config_home(base, sizeof base) != 0)
        return -1;
    n = snprintf(out, cap, "%s%sopenmmo%stoken", base, sep, sep);
    return (n < 0 || (size_t)n >= cap) ? -1 : 0;
}

/*
 * The whole file, split into its two parts. Returns 0 with the name and the token, or -1 for
 * anything else, a missing file, a truncated one, a hex digit that is not one.
 */
static int token_read(char *name, size_t namecap, u8 *out, size_t cap,
                      size_t *outlen)
{
    char path[1024];
    char buf[1024];
    FILE *f;
    size_t n, i, got = 0;
    char *nl, *hex;

    if (mmo_token_path(path, sizeof path) != 0)
        return -1;
    f = fopen(path, "rb");
    if (f == NULL)
        return -1;
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';

    nl = strchr(buf, '\n');
    if (nl == NULL || nl == buf)
        return -1;                  /* no name line, or an empty name */
    *nl = '\0';
    if (name != NULL)
        snprintf(name, namecap, "%s", buf);

    hex = nl + 1;
    for (i = 0; hex[i] != '\0' && hex[i] != '\n'; i += 2) {
        int hi = hex_value(hex[i]);
        /* hex[i + 1] is in bounds even at the end: the NUL is, and it is not
         * a hex digit, so an odd number of digits is refused here. */
        int lo = hex_value(hex[i + 1]);

        if (hi < 0 || lo < 0)
            return -1;
        if (out == NULL || got >= cap)
            return -1;
        out[got++] = (u8)((hi << 4) | lo);
    }
    if (got == 0)
        return -1;
    if (outlen != NULL)
        *outlen = got;
    return 0;
}

int mmo_token_save(const char *name, const u8 *token, size_t n)
{
    char path[1024], dir[1024];
    const char *sep = mmo_plat_sep();
    FILE *f;
    size_t i;

    if (name == NULL || name[0] == '\0' || token == NULL ||
        n == 0 || n > MMO_TOKEN_MAX)
        return -1;
    /* A newline in the name would forge the split this file is parsed on. No
     * account name has one; refusing beats writing a file that reads back as
     * somebody else. */
    if (strchr(name, '\n') != NULL || strlen(name) >= MMO_TOKEN_NAME)
        return -1;
    if (mmo_token_path(path, sizeof path) != 0)
        return -1;
    if (mmo_plat_config_home(dir, sizeof dir) != 0)
        return -1;
    if (strlen(dir) + strlen(sep) + strlen("openmmo") + 1 >= sizeof dir)
        return -1;
    strcat(dir, sep);
    strcat(dir, "openmmo");
    mmo_plat_mkdir(dir);            /* already there is fine */

    /* Made unreadable to anyone else before a byte is written to it, so the
     * credential is never briefly world readable on a shared machine. */
    if (mmo_plat_private_file(path) != 0)
        return -1;
    f = fopen(path, "wb");
    if (f == NULL)
        return -1;
    if (fprintf(f, "%s\n", name) < 0)
        goto failed;
    for (i = 0; i < n; i++) {
        if (fprintf(f, "%02x", (unsigned)token[i]) < 0)
            goto failed;
    }
    if (fputc('\n', f) == EOF)
        goto failed;
    if (fclose(f) != 0) {
        remove(path);
        return -1;
    }
    return 0;

failed:
    fclose(f);
    remove(path);
    return -1;
}

size_t mmo_token_load(const char *name, u8 *out, size_t cap)
{
    char saved[MMO_TOKEN_NAME];
    size_t n = 0;

    if (name == NULL || name[0] == '\0' || out == NULL || cap == 0)
        return 0;
    if (token_read(saved, sizeof saved, out, cap, &n) != 0)
        return 0;
    /* Somebody else's saved sign-in. Left alone rather than cleared: the
     * player may be visiting a second account and still want their own. */
    if (!mmo_token_name_is(saved, name))
        return 0;
    return n;
}

int mmo_token_who(char *out, size_t cap)
{
    u8 scratch[MMO_TOKEN_MAX];
    size_t n = 0;

    if (out == NULL || cap == 0)
        return 0;
    if (token_read(out, cap, scratch, sizeof scratch, &n) != 0)
        return 0;
    return out[0] != '\0';
}

void mmo_token_clear(void)
{
    char path[1024];

    if (mmo_token_path(path, sizeof path) == 0)
        remove(path);
}
