/* Publish the six apps onto "<channel>.hud" for the window. */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants/heap.h"
#include "field/field_system.h"
#include "field_system.h"
#include "field_task.h"
#include "item.h"
#include "message_util.h"
#include "party.h"
#include "pokedex.h"
#include "pokemon.h"
#include "savedata.h"
#include "string_gf.h"

#include "../../../include/charcode.h"
#include "../../../include/client.h"
#include "../../../include/game.h"
#include "../../../include/idmap.h"
#include "../../../include/hud_channel.h"
#include "../../../include/platform.h"
#include "../../../include/region.h"
#include "../../../include/status_channel.h"

extern FieldSystem *pc_lab_field_system(void);
extern int openmmo_font_export(uint8_t *gfx, uint8_t *advance, unsigned max,
                               uint32_t *out_n);
extern int openmmo_apps_busy(void); /* openmmo_apps.c */
/* openmmo_trade.c / openmmo_link.c: a comm scene owns both screens. */
extern int openmmo_trade_scene_up(void);
extern int openmmo_link_scene_up(void);
/* Which engine party slot a server record sits in, and
 * whether the seated party is still the one the mirror describes. */
extern int openmmo_party_engine_slot(s64 id);
extern int openmmo_party_seat_is_current(const openmmo_client *c);

static struct openmmo_hud_shm *g_hud;
static uint32_t g_cmd_tail;
static int g_font_sent;

static void put_str(char *dst, size_t cap, const char *src)
{
    size_t n = 0;

    if (dst == NULL || cap == 0)
        return;
    if (src == NULL)
        src = "";
    while (src[n] != '\0' && n + 1 < cap) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = '\0';
}

/* This frame's projected plates. */
extern int openmmo_label_plate_count(void);
extern int openmmo_label_plate_at(int i, int *x, int *y, const char **name);

int openmmo_hud_windowed(void)
{
    return g_hud != NULL;
}

/* CMD_GTL's wide arguments, as the window last wrote them. NULL headless. */
const struct openmmo_hud_gtl_ask *openmmo_hud_gtl_args(void)
{
    return g_hud != NULL ? &g_hud->cmd_gtl : NULL;
}

/* CMD_MAIL_SEND's three strings, likewise. NULL headless. */
const struct openmmo_hud_mail_send *openmmo_hud_mail_args(void)
{
    return g_hud != NULL ? &g_hud->cmd_mail : NULL;
}

static mmo_shm g_hud_page = MMO_SHM_INIT;

void openmmo_hud_open(void)
{
    const char *chan = getenv("PC_VIEW");
    char name[128];
    struct openmmo_hud_shm *s;

    if (g_hud != NULL)
        return;
    if (chan == NULL || chan[0] == '\0')
        return;
    snprintf(name, sizeof name, "%s%s", chan, OPENMMO_HUD_SUFFIX);

    if (mmo_shm_create(&g_hud_page, name, sizeof *s) != 0) {
        fprintf(stderr, "openmmo: no hud page '%s' (%s)\n", name,
                mmo_plat_error());
        return;
    }

    s = (struct openmmo_hud_shm *)g_hud_page.addr;
    memset(s, 0, sizeof *s);
    s->version = OPENMMO_HUD_VERSION;
    s->writer = (uint32_t)mmo_plat_pid();
    __atomic_store_n(&s->magic, OPENMMO_HUD_MAGIC, __ATOMIC_RELEASE);
    g_hud = s;
    g_cmd_tail = s->cmd_head;
    printf("openmmo: hud page '%s' published\n", name);
}

static void maybe_font(void)
{
    struct openmmo_hud_font font;

    if (g_hud == NULL || g_font_sent)
        return;
    memset(&font, 0, sizeof font);
    font.cell = OPENMMO_HUD_CELL;
    if (!openmmo_font_export(font.gfx[0], font.advance, OPENMMO_HUD_GLYPHS,
                             &font.glyph_n))
        return;
    openmmo_hud_publish_font(g_hud, &font);
    g_font_sent = 1;
}

