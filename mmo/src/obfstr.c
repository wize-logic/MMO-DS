/* The two runtime halves of obfstr.h. */

#include <stddef.h>

#include "obfstr.h"

char *obfstr_decode(const unsigned char *enc, unsigned key,
                    char *buf, size_t cap)
{
    size_t i;

    if (buf == NULL || cap == 0)
        return buf;
    if (enc == NULL) {
        buf[0] = '\0';
        return buf;
    }

    for (i = 0; i + 1 < cap && i < (size_t)OBFSTR_MAX; i++) {
        char c = (char)(enc[i] ^ OBFSTR_KEYBYTE(key, i));

        buf[i] = c;
        if (c == '\0')
            return buf;
    }
    buf[i] = '\0';
    return buf;
}

void obfstr_wipe(char *buf, size_t cap)
{
    volatile char *p = (volatile char *)buf;

    if (buf == NULL)
        return;
    while (cap-- > 0)
        *p++ = '\0';
}
