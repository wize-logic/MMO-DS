/* SHA-256, HMAC-SHA256 and AES-128-CTR, vendored for the -m32 build. */
#include "crypto.h"

#include <string.h>

/* ====================================================================== */
/* SHA-256 (FIPS 180-4)                                                    */
/* ====================================================================== */

static const u32 SHA_K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static u32 rotr(u32 x, unsigned n) { return (x >> n) | (x << (32 - n)); }

static void sha256_block(mmo_sha256_ctx *c, const u8 *p)
{
    u32 w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (u32)p[i * 4] << 24 | (u32)p[i * 4 + 1] << 16 |
               (u32)p[i * 4 + 2] << 8 | (u32)p[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        u32 s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        u32 s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    u32 a = c->state[0], b = c->state[1], cc = c->state[2], d = c->state[3];
    u32 e = c->state[4], f = c->state[5], g = c->state[6], h = c->state[7];
    for (int i = 0; i < 64; i++) {
        u32 S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        u32 ch = (e & f) ^ (~e & g);
        u32 t1 = h + S1 + ch + SHA_K[i] + w[i];
        u32 S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        u32 maj = (a & b) ^ (a & cc) ^ (b & cc);
        u32 t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->state[0] += a; c->state[1] += b; c->state[2] += cc; c->state[3] += d;
    c->state[4] += e; c->state[5] += f; c->state[6] += g; c->state[7] += h;
}

void mmo_sha256_init(mmo_sha256_ctx *c)
{
    c->state[0] = 0x6a09e667; c->state[1] = 0xbb67ae85;
    c->state[2] = 0x3c6ef372; c->state[3] = 0xa54ff53a;
    c->state[4] = 0x510e527f; c->state[5] = 0x9b05688c;
    c->state[6] = 0x1f83d9ab; c->state[7] = 0x5be0cd19;
    c->total = 0;
    c->used = 0;
}

void mmo_sha256_update(mmo_sha256_ctx *c, const void *data, size_t n)
{
    const u8 *p = data;
    c->total += n;
    while (n > 0) {
        size_t take = MMO_SHA256_BLOCK - c->used;
        if (take > n) take = n;
        memcpy(c->buf + c->used, p, take);
        c->used += take;
        p += take;
        n -= take;
        if (c->used == MMO_SHA256_BLOCK) {
            sha256_block(c, c->buf);
            c->used = 0;
        }
    }
}

void mmo_sha256_final(mmo_sha256_ctx *c, u8 out[MMO_SHA256_DIGEST])
{
    u64 bits = c->total * 8;
    u8 pad = 0x80;
    mmo_sha256_update(c, &pad, 1);
    u8 zero = 0;
    while (c->used != 56)
        mmo_sha256_update(c, &zero, 1);
    u8 len[8];
    for (int i = 0; i < 8; i++)
        len[i] = (u8)(bits >> (56 - i * 8));
    mmo_sha256_update(c, len, 8);      /* triggers the final block */

    for (int i = 0; i < 8; i++) {
        out[i * 4]     = (u8)(c->state[i] >> 24);
        out[i * 4 + 1] = (u8)(c->state[i] >> 16);
        out[i * 4 + 2] = (u8)(c->state[i] >> 8);
        out[i * 4 + 3] = (u8)(c->state[i]);
    }
}

void mmo_sha256(const void *data, size_t n, u8 out[MMO_SHA256_DIGEST])
{
    mmo_sha256_ctx c;
    mmo_sha256_init(&c);
    mmo_sha256_update(&c, data, n);
    mmo_sha256_final(&c, out);
}

/* ====================================================================== */
/* SHA-1 (FIPS 180-4), password digest only, never the transport            */
/* ====================================================================== */

static u32 rotl(u32 x, unsigned n) { return (x << n) | (x >> (32 - n)); }

static void sha1_block(u32 h[5], const u8 p[64])
{
    u32 w[80];
    for (int t = 0; t < 16; t++)
        w[t] = (u32)p[t * 4] << 24 | (u32)p[t * 4 + 1] << 16 |
               (u32)p[t * 4 + 2] << 8 | (u32)p[t * 4 + 3];
    for (int t = 16; t < 80; t++)
        w[t] = rotl(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);

    u32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int t = 0; t < 80; t++) {
        u32 f, k;
        if (t < 20)      { f = (b & c) | (~b & d);          k = 0x5a827999; }
        else if (t < 40) { f = b ^ c ^ d;                   k = 0x6ed9eba1; }
        else if (t < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
        else             { f = b ^ c ^ d;                   k = 0xca62c1d6; }
        u32 tmp = rotl(a, 5) + f + e + k + w[t];
        e = d; d = c; c = rotl(b, 30); b = a; a = tmp;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void mmo_sha1(const void *data, size_t n, u8 out[MMO_SHA1_DIGEST])
{
    u32 h[5] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 };
    const u8 *msg = data;
    u64 bits = (u64)n * 8;

    /* Feed every complete 64-byte block. */
    size_t i = 0;
    for (; n - i >= 64; i += 64)
        sha1_block(h, msg + i);

    /* Final block(s): the tail, a 0x80 marker, then the 64-bit big-endian bit
     * length in the last 8 bytes, spilling into a second block if the tail
     * leaves no room for the length. */
    u8 block[64];
    size_t rem = n - i;
    memset(block, 0, sizeof block);
    memcpy(block, msg + i, rem);
    block[rem] = 0x80;
    if (rem >= 56) {
        sha1_block(h, block);
        memset(block, 0, sizeof block);
    }
    for (int j = 0; j < 8; j++)
        block[63 - j] = (u8)(bits >> (j * 8));
    sha1_block(h, block);

    for (int j = 0; j < 5; j++) {
        out[j * 4]     = (u8)(h[j] >> 24);
        out[j * 4 + 1] = (u8)(h[j] >> 16);
        out[j * 4 + 2] = (u8)(h[j] >> 8);
        out[j * 4 + 3] = (u8)h[j];
    }
}

void mmo_sha1_hex(const void *data, size_t n, char out[41])
{
    static const char HEX[] = "0123456789abcdef";
    u8 d[MMO_SHA1_DIGEST];
    mmo_sha1(data, n, d);
    for (int i = 0; i < MMO_SHA1_DIGEST; i++) {
        out[i * 2]     = HEX[d[i] >> 4];
        out[i * 2 + 1] = HEX[d[i] & 0xf];
    }
    out[40] = '\0';
}

/* ====================================================================== */
/* HMAC-SHA256 (RFC 2104)                                                  */
/* ====================================================================== */

void mmo_hmac_sha256(const void *key, size_t keylen,
                     const void *data, size_t datalen,
                     u8 out[MMO_SHA256_DIGEST])
{
    u8 k[MMO_SHA256_BLOCK];
    memset(k, 0, sizeof k);
    if (keylen > MMO_SHA256_BLOCK)
        mmo_sha256(key, keylen, k);    /* long keys are hashed down first */
    else
        memcpy(k, key, keylen);

    u8 ipad[MMO_SHA256_BLOCK], opad[MMO_SHA256_BLOCK];
    for (int i = 0; i < MMO_SHA256_BLOCK; i++) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }

    u8 inner[MMO_SHA256_DIGEST];
    mmo_sha256_ctx c;
    mmo_sha256_init(&c);
    mmo_sha256_update(&c, ipad, sizeof ipad);
    mmo_sha256_update(&c, data, datalen);
    mmo_sha256_final(&c, inner);

    mmo_sha256_init(&c);
    mmo_sha256_update(&c, opad, sizeof opad);
    mmo_sha256_update(&c, inner, sizeof inner);
    mmo_sha256_final(&c, out);
}

/* ====================================================================== */
/* AES-128 (FIPS 197), forward direction only, for CTR keystream          */
/* ====================================================================== */

static const u8 AES_SBOX[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b,
    0xfe, 0xd7, 0xab, 0x76, 0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
    0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26,
    0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2,
    0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
    0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed,
    0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f,
    0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
    0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec,
    0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14,
    0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
    0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d,
    0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f,
    0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
    0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11,
    0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f,
    0xb0, 0x54, 0xbb, 0x16,
};

static const u8 AES_RCON[10] = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36,
};

static u32 aes_subword(u32 x)
{
    return (u32)AES_SBOX[x >> 24] << 24 | (u32)AES_SBOX[(x >> 16) & 0xff] << 16 |
           (u32)AES_SBOX[(x >> 8) & 0xff] << 8 | (u32)AES_SBOX[x & 0xff];
}

static void aes128_key_expand(u32 rk[44], const u8 key[16])
{
    for (int i = 0; i < 4; i++)
        rk[i] = (u32)key[i * 4] << 24 | (u32)key[i * 4 + 1] << 16 |
                (u32)key[i * 4 + 2] << 8 | (u32)key[i * 4 + 3];
    for (int i = 4; i < 44; i++) {
        u32 t = rk[i - 1];
        if (i % 4 == 0)
            t = aes_subword((t << 8) | (t >> 24)) ^ ((u32)AES_RCON[i / 4 - 1] << 24);
        rk[i] = rk[i - 4] ^ t;
    }
}

static u8 xtime(u8 x) { return (u8)((x << 1) ^ ((x >> 7) * 0x1b)); }

/* Encipher one 16-byte block in place through the standard AES round. */
static void aes128_encrypt_block(const u32 rk[44], u8 s[16])
{
    /* AddRoundKey (round 0) */
    for (int c = 0; c < 4; c++) {
        u32 k = rk[c];
        s[c * 4]     ^= (u8)(k >> 24);
        s[c * 4 + 1] ^= (u8)(k >> 16);
        s[c * 4 + 2] ^= (u8)(k >> 8);
        s[c * 4 + 3] ^= (u8)(k);
    }

    for (int round = 1; round <= 10; round++) {
        /* SubBytes */
        for (int i = 0; i < 16; i++)
            s[i] = AES_SBOX[s[i]];

        /* ShiftRows: state is column-major (s[col*4 + row]); row r rotates
         * left by r positions across the four columns. */
        u8 t[16];
        for (int col = 0; col < 4; col++)
            for (int row = 0; row < 4; row++)
                t[col * 4 + row] = s[((col + row) % 4) * 4 + row];
        memcpy(s, t, 16);

        /* MixColumns, except on the final round */
        if (round != 10) {
            for (int col = 0; col < 4; col++) {
                u8 *p = s + col * 4;
                u8 a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
                p[0] = (u8)(xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3);
                p[1] = (u8)(a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3);
                p[2] = (u8)(a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3));
                p[3] = (u8)((xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3));
            }
        }

        /* AddRoundKey */
        for (int c = 0; c < 4; c++) {
            u32 k = rk[round * 4 + c];
            s[c * 4]     ^= (u8)(k >> 24);
            s[c * 4 + 1] ^= (u8)(k >> 16);
            s[c * 4 + 2] ^= (u8)(k >> 8);
            s[c * 4 + 3] ^= (u8)(k);
        }
    }
}