static uint32_t lower_now(FieldSystem *fs)
{
    extern int openmmo_underground_active(void);

    if (fs == NULL)
        return OPENMMO_HUD_LOWER_GUEST;
    if (FieldSystem_HasChildProcess(fs))
        return OPENMMO_HUD_LOWER_GUEST;
    /*
     * The Underground owns both screens and neither of them is the Poketch: down there the
     * cavern is the touch screen and the other one is the radar map of it, which is why the
     * window lays the two out the other way round (openmmo_view_geom.swapped).
     */
    if (openmmo_underground_active())
        return OPENMMO_HUD_LOWER_GUEST;
    /* A comm scene between its applications (the trade sequence swapping
     * table for animation, an encounter winding the map back up) has no
     * child for a few frames but still owns both screens; without this the
     * second screen collapsed and re-opened mid-scene. */
    if (openmmo_trade_scene_up() || openmmo_link_scene_up())
        return OPENMMO_HUD_LOWER_GUEST;
    /*
     * Every engine screen that owns the two DS screens rides processManager->child
     * (FieldSystem_StartChildProcess).
     */
    return OPENMMO_HUD_LOWER_POKETCH;
}

static void fill_chat(struct openmmo_hud_snap *s, const openmmo_client *c)
{
    openmmo_event ev[OPENMMO_HUD_CHAT_N];
    int n, i;

    if (c == NULL)
        return;
    n = openmmo_client_chat(c, ev, OPENMMO_HUD_CHAT_N);
    if (n < 0)
        n = 0;
    if (n > (int)OPENMMO_HUD_CHAT_N)
        n = (int)OPENMMO_HUD_CHAT_N;
    s->chat_n = (uint32_t)n;
    for (i = 0; i < n; i++) {
        s->chat[i].type = (uint32_t)ev[i].chat.type;
        put_str(s->chat[i].sender, sizeof s->chat[i].sender, ev[i].chat.sender);
        put_str(s->chat[i].text, sizeof s->chat[i].text, ev[i].chat.text);
    }
}

static void latin1_from_string(const String *src, char *dst, size_t cap)
{
    uint8_t utf16[96];
    mmo_charcode_result r;
    size_t i, n = 0;

    if (dst == NULL || cap == 0)
        return;
    dst[0] = '\0';
    if (src == NULL)
        return;
    r = mmo_charcode_to_utf16le(String_GetData(src), utf16, sizeof utf16);
    for (i = 0; i + 1 < r.written * 2 && n + 1 < cap; i += 2) {
        unsigned cp = (unsigned)utf16[i] | ((unsigned)utf16[i + 1] << 8);

        dst[n++] = (cp < 256) ? (char)cp : '?';
    }
    dst[n] = '\0';
}

static void party_name(const openmmo_party_mon *mon, char *dst, size_t cap)
{
    String *s;

    if (dst == NULL || cap == 0)
        return;
    dst[0] = '\0';
    if (mon == NULL)
        return;
    if (mon->nickname[0] != '\0') {
        put_str(dst, cap, mon->nickname);
        return;
    }
    if (mon->egg) {
        put_str(dst, cap, "Egg");
        return;
    }
    if (mon->species != 0) {
        s = MessageUtil_SpeciesName(mon->species, HEAP_ID_SYSTEM);
        if (s != NULL) {
            latin1_from_string(s, dst, cap);
            String_Free(s);
            if (dst[0] != '\0')
                return;
        }
    }
    if (mon->dex_id != 0) {
        char num[12];

        snprintf(num, sizeof num, "%u", (unsigned)mon->dex_id);
        put_str(dst, cap, num);
    }
}

/*
 * What the party strip draws that the wire does not carry, and the current HP when the engine
 * is the half that knows it.
 */
