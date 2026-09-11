/* The server<->engine id translation chokepoint. See idmap.h. */
#include "idmap.h"
#include "region.h"

/* The item ids the server and the engine both have. Regenerate with
 * tools/gen_idmap_items.py; do not hand-edit. */
#include "idmap_items.gen.h"

/* How long each text bank is. Regenerate with tools/gen_idmap_text.py; do not
 * hand-edit. */
#include "idmap_text.gen.h"

static u16 trap(const char **why, const char *msg)
{
    if (why)
        *why = msg;
    return MMO_ID_NONE;
}

static u16 ok(const char **why, u16 id)
{
    if (why)
        *why = 0;
    return id;
}

u16 mmo_id_species_from_server(u16 dex, const char **why)
{
    if (dex == 0)
        return trap(why, "species: id 0 is not a species");
    if (dex > MMO_SPECIES_MAX)
        return trap(why, "species: National Dex number past the last species either half names");
    return ok(why, dex);
}

u16 mmo_id_species_to_server(u16 ds, const char **why)
{
    /* The engine numbers species by National Dex too, so the range is the same
     * in both directions. Whether the client has a picture for one is a
     * different question and a different bound; see the header. */
    if (ds == 0)
        return trap(why, "species: id 0 is not a species");
    if (ds > MMO_SPECIES_MAX)
        return trap(why, "species: engine id past the National Dex range");
    return ok(why, ds);
}

u16 mmo_id_move_from_server(u16 move, const char **why)
{
    if (move == 0)
        return trap(why, "move: id 0 is not a move");
    if (move > MMO_MOVE_MAX)
        return trap(why, "move: server sent an id past the last move the engine has");
    return ok(why, move);
}

u16 mmo_id_move_to_server(u16 ds, const char **why)
{
    if (ds == 0)
        return trap(why, "move: id 0 is not a move");
    if (ds > MMO_MOVE_MAX)
        return trap(why, "move: engine id past the last platinum move");
    return ok(why, ds);
}

/* The shared table is emitted in ascending order, so this is a binary search. */
static int item_shared(u16 id)
{
    unsigned lo = 0, hi = MMO_ITEM_SHARED_COUNT;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        if (MMO_ITEM_SHARED[mid] == id)
            return 1;
        if (MMO_ITEM_SHARED[mid] < id)
            lo = mid + 1;
        else
            hi = mid;
    }
    return 0;
}

u16 mmo_id_item_from_server(u16 wire, const char **why)
{
    unsigned region = wire / MMO_ITEM_REGION_MUL;
    unsigned index = wire % MMO_ITEM_REGION_MUL;
    if (region != MMO_ITEM_REGION)
        return trap(why, "item: wire id is not in the region-5 block");
    if (index == 0)
        return trap(why, "item: index 0 is not an item");
    if (!item_shared((u16)index))
        return trap(why, "item: the server's item id has no item in the engine");
    return ok(why, (u16)index);
}

u16 mmo_id_item_to_server(u16 ds, const char **why)
{
    if (ds == 0)
        return trap(why, "item: id 0 is not an item");
    if (!item_shared(ds))
        return trap(why, "item: the engine's item has no id the server knows");
    return ok(why, (u16)(MMO_ITEM_REGION * MMO_ITEM_REGION_MUL + ds));
}

/* Text. */
int mmo_id_text_from_server(u32 wire, u16 *bank, u16 *entry, const char **why)
{
    unsigned region = (wire >> MMO_TEXT_REGION_SHIFT) & 0xFu;
    unsigned b = (wire >> MMO_TEXT_BANK_SHIFT) & MMO_TEXT_BANK_MASK;
    unsigned e = wire & MMO_TEXT_ENTRY_MASK;

    if (region != MMO_TEXT_REGION) {
        trap(why, "text: id is not in the region this engine has strings for");
        return -1;
    }
    if (b >= MMO_TEXT_BANK_COUNT) {
        trap(why, "text: bank is past the end of the engine's text archive");
        return -1;
    }
    if (MMO_TEXT_BANK_SIZE[b] == 0) {
        trap(why, "text: bank is one the engine builds, so its length is unknown here");
        return -1;
    }
    if (e >= MMO_TEXT_BANK_SIZE[b]) {
        trap(why, "text: message is past the end of its bank");
        return -1;
    }
    if (bank)
        *bank = (u16)b;
    if (entry)
        *entry = (u16)e;
    ok(why, 0);
    return 0;
}

int mmo_id_text_to_server(u16 bank, u16 entry, u32 *wire, const char **why)
{
    if (bank >= MMO_TEXT_BANK_COUNT) {
        trap(why, "text: bank is past the end of the engine's text archive");
        return -1;
    }
    if (MMO_TEXT_BANK_SIZE[bank] == 0) {
        trap(why, "text: bank is one the engine builds, so its length is unknown here");
        return -1;
    }
    if (entry >= MMO_TEXT_BANK_SIZE[bank]) {
        trap(why, "text: message is past the end of its bank");
        return -1;
    }
    if (wire)
        *wire = ((u32)MMO_TEXT_REGION << MMO_TEXT_REGION_SHIFT)
              | ((u32)bank << MMO_TEXT_BANK_SHIFT) | (u32)entry;
    ok(why, 0);
    return 0;
}

/* Is this a bank the engine's text archive actually has? */
int mmo_id_text_bank_valid(int bank)
{
    return bank >= 0 && bank < MMO_TEXT_BANK_COUNT;
}

/* A Platinum header is the two map bytes concatenated. Twinleaf bedroom is
 * 415, so bank 1 map 159. Only a region this engine draws has a header at
 * all: a GBA bank/map is a different id space, and guessing one would seat
 * the avatar on the wrong map. */
int mmo_id_map_header_from_server(int region, int bank, int map, const char **why)
{
    int header;

    if (!mmo_region_is_drawable(region)) {
        trap(why, "map: region is not one this engine can draw");
        return -1;
    }
    if (bank < 0 || bank > 255 || map < 0 || map > 255) {
        trap(why, "map: bank or map is not a byte");
        return -1;
    }
    header = (bank << 8) | map;
    if (header > MMO_MAP_HEADER_MAX) {
        trap(why, "map: header is past the last one the engine draws");
        return -1;
    }
    ok(why, 0);
    return header;
}