void mmo_aes128ctr_init(mmo_aes128ctr *c,
                        const u8 key[MMO_AES128_KEY],
                        const u8 iv[MMO_AES_BLOCK])
{
    aes128_key_expand(c->rk, key);
    memcpy(c->counter, iv, MMO_AES_BLOCK);
    c->offset = MMO_AES_BLOCK;      /* force a fresh block on the first byte */
}

/* Increment the 128-bit counter block as one big-endian integer, matching
 * Java's AES/CTR/NoPadding. */
static void ctr_incr(u8 ctr[MMO_AES_BLOCK])
{
    for (int i = MMO_AES_BLOCK - 1; i >= 0; i--)
        if (++ctr[i] != 0)
            break;
}

void mmo_aes128ctr_xor(mmo_aes128ctr *c, const void *in, void *out, size_t n)
{
    const u8 *ip = in;
    u8 *op = out;
    for (size_t i = 0; i < n; i++) {
        if (c->offset == MMO_AES_BLOCK) {
            memcpy(c->keystream, c->counter, MMO_AES_BLOCK);
            aes128_encrypt_block(c->rk, c->keystream);
            ctr_incr(c->counter);
            c->offset = 0;
        }
        op[i] = (u8)(ip[i] ^ c->keystream[c->offset++]);
    }
}
