/* NIST P-256 (secp256r1) ECDH and ECDSA-verify, vendored for -m32. */
#include "p256.h"

#include <string.h>

#include "crypto.h"

/* ---- big integers: eight 32-bit limbs, little-endian ------------------ */

typedef u32 bn[8];

/* The curve constants, big-endian, exactly as published for secp256r1. */
static const u8 P_BE[32] = {
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff };
static const u8 N_BE[32] = {   /* group order */
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xbc,0xe6,0xfa,0xad,0xa7,0x17,0x9e,0x84,0xf3,0xb9,0xca,0xc2,0xfc,0x63,0x25,0x51 };
static const u8 B_BE[32] = {   /* curve coefficient b */
    0x5a,0xc6,0x35,0xd8,0xaa,0x3a,0x93,0xe7,0xb3,0xeb,0xbd,0x55,0x76,0x98,0x86,0xbc,
    0x65,0x1d,0x06,0xb0,0xcc,0x53,0xb0,0xf6,0x3b,0xce,0x3c,0x3e,0x27,0xd2,0x60,0x4b };
static const u8 GX_BE[32] = {
    0x6b,0x17,0xd1,0xf2,0xe1,0x2c,0x42,0x47,0xf8,0xbc,0xe6,0xe5,0x63,0xa4,0x40,0xf2,
    0x77,0x03,0x7d,0x81,0x2d,0xeb,0x33,0xa0,0xf4,0xa1,0x39,0x45,0xd8,0x98,0xc2,0x96 };
static const u8 GY_BE[32] = {
    0x4f,0xe3,0x42,0xe2,0xfe,0x1a,0x7f,0x9b,0x8e,0xe7,0xeb,0x4a,0x7c,0x0f,0x9e,0x16,
    0x2b,0xce,0x33,0x57,0x6b,0x31,0x5e,0xce,0xcb,0xb6,0x40,0x68,0x37,0xbf,0x51,0xf5 };

static void bn_zero(bn r) { memset(r, 0, sizeof(bn)); }
static void bn_copy(bn r, const bn a) { memcpy(r, a, sizeof(bn)); }

static int bn_is_zero(const bn a)
{
    u32 x = 0;
    for (int i = 0; i < 8; i++) x |= a[i];
    return x == 0;
}

