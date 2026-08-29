/* The UTF-16LE <-> engine charcode name bridge. */
#include <stdio.h>
#include <string.h>

#include "charcode.h"

static int failures;

#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) {                                                             \
            printf("  ok   %s\n", msg);                                         \
        } else {                                                                \
            printf("  FAIL %s\n", msg);                                         \
            failures++;                                                         \
        }                                                                       \
    } while (0)

/* Build a UTF-16LE byte buffer (no terminator) from a code-point array. */
static size_t utf16le(const uint32_t *cps, size_t n, uint8_t *out)
{
    size_t off = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t cp = cps[i];
        if (cp <= 0xFFFF) {
            out[off++] = (uint8_t)(cp & 0xFF);
            out[off++] = (uint8_t)(cp >> 8);
        } else {
            cp -= 0x10000u;
            uint32_t hi = 0xD800u + (cp >> 10), lo = 0xDC00u + (cp & 0x3FFu);
            out[off++] = (uint8_t)(hi & 0xFF);
            out[off++] = (uint8_t)(hi >> 8);
            out[off++] = (uint8_t)(lo & 0xFF);
            out[off++] = (uint8_t)(lo >> 8);
        }
    }
    return off;
}

static void test_pinned_glyphs(void)
{
    printf("pinned charcodes match the engine charmap:\n");
    struct {
        uint32_t cp;
        mmo_charcode cc;
        const char *what;
    } cases[] = {
        { 'A', 0x012B, "'A' -> 0x012B" },  { 'Z', 0x0144, "'Z' -> 0x0144" },
        { 'a', 0x0145, "'a' -> 0x0145" },  { 'z', 0x015E, "'z' -> 0x015E" },
        { '0', 0x0121, "'0' -> 0x0121" },  { '9', 0x012A, "'9' -> 0x012A" },
        { ' ', 0x01DE, "space -> 0x01DE" }, { '?', 0x01AC, "'?' -> 0x01AC" },
        { '!', 0x01AB, "'!' -> 0x01AB" },  { '_', 0x01E9, "'_' -> 0x01E9" },
        { '@', 0x01D0, "'@' -> 0x01D0" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint8_t in[4];
        size_t nb = utf16le(&cases[i].cp, 1, in);
        mmo_charcode out[4];
        mmo_charcode_result r = mmo_utf16le_to_charcode(in, nb, out, 4);
        CHECK(r.written == 1 && !r.truncated && r.unmapped == 0 &&
                  out[0] == cases[i].cc && out[1] == MMO_CHAR_EOS,
              cases[i].what);
    }
}

static void test_roundtrip(void)
{
    printf("ASCII names round-trip through charcode and back:\n");
    const char *names[] = { "Red", "player_1", "AshK", "Gold99", "a b c" };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        const char *s = names[i];
        size_t n = strlen(s);
        uint32_t cps[32];
        for (size_t k = 0; k < n; k++)
            cps[k] = (uint8_t)s[k];

        uint8_t in[64];
        size_t nb = utf16le(cps, n, in);
        mmo_charcode cc[40];
        mmo_charcode_result r = mmo_utf16le_to_charcode(in, nb, cc, 40);
        CHECK(r.written == n && !r.truncated && r.unmapped == 0,
              "decoded every glyph with no loss");

        uint8_t back[64];
        mmo_charcode_result r2 = mmo_charcode_to_utf16le(cc, back, sizeof(back));
        CHECK(r2.written == n && !r2.truncated && r2.unmapped == 0 &&
                  memcmp(back, in, nb) == 0,
              "re-encoded UTF-16LE is byte-identical to the source");
    }
}

static void test_eos_and_terminator(void)
{
    printf("output is always EOS-terminated:\n");
    uint32_t cps[3] = { 'H', 'i', '!' };
    uint8_t in[8];
    size_t nb = utf16le(cps, 3, in);
    mmo_charcode out[8];
    mmo_charcode_result r = mmo_utf16le_to_charcode(in, nb, out, 8);
    CHECK(r.written == 3 && out[3] == MMO_CHAR_EOS, "EOS follows the last glyph");

    /* An empty name is just the terminator. */
    mmo_charcode e[4];
    mmo_charcode_result re = mmo_utf16le_to_charcode(in, 0, e, 4);
    CHECK(re.written == 0 && e[0] == MMO_CHAR_EOS, "empty name is a bare EOS");
}

