/* The engine bag and shop are displays of server state. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bag.h"
#include "constants/heap.h"
#include "constants/items.h"
#include "field/field_system.h"
#include "field_system.h"
#include "field_task.h"
#include "item_use_functions.h"
#include "overlay007/shop_menu.h"
#include "savedata.h"
#include "unk_0203D1B8.h"

#include "../../../include/client.h"
#include "../../../include/idmap.h"

static openmmo_client *s_client;
static int s_bag_seat;
static int s_bag_display;
static int s_bag_dirty;
static int s_bag_seated;
static int s_shop_pending;
static int s_shop_opened;

extern void openmmo_party_mark_touched(void); /* openmmo_encounter.c */

/* The one Bag that is a display of the server's (set by seat_full). A
 * different Bag pointer is a copy the engine rolled for itself, the battle's
 * DTO bag, a tutorial's demo bag. */
static Bag *s_display_bag;

static int is_display_bag(const void *p)
{
    const char *b = (const char *)s_display_bag;
    const char *q = p;

    if (s_display_bag == NULL)
        return 1;
    return q >= b && q < b + sizeof(Bag);
}

void openmmo_bag_attach(openmmo_client *c)
{
    s_client = c;
    s_bag_dirty = 1;
    s_bag_seated = 0;
    s_shop_pending = 0;
    s_shop_opened = 0;
}

int openmmo_bag_may_write(void)
{
    const openmmo_world_state *ws;

    if (s_bag_seat || s_bag_display)
        return 1;
    if (s_client == NULL)
        return 1;
    ws = openmmo_client_world_state(s_client);
    if (ws == NULL || !ws->valid)
        return 1;
    return 0;
}

void openmmo_bag_mark_dirty(void)
{
    s_bag_dirty = 1;
}

/*
 * A refused bag write is the engine saying what should have happened, a potion drunk, a ball
 * thrown, an item tossed, a script's gift, so it is reported as a delta for the server's bag,
 * whose answer is what the display then shows.
 */
static void report_bag_delta(u16 engine_item, int delta)
{
    const openmmo_shop *shop;
    const char *why = NULL;
    u16 wire;

    if (s_client == NULL)
        return;
    shop = openmmo_client_shop(s_client);
    if (shop != NULL && shop->valid && shop->open)
        return;
    wire = mmo_id_item_to_server(engine_item, &why);
    if (wire == MMO_ID_NONE) {
        printf("openmmo: bag delta for engine item %u x%d has no wire id (%s)\n",
               (unsigned)engine_item, delta,
               why != NULL ? why : "untranslatable");
        return;
    }
    if (openmmo_client_send_bag_delta(s_client, (int)wire, delta) == 0)
        printf("openmmo: bag %s engine item %u x%d (wire %u)\n",
               delta < 0 ? "consumed" : "gained",
               (unsigned)engine_item, delta < 0 ? -delta : delta,
               (unsigned)wire);
}

/*
 * Bag_RegisterItem's hook: the bag screen registered a key item to Y, or cleared it
 * (ITEM_NONE).
 */
void openmmo_bag_registered(void *bag, u32 item)
{
    const char *why = NULL;
    u16 wire = 0;

    /* may_write covers the seat itself (s_bag_seat) and a sessionless boot,
     * both of which are the engine's own business and not a report. */
    if (openmmo_bag_may_write() || s_client == NULL || !is_display_bag(bag))
        return;
    if (item != 0) {
        wire = mmo_id_item_to_server((u16)item, &why);
        if (wire == MMO_ID_NONE) {
            printf("openmmo: registered engine item %u has no wire id (%s)\n",
                   (unsigned)item, why != NULL ? why : "untranslatable");
            return;
        }
    }
    if (openmmo_client_send_registered_item(s_client, (int)wire) == 0)
        printf("openmmo: registered item %s (wire %u)\n",
               item == 0 ? "cleared" : "recorded", (unsigned)wire);
}

