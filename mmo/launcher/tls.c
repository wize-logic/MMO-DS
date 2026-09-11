/*
 * One TLS client session under the update fetch, on mbedTLS. tls.h carries the
 * contract and mmo_mbedtls_config.h the build of the library.
 */

#include "tls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "platform.h"

#include "mbedtls/build_info.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"   /* the MBEDTLS_ERR_NET_* codes a bio speaks;
                                    * MBEDTLS_NET_C itself is off */
#include "mbedtls/platform_time.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

#include "tls_roots.gen.h"         /* mmo_tls_roots_pem[], out of cacert.pem */

/* An operator's feed-ca file is read whole; a ca bundle is far below this. */
#define TLS_CA_FILE_MAX (1024 * 1024)

struct mmo_tls {
    mmo_tls_send_fn send;
    mmo_tls_recv_fn recv;
    void *ud;
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_x509_crt         roots;
    mbedtls_ssl_config       conf;
    mbedtls_ssl_context      ssl;
    char host[256];
    int  up;                       /* the handshake has completed */
};

/* ------------------------------------------------- what the library asks */

time_t mmo_tls_time(time_t *t)
{
    long long s = mmo_plat_unix_time();

    if (t != NULL)
        *t = (time_t)s;
    return (time_t)s;
}

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len,
                          size_t *olen)
{
    (void)data;
    *olen = 0;
    if (mmo_plat_random(output, len) != 0)
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    *olen = len;
    return 0;
}

/*
 * Seconds since the epoch to a broken-down utc time, by arithmetic alone (the proleptic
 * Gregorian calendar, days counted from 0000-03-01 so that leap days fall at the end of a
 * year).
 */
struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *tt,
                                     struct tm *tm_buf)
{
    long long t = (long long)*tt, days, rem, z, era, y;
    unsigned doe, yoe, doy, mp, m, d;
    int leap;

    days = t / 86400;
    rem = t % 86400;
    if (rem < 0) {
        rem += 86400;
        days -= 1;
    }
    memset(tm_buf, 0, sizeof *tm_buf);
    tm_buf->tm_hour = (int)(rem / 3600);
    tm_buf->tm_min = (int)(rem % 3600 / 60);
    tm_buf->tm_sec = (int)(rem % 60);
    tm_buf->tm_wday = (int)((days % 7 + 11) % 7);   /* day 0 was a Thursday */

    z = days + 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = (unsigned)(z - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = (long long)yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2)
        y += 1;
    leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    tm_buf->tm_year = (int)(y - 1900);
    tm_buf->tm_mon = (int)m - 1;
    tm_buf->tm_mday = (int)d;
    tm_buf->tm_yday = m <= 2 ? (int)doy - 306 : (int)doy + 59 + leap;
    return tm_buf;
}

/* ------------------------------------------------------------- the wire */

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    mmo_tls *t = ctx;
    long w = t->send(t->ud, buf, len);

    if (w > 0)
        return (int)w;
    if (w == 0 || w == MMO_TLS_WANT_WRITE)
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    if (w == MMO_TLS_WANT_READ)
        return MBEDTLS_ERR_SSL_WANT_READ;
    return MBEDTLS_ERR_NET_SEND_FAILED;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    mmo_tls *t = ctx;
    long r = t->recv(t->ud, buf, len);

    if (r > 0)
        return (int)r;
    if (r == 0)
        return 0;                  /* the library reports it as CONN_EOF */
    if (r == MMO_TLS_WANT_READ)
        return MBEDTLS_ERR_SSL_WANT_READ;
    if (r == MMO_TLS_WANT_WRITE)
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

/* ------------------------------------------------------------ messages */

static int fail(char *err, size_t cap, const char *fmt, const char *a,
                const char *b)
{
    if (err != NULL && cap > 0)
        snprintf(err, cap, fmt, a, b);
    return MMO_TLS_ERR;
}

/* The library's own words for a code, after ours. */
static int fail_lib(char *err, size_t cap, const char *fmt, const char *a,
                    int rc)
{
    char why[128];

    mbedtls_strerror(rc, why, sizeof why);
    return fail(err, cap, fmt, a, why);
}

/* ------------------------------------------------------------- session */