static void fill_party_engine(struct openmmo_hud_snap *s, const openmmo_client *c,
                              const openmmo_party *p, const FieldSystem *fs)
{
    Party *party;
    int i, n, engine_live;

    if (fs == NULL || fs->saveData == NULL)
        return;
    party = SaveData_GetParty(fs->saveData);
    if (party == NULL)
        return;
    n = Party_GetCurrentCount(party);
    engine_live = openmmo_party_seat_is_current(c);
    for (i = 0; i < (int)s->party_n; i++) {
        int slot = openmmo_party_engine_slot(p->mon[i].id);
        Pokemon *mon;

        if (slot < 0 || slot >= n)
            continue;
        mon = Party_GetPokemonBySlotIndex(party, slot);
        if (mon == NULL)
            continue;
        s->party[i].max_hp = Pokemon_GetValue(mon, MON_DATA_MAX_HP, NULL);
        s->party[i].status = Pokemon_GetValue(mon, MON_DATA_STATUS, NULL);
        if (engine_live)
            s->party[i].hp = (uint32_t)Pokemon_GetValue(mon, MON_DATA_HP, NULL);
    }
}

static void fill_party(struct openmmo_hud_snap *s, const openmmo_client *c,
                       const FieldSystem *fs)
{
    const openmmo_party *p;
    int i, n;

    if (c == NULL)
        return;
    p = openmmo_client_party(c);
    if (p == NULL || !p->valid)
        return;
    n = p->count;
    if (n > (int)OPENMMO_HUD_PARTY_N)
        n = (int)OPENMMO_HUD_PARTY_N;
    s->party_n = (uint32_t)n;
    for (i = 0; i < n; i++) {
        party_name(&p->mon[i], s->party[i].name, sizeof s->party[i].name);
        s->party[i].species = p->mon[i].species;
        s->party[i].level = (uint32_t)p->mon[i].level;
        s->party[i].hp = (uint32_t)p->mon[i].hp;
        s->party[i].egg = p->mon[i].egg ? 1u : 0u;
    }
    fill_party_engine(s, c, p, fs);
}

/* The official client's instance window (s2c 0xD3 / 0xD4). The triples are all the wire
 * carries; the table that names them is not on it. */
static void fill_objectives(struct openmmo_hud_snap *s, const openmmo_client *c)
{
    const openmmo_objectives *o;
    int i, n;

    if (c == NULL)
        return;
    o = openmmo_client_objectives(c);
    if (o == NULL || !o->valid)
        return;
    n = o->count;
    if (n > (int)OPENMMO_HUD_OBJECTIVE_N)
        n = (int)OPENMMO_HUD_OBJECTIVE_N;
    s->objective_n = (uint32_t)n;
    for (i = 0; i < n; i++) {
        s->objective[i].id = (int32_t)o->entry[i].id;
        s->objective[i].value = (int32_t)o->entry[i].value;
        s->objective[i].count = (int32_t)o->entry[i].count;
    }
}

/* An item's display name, out of the engine's own bank, by its server id.
 * Falls back to "Item <wire id>" for a stack the engine cannot name. */
static void item_label(u16 server_item, char *dst, size_t cap)
{
    const char *why;
    u16 engine_item = mmo_id_item_from_server(server_item, &why);

    if (dst == NULL || cap == 0)
        return;
    dst[0] = '\0';
    if (engine_item != 0) {
        String *str = String_Init(24, HEAP_ID_SYSTEM);

        if (str != NULL) {
            Item_LoadName(str, engine_item, HEAP_ID_SYSTEM);
            latin1_from_string(str, dst, cap);
            String_Free(str);
        }
    }
    if (dst[0] == '\0')
        snprintf(dst, cap, "Item %u", (unsigned)server_item);
}

/* A species' display name by its server dex id, same fallback shape. */
static void species_label(u16 dex, char *dst, size_t cap)
{
    const char *why;
    u16 engine_species = mmo_id_species_from_server(dex, &why);

    if (dst == NULL || cap == 0)
        return;
    dst[0] = '\0';
    if (engine_species != 0) {
        String *str = MessageUtil_SpeciesName(engine_species, HEAP_ID_SYSTEM);

        if (str != NULL) {
            latin1_from_string(str, dst, cap);
            String_Free(str);
        }
    }
    if (dst[0] == '\0')
        snprintf(dst, cap, "No.%u", (unsigned)dex);
}

