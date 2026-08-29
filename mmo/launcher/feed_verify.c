/* RSASSA-PKCS1-v1_5 with SHA-256, over a DER public key. */

#include "feed.h"

#include <stdio.h>
#include <string.h>

#include "crypto.h"

static int fail(char *err, size_t cap, const char *msg)
{
    if (err != NULL && cap > 0)
        snprintf(err, cap, "%s", msg);
    return -1;
}

/* ====================================================================== */
/* base64                                                                  */
/* ====================================================================== */

static int b64val(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Decode into `out`, returning the length or -1. Whitespace is skipped; '='
 * ends the stream; anything else outside the alphabet is refused, so a
 * truncated or mangled paste fails instead of decoding short. */
static int b64decode(const char *text, size_t len, u8 *out, size_t cap)
{
    size_t i, n = 0;
    u32 acc = 0;
    int bits = 0;

    for (i = 0; i < len; i++) {
        int c = (unsigned char)text[i], v;

        if (c == '\n' || c == '\r' || c == ' ' || c == '\t')
            continue;
        if (c == '=')
            break;
        v = b64val(c);
        if (v < 0)
            return -1;
        acc = (acc << 6) | (u32)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n >= cap)
                return -1;
            out[n++] = (u8)((acc >> bits) & 0xff);
        }
    }
    return (int)n;
}

/* ====================================================================== */
/* just enough DER                                                         */
/* ====================================================================== */

typedef struct { const u8 *p, *end; } der;

/* Take one TLV of tag `tag`, leaving its body in `body`. Long-form lengths up
 * to four bytes; indefinite length is refused (it cannot appear in DER). */
static int der_take(der *d, u8 tag, der *body)
{
    size_t len = 0;
    int nlen;

    if (d->end - d->p < 2 || *d->p != tag)
        return -1;
    d->p++;
    if ((*d->p & 0x80) == 0) {
        len = *d->p++;
    } else {
        nlen = *d->p++ & 0x7f;
        if (nlen == 0 || nlen > 4 || d->end - d->p < nlen)
            return -1;
        while (nlen-- > 0)
            len = (len << 8) | *d->p++;
    }
    if ((size_t)(d->end - d->p) < len)
        return -1;
    body->p = d->p;
    body->end = d->p + len;
    d->p += len;
    return 0;
}

/* An INTEGER's magnitude, with DER's leading sign byte dropped and negatives
 * refused: every integer in a public key is positive. */
static int der_uint(der *d, const u8 **out, size_t *outlen)
{
    der v;

    if (der_take(d, 0x02, &v) != 0)
        return -1;
    if (v.p == v.end)
        return -1;
    if ((v.p[0] & 0x80) != 0)
        return -1;
    while (v.end - v.p > 1 && v.p[0] == 0x00)
        v.p++;
    *out = v.p;
    *outlen = (size_t)(v.end - v.p);
    return 0;
}

static const u8 OID_RSA[] = {
    0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01
};

/* ====================================================================== */
/* the modulus, and Montgomery's constants                                 */
/* ====================================================================== */

static int bn_cmp(const u32 *a, const u32 *b, int k)
{
    int i;

    for (i = k - 1; i >= 0; i--) {
        if (a[i] != b[i])
            return a[i] > b[i] ? 1 : -1;
    }
    return 0;
}

static void bn_sub(u32 *a, const u32 *b, int k)
{
    u64 borrow = 0;
    int i;

    for (i = 0; i < k; i++) {
        u64 d = (u64)a[i] - b[i] - borrow;
        a[i] = (u32)d;
        borrow = (d >> 32) & 1;
    }
}

/* a = 2a mod n, for an a already below n. */
static void bn_dbl_mod(u32 *a, const u32 *n, int k)
{
    u32 carry = 0;
    int i;

    for (i = 0; i < k; i++) {
        u32 hi = a[i] >> 31;
        a[i] = (a[i] << 1) | carry;
        carry = hi;
    }
    if (carry != 0 || bn_cmp(a, n, k) >= 0)
        bn_sub(a, n, k);
}

/*
 * CIOS Montgomery multiplication: t = a*b*R^-1 mod n, with R = 2^(32*k). Every intermediate
 * here is `t[j] + x*y + c` with all four words below 2^32, whose maximum is exactly 2^64 - 1,
 * so a u64 accumulator is not tight, it is exact.
 */
