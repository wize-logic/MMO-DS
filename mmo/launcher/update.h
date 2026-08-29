/*
 * The download half of the feed: fetch what the signature vouches for, and nothing
 * else.
 */

#ifndef OPENMMO_UPDATE_H
#define OPENMMO_UPDATE_H

#include <stddef.h>

#include "feed.h"
#include "launch_plan.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The public key at `path` (PEM, or bare base64 SPKI), ready to verify with.
 * Returns 0, or -1 with a player-readable line in `msg`.
 */
int mmo_update_load_key(const char *path, mmo_rsa_pubkey *key,
                        char *msg, size_t msgcap);

/*
 * Bring the install at `root` up to what the server at `url` publishes, verifying everything
 * under `key`.
 */
int mmo_update_run(const char *url, const char *feed_dir,
                   const mmo_rsa_pubkey *key, const char *root,
                   const char *self_rel,
                   void (*note)(void *ud, const char *line),
                   void (*tick)(void *ud, const char *line), void *ud,
                   int *self_updated, char *msg, size_t msgcap);

/* What the channel says is current, without fetching anything else. */
int mmo_update_latest_revision(const char *url, const mmo_rsa_pubkey *key,
                               int *revision, char *msg, size_t msgcap);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_UPDATE_H */