static void fill_gtl(struct openmmo_hud_snap *s, const openmmo_client *c)
{
    const openmmo_gtl *g;
    int i, n;

    if (c == NULL)
        return;
    g = openmmo_client_gtl(c);
    if (g == NULL)
        return;
    s->gtl.open = g->session_open ? 1u : 0u;
    s->gtl.kind = (uint32_t)g->kind;
    s->gtl.page = (uint32_t)g->page;
    s->gtl.total = g->total > 0 ? (uint32_t)g->total : 0u;
    n = g->count;
    if (n > (int)OPENMMO_HUD_GTL_ROWS)
        n = (int)OPENMMO_HUD_GTL_ROWS;
    s->gtl.row_n = (uint32_t)n;
    for (i = 0; i < n; i++) {
        const openmmo_gtl_row *r = &g->row[i];
        struct openmmo_hud_gtl_row *d = &s->gtl.row[i];

        d->kind = r->kind == MMO_GTL_KIND_ITEM ? 1u : 0u;
        d->price = r->price > 0 ? (uint32_t)r->price : 0u;
        d->quantity = r->quantity > 0 ? (uint32_t)r->quantity : 0u;
        d->listed_at = r->listed_at > 0 ? (uint32_t)r->listed_at : 0u;
        d->expires_at = r->expires_at > 0 ? (uint32_t)r->expires_at : 0u;
        d->own_state = (uint32_t)r->own_state;
        d->own_remaining =
            r->own_remaining > 0 ? (uint32_t)r->own_remaining : 0u;
        d->own_unclaimed =
            r->own_unclaimed > 0 ? (uint32_t)r->own_unclaimed : 0u;
        d->nature = OPENMMO_HUD_GTL_NONE;
        if (r->have_mon) {
            int v;

            if (r->mon.nickname[0] != '\0')
                put_str(d->label, sizeof d->label, r->mon.nickname);
            else
                species_label(r->mon.dex_id, d->label, sizeof d->label);
            d->species = r->mon.species;
            d->level = (uint32_t)r->mon.level;
            d->nature = (uint32_t)r->mon.nature;
            d->shiny = r->mon.shiny ? 1u : 0u;
            for (v = 0; v < 6; v++)
                d->iv[v] = r->mon.iv[v];
        } else
            item_label(r->item_id, d->label, sizeof d->label);
    }
    n = g->quote_count;
    if (n > (int)OPENMMO_HUD_GTL_ROWS)
        n = (int)OPENMMO_HUD_GTL_ROWS;
    s->gtl.quote_n = (uint32_t)n;
    for (i = 0; i < n; i++) {
        s->gtl.quote[i].item = (uint32_t)g->quote[i].item_id;
        s->gtl.quote[i].price =
            g->quote[i].price > 0 ? (uint32_t)g->quote[i].price : 0u;
        item_label(g->quote[i].item_id, s->gtl.quote[i].label,
                   sizeof s->gtl.quote[i].label);
    }
    n = g->log.count;
    if (n > (int)OPENMMO_HUD_GTL_LOG_N)
        n = (int)OPENMMO_HUD_GTL_LOG_N;
    s->gtl.log_n = (uint32_t)n;
    for (i = 0; i < n; i++) {
        const mmo_gtl_log_row *lr = &g->log.rows[i];
        struct openmmo_hud_gtl_logrow *d = &s->gtl.log[i];

        d->type = (uint32_t)(lr->type & 3);
        d->amount = lr->amount > 0 ? (uint32_t)lr->amount : 0u;
        d->total = lr->total > 0 ? (uint32_t)lr->total : 0u;
        d->epoch = lr->epoch > 0 ? (uint32_t)lr->epoch : 0u;
        if (lr->type & 1)
            item_label((u16)lr->what, d->label, sizeof d->label);
        else
            species_label((u16)lr->what, d->label, sizeof d->label);
    }
    s->gtl.result_code = (int32_t)g->result.code;
    s->gtl.result_a = (int32_t)g->result.a;
    s->gtl.result_b = (int32_t)g->result.b;
    s->gtl.result_seq = (uint32_t)g->result_seq;
    /* A result naming a species or an item resolves here too: the window
     * has no banks, so the guest swaps the id blank for the name by writing
     * it into the matching row label when it can... the window formats the
     * toast from code+a+b and looks names up in the page it already shows. */
}

