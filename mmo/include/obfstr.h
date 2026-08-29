/* String literals that are not in the binary in clear. */
#ifndef MMO_OBFSTR_H
#define MMO_OBFSTR_H

#include <stddef.h>

/* The longest literal this header carries, NUL included. Every obfuscated
 * array is this size whatever the literal, so the length of the string is not
 * on show either. */
#define OBFSTR_MAX 64

/* Seconds past midnight of the build, from __TIME__ ("HH:MM:SS"). Indexing a
 * string literal in a constant expression is what makes the whole file work;
 * gcc and clang fold it, and mingw is gcc. */
#define OBFSTR_TIME_SEED                          \
    ((unsigned)(__TIME__[0] - '0') * 36000u +     \
     (unsigned)(__TIME__[1] - '0') *  3600u +     \
     (unsigned)(__TIME__[3] - '0') *   600u +     \
     (unsigned)(__TIME__[4] - '0') *    60u +     \
     (unsigned)(__TIME__[6] - '0') *    10u +     \
     (unsigned)(__TIME__[7] - '0'))

/* xorshift32, three steps, spelled as macros so the compiler folds it. The
 * `| 1u` is why there is no zero case to test for: xorshift fixes zero. */
#define OBFSTR__X1(k) ((k) ^ ((k) << 13))
#define OBFSTR__X2(k) ((k) ^ ((k) >> 17))
#define OBFSTR__X3(k) ((k) ^ ((k) <<  5))

/* One site's key. It moves with the build clock, so two releases do not carry
 * the same ciphertext for the same address, and a signature taken off one
 * binary does not find it in the next. */
#define OBFSTR_KEY(site)                                                   \
    (OBFSTR__X3(OBFSTR__X2(OBFSTR__X1(                                     \
        ((OBFSTR_TIME_SEED ^ ((unsigned)(site) * 0x9E3779B9u)) | 1u)))))

/* The key byte index i is masked with. */
#define OBFSTR_KEYBYTE(key, i)                                             \
    ((unsigned char)(((((unsigned)(key)) >> ((((unsigned)(i)) & 3u) * 8u)) \
                      ^ (((unsigned)(i)) * 0x1Fu + 0x2Bu)) & 0xFFu))

/* Byte i of the literal, or 0 past its end. The index is clamped inside the
 * subscript as well as tested outside it: the false arm of a conditional is
 * still parsed, and a constant subscript past the end of the literal is an
 * error whether or not it is ever taken. */
#define OBFSTR__AT(s, i)                                                   \
    ((unsigned char)((size_t)(i) < sizeof (s)                              \
                     ? (s)[(size_t)(i) < sizeof (s) ? (i) : 0] : 0))

#define OBFSTR__B(s, key, i)                                               \
    ((unsigned char)(OBFSTR__AT(s, i) ^ OBFSTR_KEYBYTE(key, i)))

#define OBFSTR__B8(s, key, b)                                              \
    OBFSTR__B(s, key, (b) + 0), OBFSTR__B(s, key, (b) + 1),                \
    OBFSTR__B(s, key, (b) + 2), OBFSTR__B(s, key, (b) + 3),                \
    OBFSTR__B(s, key, (b) + 4), OBFSTR__B(s, key, (b) + 5),                \
    OBFSTR__B(s, key, (b) + 6), OBFSTR__B(s, key, (b) + 7)

/* The initialiser for an `unsigned char [OBFSTR_MAX]`. */
#define OBFSTR_BYTES(s, key)                                               \
    OBFSTR__B8(s, key,  0), OBFSTR__B8(s, key,  8),                        \
    OBFSTR__B8(s, key, 16), OBFSTR__B8(s, key, 24),                        \
    OBFSTR__B8(s, key, 32), OBFSTR__B8(s, key, 40),                        \
    OBFSTR__B8(s, key, 48), OBFSTR__B8(s, key, 56)

/* Refuse a literal that does not fit, where it is written rather than at the
 * far end of a truncated string. C99 has no _Static_assert; a negative array
 * bound is the check every compiler here already understands. */
#define OBFSTR_FITS(name, s)                                               \
    typedef char name[1 - 2 * !((int)sizeof (s) <= OBFSTR_MAX)]

/* Undo it into `buf`, NUL-terminated, and return buf. Never writes more than
 * cap bytes and never reads past OBFSTR_MAX. */
char *obfstr_decode(const unsigned char *enc, unsigned key,
                    char *buf, size_t cap);

/* Overwrite a decoded buffer. Written through a volatile pointer so that the
 * store to a dead local survives the optimiser, which is the whole point of
 * it, and the one thing a plain memset is allowed to drop. */
void obfstr_wipe(char *buf, size_t cap);

#endif /* MMO_OBFSTR_H */