static void test_truncation(void)
{
    printf("a name too long for the field is truncated and flagged:\n");
    /* An 11-glyph name into a trainer-sized 7+1 buffer. */
    const char *s = "Bulbasaurus"; /* 11 chars */
    size_t n = strlen(s);
    uint32_t cps[16];
    for (size_t k = 0; k < n; k++)
        cps[k] = (uint8_t)s[k];
    uint8_t in[32];
    size_t nb = utf16le(cps, n, in);

    mmo_charcode out[MMO_TRAINER_NAME_LEN + 1];
    mmo_charcode_result r =
        mmo_utf16le_to_charcode(in, nb, out, MMO_TRAINER_NAME_LEN + 1);
    CHECK(r.truncated && r.written == MMO_TRAINER_NAME_LEN,
          "kept 7 glyphs and set the truncated flag");
    CHECK(out[MMO_TRAINER_NAME_LEN] == MMO_CHAR_EOS,
          "the truncated result is still EOS-terminated");
    CHECK(out[0] == 0x012C /* 'B' */ && out[6] == 0x0145 /* 'a' */,
          "the kept glyphs are the leading ones");
    CHECK(mmo_name_fits_trainer(7) && !mmo_name_fits_trainer(8),
          "the fit test agrees with TRAINER_NAME_LEN");
}

static void test_unmapped(void)
{
    printf("a code point with no engine glyph becomes a visible '?':\n");
    /* '[' (0x5B) has no charmap glyph; U+4E00 (CJK) has none either. */
    uint32_t cps[3] = { 'A', 0x5B, 0x4E00 };
    uint8_t in[8];
    size_t nb = utf16le(cps, 3, in);
    mmo_charcode out[8];
    mmo_charcode_result r = mmo_utf16le_to_charcode(in, nb, out, 8);
    CHECK(r.written == 3 && r.unmapped == 2 && !r.truncated,
          "two unmapped code points are counted, not dropped");
    CHECK(out[0] == 0x012B && out[1] == MMO_CHAR_QUESTION &&
              out[2] == MMO_CHAR_QUESTION,
          "each unmapped glyph is CHAR_QUESTION");
}

static void test_surrogates(void)
{
    printf("astral and malformed surrogates are one unmapped glyph each:\n");
    /* U+1F600 (emoji): a valid surrogate pair, but no engine glyph. */
    uint32_t emoji = 0x1F600;
    uint8_t in[8];
    size_t nb = utf16le(&emoji, 1, in);
    mmo_charcode out[8];
    mmo_charcode_result r = mmo_utf16le_to_charcode(in, nb, out, 8);
    CHECK(r.written == 1 && r.unmapped == 1 && out[0] == MMO_CHAR_QUESTION,
          "a valid astral pair is one unmapped glyph");

    /* A lone high surrogate with no low surrogate following. */
    uint8_t lone[2] = { 0x3D, 0xD8 }; /* 0xD83D */
    mmo_charcode_result r2 = mmo_utf16le_to_charcode(lone, 2, out, 8);
    CHECK(r2.written == 1 && r2.unmapped == 1 && out[0] == MMO_CHAR_QUESTION,
          "a lone surrogate is one unmapped glyph, not a crash");
}

static void test_reverse_unmapped(void)
{
    printf("a charcode with no reverse mapping encodes as U+FFFD:\n");
    /* 0x0200 is a control/format code, not a name glyph. */
    mmo_charcode src[3] = { 0x012B /* A */, 0x0200, MMO_CHAR_EOS };
    uint8_t out[16];
    mmo_charcode_result r = mmo_charcode_to_utf16le(src, out, sizeof(out));
    CHECK(r.written == 2 && r.unmapped == 1,
          "the unmapped charcode is counted");
    CHECK(out[2] == 0xFD && out[3] == 0xFF,
          "it is emitted as U+FFFD, not silently skipped");
}

static void test_zero_cap(void)
{
    printf("a zero-capacity destination is safe:\n");
    uint32_t cp = 'A';
    uint8_t in[4];
    size_t nb = utf16le(&cp, 1, in);
    mmo_charcode_result r = mmo_utf16le_to_charcode(in, nb, NULL, 0);
    CHECK(r.written == 0 && r.truncated, "no room reported as truncation");
}

/* The accented Latin set. Every one of these is a row generated straight out of
 * the engine's charmap, and every one of them used to convert to '?', which is
 * the defect this range is the regression for: an accented username was not
 * unrenderable, it was unmapped. The charcodes are read from charmap.txt. */
