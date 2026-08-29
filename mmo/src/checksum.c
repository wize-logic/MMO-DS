/* The CRC-16 and HMAC-SHA256 frame-checksum profiles. See checksum.h. */
#include "checksum.h"

#include <string.h>

#include "crypto.h"

/* CRC-16/ARC, computed bit-serially. This is the same reflected poly-0xA001,
 * init-0 CRC the server's table-driven Crc16Checksum uses; the bitwise form
 * needs no 256-entry table and pins to the identical answer. */
int mmo_checksum_supported(u8 size)
{
    return size == 0 || size == 2 || (size >= 4 && size <= MMO_CHECKSUM_MAX);
}

u16 mmo_crc16(const u8 *data, size_t n)
{
    u16 crc = 0;
    for (size_t i = 0; i < n; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (u16)((crc >> 1) ^ 0xA001) : (u16)(crc >> 1);
    }
    return crc;
}

/* HMAC-SHA256(key16, data || be32(round)) into the full 32-byte digest. */
static void hmac_sha256_round(const u8 *key, const u8 *data, size_t n,
                              u32 round, u8 out[MMO_SHA256_DIGEST])
{
    u8 k_ipad[MMO_SHA256_BLOCK], k_opad[MMO_SHA256_BLOCK];
    u8 be[4] = { (u8)(round >> 24), (u8)(round >> 16),
                 (u8)(round >> 8), (u8)round };
    u8 inner[MMO_SHA256_DIGEST];
    mmo_sha256_ctx c;

    memset(k_ipad, 0x36, sizeof k_ipad);
    memset(k_opad, 0x5c, sizeof k_opad);
    for (size_t i = 0; i < MMO_CHECKSUM_KEY; i++) {
        k_ipad[i] ^= key[i];
        k_opad[i] ^= key[i];
    }

    mmo_sha256_init(&c);
    mmo_sha256_update(&c, k_ipad, sizeof k_ipad);
    mmo_sha256_update(&c, data, n);
    mmo_sha256_update(&c, be, sizeof be);
    mmo_sha256_final(&c, inner);

    mmo_sha256_init(&c);
    mmo_sha256_update(&c, k_opad, sizeof k_opad);
    mmo_sha256_update(&c, inner, sizeof inner);
    mmo_sha256_final(&c, out);
}

size_t mmo_checksum_calc(u8 size, const u8 *key, u32 *round,
                         const u8 *data, size_t n, u8 tag[MMO_CHECKSUM_MAX])
{
    if (size == 0)
        return 0;
    if (size == 2) {
        u16 crc = mmo_crc16(data, n);
        tag[0] = (u8)(crc & 0xFF);
        tag[1] = (u8)((crc >> 8) & 0xFF);
        return 2;
    }
    if (size >= 4 && size <= MMO_CHECKSUM_MAX) {
        u8 full[MMO_SHA256_DIGEST];
        hmac_sha256_round(key, data, n, *round, full);
        (*round)++;
        memcpy(tag, full, size);
        return size;
    }
    return 0; /* unsupported profile: emit no tag rather than a wrong one */
}

int mmo_checksum_verify(u8 size, const u8 *key, u32 *round,
                        const u8 *data, size_t n,
                        const u8 *tag, size_t taglen)
{
    /* Expected tag length for the profile; SIZE_MAX marks an unsupported size. */
    size_t expect = (size == 0)               ? 0
                  : (size == 2)               ? 2
                  : (size >= 4 && size <= MMO_CHECKSUM_MAX) ? size
                  :                             (size_t)-1;

    /* A wrong length is rejected before any work, the server's verify does the
     * same up front, so it never advances its round counter on this path. */
    if (taglen != expect)
        return -1;
    if (expect == 0)
        return 0;

    u8 want[MMO_CHECKSUM_MAX];
    mmo_checksum_calc(size, key, round, data, n, want);
    return memcmp(want, tag, expect) == 0 ? 0 : -1;
}