/* -1 if a<b, 0 if a==b, 1 if a>b. */
static int bn_cmp(const bn a, const bn b)
{
    for (int i = 7; i >= 0; i--)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

/* r = a + b; returns the carry out of the top limb. */
static u32 bn_add(bn r, const bn a, const bn b)
{
    u64 c = 0;
    for (int i = 0; i < 8; i++) {
        c += (u64)a[i] + b[i];
        r[i] = (u32)c;
        c >>= 32;
    }
    return (u32)c;
}

/* r = a - b; returns the borrow out of the top limb (1 if a<b). */
static u32 bn_sub(bn r, const bn a, const bn b)
{
    u64 borrow = 0;
    for (int i = 0; i < 8; i++) {
        u64 t = (u64)a[i] - b[i] - borrow;
        r[i] = (u32)t;
        borrow = (t >> 32) & 1;
    }
    return (u32)borrow;
}

static void bn_from_be(bn r, const u8 be[32])
{
    for (int i = 0; i < 8; i++) {
        const u8 *p = be + (7 - i) * 4;
        r[i] = (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
    }
}

static void bn_to_be(const bn a, u8 be[32])
{
    for (int i = 0; i < 8; i++) {
        u8 *p = be + (7 - i) * 4;
        p[0] = (u8)(a[i] >> 24); p[1] = (u8)(a[i] >> 16);
        p[2] = (u8)(a[i] >> 8);  p[3] = (u8)a[i];
    }
}

/* ---- modular arithmetic, modulus passed in ---------------------------- */

/* out[16] = a * b, schoolbook. */
static void bn_mul_wide(u32 out[16], const bn a, const bn b)
{
    memset(out, 0, 16 * sizeof(u32));
    for (int i = 0; i < 8; i++) {
        u64 carry = 0;
        for (int j = 0; j < 8; j++) {
            u64 t = (u64)a[i] * b[j] + out[i + j] + carry;
            out[i + j] = (u32)t;
            carry = t >> 32;
        }
        out[i + 8] += (u32)carry;
    }
}

/* r = t mod m, where t is a 512-bit value and m a 256-bit modulus. Bit-by-bit
 * shift-and-subtract: the remainder is at most 257 bits mid-loop, so it lives
 * in nine limbs. */
static void bn_mod_wide(bn r, const u32 t[16], const bn m)
{
    u32 rem[9];
    memset(rem, 0, sizeof rem);
    for (int i = 511; i >= 0; i--) {
        /* rem <<= 1 */
        u32 carry = 0;
        for (int k = 0; k < 9; k++) {
            u32 next = rem[k] >> 31;
            rem[k] = (rem[k] << 1) | carry;
            carry = next;
        }
        rem[0] |= (t[i >> 5] >> (i & 31)) & 1u;
        /* if rem >= m, rem -= m (rem[8] is 0 or 1; a set top limb is always >=) */
        int ge;
        if (rem[8]) ge = 1;
        else {
            ge = 1;
            for (int k = 7; k >= 0; k--) {
                if (rem[k] != m[k]) { ge = rem[k] > m[k]; break; }
            }
        }
        if (ge) {
            u64 borrow = 0;
            for (int k = 0; k < 8; k++) {
                u64 x = (u64)rem[k] - m[k] - borrow;
                rem[k] = (u32)x;
                borrow = (x >> 32) & 1;
            }
            rem[8] -= (u32)borrow;
        }
    }
    for (int k = 0; k < 8; k++) r[k] = rem[k];
}

static void bn_mulmod(bn r, const bn a, const bn b, const bn m)
{
    u32 t[16];
    bn_mul_wide(t, a, b);
    bn_mod_wide(r, t, m);
}

/* r = a + b mod m, with a,b < m. */
static void bn_addmod(bn r, const bn a, const bn b, const bn m)
{
    u32 carry = bn_add(r, a, b);
    if (carry || bn_cmp(r, m) >= 0) bn_sub(r, r, m);
}

/* r = a - b mod m, with a,b < m. */
static void bn_submod(bn r, const bn a, const bn b, const bn m)
{
    if (bn_sub(r, a, b)) bn_add(r, r, m);   /* borrowed: add m back */
}

/* r = a^-1 mod m for prime m, via Fermat: a^(m-2). Square-and-multiply over the
 * bits of (m-2), most significant first. */
static void bn_invmod(bn r, const bn a, const bn m)
{
    bn e, two, base, acc;
    bn_zero(two); two[0] = 2;
    bn_sub(e, m, two);              /* e = m - 2 */
    bn_copy(base, a);
    bn_zero(acc); acc[0] = 1;
    for (int i = 255; i >= 0; i--) {
        bn_mulmod(acc, acc, acc, m);
        if ((e[i >> 5] >> (i & 31)) & 1u)
            bn_mulmod(acc, acc, base, m);
    }
    bn_copy(r, acc);
}

/* ---- curve points, Jacobian coordinates (X:Y:Z), identity == Z zero --- */

typedef struct { bn x, y, z; } jpt;

static bn P, N, B;   /* filled by curve_init() */
static jpt G;
static int inited;

static void curve_init(void)
{
    if (inited) return;
    bn_from_be(P, P_BE);
    bn_from_be(N, N_BE);
    bn_from_be(B, B_BE);
    bn_from_be(G.x, GX_BE);
    bn_from_be(G.y, GY_BE);
    bn_zero(G.z); G.z[0] = 1;
    inited = 1;
}

static int jpt_is_inf(const jpt *p) { return bn_is_zero(p->z); }
static void jpt_set_inf(jpt *p) { bn_zero(p->x); bn_zero(p->y); bn_zero(p->z); }

/* R = 2*P (Jacobian, a = -3), formula dbl-2001-b. Alias-safe: written last. */
static void jpt_double(jpt *R, const jpt *P1)
{
    if (jpt_is_inf(P1) || bn_is_zero(P1->y)) { jpt_set_inf(R); return; }

    bn delta, gamma, beta, alpha, t0, t1, x3, y3, z3;
    bn_mulmod(delta, P1->z, P1->z, P);          /* delta = Z^2 */
    bn_mulmod(gamma, P1->y, P1->y, P);          /* gamma = Y^2 */
    bn_mulmod(beta, P1->x, gamma, P);           /* beta  = X*gamma */

    bn_submod(t0, P1->x, delta, P);             /* X - delta */
    bn_addmod(t1, P1->x, delta, P);             /* X + delta */
    bn_mulmod(alpha, t0, t1, P);                /* (X-d)(X+d) */
    bn_addmod(t0, alpha, alpha, P);
    bn_addmod(alpha, t0, alpha, P);             /* alpha = 3*(X-d)(X+d) */

    bn_mulmod(x3, alpha, alpha, P);             /* alpha^2 */
    bn_addmod(t0, beta, beta, P);
    bn_addmod(t0, t0, t0, P);                   /* 4*beta */
    bn_addmod(t1, t0, t0, P);                   /* 8*beta */
    bn_submod(x3, x3, t1, P);                   /* X3 = alpha^2 - 8*beta */

    bn_addmod(z3, P1->y, P1->z, P);
    bn_mulmod(z3, z3, z3, P);
    bn_submod(z3, z3, gamma, P);
    bn_submod(z3, z3, delta, P);                /* Z3 = (Y+Z)^2 - gamma - delta */

    bn_submod(y3, t0, x3, P);                   /* 4*beta - X3 */
    bn_mulmod(y3, alpha, y3, P);
    bn_mulmod(t1, gamma, gamma, P);             /* gamma^2 */
    bn_addmod(t1, t1, t1, P);
    bn_addmod(t1, t1, t1, P);
    bn_addmod(t1, t1, t1, P);                   /* 8*gamma^2 */
    bn_submod(y3, y3, t1, P);                   /* Y3 = alpha*(4b-X3) - 8*gamma^2 */

    bn_copy(R->x, x3); bn_copy(R->y, y3); bn_copy(R->z, z3);
}

/* R = P1 + P2 (Jacobian + Jacobian), formula add-2007-bl. Alias-safe. */
static void jpt_add(jpt *R, const jpt *P1, const jpt *P2)
{
    if (jpt_is_inf(P1)) { *R = *P2; return; }
    if (jpt_is_inf(P2)) { *R = *P1; return; }

    bn z1z1, z2z2, u1, u2, s1, s2, h, i, j, r, v, t0, x3, y3, z3;
    bn_mulmod(z1z1, P1->z, P1->z, P);
    bn_mulmod(z2z2, P2->z, P2->z, P);
    bn_mulmod(u1, P1->x, z2z2, P);              /* U1 = X1*Z2^2 */
    bn_mulmod(u2, P2->x, z1z1, P);              /* U2 = X2*Z1^2 */
    bn_mulmod(s1, P1->y, P2->z, P);
    bn_mulmod(s1, s1, z2z2, P);                 /* S1 = Y1*Z2^3 */
    bn_mulmod(s2, P2->y, P1->z, P);
    bn_mulmod(s2, s2, z1z1, P);                 /* S2 = Y2*Z1^3 */

    bn_submod(h, u2, u1, P);                    /* H = U2 - U1 */
    bn_submod(r, s2, s1, P);                    /* (S2 - S1) */
    if (bn_is_zero(h)) {
        if (bn_is_zero(r)) { jpt_double(R, P1); return; }  /* P1 == P2 */
        jpt_set_inf(R); return;                            /* P1 == -P2 */
    }
    bn_addmod(r, r, r, P);                      /* r = 2*(S2 - S1) */

    bn_addmod(t0, h, h, P);
    bn_mulmod(i, t0, t0, P);                    /* I = (2H)^2 */
    bn_mulmod(j, h, i, P);                      /* J = H*I */
    bn_mulmod(v, u1, i, P);                     /* V = U1*I */

    bn_mulmod(x3, r, r, P);
    bn_submod(x3, x3, j, P);
    bn_submod(x3, x3, v, P);
    bn_submod(x3, x3, v, P);                    /* X3 = r^2 - J - 2V */

    bn_submod(y3, v, x3, P);
    bn_mulmod(y3, r, y3, P);
    bn_mulmod(t0, s1, j, P);
    bn_addmod(t0, t0, t0, P);
    bn_submod(y3, y3, t0, P);                   /* Y3 = r*(V-X3) - 2*S1*J */

    bn_addmod(z3, P1->z, P2->z, P);
    bn_mulmod(z3, z3, z3, P);
    bn_submod(z3, z3, z1z1, P);
    bn_submod(z3, z3, z2z2, P);
    bn_mulmod(z3, z3, h, P);                    /* Z3 = ((Z1+Z2)^2-Z1Z1-Z2Z2)*H */

    bn_copy(R->x, x3); bn_copy(R->y, y3); bn_copy(R->z, z3);
}

/* R = k*P via left-to-right double-and-add over the 256 bits of k. */
static void jpt_mul(jpt *R, const bn k, const jpt *P1)
{
    jpt acc;
    jpt_set_inf(&acc);
    for (int i = 255; i >= 0; i--) {
        jpt_double(&acc, &acc);
        if ((k[i >> 5] >> (i & 31)) & 1u)
            jpt_add(&acc, &acc, P1);
    }
    *R = acc;
}

/* Affine coordinates of P into x/y (x only if y is NULL). Fails on identity. */
static int jpt_affine(bn x, bn y, const jpt *p)
{
    if (jpt_is_inf(p)) return -1;
    bn zi, z2;
    bn_invmod(zi, p->z, P);
    bn_mulmod(z2, zi, zi, P);
    bn_mulmod(x, p->x, z2, P);
    if (y) {
        bn z3;
        bn_mulmod(z3, z2, zi, P);
        bn_mulmod(y, p->y, z3, P);
    }
    return 0;
}

/* Load and validate an uncompressed point 0x04||X||Y: X,Y in [0,p) and on the
 * curve y^2 == x^3 - 3x + b. Returns 0 on success, -1 otherwise. */
static int jpt_from_uncompressed(jpt *out, const u8 in[MMO_P256_POINT])
{
    if (in[0] != 0x04) return -1;
    bn x, y, lhs, rhs, t;
    bn_from_be(x, in + 1);
    bn_from_be(y, in + 33);
    if (bn_cmp(x, P) >= 0 || bn_cmp(y, P) >= 0) return -1;

    bn_mulmod(lhs, y, y, P);                    /* y^2 */
    bn_mulmod(rhs, x, x, P);
    bn_mulmod(rhs, rhs, x, P);                  /* x^3 */
    bn_addmod(t, x, x, P);
    bn_addmod(t, t, x, P);                      /* 3x */
    bn_submod(rhs, rhs, t, P);                  /* x^3 - 3x */
    bn_addmod(rhs, rhs, B, P);                  /* x^3 - 3x + b */
    if (bn_cmp(lhs, rhs) != 0) return -1;

    bn_copy(out->x, x); bn_copy(out->y, y);
    bn_zero(out->z); out->z[0] = 1;
    return 0;
}

/* A scalar is valid iff 0 < k < n. */
static int scalar_ok(const bn k)
{
    return !bn_is_zero(k) && bn_cmp(k, N) < 0;
}

/* ---- public API ------------------------------------------------------- */

int mmo_p256_derive_pub(const u8 priv[MMO_P256_SCALAR], u8 pub[MMO_P256_POINT])
{
    curve_init();
    bn k;
    bn_from_be(k, priv);
    if (!scalar_ok(k)) return -1;

    jpt q;
    jpt_mul(&q, k, &G);
    bn x, y;
    if (jpt_affine(x, y, &q) != 0) return -1;
    pub[0] = 0x04;
    bn_to_be(x, pub + 1);
    bn_to_be(y, pub + 33);
    return 0;
}

int mmo_p256_ecdh(const u8 priv[MMO_P256_SCALAR],
                  const u8 peer_pub[MMO_P256_POINT],
                  u8 shared[MMO_P256_SCALAR])
{
    curve_init();
    bn k;
    bn_from_be(k, priv);
    if (!scalar_ok(k)) return -1;

    jpt peer, s;
    if (jpt_from_uncompressed(&peer, peer_pub) != 0) return -1;
    jpt_mul(&s, k, &peer);
    bn x;
    if (jpt_affine(x, NULL, &s) != 0) return -1;   /* small-subgroup / identity */
    bn_to_be(x, shared);
    return 0;
}

/* Parse a DER-encoded INTEGER at *pp (bounded by end) into r. Advances *pp.
 * Returns 0 on success, -1 on malformed input. Rejects negative integers. */
static int der_read_int(const u8 **pp, const u8 *end, bn r)
{
    const u8 *p = *pp;
    if (p >= end || *p++ != 0x02) return -1;          /* INTEGER tag */
    if (p >= end) return -1;
    size_t len = *p++;
    if (len & 0x80) return -1;                        /* only short form: r,s < 128B */
    if (len == 0 || (size_t)(end - p) < len) return -1;
    if (p[0] & 0x80) return -1;                       /* negative: not a valid r/s */
    /* skip a single leading zero used only to keep the value positive */
    if (len > 1 && p[0] == 0x00) { p++; len--; }
    if (len > 32) return -1;
    u8 be[32];
    memset(be, 0, sizeof be);
    memcpy(be + (32 - len), p, len);
    bn_from_be(r, be);
    *pp = p + len;
    return 0;
}

int mmo_p256_ecdsa_verify(const u8 pub[MMO_P256_POINT],
                          const void *msg, size_t msglen,
                          const void *sig, size_t siglen)
{
    curve_init();

    /* Parse SEQUENCE { INTEGER r, INTEGER s }. */
    const u8 *p = sig, *end = (const u8 *)sig + siglen;
    if (siglen < 2 || *p++ != 0x30) return 0;         /* SEQUENCE tag */
    size_t seqlen = *p++;
    if (seqlen & 0x80) return 0;                      /* r+s always < 128 bytes */
    if ((size_t)(end - p) != seqlen) return 0;
    bn r, s;
    if (der_read_int(&p, end, r) != 0) return 0;
    if (der_read_int(&p, end, s) != 0) return 0;
    if (p != end) return 0;                           /* trailing garbage */

    if (bn_is_zero(r) || bn_cmp(r, N) >= 0) return 0;
    if (bn_is_zero(s) || bn_cmp(s, N) >= 0) return 0;

    /* e = SHA-256(msg) as an integer (reduced mod n by the multiplies). */
    u8 h[MMO_SHA256_DIGEST];
    mmo_sha256(msg, msglen, h);
    bn e;
    bn_from_be(e, h);

    jpt q;
    if (jpt_from_uncompressed(&q, pub) != 0) return 0;

    bn w, u1, u2;
    bn_invmod(w, s, N);
    bn_mulmod(u1, e, w, N);
    bn_mulmod(u2, r, w, N);

    jpt a, b, sum;
    jpt_mul(&a, u1, &G);
    jpt_mul(&b, u2, &q);
    jpt_add(&sum, &a, &b);

    bn x;
    if (jpt_affine(x, NULL, &sum) != 0) return 0;     /* R == identity */
    /* v = x mod n; valid iff v == r */
    if (bn_cmp(x, N) >= 0) bn_sub(x, x, N);
    return bn_cmp(x, r) == 0;
}
