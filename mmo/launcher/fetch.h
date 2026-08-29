/* One HTTP GET, for the update the feed vouches for. */

#ifndef OPENMMO_FETCH_H
#define OPENMMO_FETCH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MMO_FETCH_HOST 256
#define MMO_FETCH_PATH 512
#define MMO_FETCH_ERR  256

/*
 * Split `http://host[:port][/base]` into its parts. `base` keeps a leading slash and loses a
 * trailing one; an empty path is "".
 */
int mmo_fetch_split(const char *url, char *host, size_t hostcap,
                    char *port, size_t portcap, char *base, size_t basecap,
                    char *err, size_t errcap);

/*
 * Get http://host:port/path into `buf`. Only a 200 with a body no larger than `cap` succeeds;
 * a redirect reports where the server pointed.
 */
long mmo_fetch_buf(const char *host, const char *port, const char *path,
                   void *buf, size_t cap, char *err, size_t errcap);

/*
 * The same get, streamed into the file at `dest`, which is truncated first. `cap` bounds the
 * body.
 */
long mmo_fetch_file(const char *host, const char *port, const char *path,
                    const char *dest, long cap,
                    void (*tick)(void *ud, long got, long total), void *tickud,
                    char *err, size_t errcap);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_FETCH_H */
