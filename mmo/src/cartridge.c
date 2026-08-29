/* The slot table, and the sentence a refused image earns. */
#include <stdio.h>
#include <string.h>

#include "cartridge.h"

#include "cartridges.gen.h"

int mmo_cartridge_count(void)
{
    return MMO_CARTRIDGE_COUNT;
}

const MmoCartridge *mmo_cartridge_at(int index)
{
    if (index < 0 || index >= MMO_CARTRIDGE_COUNT)
        return NULL;
    return &MMO_CARTRIDGES[index];
}

const MmoCartridge *mmo_cartridge_by_code(const char *code)
{
    int i;

    if (!code)
        return NULL;
    for (i = 0; i < MMO_CARTRIDGE_COUNT; i++) {
        if (strncmp(MMO_CARTRIDGES[i].code, code, 4) == 0)
            return &MMO_CARTRIDGES[i];
    }
    return NULL;
}

const MmoCartridge *mmo_cartridge_slot_of(const char *code)
{
    const MmoCartridge *exact;
    int i;

    if (!code)
        return NULL;
    /* An exact row first: a build we have read is a better answer about its
     * own slot than another build of the same game. */
    exact = mmo_cartridge_by_code(code);
    if (exact)
        return exact;
    for (i = 0; i < MMO_CARTRIDGE_COUNT; i++) {
        if (strncmp(MMO_CARTRIDGES[i].code, code, 3) == 0)
            return &MMO_CARTRIDGES[i];
    }
    return NULL;
}

const char *mmo_cartridge_language(char region)
{
    int i;

    for (i = 0; i < MMO_CARTRIDGE_LANG_COUNT; i++) {
        if (MMO_CARTRIDGE_LANGS[i].region == region)
            return MMO_CARTRIDGE_LANGS[i].name;
    }
    return NULL;
}

int mmo_cartridge_serves(const MmoCartridge *cart, const char *kind)
{
    const char *p;
    size_t n;

    if (!cart || !kind || cart->status != MMO_CART_READ)
        return 0;
    n = strlen(kind);
    if (n == 0)
        return 0;
    /* The kind list is comma separated and short. Match whole entries, so
     * "poke" never answers for "pokemon". */
    for (p = cart->kinds; *p != '\0'; ) {
        const char *end = strchr(p, ',');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == n && strncmp(p, kind, n) == 0)
            return 1;
        if (!end)
            break;
        p = end + 1;
    }
    return 0;
}

int mmo_cartridge_refusal(const char *code, char *buf, size_t n)
{
    const MmoCartridge *cart;
    const char *lang;

    if (!buf || n == 0 || !code)
        return -1;
    buf[0] = '\0';

    cart = mmo_cartridge_by_code(code);
    if (cart && cart->status == MMO_CART_READ)
        return -1;

    if (cart) {
        switch (cart->status) {
        case MMO_CART_HOST:
            return snprintf(buf, n,
                            "%.4s is the game's own image. It is what this "
                            "client plays, not something to import from.",
                            code);
        case MMO_CART_EXTRACTS:
            return snprintf(buf, n,
                            "This client can read %s but not draw what it "
                            "reads, so nothing is taken from it.",
                            cart->name);
        case MMO_CART_REFUSES:
            return snprintf(buf, n,
                            "%s does not store its content the way this "
                            "client's loaders read it.",
                            cart->name);
        default:
            break;
        }
    }

    /* Either an unread row, or a build of a game we know with no row of its
     * own. Both are the same sentence, and both name the slot. */
    cart = mmo_cartridge_slot_of(code);
    if (cart) {
        lang = mmo_cartridge_language(code[3]);
        if (lang)
            return snprintf(buf, n,
                            "The %s build of %s is not one this client has "
                            "read (%.4s).",
                            lang, cart->name, code);
        return snprintf(buf, n,
                        "That build of %s is not one this client has read "
                        "(%.4s).",
                        cart->name, code);
    }

    return snprintf(buf, n,
                    "%.4s is not a cartridge this client knows.", code);
}