static void bn_montmul(u32 *out, const u32 *a, const u32 *b, const u32 *n,
                       u32 n0inv, int k)
{
    u32 t[MMO_RSA_MAX_LIMBS + 2];
    int i, j;

    memset(t, 0, sizeof(u32) * (size_t)(k + 2));
    for (i = 0; i < k; i++) {
        u64 c = 0;
        u32 m;

        for (j = 0; j < k; j++) {
            u64 s = (u64)t[j] + (u64)a[j] * b[i] + c;
            t[j] = (u32)s;
            c = s >> 32;
        }
        c += t[k];
        t[k] = (u32)c;
        t[k + 1] = (u32)(c >> 32);

        m = (u32)(t[0] * n0inv);
        c = (u64)t[0] + (u64)m * n[0];
        c >>= 32;
        for (j = 1; j < k; j++) {
            u64 s = (u64)t[j] + (u64)m * n[j] + c;
            t[j - 1] = (u32)s;
            c = s >> 32;
        }
        c += t[k];
        t[k - 1] = (u32)c;
        t[k] = t[k + 1] + (u32)(c >> 32);
    }
    if (t[k] != 0 || bn_cmp(t, n, k) >= 0)
        bn_sub(t, n, k);
    memcpy(out, t, sizeof(u32) * (size_t)k);
}

/* Big-endian bytes to little-endian limbs, zero-padded to k limbs. */
static void bn_from_be(u32 *x, int k, const u8 *b, size_t len)
{
    size_t i;

    memset(x, 0, sizeof(u32) * (size_t)k);
    for (i = 0; i < len; i++) {
        size_t bit = (len - 1 - i) * 8;
        size_t limb = bit / 32;

        if ((int)limb < k)
            x[limb] |= (u32)b[i] << (bit % 32);
    }
}

static void bn_to_be(const u32 *x, int k, u8 *b, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        size_t bit = (len - 1 - i) * 8;
        size_t limb = bit / 32;

        b[i] = (int)limb < k ? (u8)(x[limb] >> (bit % 32)) : 0;
    }
}

/*
 * The two Montgomery constants. n0inv comes from Newton's iteration, which doubles the correct
 * bit count each time and so needs five rounds to cover a 32-bit limb.
 */
static void mont_setup(mmo_rsa_pubkey *key)
{
    u32 inv = 1;
    int i;

    for (i = 0; i < 5; i++)
        inv *= 2u - key->n[0] * inv;
    key->n0inv = (u32)(0u - inv);

    memset(key->r2, 0, sizeof key->r2);
    key->r2[0] = 1;
    for (i = 0; i < 64 * key->limbs; i++)
        bn_dbl_mod(key->r2, key->n, key->limbs);
}

/* ====================================================================== */
/* the key                                                                 */
/* ====================================================================== */

int mmo_rsa_pubkey_der(const u8 *dr, size_t len, mmo_rsa_pubkey *key,
                       char *err, size_t errcap)
{
    der top = { dr, dr + len }, spki, alg, oid, bits, inner;
    const u8 *n, *e;
    size_t nlen, elen, i;

    memset(key, 0, sizeof *key);
    if (der_take(&top, 0x30, &spki) != 0)
        return fail(err, errcap, "not a DER SubjectPublicKeyInfo");
    if (der_take(&spki, 0x30, &alg) != 0)
        return fail(err, errcap, "public key has no algorithm identifier");
    oid = alg;
    if ((size_t)(oid.end - oid.p) < sizeof OID_RSA ||
        memcmp(oid.p, OID_RSA, sizeof OID_RSA) != 0)
        return fail(err, errcap, "public key is not rsaEncryption");
    if (der_take(&spki, 0x03, &bits) != 0 || bits.p == bits.end)
        return fail(err, errcap, "public key has no BIT STRING");
    if (*bits.p != 0x00)
        return fail(err, errcap, "public key BIT STRING is not byte-aligned");
    bits.p++;
    if (der_take(&bits, 0x30, &inner) != 0)
        return fail(err, errcap, "public key body is not a SEQUENCE");
    if (der_uint(&inner, &n, &nlen) != 0)
        return fail(err, errcap, "public key has no modulus");
    if (der_uint(&inner, &e, &elen) != 0)
        return fail(err, errcap, "public key has no exponent");

    if (nlen < 64 || nlen > MMO_RSA_MAX_BYTES)
        return fail(err, errcap, "unsupported modulus size");
    if (nlen % 4 != 0)
        return fail(err, errcap, "modulus is not a whole number of limbs");
    if ((n[nlen - 1] & 1) == 0)
        return fail(err, errcap, "modulus is even");
    if (elen == 0 || elen > 4)
        return fail(err, errcap, "unsupported public exponent");

    key->nbytes = (int)nlen;
    key->limbs = (int)(nlen / 4);
    bn_from_be(key->n, key->limbs, n, nlen);
    key->e = 0;
    for (i = 0; i < elen; i++)
        key->e = (key->e << 8) | e[i];
    if (key->e < 3 || (key->e & 1) == 0)
        return fail(err, errcap, "public exponent is not an odd value above 2");
    mont_setup(key);
    return 0;
}

