/* The in-binary known-answer suite. */
#ifndef MMO_SELFTEST_H
#define MMO_SELFTEST_H

#include <stdio.h>

/*
 * Runs every vector, printing one line per check and a per-group summary to `out`. Returns the
 * number of checks that failed, 0 when the binary's own frame/CRC/HMAC/AES-CTR/ECDH agree
 * with the standards.
 */
int openmmo_selftest_run(FILE *out);

#endif /* MMO_SELFTEST_H */
