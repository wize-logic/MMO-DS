/* UTF-16LE and UTF-8 <-> engine charcode conversion. */
#include "charcode.h"
#include "text_channel.h"

/* Contiguous charcode ranges, from charmap.txt. */
#define CC_ZERO  0x0121u /* '0'..'9' -> 0x0121..0x012A */
#define CC_UPPER 0x012Bu /* 'A'..'Z' -> 0x012B..0x0144 */
#define CC_LOWER 0x0145u /* 'a'..'z' -> 0x0145..0x015E */

/*
 * Everything that is not a letter, a digit or the space: Unicode code point to engine
 * charcode, one row per glyph, both directions read from this one table.
 */
static const struct {
    uint32_t cp;
    uint16_t cc;
} kGlyphs[] = {
#include "charcode_glyphs.gen.h"
};
#define NGLYPHS (sizeof(kGlyphs) / sizeof(kGlyphs[0]))

/* One Unicode code point -> engine charcode, or 0 (CHAR_NONE, never a real
 * mapped glyph) when the engine has no glyph for it. */
static uint16_t cp_to_charcode(uint32_t cp)
{
    if (cp == 0x20)
        return MMO_CHAR_SPACE;
    if (cp >= '0' && cp <= '9')
        return (uint16_t)(CC_ZERO + (cp - '0'));
    if (cp >= 'A' && cp <= 'Z')
        return (uint16_t)(CC_UPPER + (cp - 'A'));
    if (cp >= 'a' && cp <= 'z')
        return (uint16_t)(CC_LOWER + (cp - 'a'));
    for (size_t i = 0; i < NGLYPHS; i++)
        if (kGlyphs[i].cp == cp)
            return kGlyphs[i].cc;
    return 0;
}

/* One engine charcode -> Unicode code point, or 0xFFFFFFFF when nothing maps
 * back (a control/format code, or a glyph outside the name repertoire). */
static uint32_t charcode_to_cp(uint16_t cc)
{
    if (cc == MMO_CHAR_SPACE)
        return 0x20;
    if (cc >= CC_ZERO && cc <= CC_ZERO + 9)
        return (uint32_t)('0' + (cc - CC_ZERO));
    if (cc >= CC_UPPER && cc <= CC_UPPER + 25)
        return (uint32_t)('A' + (cc - CC_UPPER));
    if (cc >= CC_LOWER && cc <= CC_LOWER + 25)
        return (uint32_t)('a' + (cc - CC_LOWER));
    for (size_t i = 0; i < NGLYPHS; i++)
        if (kGlyphs[i].cc == cc)
            return kGlyphs[i].cp;
    return 0xFFFFFFFFu;
}

mmo_charcode_result mmo_utf16le_to_charcode(const uint8_t *src, size_t src_bytes,
                                            mmo_charcode *dst, size_t dst_cap)
{
    mmo_charcode_result r = { 0, 0, 0 };
    size_t i = 0;

    /* Whole code units only; a trailing odd byte is not a unit. */
    size_t units = src_bytes / 2;
    for (size_t u = 0; u < units;) {
        uint32_t cp = (uint32_t)src[2 * u] | ((uint32_t)src[2 * u + 1] << 8);
        u++;
        if (cp == 0x0000)
            break; /* NUL terminator */

        int bad = 0;
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            /* High surrogate: needs a following low surrogate. */
            if (u < units) {
                uint32_t lo = (uint32_t)src[2 * u] | ((uint32_t)src[2 * u + 1] << 8);
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                    u++;
                } else {
                    bad = 1;
                }
            } else {
                bad = 1;
            }
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            bad = 1; /* lone low surrogate */
        }

        if (dst_cap == 0 || i >= dst_cap - 1) {
            r.truncated = 1;
            break;
        }

        uint16_t cc = bad ? 0 : cp_to_charcode(cp);
        if (cc == 0) {
            cc = MMO_CHAR_QUESTION;
            r.unmapped++;
        }
        dst[i++] = cc;
    }

    if (dst_cap > 0)
        dst[i] = MMO_CHAR_EOS;
    r.written = i;
    return r;
}

/* Append one code point as UTF-16LE if it fits; return bytes written, or 0 if
 * it did not fit (caller then flags truncation). */