/* The wallet and the bag, for the GTL create picker and the buy math. */
/* The mailbox. The held page is whichever box the last 0x97 was, so the
 * window's tab and this flag are the same fact; a row's own id rides in two
 * halves because the game is -m32 and the window is not. */
static void fill_mail(struct openmmo_hud_snap *s, const openmmo_client *c)
{
    const openmmo_mail *m;
    int i, n;

    if (c == NULL)
        return;
    m = openmmo_client_mail(c);
    if (m == NULL)
        return;
    s->mail.valid = m->valid ? 1u : 0u;
    s->mail.inbox = m->inbox > 0 ? (uint32_t)m->inbox : 0u;
    s->mail.unread = m->inbox_seen > 0 ? (uint32_t)m->inbox_seen : 0u;
    s->mail.sent = m->sent > 0 ? (uint32_t)m->sent : 0u;
    s->mail.listed_sent = m->listed_sent ? 1u : 0u;
    s->mail.page = m->page > 0 ? (uint32_t)m->page : 0u;
    n = m->count;
    if (n > (int)OPENMMO_HUD_MAIL_ROWS)
        n = (int)OPENMMO_HUD_MAIL_ROWS;
    s->mail.row_n = (uint32_t)n;
    for (i = 0; i < n; i++) {
        const mmo_mail *e = &m->entry[i];
        struct openmmo_hud_mail_row *d = &s->mail.row[i];
        const char *who = m->listed_sent ? e->recipient : e->sender;

        d->id_lo = (uint32_t)((u64)e->mail_id & 0xFFFFFFFFu);
        d->id_hi = (uint32_t)(((u64)e->mail_id >> 32) & 0xFFFFFFFFu);
        if (who[0] == '\0' && e->sender_id == 0)
            who = "SYSTEM";
        put_str(d->who, sizeof d->who, who);
        put_str(d->subject, sizeof d->subject, e->subject);
        d->epoch = e->sent_at > 0 ? (uint32_t)e->sent_at : 0u;
        d->unread = e->unread ? 1u : 0u;
    }
    if (m->have_detail && m->detail.mail_id != 0) {
        const mmo_mail *e = &m->detail;
        const char *who = m->detail_sent ? e->recipient : e->sender;

        s->mail.open_id_lo = (uint32_t)((u64)e->mail_id & 0xFFFFFFFFu);
        s->mail.open_id_hi = (uint32_t)(((u64)e->mail_id >> 32) & 0xFFFFFFFFu);
        s->mail.open_sent = m->detail_sent ? 1u : 0u;
        s->mail.open_epoch = e->sent_at > 0 ? (uint32_t)e->sent_at : 0u;
        if (who[0] == '\0' && e->sender_id == 0)
            who = "SYSTEM";
        put_str(s->mail.open_who, sizeof s->mail.open_who, who);
        put_str(s->mail.open_subject, sizeof s->mail.open_subject, e->subject);
        put_str(s->mail.open_body, sizeof s->mail.open_body, e->body);
    }
    s->mail.result_code = (int32_t)m->result;
    s->mail.result_seq = (uint32_t)m->result_seq;
}

static void fill_bag(struct openmmo_hud_snap *s, const openmmo_client *c)
{
    const openmmo_bag *b;
    const openmmo_world_state *w;
    int i, n;

    if (c == NULL)
        return;
    w = openmmo_client_world_state(c);
    if (w != NULL && w->valid && w->money > 0)
        s->money = (uint32_t)w->money;
    b = openmmo_client_bag(c);
    if (b == NULL || !b->valid)
        return;
    n = b->count;
    if (n > (int)OPENMMO_HUD_BAG_N)
        n = (int)OPENMMO_HUD_BAG_N;
    s->bag_n = (uint32_t)n;
    for (i = 0; i < n; i++) {
        s->bag[i].item = (uint32_t)b->stack[i].item_id;
        s->bag[i].count =
            b->stack[i].quantity > 0 ? (uint32_t)b->stack[i].quantity : 0u;
        item_label(b->stack[i].item_id, s->bag[i].label,
                   sizeof s->bag[i].label);
    }
}