static void test_accented_latin(void)
{
    printf("the accented Latin a European name needs has real glyphs:\n");
    struct {
        uint32_t cp;
        mmo_charcode cc;
        const char *what;
    } cases[] = {
        { 0x00C0, 0x015F, "'A-grave' -> 0x015F" },
        { 0x00C9, 0x0168, "'E-acute' -> 0x0168" },
        { 0x00D1, 0x0170, "'N-tilde' -> 0x0170" },
        { 0x00DF, 0x017E, "'sharp s' -> 0x017E" },
        { 0x00E9, 0x0188, "'e-acute' -> 0x0188" },
        { 0x00F1, 0x0190, "'n-tilde' -> 0x0190" },
        { 0x00FC, 0x019B, "'u-diaeresis' -> 0x019B" },
        { 0x00FF, 0x019E, "'y-diaeresis' -> 0x019E" },
        { 0x0152, 0x019F, "'OE ligature' -> 0x019F" },
        { 0x015F, 0x01A2, "'s-cedilla' -> 0x01A2" },
        { 0x00B0, 0x01E8, "'degree' -> 0x01E8" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint8_t in[4];
        size_t nb = utf16le(&cases[i].cp, 1, in);
        mmo_charcode out[4];
        mmo_charcode_result r = mmo_utf16le_to_charcode(in, nb, out, 4);
        CHECK(r.written == 1 && r.unmapped == 0 && out[0] == cases[i].cc,
              cases[i].what);

        /* And back, because the reverse direction reads the same table and a
         * row that only works one way is a row that will strand a name. */
        uint8_t back[8];
        mmo_charcode src[2] = { cases[i].cc, MMO_CHAR_EOS };
        mmo_charcode_result rr = mmo_charcode_to_utf16le(src, back, sizeof back);
        uint32_t cp = (uint32_t)back[0] | ((uint32_t)back[1] << 8);
        CHECK(rr.written == 1 && rr.unmapped == 0 && cp == cases[i].cp,
              "and the same glyph converts back to the same code point");
    }
}

/* The UTF-8 entry point. Names that reach the client as host bytes rather than
 * off the wire come in through this one, so it has to agree with the UTF-16
 * path glyph for glyph and it has to survive malformed input. */
static void test_utf8(void)
{
    printf("UTF-8 names convert to the same glyphs as UTF-16 ones:\n");

    mmo_charcode out[40];
    mmo_charcode_result r = mmo_utf8_to_charcode("\xC3\x9C" "n\xC3\xAF" "c\xC3\xB6" "d\xC3\xA9", out, 40);
    CHECK(r.written == 7 && r.unmapped == 0 && !r.truncated,
          "a seven-glyph accented name is seven glyphs, none unmapped");
    CHECK(out[0] == 0x017B && out[2] == 0x018E && out[4] == 0x0195 &&
              out[6] == 0x0188 && out[7] == MMO_CHAR_EOS,
          "and every accented glyph is the charmap's charcode");

    /* Three bytes, one code point, and the same answer as the UTF-16 path. */
    uint32_t cp = 0x2642; /* male sign */
    uint8_t u16[4];
    size_t nb = utf16le(&cp, 1, u16);
    mmo_charcode via16[4];
    mmo_utf16le_to_charcode(u16, nb, via16, 4);
    mmo_charcode via8[4];
    mmo_utf8_to_charcode("\xE2\x99\x82", via8, 4);
    CHECK(via8[0] == via16[0] && via8[0] == 0x01BB,
          "a three-byte sequence and its UTF-16 form agree");

    /* Four bytes: outside the BMP, so outside the font. Unmapped, not dropped,
     * and not consumed as four separate characters. */
    mmo_charcode astral[4];
    mmo_charcode_result ra = mmo_utf8_to_charcode("\xF0\x9F\x92\xA9", astral, 4);
    CHECK(ra.written == 1 && ra.unmapped == 1 && astral[0] == MMO_CHAR_QUESTION,
          "an astral code point is one unmapped glyph, not four");

    /* A lone continuation byte and a truncated sequence: one bad code point
     * each, and the loop still terminates. */
    mmo_charcode bad[8];
    mmo_charcode_result rb = mmo_utf8_to_charcode("A\x80" "B", bad, 8);
    CHECK(rb.written == 3 && rb.unmapped == 1 && bad[1] == MMO_CHAR_QUESTION,
          "a stray continuation byte is one '?' between two real letters");

    mmo_charcode cut[8];
    mmo_charcode_result rc = mmo_utf8_to_charcode("A\xC3", cut, 8);
    CHECK(rc.written == 2 && rc.unmapped == 1 && cut[2] == MMO_CHAR_EOS,
          "a sequence cut off at the end of the string terminates");

    /* Truncation is flagged, exactly as it is on the UTF-16 path. */
    mmo_charcode small[4];
    mmo_charcode_result rt = mmo_utf8_to_charcode("ABCDEF", small, 4);
    CHECK(rt.written == 3 && rt.truncated && small[3] == MMO_CHAR_EOS,
          "a name longer than the destination is cut short and says so");
}

int charcode_tests_run(void)
{
    failures = 0;
    test_pinned_glyphs();
    test_roundtrip();
    test_eos_and_terminator();
    test_truncation();
    test_unmapped();
    test_surrogates();
    test_reverse_unmapped();
    test_zero_cap();
    test_accented_latin();
    test_utf8();

    if (failures)
        printf("charcode: %d check(s) FAILED\n", failures);
    else
        printf("charcode: all checks passed\n");
    return failures;
}