static size_t put_utf16le(uint32_t cp, uint8_t *dst, size_t off, size_t cap)
{
    if (cp <= 0xFFFF) {
        if (off + 2 > cap)
            return 0;
        dst[off] = (uint8_t)(cp & 0xFF);
        dst[off + 1] = (uint8_t)(cp >> 8);
        return 2;
    }
    if (off + 4 > cap)
        return 0;
    cp -= 0x10000u;
    uint32_t hi = 0xD800u + (cp >> 10);
    uint32_t lo = 0xDC00u + (cp & 0x3FFu);
    dst[off] = (uint8_t)(hi & 0xFF);
    dst[off + 1] = (uint8_t)(hi >> 8);
    dst[off + 2] = (uint8_t)(lo & 0xFF);
    dst[off + 3] = (uint8_t)(lo >> 8);
    return 4;
}

mmo_charcode_result mmo_charcode_to_utf16le(const mmo_charcode *src,
                                            uint8_t *dst, size_t dst_cap_bytes)
{
    mmo_charcode_result r = { 0, 0, 0 };
    size_t off = 0;

    for (const mmo_charcode *p = src; *p != MMO_CHAR_EOS; p++) {
        uint32_t cp = charcode_to_cp(*p);
        if (cp == 0xFFFFFFFFu) {
            cp = 0xFFFDu; /* visible replacement character */
            r.unmapped++;
        }
        size_t n = put_utf16le(cp, dst, off, dst_cap_bytes);
        if (n == 0) {
            r.truncated = 1;
            break;
        }
        off += n;
        r.written++;
    }
    return r;
}

/* Append one code point as UTF-8 if it fits, terminator included; return bytes
 * written, or 0 if it did not fit. */
static size_t put_utf8(uint32_t cp, char *dst, size_t off, size_t cap)
{
    size_t n = cp < 0x80u ? 1 : cp < 0x800u ? 2 : cp < 0x10000u ? 3 : 4;

    if (off + n + 1 > cap)
        return 0;
    return openmmo_text_cp_to_utf8(cp, dst + off);
}

mmo_charcode_result mmo_charcode_to_utf8(const mmo_charcode *src, char *dst,
                                         size_t dst_cap)
{
    mmo_charcode_result r = { 0, 0, 0 };
    size_t off = 0;

    for (const mmo_charcode *p = src; *p != MMO_CHAR_EOS; p++) {
        uint32_t cp = charcode_to_cp(*p);
        if (cp == 0xFFFFFFFFu) {
            cp = 0xFFFDu; /* visible replacement character */
            r.unmapped++;
        }
        size_t n = put_utf8(cp, dst, off, dst_cap);
        if (n == 0) {
            r.truncated = 1;
            break;
        }
        off += n;
        r.written++;
    }
    if (dst_cap > 0)
        dst[off] = '\0';
    return r;
}

mmo_charcode_result mmo_utf8_to_charcode(const char *src, mmo_charcode *dst,
                                         size_t dst_cap)
{
    mmo_charcode_result r = { 0, 0, 0 };
    const uint8_t *p = (const uint8_t *)src;
    size_t i = 0;

    while (*p != '\0') {
        uint32_t cp;
        int extra;

        if (*p < 0x80) {
            cp = *p++;
            extra = 0;
        } else if ((*p & 0xE0) == 0xC0) {
            cp = (uint32_t)(*p++ & 0x1F);
            extra = 1;
        } else if ((*p & 0xF0) == 0xE0) {
            cp = (uint32_t)(*p++ & 0x0F);
            extra = 2;
        } else if ((*p & 0xF8) == 0xF0) {
            cp = (uint32_t)(*p++ & 0x07);
            extra = 3;
        } else {
            /* A continuation byte or an out-of-range lead: one bad code point,
             * one byte consumed, so a malformed string still terminates. */
            p++;
            cp = 0;
            extra = -1;
        }

        for (; extra > 0; extra--) {
            if ((*p & 0xC0) != 0x80) {
                extra = -1; /* truncated sequence; stop where it broke */
                break;
            }
            cp = (cp << 6) | (uint32_t)(*p++ & 0x3F);
        }

        if (dst_cap == 0 || i >= dst_cap - 1) {
            r.truncated = 1;
            break;
        }

        uint16_t cc = (extra < 0) ? 0 : cp_to_charcode(cp);
        if (cc == 0) {
            cc = MMO_CHAR_QUESTION;
            r.unmapped++;
        }
        dst[i++] = cc;
    }

    if (dst_cap > 0)
        dst[i] = MMO_CHAR_EOS;
    r.written = i;
    return r;
}

int mmo_name_fits_trainer(size_t glyphs)
{
    return glyphs <= MMO_TRAINER_NAME_LEN;
}