/* The two gates the bag patch calls. Both report the delta and then let the write proceed. */
/*
 * ADDS report only from the display bag: a script's gift lands there, while an add to a COPY
 * is setup plumbing, Dawn's catch demo seeds its demo bag with twenty balls, and reporting
 * that granted the player twenty balls for a scene that says five.
 */
int openmmo_bag_gate_add(void *bag, u16 item, u16 count)
{
    if (!openmmo_bag_may_write() && is_display_bag(bag))
        report_bag_delta(item, (int)count);
    return 1;
}

int openmmo_bag_gate_remove(void *bag, u16 item, u16 count)
{
    (void)bag;
    if (!openmmo_bag_may_write()) {
        report_bag_delta(item, -(int)count);
        openmmo_party_mark_touched();
    }
    return 1;
}

void openmmo_shop_mark_pending(void)
{
    s_shop_pending = 1;
}

static const char *pocket_name(u32 pocket)
{
    switch (pocket) {
    case POCKET_ITEMS:
        return "items";
    case POCKET_MEDICINE:
        return "medicine";
    case POCKET_BALLS:
        return "balls";
    case POCKET_TMHMS:
        return "tmhm";
    case POCKET_BERRIES:
        return "berries";
    case POCKET_MAIL:
        return "mail";
    case POCKET_BATTLE_ITEMS:
        return "battle";
    case POCKET_KEY_ITEMS:
        return "key";
    default:
        return "unknown";
    }
}

static u16 pocket_size(u32 pocket)
{
    switch (pocket) {
    case POCKET_ITEMS:
        return ITEM_POCKET_SIZE;
    case POCKET_MEDICINE:
        return MEDICINE_POCKET_SIZE;
    case POCKET_BALLS:
        return POKEBALL_POCKET_SIZE;
    case POCKET_TMHMS:
        return TMHM_POCKET_SIZE;
    case POCKET_BERRIES:
        return BERRY_POCKET_SIZE;
    case POCKET_MAIL:
        return MAIL_POCKET_SIZE;
    case POCKET_BATTLE_ITEMS:
        return BATTLE_ITEM_POCKET_SIZE;
    case POCKET_KEY_ITEMS:
        return KEY_ITEM_POCKET_SIZE;
    default:
        return 0;
    }
}

/* The bag draws engine ids. An unmapped stack, or one whose engine id is
 * not the Gen-5 index the wire used, is refused: seating Potion's gba id
 * (13) would add a Dusk Ball. */
static int refuse_engine_item(u16 wire, u16 engine_id, const char *what)
{
    const char *why = NULL;
    unsigned index;

    if (engine_id == 0) {
        (void)mmo_id_item_from_server(wire, &why);
        printf("openmmo: %s refuses wire item %u (%s)\n",
               what, (unsigned)wire, why != NULL ? why : "unmapped");
        return 1;
    }
    index = (unsigned)wire % (unsigned)MMO_ITEM_REGION_MUL;
    if (index != (unsigned)engine_id) {
        printf("openmmo: %s refuses wire item %u: engine id %u is not index %u "
               "(a gba id here would draw a different item)\n",
               what, (unsigned)wire, (unsigned)engine_id, index);
        return 1;
    }
    return 0;
}

static void dump_pockets(Bag *bag)
{
    u32 pocket;
    u16 slot, n;
    BagItem *it;

    for (pocket = 0; pocket < POCKET_MAX; pocket++) {
        n = pocket_size(pocket);
        for (slot = 0; slot < n; slot++) {
            it = Bag_GetItemSlot(bag, (u16)pocket, slot);
            if (it == NULL || it->item == 0 || it->quantity == 0)
                continue;
            printf("openmmo: pocket %s slot %u item %u x%u\n",
                   pocket_name(pocket), (unsigned)slot,
                   (unsigned)it->item, (unsigned)it->quantity);
        }
    }
}