static int load_ca_file(mmo_tls *t, const char *path, char *err, size_t errcap)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long n;
    int rc;

    if (f == NULL)
        return fail(err, errcap, "%.300s (feed-ca) cannot be read", path, NULL);
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 ||
        n > TLS_CA_FILE_MAX || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return fail(err, errcap, "%.300s (feed-ca) cannot be read whole", path,
                    NULL);
    }
    buf = malloc((size_t)n + 1);
    if (buf == NULL) {
        fclose(f);
        return fail(err, errcap, "out of memory", NULL, NULL);
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        return fail(err, errcap, "%.300s (feed-ca) cannot be read", path, NULL);
    }
    fclose(f);
    buf[n] = '\0';                 /* PEM parsing wants the terminator counted */
    rc = mbedtls_x509_crt_parse(&t->roots, buf, (size_t)n + 1);
    free(buf);
    /* An operator's file is held to be clean: a positive count is
     * certificates in it this client could not read, and a file that names a
     * Ca it then cannot use is a setting that reads as trust while being
     * none. */
    if (rc != 0)
        return fail(err, errcap, "%.300s (feed-ca) holds no certificate this "
                                 "client can read", path, NULL);
    return 0;
}

mmo_tls *mmo_tls_open(const char *host, const char *ca,
                      mmo_tls_send_fn send, mmo_tls_recv_fn recv, void *ud,
                      char *err, size_t errcap)
{
    /* The key shares offered, in the order they are tried: X25519 first
     * because that is what the edges an operator is likely to sit behind
     * pick, so the handshake needs no second round. */
    static const uint16_t groups[] = {
        MBEDTLS_SSL_IANA_TLS_GROUP_X25519,
        MBEDTLS_SSL_IANA_TLS_GROUP_SECP256R1,
        MBEDTLS_SSL_IANA_TLS_GROUP_SECP384R1,
        MBEDTLS_SSL_IANA_TLS_GROUP_NONE
    };
    static const char pers[] = "openmmo-launch update fetch";
    mmo_tls *t;
    int rc;

    if (err != NULL && errcap > 0)
        err[0] = '\0';
    t = calloc(1, sizeof *t);
    if (t == NULL) {
        fail(err, errcap, "out of memory", NULL, NULL);
        return NULL;
    }
    t->send = send;
    t->recv = recv;
    t->ud = ud;
    snprintf(t->host, sizeof t->host, "%s", host);
    mbedtls_entropy_init(&t->entropy);
    mbedtls_ctr_drbg_init(&t->drbg);
    mbedtls_x509_crt_init(&t->roots);
    mbedtls_ssl_config_init(&t->conf);
    mbedtls_ssl_init(&t->ssl);

    /* TLS 1.3 runs its key schedule through the PSA layer, which wants to
     * have been started once; the call is idempotent. */
    if (psa_crypto_init() != PSA_SUCCESS) {
        fail(err, errcap, "the TLS library could not start", NULL, NULL);
        goto bad;
    }
    rc = mbedtls_ctr_drbg_seed(&t->drbg, mbedtls_entropy_func, &t->entropy,
                               (const unsigned char *)pers, sizeof pers - 1);
    if (rc != 0) {
        fail_lib(err, errcap, "no randomness for a TLS session%.0s: %s", "",
                 rc);
        goto bad;
    }
    /* The compiled-in roots. A positive return is roots this build cannot
     * read (an algorithm left out), which is not a reason to trust nothing:
     * the rest of the store still stands. Negative is no store at all. */
    rc = mbedtls_x509_crt_parse(&t->roots,
                                (const unsigned char *)mmo_tls_roots_pem,
                                sizeof mmo_tls_roots_pem);
    if (rc < 0) {
        fail_lib(err, errcap, "the compiled-in root certificates cannot be "
                              "read%.0s: %s", "", rc);
        goto bad;
    }
    if (ca != NULL && ca[0] != '\0' && load_ca_file(t, ca, err, errcap) != 0)
        goto bad;

    rc = mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT,
                                     MBEDTLS_SSL_TRANSPORT_STREAM,
                                     MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0) {
        fail_lib(err, errcap, "the TLS session cannot be configured%.0s: %s",
                 "", rc);
        goto bad;
    }
    mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&t->conf, &t->roots, NULL);
    mbedtls_ssl_conf_rng(&t->conf, mbedtls_ctr_drbg_random, &t->drbg);
    mbedtls_ssl_conf_groups(&t->conf, groups);
    rc = mbedtls_ssl_setup(&t->ssl, &t->conf);
    if (rc == 0)
        rc = mbedtls_ssl_set_hostname(&t->ssl, host);
    if (rc != 0) {
        fail_lib(err, errcap, "the TLS session cannot be set up%.0s: %s", "",
                 rc);
        goto bad;
    }
    mbedtls_ssl_set_bio(&t->ssl, t, bio_send, bio_recv, NULL);
    return t;
