/* One HTTP GET, plain or under TLS, for the update the feed vouches for. */

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
 * Where a channel is served: the parts of `http://host[:port][/base]` or
 * `https://host[:port][/base]`. `base` keeps a leading slash and loses a trailing one; an
 * empty path is "".
 */
typedef struct {
    int  tls;                      /* 1 for https://, 0 for http:// */
    char host[MMO_FETCH_HOST];
    char port[16];                 /* the URL's, else 443 or 80 */
    char base[MMO_FETCH_PATH];
    const char *ca;
} mmo_fetch_origin;

/* Split a URL into `o`. Anything but http:// or https:// is refused by name.
 * Returns 0, or -1 with the reason in `err`. */
int mmo_fetch_split(const char *url, mmo_fetch_origin *o,
                    char *err, size_t errcap);

/* Get `path` from `o` into `buf`. Only a 200 with a body no larger than `cap` succeeds. */
long mmo_fetch_buf(const mmo_fetch_origin *o, const char *path,
                   void *buf, size_t cap, char *err, size_t errcap);

/*
 * The same get, streamed into the file at `dest`, which is truncated first. `cap` bounds the
 * body.
 */
long mmo_fetch_file(const mmo_fetch_origin *o, const char *path,
                    const char *dest, long cap,
                    void (*tick)(void *ud, long got, long total), void *tickud,
                    char *err, size_t errcap);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_FETCH_H */