static int stack_in_server(const openmmo_bag *b, u16 engine_id)
{
    int i;

    for (i = 0; i < b->count; i++) {
        if (b->stack[i].engine_id == engine_id)
            return 1;
    }
    return 0;
}

static int find_engine_item(Bag *bag, u16 item, BagItem **out)
{
    u32 pocket;
    u16 slot, n;
    BagItem *it;

    for (pocket = 0; pocket < POCKET_MAX; pocket++) {
        n = pocket_size(pocket);
        for (slot = 0; slot < n; slot++) {
            it = Bag_GetItemSlot(bag, (u16)pocket, slot);
            if (it != NULL && it->item == item) {
                if (out != NULL)
                    *out = it;
                return 1;
            }
        }
    }
    return 0;
}

static void seat_full(Bag *bag, const openmmo_bag *src)
{
    int i, fit, skipped, overflow;
    u16 qty;

    s_display_bag = bag;
    s_bag_seat = 1;
    Bag_Init(bag);
    fit = 0;
    skipped = 0;
    overflow = 0;
    for (i = 0; i < src->count; i++) {
        if (refuse_engine_item(src->stack[i].item_id, src->stack[i].engine_id, "bag")) {
            skipped++;
            continue;
        }
        qty = src->stack[i].quantity < 0 ? 0
            : src->stack[i].quantity > 999 ? 999
            : (u16)src->stack[i].quantity;
        if (qty == 0)
            continue;
        if (!Bag_TryAddItem(bag, src->stack[i].engine_id, qty, HEAP_ID_FIELD1)) {
            printf("openmmo: bag will not take engine item %u x%u "
                   "(pocket full or no item param)\n",
                   (unsigned)src->stack[i].engine_id, (unsigned)qty);
            overflow++;
            continue;
        }
        fit++;
    }
    /*
     * The register, behind the bag it points into, while s_bag_seat still marks this as the
     * seat so the hook does not echo it back. Absolute: a seat that carries none clears a
     * stale one.
     */
    if (src->registered_valid) {
        u16 reg = 0;

        if (src->registered_item != 0) {
            const char *why = NULL;

            reg = mmo_id_item_from_server(src->registered_item, &why);
            if (reg == MMO_ID_NONE) {
                printf("openmmo: registered wire item %u has no engine id"
                       " (%s); register cleared\n",
                       (unsigned)src->registered_item,
                       why != NULL ? why : "untranslatable");
                reg = 0;
            }
        }
        Bag_RegisterItem(bag, reg);
    }
    s_bag_seat = 0;
    s_bag_seated = 1;
    s_bag_dirty = 0;
    printf("openmmo: bag seated %d stack(s) (%d unmapped, %d fit-fail)\n",
           fit, skipped, overflow);
    dump_pockets(bag);
}

static void seat_inplace(Bag *bag, const openmmo_bag *src)
{
    int i;
    u16 qty;
    BagItem *slot;
    u32 pocket;
    u16 n, s;
    BagItem *it;

    s_bag_display = 1;
    for (i = 0; i < src->count; i++) {
        if (refuse_engine_item(src->stack[i].item_id, src->stack[i].engine_id, "bag"))
            continue;
        qty = src->stack[i].quantity < 0 ? 0
            : src->stack[i].quantity > 999 ? 999
            : (u16)src->stack[i].quantity;
        if (find_engine_item(bag, src->stack[i].engine_id, &slot)) {
            slot->quantity = qty;
            if (qty == 0)
                slot->item = 0;
        } else if (qty > 0) {
            printf("openmmo: bag screen open; cannot add engine item %u x%u "
                   "without reshuffling pockets\n",
                   (unsigned)src->stack[i].engine_id, (unsigned)qty);
        }
    }
    for (pocket = 0; pocket < POCKET_MAX; pocket++) {
        n = pocket_size(pocket);
        for (s = 0; s < n; s++) {
            it = Bag_GetItemSlot(bag, (u16)pocket, s);
            if (it == NULL || it->item == 0 || it->quantity == 0)
                continue;
            if (!stack_in_server(src, it->item)) {
                printf("openmmo: bag screen open; stale %s item %u x%u "
                       "left until the screen closes\n",
                       pocket_name(pocket), (unsigned)it->item,
                       (unsigned)it->quantity);
            }
        }
    }
    s_bag_display = 0;
}