static void fill_friends(struct openmmo_hud_snap *s, const openmmo_client *c)
{
    const openmmo_friends *f;
    int i, n;

    if (c == NULL)
        return;
    f = openmmo_client_friends(c);
    if (f == NULL || !f->valid)
        return;
    n = f->count;
    if (n > (int)OPENMMO_HUD_FRIEND_N)
        n = (int)OPENMMO_HUD_FRIEND_N;
    s->friends_n = (uint32_t)n;
    for (i = 0; i < n; i++) {
        put_str(s->friends[i].name, sizeof s->friends[i].name, f->entry[i].name);
        s->friends[i].online = f->entry[i].online ? 1u : 0u;
    }
}

static void fill_guild(struct openmmo_hud_snap *s, const openmmo_client *c)
{
    const openmmo_guild *g;
    int i, n;

    if (c == NULL)
        return;
    g = openmmo_client_guild(c);
    if (g == NULL || !g->valid || !g->in_guild)
        return;
    put_str(s->guild.name, sizeof s->guild.name, g->profile.name);
    put_str(s->guild.tag, sizeof s->guild.tag, g->profile.tag);
    put_str(s->guild.motd, sizeof s->guild.motd, g->profile.message);
    n = g->member_count;
    if (n > (int)OPENMMO_HUD_MEMBER_N)
        n = (int)OPENMMO_HUD_MEMBER_N;
    s->guild.member_n = (uint32_t)n;
    for (i = 0; i < n; i++) {
        put_str(s->guild.member[i].name, sizeof s->guild.member[i].name,
                g->member[i].name);
        s->guild.member[i].online = g->member[i].online ? 1u : 0u;
    }
}

static void fill_map(struct openmmo_hud_snap *s, const openmmo_client *c)
{
    const openmmo_world_state *ws;
    openmmo_event live[OPENMMO_HUD_PEER_N];
    int n, i;
    char map[32];

    if (c == NULL)
        return;
    ws = openmmo_client_world_state(c);
    if (ws != NULL && ws->valid) {
        put_str(s->map.region, sizeof s->map.region, mmo_region_name(ws->region));
        snprintf(map, sizeof map, "map %d", ws->map_id);
        put_str(s->map.name, sizeof s->map.name, map);
        s->map.x = (uint32_t)ws->x;
        s->map.y = (uint32_t)ws->y;
    }
    n = openmmo_client_live_entities(c, live, OPENMMO_HUD_PEER_N);
    if (n < 0)
        n = 0;
    if (n > (int)OPENMMO_HUD_PEER_N)
        n = (int)OPENMMO_HUD_PEER_N;
    s->map.peer_n = (uint32_t)n;
    for (i = 0; i < n; i++)
        put_str(s->map.peer[i], sizeof s->map.peer[i], live[i].entity.name);
}

static void fill_net(struct openmmo_hud_snap *s, const openmmo_client *c,
                     uint32_t flags, const char *reason)
{
    openmmo_status st;
    openmmo_battle_state battle;

    s->net.latency_ms = -1;
    if (c == NULL)
        return;
    st = openmmo_client_status(c);
    s->net.state = (uint32_t)st;
    if ((st == OPENMMO_FAILED || st == OPENMMO_DISCONNECTED) &&
        reason != NULL && reason[0] != '\0')
        put_str(s->net.reason, sizeof s->net.reason, reason);
    (void)flags;
    battle = openmmo_client_battle_state(c);
    if (battle == OPENMMO_BATTLE_ENTERING)
        put_str(s->net.battle, sizeof s->net.battle, "BATTLE...");
    else if (battle == OPENMMO_BATTLE_ACTIVE)
        put_str(s->net.battle, sizeof s->net.battle, "IN BATTLE - B TO RUN");
}

/* The nameplates this frame's render projected, copied into the snapshot the
 * window reads. openmmo_label.c says why the window draws them rather than the
 * game: at --hd3d 2 and above the port re-composes the panel and a plate
 * painted on the guest surface never reaches the screen. */