int mmo_rsa_pubkey_text(const char *text, size_t len, mmo_rsa_pubkey *key,
                        char *err, size_t errcap)
{
    static const char BEGIN[] = "-----BEGIN";
    u8 der_buf[MMO_RSA_MAX_BYTES * 2 + 64];
    const char *p = text, *end = text + len, *q;
    int n;

    /* PEM armour if it is there, the bare literal if it is not. */
    q = memchr(p, '-', (size_t)(end - p));
    if (q != NULL && (size_t)(end - q) >= sizeof BEGIN - 1 &&
        memcmp(q, BEGIN, sizeof BEGIN - 1) == 0) {
        const char *nl = memchr(q, '\n', (size_t)(end - q));

        if (nl == NULL)
            return fail(err, errcap, "PEM header is not terminated");
        p = nl + 1;
        q = memchr(p, '-', (size_t)(end - p));
        if (q != NULL)
            end = q;
    }
    n = b64decode(p, (size_t)(end - p), der_buf, sizeof der_buf);
    if (n <= 0)
        return fail(err, errcap, "public key is not base64");
    return mmo_rsa_pubkey_der(der_buf, (size_t)n, key, err, errcap);
}

/* ====================================================================== */
/* the signature                                                           */
/* ====================================================================== */

/* The DigestInfo prefix for SHA-256 from RFC 8017 A.2.4, byte for byte. */
static const u8 SHA256_DIGESTINFO[] = {
    0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
    0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20
};

int mmo_rsa_verify_sha256(const mmo_rsa_pubkey *key,
                          const void *msg, size_t msglen,
                          const u8 *sig, size_t siglen)
{
    u32 s[MMO_RSA_MAX_LIMBS], x[MMO_RSA_MAX_LIMBS], one[MMO_RSA_MAX_LIMBS];
    u8 em[MMO_RSA_MAX_BYTES], want[MMO_RSA_MAX_BYTES];
    u8 digest[MMO_SHA256_DIGEST];
    size_t pslen, at;
    int bit, k;

    if (key->limbs == 0 || siglen != (size_t)key->nbytes)
        return 0;
    k = key->limbs;

    bn_from_be(s, k, sig, siglen);
    if (bn_cmp(s, key->n, k) >= 0)
        return 0;

    /* s^e mod n, with the exponent walked from its top bit down. */
    bn_montmul(x, s, key->r2, key->n, key->n0inv, k);   /* x = s*R mod n */
    memcpy(s, x, sizeof(u32) * (size_t)k);
    for (bit = 31; bit >= 0; bit--) {
        if ((key->e >> bit) & 1)
            break;
    }
    for (bit--; bit >= 0; bit--) {
        bn_montmul(x, x, x, key->n, key->n0inv, k);
        if ((key->e >> bit) & 1)
            bn_montmul(x, x, s, key->n, key->n0inv, k);
    }
    memset(one, 0, sizeof(u32) * (size_t)k);
    one[0] = 1;
    bn_montmul(x, x, one, key->n, key->n0inv, k);
    bn_to_be(x, k, em, (size_t)key->nbytes);

    /* The block that would have been signed, composed rather than parsed. */
    if ((size_t)key->nbytes < 11 + sizeof SHA256_DIGESTINFO + MMO_SHA256_DIGEST)
        return 0;
    pslen = (size_t)key->nbytes - 3 - sizeof SHA256_DIGESTINFO - MMO_SHA256_DIGEST;
    mmo_sha256(msg, msglen, digest);
    want[0] = 0x00;
    want[1] = 0x01;
    memset(want + 2, 0xff, pslen);
    at = 2 + pslen;
    want[at++] = 0x00;
    memcpy(want + at, SHA256_DIGESTINFO, sizeof SHA256_DIGESTINFO);
    at += sizeof SHA256_DIGESTINFO;
    memcpy(want + at, digest, MMO_SHA256_DIGEST);

    return memcmp(em, want, (size_t)key->nbytes) == 0;
}

int mmo_feed_hash_file(const char *path, char out[65])
{
    static const char HEX[] = "0123456789abcdef";
    mmo_sha256_ctx c;
    u8 digest[MMO_SHA256_DIGEST], buf[8192];
    FILE *f = fopen(path, "rb");
    size_t got;
    int i;

    if (f == NULL)
        return -1;
    mmo_sha256_init(&c);
    while ((got = fread(buf, 1, sizeof buf, f)) > 0)
        mmo_sha256_update(&c, buf, got);
    if (ferror(f) != 0) {
        fclose(f);
        return -1;
    }
    fclose(f);
    mmo_sha256_final(&c, digest);
    for (i = 0; i < MMO_SHA256_DIGEST; i++) {
        out[i * 2] = HEX[digest[i] >> 4];
        out[i * 2 + 1] = HEX[digest[i] & 0x0f];
    }
    out[64] = '\0';
    return 0;
}