static int pocket_has_items(Bag *bag, u32 pocket)
{
    u16 slot, n;
    BagItem *it;

    n = pocket_size(pocket);
    for (slot = 0; slot < n; slot++) {
        it = Bag_GetItemSlot(bag, (u16)pocket, slot);
        if (it != NULL && it->item != 0 && it->quantity != 0)
            return 1;
    }
    return 0;
}

void openmmo_bag_aim_cursor(FieldSystem *fs)
{
    Bag *bag;
    u32 p;
    u16 cur;

    if (fs == NULL || fs->bagCursor == NULL || fs->saveData == NULL)
        return;
    bag = SaveData_GetBag(fs->saveData);
    if (bag == NULL)
        return;
    cur = BagCursor_GetFieldPocket(fs->bagCursor);
    if (cur < POCKET_MAX && pocket_has_items(bag, cur))
        return;
    for (p = 0; p < POCKET_MAX; p++) {
        if (!pocket_has_items(bag, p))
            continue;
        BagCursor_SetFieldPocket(fs->bagCursor, (u16)p);
        printf("openmmo: bag cursor on %s (pocket %u was empty)\n",
               pocket_name(p), (unsigned)cur);
        return;
    }
}

void openmmo_bag_sync(SaveData *save, int bag_app_open)
{
    const openmmo_bag *src;
    Bag *bag;

    if (save == NULL || s_client == NULL)
        return;
    src = openmmo_client_bag(s_client);
    if (src == NULL || !src->valid)
        return;
    bag = SaveData_GetBag(save);
    if (bag == NULL)
        return;

    if (bag_app_open) {
        if (s_bag_seated)
            seat_inplace(bag, src);
        return;
    }
    if (!s_bag_dirty && s_bag_seated)
        return;
    seat_full(bag, src);
}

int openmmo_shop_price(u16 item, u32 *out)
{
    const openmmo_shop *shop;
    int i;

    if (out == NULL || s_client == NULL)
        return 0;
    shop = openmmo_client_shop(s_client);
    if (shop == NULL || !shop->valid || !shop->open)
        return 0;
    for (i = 0; i < shop->count; i++) {
        if (shop->item[i].engine_id == item) {
            if (shop->item[i].price < 0)
                return 0;
            *out = (u32)shop->item[i].price;
            return 1;
        }
    }
    return 0;
}

int openmmo_shop_buying(u16 item, u16 qty)
{
    const openmmo_shop *shop;
    const char *why = NULL;
    u16 wire;

    if (s_client == NULL)
        return 0;
    shop = openmmo_client_shop(s_client);
    if (shop == NULL || !shop->valid || !shop->open)
        return 0;
    wire = mmo_id_item_to_server(item, &why);
    if (wire == 0) {
        printf("openmmo: shop buy: no wire id for engine item %u (%s)\n",
               (unsigned)item, why != NULL ? why : "unmapped");
        return 1;
    }
    if (openmmo_client_shop_buy(s_client, (s16)wire, (s16)qty) != 0) {
        printf("openmmo: shop buy failed (engine item %u x%u)\n",
               (unsigned)item, (unsigned)qty);
        return 1;
    }
    printf("openmmo: shop buy engine %u wire %u x%u\n",
           (unsigned)item, (unsigned)wire, (unsigned)qty);
    s_bag_dirty = 1;
    return 1;
}