static void fill_plates(struct openmmo_hud_snap *s)
{
    int i, n = openmmo_label_plate_count();
    int x, y;
    const char *name;

    if (n > OPENMMO_HUD_PLATE_N)
        n = OPENMMO_HUD_PLATE_N;
    for (i = 0; i < n; i++) {
        if (!openmmo_label_plate_at(i, &x, &y, &name))
            break;
        s->plate[i].x = (int32_t)x;
        s->plate[i].y = (int32_t)y;
        put_str(s->plate[i].name, sizeof s->plate[i].name, name);
    }
    s->plate_n = (uint32_t)i;
}

static struct openmmo_hud_snap g_last;
static int g_have_last;

/* The plates alone, at the end of the frame that projected them. */
void openmmo_hud_publish_plates(void)
{
    if (g_hud == NULL || !g_have_last)
        return;
    fill_plates(&g_last);
    openmmo_hud_publish(g_hud, &g_last);
}

void openmmo_hud_publish_now(openmmo_client *c, uint32_t flags,
                             const char *reason, const char *compose,
                             int composing, const char *compose_to,
                             uint32_t send_type)
{
    FieldSystem *fs = pc_lab_field_system();
    struct openmmo_hud_snap snap;

    if (g_hud == NULL)
        return;
    maybe_font();
    memset(&snap, 0, sizeof snap);
    snap.lower = lower_now(fs);
    /* No app: the device is vanilla now, and the window's UI layer is the
     * home of every host screen. The field is kept for the page layout. */
    snap.app = OPENMMO_HUD_APP_CHAT;
    snap.composing = composing ? 1u : 0u;
    put_str(snap.compose, sizeof snap.compose, compose);
    put_str(snap.compose_to, sizeof snap.compose_to, compose_to);
    snap.send_type = send_type;
    snap.font_ready = g_font_sent ? 1u : 0u;
    /*
     * Busy is "the guest has the player's attention": a dialog or menu on a running map, any
     * child application (their map is torn down, which is why IsRunningFieldMap must not gate
     * this, the trade and battle scenes ran with the panels sitting bright on top of them), a
     * screen this client asked the engine for, or a comm scene between its applications.
     */
    snap.guest_busy = ((fs != NULL
                        && (fs->task != NULL
                            || FieldSystem_HasChildProcess(fs)))
                       || openmmo_apps_busy()
                       || openmmo_trade_scene_up()
                       || openmmo_link_scene_up())
                      ? 1u : 0u;
    /* A fresh character has no dex; official disables the button, and the
     * window needs the fact to draw the same refusal. */
    snap.has_dex = (fs != NULL && fs->saveData != NULL &&
                    Pokedex_IsObtained(SaveData_GetPokedex(fs->saveData)))
                       ? 1u : 0u;
    /* Down in the cavern none of those screens can be opened at all; the
     * window draws the whole row disabled off this. openmmo_apps.c refuses
     * the command too, for a click that was already in flight. */
    {
        extern int openmmo_underground_active(void);

        snap.underground = openmmo_underground_active() ? 1u : 0u;
    }
    fill_chat(&snap, c);
    fill_party(&snap, c, fs);
    fill_friends(&snap, c);
    fill_guild(&snap, c);
    fill_map(&snap, c);
    fill_objectives(&snap, c);
    fill_gtl(&snap, c);
    fill_mail(&snap, c);
    fill_bag(&snap, c);
    fill_net(&snap, c, flags, reason);
    fill_plates(&snap);
    if (g_have_last && memcmp(&g_last, &snap, sizeof snap) == 0)
        return;
    g_last = snap;
    g_have_last = 1;
    openmmo_hud_publish(g_hud, &snap);
}

unsigned openmmo_hud_take_cmds(uint32_t *dst, unsigned max)
{
    uint32_t dropped = 0;

    if (g_hud == NULL || dst == NULL || max == 0)
        return 0;
    return openmmo_hud_read_cmds(g_hud, &g_cmd_tail, dst, max, &dropped);
}

/* The name a consumed CMD_PLAYER carried; "" with no page. The pointer is
 * into the page's own slot: copy it before the ring can wrap. */
const char *openmmo_hud_cmd_name(int32_t arg)
{
    if (g_hud == NULL)
        return "";
    return openmmo_hud_player_name(g_hud, arg);
}
