#ifndef OPENMMO_CHARCODE_H
#define OPENMMO_CHARCODE_H
/*
 * The name/text bridge between the server's UTF-16 and the engine's private glyph
 * encoding.
 */

#include <stddef.h>
#include <stdint.h>

/* Mirrors the engine's charcode_t (u16). A caller in the fused binary that
 * bridges to a real charcode_t buffer should assert, with the engine header in
 * scope, that sizeof(charcode_t) == sizeof(mmo_charcode). */
typedef uint16_t mmo_charcode;

#define MMO_CHAR_EOS      0xFFFFu /* engine CHAR_EOS: the string terminator */
#define MMO_CHAR_QUESTION 0x01ACu /* engine CHAR_QUESTION: the visible fallback glyph */
#define MMO_CHAR_SPACE    0x01DEu /* engine CHAR_SPACE: the half-width ASCII space */

#define MMO_TRAINER_NAME_LEN 7  /* engine TRAINER_NAME_LEN */
#define MMO_MON_NAME_LEN     10 /* engine MON_NAME_LEN */

typedef struct {
    size_t written;   /* glyphs (or code points) written, not counting any terminator */
    size_t unmapped;  /* source units with no glyph, rendered as the fallback */
    int    truncated; /* nonzero if the source did not fit and was cut short */
} mmo_charcode_result;

/*
 * Decode a server UTF-16LE name (src_bytes bytes; a U+0000 code unit or the end of the buffer
 * ends it) into an EOS-terminated engine charcode string. dst_cap counts charcode slots
 * including the EOS slot, so at most dst_cap-1 glyphs are written.
 */
mmo_charcode_result mmo_utf16le_to_charcode(const uint8_t *src, size_t src_bytes,
                                            mmo_charcode *dst, size_t dst_cap);

/* Encode an EOS-terminated engine charcode string back to UTF-16LE bytes (no
 * terminator written). dst_cap_bytes bounds the output. A charcode with no known
 * reverse mapping becomes U+FFFD and bumps result.unmapped. */
mmo_charcode_result mmo_charcode_to_utf16le(const mmo_charcode *src,
                                            uint8_t *dst, size_t dst_cap_bytes);

/*
 * Encode an EOS-terminated engine charcode string as NUL-terminated UTF-8. dst_cap counts
 * bytes, the terminator included; dst is terminated whenever dst_cap > 0.
 */
mmo_charcode_result mmo_charcode_to_utf8(const mmo_charcode *src, char *dst,
                                         size_t dst_cap);

/* As above, from a NUL-terminated UTF-8 string. This is the entry point for
 * text that reaches the client as host bytes rather than off the wire, a
 * configured player name, a command line. A malformed sequence is one unmapped
 * code point and never runs off the end of the string. */
mmo_charcode_result mmo_utf8_to_charcode(const char *src, mmo_charcode *dst,
                                         size_t dst_cap);

/* True if a name of `glyphs` glyphs fits an engine trainer-name field. */
int mmo_name_fits_trainer(size_t glyphs);

#endif /* OPENMMO_CHARCODE_H */