int openmmo_shop_selling(u16 item, u16 qty)
{
    const openmmo_bag *bag;
    const openmmo_shop *shop;
    const char *why = NULL;
    s64 entity = 0;
    int i;

    if (s_client == NULL)
        return 0;
    /*
     * Only the SERVER shop sells over the RPC. An engine mart (a locally scripted clerk) has
     * no shop session to sell into, this hook used to claim the sale anyway and skip the
     * vanilla flow, which is a sell that does nothing at all.
     */
    shop = openmmo_client_shop(s_client);
    if (shop == NULL || !shop->valid || !shop->open)
        return 0;
    bag = openmmo_client_bag(s_client);
    if (bag == NULL || !bag->valid)
        return 0;
    for (i = 0; i < bag->count; i++) {
        if (bag->stack[i].engine_id == item) {
            entity = bag->stack[i].object_id;
            break;
        }
    }
    if (entity == 0) {
        printf("openmmo: shop sell: no bag stack for engine item %u (%s)\n",
               (unsigned)item, why != NULL ? why : "not held");
        return 1;
    }
    if (openmmo_client_shop_sell(s_client, entity, (s16)qty) != 0) {
        printf("openmmo: shop sell failed (engine item %u x%u)\n",
               (unsigned)item, (unsigned)qty);
        return 1;
    }
    printf("openmmo: shop sell engine %u entity %lld x%u\n",
           (unsigned)item, (long long)entity, (unsigned)qty);
    s_bag_dirty = 1;
    /* TrashSelectedItem rebuilds the open list; that one remove is a
     * display write. The store is still the 0x42 that follows. */
    s_bag_display = 1;
    return 1;
}

void openmmo_bag_display_done(void)
{
    s_bag_display = 0;
}

static int catalog_items(u16 *dst, int cap)
{
    const openmmo_shop *shop;
    int i, n;

    shop = openmmo_client_shop(s_client);
    if (shop == NULL || !shop->valid || !shop->open)
        return 0;
    n = 0;
    for (i = 0; i < shop->count && n < cap - 1; i++) {
        if (refuse_engine_item(shop->item[i].item_id, shop->item[i].engine_id, "shop"))
            continue;
        dst[n++] = shop->item[i].engine_id;
    }
    dst[n] = SHOP_ITEM_END;
    return n;
}

int openmmo_shop_try_open(FieldSystem *fs)
{
    u16 items[OPENMMO_SHOP_MAX + 1];
    int n;
    const openmmo_shop *shop;

    if (fs == NULL || s_client == NULL)
        return 0;
    shop = openmmo_client_shop(s_client);
    if (shop == NULL || !shop->valid || !shop->open) {
        s_shop_pending = 0;
        return 0;
    }
    if (!s_shop_pending && s_shop_opened)
        return 0;
    if (fs->task != NULL || !FieldSystem_IsRunningFieldMap(fs)
        || FieldSystem_HasChildProcess(fs)) {
        s_shop_pending = 1;
        return 0;
    }
    n = catalog_items(items, OPENMMO_SHOP_MAX + 1);
    if (n <= 0) {
        printf("openmmo: shop catalog has no item this engine can draw\n");
        s_shop_pending = 0;
        return 0;
    }
    Shop_Start(NULL, fs, items, MART_TYPE_NORMAL, FALSE);
    s_shop_pending = 0;
    s_shop_opened = 1;
    printf("openmmo: shop screen opened (%d line(s))\n", n);
    return 1;
}

void openmmo_bag_try_open(FieldSystem *fs)
{
    static int opened;
    ItemUseContext ctx;
    const char *env;

    if (opened || fs == NULL)
        return;
    env = getenv("OPENMMO_BAG");
    if (env == NULL || env[0] == '\0' || env[0] == '0')
        return;
    if (fs->task != NULL || !FieldSystem_IsRunningFieldMap(fs)
        || FieldSystem_HasChildProcess(fs))
        return;
    if (!s_bag_seated)
        return;
    memset(&ctx, 0, sizeof(ctx));
    ItemUseContext_Init(fs, &ctx);
    FieldSystem_OpenBag(fs, &ctx);
    opened = 1;
    printf("openmmo: bag screen opened (OPENMMO_BAG)\n");
}