bad:
    mmo_tls_close(t);
    return NULL;
}

/* The verification flags as one line: the library writes one sentence per
 * flag, each on its own line, and a player reads one. */
static void verify_words(mmo_tls *t, char *out, size_t cap)
{
    uint32_t flags = mbedtls_ssl_get_verify_result(&t->ssl);
    char raw[512];
    size_t i, at = 0;

    if (mbedtls_x509_crt_verify_info(raw, sizeof raw, "", flags) < 0 ||
        raw[0] == '\0')
        snprintf(raw, sizeof raw, "it did not verify\n");
    for (i = 0; raw[i] != '\0' && at + 3 < cap; i++) {
        if (raw[i] == '\n') {
            if (raw[i + 1] == '\0')
                break;
            out[at++] = ';';
            out[at++] = ' ';
        } else {
            out[at++] = raw[i];
        }
    }
    out[at] = '\0';
}

int mmo_tls_handshake(mmo_tls *t, char *err, size_t errcap)
{
    int rc = mbedtls_ssl_handshake(&t->ssl);

    if (rc == 0) {
        t->up = 1;
        return 0;
    }
    if (rc == MBEDTLS_ERR_SSL_WANT_READ)
        return MMO_TLS_WANT_READ;
    if (rc == MBEDTLS_ERR_SSL_WANT_WRITE)
        return MMO_TLS_WANT_WRITE;
    if (rc == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
        char why[160];

        verify_words(t, why, sizeof why);
        return fail(err, errcap, "%.100s's certificate is not trusted: %s "
                                 "(an operator names a private authority "
                                 "with feed-ca in launcher.cfg)",
                    t->host, why);
    }
    if (rc == MBEDTLS_ERR_SSL_INVALID_RECORD ||
        rc == MBEDTLS_ERR_SSL_UNEXPECTED_MESSAGE)
        return fail_lib(err, errcap, "%.100s did not answer in TLS, is "
                                     "that port really https? (%s)",
                        t->host, rc);
    return fail_lib(err, errcap, "the TLS handshake with %.100s failed: %s",
                    t->host, rc);
}

long mmo_tls_send(mmo_tls *t, const void *data, size_t n,
                  char *err, size_t errcap)
{
    int rc = mbedtls_ssl_write(&t->ssl, data, n);

    if (rc >= 0)
        return rc;
    if (rc == MBEDTLS_ERR_SSL_WANT_READ)
        return MMO_TLS_WANT_READ;
    if (rc == MBEDTLS_ERR_SSL_WANT_WRITE)
        return MMO_TLS_WANT_WRITE;
    return fail_lib(err, errcap, "sending to %.100s failed: %s", t->host, rc);
}

long mmo_tls_recv(mmo_tls *t, void *buf, size_t cap, char *err, size_t errcap)
{
    for (;;) {
        int rc = mbedtls_ssl_read(&t->ssl, buf, cap);

        if (rc > 0)
            return rc;
        /* The peer said goodbye, or simply went: either way the bytes have
         * stopped, and the HTTP layer above knows whether it had them all. */
        if (rc == 0 || rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ||
            rc == MBEDTLS_ERR_SSL_CONN_EOF)
            return 0;
        if (rc == MBEDTLS_ERR_SSL_WANT_READ)
            return MMO_TLS_WANT_READ;
        if (rc == MBEDTLS_ERR_SSL_WANT_WRITE)
            return MMO_TLS_WANT_WRITE;
#if defined(MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
        /* A TLS 1.3 server's ticket, offered after the handshake; this
         * client keeps no sessions, so it is read past. */
        if (rc == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
            continue;
#endif
        return fail_lib(err, errcap, "reading from %.100s failed: %s", t->host,
                        rc);
    }
}

void mmo_tls_close(mmo_tls *t)
{
    if (t == NULL)
        return;
    if (t->up)
        (void)mbedtls_ssl_close_notify(&t->ssl);   /* one try, unwaited */
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->conf);
    mbedtls_x509_crt_free(&t->roots);
    mbedtls_ctr_drbg_free(&t->drbg);
    mbedtls_entropy_free(&t->entropy);
    free(t);
}
