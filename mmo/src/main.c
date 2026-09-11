/* Command-line front end for the native client. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/time.h>

#include "mmo.h"
#include "client.h"
#include "endpoint.h"
#include "game.h"
#include "script.h"
#include "selftest.h"
#include "net.h"
#include "session.h"
#include "login.h"
#include "crypto.h"
#include "trace.h"
#include "mockserver.h"
#include "sprite.h"
#include "cartridge.h"
#include "platform.h"
#include "imports.h"
#include "offline_chain.h"
#include "offline_import.h"
#include "region.h"
#include "appearance.h"
#include "battle_anim.h"
#include "display.h"

/* Hold this many idle frames after AUTHED before declaring the login session
 * held, the login stream has no keepalive, so a held session is just the idle
 * connection staying up (~2s at the 2ms pacing below). */
#define LOGIN_HOLD_FRAMES 1000

/* A hard backstop so a wedged loop cannot spin forever; the library's own
 * per-step deadlines fail a stuck session long before this. */
#define MAX_FRAMES 120000

static int usage(const char *argv0)
{
    fprintf(stderr,
        "%s %s\n"
        "usage: %s <command> [options]\n"
        "  version           print version and exit\n"
        "  login --user U --pass W\n"
        "                    open an authenticated session and hold it\n"
        "  join  --user U --pass W \\\n"
        "        [--character NAME | --index N | --region NAME]\n"
        "                    authenticate, take a game-server ticket, and join it.\n"
        "                    With more than one character, --character names one,\n"
        "                    --index takes that 0-based row of the ordered list,\n"
        "                    --region takes the unique character in that world.\n"
        "                    With none of those, join takes the first row.\n"
        "  create --user U --pass W \\\n"
        "         --name NAME [--gender male|female] [--region sinnoh] \\\n"
        "         [--body NAME] [--skin SLOT=TYPE:COLOR]...\n"
        "                    authenticate, join the game server, and submit a new\n"
        "                    character (name, gender, region, twelve-slot appearance).\n"
        "                    An empty character list is the starting state, not an error\n"
        "  pair  --user U --pass W --user2 U2 --pass2 W2 \\\n"
        "        [--character NAME] [--character2 NAME]\n"
        "                    join two sessions at once and watch them see each\n"
        "                    other: refuse if they landed on different maps, then\n"
        "                    report each spawn, walk one player a tile and confirm\n"
        "                    the other observes the step, then send a chat line\n"
        "                    the other has to hear\n"
        "  warp  --user U --pass W \\\n"
        "        [--tx X --tz Z]\n"
        "                    join, walk to a warp tile (default 10,2, the Pallet\n"
        "                    player's-house staircase) and report the server-driven\n"
        "                    map transition it triggers\n"
        "  map   --user U --pass W\n"
        "                    join, report the server's scene (weather/lighting/type)\n"
        "                    for the map, and probe each edge for a seamless\n"
        "                    LoadMap-less connection crossing\n"
        "  encounter --user U --pass W \\\n"
        "            [--action run|move|item|switch] [--move ID] [--item ID] \\\n"
        "            [--slot N] [--target ID]\n"
        "                    join as a developer character and ask the server for a\n"
        "                    wild encounter (/testbattle), reporting the server-driven\n"
        "                    battle the client no longer rolls locally. Default is to\n"
        "                    run away; --action move sends the lead's first move (or\n"
        "                    --move) after the prompt, --action item uses --item,\n"
        "                    --action switch waits for the prompt or a forced\n"
        "                    replacement (--slot). --slot with --action move names\n"
        "                    the replacement if the lead faints\n"
        "  flags --user U --pass W\n"
        "                    join and report the server-owned progression store (the\n"
        "                    world-flag table, set story flags and story variables)\n"
        "                    the client reads but never writes\n"
        "  playthrough --user U --pass W \\\n"
        "              [--character NAME]\n"
        "                    walk the Sinnoh mainline gates against a live game:\n"
        "                    rival, starter, first wild, each gym, each HM wall,\n"
        "                    the five rooms, the Hall of Fame. Prints each gate and\n"
        "                    the furthest one reached. Needs a developer character\n"
        "                    (the seeded Sinnoh one) so /story can jump\n"
        "  shop  --user U --pass W \\\n"
        "        [--buy ITEM QTY | --sell ITEM QTY]\n"
        "                    join as a developer, open a mart (/shop), and report\n"
        "                    the shelf; with --buy or --sell, send that intent and\n"
        "                    report the bag and cash the server sends back\n"
        "  dialog --user U --pass W\n"
        "                    join as a developer, ask for a dialog box (/dialog),\n"
        "                    reply, and report the lock the server released\n"
        "  talk   --user U --pass W \\\n"
        "         [--here] [--face north|south|west|east]\n"
        "                    join, walk to the Twinleaf bedroom Wii, press A\n"
        "                    (TileInteract), reply, and report the box.\n"
        "                    --here presses A on the tile the join placed the\n"
        "                    player on, turning to --face first, and expects a\n"
        "                    person to answer rather than a sign\n"
        "  yesno --user U --pass W\n"
        "                    join as a developer, ask for a yes/no prompt\n"
        "                    (/yesno), reply yes, and report the lock released\n"
        "  menu  --user U --pass W\n"
        "                    join as a developer, ask for a species menu\n"
        "                    (/menu), pick the first row, and report the lock\n"
        "  list  --user U --pass W\n"
        "                    join as a developer, ask for a text list\n"
        "                    (/list), pick the first row, and report the lock\n"
        "  move  --user U --pass W\n"
        "                    join as a developer, ask for a scripted facing\n"
        "                    sequence (/move), and report the bytes that arrived\n"
        "  quest --user U --pass W\n"
        "                    join as a developer, ask for one objective\n"
        "                    (/quest), and report the store the server sent\n"
        "  ui    --user U --pass W\n"
        "                    join as a developer, report the HUD pages the\n"
        "                    join already sent, ask for a prompt (/ui), and\n"
        "                    report the store the server sent\n"
        "  sync  --user U --pass W\n"
        "                    join as a developer, report whether join pushed\n"
        "                    a digest or a transfer, ask for a known blob\n"
        "                    (/sync), and report the store after reassembly\n"
        "  compete --user U --pass W [--send]\n"
        "                    join as a developer, ask for a rental offer, a\n"
        "                    tournament page, a bracket and a score board\n"
        "                    (/compete), and report the store they filled;\n"
        "                    with --send, first send every request of the group\n"
        "  gm    --user U --pass W [--send]\n"
        "                    join as a developer, ask for a staff lookup, a\n"
        "                    panel body and a panel row (/gm), and report the\n"
        "                    store they filled; with --send, first send the\n"
        "                    two staff requests of the group\n"
        "  friends --user U --pass W \\\n"
        "          [--add NAME | --remove NAME]\n"
        "                    join and report the server-owned friends list;\n"
        "                    with --add or --remove, send that name and report\n"
        "                    the list the server sends back\n"
        "  guild  --user U --pass W \\\n"
        "         [--create NAME TAG | --invite NAME | --leave | --disband |\n"
        "          --motd TEXT | --rank ID N | --kick ID | --log]\n"
        "                    join and report the server-owned guild; with an\n"
        "                    action, send that packet and report what comes back\n"
        "  mail   --user U --pass W \\\n"
        "         [--sent | --send NAME --subject TEXT --body TEXT |\n"
        "          --read ID | --delete ID]\n"
        "                    join and report the server-owned mailbox; with an\n"
        "                    action, send that packet and report what comes back\n"
        "  link   --user U --pass W \\\n"
        "         [--invite NAME | --leave | --kick ID | --captain ID]\n"
        "                    join and report the server-owned link; with an\n"
        "                    action, send that packet and report what comes back\n"
        "  gift  --user U --pass W\n"
        "                    join as a developer, ask for a Potion (/gift),\n"
        "                    and report the bag the server sent back\n"
        "  use   --user U --pass W \\\n"
        "        --item ID [--slot N]\n"
        "                    join and use that bag item on a party member (slot 0\n"
        "                    if left off); report the party and bag the server\n"
        "                    sends back\n"
        "  storage --user U --pass W \\\n"
        "          [--deposit PARTY_SLOT | --withdraw PC_SLOT |\n"
        "           --board PARTY_SLOT | --collect DAYCARE_SLOT |\n"
        "           --add SPECIES [--level N] |\n"
        "           --release PARTY_SLOT | --release-pc PC_SLOT]\n"
        "          [--to SLOT] [--box N]\n"
        "                    join and report the server-owned party, PC and day\n"
        "                    care; with a move, send it and report the containers\n"
        "                    again once the server has sent them back. --board\n"
        "                    hands a party member over the day care counter and\n"
        "                    --collect takes one back. --to names the destination\n"
        "                    slot (the first free one if left off); a taken one is\n"
        "                    the swap the box screen draws. --add asks the\n"
        "                    developer's /party for a monster of that species;\n"
        "                    --release lets a party member go, --release-pc a\n"
        "                    stored one. Slots are the wire's, 0-based, over one\n"
        "                    flat PC; with --box N the PC slots (--to on a\n"
        "                    deposit, --withdraw, --release-pc) count from 1\n"
        "                    inside that box instead, and a deposit with no --to\n"
        "                    takes the first free slot of that box\n"
        "  movelearn --user U --pass W [--slot N]\n"
        "                    join and answer the server's move offer: report the\n"
        "                    four moves the monster is choosing between, drop slot\n"
        "                    N for the new move (or keep the moveset if left off),\n"
        "                    and report the party the server sends back\n"
        "  breed --user U --pass W \\\n"
        "        [--own MONSTER --partner MONSTER [--gender -1|0|1]]\n"
        "                    join and report the server-owned incubator slots and\n"
        "                    any egg the party carries; with two parents, ask what\n"
        "                    they would produce and report the server's forecast\n"
        "  evolve --user U --pass W [--decline]\n"
        "                    join and answer the server's evolution: report the\n"
        "                    monster and the species it becomes, let it through\n"
        "                    (or stop it with --decline, where the server allowed\n"
        "                    that), and report the party the server sends back\n"
        "  script --script FILE [--mode login|join] [--user U --pass W]\n"
        "                    drive a headless, reproducible session from an action\n"
        "                    script (connect/await/frames/move/chat/disconnect);\n"
        "                    '-' reads the script from stdin\n"
        "  record --out FILE [--user U --pass W]\n"
        "                    capture a live login session (pinned client key) to a\n"
        "                    replayable trace of both the wire and cleartext layers\n"
        "  replay --trace FILE [--root pinned|mock]\n"
        "                    replay a recorded trace offline and check it still\n"
        "                    reproduces byte- and render-clean; exit 0/1, no server\n"
        "  sprite [--table] [--species N] [--form F] [--gender 0|1|2] \\\n"
        "         [--shiny 0|1] [--face front|back]\n"
        "                    which archive members hold a species' picture, or the\n"
        "                    reason this client has none for it; --table prints the\n"
        "                    whole space, which is what the engine's own answers\n"
        "                    are diffed against. No server\n"
        "  regions           print the worlds a character can be made in, in the\n"
        "                    order a region list draws them, with the reason each\n"
        "                    unavailable one is greyed out. No server\n"
        "  appearances       print the bodies a character can look like, with the\n"
        "                    reason each greyed row is not offered. No server\n"
        "  import-save --report FILE [--chain FILE] [--character N]\n"
        "  imports [DIR...] [--found MANIFEST]\n"
        "                    print what each content package takes out of a\n"
        "                    cartridge and whether one has filled it; exits\n"
        "                    non-zero when a package is short, and says in a\n"
        "                    sentence what the game draws instead. No server\n"
        "  cartridges [CODE] print the cartridge slots this client knows, what\n"
        "                    each one serves a package, and the sentence a player\n"
        "                    is told about the ones that serve nothing. With a\n"
        "                    four-character game code it answers for that image\n"
        "                    and exits non-zero when it cannot fill. No server\n"
        "  selftest          run the built-in codec/crypto known-answer checks\n"
        "                    (frame, CRC, HMAC, AES-CTR, ECDH) and exit; no server\n",
        MMO_CLIENT_NAME, MMO_CLIENT_VERSION, argv0);
    return 2;
}

/* Nature names for the report, in the engine's own `enum Nature` order, which
 * is the order the server's table and the game client's both use, so the id
 * needs no translation. Diagnostics only: what a player reads comes from the
 * engine's text bank, not from here. */
static const char *nature_name(int nature)
{
    static const char *const names[] = {
        "hardy", "lonely", "brave", "adamant", "naughty",
        "bold", "docile", "relaxed", "impish", "lax",
        "timid", "hasty", "serious", "jolly", "naive",
        "modest", "mild", "quiet", "bashful", "rash",
        "calm", "gentle", "sassy", "careful", "quirky",
    };
    if (nature < 0 || nature >= (int)(sizeof names / sizeof *names))
        return "?";
    return names[nature];
}

static void print_chat_line(const char *tag, const openmmo_event *ev)
{
    const char *who = ev->chat.sender[0] ? ev->chat.sender : "-";

    if (tag && tag[0])
        printf("%s: chat [%d] %s: %s\n", tag, ev->chat.type, who, ev->chat.text);
    else
        printf("chat [%d] %s: %s\n", ev->chat.type, who, ev->chat.text);
}

/* Report the party the server sent, the container the client holds, not a local
 * save. Species are engine ids; a 0 is one the id map has no verified engine
 * correspondence for, which is shown rather than hidden. */
static void print_party(const openmmo_party *p)
{
    if (!p || !p->valid) {
        printf("party: no container received\n");
        return;
    }
    printf("party: %d of %d member(s)%s%s\n", p->count, p->total,
           p->untranslatable ? " (some species unmapped)" : "",
           p->malformed ? " [a container failed to decode]" : "");
    if (p->trailing)
        printf("party: %d byte(s) past the last record the game client reads\n",
               p->trailing);
    if (p->unmapped_moves)
        printf("party: %d move slot(s) have no engine id\n", p->unmapped_moves);
    if (p->hidden_abilities)
        printf("party: %d member(s) on a hidden ability the engine cannot name\n",
               p->hidden_abilities);
    for (int i = 0; i < p->count; i++) {
        const openmmo_party_mon *m = &p->mon[i];
        printf("  slot %d: dex %d (engine species %d) level %d hp %d%s%s, OT %s%s%s\n",
               m->slot, m->dex_id, m->species, m->level, m->hp,
               m->egg ? " egg" : "", m->shiny ? " shiny" : "", m->ot,
               m->nickname[0] ? ", nicknamed " : "", m->nickname);
        /* The contest half, printed only when there is one: it is zero for
         * every monster that has never been raised for one, and a line of
         * zeroes under every party member would bury the rest. The ribbon mask
         * is what decides which contest rank this monster may enter. */
        if (m->cond[0] || m->cond[1] || m->cond[2] || m->cond[3] || m->cond[4]
            || m->sheen || m->ribbons_super)
            printf("    contest: cool %u beauty %u cute %u smart %u tough %u,"
                   " sheen %u, ribbons %08x%08x\n",
                   m->cond[0], m->cond[1], m->cond[2], m->cond[3], m->cond[4],
                   m->sheen, (unsigned)(m->ribbons_super >> 32),
                   (unsigned)(m->ribbons_super & 0xffffffffu));
        printf("    nature %s, ability slot %d%s, friendship %d, form %d\n",
               nature_name(m->nature), m->ability_slot,
               m->ability_unrenderable ? " (hidden: no engine ability)" : "",
               m->friendship, m->form);
        /*
         * Only when it is carrying something: a line of "holding nothing" under every party
         * member would bury the rest, the same bargain the contest line takes.
         */
        if (m->held_item)
            printf("    holding: wire %d -> engine %u%s\n", m->held_item,
                   (unsigned)m->held_item_engine,
                   m->held_item_engine ? "" : " (no engine id)");
        /* Where it was caught, printed only when the record said. */
        if (m->caught_location_label > 0)
            printf("    caught: location label %d\n",
                   m->caught_location_label);
        else if (m->caught_map_header >= 0)
            printf("    caught: engine map header %d\n", m->caught_map_header);
        /* Only when it is suffering from something, the same bargain again. The
         * word is the engine's own, printed as the bits it is: a Center clears
         * every one of them and a healthy party prints no line at all. */
        if (m->status)
            printf("    status: %s%s%s%s%s%s0x%03x\n",
                   (m->status & 0x07) ? "asleep " : "",
                   (m->status & 0x08) ? "poisoned " : "",
                   (m->status & 0x10) ? "burned " : "",
                   (m->status & 0x20) ? "frozen " : "",
                   (m->status & 0x40) ? "paralysed " : "",
                   (m->status & 0x80) ? "badly poisoned " : "",
                   (unsigned)m->status);
        printf("    IV %d/%d/%d/%d/%d/%d  EV %d/%d/%d/%d/%d/%d (hp/atk/def/spe/spa/spd)\n",
               m->iv[0], m->iv[1], m->iv[2], m->iv[3], m->iv[4], m->iv[5],
               m->ev[0], m->ev[1], m->ev[2], m->ev[3], m->ev[4], m->ev[5]);
        for (int j = 0; j < 4; j++) {
            if (!m->move_id[j])
                continue;
            printf("    move %d: server %d -> engine %d%s, pp %d%s\n", j,
                   m->move_id[j], m->move[j],
                   m->move[j] ? "" : " (no engine id)", m->move_pp[j],
                   m->move_pp_up[j] ? " (pp up)" : "");
        }
    }
}

/* Report the bag the server sent, the stacks the client holds, not a local
 * save. Engine ids are through the 4.1 map; a 0 is one the id map has no
 * verified engine correspondence for, which is shown rather than hidden. */
static void print_bag(const openmmo_bag *b)
{
    if (!b || !b->valid) {
        printf("bag: no snapshot received\n");
        return;
    }
    printf("bag: %d of %d stack(s)%s%s%s\n", b->count, b->total,
           b->untranslatable ? " (some items unmapped)" : "",
           b->dropped ? " [stacks dropped for want of room]" : "",
           b->malformed ? " [a packet failed to decode]" : "");
    for (int i = 0; i < b->count; i++) {
        const openmmo_bag_stack *s = &b->stack[i];
        printf("  stack %d: wire %d -> engine %d%s x%d\n", i,
               s->item_id, s->engine_id,
               s->engine_id ? "" : " (no engine id)", s->quantity);
    }
}

static void print_guild(const openmmo_guild *g)
{
    int i;

    if (!g || !g->valid) {
        printf("guild: no snapshot received\n");
        return;
    }
    if (!g->in_guild) {
        printf("guild: none%s\n",
               g->malformed ? " [a packet failed to decode]" : "");
        return;
    }
    printf("guild: %s [%s] id %lld members %d%s%s%s\n",
           g->profile.name[0] ? g->profile.name : "?",
           g->profile.tag[0] ? g->profile.tag : "?",
           (long long)g->profile.guild_id, g->member_count,
           g->dropped ? " [entries dropped for want of room]" : "",
           g->malformed ? " [a packet failed to decode]" : "",
           g->profile.message[0] ? "" : "");
    if (g->profile.message[0])
        printf("  motd: %s\n", g->profile.message);
    if (g->profile.rank_count > 0) {
        printf("  ranks:");
        for (i = 0; i < g->profile.rank_count; i++)
            printf(" %d=%s", i,
                   g->profile.rank_label[i][0] ? g->profile.rank_label[i] : "-");
        printf("\n");
    }
    printf("  perms %d %d %d %d %d\n",
           (int)g->profile.perm[0], (int)g->profile.perm[1],
           (int)g->profile.perm[2], (int)g->profile.perm[3],
           (int)g->profile.perm[4]);
    for (i = 0; i < g->member_count; i++)
        printf("  %s %s rank %d id %lld\n",
               g->member[i].name[0] ? g->member[i].name : "?",
               g->member[i].online ? "online" : "offline",
               (int)g->member[i].rank,
               (long long)g->member[i].entity_id);
    if (g->log_valid) {
        printf("  log: %d of %d\n", g->log_count, (int)g->log_total);
        for (i = 0; i < g->log_count; i++)
            printf("    type %d %s %s\n", (int)g->log[i].type,
                   g->log[i].actor, g->log[i].target);
    }
}

static void print_mail(const openmmo_mail *m)
{
    int i;

    if (!m || !m->valid) {
        printf("mail: no snapshot received\n");
        return;
    }
    /* The middle count is what raises the official client's mail badge (f/gz reads it),
     * so it is worth seeing from here as well as the two box sizes. */
    printf("mail: inbox %d unread %d sent %d%s%s", m->inbox, m->inbox_seen,
           m->sent,
           m->dropped ? " [entries dropped for want of room]" : "",
           m->malformed ? " [a packet failed to decode]" : "");
    if (m->result >= 0)
        printf(" result %d", m->result);
    printf("\n");
    if (m->have_detail) {
        printf("  open id %lld %s \"%s\"\n    %s\n",
               (long long)m->detail.mail_id,
               m->detail.sender[0] ? m->detail.sender : m->detail.recipient,
               m->detail.subject,
               m->detail.body);
    }
    for (i = 0; i < m->count; i++) {
        const mmo_mail *e = &m->entry[i];
        printf("  %s id %lld %s \"%s\"%s\n",
               m->listed_sent ? "sent" : "inbox",
               (long long)e->mail_id,
               m->listed_sent
                   ? (e->recipient[0] ? e->recipient : "?")
                   : (e->sender[0] ? e->sender : "?"),
               e->subject,
               !m->listed_sent && e->unread ? " unread" : "");
    }
}

static void print_link(const openmmo_link *l)
{
    int i;

    if (!l || !l->valid || !l->present) {
        printf("link: none%s\n",
               l && l->malformed ? " [a packet failed to decode]" : "");
        return;
    }
    printf("link: leader %lld members %d%s%s\n",
           (long long)l->leader, l->count,
           l->dropped ? " [entries dropped for want of room]" : "",
           l->malformed ? " [a packet failed to decode]" : "");
    for (i = 0; i < l->count; i++)
        printf("  %s %s id %lld\n",
               l->member[i].name[0] ? l->member[i].name : "?",
               l->member[i].entity_id == l->leader ? "captain" : "member",
               (long long)l->member[i].entity_id);
}

static void print_friends(const openmmo_friends *f)
{
    int i;

    if (!f || !f->valid) {
        printf("friends: no snapshot received\n");
        return;
    }
    printf("friends: %d%s%s\n", f->count,
           f->dropped ? " [entries dropped for want of room]" : "",
           f->malformed ? " [a packet failed to decode]" : "");
    for (i = 0; i < f->count; i++)
        printf("  %s %s id %lld%s\n",
               f->entry[i].name[0] ? f->entry[i].name : "?",
               f->entry[i].online ? "online" : "offline",
               (long long)f->entry[i].player,
               f->entry[i].unknown ? " [unknown field set]" : "");
}

static void print_objectives(const openmmo_objectives *o)
{
    int i;

    if (!o || !o->valid) {
        printf("objectives: no snapshot received\n");
        return;
    }
    printf("objectives: %d%s%s\n", o->count,
           o->dropped ? " [entries dropped for want of room]" : "",
           o->malformed ? " [a packet failed to decode]" : "");
    for (i = 0; i < o->count; i++)
        printf("  id %d value %d count %d\n",
               (int)o->entry[i].id, o->entry[i].value,
               (int)o->entry[i].count);
}

static void print_ui(const openmmo_ui *u)
{
    int i;

    if (!u || !u->valid) {
        printf("ui: no snapshot received\n");
        return;
    }
    printf("ui: last 0x%02x scale %s%d pages %d names %d options %d rows %d%s%s%s\n",
           u->last_op,
           u->scale_valid ? "" : "none ",
           u->scale_valid ? (int)u->scale : 0,
           (u->page_valid[0] ? 1 : 0) + (u->page_valid[1] ? 1 : 0),
           u->names_valid ? u->names.count : 0,
           u->options_valid ? u->options.count : 0,
           u->list_valid ? u->list.count : 0,
           u->menu_visible ? " menu-on" : "",
           (u->confirm_valid && u->confirm.visible) ? " confirm" : "",
           u->prompt_open ? " prompt" : "");
    if (u->malformed)
        printf("  [a packet failed to decode]\n");
    for (i = 0; i < MMO_UI_MENU_TYPES; i++) {
        if (!u->page_valid[i])
            continue;
        printf("  page %d %s len %d\n", i,
               u->page_present[i] ? "present" : "empty",
               u->page_len[i]);
    }
    if (u->names_valid) {
        for (i = 0; i < u->names.count; i++)
            printf("  name %d \"%s\" id %lld\n", i,
                   u->names.entry[i].name[0] ? u->names.entry[i].name : "?",
                   (long long)u->names.entry[i].entity_id);
    }
    if (u->options_valid) {
        for (i = 0; i < u->options.count; i++)
            printf("  option %d type %d sub %d\n", i,
                   (int)u->options.entry[i].type_id,
                   (int)u->options.entry[i].sub_type);
    }
    if (u->list_valid) {
        for (i = 0; i < u->list.count; i++)
            printf("  row %d \"%s\" type %d\n", i,
                   u->list.row[i].label[0] ? u->list.row[i].label : "?",
                   (int)u->list.row[i].row_type);
    }
    if (u->confirm_valid)
        printf("  confirm %s entity %lld wait %u/%u s\n",
               u->confirm.visible ? "open" : "closed",
               (long long)u->confirm.entity_id,
               (unsigned)u->confirm.request_s,
               (unsigned)u->confirm.response_s);
    if (u->prompt_open || u->prompt.kind)
        printf("  prompt kind %d type %d value %d relations %d\n",
               (int)u->prompt.kind, (int)u->prompt.prompt_type,
               (int)u->prompt.value, u->prompt.relation_count);
}

static void print_sync(const openmmo_sync *s)
{
    if (!s || !s->valid) {
        printf("sync: no snapshot received\n");
        return;
    }
    printf("sync: last 0x%02x digest %s transfer %s stream %s image %s\n",
           s->last_op,
           s->digest_valid ? "yes" : "no",
           s->transfer_done ? "open" : (s->transfer_valid ? "partial" : "no"),
           s->stream_done ? "done" : (s->stream_valid ? "partial" : "no"),
           s->image_last ? "done" : (s->image_valid ? "partial" : "no"));
    if (s->malformed)
        printf("  [a packet failed to decode]\n");
    if (s->digest_valid)
        printf("  digest 0x%02x bits %u len %u\n",
               s->digest_op, (unsigned)s->digest_bits,
               (unsigned)s->digest_len);
    if (s->transfer_valid)
        printf("  transfer id %lld size %d got %d%s%s\n",
               (long long)s->transfer_id, s->transfer_size, s->transfer_got,
               s->transfer_done ? " done" : "",
               s->transfer_open_ok ? " opened" : "");
    if (s->transfer_done && s->transfer_plain_len > 0)
        printf("  plain %d \"%.*s\"\n", s->transfer_plain_len,
               s->transfer_plain_len, (const char *)s->transfer_plain);
    if (s->stream_valid)
        printf("  stream id %lld got %d%s\n",
               (long long)s->stream_id, s->stream_got,
               s->stream_done ? " done" : "");
    if (s->image_valid)
        printf("  image ctrl %d type %d len %d%s\n",
               (int)s->image_ctrl, (int)s->image_type, s->image_len,
               s->image_last ? " last" : "");
}

static void print_compete(const openmmo_compete *cp)
{
    int i, j;

    if (!cp || !cp->valid) {
        printf("compete: nothing received\n");
        return;
    }
    printf("compete: last 0x%02x rentals %s tourneys %s count %s bracket %s "
           "board %s\n",
           cp->last_op,
           cp->rentals_valid ? "yes" : "no",
           cp->page_valid ? "yes" : "no",
           cp->count_valid ? "yes" : "no",
           cp->matchups_valid ? "yes" : "no",
           cp->board_valid ? "yes" : "no");
    if (cp->malformed)
        printf("  [a packet failed to decode]\n");
    if (cp->rentals_valid) {
        printf("  rentals mode %d params %d %d %d\n", (int)cp->rentals.mode,
               (int)cp->rentals.param[0], (int)cp->rentals.param[1],
               (int)cp->rentals.param[2]);
        for (i = 0; i < cp->rentals.preview_count; i++)
            printf("    preview species %d level %d\n",
                   (int)cp->rentals.preview[i].species,
                   (int)cp->rentals.preview[i].level);
        for (i = 0; i < cp->rentals.monster_count; i++)
            printf("    monster dex %u level %d \"%s\"\n",
                   (unsigned)cp->rentals.monster[i].dex_id,
                   cp->rentals.monster[i].level,
                   cp->rentals.monster[i].nickname);
    }
    if (cp->page_valid) {
        printf("  tourneys tab %d list %d page %d total %d count %d\n",
               (int)cp->page.tab, cp->page.second_list, (int)cp->page.page,
               cp->page.total, cp->page.count);
        for (i = 0; i < cp->page.count; i++) {
            const mmo_tourney *t = &cp->page.entry[i];

            printf("    #%lld \"%s\" kind %d type %d capacity %d format %d "
                   "mode %d\n",
                   (long long)t->id, t->name, (int)t->kind, (int)t->type,
                   (int)t->capacity, (int)t->format, (int)t->mode);
        }
    }
    if (cp->count_valid)
        printf("  entries tourney %lld entered %d checked-in %d\n",
               (long long)cp->count.tourney_id, (int)cp->count.entered,
               (int)cp->count.checked_in);
    if (cp->matchups_valid) {
        printf("  bracket %d slot(s)\n", cp->matchups.count);
        for (i = 0; i < cp->matchups.count; i++) {
            const mmo_matchup *m = &cp->matchups.entry[i];

            printf("    slot %d winner %d type %d id %lld",
                   i, (int)m->winner, (int)m->type, (long long)m->id);
            if (m->has_entrants)
                printf(" entrants %d %d", (int)m->entrant[0],
                       (int)m->entrant[1]);
            printf("\n");
        }
    }
    if (cp->board_valid) {
        printf("  board category %d %d row(s)\n", (int)cp->board.category,
               cp->board.count);
        for (i = 0; i < cp->board.count; i++) {
            const mmo_board_row *row = &cp->board.row[i];

            printf("    rank %d score %d id %lld mons %d\n", row->rank,
                   row->score, (long long)row->entity_id, row->monster_count);
            for (j = 0; j < row->monster_count; j++)
                printf("      \"%s\" %d %d %d %d\n", row->monster[j].name,
                       (int)row->monster[j].value[0],
                       (int)row->monster[j].value[1],
                       (int)row->monster[j].value[2],
                       (int)row->monster[j].value[3]);
        }
    }
}

static void print_shop(const openmmo_shop *s)
{
    int i;

    if (!s || !s->valid) {
        printf("shop: no catalog received\n");
        return;
    }
    if (!s->open) {
        printf("shop: closed\n");
        return;
    }
    printf("shop: %d of %d line(s)%s%s%s\n", s->count, s->total,
           s->untranslatable ? " (some items unmapped)" : "",
           s->dropped ? " [lines dropped for want of room]" : "",
           s->malformed ? " [a packet failed to decode]" : "");
    for (i = 0; i < s->count; i++) {
        const openmmo_shop_item *it = &s->item[i];
        printf("  line %d: wire %d -> engine %d%s stock %d price %d\n", i,
               it->item_id, it->engine_id,
               it->engine_id ? "" : " (no engine id)",
               it->stock, it->price);
    }
}

static void print_dialog(const openmmo_dialog *d)
{
    if (!d || !d->valid) {
        printf("dialog: no box received\n");
        return;
    }
    if (d->close) {
        printf("dialog: closed (type %d)\n", (int)d->action_type);
        return;
    }
    if (!d->open && !d->awaiting) {
        printf("dialog: type %d text 0x%08x (no string)%s\n",
               (int)d->action_type, (unsigned)d->text_id,
               d->locked ? " locked" : "");
        return;
    }
    if (d->resolved)
        printf("dialog: type %d text 0x%08x -> bank %u entry %u%s\n",
               (int)d->action_type, (unsigned)d->text_id,
               d->bank, d->entry,
               d->locked ? " locked" : "");
    else if (d->text_id == 0)
        printf("dialog: type %d (no string)%s\n",
               (int)d->action_type,
               d->locked ? " locked" : "");
    else
        printf("dialog: type %d text 0x%08x refused (%s)%s\n",
               (int)d->action_type, (unsigned)d->text_id,
               d->why != NULL ? d->why : "unresolvable",
               d->locked ? " locked" : "");
    if (d->choice_count > 0) {
        int i;

        printf("dialog: %d choice(s)", d->choice_count);
        for (i = 0; i < d->choice_count; i++)
            printf(" %d", (int)d->choices[i]);
        printf("\n");
    }
}

static void print_script_move(const openmmo_script_move *s)
{
    int i;

    if (!s || !s->valid) {
        printf("move: no sequence received\n");
        return;
    }
    printf("move: entity %lld flag %u %d byte(s)%s%s mapped %d",
           (long long)s->entity_id, (unsigned)s->flag, (int)s->count,
           s->is_self ? " self" : "",
           s->entity_id == -2 ? " follower" : "",
           s->mapped);
    for (i = 0; i < s->count; i++)
        printf("%s%02x", i == 0 ? " [" : " ", (unsigned)s->actions[i]);
    if (s->count > 0)
        printf("]");
    printf("\n");
}

/* Drive a client to a terminal outcome, narrating the event stream. Returns 0 on
 * success (session held for login, IN_GAME for join), 1 on any failure. */
static int drive(const openmmo_config *cfg)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) {
        fprintf(stderr, "error: out of memory\n");
        return 1;
    }

    int rc = 1;
    int held = 0;
    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_CHARACTERS: {
                printf("created \"%s\" (%s, %s): the account now has %d character(s)",
                       ev.character.name,
                       ev.character.gender ? "female" : "male",
                       mmo_region_name(ev.character.region),
                       ev.character.count);
                if (ev.character.id)
                    printf(", first id %lld", (long long)ev.character.id);
                printf("\n");
                rc = 0;
                goto done;
            }
            case OPENMMO_EV_JOINED: {
                printf("joined the game (playtime %d, reward points %d, "
                       "balance %d)\n", ev.join.playtime, ev.join.reward_points,
                       ev.join.balance);
                const openmmo_world_state *ws = openmmo_client_world_state(c);
                if (ws) {
                    printf("world state: region %d map %d at (%d,%d,%d), money %d, "
                           "gender %d\n", ws->region, ws->map_id, ws->x, ws->y,
                           ws->z, ws->money, ws->gender);
                    printf("world state: character %s id %lld\n",
                           ws->name[0] ? ws->name : "?",
                           (long long)ws->character_id);
                    printf("world state: party %d (%d unmapped), bag %d stacks "
                           "(%d unmapped), %d flags, %d vars\n",
                           ws->party_count, ws->party_untranslatable,
                           ws->item_count, ws->item_untranslatable,
                           ws->flag_count, ws->var_count);
                    if (ws->party_count == 0 && ws->item_count == 0)
                        printf("starter: none (a new character starts empty; "
                               "grass will not start a battle)\n");
                }
                /* The save blocks the seat carried. The engine is what applies
                 * them, so a headless join is the only place they can be seen
                 * at all, without this line the whole of the seat's third
                 * list is invisible outside a windowed session. */
                {
                    const openmmo_script_state *st =
                        openmmo_client_script_state(c);

                    if (st != NULL && st->seated) {
                        printf("save blocks: %d seated", st->block_count);
                        for (int b = 0; b < st->block_count; b++)
                            printf("%s %d (%d bytes)", b ? "," : ":",
                                   st->block[b].id, st->block[b].len);
                        if (st->blocks_dropped)
                            printf(", %d dropped", st->blocks_dropped);
                        printf("\n");
                    }
                }
                print_party(openmmo_client_party(c));
                print_bag(openmmo_client_bag(c));
                print_friends(openmmo_client_friends(c));
                print_guild(openmmo_client_guild(c));
                print_mail(openmmo_client_mail(c));
                print_link(openmmo_client_link(c));
                rc = 0;
                goto done;
            }
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message);
                goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message);
                goto done;
            }
        }

        /* Login-hold success: once authed, hold the idle session a while and
         * report it held if it stays up. */
        if (cfg->mode == OPENMMO_MODE_LOGIN_HOLD &&
            openmmo_client_status(c) == OPENMMO_AUTHED && ++held >= LOGIN_HOLD_FRAMES) {
            printf("session held (idle, still connected)\n");
            rc = 0;
            goto done;
        }

        usleep(2000);
    }
    fprintf(stderr, "error: gave up waiting for the session to settle\n");

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

static int region_from_arg(const char *s, int *out);

/* Mark one select selector. A second one is a refusal, not a last-write-wins. */
static void set_select_how(openmmo_config *cfg, int how)
{
    if (cfg->select_how != OPENMMO_SELECT_FIRST && cfg->select_how != how)
        cfg->select_how = OPENMMO_SELECT_MANY;
    else
        cfg->select_how = how;
}

/* Shared option parsing for the login/join commands. */
static void parse_opts(int argc, char **argv, openmmo_config *cfg)
{
    for (int i = 0; i < argc; i++) {
        int has_next = (i + 1 < argc);
        if (strcmp(argv[i], "--user") == 0 && has_next)
            cfg->user = argv[++i];
        else if (strcmp(argv[i], "--pass") == 0 && has_next)
            cfg->pass = argv[++i];
        else if (strcmp(argv[i], "--any-region") == 0)
            cfg->allow_undrawable_region = 1;
        else if (strcmp(argv[i], "--character") == 0 && has_next) {
            cfg->select_name = argv[++i];
            set_select_how(cfg, OPENMMO_SELECT_NAME);
        } else if (strcmp(argv[i], "--index") == 0 && has_next) {
            cfg->select_index = atoi(argv[++i]);
            set_select_how(cfg, OPENMMO_SELECT_INDEX);
        } else if (strcmp(argv[i], "--region") == 0 && has_next) {
            int region;
            if (region_from_arg(argv[++i], &region) == 0) {
                cfg->select_region = region;
                set_select_how(cfg, OPENMMO_SELECT_REGION);
            } else {
                cfg->select_how = OPENMMO_SELECT_BAD;
            }
        }
    }
}

static int cmd_login(int argc, char **argv)
{
    openmmo_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = "test";
    cfg.pass = "test";
    cfg.mode = OPENMMO_MODE_LOGIN_HOLD;
    parse_opts(argc, argv, &cfg);
    return drive(&cfg);
}

static int cmd_join(int argc, char **argv)
{
    openmmo_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = "test";
    cfg.pass = "test";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    parse_opts(argc, argv, &cfg);
    return drive(&cfg);
}

/* Wire slot names, in mask-bit order. "facial" and "rod" are accepted as
 * short forms of facial_hair and fishing_rod. */
static int skin_slot_from_name(const char *s)
{
    static const char *const names[MMO_SKIN_SLOTS] = {
        "forehead", "hat", "hair", "eyes", "facial_hair", "back",
        "top", "gloves", "footwear", "leggings", "fishing_rod", "bike",
    };
    for (int i = 0; i < MMO_SKIN_SLOTS; i++) {
        if (strcmp(s, names[i]) == 0)
            return i;
    }
    if (strcmp(s, "facial") == 0) return MMO_SKIN_FACIAL_HAIR;
    if (strcmp(s, "rod") == 0) return MMO_SKIN_FISHING_ROD;
    return -1;
}

static int region_from_arg(const char *s, int *out)
{
    if (!s || !out)
        return -1;
    for (int i = 0; i < mmo_region_count(); i++) {
        const mmo_region *r = mmo_region_at(i);
        if (strcasecmp(s, r->name) == 0) {
            *out = r->id;
            return 0;
        }
    }
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end != s && *end == '\0' && v >= 0 && v <= 255) {
        *out = (int)v;
        return 0;
    }
    return -1;
}

static int parse_skin_arg(const char *s, openmmo_config *cfg)
{
    /* SLOT=TYPE:COLOR */
    char slot[24];
    unsigned type = 0, color = 0;
    if (sscanf(s, "%23[^=]=%u:%u", slot, &type, &color) != 3) {
        fprintf(stderr, "create: --skin wants SLOT=TYPE:COLOR (got %s)\n", s);
        return -1;
    }
    int i = skin_slot_from_name(slot);
    if (i < 0) {
        fprintf(stderr, "create: unknown skin slot '%s'\n", slot);
        return -1;
    }
    if (type > MMO_SKIN_TYPE_MASK || color > MMO_SKIN_COLOR_MASK) {
        fprintf(stderr, "create: skin %s type/colour out of range\n", slot);
        return -1;
    }
    cfg->create_skin_mask = (u16)(cfg->create_skin_mask | (1u << i));
    cfg->create_skin_word[i] = (u16)((type & MMO_SKIN_TYPE_MASK) |
                                     ((color & MMO_SKIN_COLOR_MASK) << MMO_SKIN_COLOR_SHIFT));
    return 0;
}

static int cmd_create(int argc, char **argv)
{
    openmmo_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = "test";
    cfg.pass = "test";
    cfg.mode = OPENMMO_MODE_CREATE_CHAR;
    cfg.create_region = -1;
    cfg.create_skin_region = -1;

    for (int i = 0; i < argc; i++) {
        int has_next = (i + 1 < argc);
        if (strcmp(argv[i], "--name") == 0 && has_next)
            cfg.create_name = argv[++i];
        else if (strcmp(argv[i], "--gender") == 0 && has_next) {
            const char *g = argv[++i];
            if (strcmp(g, "male") == 0 || strcmp(g, "m") == 0 || strcmp(g, "0") == 0)
                cfg.create_gender = 0;
            else if (strcmp(g, "female") == 0 || strcmp(g, "f") == 0 || strcmp(g, "1") == 0)
                cfg.create_gender = 1;
            else {
                fprintf(stderr, "create: --gender is male or female\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--region") == 0 && has_next) {
            if (region_from_arg(argv[++i], &cfg.create_region) != 0) {
                fprintf(stderr, "create: unknown region '%s'\n", argv[i]);
                return 2;
            }
        } else if (strcmp(argv[i], "--body") == 0 && has_next) {
            const mmo_appearance *a = mmo_appearance_by_name(argv[++i]);
            int idx = -1, type, j;
            if (!a || !a->offered) {
                fprintf(stderr, "create: unknown or unoffered body '%s'\n",
                        argv[i]);
                return 2;
            }
            for (j = 0; j < mmo_appearance_count(); j++) {
                if (mmo_appearance_at(j) == a) {
                    idx = j;
                    break;
                }
            }
            type = mmo_appearance_body_type(idx);
            if (type < 0) {
                fprintf(stderr, "create: body '%s' is not offered\n", a->name);
                return 2;
            }
            cfg.create_skin_mask = (u16)(cfg.create_skin_mask | (1u << MMO_SKIN_FOREHEAD));
            cfg.create_skin_word[MMO_SKIN_FOREHEAD] =
                (u16)(type & MMO_SKIN_TYPE_MASK);
        } else if (strcmp(argv[i], "--skin") == 0 && has_next) {
            if (parse_skin_arg(argv[++i], &cfg) != 0)
                return 2;
        }
    }
    parse_opts(argc, argv, &cfg);

    if (!cfg.create_name || cfg.create_name[0] == '\0') {
        fprintf(stderr, "create: --name is required\n");
        return 2;
    }
    if (mmo_utf16_units(cfg.create_name) > MMO_CHAR_NAME_MAX) {
        fprintf(stderr, "create: a name is longer than 32 characters\n");
        return 2;
    }
    return drive(&cfg);
}

/* --- pair: two sessions, one map, mutual sight ------------------------------ */

/* Per-client observation state. */
typedef struct {
    const char    *tag;         /* "A" / "B", for the log */
    openmmo_client *c;
    int            joined;       /* reached IN_GAME */
    int            ready;        /* past the login handshake (or failed it) */
    int            spawns_seen;  /* remote SPAWN events observed */
    int            steps_seen;   /* remote STEP events observed */
    int            chats_seen;   /* the play-path chat line heard (type 0) */
    int            corrections_seen; /* self-correction (snap-back) events observed */
    int            have_map;     /* world-state map identity captured on JOINED */
    int            region, bank, map;
} pair_side;

/* A line no join-notice or welcome will collide with. A hears the echo, B the
 * broadcast; the check is that B heard it. */
static const char PAIR_CHAT[] = "play-path hello";

/* The engine facings, tried in turn until the server accepts one (a blocked step
 * is snapped back silently, so an unwalkable direction simply yields no peer
 * step). NORTH=0 SOUTH=1 WEST=2 EAST=3. */
static const int WALK_DIRS[4] = { 1, 0, 2, 3 };
static const char *DIR_NAME[4] = { "north", "south", "west", "east" };

/* Drain and narrate one side's event queue, updating its counters. */
static void pair_drain(pair_side *s)
{
    openmmo_event ev;
    while (openmmo_client_poll_event(s->c, &ev)) {
        switch (ev.kind) {
        case OPENMMO_EV_STATUS:
            printf("%s: -> %s\n", s->tag, openmmo_status_name(ev.status));
            if (ev.status == OPENMMO_AUTHED
                || ev.status == OPENMMO_REQUESTING_GAME
                || ev.status == OPENMMO_JOINING_GAME
                || ev.status == OPENMMO_IN_GAME
                || ev.status == OPENMMO_FAILED)
                s->ready = 1;
            break;
        case OPENMMO_EV_CHAT:
            print_chat_line(s->tag, &ev);
            if (ev.chat.type == MMO_CHAT_NORMAL
                && strcmp(ev.chat.text, PAIR_CHAT) == 0)
                s->chats_seen++;
            break;
        case OPENMMO_EV_JOINED: {
            int x = -1, z = -1;
            const openmmo_world_state *ws = openmmo_client_world_state(s->c);
            openmmo_client_self_tile(s->c, &x, &z);
            if (ws && ws->valid) {
                s->have_map = 1;
                s->region = ws->region;
                s->bank = ws->bank_id;
                s->map = ws->map_id;
                printf("%s: in region %d bank %d map %d at (%d, %d)\n",
                       s->tag, s->region, s->bank, s->map, x, z);
            } else {
                printf("%s: in the map at (%d, %d)\n", s->tag, x, z);
            }
            s->joined = 1;
            break;
        }
        case OPENMMO_EV_FAILED:
            printf("%s: error: %s\n", s->tag, ev.message);
            break;
        case OPENMMO_EV_DISCONNECTED:
            printf("%s: session ended: %s\n", s->tag, ev.message);
            break;
        case OPENMMO_EV_ENTITY_SPAWN:
            s->spawns_seen++;
            printf("%s: sees a %s player appear at (%d, %d) [slot %d] named \"%s\"\n",
                   s->tag, ev.entity.gender ? "female" : "male",
                   ev.entity.x, ev.entity.z, ev.entity.slot, ev.entity.name);
            break;
        case OPENMMO_EV_ENTITY_STEP:
            s->steps_seen++;
            printf("%s: sees that player step to (%d, %d)\n",
                   s->tag, ev.entity.x, ev.entity.z);
            break;
        case OPENMMO_EV_ENTITY_TURN:
            printf("%s: sees that player turn (dir %d)\n", s->tag, ev.entity.dir);
            break;
        case OPENMMO_EV_ENTITY_DESPAWN:
            printf("%s: sees that player leave [slot %d]\n", s->tag, ev.entity.slot);
            break;
        case OPENMMO_EV_SELF_CORRECT:
            s->corrections_seen++;
            printf("%s: server corrected us to (%d, %d) facing %d\n",
                   s->tag, ev.entity.x, ev.entity.z, ev.entity.dir);
            break;
        case OPENMMO_EV_DIALOG: {
            const openmmo_dialog *box = openmmo_client_dialog(s->c);
            if (box && box->awaiting) {
                u8 r = (box->action_type == MMO_DIALOG_ACTION_YESNO
                        || box->action_type == MMO_DIALOG_ACTION_MENU
                        || box->action_type == MMO_DIALOG_ACTION_LIST)
                           ? 1
                           : 0;
                openmmo_client_reply_dialog(s->c, r);
            }
            break;
        }
        }
    }
}

/* Send one tile step for `mover` from the tile the server spawned it on. Returns
 * 0 on a queued send, -1 if the self tile is not yet known. */
static int pair_walk(pair_side *mover, int dir)
{
    int x, z;
    if (openmmo_client_self_tile(mover->c, &x, &z) != 0)
        return -1;
    printf("%s: walk %s from (%d, %d)\n", mover->tag, DIR_NAME[dir & 3], x, z);
    openmmo_client_send_move(mover->c, x, z, dir, 0);
    return 0;
}

/* Undo the step `mover` just took, so a run ends where it began. */
static void pair_step_back(pair_side *mover, int *last_dir)
{
    if (*last_dir < 0)
        return;
    pair_walk(mover, *last_dir ^ 1);
    *last_dir = -1;
}

/* Drive two clients to IN_GAME, watch them see each other, walk each once
 * and confirm the peer observes it, then send a chat line the other has to
 * hear. Returns 0 if both saw the other spawn, each peer observed a step,
 * and B heard A's line, 1 otherwise. */
static int drive_pair(const openmmo_config *ca, const openmmo_config *cb)
{
    enum { PH_JOIN, PH_SETTLE, PH_WALK_A, PH_WALK_B, PH_CHAT, PH_DONE } ph = PH_JOIN;
    pair_side a = { "A", openmmo_client_new(), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    pair_side b = { "B", openmmo_client_new(), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    if (!a.c || !b.c) {
        fprintf(stderr, "error: out of memory\n");
        openmmo_client_free(a.c);
        openmmo_client_free(b.c);
        return 1;
    }
    /* Two handshakes started in the same millisecond against one login
     * process get INVALID_PASSWORD on one or both. Two OS processes at
     * the same moment do not. Start B once A is through the login
     * handshake (or has failed it). */
    openmmo_client_start(a.c, ca);

    int b_started = 0;

    int phase_frames = 0;   /* frames spent in the current phase */
    int dir_idx = 0;        /* which WALK_DIRS entry the mover is trying */
    int a_last_dir = -1;    /* the step each side owes back; -1 when it owes none */
    int b_last_dir = -1;
    const int SETTLE = 120;      /* let the spawn exchange arrive */
    const int WALK_WAIT = 120;   /* frames to wait for the peer to report a step */
    const int CHAT_WAIT = 250;   /* frames to wait for B to hear A's line */
    const int JOIN_WAIT = 8000;  /* ~16s at the 2ms pacing below */

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(a.c);
        if (b_started)
            openmmo_client_pump(b.c);
        pair_drain(&a);
        if (b_started)
            pair_drain(&b);
        if (!b_started && a.ready) {
            openmmo_client_start(b.c, cb);
            b_started = 1;
        }
        phase_frames++;

        switch (ph) {
        case PH_JOIN:
            if (a.joined && b.joined) { ph = PH_SETTLE; phase_frames = 0; }
            else if (phase_frames > JOIN_WAIT) {
                fprintf(stderr, "error: both sessions did not reach the map "
                        "(A joined=%d, B joined=%d)\n", a.joined, b.joined);
                goto report;
            }
            break;
        case PH_SETTLE:
            if (phase_frames >= SETTLE) {
                if (a.have_map && b.have_map &&
                    (a.region != b.region || a.bank != b.bank || a.map != b.map)) {
                    fprintf(stderr,
                            "error: A is on region %d bank %d map %d, "
                            "B is on region %d bank %d map %d, "
                            "they cannot see each other\n",
                            a.region, a.bank, a.map, b.region, b.bank, b.map);
                    goto report;
                }
                ph = PH_WALK_A; phase_frames = 0; dir_idx = 0;
            }
            break;
        case PH_WALK_A:
            /* A story box owns the walker (the rival on the first step). */
            if (openmmo_client_in_dialog(a.c) || openmmo_client_in_dialog(b.c)) {
                if (phase_frames > 0)
                    phase_frames--;
                break;
            }
            /* B observing a step confirms A's move crossed the server. */
            if (b.steps_seen > 0) {
                pair_step_back(&a, &a_last_dir);
                ph = PH_WALK_B; phase_frames = 0; dir_idx = 0; break;
            }
            if (phase_frames % WALK_WAIT == 0) {
                if (dir_idx >= 4) { ph = PH_WALK_B; phase_frames = 0; dir_idx = 0; break; }
                a_last_dir = WALK_DIRS[dir_idx++];
                pair_walk(&a, a_last_dir);
            }
            break;
        case PH_WALK_B:
            if (openmmo_client_in_dialog(a.c) || openmmo_client_in_dialog(b.c)) {
                if (phase_frames > 0)
                    phase_frames--;
                break;
            }
            if (a.steps_seen > 0) {
                pair_step_back(&b, &b_last_dir);
                ph = PH_CHAT; phase_frames = 0; break;
            }
            if (phase_frames % WALK_WAIT == 0) {
                if (dir_idx >= 4) { ph = PH_CHAT; phase_frames = 0; break; }
                b_last_dir = WALK_DIRS[dir_idx++];
                pair_walk(&b, b_last_dir);
            }
            break;
        case PH_CHAT:
            if (b.chats_seen > 0) { ph = PH_DONE; break; }
            if (phase_frames == 1) {
                printf("A: chat %s\n", PAIR_CHAT);
                if (openmmo_client_send_chat(a.c, PAIR_CHAT) != 0) {
                    fprintf(stderr, "error: chat send failed (not in game?)\n");
                    ph = PH_DONE;
                    break;
                }
            }
            if (phase_frames > CHAT_WAIT) { ph = PH_DONE; break; }
            break;
        case PH_DONE:
            goto report;
        }
        usleep(2000);
    }

report: {
    printf("\nresult: A saw %d spawn(s)/%d step(s), B saw %d spawn(s)/%d step(s)/%d chat(s)\n",
           a.spawns_seen, a.steps_seen, b.spawns_seen, b.steps_seen, b.chats_seen);
    int same_map = a.have_map && b.have_map &&
                   a.region == b.region && a.bank == b.bank && a.map == b.map;
    int mutual_sight = a.spawns_seen > 0 && b.spawns_seen > 0;
    int mutual_walk = a.steps_seen > 0 && b.steps_seen > 0;
    int heard = b.chats_seen > 0;
    if (mutual_sight && mutual_walk && heard)
        printf("ok: two clients on one map saw each other spawn, walk and talk\n");
    else if (!same_map && a.have_map && b.have_map)
        printf("fail: different maps, so presence was never tested\n");
    else if (mutual_sight && mutual_walk)
        printf("partial: mutual sight and walk confirmed, chat not heard\n");
    else if (mutual_sight)
        printf("partial: mutual sight confirmed, walk not observed both ways\n");
    else if (same_map)
        printf("fail: same map, but the two clients did not see each other\n");
    else
        printf("fail: the two clients did not see each other\n");
    openmmo_client_disconnect(a.c);
    openmmo_client_disconnect(b.c);
    openmmo_client_free(a.c);
    openmmo_client_free(b.c);
    return (mutual_sight && mutual_walk && heard) ? 0 : 1;
}
}

static int cmd_pair(int argc, char **argv)
{
    openmmo_config a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    a.mode = b.mode = OPENMMO_MODE_GAME_JOIN;
    a.user = "admin"; a.pass = "admin";
    b.user = "test";  b.pass = "test";

    for (int i = 0; i < argc; i++) {
        int has_next = (i + 1 < argc);
        if (strcmp(argv[i], "--user") == 0 && has_next)
            a.user = argv[++i];
        else if (strcmp(argv[i], "--pass") == 0 && has_next)
            a.pass = argv[++i];
        else if (strcmp(argv[i], "--user2") == 0 && has_next)
            b.user = argv[++i];
        else if (strcmp(argv[i], "--pass2") == 0 && has_next)
            b.pass = argv[++i];
        else if (strcmp(argv[i], "--character") == 0 && has_next) {
            a.select_name = argv[++i];
            set_select_how(&a, OPENMMO_SELECT_NAME);
        } else if (strcmp(argv[i], "--character2") == 0 && has_next) {
            b.select_name = argv[++i];
            set_select_how(&b, OPENMMO_SELECT_NAME);
        } else if (strcmp(argv[i], "--index") == 0 && has_next) {
            a.select_index = atoi(argv[++i]);
            set_select_how(&a, OPENMMO_SELECT_INDEX);
        } else if (strcmp(argv[i], "--index2") == 0 && has_next) {
            b.select_index = atoi(argv[++i]);
            set_select_how(&b, OPENMMO_SELECT_INDEX);
        }
    }
    return drive_pair(&a, &b);
}

/* --- contest: two sessions queue, are seated, and relay --------------------- */
typedef struct {
    const char    *tag;
    openmmo_client *c;
    int ready;
    int joined;
    int queued;
    int seated;
    int seat;
    int humans;
    int got_blob;
    int blob_from;
    int asked;
    int requeued;
    int refused;
    int left;
    int peer_gone;
    int failed;
} contest_side;

static void contest_drain(contest_side *s)
{
    openmmo_event ev;

    while (openmmo_client_poll_event(s->c, &ev)) {
        switch (ev.kind) {
        case OPENMMO_EV_STATUS:
            printf("%s: -> %s\n", s->tag, openmmo_status_name(ev.status));
            if (ev.status == OPENMMO_AUTHED
                || ev.status == OPENMMO_REQUESTING_GAME
                || ev.status == OPENMMO_JOINING_GAME
                || ev.status == OPENMMO_IN_GAME
                || ev.status == OPENMMO_FAILED)
                s->ready = 1;
            if (ev.status == OPENMMO_FAILED)
                s->failed = 1;
            break;
        case OPENMMO_EV_JOINED:
            s->joined = 1;
            break;
        case OPENMMO_EV_CONTEST: {
            const openmmo_contest *ct = openmmo_client_contest(s->c);

            /* Neither queued nor seated after having asked: the server said no
             * out loud. A refusal that arrived as silence would leave the
             * Contest Hall's script paused with a player standing at the desk,
             * which is what this drive checks before it checks anything else. */
            if (!ct->valid && !ct->queued && s->asked) {
                s->refused = 1;
                printf("%s: the queue request was refused\n", s->tag);
                break;
            }
            if (ct->valid && !s->seated) {
                s->seated = 1;
                s->seat = ct->seat;
                s->humans = ct->humans;
                printf("%s: seated in contest %d as seat %d of %d, rank %d type %d\n",
                       s->tag, ct->session_id, ct->seat, ct->humans,
                       ct->rank, ct->type);
                for (int i = 0; i < ct->humans; i++)
                    printf("%s:   seat %d is %s\n", s->tag, i,
                           ct->contestant[i].name[0] ? ct->contestant[i].name : "?");
            } else if (ct->queued && !s->seated && !s->queued) {
                s->queued = 1;
                printf("%s: queued (%d of %d waiting)\n", s->tag,
                       ct->queued_have, ct->queued_want);
            }
            break;
        }
        default:
            break;
        }
    }
    /* The relayed half is a queue rather than an event, for the same reason a
     * link battle's blobs are: the engine drains it on the contest's own clock. */
    {
        mmo_contest_comm blob;

        while (openmmo_client_contest_recv(s->c, &blob)) {
            if (blob.kind == MMO_CONTEST_KIND_LEAVE) {
                /* The engine reads this as CommSys_IsPlayerConnected turning
                 * false for that seat, which is what lets every barrier after
                 * it stop waiting for a console that has gone. */
                s->peer_gone = 1;
                printf("%s: seat %d left the contest\n", s->tag, blob.seat);
            }
            if (blob.kind == MMO_CONTEST_KIND_DATA) {
                s->got_blob = 1;
                s->blob_from = blob.seat;
                printf("%s: received %d byte(s) from seat %d, command %d\n",
                       s->tag, blob.len, blob.seat,
                       blob.len > 0 ? blob.data[0] : -1);
            }
        }
    }
}

/*
 * `drop` runs the other half of the story: instead of both sides finishing, B walks out once
 * the contest is seated and A has to be told.
 */
static int drive_contest(const openmmo_config *ca, const openmmo_config *cb,
                         int rank, int type, int drop)
{
    enum { PH_JOIN, PH_REFUSED, PH_QUEUE, PH_SEATED, PH_RELAY, PH_RESULT,
           PH_DROP, PH_SETTLE, PH_DONE } ph = PH_JOIN;
    contest_side a = { .tag = "A", .c = openmmo_client_new() };
    contest_side b = { .tag = "B", .c = openmmo_client_new() };
    int b_started = 0, phase_frames = 0, rc = 1;
    const int JOIN_WAIT = 8000;
    const int STEP_WAIT = 3000;
    /* The lobby holds a short group for a window, in case a third player is a
     * few seconds behind, so waiting to be seated is the one step that takes
     * longer than a round trip. At the 2ms pacing below this is ~50 seconds. */
    const int SEAT_WAIT = 25000;
    /* A refusal comes back within one round trip, so this is only long enough
     * that a loopback which hiccups is not read as a pass. */
    const int SETTLE_WAIT = 500;

    if (!a.c || !b.c) {
        fprintf(stderr, "error: out of memory\n");
        openmmo_client_free(a.c);
        openmmo_client_free(b.c);
        return 1;
    }
    openmmo_client_start(a.c, ca);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(a.c);
        if (b_started)
            openmmo_client_pump(b.c);
        contest_drain(&a);
        if (b_started)
            contest_drain(&b);
        if (!b_started && a.ready) {
            openmmo_client_start(b.c, cb);
            b_started = 1;
        }
        if (a.failed || (b_started && b.failed))
            goto done;
        phase_frames++;

        switch (ph) {
        case PH_JOIN:
            if (a.joined && b.joined) {
                /* A slot with nothing in it first. The server has to say no on
                 * the wire and not merely in a chat line: this side queues
                 * itself the moment it asks, and the Contest Hall's script then
                 * waits on the answer. */
                openmmo_client_contest_queue(a.c, rank, type,
                                             OPENMMO_PARTY_MAX - 1);
                a.asked = 1;
                printf("A: asked with an empty party slot\n");
                ph = PH_REFUSED;
                phase_frames = 0;
            } else if (phase_frames > JOIN_WAIT) {
                fprintf(stderr, "error: both sessions did not reach the map\n");
                goto done;
            }
            break;

        case PH_REFUSED:
            if (a.refused) {
                a.asked = 0;
                a.refused = 0;
                openmmo_client_contest_queue(a.c, rank, type, 0);
                openmmo_client_contest_queue(b.c, rank, type, 0);
                a.asked = b.asked = 1;
                printf("both sessions asked for a rank %d type %d contest\n",
                       rank, type);
                ph = PH_QUEUE;
                phase_frames = 0;
            } else if (phase_frames > STEP_WAIT) {
                fprintf(stderr, "error: an empty party slot was not refused;"
                                " the desk would wait forever\n");
                goto done;
            }
            break;

        case PH_QUEUE:
            if (a.seated && b.seated) {
                if (a.seat == b.seat) {
                    fprintf(stderr, "error: both players were given seat %d\n",
                            a.seat);
                    goto done;
                }
                if (a.humans != 2 || b.humans != 2) {
                    fprintf(stderr, "error: a two-player contest seated %d and"
                                    " %d humans\n", a.humans, b.humans);
                    goto done;
                }
                ph = PH_SEATED;
                phase_frames = 0;
            } else if (phase_frames > SEAT_WAIT) {
                fprintf(stderr, "error: the lobby did not seat both players"
                                " (A %s, B %s)\n",
                        a.seated ? "seated" : "waiting",
                        b.seated ? "seated" : "waiting");
                goto done;
            }
            break;

        case PH_SEATED: {
            if (drop) {
                /* A plays its contest out and says so while B is still seated,
                 * so B is the seat the result is waiting on when it goes. */
                static const u8 alone[2] = { 0, 1 };

                openmmo_client_contest_result(a.c, alone, 2);
                printf("A: reported the placements while B was still in\n");
                ph = PH_DROP;
                phase_frames = 0;
                break;
            }
            /* One of the contest's own commands: id 26 is the seed exchange
             * every link contest opens with. The bytes are the engine's and
             * nothing on this wire reads them, so any body will do to prove the
             * relay carries one whole and names its sender. */
            static const u8 seed[] = { 26, 110, 0, 1 };

            openmmo_client_contest_send(a.c, MMO_CONTEST_KIND_DATA,
                                        seed, sizeof seed);
            printf("A: broadcast command %d (%d bytes)\n", seed[0],
                   (int)sizeof seed);
            ph = PH_RELAY;
            phase_frames = 0;
            break;
        }

        case PH_DROP:
            /* Far enough after the report that the server has taken it: the two
             * travel on different sockets, so nothing but the gap orders them. */
            if (!b.left) {
                if (phase_frames < 100)
                    break;
                b.left = 1;
                /* B is gone. The server hears the socket close and tells
                 * everyone still seated, which is what the engine reads as
                 * CommSys_IsPlayerConnected turning false for that seat. */
                openmmo_client_disconnect(b.c);
                printf("B: left without finishing\n");
                phase_frames = 0;
                break;
            }
            if (a.peer_gone) {
                printf("contest: a seat that walked out was announced to the"
                       " one still in it\n");
                ph = PH_SETTLE;
                phase_frames = 0;
                break;
            }
            if (phase_frames > STEP_WAIT) {
                fprintf(stderr, "error: B left and A was never told; every"
                                " barrier after this would wait forever\n");
                goto done;
            }
            break;

        case PH_RELAY:
            if (b.got_blob) {
                if (b.blob_from != a.seat) {
                    fprintf(stderr, "error: B was told the blob came from seat"
                                    " %d, but A holds seat %d\n",
                            b.blob_from, a.seat);
                    goto done;
                }
                if (a.got_blob) {
                    fprintf(stderr, "error: A received its own broadcast; the"
                                    " relay must not loop a sender back\n");
                    goto done;
                }
                ph = PH_RESULT;
                phase_frames = 0;
            } else if (phase_frames > STEP_WAIT) {
                fprintf(stderr, "error: the broadcast never reached B\n");
                goto done;
            }
            break;

        case PH_RESULT: {
            /* Both sides agree: seat 0 won. A contest whose clients disagree is
             * one the server must refuse, but that is a check for a suite
             * rather than a live drive, here the point is that agreement
             * settles and neither side is left waiting. */
            static const u8 placement[2] = { 0, 1 };

            openmmo_client_contest_result(a.c, placement, 2);
            openmmo_client_contest_result(b.c, placement, 2);
            printf("both sides reported the same placements\n");
            ph = PH_DONE;
            phase_frames = 0;
            break;
        }

        case PH_SETTLE:
            if (!a.requeued) {
                a.requeued = 1;
                a.asked = 1;
                openmmo_client_contest_queue(a.c, rank, type, 0);
                phase_frames = 0;
                break;
            }
            if (a.refused) {
                fprintf(stderr, "error: A is still in a contest nobody is left"
                                " to finish; the result it reported was never"
                                " taken\n");
                goto done;
            }
            if (phase_frames > SETTLE_WAIT) {
                printf("contest: the seat that stayed was released once it"
                       " reported\n");
                openmmo_client_contest_cancel(a.c);
                rc = 0;
                goto done;
            }
            break;

        case PH_DONE:
            if (phase_frames > 120) {
                int dropped = openmmo_client_contest_dropped(a.c)
                              + openmmo_client_contest_dropped(b.c);

                if (dropped) {
                    fprintf(stderr, "error: %d relayed blob(s) were dropped\n",
                            dropped);
                    goto done;
                }
                printf("contest: the queue seated two players, the relay named"
                       " its sender, and the result settled\n");
                rc = 0;
                goto done;
            }
            break;
        }
        mmo_plat_sleep_us(2000);
    }
    fprintf(stderr, "error: the contest drive ran out of frames\n");

done:
    openmmo_client_disconnect(a.c);
    openmmo_client_disconnect(b.c);
    openmmo_client_free(a.c);
    openmmo_client_free(b.c);
    return rc;
}

static int cmd_contest(int argc, char **argv)
{
    openmmo_config a, b;
    int rank = 0, type = 0, drop = 0;

    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    a.mode = b.mode = OPENMMO_MODE_GAME_JOIN;
    a.user = "admin"; a.pass = "admin";
    b.user = "test";  b.pass = "test";
    a.local_scripts = b.local_scripts = 1;

    for (int i = 0; i < argc; i++) {
        int has_next = (i + 1 < argc);
        if (strcmp(argv[i], "--user") == 0 && has_next)
            a.user = argv[++i];
        else if (strcmp(argv[i], "--pass") == 0 && has_next)
            a.pass = argv[++i];
        else if (strcmp(argv[i], "--user2") == 0 && has_next)
            b.user = argv[++i];
        else if (strcmp(argv[i], "--pass2") == 0 && has_next)
            b.pass = argv[++i];
        else if (strcmp(argv[i], "--character") == 0 && has_next) {
            a.select_name = argv[++i];
            set_select_how(&a, OPENMMO_SELECT_NAME);
        } else if (strcmp(argv[i], "--character2") == 0 && has_next) {
            b.select_name = argv[++i];
            set_select_how(&b, OPENMMO_SELECT_NAME);
        } else if (strcmp(argv[i], "--rank") == 0 && has_next) {
            rank = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--type") == 0 && has_next) {
            type = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--drop") == 0) {
            drop = 1;
        }
    }
    return drive_contest(&a, &b, rank, type, drop);
}

/* --- warp: a server-driven map transition, watched live --------------------- */

/* Engine facings, matching client.h's DIR_* (NORTH=0/SOUTH=1/WEST=2/EAST=3). */
enum { WDIR_NORTH = 0, WDIR_SOUTH = 1, WDIR_WEST = 2, WDIR_EAST = 3 };

static int drive_warp(const openmmo_config *cfg, int tx, int tz)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }

    /* Line-buffer so progress is visible while this long-running walk is in flight,
     * not only once it returns. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1;
    int joined = 0;
    int px = 0, pz = 0;      /* predicted tile */
    int axis_x_first = 1;    /* which axis the walker reduces first */
    int nudge_idx = 0;       /* rotates the step-off direction when on the warp tile */
    int warps_seen = 0;
    const int WANT_WARPS = 2;
    const int STEP_INTERVAL = 60; /* frames between steps (~120ms), room for a snap-back */
    int since_step = 0;

    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED:
                if (openmmo_client_self_tile(c, &px, &pz) == 0) {
                    joined = 1;
                    since_step = 0;
                    printf("in the map at (%d, %d); walking to the warp at (%d, %d)\n",
                           px, pz, tx, tz);
                }
                break;
            case OPENMMO_EV_SELF_CORRECT:
                /* The last step hit something the server knew about; take its tile
                 * as truth and try the other axis first next time. */
                px = ev.entity.x; pz = ev.entity.z;
                axis_x_first = !axis_x_first;
                printf("snapped back to (%d, %d)\n", px, pz);
                break;
            case OPENMMO_EV_WARP:
                warps_seen++;
                printf("WARP #%d -> region %d bank %d map %d at (%d, %d) dir %d\n",
                       warps_seen, ev.warp.region, ev.warp.bank, ev.warp.map,
                       ev.warp.x, ev.warp.z, ev.warp.dir);
                px = ev.warp.x; pz = ev.warp.z; /* re-anchor from the placement */
                since_step = 0;
                if (warps_seen >= WANT_WARPS) { rc = 0; goto done; }
                break;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }

        if (joined && ++since_step >= STEP_INTERVAL) {
            since_step = 0;
            int dx = tx - px, dz = tz - pz;
            int dir = -1;
            if (dx == 0 && dz == 0) {
                /* Already on the warp tile, a warp fires on stepping onto it, not
                 * on standing there. Step off; the greedy walk re-enters next tick
                 * and triggers the transition. Rotate the direction so a blocked
                 * side (a snap-back keeps us here) is not retried forever. */
                static const int NUDGE[4] =
                    { WDIR_SOUTH, WDIR_NORTH, WDIR_EAST, WDIR_WEST };
                dir = NUDGE[nudge_idx++ & 3];
            } else if (axis_x_first) {
                if (dx) dir = dx > 0 ? WDIR_EAST : WDIR_WEST;
                else if (dz) dir = dz > 0 ? WDIR_SOUTH : WDIR_NORTH;
            } else {
                if (dz) dir = dz > 0 ? WDIR_SOUTH : WDIR_NORTH;
                else if (dx) dir = dx > 0 ? WDIR_EAST : WDIR_WEST;
            }
            if (dir >= 0) {
                openmmo_client_send_move(c, px, pz, dir, 0);
                switch (dir) {
                case WDIR_EAST:  px++; break;
                case WDIR_WEST:  px--; break;
                case WDIR_SOUTH: pz++; break;
                case WDIR_NORTH: pz--; break;
                }
            }
        }

        usleep(2000);
    }
    fprintf(stderr, "error: only saw %d of %d warps\n", warps_seen, WANT_WARPS);

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

static int cmd_warp(int argc, char **argv)
{
    openmmo_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    int tx = 10, tz = 2;
    for (int i = 0; i < argc; i++) {
        int has_next = (i + 1 < argc);
        if (strcmp(argv[i], "--tx") == 0 && has_next) tx = atoi(argv[++i]);
        else if (strcmp(argv[i], "--tz") == 0 && has_next) tz = atoi(argv[++i]);
    }
    parse_opts(argc, argv, &cfg); /* host/port/user/pass/gameport */
    return drive_warp(&cfg, tx, tz);
}

/* --- map: the server's scene and a seamless map-edge crossing, watched live ---- */

static const char *weather_name(int w)
{
    /* A few of the common Weather ordinals (common/enums/Weather.kt); others print
     * as their number, which is enough to see the server drive it. */
    switch (w) {
    case 0:  return "IN_HOUSE";
    case 2:  return "REGULAR";
    case 3:  return "RAINY";
    case 11: return "CLOUDY";
    default: return NULL;
    }
}

static int drive_map(const openmmo_config *cfg)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* Probe the edges in this order; each is an engine DIR_*. */
    static const int DIR_ORDER[4] =
        { WDIR_SOUTH, WDIR_NORTH, WDIR_EAST, WDIR_WEST };
    static const char *DIR_NAME[4] = { "south", "north", "east", "west" };

    int rc = 1, joined = 0;
    int px = 0, pz = 0;
    int di = 0;                  /* which edge we are walking toward */
    int walls = 0;              /* consecutive snap-backs in this direction */
    int steps = 0;              /* steps taken in this direction */
    const int WALL_LIMIT = 2, MAX_STEPS = 40;
    const int STEP_INTERVAL = 60;
    int since_step = 0;

    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_MAP: {
                const char *wn = weather_name(ev.map.weather);
                printf("map region %d bank %d map %d: %s, weather %d%s%s, "
                       "lighting %d, type %d",
                       ev.map.region, ev.map.bank, ev.map.map,
                       ev.map.is_nds ? "NDS" : "GBA",
                       ev.map.weather, wn ? " " : "", wn ? wn : "",
                       ev.map.lighting, ev.map.map_type);
                if (!ev.map.is_nds)
                    printf(", %dx%d, encounter %d",
                           ev.map.width, ev.map.height, ev.map.encounter_type);
                printf("\n");
                break;
            }
            case OPENMMO_EV_WEATHER:
                if (ev.weather.source == 0)
                    printf("weather mode %d %s\n", ev.weather.mode,
                           ev.weather.enabled ? "on" : "off");
                else
                    printf("weather control effect %d\n", ev.weather.effect);
                break;
            case OPENMMO_EV_JOINED:
                if (openmmo_client_self_tile(c, &px, &pz) == 0) {
                    joined = 1;
                    since_step = 0;
                    printf("in the map at (%d, %d); probing the %s edge\n",
                           px, pz, DIR_NAME[di]);
                }
                break;
            case OPENMMO_EV_SELF_CORRECT:
                px = ev.entity.x; pz = ev.entity.z;
                if (++walls >= WALL_LIMIT) {
                    printf("%s edge is walled at (%d, %d)\n", DIR_NAME[di], px, pz);
                    walls = 0; steps = 0;
                    if (++di >= 4) { printf("all four edges probed\n"); rc = 0; goto done; }
                    printf("probing the %s edge\n", DIR_NAME[di]);
                }
                break;
            case OPENMMO_EV_WARP:
                printf("%s edge crossed -> region %d bank %d map %d at (%d, %d) "
                       "dir %d (%s)\n",
                       DIR_NAME[di], ev.warp.region, ev.warp.bank, ev.warp.map,
                       ev.warp.x, ev.warp.z, ev.warp.dir,
                       ev.warp.seamless ? "seamless" : "warp");
                rc = 0; goto done;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }

        if (joined && ++since_step >= STEP_INTERVAL) {
            since_step = 0;
            int dir = DIR_ORDER[di];
            openmmo_client_send_move(c, px, pz, dir, 0);
            switch (dir) {
            case WDIR_EAST:  px++; break;
            case WDIR_WEST:  px--; break;
            case WDIR_SOUTH: pz++; break;
            case WDIR_NORTH: pz--; break;
            }
            if (++steps >= MAX_STEPS) {
                printf("%s edge not reached in %d steps\n", DIR_NAME[di], MAX_STEPS);
                walls = 0; steps = 0;
                if (++di >= 4) { printf("all four edges probed\n"); rc = 0; goto done; }
                printf("probing the %s edge\n", DIR_NAME[di]);
            }
        }

        usleep(2000);
    }
    fprintf(stderr, "error: gave up before probing every edge\n");

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

static int cmd_map(int argc, char **argv)
{
    openmmo_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    parse_opts(argc, argv, &cfg);
    return drive_map(&cfg);
}

/* --- encounter: a server-driven wild encounter, the client no longer rolls ----- */
static int send_encounter_intent(openmmo_client *c, int action, s16 move_or_item,
                                 s64 target, u8 extra)
{
    mmo_battle_select sel;
    s16 id = move_or_item;

    memset(&sel, 0, sizeof sel);
    sel.action = action;
    sel.target = target;
    sel.extra = extra;
    if (action == MMO_BATTLE_ACTION_MOVE && id == 0) {
        const openmmo_party *p = openmmo_client_party(c);
        if (p && p->valid && p->count > 0) {
            for (int i = 0; i < 4; i++) {
                if (p->mon[0].move_id[i]) {
                    id = (s16)p->mon[0].move_id[i];
                    break;
                }
            }
        }
        if (id == 0) {
            fprintf(stderr, "error: the lead has no move to send\n");
            return -1;
        }
        printf("sending move %d\n", (int)id);
    } else if (action == MMO_BATTLE_ACTION_MOVE) {
        printf("sending move %d\n", (int)id);
    } else if (action == MMO_BATTLE_ACTION_ITEM) {
        printf("sending item %d target %lld extra %u\n",
               (int)id, (long long)target, (unsigned)extra);
    } else if (action == MMO_BATTLE_ACTION_SWITCH) {
        printf("sending switch to party index %d\n", (int)id);
    } else {
        printf("asking to run away\n");
    }
    sel.move_or_item = id;
    if (openmmo_client_battle_select(c, &sel) != 0) {
        fprintf(stderr, "error: could not send the battle intent\n");
        return -1;
    }
    return 0;
}

static void print_battle_anims(const openmmo_client *c)
{
    const openmmo_battle_anims *an = openmmo_client_battle_anims(c);
    int i;

    for (i = 0; an && i < an->n; i++) {
        const mmo_battle_anim *a = &an->cmd[i];
        printf("  anim %s", mmo_battle_anim_name(a->command));
        if (a->kind == MMO_ANIM_PRINT) {
            const char *why = NULL;
            const char *name = mmo_display_move_name((int)a->id, &why);

            if (name)
                printf(" %s", name);
            else
                printf(" id %u", a->id);
        } else if (a->kind == MMO_ANIM_MOVE
            || a->kind == MMO_ANIM_STATUS
            || a->kind == MMO_ANIM_SHOW
            || a->kind == MMO_ANIM_CATCH)
            printf(" id %u", a->id);
        if (a->kind == MMO_ANIM_HP || a->kind == MMO_ANIM_EXP)
            printf(" hp %d", a->hp);
        if (a->fallback && a->why)
            printf(" (fallback: %s)", a->why);
        printf("\n");
    }
}

static void print_battle_field(const openmmo_client *c)
{
    const openmmo_battle_field *f = openmmo_client_battle_field(c);
    int i;

    if (!f || !f->valid)
        return;
    if (f->caught)
        printf("  caught species %d item %d entity %u\n",
               f->caught_species, f->caught_item, f->caught_id);
    if (f->switch_owed)
        printf("  switch owed\n");
    if (f->xp_gained)
        printf("  xp +%d%s\n", f->xp_gained, f->leveled ? " (level up)" : "");
    if (f->prize)
        printf("  prize %d\n", f->prize);
    for (i = 0; i < f->n_mons; i++) {
        if (f->mon[i].faint == 1)
            printf("  faint entity %u species %d\n",
                   f->mon[i].entity_id, f->mon[i].species);
    }
}

static int drive_encounter(const openmmo_config *cfg, int action, s16 move_or_item,
                           s64 target, u8 extra, int have_slot, s16 slot,
                           int wild_dex, int wild_level)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1, asked = 0, since_join = 0, acted = 0, replaced = 0, round = 1;
    const int ROUNDS = 2;
    const int ASK_DELAY = 60;   /* let the arrival burst settle before asking */

    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED: {
                const openmmo_world_state *ws = openmmo_client_world_state(c);
                const openmmo_party *p = openmmo_client_party(c);
                int empty = (p && p->valid) ? p->count == 0
                                            : (ws && ws->party_count == 0);
                printf("in the world; will ask the server for a wild encounter\n");
                if (empty) {
                    fprintf(stderr,
                            "encounter: party is empty; a new character starts "
                            "with none and grass will not start a battle\n");
                    rc = 1;
                    goto done;
                }
                since_join = 1;
                break;
            }
            case OPENMMO_EV_ENCOUNTER:
                printf("encounter %d: the server started a %s battle (background %d)\n",
                       round, ev.encounter.wild ? "wild" : "trainer",
                       ev.encounter.background);
                /* A run / item goes up as soon as the field opens, that is how
                 * the leave arm was measured. A move waits for the prompt; a
                 * switch waits for the prompt or the forced-replacement flag. */
                if (action == MMO_BATTLE_ACTION_RUN
                    || action == MMO_BATTLE_ACTION_ITEM) {
                    if (send_encounter_intent(c, action, move_or_item, target,
                                              extra) != 0)
                        goto done;
                    acted = 1;
                }
                break;
            case OPENMMO_EV_BATTLE_EVENT:
                printf("battle event 0x%02x", ev.battle.opcode);
                if (ev.battle.entity_id)
                    printf(" entity %u", ev.battle.entity_id);
                if (ev.battle.move)
                    printf(" move %d", ev.battle.move);
                if (ev.battle.n_targets)
                    printf(" targets %d", ev.battle.n_targets);
                if (ev.battle.hp)
                    printf(" hp %d", ev.battle.hp);
                printf(" kind %d\n", ev.battle.kind);
                if (ev.battle.opcode == MMO_GAME_OP_BATTLE_MOVE_EVENT
                    || ev.battle.opcode == MMO_GAME_OP_BATTLE_SWITCH_IN
                    || ev.battle.opcode == MMO_GAME_OP_BATTLE_SLOT_FLAG
                    || ev.battle.opcode == MMO_GAME_OP_BATTLE_LIST_EVENT
                    || ev.battle.opcode == MMO_GAME_OP_BATTLE_STAT_COUNTERS)
                    print_battle_anims(c);
                if (action == MMO_BATTLE_ACTION_MOVE &&
                    ev.battle.opcode == MMO_GAME_OP_BATTLE_QUEUED_EVENT) {
                    if (replaced) {
                        if (send_encounter_intent(c, MMO_BATTLE_ACTION_RUN,
                                                  0, 0, 0) != 0)
                            goto done;
                    } else if (send_encounter_intent(c, action, move_or_item,
                                                     target, extra) != 0) {
                        goto done;
                    }
                    acted = 1;
                }
                if (!acted && action == MMO_BATTLE_ACTION_SWITCH &&
                    (ev.battle.opcode == MMO_GAME_OP_BATTLE_QUEUED_EVENT
                     || ev.battle.opcode == MMO_GAME_OP_BATTLE_SLOT_FLAG)) {
                    if (send_encounter_intent(c, action, move_or_item, target,
                                              extra) != 0)
                        goto done;
                    acted = 1;
                } else if (acted && action == MMO_BATTLE_ACTION_SWITCH &&
                           ev.battle.opcode == MMO_GAME_OP_BATTLE_QUEUED_EVENT) {
                    /* The switch spent the turn. Leave so the two-round
                     * loop can settle the fight. */
                    if (send_encounter_intent(c, MMO_BATTLE_ACTION_RUN,
                                              0, 0, 0) != 0)
                        goto done;
                }
                /* A move that faints the lead owes a replacement. --slot names it. */
                if (acted && have_slot && !replaced &&
                    action == MMO_BATTLE_ACTION_MOVE &&
                    ev.battle.opcode == MMO_GAME_OP_BATTLE_SLOT_FLAG &&
                    ev.battle.kind == 0) {
                    if (send_encounter_intent(c, MMO_BATTLE_ACTION_SWITCH,
                                              slot, 0, 0) != 0)
                        goto done;
                    replaced = 1;
                }
                break;
            case OPENMMO_EV_PARTY:
                printf("party now %d (of %d)\n", ev.party.count, ev.party.total);
                break;
            case OPENMMO_EV_BATTLE_END:
                print_battle_field(c);
                printf("battle %d over: back in the overworld, acknowledged\n", round);
                if (++round > ROUNDS) { rc = 0; goto done; }
                /* Ask again on the same session: the server only answers if it
                 * settled the last battle on the acknowledgement above. */
                acted = 0;
                replaced = 0;
                asked = 0;
                since_join = 1;
                break;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }

        if (since_join && !asked && ++since_join >= ASK_DELAY) {
            asked = 1;
            if (wild_dex > 0) {
                char cmd[64];
                snprintf(cmd, sizeof cmd, "/testbattle %d %d", wild_dex,
                         wild_level > 0 ? wild_level : 3);
                printf("sending %s\n", cmd);
                openmmo_client_send_chat(c, cmd);
            } else {
                printf("sending /testbattle\n");
                openmmo_client_send_chat(c, "/testbattle");
            }
        }

        usleep(2000);
    }
    fprintf(stderr, "error: the %s did not arrive before the frame budget ran out\n",
            acted ? "end of the battle" : "encounter");

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

static int cmd_encounter(int argc, char **argv)
{
    openmmo_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    parse_opts(argc, argv, &cfg);

    int action = MMO_BATTLE_ACTION_RUN;
    s16 move_or_item = 0;
    s16 slot = 0;
    s64 target = 0;
    u8 extra = 0;
    int have_item = 0, have_slot = 0;
    int wild_dex = 0, wild_level = 0;
    for (int i = 0; i < argc; i++) {
        int has_next = (i + 1 < argc);
        if (strcmp(argv[i], "--action") == 0 && has_next) {
            const char *a = argv[++i];
            if (strcmp(a, "run") == 0)
                action = MMO_BATTLE_ACTION_RUN;
            else if (strcmp(a, "move") == 0)
                action = MMO_BATTLE_ACTION_MOVE;
            else if (strcmp(a, "item") == 0)
                action = MMO_BATTLE_ACTION_ITEM;
            else if (strcmp(a, "switch") == 0)
                action = MMO_BATTLE_ACTION_SWITCH;
            else {
                fprintf(stderr, "error: --action is run, move, item or switch\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--move") == 0 && has_next) {
            move_or_item = (s16)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--item") == 0 && has_next) {
            move_or_item = (s16)atoi(argv[++i]);
            have_item = 1;
        } else if (strcmp(argv[i], "--slot") == 0 && has_next) {
            slot = (s16)atoi(argv[++i]);
            if (action == MMO_BATTLE_ACTION_SWITCH)
                move_or_item = slot;
            have_slot = 1;
        } else if (strcmp(argv[i], "--target") == 0 && has_next) {
            target = (s64)strtoll(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--wild") == 0 && has_next) {
            wild_dex = atoi(argv[++i]);
            if (i + 1 < argc && argv[i + 1][0] != '-')
                wild_level = atoi(argv[++i]);
        }
    }
    if (action == MMO_BATTLE_ACTION_ITEM) {
        if (!have_item) {
            fprintf(stderr, "error: --action item needs --item ID\n");
            return 1;
        }
        extra = 0xFF; /* every captured item throw and potion */
    } else if (action == MMO_BATTLE_ACTION_SWITCH && !have_slot) {
        fprintf(stderr, "error: --action switch needs --slot N\n");
        return 1;
    }
    if (action == MMO_BATTLE_ACTION_SWITCH)
        move_or_item = slot;
    return drive_encounter(&cfg, action, move_or_item, target, extra,
                           have_slot, slot, wild_dex, wild_level);
}

/*
 * Join and report the server-owned progression store the world-state block seats, the world-
 * flag table, the set story flags and the story variables.
 */
static int drive_flags(const openmmo_config *cfg)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1;
    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED: {
                const openmmo_story_store *s = openmmo_client_story_store(c);
                printf("in the world; progression store (region %d):\n", s->region);
                printf("  world-flag groups: %d\n", s->flag_group_count);
                for (int g = 0; g < s->flag_group_count; g++)
                    printf("    group %d: %d-byte block\n", g, s->flag_block_len[g]);
                printf("  story flags set: %d%s\n", s->set_flag_count,
                       s->flag_overflow ? " (+overflow)" : "");
                for (int i = 0; i < s->set_flag_count && i < 12; i++)
                    printf("    flag 0x%04x set\n", s->set_flag[i]);
                printf("  story vars present: %d\n", s->var_count);
                for (int k = 0, shown = 0; k < OPENMMO_STORY_VARS && shown < 12; k++)
                    if (s->var_present[k]) {
                        printf("    var 0x%04x = %d\n", 0x4000 + k, s->var[k]);
                        shown++;
                    }
                rc = 0; goto done;
            }
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }
        usleep(2000);
    }
    fprintf(stderr, "error: never reached the world before the frame budget ran out\n");

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

/* --- storage: the party and the PC, and a monster moved between them --------- */

/* Who the server says is boarding at the day care. Printed with the other two
 * because a boarder is neither in the party nor in the PC and the whole point of
 * the container is that it can be seen to be somewhere. */
static void print_daycare(const openmmo_client *c)
{
    const openmmo_storage *d = openmmo_client_daycare(c);

    if (!d->valid) {
        printf("  daycare: no container received\n");
        return;
    }
    printf("  daycare: %d boarding\n", d->count);
    for (int i = 0; i < d->count; i++)
        printf("    slot %d: #%d %s lv%d (id %lld)\n", d->mon[i].slot,
               d->mon[i].dex_id, d->mon[i].nickname, d->mon[i].level,
               (long long)d->mon[i].id);
}

static void print_containers(const openmmo_client *c, const char *when)
{
    const openmmo_party *p = openmmo_client_party(c);
    const openmmo_storage *s = openmmo_client_storage(c);

    printf("%s:\n", when);
    printf("  party: %d of %d\n", p->count, p->total);
    for (int i = 0; i < p->count; i++)
        printf("    slot %d: #%d %s lv%d hp%d (id %lld)\n", p->mon[i].slot,
               p->mon[i].dex_id, p->mon[i].nickname, p->mon[i].level,
               p->mon[i].hp, (long long)p->mon[i].id);
    if (!s->valid) {
        printf("  pc: no container received\n");
        return;
    }
    printf("  pc: %d held%s%s\n", s->count,
           s->dropped ? " (+dropped)" : "",
           s->malformed ? " (malformed container seen)" : "");
    for (int i = 0; i < s->count; i++)
        printf("    slot %d: #%d %s lv%d (id %lld)\n", s->mon[i].slot,
               s->mon[i].dex_id, s->mon[i].nickname, s->mon[i].level,
               (long long)s->mon[i].id);
    print_daycare(c);
}

/* The lowest slot in a container nothing sits on. -1 if it is full. */
static int first_free_slot(const openmmo_client *c, int container)
{
    if (container == OPENMMO_CONTAINER_PARTY) {
        const openmmo_party *p = openmmo_client_party(c);
        return p->count < OPENMMO_PARTY_MAX ? p->count : -1;
    }
    const openmmo_storage *s = container == OPENMMO_CONTAINER_DAYCARE
                                   ? openmmo_client_daycare(c)
                                   : openmmo_client_storage(c);
    int slots = container == OPENMMO_CONTAINER_DAYCARE ? OPENMMO_DAYCARE_MAX
                                                       : OPENMMO_STORAGE_MAX;
    for (int slot = 0; slot < slots; slot++) {
        int taken = 0;
        for (int i = 0; i < s->count; i++)
            if (s->mon[i].slot == slot) { taken = 1; break; }
        if (!taken)
            return slot;
    }
    return -1;
}

/* The monster sitting on `slot` of a container, or NULL. The PC is sparse, so
 * a slot is looked up rather than indexed. */
static const openmmo_party_mon *mon_at(const openmmo_client *c, int container,
                                       int slot)
{
    if (container == OPENMMO_CONTAINER_PARTY) {
        const openmmo_party *p = openmmo_client_party(c);
        for (int i = 0; i < p->count; i++)
            if (p->mon[i].slot == slot)
                return &p->mon[i];
        return NULL;
    }
    {
        const openmmo_storage *s = openmmo_client_storage(c);
        for (int i = 0; i < s->count; i++)
            if (s->mon[i].slot == slot)
                return &s->mon[i];
    }
    return NULL;
}

/* The first empty slot of box `box` (counted from one, the way the box screen
 * numbers them). -1 when the box is full. */
static int first_free_in_box(const openmmo_client *c, int box)
{
    int base = (box - 1) * OPENMMO_BOX_SIZE;

    for (int slot = base; slot < base + OPENMMO_BOX_SIZE; slot++)
        if (mon_at(c, OPENMMO_CONTAINER_PC, slot) == NULL)
            return slot;
    return -1;
}

/* One storage gesture, as the command line named it. Slots are the wire's
 * (0-based, the PC one flat list) unless `box` is given, in which case
 * `to_slot`, `withdraw` and `release_pc` count from one inside that box. */
typedef struct {
    int deposit, withdraw, board, collect, to_slot;
    int box;            /* 1-based, or -1 for "the flat PC" */
    const char *add;    /* a species for /party, or NULL */
    int add_level;      /* -1 = the server's default */
    int release;        /* party slot to let go, or -1 */
    int release_pc;     /* PC slot to let go, or -1 */
} storage_move;

static int drive_storage(const openmmo_config *cfg, const storage_move *m)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1, sent = 0, joined = 0;
    int deposit = m->deposit, withdraw = m->withdraw, board = m->board,
        collect = m->collect, to_slot = m->to_slot;
    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED:
                joined = 1;
                print_containers(c, "on join");
                rc = 0;
                break;
            case OPENMMO_EV_PARTY:
            case OPENMMO_EV_STORAGE:
                /* Both containers come back together, so report once the second
                 * of the pair has landed. */
                if (sent && ev.kind == OPENMMO_EV_STORAGE) {
                    print_containers(c, "after the move");
                    rc = 0;
                    goto done;
                }
                break;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }

        if (joined && !sent) {
            if (m->add != NULL) {
                /* The developer's /party puts a rolled monster in the party
                 * and resends every container, so the round trip closes the
                 * same way a move does. */
                char line[96];

                if (m->add_level > 0)
                    snprintf(line, sizeof line, "/party %s %d", m->add,
                             m->add_level);
                else
                    snprintf(line, sizeof line, "/party %s", m->add);
                printf("saying: %s\n", line);
                if (openmmo_client_send_chat(c, line) != 0) {
                    fprintf(stderr, "error: could not send the line\n");
                    goto done;
                }
                sent = 1;
                rc = 1;
                continue;
            }
            if (m->release >= 0 || m->release_pc >= 0) {
                int container = m->release >= 0 ? OPENMMO_CONTAINER_PARTY
                                                : OPENMMO_CONTAINER_PC;
                int slot = m->release >= 0 ? m->release : m->release_pc;
                const openmmo_party_mon *mon;

                if (container == OPENMMO_CONTAINER_PC && m->box > 0)
                    slot = (m->box - 1) * OPENMMO_BOX_SIZE + slot - 1;
                mon = mon_at(c, container, slot);
                if (mon == NULL) {
                    fprintf(stderr, "error: nothing sits on %s slot %d\n",
                            container == OPENMMO_CONTAINER_PARTY ? "party"
                                                                 : "pc",
                            slot);
                    goto done;
                }
                printf("releasing %s slot %d: #%d %s lv%d (id %lld)\n",
                       container == OPENMMO_CONTAINER_PARTY ? "party" : "pc",
                       slot, mon->dex_id, mon->nickname, mon->level,
                       (long long)mon->id);
                if (openmmo_client_send_pokemon_release(c, mon->id) != 0) {
                    fprintf(stderr, "error: the client refused that release\n");
                    goto done;
                }
                sent = 1;
                rc = 1;
                continue;
            }
            if (deposit < 0 && withdraw < 0 && board < 0 && collect < 0)
                goto done;                /* report only */
            int from_c = OPENMMO_CONTAINER_PARTY, to_c = OPENMMO_CONTAINER_PC;
            int from_s = deposit;

            if (withdraw >= 0) {
                from_c = OPENMMO_CONTAINER_PC;
                to_c = OPENMMO_CONTAINER_PARTY;
                from_s = withdraw;
                if (m->box > 0)
                    from_s = (m->box - 1) * OPENMMO_BOX_SIZE + withdraw - 1;
            } else if (board >= 0) {
                to_c = OPENMMO_CONTAINER_DAYCARE;
                from_s = board;
            } else if (collect >= 0) {
                from_c = OPENMMO_CONTAINER_DAYCARE;
                to_c = OPENMMO_CONTAINER_PARTY;
                from_s = collect;
            }
            /* Named or the first free one. Naming a taken slot is the swap the
             * box screen draws, and it is the gesture that once wedged the
             * server's flusher, so it has to be drivable from here. A box
             * named on a deposit narrows "first free" to that box. */
            int to_s;

            if (to_c == OPENMMO_CONTAINER_PC && m->box > 0)
                to_s = to_slot >= 0
                           ? (m->box - 1) * OPENMMO_BOX_SIZE + to_slot - 1
                           : first_free_in_box(c, m->box);
            else
                to_s = to_slot >= 0 ? to_slot : first_free_slot(c, to_c);
            if (to_s < 0) {
                fprintf(stderr, "error: the destination container is full\n");
                goto done;
            }
            printf("moving container %d slot %d -> container %d slot %d\n",
                   from_c, from_s, to_c, to_s);
            if (openmmo_client_move_pokemon(c, from_c, from_s, to_c, to_s) != 0) {
                fprintf(stderr, "error: the client refused that move\n");
                rc = 1;
                goto done;
            }
            sent = 1;
            rc = 1;                       /* the round trip has not closed yet */
        }
        usleep(2000);
    }
    if (sent)
        fprintf(stderr, "error: the server never sent the containers back\n");
    else
        fprintf(stderr, "error: never reached the world before the frame budget "
                        "ran out\n");

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

static int cmd_storage(int argc, char **argv)
{
    openmmo_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = "test";
    cfg.pass = "test";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    parse_opts(argc, argv, &cfg);

    storage_move m = { -1, -1, -1, -1, -1, -1, NULL, -1, -1, -1 };
    int named = 0;
    for (int i = 0; i < argc; i++) {
        int has_next = (i + 1 < argc);
        if (strcmp(argv[i], "--deposit") == 0 && has_next) {
            m.deposit = atoi(argv[++i]);
            named++;
        } else if (strcmp(argv[i], "--withdraw") == 0 && has_next) {
            m.withdraw = atoi(argv[++i]);
            named++;
        } else if (strcmp(argv[i], "--board") == 0 && has_next) {
            m.board = atoi(argv[++i]);
            named++;
        } else if (strcmp(argv[i], "--collect") == 0 && has_next) {
            m.collect = atoi(argv[++i]);
            named++;
        } else if (strcmp(argv[i], "--to") == 0 && has_next) {
            m.to_slot = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--box") == 0 && has_next) {
            m.box = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--add") == 0 && has_next) {
            m.add = argv[++i];
            named++;
        } else if (strcmp(argv[i], "--level") == 0 && has_next) {
            m.add_level = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--release") == 0 && has_next) {
            m.release = atoi(argv[++i]);
            named++;
        } else if (strcmp(argv[i], "--release-pc") == 0 && has_next) {
            m.release_pc = atoi(argv[++i]);
            named++;
        }
    }
    if (named > 1) {
        fprintf(stderr, "error: --deposit, --withdraw, --board, --collect,"
                        " --add, --release and --release-pc are one move"
                        " each\n");
        return 1;
    }
    if (m.box == 0 || m.box > OPENMMO_STORAGE_MAX / OPENMMO_BOX_SIZE) {
        fprintf(stderr, "error: --box counts boxes from 1 to %d\n",
                OPENMMO_STORAGE_MAX / OPENMMO_BOX_SIZE);
        return 1;
    }
    /* A release is a statement the game client makes about a box screen it
     * ran itself, and the server only hears it from a client that says it
     * runs the field scripts; this one never walks, so saying so is safe. */
    if (m.release >= 0 || m.release_pc >= 0)
        cfg.local_scripts = 1;
    return drive_storage(&cfg, &m);
}

/* Join as a developer, open a mart via /shop, and optionally buy or sell. */
static int drive_shop(const openmmo_config *cfg, int buy_id, int buy_qty,
                      int sell_id, int sell_qty)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1;
    int asked = 0;
    int sent = 0;
    int saw_shop = 0;
    int saw_bag = 0;
    int saw_money = 0;
    int want_trade = (buy_id >= 0) || (sell_id >= 0);
    const openmmo_world_state *ws;
    const openmmo_bag *bag;
    int i;

    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED:
                ws = openmmo_client_world_state(c);
                if (ws)
                    printf("money %d\n", ws->money);
                print_bag(openmmo_client_bag(c));
                printf("opening shop\n");
                if (openmmo_client_send_chat(c, "/shop") != 0) {
                    fprintf(stderr, "error: could not send /shop\n");
                    goto done;
                }
                asked = 1;
                break;
            case OPENMMO_EV_SHOP:
                saw_shop = 1;
                print_shop(openmmo_client_shop(c));
                if (!want_trade) {
                    rc = ev.shop.open ? 0 : 1;
                    goto done;
                }
                break;
            case OPENMMO_EV_BAG:
                if (sent) {
                    saw_bag = 1;
                    print_bag(openmmo_client_bag(c));
                }
                break;
            case OPENMMO_EV_MONEY:
                if (sent) {
                    saw_money = 1;
                    printf("money now %d\n", ev.money.money);
                }
                break;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }

        if (asked && saw_shop && want_trade && !sent) {
            if (buy_id >= 0) {
                printf("buying wire %d x%d\n", buy_id, buy_qty);
                if (openmmo_client_shop_buy(c, (s16)buy_id, (s16)buy_qty) != 0) {
                    fprintf(stderr, "error: the client refused that buy\n");
                    goto done;
                }
            } else {
                s64 entity = 0;
                bag = openmmo_client_bag(c);
                for (i = 0; bag && i < bag->count; i++) {
                    if (bag->stack[i].item_id == (u16)sell_id) {
                        entity = bag->stack[i].object_id;
                        break;
                    }
                }
                if (!entity) {
                    fprintf(stderr, "error: bag has no stack of wire %d\n",
                            sell_id);
                    goto done;
                }
                printf("selling wire %d x%d (stack %lld)\n", sell_id, sell_qty,
                       (long long)entity);
                if (openmmo_client_shop_sell(c, entity, (s16)sell_qty) != 0) {
                    fprintf(stderr, "error: the client refused that sell\n");
                    goto done;
                }
            }
            sent = 1;
        }

        if (sent && saw_bag && saw_money) {
            rc = 0;
            goto done;
        }
        usleep(2000);
    }
    if (!asked)
        fprintf(stderr, "error: never reached the world before the frame budget "
                        "ran out\n");
    else if (!saw_shop)
        fprintf(stderr, "error: the server never opened a shop\n");
    else
        fprintf(stderr, "error: the server never settled the trade\n");

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

static int cmd_shop(int argc, char **argv)
{
    openmmo_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    parse_opts(argc, argv, &cfg);

    int buy_id = -1, buy_qty = 0;
    int sell_id = -1, sell_qty = 0;
    for (int i = 0; i < argc; i++) {
        int has_next = (i + 1 < argc);
        if (strcmp(argv[i], "--buy") == 0 && has_next) {
            buy_id = atoi(argv[++i]);
            if (i + 1 < argc && argv[i + 1][0] != '-')
                buy_qty = atoi(argv[++i]);
            else
                buy_qty = 1;
        } else if (strcmp(argv[i], "--sell") == 0 && has_next) {
            sell_id = atoi(argv[++i]);
            if (i + 1 < argc && argv[i + 1][0] != '-')
                sell_qty = atoi(argv[++i]);
            else
                sell_qty = 1;
        }
    }
    if (buy_id >= 0 && sell_id >= 0) {
        fprintf(stderr, "error: --buy and --sell are one trade each\n");
        return 1;
    }
    if (buy_id >= 0 && buy_qty <= 0) {
        fprintf(stderr, "error: --buy needs a positive quantity\n");
        return 1;
    }
    if (sell_id >= 0 && sell_qty <= 0) {
        fprintf(stderr, "error: --sell needs a positive quantity\n");
        return 1;
    }
    return drive_shop(&cfg, buy_id, buy_qty, sell_id, sell_qty);
}

/* Join as a developer and ask the server for a box or prompt. */
static int drive_prompt(const openmmo_config *cfg, const char *cmd,
                        int want_type, u8 response)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1;
    int asked = 0;
    int replied = 0;
    int saw_box = 0;
    const openmmo_dialog *box;
    char line[32];

    snprintf(line, sizeof line, "/%s", cmd);
    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED:
                printf("asking for a %s\n", cmd);
                if (openmmo_client_send_chat(c, line) != 0) {
                    fprintf(stderr, "error: could not send %s\n", line);
                    goto done;
                }
                asked = 1;
                break;
            case OPENMMO_EV_DIALOG:
                box = openmmo_client_dialog(c);
                if (box && box->awaiting && !replied) {
                    print_dialog(box);
                    if (box->action_type == want_type)
                        saw_box = 1;
                    if (openmmo_client_reply_dialog(c, response) != 0) {
                        fprintf(stderr, "error: could not reply to the box\n");
                        goto done;
                    }
                    replied = 1;
                    printf("dialog: replied flags %d response %u\n",
                           (int)box->flags, (unsigned)response);
                }
                if (replied && !openmmo_client_in_dialog(c)) {
                    printf("dialog: unlocked\n");
                    rc = saw_box ? 0 : 1;
                    goto done;
                }
                break;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }
        usleep(2000);
    }
    if (!asked)
        fprintf(stderr, "error: never reached the world before the frame budget "
                        "ran out\n");
    else if (!saw_box)
        fprintf(stderr, "error: the server never sent a type %d prompt\n",
                want_type);
    else
        fprintf(stderr, "error: the lock was never released\n");

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

static int cmd_prompt(int argc, char **argv, const char *cmd,
                      int want_type, u8 response)
{
    openmmo_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    parse_opts(argc, argv, &cfg);
    return drive_prompt(&cfg, cmd, want_type, response);
}

static int cmd_dialog(int argc, char **argv)
{
    return cmd_prompt(argc, argv, "dialog", MMO_DIALOG_ACTION_SIGN, 0);
}

/* Join, walk to the bedroom Wii at (5,4), and send TileInteract. The
 * spawn is (4,6); east then north lands on (5,5) facing it. */
/* The A-press, driven two ways. */
static int drive_talk(const openmmo_config *cfg, int here, int face)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    const int want = here ? MMO_DIALOG_ACTION_NPC : MMO_DIALOG_ACTION_SIGN;
    int rc = 1;
    int walked = 0;
    int asked = 0;
    int replied = 0;
    int saw_box = 0;
    int x = 0, z = 0;
    const openmmo_dialog *box;

    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED:
                if (openmmo_client_self_tile(c, &x, &z) != 0) {
                    fprintf(stderr, "error: join did not place the player\n");
                    goto done;
                }
                printf("talk: standing at (%d, %d)\n", x, z);
                break;
            case OPENMMO_EV_DIALOG:
                box = openmmo_client_dialog(c);
                if (box && box->awaiting && !replied) {
                    print_dialog(box);
                    if (box->action_type == want)
                        saw_box = 1;
                    if (openmmo_client_reply_dialog(c, 0) != 0) {
                        fprintf(stderr, "error: could not reply to the box\n");
                        goto done;
                    }
                    replied = 1;
                    printf("dialog: replied flags %d response 0\n",
                           (int)box->flags);
                }
                if (replied && !openmmo_client_in_dialog(c)) {
                    printf("dialog: unlocked\n");
                    rc = saw_box ? 0 : 1;
                    goto done;
                }
                break;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }

        if (!asked && openmmo_client_status(c) == OPENMMO_IN_GAME
            && openmmo_client_self_tile(c, &x, &z) == 0) {
            if (!walked && here) {
                printf("talk: standing where the join put us, facing %d\n", face);
                if (openmmo_client_send_face(c, face) != 0) {
                    fprintf(stderr, "error: could not turn to face\n");
                    goto done;
                }
                walked = 1;
            } else if (!walked) {
                if (x == 5 && z == 5) {
                    walked = 1;
                } else if (x == 4 && z == 6) {
                    printf("talk: walking to the Wii\n");
                    if (openmmo_client_send_move(c, 4, 6, 3, 0) != 0
                        || openmmo_client_send_move(c, 5, 6, 0, 0) != 0) {
                        fprintf(stderr, "error: could not walk to the Wii\n");
                        goto done;
                    }
                    walked = 1;
                } else {
                    fprintf(stderr,
                            "error: talk expects the bedroom spawn (4, 6), "
                            "not (%d, %d)\n", x, z);
                    goto done;
                }
            }
            if (walked && !asked && (here || (x == 5 && z == 5))) {
                printf("talk: pressing A\n");
                if (openmmo_client_interact_tile(c) != 0) {
                    fprintf(stderr, "error: could not send TileInteract\n");
                    goto done;
                }
                asked = 1;
            }
        }
        usleep(2000);
    }
    if (!asked)
        fprintf(stderr, "error: never reached the tile to press A on before the "
                        "frame budget ran out\n");
    else if (!saw_box)
        fprintf(stderr, "error: the server never sent %s box\n",
                here ? "a person's" : "the Wii");
    else
        fprintf(stderr, "error: the lock was never released\n");

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

static int cmd_talk(int argc, char **argv)
{
    openmmo_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    int here = 0;
    int face = 0; /* DIR_NORTH */
    for (int i = 0; i < argc; i++) {
        int has_next = (i + 1 < argc);
        if (strcmp(argv[i], "--here") == 0) here = 1;
        else if (strcmp(argv[i], "--face") == 0 && has_next) {
            const char *d = argv[++i];
            if (strcmp(d, "north") == 0) face = 0;
            else if (strcmp(d, "south") == 0) face = 1;
            else if (strcmp(d, "west") == 0) face = 2;
            else if (strcmp(d, "east") == 0) face = 3;
            else {
                fprintf(stderr, "error: --face takes north, south, west or "
                                "east, not '%s'\n", d);
                return 2;
            }
        }
    }
    parse_opts(argc, argv, &cfg);
    return drive_talk(&cfg, here, face);
}

static int cmd_yesno(int argc, char **argv)
{
    return cmd_prompt(argc, argv, "yesno", MMO_DIALOG_ACTION_YESNO, 1);
}

static int cmd_menu(int argc, char **argv)
{
    return cmd_prompt(argc, argv, "menu", MMO_DIALOG_ACTION_MENU, 1);
}

static int cmd_list(int argc, char **argv)
{
    return cmd_prompt(argc, argv, "list", MMO_DIALOG_ACTION_LIST, 0);
}

/* Join as a developer and ask the server for one objective. */
static int drive_quest(const openmmo_config *cfg)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1;
    int asked = 0;
    int saw = 0;

    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED:
                print_objectives(openmmo_client_objectives(c));
                printf("asking for an objective\n");
                if (openmmo_client_send_chat(c, "/quest") != 0) {
                    fprintf(stderr, "error: could not send /quest\n");
                    goto done;
                }
                asked = 1;
                break;
            case OPENMMO_EV_OBJECTIVE:
                print_objectives(openmmo_client_objectives(c));
                printf("quest: id %d value %d count %d%s\n",
                       (int)ev.objective.id, ev.objective.value,
                       (int)ev.objective.tally,
                       ev.objective.replace ? " (replace)" : "");
                if (ev.objective.id == 1 && ev.objective.value == 3
                    && ev.objective.tally == 5)
                    saw = 1;
                rc = saw ? 0 : 1;
                goto done;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }
        usleep(2000);
    }
    if (!asked)
        fprintf(stderr, "error: never reached the world before the frame budget "
                        "ran out\n");
    else
        fprintf(stderr, "error: the server never sent an objective\n");

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

static int cmd_quest(int argc, char **argv)
{
    openmmo_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    parse_opts(argc, argv, &cfg);
    return drive_quest(&cfg);
}

/* Join as a developer and ask the server for a confirm + list. */
static int drive_ui(const openmmo_config *cfg)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1;
    int asked = 0;
    int saw = 0;

    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED:
                print_ui(openmmo_client_ui(c));
                printf("asking for a prompt\n");
                if (openmmo_client_send_chat(c, "/ui") != 0) {
                    fprintf(stderr, "error: could not send /ui\n");
                    goto done;
                }
                asked = 1;
                break;
            case OPENMMO_EV_UI:
                print_ui(openmmo_client_ui(c));
                printf("ui: opcode 0x%02x pages %d options %d rows %d%s%s\n",
                       ev.ui.opcode, ev.ui.pages, ev.ui.options, ev.ui.rows,
                       ev.ui.confirm ? " confirm" : "",
                       ev.ui.prompt ? " prompt" : "");
                if (ev.ui.confirm && ev.ui.options > 0 && ev.ui.rows > 0)
                    saw = 1;
                if (saw) {
                    rc = 0;
                    goto done;
                }
                break;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }
        usleep(2000);
    }
    if (!asked)
        fprintf(stderr, "error: never reached the world before the frame budget "
                        "ran out\n");
    else
        fprintf(stderr, "error: the server never sent the prompt\n");

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

static int cmd_ui(int argc, char **argv)
{
    openmmo_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    parse_opts(argc, argv, &cfg);
    return drive_ui(&cfg);
}

/* Join as a developer and ask the server for a known chunked blob. */
static int drive_sync(const openmmo_config *cfg)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1;
    int asked = 0;
    int saw = 0;

    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED:
                print_sync(openmmo_client_sync(c));
                printf("asking for a transfer\n");
                if (openmmo_client_send_chat(c, "/sync") != 0) {
                    fprintf(stderr, "error: could not send /sync\n");
                    goto done;
                }
                asked = 1;
                break;
            case OPENMMO_EV_SYNC:
                /*
                 * The event carries the store as it stood when that packet was seated; the
                 * store itself is already several packets further on, because one pump drains
                 * the whole burst. Print the event here and the store once at the end, or the
                 * two read as a disagreement.
                 */
                printf("sync: opcode 0x%02x digest %d transfer %d stream %d image %d plain %d\n",
                       ev.sync.opcode, ev.sync.digest, ev.sync.transfer_done,
                       ev.sync.stream_done, ev.sync.image_done,
                       ev.sync.plain_len);
                if (ev.sync.digest && ev.sync.transfer_done
                    && ev.sync.stream_done && ev.sync.image_done)
                    saw = 1;
                if (saw) {
                    print_sync(openmmo_client_sync(c));
                    rc = 0;
                    goto done;
                }
                break;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }
        usleep(2000);
    }
    if (!asked)
        fprintf(stderr, "error: never reached the world before the frame budget "
                        "ran out\n");
    else
        fprintf(stderr, "error: the server never sent the transfer\n");

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

/* Join as a developer and ask the server for one of each competitive
 * packet. Join sends none of them, so the store is empty until /compete
 * lands; the print at JOINED is what says so. */
static int drive_compete(const openmmo_config *cfg, int send_all)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1;
    int asked = 0;
    int saw = 0;

    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED:
                print_compete(openmmo_client_compete(c));
                if (send_all) {
                    static const s8 langs[2] = { 0, 1 };
                    int bad = 0;

                    static const s8 queues[1] = { 0 };
                    static const s8 slots[1] = { 0 };

                    /* Every c2s of the group, in one burst. Three of them are
                     * answered now, the signup, the withdrawal it is
                     * followed by, and the language list, and the rest still
                     * measure only whether they are framed and accepted. */
                    bad |= openmmo_client_queue_langs(c, langs, 2) != 0;
                    bad |= openmmo_client_tier_select(c, 0, 1) != 0;
                    bad |= openmmo_client_queue_tier(c, 3) != 0;
                    bad |= openmmo_client_queue_target(c, 11, 2) != 0;
                    bad |= openmmo_client_queue_teleport(c) != 0;
                    bad |= openmmo_client_queue_signup(c, queues, slots, 1) != 0;
                    /* Refused rather than framed: the count byte is the
                     * discriminator, so an empty list is a tournament. */
                    bad |= openmmo_client_queue_signup(c, queues, slots, 0) != -1;
                    bad |= openmmo_client_tourney_signup(c, 7, 0) != 0;
                    bad |= openmmo_client_queue_join(c) != 0;
                    bad |= openmmo_client_queue_leave(c) != 0;
                    bad |= openmmo_client_queue_cancel(c) != 0;
                    bad |= openmmo_client_tourney_register(c, 1) != 0;
                    bad |= openmmo_client_tourney_view(c, 7, 1, 0) != 0;
                    bad |= openmmo_client_tourney_teleport(c) != 0;
                    bad |= openmmo_client_score_board(c, 0) != 0;
                    bad |= openmmo_client_coop_board(c) != 0;
                    if (bad) {
                        fprintf(stderr, "error: a request would not send\n");
                        goto done;
                    }
                    printf("sent fifteen requests\n");
                }
                printf("asking for a board\n");
                if (openmmo_client_send_chat(c, "/compete") != 0) {
                    fprintf(stderr, "error: could not send /compete\n");
                    goto done;
                }
                asked = 1;
                break;
            case OPENMMO_EV_COMPETE:
                /* One pump drains the whole burst, so the event carries the
                 * store as of its own packet and the store is already
                 * further on. Print the events in order and the store once
                 * at the end. */
                printf("compete: opcode 0x%02x rentals %d tourneys %d "
                       "matchups %d rows %d entered %d\n",
                       ev.compete.opcode, ev.compete.rentals,
                       ev.compete.tourneys, ev.compete.matchups,
                       ev.compete.board_rows, (int)ev.compete.entered);
                if (ev.compete.opcode == 0xA4)
                    saw = 1;
                if (saw) {
                    print_compete(openmmo_client_compete(c));
                    rc = 0;
                    goto done;
                }
                break;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }
        usleep(2000);
    }
    if (!asked)
        fprintf(stderr, "error: never reached the world before the frame budget "
                        "ran out\n");
    else
        fprintf(stderr, "error: the server never sent the board\n");

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

static int cmd_compete(int argc, char **argv)
{
    openmmo_config cfg;
    int send_all = 0;
    int i;

    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    for (i = 0; i < argc; i++)
        if (strcmp(argv[i], "--send") == 0)
            send_all = 1;
    parse_opts(argc, argv, &cfg);
    return drive_compete(&cfg, send_all);
}

static void print_gm(const openmmo_gm *g)
{
    int i, j;

    if (!g || !g->valid) {
        printf("gm: nothing received\n");
        return;
    }
    printf("gm: last 0x%02x lookup %s panel %s row %s\n", g->last_op,
           g->lookup_valid ? "yes" : "no",
           g->panel_valid ? "yes" : "no",
           g->entry_valid ? "yes" : "no");
    if (g->malformed)
        printf("  [a packet failed to decode]\n");
    if (g->lookup_valid) {
        const mmo_gm_lookup *lk = &g->lookup;

        if (!lk->found) {
            printf("  lookup: no such player\n");
        } else {
            printf("  lookup \"%s\" / \"%s\" rank %d status %d characters %d\n",
                   lk->session.name, lk->session.secondary, (int)lk->rank,
                   (int)lk->session.status, lk->count);
            if (lk->has_account)
                printf("    account %d from %s, playtime %lld\n",
                       (int)lk->account_id, lk->address,
                       (long long)lk->playtime);
            if (lk->has_detail)
                printf("    detail \"%s\"\n", lk->detail_text);
            for (i = 0; i < lk->count; i++)
                printf("    character %lld \"%s\" / \"%s\"\n",
                       (long long)lk->entry[i].entity_id,
                       lk->entry[i].name_a, lk->entry[i].name_b);
        }
    }
    if (g->panel_valid) {
        const mmo_gm_panel *p = &g->panel;

        printf("  panel variant %d%s\n", (int)(u8)p->variant,
               p->known ? "" : " (no body)");
        for (i = 0; i < p->row_count; i++) {
            printf("    row \"%s\"\n", p->row[i].label);
            for (j = 0; j < p->row[i].option_count; j++)
                printf("      option \"%s\" kind %d\n", p->row[i].option[j],
                       (int)p->row[i].option_kind[j]);
        }
    }
    if (g->entry_valid) {
        if (g->entry.clear)
            printf("  row: cleared\n");
        else
            printf("  row \"%s\" = \"%s\" (%d)\n", g->entry.label,
                   g->entry.value, (int)g->entry.count);
    }
}

/* Join as a developer and ask the server for one of each staff packet.
 * Nothing on this path is addressed to an ordinary player; what it measures
 * is that the three decode and that the two requests frame and send. */
static int drive_gm(const openmmo_config *cfg, int send_all)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1;
    int asked = 0;

    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED:
                print_gm(openmmo_client_gm(c));
                if (send_all) {
                    int bad = 0;

                    bad |= openmmo_client_admin_note_add(c, 1, "watched") != 0;
                    bad |= openmmo_client_admin_note_delete(c, 1, 2) != 0;
                    bad |= openmmo_client_moderation_confirm(c, 1) != 0;
                    if (bad) {
                        fprintf(stderr, "error: a request would not send\n");
                        goto done;
                    }
                    printf("sent three requests\n");
                }
                printf("asking for a panel\n");
                if (openmmo_client_send_chat(c, "/gm") != 0) {
                    fprintf(stderr, "error: could not send /gm\n");
                    goto done;
                }
                asked = 1;
                break;
            case OPENMMO_EV_GM:
                printf("gm: opcode 0x%02x found %d variant %d rows %d\n",
                       ev.gm.opcode, ev.gm.found, ev.gm.variant, ev.gm.rows);
                if (ev.gm.opcode == MMO_GAME_OP_GM_PANEL_ENTRY) {
                    print_gm(openmmo_client_gm(c));
                    rc = 0;
                    goto done;
                }
                break;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }
        usleep(2000);
    }
    if (!asked)
        fprintf(stderr, "error: never reached the world before the frame budget "
                        "ran out\n");
    else
        fprintf(stderr, "error: the server never sent the panel\n");

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

static int cmd_gm(int argc, char **argv)
{
    openmmo_config cfg;
    int send_all = 0;
    int i;

    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    for (i = 0; i < argc; i++)
        if (strcmp(argv[i], "--send") == 0)
            send_all = 1;
    parse_opts(argc, argv, &cfg);
    return drive_gm(&cfg, send_all);
}

static int cmd_sync(int argc, char **argv)
{
    openmmo_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    parse_opts(argc, argv, &cfg);
    return drive_sync(&cfg);
}

/* Join and report the friends list; optionally add or remove one name. */
static int drive_friends(const openmmo_config *cfg, const char *add,
                         const char *remove)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1;
    int asked = 0;
    int saw = 0;

    if (add && remove) {
        fprintf(stderr, "error: --add and --remove are one name each\n");
        openmmo_client_free(c);
        return 1;
    }

    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED:
                print_friends(openmmo_client_friends(c));
                if (!add && !remove) {
                    if (openmmo_client_friends(c)->valid) {
                        rc = 0;
                        goto done;
                    }
                    break;
                }
                printf("%s \"%s\"\n", add ? "adding" : "removing",
                       add ? add : remove);
                if ((add && openmmo_client_add_friend(c, add) != 0) ||
                    (remove && openmmo_client_remove_friend(c, remove) != 0)) {
                    fprintf(stderr, "error: could not send the name\n");
                    goto done;
                }
                asked = 1;
                break;
            case OPENMMO_EV_FRIENDS:
                print_friends(openmmo_client_friends(c));
                if (!add && !remove) {
                    saw = 1;
                    rc = 0;
                    goto done;
                }
                if (asked) {
                    saw = 1;
                    rc = 0;
                    goto done;
                }
                break;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }
        usleep(2000);
    }
    if (add || remove) {
        if (!asked)
            fprintf(stderr, "error: never reached the world before the frame budget "
                            "ran out\n");
        else if (!saw)
            fprintf(stderr, "error: the server never sent the friends list back\n");
    } else {
        fprintf(stderr, "error: the server never sent the friends list\n");
    }

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

static int cmd_friends(int argc, char **argv)
{
    openmmo_config cfg;
    const char *add = NULL;
    const char *remove = NULL;
    int i;

    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    for (i = 0; i < argc; i++) {
        int has_next = (i + 1 < argc);
        if (strcmp(argv[i], "--add") == 0 && has_next)
            add = argv[++i];
        else if (strcmp(argv[i], "--remove") == 0 && has_next)
            remove = argv[++i];
    }
    parse_opts(argc, argv, &cfg);
    return drive_friends(&cfg, add, remove);
}

enum {
    GUILD_ACT_NONE = 0,
    GUILD_ACT_CREATE,
    GUILD_ACT_INVITE,
    GUILD_ACT_LEAVE,
    GUILD_ACT_DISBAND,
    GUILD_ACT_MOTD,
    GUILD_ACT_RANK,
    GUILD_ACT_KICK,
    GUILD_ACT_LOG
};

static int drive_guild(const openmmo_config *cfg, int act, const char *name,
                       const char *tag, s64 member, u8 rank)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1;
    int asked = 0;
    int saw = 0;

    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED:
                print_guild(openmmo_client_guild(c));
                if (act == GUILD_ACT_NONE) {
                    saw = 1;
                    rc = 0;
                    goto done;
                }
                if (act == GUILD_ACT_CREATE) {
                    printf("creating guild %s [%s]\n", name, tag);
                    if (openmmo_client_guild_create(c, name, tag) != 0) {
                        fprintf(stderr, "error: could not send create\n");
                        goto done;
                    }
                } else if (act == GUILD_ACT_INVITE) {
                    printf("inviting %s\n", name);
                    if (openmmo_client_guild_invite(c, name) != 0) {
                        fprintf(stderr, "error: could not send invite\n");
                        goto done;
                    }
                } else if (act == GUILD_ACT_LEAVE) {
                    printf("leaving guild\n");
                    if (openmmo_client_guild_leave(c) != 0) {
                        fprintf(stderr, "error: could not send leave\n");
                        goto done;
                    }
                } else if (act == GUILD_ACT_DISBAND) {
                    printf("disbanding guild\n");
                    if (openmmo_client_guild_disband(c) != 0) {
                        fprintf(stderr, "error: could not send disband\n");
                        goto done;
                    }
                } else if (act == GUILD_ACT_MOTD) {
                    printf("setting motd\n");
                    if (openmmo_client_guild_motd(c, name) != 0) {
                        fprintf(stderr, "error: could not send motd\n");
                        goto done;
                    }
                } else if (act == GUILD_ACT_RANK) {
                    printf("assigning rank %u to %lld\n", (unsigned)rank,
                           (long long)member);
                    if (openmmo_client_guild_rank(c, member, rank) != 0) {
                        fprintf(stderr, "error: could not send rank\n");
                        goto done;
                    }
                } else if (act == GUILD_ACT_KICK) {
                    printf("kicking %lld\n", (long long)member);
                    if (openmmo_client_guild_kick(c, member) != 0) {
                        fprintf(stderr, "error: could not send kick\n");
                        goto done;
                    }
                } else if (act == GUILD_ACT_LOG) {
                    printf("asking for the guild log\n");
                    if (openmmo_client_guild_log(c, 0) != 0) {
                        fprintf(stderr, "error: could not send log\n");
                        goto done;
                    }
                }
                asked = 1;
                break;
            case OPENMMO_EV_GUILD:
                print_guild(openmmo_client_guild(c));
                if (!act) {
                    saw = 1;
                    rc = 0;
                    goto done;
                }
                if (asked) {
                    /* Create answers with 0x80 then 0x88. The first
                     * event is the profile; wait for the roster. */
                    if (act == GUILD_ACT_CREATE
                        && openmmo_client_guild(c)->member_count == 0)
                        break;
                    saw = 1;
                    rc = 0;
                    goto done;
                }
                break;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }
        usleep(2000);
    }
    if (act) {
        if (!asked)
            fprintf(stderr, "error: never reached the world before the frame budget "
                            "ran out\n");
        else if (!saw)
            fprintf(stderr, "error: the server never sent the guild back\n");
    } else {
        fprintf(stderr, "error: the server never sent the guild\n");
    }

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

static int cmd_guild(int argc, char **argv)
{
    openmmo_config cfg;
    const char *name = NULL;
    const char *tag = NULL;
    s64 member = 0;
    u8 rank = 0;
    int act = GUILD_ACT_NONE;
    int i;

    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    for (i = 0; i < argc; i++) {
        int has_next = (i + 1 < argc);
        if (strcmp(argv[i], "--create") == 0 && i + 2 < argc) {
            act = GUILD_ACT_CREATE;
            name = argv[++i];
            tag = argv[++i];
        } else if (strcmp(argv[i], "--invite") == 0 && has_next) {
            act = GUILD_ACT_INVITE;
            name = argv[++i];
        } else if (strcmp(argv[i], "--leave") == 0) {
            act = GUILD_ACT_LEAVE;
        } else if (strcmp(argv[i], "--disband") == 0) {
            act = GUILD_ACT_DISBAND;
        } else if (strcmp(argv[i], "--motd") == 0 && has_next) {
            act = GUILD_ACT_MOTD;
            name = argv[++i];
        } else if (strcmp(argv[i], "--rank") == 0 && i + 2 < argc) {
            act = GUILD_ACT_RANK;
            member = (s64)strtoll(argv[++i], NULL, 0);
            rank = (u8)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--kick") == 0 && has_next) {
            act = GUILD_ACT_KICK;
            member = (s64)strtoll(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--log") == 0) {
            act = GUILD_ACT_LOG;
        }
    }
    parse_opts(argc, argv, &cfg);
    return drive_guild(&cfg, act, name, tag, member, rank);
}

enum {
    MAIL_ACT_NONE = 0,
    MAIL_ACT_SENT,
    MAIL_ACT_SEND,
    MAIL_ACT_READ,
    MAIL_ACT_DELETE
};

static int drive_mail(const openmmo_config *cfg, int act, const char *name,
                      const char *subject, const char *body, s64 mail_id)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1;
    int asked = 0;
    int saw = 0;

    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED:
                print_mail(openmmo_client_mail(c));
                if (act == MAIL_ACT_NONE) {
                    saw = 1;
                    rc = 0;
                    goto done;
                }
                if (act == MAIL_ACT_SENT) {
                    printf("asking for sent mail\n");
                    if (openmmo_client_mail_page(c, 0, 1) != 0) {
                        fprintf(stderr, "error: could not request sent mail\n");
                        goto done;
                    }
                } else if (act == MAIL_ACT_SEND) {
                    printf("sending to %s\n", name);
                    if (openmmo_client_mail_send(c, name, subject, body) != 0) {
                        fprintf(stderr, "error: could not send mail\n");
                        goto done;
                    }
                } else if (act == MAIL_ACT_READ) {
                    printf("opening %lld\n", (long long)mail_id);
                    if (openmmo_client_mail_read(c, mail_id) != 0) {
                        fprintf(stderr, "error: could not read mail\n");
                        goto done;
                    }
                } else if (act == MAIL_ACT_DELETE) {
                    printf("deleting %lld\n", (long long)mail_id);
                    if (openmmo_client_mail_delete(c, mail_id, 0) != 0) {
                        fprintf(stderr, "error: could not delete mail\n");
                        goto done;
                    }
                }
                asked = 1;
                break;
            case OPENMMO_EV_MAIL:
                print_mail(openmmo_client_mail(c));
                if (!act) {
                    saw = 1;
                    rc = 0;
                    goto done;
                }
                if (asked) {
                    /* Compose answers with 0x96 then 0x98 then 0x97.
                     * Wait for the page so the letter is listed. */
                    if (act == MAIL_ACT_SEND
                        && openmmo_client_mail(c)->count == 0
                        && openmmo_client_mail(c)->result == 0)
                        break;
                    if (act == MAIL_ACT_READ
                        && !openmmo_client_mail(c)->have_detail)
                        break;
                    /* A page ask answers 0x98 then 0x97 as well, and the
                     * counts land first: leaving on them printed the inbox
                     * that was already held and never the sent box. */
                    if (act == MAIL_ACT_SENT
                        && !openmmo_client_mail(c)->listed_sent)
                        break;
                    /* Same for a delete: the counts are already right while
                     * the held page still lists the letter that went. */
                    if (act == MAIL_ACT_DELETE) {
                        const openmmo_mail *m = openmmo_client_mail(c);
                        int still = 0, k;

                        for (k = 0; k < m->count; k++)
                            if (m->entry[k].mail_id == mail_id)
                                still = 1;
                        if (still)
                            break;
                    }
                    saw = 1;
                    rc = 0;
                    goto done;
                }
                break;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }
        usleep(2000);
    }
    if (act) {
        if (!asked)
            fprintf(stderr, "error: never reached the world before the frame budget "
                            "ran out\n");
        else if (!saw)
            fprintf(stderr, "error: the server never sent the mailbox back\n");
    } else {
        fprintf(stderr, "error: the server never sent the mailbox\n");
    }

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

static int cmd_mail(int argc, char **argv)
{
    openmmo_config cfg;
    const char *name = NULL;
    const char *subject = NULL;
    const char *body = NULL;
    s64 mail_id = 0;
    int act = MAIL_ACT_NONE;
    int i;

    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    for (i = 0; i < argc; i++) {
        int has_next = (i + 1 < argc);
        if (strcmp(argv[i], "--sent") == 0) {
            act = MAIL_ACT_SENT;
        } else if (strcmp(argv[i], "--send") == 0 && has_next) {
            act = MAIL_ACT_SEND;
            name = argv[++i];
        } else if (strcmp(argv[i], "--subject") == 0 && has_next) {
            subject = argv[++i];
        } else if (strcmp(argv[i], "--body") == 0 && has_next) {
            body = argv[++i];
        } else if (strcmp(argv[i], "--read") == 0 && has_next) {
            act = MAIL_ACT_READ;
            mail_id = (s64)strtoll(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--delete") == 0 && has_next) {
            act = MAIL_ACT_DELETE;
            mail_id = (s64)strtoll(argv[++i], NULL, 0);
        }
    }
    if (act == MAIL_ACT_SEND && (!name || !subject || !body)) {
        fprintf(stderr, "error: --send needs --subject and --body\n");
        return 1;
    }
    parse_opts(argc, argv, &cfg);
    return drive_mail(&cfg, act, name, subject, body, mail_id);
}

/* --- gtl: the global trade link, driven headless ----------------------------- */
enum {
    GTL_ACT_SEARCH = 0,
    GTL_ACT_LIST_MON,
    GTL_ACT_LIST_ITEM,
    GTL_ACT_CANCEL,
    GTL_ACT_BUY,
    GTL_ACT_CLAIM,
    GTL_ACT_PRICE,
    GTL_ACT_LOG,
};

static void print_gtl_page(const openmmo_gtl *g)
{
    printf("page %d of kind %d: %d row(s), %d match(es)\n",
           g->page, g->kind, g->count, (int)g->total);
    for (int i = 0; i < g->count; i++) {
        const openmmo_gtl_row *r = &g->row[i];
        if (r->have_mon)
            printf("  #%lld  %s dex %u lv %d  $%d",
                   (long long)r->listing_id,
                   r->mon.nickname[0] ? r->mon.nickname : "(species)",
                   r->mon.dex_id, r->mon.level, (int)r->price);
        else
            printf("  #%lld  item %u x%d  $%d",
                   (long long)r->listing_id, r->item_id, r->quantity,
                   (int)r->price);
        if (g->kind == MMO_GTL_KIND_OWN)
            printf("  [state %d, %d up, %d unclaimed]", r->own_state,
                   r->own_remaining, r->own_unclaimed);
        printf("\n");
    }
    for (int i = 0; i < g->quote_count; i++)
        printf("  quote: item %u from $%d\n", g->quote[i].item_id,
               (int)g->quote[i].price);
}

static int drive_gtl(const openmmo_config *cfg, int act, int kind, int sort,
                     int page, const mmo_gtl_search *filter, int slot,
                     u16 item_id, s32 quantity, s32 price, s64 listing_id)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1;
    int opened = 0;   /* the 0xDC arrived */
    int acted = 0;    /* the verb went out */
    int settle = 0;   /* frames left to let a purchase's notices land */

    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED:
                printf("in the world; opening the trade link\n");
                if (openmmo_client_gtl_open(c) != 0) {
                    fprintf(stderr, "error: could not open the session\n");
                    goto done;
                }
                break;
            case OPENMMO_EV_GTL: {
                const openmmo_gtl *g = openmmo_client_gtl(c);
                if (!opened && g->session_open) {
                    opened = 1;
                    printf("the shelf is open\n");
                    break;
                }
                if (ev.gtl.opcode == MMO_GAME_OP_GTL_SEARCH_PAGE) {
                    print_gtl_page(g);
                    if (act == GTL_ACT_SEARCH) { rc = 0; goto done; }
                }
                if (ev.gtl.opcode == MMO_GAME_OP_GTL_LOG && act == GTL_ACT_LOG) {
                    printf("trade log: %d row(s)\n", g->log.count);
                    for (int i = 0; i < g->log.count; i++) {
                        const mmo_gtl_log_row *lr = &g->log.rows[i];
                        printf("  %s %s %d x%d for $%d at %d\n",
                               lr->bought ? "bought" : "sold",
                               (lr->type & 1) ? "item" : "mon", lr->what,
                               lr->amount, (int)lr->total, (int)lr->epoch);
                    }
                    rc = 0;
                    goto done;
                }
                if (ev.gtl.opcode == MMO_GAME_OP_GTL_RESULT) {
                    printf("result: code %d a %lld b %d\n", g->result.code,
                           (long long)g->result.a, (int)g->result.b);
                    /* The session open answers with a sold-notice of its
                     * own (20/21); only the verb's own answer ends the
                     * run. */
                    if (acted && g->result.code != MMO_GTL_R_SOLD_ONE &&
                        g->result.code != MMO_GTL_R_SOLD_MANY &&
                        (act == GTL_ACT_CLAIM || act == GTL_ACT_PRICE)) {
                        rc = 0;
                        goto done;
                    }
                }
                break;
            }
            case OPENMMO_EV_PARTY:
            case OPENMMO_EV_STORAGE:
                if (acted && settle == 0)
                    settle = 200; /* let the notices behind the resend land */
                break;
            default:
                break;
            }
        }

        if (opened && !acted) {
            acted = 1;
            if (act == GTL_ACT_SEARCH) {
                printf("asking for page %d\n", page);
                if (openmmo_client_gtl_search(c, kind, sort, page, filter) !=
                    0) {
                    fprintf(stderr, "error: could not send the search\n");
                    goto done;
                }
            } else if (act == GTL_ACT_LIST_MON) {
                const openmmo_party *p = openmmo_client_party(c);
                if (slot < 0 || slot >= p->count) {
                    fprintf(stderr, "error: no party monster in slot %d\n", slot);
                    goto done;
                }
                printf("listing %s (slot %d) for $%d\n",
                       p->mon[slot].nickname[0] ? p->mon[slot].nickname : "?",
                       slot, (int)price);
                if (openmmo_client_gtl_list_mon(c, p->mon[slot].id, price) != 0) {
                    fprintf(stderr, "error: could not send the listing\n");
                    goto done;
                }
                settle = 400;
            } else if (act == GTL_ACT_LIST_ITEM) {
                printf("listing item %u x%d for $%d\n", item_id, (int)quantity,
                       (int)price);
                if (openmmo_client_gtl_list_item(c, item_id, quantity, price) !=
                    0) {
                    fprintf(stderr, "error: could not send the listing\n");
                    goto done;
                }
                settle = 400;
            } else if (act == GTL_ACT_CANCEL) {
                printf("taking back #%lld\n", (long long)listing_id);
                if (openmmo_client_gtl_cancel(c, listing_id) != 0) {
                    fprintf(stderr, "error: could not send the take-back\n");
                    goto done;
                }
                settle = 400;
            } else if (act == GTL_ACT_BUY) {
                printf("buying %d of #%lld\n", (int)quantity,
                       (long long)listing_id);
                if (openmmo_client_gtl_buy(c, listing_id, quantity) != 0) {
                    fprintf(stderr, "error: could not send the purchase\n");
                    goto done;
                }
                settle = 400;
            } else if (act == GTL_ACT_CLAIM) {
                printf("claiming #%lld\n", (long long)listing_id);
                if (openmmo_client_gtl_claim(c, &listing_id, 1) != 0) {
                    fprintf(stderr, "error: could not send the claim\n");
                    goto done;
                }
            } else if (act == GTL_ACT_PRICE) {
                printf("repricing #%lld to $%d\n", (long long)listing_id,
                       (int)price);
                if (openmmo_client_gtl_price(c, listing_id, price) != 0) {
                    fprintf(stderr,
                            "error: could not send the price change\n");
                    goto done;
                }
            } else if (act == GTL_ACT_LOG) {
                printf("asking for the trade log\n");
                if (openmmo_client_gtl_log(c) != 0) {
                    fprintf(stderr, "error: could not ask for the log\n");
                    goto done;
                }
            }
        }

        /* The mutating verbs have no single answering packet: the server
         * replies with notices and resends. Let those land, then show the
         * shelf's own page as the proof. */
        if (settle > 0 && --settle == 0) {
            print_party(openmmo_client_party(c));
            rc = 0;
            goto done;
        }

        mmo_plat_sleep_us(2000);
    }
    fprintf(stderr,
            "error: the shelf never answered before the frame budget ran out\n");
done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

static int cmd_gtl(int argc, char **argv)
{
    openmmo_config cfg;
    mmo_gtl_search filter = { 0, -1, -1, -1, -1, -1, -1 };
    int act = GTL_ACT_SEARCH;
    int kind = MMO_GTL_KIND_POKEMON;
    int sort = MMO_GTL_SORT_NEWEST;
    int page = 0;
    int slot = -1;
    u16 item_id = 0;
    s32 quantity = 1;
    s32 price = 0;
    s64 listing_id = 0;

    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    for (int i = 0; i < argc; i++) {
        int has_next = (i + 1 < argc);
        if (strcmp(argv[i], "--items") == 0)
            kind = MMO_GTL_KIND_ITEM;
        else if (strcmp(argv[i], "--own") == 0)
            kind = MMO_GTL_KIND_OWN;
        else if (strcmp(argv[i], "--page") == 0 && has_next)
            page = atoi(argv[++i]);
        else if (strcmp(argv[i], "--species") == 0 && has_next)
            filter.species = (u16)atoi(argv[++i]);
        else if (strcmp(argv[i], "--minlvl") == 0 && has_next)
            filter.min_level = atoi(argv[++i]);
        else if (strcmp(argv[i], "--maxlvl") == 0 && has_next)
            filter.max_level = atoi(argv[++i]);
        else if (strcmp(argv[i], "--shiny") == 0)
            filter.shiny = 1;
        else if (strcmp(argv[i], "--nature") == 0 && has_next)
            filter.nature = atoi(argv[++i]);
        else if (strcmp(argv[i], "--minprice") == 0 && has_next)
            filter.min_price = atoi(argv[++i]);
        else if (strcmp(argv[i], "--maxprice") == 0 && has_next)
            filter.max_price = atoi(argv[++i]);
        else if (strcmp(argv[i], "--list-mon") == 0 && has_next) {
            act = GTL_ACT_LIST_MON;
            slot = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--list-item") == 0 && has_next) {
            act = GTL_ACT_LIST_ITEM;
            item_id = (u16)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--qty") == 0 && has_next)
            quantity = atoi(argv[++i]);
        else if (strcmp(argv[i], "--price") == 0 && has_next)
            price = atoi(argv[++i]);
        else if (strcmp(argv[i], "--cancel") == 0 && has_next) {
            act = GTL_ACT_CANCEL;
            listing_id = (s64)strtoll(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--buy") == 0 && has_next) {
            act = GTL_ACT_BUY;
            listing_id = (s64)strtoll(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--claim") == 0 && has_next) {
            act = GTL_ACT_CLAIM;
            listing_id = (s64)strtoll(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--reprice") == 0 && has_next) {
            act = GTL_ACT_PRICE;
            listing_id = (s64)strtoll(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--sort") == 0 && has_next)
            sort = atoi(argv[++i]);
        else if (strcmp(argv[i], "--log") == 0)
            act = GTL_ACT_LOG;
    }
    if (act == GTL_ACT_PRICE && price < 1) {
        fprintf(stderr, "error: a reprice needs --price\n");
        return 1;
    }
    if ((act == GTL_ACT_LIST_MON || act == GTL_ACT_LIST_ITEM) && price < 1) {
        fprintf(stderr, "error: a listing needs --price\n");
        return 1;
    }
    parse_opts(argc, argv, &cfg);
    return drive_gtl(&cfg, act, kind, sort, page, &filter, slot, item_id,
                     quantity, price, listing_id);
}

/* --- trade: two sessions swap their lead monsters ---------------------------- */
typedef struct {
    const char *tag;
    openmmo_client *c;
    int joined;
    int open;         /* the table opened */
    int picked;       /* our slot went up */
    int saw_peer;     /* the peer's record arrived */
    int confirmed;
    int done;         /* the 0x6A DONE arrived */
    int settled;      /* the party resend after it */
    int before_count;
    s64 before_lead; /* monster id of slot 0 before the trade, the id, not
                       * the dex, because both dev checkpoints lead with the
                       * same species and a dex check would pass untraded */
    s64 after_lead;
    int after_count;
} trade_side;

static void trade_drain(trade_side *s, trade_side *peer)
{
    openmmo_event ev;
    while (openmmo_client_poll_event(s->c, &ev)) {
        switch (ev.kind) {
        case OPENMMO_EV_STATUS:
            printf("%s -> %s\n", s->tag, openmmo_status_name(ev.status));
            break;
        case OPENMMO_EV_CHAT:
            print_chat_line(s->tag, &ev);
            break;
        case OPENMMO_EV_JOINED:
            /* The party container can land after this event; the pick below
             * waits for it rather than reading an empty store. */
            s->joined = 1;
            printf("%s in the world\n", s->tag);
            break;
        case OPENMMO_EV_DUEL_INVITE:
            if (ev.duel.request_type == 1) {
                printf("%s hears %s's trade offer; accepting\n", s->tag,
                       ev.duel.name);
                openmmo_client_trade_action(s->c, MMO_TRADE_ACTION_ACCEPT);
            }
            break;
        case OPENMMO_EV_TRADE:
            if (ev.trade.state == MMO_TRADE_STATE_OPEN) {
                s->open = 1;
                printf("%s at the table with %s\n", s->tag, ev.trade.peer);
            } else if (ev.trade.entry) {
                s->saw_peer = 1;
                printf("%s sees the offer: dex %u lv %d\n", s->tag,
                       openmmo_client_trade(s->c)->peer_mon.dex_id,
                       openmmo_client_trade(s->c)->peer_mon.level);
            } else if (ev.trade.state == MMO_TRADE_STATE_DONE) {
                s->done = 1;
                if (s->after_count > 0 && !s->settled) {
                    s->settled = 1;
                    printf("%s now leads with monster %lld of %d\n", s->tag,
                           (long long)s->after_lead, s->after_count);
                }
                printf("%s: the trade is written\n", s->tag);
            } else if (ev.trade.state == MMO_TRADE_STATE_CANCELLED) {
                printf("%s: the trade was cancelled\n", s->tag);
            }
            break;
        case OPENMMO_EV_PARTY: {
            /* The settlement's resend lands BEFORE the DONE state, so the
             * latest party is kept on every resend and read once DONE says
             * the swap is written. */
            const openmmo_party *p = openmmo_client_party(s->c);
            s->after_count = p->count;
            s->after_lead = p->count > 0 ? p->mon[0].id : 0;
            if (s->done && !s->settled) {
                s->settled = 1;
                printf("%s now leads with monster %lld (dex %u) of %d\n",
                       s->tag, (long long)s->after_lead,
                       p->count > 0 ? p->mon[0].dex_id : 0, s->after_count);
            }
            break;
        }
        default:
            break;
        }
    }
    (void)peer;
}

static int drive_trade(const openmmo_config *ca, const openmmo_config *cb)
{
    trade_side a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    a.tag = "A";
    b.tag = "B";
    a.c = openmmo_client_new();
    b.c = openmmo_client_new();
    if (!a.c || !b.c) {
        fprintf(stderr, "error: out of memory\n");
        openmmo_client_free(a.c);
        openmmo_client_free(b.c);
        return 1;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1;
    int b_started = 0;
    int asked = 0;

    /* Two handshakes in the same millisecond against one login process can
     * collide; B starts once A is in the world. */
    openmmo_client_start(a.c, ca);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(a.c);
        if (b_started)
            openmmo_client_pump(b.c);
        trade_drain(&a, &b);
        if (b_started)
            trade_drain(&b, &a);

        if (!b_started && a.joined) {
            openmmo_client_start(b.c, cb);
            b_started = 1;
        }
        if (!asked && a.joined && b.joined) {
            asked = 1;
            printf("A asks %s to trade\n", cb->select_name);
            if (openmmo_client_trade_request(a.c, cb->select_name) != 0) {
                fprintf(stderr, "error: could not send the request\n");
                goto done;
            }
        }
        trade_side *sides[2] = { &a, &b };
        for (int s = 0; s < 2; s++) {
            trade_side *t = sides[s];
            const openmmo_party *p = openmmo_client_party(t->c);
            if (t->open && !t->picked && p->count > 0) {
                t->picked = 1;
                t->before_count = p->count;
                t->before_lead = p->mon[0].id;
                printf("%s offers monster %lld (dex %u) of %d\n", t->tag,
                       (long long)t->before_lead, p->mon[0].dex_id,
                       t->before_count);
                openmmo_client_trade_select(t->c, 0);
            }
        }
        if (a.picked && a.saw_peer && !a.confirmed) {
            a.confirmed = 1;
            openmmo_client_trade_action(a.c, MMO_TRADE_ACTION_CONFIRM);
        }
        if (b.picked && b.saw_peer && !b.confirmed) {
            b.confirmed = 1;
            openmmo_client_trade_action(b.c, MMO_TRADE_ACTION_CONFIRM);
        }
        if (a.settled && b.settled) {
            if (a.after_lead == b.before_lead &&
                b.after_lead == a.before_lead &&
                a.after_count == a.before_count &&
                b.after_count == b.before_count) {
                printf("trade verified: the leads crossed and nobody "
                       "gained or lost a monster\n");
                rc = 0;
            } else {
                fprintf(stderr,
                        "error: the parties do not mirror the trade "
                        "(A %lld->%lld of %d->%d, B %lld->%lld of %d->%d)\n",
                        (long long)a.before_lead, (long long)a.after_lead,
                        a.before_count, a.after_count,
                        (long long)b.before_lead, (long long)b.after_lead,
                        b.before_count, b.after_count);
            }
            goto done;
        }

        mmo_plat_sleep_us(2000);
    }
    fprintf(stderr,
            "error: the trade never settled before the frame budget ran out\n");
done:
    openmmo_client_disconnect(a.c);
    openmmo_client_disconnect(b.c);
    openmmo_client_free(a.c);
    openmmo_client_free(b.c);
    return rc;
}

static int cmd_trade(int argc, char **argv)
{
    openmmo_config a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    a.mode = b.mode = OPENMMO_MODE_GAME_JOIN;
    a.user = "admin"; a.pass = "admin";
    b.user = "test";  b.pass = "test";

    for (int i = 0; i < argc; i++) {
        int has_next = (i + 1 < argc);
        if (strcmp(argv[i], "--user") == 0 && has_next)
            a.user = argv[++i];
        else if (strcmp(argv[i], "--pass") == 0 && has_next)
            a.pass = argv[++i];
        else if (strcmp(argv[i], "--user2") == 0 && has_next)
            b.user = argv[++i];
        else if (strcmp(argv[i], "--pass2") == 0 && has_next)
            b.pass = argv[++i];
        else if (strcmp(argv[i], "--character") == 0 && has_next) {
            a.select_name = argv[++i];
            set_select_how(&a, OPENMMO_SELECT_NAME);
        } else if (strcmp(argv[i], "--character2") == 0 && has_next) {
            b.select_name = argv[++i];
            set_select_how(&b, OPENMMO_SELECT_NAME);
        }
    }
    if (!b.select_name) {
        fprintf(stderr, "error: trade needs --character2, the name A asks\n");
        return 1;
    }
    return drive_trade(&a, &b);
}

enum {
    LINK_ACT_NONE = 0,
    LINK_ACT_INVITE,
    LINK_ACT_LEAVE,
    LINK_ACT_KICK,
    LINK_ACT_CAPTAIN
};

static int drive_link(const openmmo_config *cfg, int act, const char *name,
                      s64 member)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1;
    int asked = 0;
    int saw = 0;

    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED:
                print_link(openmmo_client_link(c));
                if (act == LINK_ACT_NONE) {
                    saw = 1;
                    rc = 0;
                    goto done;
                }
                if (act == LINK_ACT_INVITE) {
                    printf("inviting %s\n", name);
                    if (openmmo_client_link_invite(c, name) != 0) {
                        fprintf(stderr, "error: could not send invite\n");
                        goto done;
                    }
                } else if (act == LINK_ACT_LEAVE) {
                    printf("leaving link\n");
                    if (openmmo_client_link_leave(c) != 0) {
                        fprintf(stderr, "error: could not send leave\n");
                        goto done;
                    }
                } else if (act == LINK_ACT_KICK) {
                    printf("kicking %lld\n", (long long)member);
                    if (openmmo_client_link_kick(c, member) != 0) {
                        fprintf(stderr, "error: could not send kick\n");
                        goto done;
                    }
                } else if (act == LINK_ACT_CAPTAIN) {
                    printf("assigning captain %lld\n", (long long)member);
                    if (openmmo_client_link_captain(c, member) != 0) {
                        fprintf(stderr, "error: could not send captain\n");
                        goto done;
                    }
                }
                asked = 1;
                break;
            case OPENMMO_EV_LINK:
                print_link(openmmo_client_link(c));
                if (!act) {
                    saw = 1;
                    rc = 0;
                    goto done;
                }
                if (asked) {
                    saw = 1;
                    rc = 0;
                    goto done;
                }
                break;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }
        usleep(2000);
    }
    if (act) {
        if (!asked)
            fprintf(stderr, "error: never reached the world before the frame budget "
                            "ran out\n");
        else if (!saw)
            fprintf(stderr, "error: the server never sent the link back\n");
    } else {
        fprintf(stderr, "error: the server never sent the link\n");
    }

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

static int cmd_link(int argc, char **argv)
{
    openmmo_config cfg;
    const char *name = NULL;
    s64 member = 0;
    int act = LINK_ACT_NONE;
    int i;

    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    for (i = 0; i < argc; i++) {
        int has_next = (i + 1 < argc);
        if (strcmp(argv[i], "--invite") == 0 && has_next) {
            act = LINK_ACT_INVITE;
            name = argv[++i];
        } else if (strcmp(argv[i], "--leave") == 0) {
            act = LINK_ACT_LEAVE;
        } else if (strcmp(argv[i], "--kick") == 0 && has_next) {
            act = LINK_ACT_KICK;
            member = (s64)strtoll(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--captain") == 0 && has_next) {
            act = LINK_ACT_CAPTAIN;
            member = (s64)strtoll(argv[++i], NULL, 0);
        }
    }
    parse_opts(argc, argv, &cfg);
    return drive_link(&cfg, act, name, member);
}

/* Join as a developer and ask the server for a Potion. */
static int drive_gift(const openmmo_config *cfg)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1;
    int asked = 0;

    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED:
                print_bag(openmmo_client_bag(c));
                printf("asking for a gift\n");
                if (openmmo_client_send_chat(c, "/gift") != 0) {
                    fprintf(stderr, "error: could not send /gift\n");
                    goto done;
                }
                asked = 1;
                break;
            case OPENMMO_EV_BAG:
                print_bag(openmmo_client_bag(c));
                printf("gift: bag settled\n");
                rc = 0;
                goto done;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }
        usleep(2000);
    }
    if (!asked)
        fprintf(stderr, "error: never reached the world before the frame budget "
                        "ran out\n");
    else
        fprintf(stderr, "error: the server never sent a bag after the gift\n");

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

static int cmd_gift(int argc, char **argv)
{
    openmmo_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    parse_opts(argc, argv, &cfg);
    return drive_gift(&cfg);
}

/* Join, send one chat line as given, print what comes back for a moment,
 * leave. The line is how every dev command travels, so this is the generic
 * driver the fixed verbs (gift, move, ...) each hard-code one use of. */
static int drive_say(const openmmo_config *cfg, const char *line)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1;
    int said_at = -1;

    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED:
                printf("saying: %s\n", line);
                if (openmmo_client_send_chat(c, line) != 0) {
                    fprintf(stderr, "error: could not send the line\n");
                    goto done;
                }
                said_at = f;
                rc = 0;
                break;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }
        /* a beat after the send, so the reply lines land in the output */
        if (said_at >= 0 && f - said_at > 1500)
            goto done;
        usleep(2000);
    }
    fprintf(stderr, "error: never reached the world before the frame budget "
                    "ran out\n");

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

static int cmd_say(int argc, char **argv)
{
    openmmo_config cfg;
    const char *line = NULL;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    /* The line is the first argument, before any --flag: a scan for "any
     * non-dash word" reads a flag's own value as the line to say. */
    if (argc > 0 && argv[0][0] != '-') {
        line = argv[0];
        argc--;
        argv++;
    }
    parse_opts(argc, argv, &cfg);
    if (line == NULL) {
        fprintf(stderr, "usage: say \"<chat line>\" [--user U --pass P --character C]\n");
        return 2;
    }
    return drive_say(&cfg, line);
}

/* Join as a developer and ask the server for a scripted facing. */
static int drive_move(const openmmo_config *cfg)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1;
    int asked = 0;
    int saw = 0;
    const openmmo_script_move *seq;

    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED:
                printf("asking for a scripted move\n");
                if (openmmo_client_send_chat(c, "/move") != 0) {
                    fprintf(stderr, "error: could not send /move\n");
                    goto done;
                }
                asked = 1;
                break;
            case OPENMMO_EV_SCRIPT_MOVE:
                seq = openmmo_client_script_move(c);
                print_script_move(seq);
                if (seq && seq->valid && seq->count > 0)
                    saw = 1;
                break;
            case OPENMMO_EV_DIALOG:
                if (asked && saw && !openmmo_client_in_dialog(c)) {
                    printf("move: unlocked\n");
                    rc = 0;
                    goto done;
                }
                break;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }
        usleep(2000);
    }
    if (!asked)
        fprintf(stderr, "error: never reached in-game to ask for a move\n");
    else if (!saw)
        fprintf(stderr, "error: the server never sent a movement sequence\n");
    else
        rc = 0;
done:
    openmmo_client_free(c);
    return rc;
}

static int cmd_move(int argc, char **argv)
{
    openmmo_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    parse_opts(argc, argv, &cfg);
    return drive_move(&cfg);
}

/* Join and use one bag item on a party member. */
static int drive_use(const openmmo_config *cfg, int item_id, int slot)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1;
    int sent = 0;
    int saw_party = 0;
    int saw_bag = 0;
    const openmmo_party *party;
    const openmmo_bag *bag;
    int i, held;

    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED:
                print_party(openmmo_client_party(c));
                print_bag(openmmo_client_bag(c));
                party = openmmo_client_party(c);
                if (!party || !party->valid || slot < 0 || slot >= party->count) {
                    fprintf(stderr, "error: no party member in slot %d\n", slot);
                    goto done;
                }
                bag = openmmo_client_bag(c);
                held = 0;
                for (i = 0; bag && i < bag->count; i++) {
                    if (bag->stack[i].item_id == (u16)item_id)
                        held = bag->stack[i].quantity;
                }
                if (held < 1) {
                    fprintf(stderr, "error: bag has no stack of wire %d\n",
                            item_id);
                    goto done;
                }
                printf("using wire %d on slot %d id %lld\n", item_id, slot,
                       (long long)party->mon[slot].id);
                if (openmmo_client_use_item(c, (u16)item_id,
                                            party->mon[slot].id) != 0) {
                    fprintf(stderr, "error: the client refused that use\n");
                    goto done;
                }
                sent = 1;
                break;
            case OPENMMO_EV_PARTY:
                if (sent) {
                    saw_party = 1;
                    print_party(openmmo_client_party(c));
                }
                break;
            case OPENMMO_EV_BAG:
                if (sent) {
                    saw_bag = 1;
                    print_bag(openmmo_client_bag(c));
                }
                break;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }

        if (sent && saw_party && saw_bag) {
            rc = 0;
            goto done;
        }
        usleep(2000);
    }
    if (!sent)
        fprintf(stderr, "error: never reached the world before the frame budget "
                        "ran out\n");
    else
        fprintf(stderr, "error: the server never settled the use\n");

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

static int cmd_use(int argc, char **argv)
{
    openmmo_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    parse_opts(argc, argv, &cfg);

    int item_id = -1;
    int slot = 0;
    for (int i = 0; i < argc; i++) {
        int has_next = (i + 1 < argc);
        if (strcmp(argv[i], "--item") == 0 && has_next)
            item_id = atoi(argv[++i]);
        else if (strcmp(argv[i], "--slot") == 0 && has_next)
            slot = atoi(argv[++i]);
    }
    if (item_id <= 0) {
        fprintf(stderr, "error: --item needs a wire item id\n");
        return 1;
    }
    if (slot < 0) {
        fprintf(stderr, "error: --slot is a party index\n");
        return 1;
    }
    return drive_use(&cfg, item_id, slot);
}

/* --- movelearn: answer the server's move offer ------------------------------ */
static void print_movesets(const openmmo_client *c, const char *when)
{
    const openmmo_party *p = openmmo_client_party(c);
    printf("%s:\n", when);
    for (int i = 0; i < p->count; i++) {
        printf("  slot %d: #%d %s lv%d moves", p->mon[i].slot, p->mon[i].dex_id,
               p->mon[i].nickname, p->mon[i].level);
        for (int m = 0; m < 4; m++)
            printf(" %d", p->mon[i].move_id[m]);
        printf("\n");
    }
}

static int drive_move_learn(const openmmo_config *cfg, int slot)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1, answered = 0;
    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED:
                print_movesets(c, "on join");
                break;
            case OPENMMO_EV_MOVE_LEARN:
                if (ev.move_learn.slot != OPENMMO_MOVE_LEARN_NO_SLOT) {
                    printf("monster %lld learned move %d into slot %d\n",
                           (long long)ev.move_learn.monster_id,
                           ev.move_learn.move_id, ev.move_learn.slot);
                    break;
                }
                printf("monster %lld is offered move %d (engine %d) and has no "
                       "room for it\n", (long long)ev.move_learn.monster_id,
                       ev.move_learn.move_id, ev.move_learn.engine_move_id);
                if (ev.move_learn.party_index >= 0) {
                    const openmmo_party *p = openmmo_client_party(c);
                    const openmmo_party_mon *m = &p->mon[ev.move_learn.party_index];
                    for (int i = 0; i < OPENMMO_MOVE_SLOTS; i++)
                        printf("  slot %d: move %d\n", i, m->move_id[i]);
                } else {
                    printf("  (not a monster the party the client holds knows)\n");
                }
                if (slot == OPENMMO_MOVE_LEARN_NO_SLOT)
                    printf("keeping the moveset\n");
                else
                    printf("dropping slot %d for it\n", slot);
                if (openmmo_client_reply_move_learn(c, slot) != 0) {
                    fprintf(stderr, "error: the client refused that answer\n");
                    goto done;
                }
                answered = 1;
                break;
            case OPENMMO_EV_PARTY:
                if (answered) {
                    print_movesets(c, "after the answer");
                    rc = 0;
                    goto done;
                }
                break;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }
        usleep(2000);
    }
    if (answered)
        fprintf(stderr, "error: the server never sent the party back\n");
    else
        fprintf(stderr, "error: no move offer arrived before the frame budget "
                        "ran out\n");

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

static int cmd_move_learn(int argc, char **argv)
{
    openmmo_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = "test";
    cfg.pass = "test";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    parse_opts(argc, argv, &cfg);

    int slot = OPENMMO_MOVE_LEARN_NO_SLOT;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--slot") == 0 && i + 1 < argc)
            slot = atoi(argv[++i]);
    }
    if (slot != OPENMMO_MOVE_LEARN_NO_SLOT &&
        (slot < 0 || slot >= OPENMMO_MOVE_SLOTS)) {
        fprintf(stderr, "error: --slot is 0..%d, or left off to keep the "
                        "moveset\n", OPENMMO_MOVE_SLOTS - 1);
        return 1;
    }
    return drive_move_learn(&cfg, slot);
}

/* --- breed: the daycare round-trip ------------------------------------------ */
static void print_incubators(const openmmo_client *c)
{
    const openmmo_incubators *in = openmmo_client_incubators(c);
    if (!in->valid) {
        printf("incubators: the server has not sent any\n");
        return;
    }
    printf("incubators: %d of %d\n", in->count, in->total);
    for (int i = 0; i < in->count; i++)
        printf("  slot %d: %d of %d uses left\n", i, in->slot[i].uses_left,
               OPENMMO_INCUBATOR_USES_FULL);
}

static void print_eggs(const openmmo_client *c)
{
    const openmmo_party *p = openmmo_client_party(c);
    int eggs = 0;
    for (int i = 0; i < p->count; i++) {
        if (!p->mon[i].egg)
            continue;
        eggs++;
        printf("  party slot %d: egg (monster %lld)\n", p->mon[i].slot,
               (long long)p->mon[i].id);
    }
    printf("eggs in the party: %d\n", eggs);
}

static const char *breed_move_source_name(int source)
{
    switch (source) {
    case OPENMMO_BREED_MOVE_LEVEL_UP: return "learned normally";
    case OPENMMO_BREED_MOVE_EARLY:    return "taught early by a parent";
    case OPENMMO_BREED_MOVE_PARENT:   return "known by a parent";
    case OPENMMO_BREED_MOVE_EGG:      return "an egg move";
    case OPENMMO_BREED_MOVE_ITEM:     return "from an item a parent holds";
    }
    return "unknown";
}

static void print_forecast(const openmmo_client *c)
{
    static const char *stat_name[OPENMMO_BREED_STATS] = {
        "hp", "atk", "def", "speed", "spAtk", "spDef"
    };
    const openmmo_breeding_forecast *f = openmmo_client_breeding_forecast(c);

    if (!f->has_preview) {
        printf("forecast: the server will not breed those two\n");
        return;
    }
    printf("forecast: #%d (engine %u) form %d, nature %d, OT %d\n", f->species,
           (unsigned)f->engine_species, f->form, f->nature, f->ot);
    for (int i = 0; i < f->stat_count && i < OPENMMO_BREED_STATS; i++) {
        printf("  %-5s %s", stat_name[i],
               f->stat[i].guaranteed ? "guaranteed" : "rolled");
        if (f->stat[i].item_id)
            printf(" (item %d)", f->stat[i].item_id);
        for (int j = 0; j < f->stat[i].outcome_count; j++)
            printf(" %d:%.1f%%", f->stat[i].outcome[j].value,
                   (double)f->stat[i].outcome[j].chance);
        printf("\n");
    }
    for (int i = 0; i < f->move_count; i++)
        printf("  move %d (engine %u), %s\n", f->move_id[i],
               (unsigned)f->move[i], breed_move_source_name(f->move_source[i]));
    if (f->gender_selectable)
        printf("  gender is selectable: %d / %d\n", f->gender_cost[0],
               f->gender_cost[1]);
    else
        printf("  gender is not selectable for this pairing\n");
}

static int drive_breed(const openmmo_config *cfg, s64 own, s64 partner,
                       int gender)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1, asked = 0;
    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED:
                print_incubators(c);
                print_eggs(c);
                if (!own || !partner) {
                    rc = 0;
                    goto done;
                }
                printf("asking what monster %lld and %lld would produce\n",
                       (long long)own, (long long)partner);
                if (openmmo_client_breeding_preview(c, own, partner, gender) != 0) {
                    fprintf(stderr, "error: the client refused that pairing\n");
                    goto done;
                }
                asked = 1;
                break;
            case OPENMMO_EV_EGG_INCUBATORS:
                print_incubators(c);
                break;
            case OPENMMO_EV_BREEDING_FORECAST:
                print_forecast(c);
                rc = 0;
                goto done;
            case OPENMMO_EV_EGG_HATCH:
                printf("the egg in party slot %d hatched into #%d (engine %u)\n",
                       ev.hatch.party_index, ev.hatch.species,
                       (unsigned)ev.hatch.engine_species);
                break;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }
        usleep(2000);
    }
    if (asked)
        fprintf(stderr, "error: the server never answered the pairing\n");
    else
        fprintf(stderr, "error: the session never reached the world\n");
done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

static int cmd_breed(int argc, char **argv)
{
    openmmo_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = "test";
    cfg.pass = "test";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    parse_opts(argc, argv, &cfg);

    s64 own = 0, partner = 0;
    int gender = OPENMMO_BREED_GENDER_ANY;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--own") == 0 && i + 1 < argc)
            own = strtoll(argv[++i], NULL, 0);
        else if (strcmp(argv[i], "--partner") == 0 && i + 1 < argc)
            partner = strtoll(argv[++i], NULL, 0);
        else if (strcmp(argv[i], "--gender") == 0 && i + 1 < argc)
            gender = atoi(argv[++i]);
    }
    if (gender < OPENMMO_BREED_GENDER_ANY || gender > OPENMMO_BREED_GENDER_SECOND) {
        fprintf(stderr, "error: --gender is %d (leave it to the server), 0 or 1\n",
                OPENMMO_BREED_GENDER_ANY);
        return 1;
    }
    return drive_breed(&cfg, own, partner, gender);
}

/* --- evolve: answer the server's evolution -------------------------------- */
static void print_party_species(const openmmo_client *c, const char *when)
{
    const openmmo_party *p = openmmo_client_party(c);
    printf("%s:\n", when);
    for (int i = 0; i < p->count; i++)
        printf("  slot %d: #%d (engine %u) %s lv%d\n", p->mon[i].slot,
               p->mon[i].dex_id, (unsigned)p->mon[i].species,
               p->mon[i].nickname, p->mon[i].level);
}

static int drive_evolve(const openmmo_config *cfg, int accept)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    int rc = 1, answered = 0;
    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED:
                print_party_species(c, "on join");
                break;
            case OPENMMO_EV_EVOLUTION:
                printf("monster %lld is evolving into #%d (engine %u)%s\n",
                       (long long)ev.evolution.monster_id, ev.evolution.species,
                       (unsigned)ev.evolution.engine_species,
                       ev.evolution.cancelable ? ", and may be stopped"
                                               : ", and may not be stopped");
                if (ev.evolution.party_index >= 0)
                    printf("  party slot %d, #%d today\n",
                           ev.evolution.party_index, ev.evolution.from_species);
                else if (ev.evolution.storage_index >= 0)
                    printf("  PC entry %d, #%d today\n",
                           ev.evolution.storage_index, ev.evolution.from_species);
                else
                    printf("  (not a monster the containers the client holds "
                           "know)\n");
                if (!accept && !ev.evolution.cancelable) {
                    fprintf(stderr, "error: the server did not allow this one to "
                                    "be stopped\n");
                    goto done;
                }
                printf("%s\n", accept ? "letting it through" : "stopping it");
                if (openmmo_client_reply_evolution(c, accept) != 0) {
                    fprintf(stderr, "error: the client refused that answer\n");
                    goto done;
                }
                answered = 1;
                break;
            case OPENMMO_EV_PARTY:
                if (answered) {
                    print_party_species(c, "after the answer");
                    rc = 0;
                    goto done;
                }
                break;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }
        usleep(2000);
    }
    if (answered)
        fprintf(stderr, "error: the server never sent the party back\n");
    else
        fprintf(stderr, "error: no evolution arrived before the frame budget "
                        "ran out\n");

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

static int cmd_evolve(int argc, char **argv)
{
    openmmo_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = "test";
    cfg.pass = "test";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    parse_opts(argc, argv, &cfg);

    int accept = 1;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--decline") == 0)
            accept = 0;
    }
    return drive_evolve(&cfg, accept);
}

/* --- import-save: a save file offered to the server as this character -------- */
static int drive_import_save(const openmmo_config *cfg, const char *report_path,
                             const char *chain_path)
{
    openmmo_client *c;
    u8 *report = NULL;
    u8 *chain = NULL;
    size_t len = 0;
    size_t chain_len = 0;
    s32 play_seconds = 0;
    char sha[65];
    int rc = 1, sent = 0, offered_chain = 0;

    /* Line buffered before anything is written, not after: setvbuf on a stream
     * that has already been printed to is undefined, and what it did here was
     * drop everything said so far when the output was a file. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (mmo_import_report_read(report_path, &report, &len) != 0)
        return 1;
    /* Read before the session opens, so a chain that is not one is a refusal
     * here rather than a session torn down half way through an upload. */
    if (chain_path != NULL &&
        mmo_chain_read(chain_path, &chain, &chain_len) != 0) {
        free(report);
        return 1;
    }
    sha[0] = '\0';
    if (mmo_import_report_stamp(report, len, &play_seconds, sha, NULL) != 0) {
        free(report);
        free(chain);
        return 1;
    }
    printf("offering %s: %zu bytes, %d seconds played, save %s\n",
           report_path, len, play_seconds, sha[0] ? sha : "(unhashed)");
    if (chain != NULL)
        printf("with %zu bytes of session records from %s\n", chain_len,
               chain_path);

    c = openmmo_client_new();
    if (!c) {
        fprintf(stderr, "error: out of memory\n");
        free(report);
        free(chain);
        return 1;
    }
    openmmo_client_start(c, cfg);

    for (int f = 0; f < MAX_FRAMES; f++) {
        openmmo_client_pump(c);

        openmmo_event ev;
        while (openmmo_client_poll_event(c, &ev)) {
            switch (ev.kind) {
            case OPENMMO_EV_STATUS:
                printf("-> %s\n", openmmo_status_name(ev.status));
                break;
            case OPENMMO_EV_CHAT:
                print_chat_line(NULL, &ev);
                break;
            case OPENMMO_EV_JOINED:
                if (!sent) {
                    if (openmmo_client_send_offline_report(c, report, len) != 0) {
                        fprintf(stderr, "error: the save could not be sent\n");
                        goto done;
                    }
                    sent = 1;
                    printf("save offered; waiting for the answer\n");
                }
                break;
            case OPENMMO_EV_FAILED:
                fprintf(stderr, "error: %s\n", ev.message); goto done;
            case OPENMMO_EV_DISCONNECTED:
                fprintf(stderr, "session ended: %s\n", ev.message); goto done;
            default:
                break;
            }
        }
        if (sent) {
            const openmmo_import_answer *a = openmmo_client_import_answer(c);

            if (a != NULL && a->status != MMO_IMPORT_STATUS_NONE) {
                static const char *const WORD[MMO_IMPORT_STATUS_COUNT] = {
                    "landed", "try again", "refused",
                    "check queued", "check declined",
                    "copy kept", "copy declined", "records kept"
                };

                printf("%s: %s\n", WORD[a->status], a->message);
                for (int i = 0; i < a->nnotes; i++)
                    printf("  %s\n", a->notes[i]);
                if (a->nnotes_sent > a->nnotes)
                    printf("  (and %d more)\n", a->nnotes_sent - a->nnotes);
                /* The save's answer first, then the records behind it, on the
                 * same channel and with an answer of their own. Only when the
                 * save landed and the server said it would look. */
                if (a->status == MMO_IMPORT_STATUS_LANDED && !offered_chain &&
                    chain != NULL) {
                    if (!a->wants_chain) {
                        printf("the server is not checking offline play, so"
                               " the session records were not sent\n");
                        rc = 0;
                        goto done;
                    }
                    offered_chain = 1;
                    if (openmmo_client_send_offline_chain(c, chain,
                                                          chain_len) != 0) {
                        fprintf(stderr, "error: the session records could not"
                                " be sent\n");
                        goto done;
                    }
                    printf("session records offered; waiting for the answer\n");
                    continue;
                }
                rc = (a->status == MMO_IMPORT_STATUS_LANDED ||
                      a->status == MMO_IMPORT_STATUS_CHECK_QUEUED ||
                      a->status == MMO_IMPORT_STATUS_CHAIN_KEPT) ? 0 : 2;
                goto done;
            }
        }
        usleep(2000);
    }
    fprintf(stderr, "error: no answer to the save before the frame budget ran out\n");

done:
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    free(report);
    free(chain);
    return rc;
}

static int cmd_import_save(int argc, char **argv)
{
    openmmo_config cfg;
    const char *report = NULL;
    const char *chain = NULL;

    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--report") == 0 && i + 1 < argc)
            report = argv[++i];
        else if (strcmp(argv[i], "--chain") == 0 && i + 1 < argc)
            chain = argv[++i];
    }
    parse_opts(argc, argv, &cfg);
    if (report == NULL) {
        fprintf(stderr, "import-save: --report FILE names the save report to"
                " offer (the game writes one beside its save)\n");
        return 2;
    }
    return drive_import_save(&cfg, report, chain);
}

static int cmd_flags(int argc, char **argv)
{
    openmmo_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    parse_opts(argc, argv, &cfg);
    return drive_flags(&cfg);
}

/* --- script: a headless session driven by an external action script ---------- */

static const char *ev_kind_name(openmmo_event_kind k)
{
    switch (k) {
    case OPENMMO_EV_STATUS:         return "status";
    case OPENMMO_EV_JOINED:         return "joined";
    case OPENMMO_EV_FAILED:         return "failed";
    case OPENMMO_EV_DISCONNECTED:   return "disconnected";
    case OPENMMO_EV_ENTITY_SPAWN:   return "entity_spawn";
    case OPENMMO_EV_ENTITY_STEP:    return "entity_step";
    case OPENMMO_EV_ENTITY_TURN:    return "entity_turn";
    case OPENMMO_EV_ENTITY_DESPAWN: return "entity_despawn";
    case OPENMMO_EV_SELF_CORRECT:   return "self_correct";
    case OPENMMO_EV_WARP:           return "warp";
    case OPENMMO_EV_MAP:            return "map";
    case OPENMMO_EV_WEATHER:        return "weather";
    case OPENMMO_EV_ENCOUNTER:      return "encounter";
    case OPENMMO_EV_BATTLE_EVENT:   return "battle_event";
    case OPENMMO_EV_BATTLE_END:     return "battle_end";
    case OPENMMO_EV_STORAGE:        return "storage";
    case OPENMMO_EV_BAG:            return "bag";
    case OPENMMO_EV_MONEY:          return "money";
    case OPENMMO_EV_SHOP:           return "shop";
    case OPENMMO_EV_STORY_FLAG:     return "story_flag";
    case OPENMMO_EV_PARTY:          return "party";
    case OPENMMO_EV_MOVE_LEARN:     return "move_learn";
    case OPENMMO_EV_BREEDING_FORECAST: return "breeding_forecast";
    case OPENMMO_EV_EGG_INCUBATORS: return "egg_incubators";
    case OPENMMO_EV_EGG_HATCH:      return "egg_hatch";
    case OPENMMO_EV_EVOLUTION:      return "evolution";
    case OPENMMO_EV_CHARACTERS:     return "characters";
    case OPENMMO_EV_CHAT:           return "chat";
    case OPENMMO_EV_DIALOG:         return "dialog";
    case OPENMMO_EV_SCRIPT_MOVE:    return "script_move";
    case OPENMMO_EV_OBJECTIVE:      return "objective";
    case OPENMMO_EV_FRIENDS:        return "friends";
    case OPENMMO_EV_GUILD:          return "guild";
    case OPENMMO_EV_MAIL:           return "mail";
    case OPENMMO_EV_LINK:           return "link";
    case OPENMMO_EV_UI:             return "ui";
    case OPENMMO_EV_SYNC:           return "sync";
    case OPENMMO_EV_COMPETE:        return "compete";
    case OPENMMO_EV_GM:             return "gm";
    case OPENMMO_EV_TRADE:          return "trade";
    case OPENMMO_EV_GTL:            return "gtl";
    }
    return "?";
}

static const char *SDIR_NAME[4] = { "north", "south", "west", "east" };

/* Running state a script drives. The predicted tile is seeded from the server's
 * own placement on JOINED and re-anchored by every snap-back and warp, the same
 * way the warp/map commands track it, so a scripted `move` steps from server
 * truth rather than a guess. */
typedef struct {
    openmmo_client *c;
    int px, pz;              /* predicted local-player tile */
    int have_tile;           /* the self tile is known (post-JOINED) */
    int terminal;            /* a FAILED/DISCONNECTED event was seen */
    char msg[128];           /* its message */
    unsigned seen;           /* bitmask of event kinds drained since last reset */
    unsigned status_seen;    /* bitmask of coarse statuses reached this session */
} script_run;

/* Drain the event queue once, narrating each event and folding it into st: the
 * tile tracker, the terminal flag and the seen-kinds mask. */
static void script_drain(script_run *st)
{
    openmmo_event ev;
    while (openmmo_client_poll_event(st->c, &ev)) {
        st->seen |= (1u << ev.kind);
        switch (ev.kind) {
        case OPENMMO_EV_STATUS:
            st->status_seen |= (1u << ev.status);
            printf("   -> %s\n", openmmo_status_name(ev.status));
            break;
        case OPENMMO_EV_JOINED:
            if (openmmo_client_self_tile(st->c, &st->px, &st->pz) == 0) {
                st->have_tile = 1;
                printf("   joined at (%d, %d)\n", st->px, st->pz);
            } else {
                printf("   joined\n");
            }
            break;
        case OPENMMO_EV_CHARACTERS:
            printf("   created \"%s\" (%d character(s))\n",
                   ev.character.name, ev.character.count);
            break;
        case OPENMMO_EV_CHAT:
            printf("   ");
            print_chat_line(NULL, &ev);
            break;
        case OPENMMO_EV_FAILED:
            st->terminal = 1;
            snprintf(st->msg, sizeof st->msg, "%s", ev.message);
            printf("   failed: %s\n", ev.message);
            break;
        case OPENMMO_EV_DISCONNECTED:
            st->terminal = 1;
            snprintf(st->msg, sizeof st->msg, "%s", ev.message);
            printf("   disconnected: %s\n", ev.message);
            break;
        case OPENMMO_EV_ENTITY_SPAWN:
            printf("   entity spawn at (%d, %d) [slot %d, %s] named \"%s\"\n",
                   ev.entity.x, ev.entity.z, ev.entity.slot,
                   ev.entity.gender ? "female" : "male", ev.entity.name);
            break;
        case OPENMMO_EV_ENTITY_STEP:
            printf("   entity step to (%d, %d) [slot %d]\n",
                   ev.entity.x, ev.entity.z, ev.entity.slot);
            break;
        case OPENMMO_EV_ENTITY_TURN:
            printf("   entity turn (dir %d) [slot %d]\n", ev.entity.dir, ev.entity.slot);
            break;
        case OPENMMO_EV_ENTITY_DESPAWN:
            printf("   entity leave [slot %d]\n", ev.entity.slot);
            break;
        case OPENMMO_EV_SELF_CORRECT:
            st->px = ev.entity.x; st->pz = ev.entity.z; st->have_tile = 1;
            printf("   corrected to (%d, %d) facing %d\n",
                   ev.entity.x, ev.entity.z, ev.entity.dir);
            break;
        case OPENMMO_EV_WARP:
            st->px = ev.warp.x; st->pz = ev.warp.z; st->have_tile = 1;
            printf("   warp -> region %d bank %d map %d at (%d, %d) dir %d (%s)\n",
                   ev.warp.region, ev.warp.bank, ev.warp.map, ev.warp.x, ev.warp.z,
                   ev.warp.dir, ev.warp.seamless ? "seamless" : "warp");
            break;
        case OPENMMO_EV_MAP:
            printf("   map region %d bank %d map %d: %s, weather %d, lighting %d, type %d\n",
                   ev.map.region, ev.map.bank, ev.map.map, ev.map.is_nds ? "NDS" : "GBA",
                   ev.map.weather, ev.map.lighting, ev.map.map_type);
            break;
        case OPENMMO_EV_WEATHER:
            printf("   weather %s\n", ev.weather.source == 0 ? "mode" : "control");
            break;
        case OPENMMO_EV_ENCOUNTER:
            printf("   encounter: %s battle (background %d)\n",
                   ev.encounter.wild ? "wild" : "trainer", ev.encounter.background);
            break;
        case OPENMMO_EV_BATTLE_EVENT:
            printf("   battle event 0x%02x kind %d\n",
                   ev.battle.opcode, ev.battle.kind);
            break;
        case OPENMMO_EV_STORY_FLAG:
            printf("   story flag 0x%04x %s\n", ev.story.flag_id,
                   ev.story.enabled ? "set" : "cleared");
            break;
        case OPENMMO_EV_PARTY:
            printf("   party now %d of %d member(s)\n", ev.party.count,
                   ev.party.total);
            break;
        case OPENMMO_EV_BAG:
            printf("   bag now %d of %d stack(s)\n", ev.bag.count,
                   ev.bag.total);
            break;
        case OPENMMO_EV_MONEY:
            printf("   money now %d\n", ev.money.money);
            break;
        case OPENMMO_EV_SHOP:
            printf("   shop %s (%d of %d line(s))\n",
                   ev.shop.open ? "open" : "closed",
                   ev.shop.count, ev.shop.total);
            break;
        case OPENMMO_EV_DIALOG:
            printf("   dialog type %d text 0x%08x%s%s%s\n",
                   ev.dialog.action_type, (unsigned)ev.dialog.text_id,
                   ev.dialog.close ? " (close)" :
                   ev.dialog.resolved ? " (resolved)" : " (unresolved)",
                   ev.dialog.in_dialog ? " locked" : "",
                   ev.dialog.choice_count > 0 ? " menu" : "");
            break;
        case OPENMMO_EV_SCRIPT_MOVE:
            printf("   script move entity %lld %d byte(s) mapped %d%s\n",
                   (long long)ev.script_move.entity_id,
                   ev.script_move.count, ev.script_move.mapped,
                   ev.script_move.is_self ? " self" : "");
            break;
        case OPENMMO_EV_OBJECTIVE:
            printf("   objective %s count %d id %d value %d tally %d\n",
                   ev.objective.replace ? "replace" : "upsert",
                   ev.objective.count, (int)ev.objective.id,
                   ev.objective.value, (int)ev.objective.tally);
            break;
        case OPENMMO_EV_FRIENDS:
            printf("   friends %s count %d online %d%s%s%s%s\n",
                   ev.friends.replace ? "replace" : "update",
                   ev.friends.count, ev.friends.online,
                   ev.friends.added ? " added" : "",
                   ev.friends.removed ? " removed" : "",
                   ev.friends.name[0] ? " " : "",
                   ev.friends.name);
            break;
        case OPENMMO_EV_GUILD:
            printf("   guild %s [%s] members %d online %d%s%s%s%s\n",
                   ev.guild.name[0] ? ev.guild.name : (ev.guild.in_guild ? "?" : "none"),
                   ev.guild.tag,
                   ev.guild.member_count, ev.guild.online,
                   ev.guild.added ? " added" : "",
                   ev.guild.removed ? " removed" : "",
                   ev.guild.member[0] ? " " : "",
                   ev.guild.member);
            break;
        case OPENMMO_EV_MAIL:
            printf("   mail %s count %d inbox %d sent %d",
                   ev.mail.sent ? "sent" : "inbox",
                   ev.mail.count, ev.mail.inbox, ev.mail.outbox);
            if (ev.mail.result >= 0)
                printf(" result %d", ev.mail.result);
            if (ev.mail.is_detail)
                printf(" open %lld %s \"%s\"",
                       (long long)ev.mail.mail_id,
                       ev.mail.other, ev.mail.subject);
            printf("\n");
            break;
        case OPENMMO_EV_LINK:
            printf("   link %s count %d leader %lld%s%s%s%s\n",
                   ev.link.present ? "in" : "none",
                   ev.link.count, (long long)ev.link.leader,
                   ev.link.added ? " added" : "",
                   ev.link.removed ? " removed" : "",
                   ev.link.name[0] ? " " : "",
                   ev.link.name);
            break;
        case OPENMMO_EV_UI:
            printf("   ui opcode 0x%02x scale %d pages %d names %d "
                   "options %d rows %d%s%s\n",
                   ev.ui.opcode, ev.ui.scale, ev.ui.pages, ev.ui.names,
                   ev.ui.options, ev.ui.rows,
                   ev.ui.confirm ? " confirm" : "",
                   ev.ui.prompt ? " prompt" : "");
            break;
        case OPENMMO_EV_SYNC:
            printf("   sync opcode 0x%02x digest %d transfer %d stream %d "
                   "image %d plain %d\n",
                   ev.sync.opcode, ev.sync.digest, ev.sync.transfer_done,
                   ev.sync.stream_done, ev.sync.image_done, ev.sync.plain_len);
            break;
        }
    }
}

/* Advance the predicted tile one step in engine facing `dir` (N=0/S=1/W=2/E=3). */
static void script_step_tile(script_run *st, int dir)
{
    switch (dir) {
    case 0: st->pz--; break; /* north */
    case 1: st->pz++; break; /* south */
    case 2: st->px--; break; /* west  */
    case 3: st->px++; break; /* east  */
    }
}

static int drive_script(const openmmo_config *cfg, const openmmo_script *scr)
{
    openmmo_client *c = openmmo_client_new();
    if (!c) { fprintf(stderr, "error: out of memory\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* Per-await frame budget (~30s at the 2ms pacing below); the library's own
     * per-step deadlines fail a wedged session sooner. */
    const int AWAIT_FRAMES = 15000;

    script_run st;
    memset(&st, 0, sizeof st);
    st.c = c;

    int rc = 0;
    for (int i = 0; i < scr->count && rc == 0; i++) {
        const openmmo_script_action *a = &scr->act[i];

        switch (a->op) {
        case OPENMMO_SCRIPT_CONNECT:
            printf("[%d] connect %s\n", a->line, cfg->user);
            /* Fresh session: forget the previous one's progress and tile. */
            st.status_seen = 0; st.terminal = 0; st.have_tile = 0; st.msg[0] = '\0';
            openmmo_client_start(c, cfg);
            break;

        case OPENMMO_SCRIPT_DISCONNECT:
            printf("[%d] disconnect\n", a->line);
            openmmo_client_disconnect(c);
            break;

        case OPENMMO_SCRIPT_CHAT:
            printf("[%d] chat %s\n", a->line, a->text);
            if (openmmo_client_send_chat(c, a->text) != 0) {
                fprintf(stderr, "error: chat send failed (not in game?)\n");
                rc = 1;
            }
            break;

        case OPENMMO_SCRIPT_MOVE:
            printf("[%d] move %s from (%d, %d)\n", a->line, SDIR_NAME[a->arg & 3],
                   st.px, st.pz);
            if (!st.have_tile) {
                fprintf(stderr, "error: move before the player tile is known "
                        "(await in_game first)\n");
                rc = 1;
            } else if (openmmo_client_send_move(c, st.px, st.pz, a->arg, 0) != 0) {
                fprintf(stderr, "error: move send failed (not in game?)\n");
                rc = 1;
            } else {
                script_step_tile(&st, a->arg);
            }
            break;

        case OPENMMO_SCRIPT_FRAMES:
            printf("[%d] frames %d\n", a->line, a->arg);
            for (int f = 0; f < a->arg; f++) {
                openmmo_client_pump(c);
                st.seen = 0;
                script_drain(&st);
                if (st.terminal) { rc = 1; break; }
                usleep(2000);
            }
            break;

        case OPENMMO_SCRIPT_AWAIT_STATUS: {
            printf("[%d] await %s\n", a->line,
                   openmmo_status_name((openmmo_status)a->arg));
            int done = 0;
            for (int f = 0; f < AWAIT_FRAMES && !done && rc == 0; f++) {
                openmmo_client_pump(c);
                st.seen = 0;
                script_drain(&st);
                /* "reached at least this status": the FSM can pass through
                 * several statuses in one pump, so a transient like AUTHED is
                 * only reliably caught from the queued STATUS events. */
                if ((st.status_seen & (1u << a->arg)) ||
                    openmmo_client_status(c) == (openmmo_status)a->arg) {
                    done = 1;
                } else if (st.terminal) {
                    fprintf(stderr, "error: session ended before reaching %s: %s\n",
                            openmmo_status_name((openmmo_status)a->arg), st.msg);
                    rc = 1;
                }
                if (!done && rc == 0) usleep(2000);
            }
            if (!done && rc == 0) {
                fprintf(stderr, "error: timed out waiting for status %s\n",
                        openmmo_status_name((openmmo_status)a->arg));
                rc = 1;
            }
            break;
        }

        case OPENMMO_SCRIPT_AWAIT_EVENT: {
            printf("[%d] await-event %s\n", a->line,
                   ev_kind_name((openmmo_event_kind)a->arg));
            unsigned want = 1u << a->arg;
            int done = 0;
            for (int f = 0; f < AWAIT_FRAMES && !done && rc == 0; f++) {
                openmmo_client_pump(c);
                st.seen = 0;
                script_drain(&st);
                if (st.seen & want) {
                    done = 1;
                } else if (st.terminal) {
                    fprintf(stderr, "error: session ended before event %s: %s\n",
                            ev_kind_name((openmmo_event_kind)a->arg), st.msg);
                    rc = 1;
                }
                if (!done && rc == 0) usleep(2000);
            }
            if (!done && rc == 0) {
                fprintf(stderr, "error: timed out waiting for event %s\n",
                        ev_kind_name((openmmo_event_kind)a->arg));
                rc = 1;
            }
            break;
        }
        }
    }

    printf(rc == 0 ? "script: ok\n" : "script: FAILED\n");
    openmmo_client_disconnect(c);
    openmmo_client_free(c);
    return rc;
}

/* --- playthrough: the mainline gates, without a person --------------------- */

#define PLAY_VAR_RIVAL           0x40A5
#define PLAY_FLAG_GAME_COMPLETED 2404
#define PLAY_GATE_FRAMES         20000
#define PLAY_JOIN_SETTLE         80

static const char *const PLAY_GATES[] = {
    "rival",
    "starter",
    "wild",
    "badge-coal",
    "badge-forest",
    "badge-cobble",
    "badge-fen",
    "badge-relic",
    "badge-mine",
    "badge-icicle",
    "badge-beacon",
    "hm-surf",
    "hm-strength",
    "hm-waterfall",
    "hm-rock-climb",
    "league-aaron",
    "league-bertha",
    "league-flint",
    "league-lucian",
    "league-cynthia",
    "hall-of-fame",
};
#define PLAY_GATE_N ((int)(sizeof PLAY_GATES / sizeof PLAY_GATES[0]))

typedef struct {
    openmmo_client *c;
    int px, pz;
    int have_tile;
    int terminal;
    char msg[160];
    int saw_dialog;
    int saw_encounter;
    int saw_warp;
    int jumped;
    int party_n;
    int surfing;
    int game_done;
    s16 rival_var;
    int have_rival_var;
    char last_chat[160];
    int auto_reply; /* 1 = close every box; 0 = see it and leave it */
    int dialog_bank;
    int dialog_entry;
} play_run;

static void play_reset_seen(play_run *st)
{
    st->saw_dialog = 0;
    st->saw_encounter = 0;
    st->saw_warp = 0;
    st->jumped = 0;
    st->last_chat[0] = 0;
}

static u8 play_reply_for(const openmmo_dialog *box)
{
    if (!box)
        return 0;
    if (box->action_type == MMO_DIALOG_ACTION_YESNO)
        return 1;
    if (box->action_type == MMO_DIALOG_ACTION_MENU
        || box->action_type == MMO_DIALOG_ACTION_LIST)
        return 1;
    return 0;
}

static void play_drain(play_run *st)
{
    openmmo_event ev;
    const openmmo_dialog *box;
    const openmmo_party *party;
    const openmmo_story_store *story;
    s16 v;

    while (openmmo_client_poll_event(st->c, &ev)) {
        switch (ev.kind) {
        case OPENMMO_EV_STATUS:
            printf("   -> %s\n", openmmo_status_name(ev.status));
            break;
        case OPENMMO_EV_JOINED:
            if (openmmo_client_self_tile(st->c, &st->px, &st->pz) == 0)
                st->have_tile = 1;
            party = openmmo_client_party(st->c);
            st->party_n = (party && party->valid) ? party->count : 0;
            printf("   joined at (%d, %d) party %d\n", st->px, st->pz, st->party_n);
            break;
        case OPENMMO_EV_CHAT:
            snprintf(st->last_chat, sizeof st->last_chat, "%s", ev.chat.text);
            printf("   ");
            print_chat_line(NULL, &ev);
            if (strstr(ev.chat.text, "Jumped to")
                || strstr(ev.chat.text, "Reset to"))
                st->jumped = 1;
            break;
        case OPENMMO_EV_DIALOG:
            box = openmmo_client_dialog(st->c);
            if (box && box->awaiting) {
                st->saw_dialog = 1;
                st->dialog_bank = box->bank;
                st->dialog_entry = box->entry;
                print_dialog(box);
                if (st->auto_reply) {
                    if (openmmo_client_reply_dialog(st->c,
                                                    play_reply_for(box)) != 0)
                        printf("   warning: could not reply to the box\n");
                    else
                        printf("   dialog: replied %u\n",
                               (unsigned)play_reply_for(box));
                }
            }
            break;
        case OPENMMO_EV_WARP:
            st->px = ev.warp.x;
            st->pz = ev.warp.z;
            st->have_tile = 1;
            st->saw_warp = 1;
            printf("   warp -> region %d bank %d map %d at (%d, %d)\n",
                   ev.warp.region, ev.warp.bank, ev.warp.map,
                   ev.warp.x, ev.warp.z);
            break;
        case OPENMMO_EV_ENCOUNTER:
            st->saw_encounter = 1;
            printf("   encounter: %s battle\n",
                   ev.encounter.wild ? "wild" : "trainer");
            openmmo_client_battle_run(st->c);
            break;
        case OPENMMO_EV_BATTLE_END:
            printf("   battle over\n");
            break;
        case OPENMMO_EV_STORY_FLAG:
            printf("   story flag 0x%04x %s\n", ev.story.flag_id,
                   ev.story.enabled ? "set" : "cleared");
            if (ev.story.flag_id == PLAY_FLAG_GAME_COMPLETED && ev.story.enabled)
                st->game_done = 1;
            break;
        case OPENMMO_EV_PARTY:
            st->party_n = ev.party.count;
            printf("   party now %d of %d\n", ev.party.count, ev.party.total);
            break;
        case OPENMMO_EV_SELF_CORRECT:
            st->px = ev.entity.x;
            st->pz = ev.entity.z;
            st->have_tile = 1;
            break;
        case OPENMMO_EV_FAILED:
            st->terminal = 1;
            snprintf(st->msg, sizeof st->msg, "%s", ev.message);
            printf("   failed: %s\n", ev.message);
            break;
        case OPENMMO_EV_DISCONNECTED:
            st->terminal = 1;
            snprintf(st->msg, sizeof st->msg, "%s", ev.message);
            printf("   disconnected: %s\n", ev.message);
            break;
        default:
            break;
        }
    }

    if (openmmo_client_self_tile(st->c, &st->px, &st->pz) == 0)
        st->have_tile = 1;
    party = openmmo_client_party(st->c);
    if (party && party->valid)
        st->party_n = party->count;
    st->surfing = (openmmo_client_transportation(st->c) & MMO_TRANSPORT_SURFING) != 0;
    story = openmmo_client_story_store(st->c);
    if (story) {
        if (openmmo_story_var_get(story, PLAY_VAR_RIVAL, &v)) {
            st->rival_var = v;
            st->have_rival_var = 1;
        }
        st->game_done = openmmo_story_flag_is_set(story, PLAY_FLAG_GAME_COMPLETED);
    }
}

static int play_pump(play_run *st, int frames)
{
    int f;

    for (f = 0; f < frames; f++) {
        openmmo_client_pump(st->c);
        play_drain(st);
        if (st->terminal)
            return -1;
        usleep(2000);
    }
    return 0;
}

static int play_until(play_run *st, int frames, int (*ok)(const play_run *))
{
    int f;

    for (f = 0; f < frames; f++) {
        openmmo_client_pump(st->c);
        play_drain(st);
        if (st->terminal)
            return -1;
        if (ok(st))
            return 0;
        usleep(2000);
    }
    return 1;
}

static int play_in_game(const play_run *st)
{
    return openmmo_client_status(st->c) == OPENMMO_IN_GAME && st->have_tile;
}

static int play_has_jump(const play_run *st)
{
    return st->jumped || st->saw_warp;
}

static int play_has_dialog(const play_run *st)
{
    return st->saw_dialog;
}

static int play_has_party(const play_run *st)
{
    return st->party_n > 0;
}

static int play_has_encounter(const play_run *st)
{
    return st->saw_encounter;
}

static int play_has_surf(const play_run *st)
{
    return st->surfing;
}

static int play_has_rival(const play_run *st)
{
    return st->have_rival_var && st->rival_var == 1;
}

static int play_has_hof(const play_run *st)
{
    return st->game_done || (st->saw_dialog && st->dialog_bank == 191);
}

/* Close every box the parked script still owes, or /story will refuse.
 * A scene walks between boxes, so "not in dialog" is not finished, wait
 * until a stretch of frames produces neither a box nor a lock. */
static int play_quiesce(play_run *st, int quiet_frames)
{
    int quiet = 0;
    int f;

    st->auto_reply = 1;
    for (f = 0; f < PLAY_GATE_FRAMES && quiet < quiet_frames; f++) {
        int before = st->saw_dialog;
        if (play_pump(st, 1) != 0)
            return -1;
        if (openmmo_client_in_dialog(st->c) || st->saw_dialog != before)
            quiet = 0;
        else
            quiet++;
    }
    if (quiet < quiet_frames) {
        snprintf(st->msg, sizeof st->msg, "scene did not go quiet");
        return -1;
    }
    return 0;
}

static int play_finish_talk(play_run *st)
{
    return play_quiesce(st, 2000);
}

static int play_begin(play_run *st, const openmmo_config *cfg)
{
    memset(st, 0, sizeof *st);
    st->auto_reply = 1;
    st->c = openmmo_client_new();
    if (!st->c) {
        fprintf(stderr, "error: out of memory\n");
        return -1;
    }
    openmmo_client_start(st->c, cfg);
    if (play_until(st, PLAY_GATE_FRAMES, play_in_game) != 0) {
        snprintf(st->msg, sizeof st->msg, "%s",
                 st->terminal ? st->msg : "never reached the world");
        return -1;
    }
    play_pump(st, PLAY_JOIN_SETTLE);
    return 0;
}

static void play_end(play_run *st)
{
    if (st->c) {
        openmmo_client_disconnect(st->c);
        openmmo_client_free(st->c);
        st->c = NULL;
    }
}

static int play_story(play_run *st, const char *name)
{
    char line[80];

    if (play_finish_talk(st) != 0)
        return -1;
    play_reset_seen(st);
    snprintf(line, sizeof line, "/story %s", name);
    printf("   sending %s\n", line);
    if (openmmo_client_send_chat(st->c, line) != 0) {
        snprintf(st->msg, sizeof st->msg, "could not send %s", line);
        return -1;
    }
    if (play_until(st, PLAY_GATE_FRAMES, play_has_jump) != 0) {
        if (strstr(st->last_chat, "Finish what you are talking")) {
            if (play_finish_talk(st) != 0)
                return -1;
            play_reset_seen(st);
            printf("   retrying %s\n", line);
            if (openmmo_client_send_chat(st->c, line) != 0) {
                snprintf(st->msg, sizeof st->msg, "could not send %s", line);
                return -1;
            }
            if (play_until(st, PLAY_GATE_FRAMES, play_has_jump) == 0) {
                play_pump(st, PLAY_JOIN_SETTLE);
                return 0;
            }
        }
        if (st->last_chat[0])
            snprintf(st->msg, sizeof st->msg, "%s", st->last_chat);
        else
            snprintf(st->msg, sizeof st->msg, "%s did not land", line);
        return -1;
    }
    play_pump(st, PLAY_JOIN_SETTLE);
    return 0;
}

static int play_step(play_run *st, int dir)
{
    if (!st->have_tile) {
        snprintf(st->msg, sizeof st->msg, "no tile to step from");
        return -1;
    }
    if (openmmo_client_send_move(st->c, st->px, st->pz, dir, 0) != 0) {
        snprintf(st->msg, sizeof st->msg, "could not step");
        return -1;
    }
    play_pump(st, 40);
    return 0;
}

static int play_face_a(play_run *st, int dir)
{
    if (openmmo_client_send_face(st->c, dir) != 0) {
        snprintf(st->msg, sizeof st->msg, "could not turn");
        return -1;
    }
    play_pump(st, 20);
    play_reset_seen(st);
    if (openmmo_client_interact_tile(st->c) != 0) {
        snprintf(st->msg, sizeof st->msg, "could not press A");
        return -1;
    }
    return 0;
}

static int play_report(const char *gate, int ok, const char *why)
{
    if (ok) {
        printf("  ok   %s\n", gate);
        return 0;
    }
    printf("  FAIL %s (%s)\n", gate, why && why[0] ? why : "not reached");
    return 1;
}

static int play_talk_gate(play_run *st, const openmmo_config *cfg,
                          const char *story, const char *gate)
{
    int rc;

    if (play_begin(st, cfg) != 0)
        return play_report(gate, 0, st->msg);
    if (play_story(st, story) != 0) {
        rc = play_report(gate, 0, st->msg);
        play_end(st);
        return rc;
    }
    st->auto_reply = 0;
    if (play_face_a(st, 0) != 0) {
        rc = play_report(gate, 0, st->msg);
        play_end(st);
        return rc;
    }
    play_until(st, 4000, play_has_dialog);
    rc = play_report(gate, st->saw_dialog, "no dialog");
    play_end(st);
    return rc;
}

static int drive_playthrough(const openmmo_config *cfg)
{
    play_run st;
    const char *front = "(none)";
    int fail = 0;
    int i, x0, z0, stayed;
    static const char *const talk_story[] = {
        "gym-roark", "gym-gardenia", "gym-maylene", "gym-wake",
        "gym-fantina", "gym-byron", "gym-candice", "gym-volkner",
        "league-aaron", "league-bertha", "league-flint",
        "league-lucian", "league-cynthia",
    };
    static const char *const talk_gate[] = {
        "badge-coal", "badge-forest", "badge-cobble", "badge-fen",
        "badge-relic", "badge-mine", "badge-icicle", "badge-beacon",
        "league-aaron", "league-bertha", "league-flint",
        "league-lucian", "league-cynthia",
    };

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("the playthrough still works without a person:\n");

    /* rival: jump onto the bed so a spent trigger is armed again, then step. */
    if (play_begin(&st, cfg) != 0) {
        fail |= play_report("rival", 0, st.msg);
        goto done;
    }
    if (play_story(&st, "twinleaf-rival") != 0 && play_story(&st, "reset") != 0) {
        fail |= play_report("rival", 0, st.msg);
        play_end(&st);
        goto done;
    }
    play_reset_seen(&st);
    if (play_step(&st, 1) != 0) {
        fail |= play_report("rival", 0, st.msg);
        play_end(&st);
        goto done;
    }
    play_until(&st, PLAY_GATE_FRAMES, play_has_dialog);
    if (play_report("rival", st.saw_dialog, "no dialog") != 0)
        fail = 1;
    else
        front = "rival";
    play_finish_talk(&st);
    play_end(&st);
    if (fail)
        goto done;

    /* starter: jump south of the four-tile trigger and walk onto it. */
    if (play_begin(&st, cfg) != 0) {
        fail |= play_report("starter", 0, st.msg);
        goto done;
    }
    if (play_story(&st, "route-201-briefcase") != 0) {
        fail |= play_report("starter", 0, st.msg);
        play_end(&st);
        goto done;
    }
    printf("   briefcase: standing at (%d, %d), pressing A north\n", st.px, st.pz);
    play_reset_seen(&st);
    st.auto_reply = 1;
    if (play_face_a(&st, 0) != 0) {
        fail |= play_report("starter", 0, st.msg);
        play_end(&st);
        goto done;
    }
    play_until(&st, PLAY_GATE_FRAMES, play_has_party);
    if (play_report("starter", st.party_n > 0, "party stayed empty") != 0)
        fail = 1;
    else
        front = "starter";
    play_end(&st);
    if (fail)
        goto done;

    /* wild: a party on Route 201 grass. */
    if (play_begin(&st, cfg) != 0) {
        fail |= play_report("wild", 0, st.msg);
        goto done;
    }
    if (play_story(&st, "route-201-grass") != 0) {
        fail |= play_report("wild", 0, st.msg);
        play_end(&st);
        goto done;
    }
    play_reset_seen(&st);
    printf("   wild: standing at (%d, %d)\n", st.px, st.pz);
    {
        static const int cycle[4] = { 0, 3, 1, 2 };
        for (i = 0; i < 200 && !st.saw_encounter && !st.terminal; i++) {
            if (play_step(&st, cycle[i & 3]) != 0)
                break;
            if ((i % 25) == 24)
                printf("   wild: at (%d, %d) after %d steps\n", st.px, st.pz, i + 1);
        }
    }
    if (play_report("wild", st.saw_encounter, "grass did not roll") != 0)
        fail = 1;
    else
        front = "wild";
    play_end(&st);
    if (fail)
        goto done;

    for (i = 0; i < (int)(sizeof talk_gate / sizeof talk_gate[0]); i++) {
        if (play_talk_gate(&st, cfg, talk_story[i], talk_gate[i]) != 0) {
            fail = 1;
            goto done;
        }
        front = talk_gate[i];
    }

    /* Surf: A at the lake with the Fen Badge. */
    if (play_begin(&st, cfg) != 0) {
        fail |= play_report("hm-surf", 0, st.msg);
        goto done;
    }
    if (play_story(&st, "hm-surf") != 0) {
        fail |= play_report("hm-surf", 0, st.msg);
        play_end(&st);
        goto done;
    }
    if (play_face_a(&st, 1) != 0) {
        fail |= play_report("hm-surf", 0, st.msg);
        play_end(&st);
        goto done;
    }
    play_until(&st, 4000, play_has_surf);
    if (play_report("hm-surf", st.surfing, "never mounted") != 0)
        fail = 1;
    else
        front = "hm-surf";
    play_end(&st);
    if (fail)
        goto done;

    /* Strength: the boulder is a wall until the move is on. */
    if (play_begin(&st, cfg) != 0) {
        fail |= play_report("hm-strength", 0, st.msg);
        goto done;
    }
    if (play_story(&st, "hm-strength") != 0) {
        fail |= play_report("hm-strength", 0, st.msg);
        play_end(&st);
        goto done;
    }
    x0 = st.px;
    z0 = st.pz;
    if (play_step(&st, 3) != 0) {
        fail |= play_report("hm-strength", 0, st.msg);
        play_end(&st);
        goto done;
    }
    stayed = (st.px == x0 && st.pz == z0);
    if (!stayed) {
        fail |= play_report("hm-strength", 0, "walked through the boulder");
        play_end(&st);
        goto done;
    }
    if (play_face_a(&st, 3) != 0) {
        fail |= play_report("hm-strength", 0, st.msg);
        play_end(&st);
        goto done;
    }
    play_until(&st, 4000, play_has_dialog);
    if (play_report("hm-strength", stayed && st.saw_dialog,
                    "boulder did not answer") != 0)
        fail = 1;
    else
        front = "hm-strength";
    play_end(&st);
    if (fail)
        goto done;

    /* Waterfall and rock climb: the wall answers. */
    if (play_begin(&st, cfg) != 0) {
        fail |= play_report("hm-waterfall", 0, st.msg);
        goto done;
    }
    if (play_story(&st, "hm-waterfall") != 0) {
        fail |= play_report("hm-waterfall", 0, st.msg);
        play_end(&st);
        goto done;
    }
    if (play_face_a(&st, 0) != 0) {
        fail |= play_report("hm-waterfall", 0, st.msg);
        play_end(&st);
        goto done;
    }
    play_until(&st, 4000, play_has_dialog);
    if (play_report("hm-waterfall", st.saw_dialog, "falls did not answer") != 0)
        fail = 1;
    else
        front = "hm-waterfall";
    play_end(&st);
    if (fail)
        goto done;

    if (play_begin(&st, cfg) != 0) {
        fail |= play_report("hm-rock-climb", 0, st.msg);
        goto done;
    }
    if (play_story(&st, "hm-rock-climb") != 0) {
        fail |= play_report("hm-rock-climb", 0, st.msg);
        play_end(&st);
        goto done;
    }
    if (play_face_a(&st, 1) != 0) {
        fail |= play_report("hm-rock-climb", 0, st.msg);
        play_end(&st);
        goto done;
    }
    play_until(&st, 4000, play_has_dialog);
    if (play_report("hm-rock-climb", st.saw_dialog, "rock face did not answer") != 0)
        fail = 1;
    else
        front = "hm-rock-climb";
    play_end(&st);
    if (fail)
        goto done;

    if (play_begin(&st, cfg) != 0) {
        fail |= play_report("hall-of-fame", 0, st.msg);
        goto done;
    }
    if (play_story(&st, "hall-of-fame") != 0) {
        fail |= play_report("hall-of-fame", 0, st.msg);
        play_end(&st);
        goto done;
    }
    play_until(&st, PLAY_GATE_FRAMES, play_has_hof);
    if (play_report("hall-of-fame", play_has_hof(&st),
                    "Hall of Fame scene did not run") != 0)
        fail = 1;
    else
        front = "hall-of-fame";
    play_end(&st);

done:
    printf("front: %s\n", front);
    if (!fail) {
        printf("playthrough: all %d gates passed\n", PLAY_GATE_N);
        return 0;
    }
    printf("playthrough: FAILED\n");
    return 1;
}

static int cmd_playthrough(int argc, char **argv)
{
    openmmo_config cfg;

    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;
    cfg.select_name = "Sinnoh";
    cfg.select_how = OPENMMO_SELECT_NAME;
    parse_opts(argc, argv, &cfg);
    return drive_playthrough(&cfg);
}

static int cmd_script(int argc, char **argv)
{
    openmmo_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.user = "admin";
    cfg.pass = "admin";
    cfg.mode = OPENMMO_MODE_GAME_JOIN;

    const char *path = NULL;
    for (int i = 0; i < argc; i++) {
        int has_next = (i + 1 < argc);
        if (strcmp(argv[i], "--script") == 0 && has_next)
            path = argv[++i];
        else if (strcmp(argv[i], "--mode") == 0 && has_next) {
            const char *m = argv[++i];
            if (strcmp(m, "login") == 0) cfg.mode = OPENMMO_MODE_LOGIN_HOLD;
            else if (strcmp(m, "join") == 0) cfg.mode = OPENMMO_MODE_GAME_JOIN;
        }
    }
    parse_opts(argc, argv, &cfg); /* host/port/user/pass/gameport */

    if (!path) {
        fprintf(stderr, "error: script needs --script FILE (or '-' for stdin)\n");
        return usage("openmmo-client");
    }

    /* Slurp the whole script (a file, or stdin for '-'). */
    FILE *f = (strcmp(path, "-") == 0) ? stdin : fopen(path, "rb");
    if (!f) { fprintf(stderr, "error: cannot open script %s\n", path); return 1; }
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) { if (f != stdin) fclose(f); fprintf(stderr, "error: out of memory\n"); return 1; }
    for (;;) {
        if (len == cap) {
            char *nb = realloc(buf, cap * 2);
            if (!nb) { free(buf); if (f != stdin) fclose(f);
                       fprintf(stderr, "error: out of memory\n"); return 1; }
            buf = nb; cap *= 2;
        }
        size_t r = fread(buf + len, 1, cap - len, f);
        if (r == 0) break;
        len += r;
    }
    if (f != stdin) fclose(f);

    openmmo_script scr;
    char err[128];
    int prc = openmmo_script_parse(buf, len, &scr, err, sizeof err);
    free(buf);
    if (prc != 0) {
        fprintf(stderr, "error: %s: %s\n", path, err);
        return 1;
    }
    if (scr.count == 0) {
        fprintf(stderr, "error: %s has no directives\n", path);
        return 1;
    }

    return drive_script(&cfg, &scr);
}

/* --- record: capture a live login session as a replayable trace ------------ */

/* A fixed, valid P-256 scalar for the recorder's ephemeral key. Any nonzero
 * value below the curve order works; start_seeded fails loudly if this one does
 * not derive a public point. Recording is a developer tool, never production, 
 * a pinned key is a predictable key, which is the whole point here. */
static const u8 RECORD_EPH_PRIV[MMO_P256_SCALAR] = {
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01,
    0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x21,
};
#define RECORD_HELLO_RANDOM ((s64)0x0123456789abcdefLL)

/* Pump the connection until it is CONNECTED, or fail after a bounded wait. */
static int pump_until_connected(mmo_net *n)
{
    for (int i = 0; i < 2000; i++) { /* ~2s at 1ms */
        if (mmo_net_pump(n) != 0)
            return -1;
        if (n->state == MMO_NET_CONNECTED)
            return 0;
        usleep(1000);
    }
    return -1;
}

/* Read one whole frame with a short live budget; body/blen point into acc. */
static int record_read_frame(mmo_net *n, mmo_buf *acc,
                             const u8 **body, size_t *blen)
{
    mmo_read_result rr = mmo_net_read_frame(n, acc, body, blen, 3000, 1000);
    if (rr != MMO_READ_OK) {
        fprintf(stderr, "record: no frame (%s)\n",
                rr == MMO_READ_TIMEOUT ? "timeout" :
                rr == MMO_READ_DROPPED ? "link dropped" : "bad frame");
        return -1;
    }
    return 0;
}

static int cmd_record(int argc, char **argv)
{
    const char *user = "admin", *pass = "admin";
    const char *out = NULL;
    char host[OPENMMO_ENDPOINT_HOST_MAX];
    u16 port = openmmo_endpoint_login_port();
    for (int i = 0; i < argc; i++) {
        int has_next = (i + 1 < argc);
        if (strcmp(argv[i], "--user") == 0 && has_next) user = argv[++i];
        else if (strcmp(argv[i], "--pass") == 0 && has_next) pass = argv[++i];
        else if (strcmp(argv[i], "--out") == 0 && has_next) out = argv[++i];
    }
    if (!out) {
        fprintf(stderr, "record: --out FILE is required\n");
        return 2;
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);
    s64 timestamp = (s64)tv.tv_sec * 1000 + tv.tv_usec / 1000;

    mmo_trace tr;
    char label[64];
    /* The provenance line names the account and the port, not the address:
     * a capture is a file that gets handed around. */
    snprintf(label, sizeof label, "login %s@%u", user, (unsigned)port);

    mmo_net net;
    mmo_net_init(&net);
    openmmo_endpoint_host(host, sizeof host);
    if (mmo_net_connect(&net, host, port) != 0 || pump_until_connected(&net) != 0) {
        fprintf(stderr, "record: could not connect: %s\n", net.errmsg);
        openmmo_endpoint_forget(host, sizeof host);
        mmo_net_close(&net);
        return 1;
    }
    openmmo_endpoint_forget(host, sizeof host);

    int rc = 1;
    mmo_buf acc = {0};
    mmo_session s;
    mmo_wbuf hello;
    mmo_wbuf_init(&hello);

    if (mmo_session_start_seeded(&s, &hello, RECORD_EPH_PRIV,
                                 RECORD_HELLO_RANDOM, timestamp) != 0) {
        fprintf(stderr, "record: seeded start failed: %s\n", s.errmsg);
        goto cleanup;
    }
    /* checksum_size is not known until ServerHello; seed the trace after it. */

    /* ClientHello: send and tap. */
    if (mmo_net_send(&net, hello.data, hello.len) != 0) {
        fprintf(stderr, "record: send ClientHello failed\n");
        goto cleanup;
    }
    mmo_net_pump(&net);

    /* ServerHello: read, feed, capture. */
    const u8 *body;
    size_t blen;
    if (record_read_frame(&net, &acc, &body, &blen) != 0)
        goto cleanup;
    mmo_wbuf shello_wire;
    mmo_wbuf_init(&shello_wire);
    mmo_frame_put(&shello_wire, body, blen); /* reconstruct the framed bytes */

    mmo_wbuf ready;
    mmo_wbuf_init(&ready);
    int hs_ok = mmo_session_on_server_hello(&s, body, blen, &ready) == 0;
    if (!hs_ok)
        fprintf(stderr, "record: ServerHello rejected: %s\n", s.errmsg);
    else {
        /* Now the profile is known: seed the trace and record the handshake. */
        mmo_trace_init(&tr, RECORD_EPH_PRIV, RECORD_HELLO_RANDOM, timestamp,
                       s.crypto.checksum_size, label);
        mmo_trace_add(&tr, MMO_TRACE_C2S, MMO_TRACE_HS,
                      hello.data, hello.len, NULL, 0);
        mmo_trace_add(&tr, MMO_TRACE_S2C, MMO_TRACE_HS,
                      shello_wire.data, shello_wire.len, NULL, 0);
        mmo_trace_add(&tr, MMO_TRACE_C2S, MMO_TRACE_HS,
                      ready.data, ready.len, NULL, 0);

        /* ClientReady: send. */
        if (mmo_net_send(&net, ready.data, ready.len) != 0) {
            fprintf(stderr, "record: send ClientReady failed\n");
            hs_ok = 0;
        }
        mmo_net_pump(&net);
    }
    mmo_wbuf_free(&shello_wire);
    mmo_wbuf_free(&ready);
    if (!hs_ok)
        goto cleanup;

    /* LoginRequest: build the plaintext, encipher, send, tap both layers. */
    char pwhex[41];
    mmo_sha1_hex(pass, strlen(pass), pwhex);
    mmo_wbuf reqbody;
    mmo_wbuf_init(&reqbody);
    /* Not asking to be remembered: this is recording a trace, and a recording
     * that earns a token would write one over the player's own and put a
     * credentials packet into the tape that a replay cannot answer. */
    mmo_login_write_request(&reqbody, user, pwhex, 0);

    u8 reqclear[MMO_TRACE_MAX_FRAME];
    reqclear[0] = MMO_LOGIN_OP_REQUEST;
    size_t reqclearlen = 1 + reqbody.len;
    if (reqclearlen > sizeof reqclear) {
        fprintf(stderr, "record: LoginRequest too large to trace\n");
        mmo_wbuf_free(&reqbody);
        goto cleanup;
    }
    memcpy(reqclear + 1, reqbody.data, reqbody.len);

    mmo_wbuf reqframe;
    mmo_wbuf_init(&reqframe);
    mmo_session_send_app(&s.crypto, MMO_LOGIN_OP_REQUEST,
                         reqbody.data, reqbody.len, &reqframe);
    mmo_wbuf_free(&reqbody);
    int sent = !reqframe.err && mmo_net_send(&net, reqframe.data, reqframe.len) == 0;
    if (sent)
        mmo_trace_add(&tr, MMO_TRACE_C2S, MMO_TRACE_APP,
                      reqframe.data, reqframe.len, reqclear, reqclearlen);
    mmo_wbuf_free(&reqframe);
    if (!sent) {
        fprintf(stderr, "record: send LoginRequest failed\n");
        goto cleanup;
    }
    mmo_net_pump(&net);

    /* LoginResponse: read the ciphertext, decipher, tap both layers. */
    if (record_read_frame(&net, &acc, &body, &blen) != 0)
        goto cleanup;
    mmo_wbuf resp_wire;
    mmo_wbuf_init(&resp_wire);
    mmo_frame_put(&resp_wire, body, blen);
    u8 respclear[MMO_TRACE_MAX_FRAME];
    size_t rplen = mmo_session_recv_app(&s.crypto, body, blen,
                                        respclear, sizeof respclear);
    if (rplen == (size_t)-1) {
        fprintf(stderr, "record: LoginResponse failed to decipher\n");
        mmo_wbuf_free(&resp_wire);
        goto cleanup;
    }
    mmo_trace_add(&tr, MMO_TRACE_S2C, MMO_TRACE_APP,
                  resp_wire.data, resp_wire.len, respclear, rplen);
    mmo_wbuf_free(&resp_wire);

    int state = (rplen >= 2) ? respclear[1] : -1;
    printf("record: login reached response state %d\n", state);

    /* Pin the rendered state: what the client draws from each server frame, not
     * just the bytes. Replay checks these back. */
    mmo_trace_derive_renders(&tr);
    for (int i = 0; i < tr.nrenders; i++)
        printf("record: rendered %s\n", tr.renders[i]);

    /* Write the trace out. */
    mmo_wbuf text;
    mmo_wbuf_init(&text);
    if (mmo_trace_serialize(&tr, &text) != 0) {
        fprintf(stderr, "record: serialize failed\n");
        mmo_wbuf_free(&text);
        goto cleanup;
    }
    FILE *f = fopen(out, "wb");
    if (!f) {
        fprintf(stderr, "record: cannot open %s for writing\n", out);
        mmo_wbuf_free(&text);
        goto cleanup;
    }
    fwrite(text.data, 1, text.len, f);
    fclose(f);
    mmo_wbuf_free(&text);

    /* Self-check: the trace we just wrote replays byte-clean before we trust it. */
    char err[256];
    if (mmo_trace_replay(&tr, NULL, err, sizeof err) != 0) {
        fprintf(stderr, "record: WARNING the capture does not replay: %s\n", err);
        goto cleanup;
    }
    printf("record: wrote %d frames to %s (replays clean)\n", tr.nrecs, out);
    rc = 0;

cleanup:
    mmo_wbuf_free(&hello);
    mmo_net_read_reset(&acc);
    mmo_net_close(&net);
    return rc;
}

/*
 * replay a recorded session trace offline and report whether it still reproduces byte-for-byte
 * (and render-for-render).
 */
static int cmd_replay(int argc, char **argv)
{
    const char *path = NULL;
    const u8 *root = NULL; /* NULL = pinned production root */
    for (int i = 0; i < argc; i++) {
        int has_next = (i + 1 < argc);
        if (strcmp(argv[i], "--trace") == 0 && has_next) path = argv[++i];
        else if (strcmp(argv[i], "--root") == 0 && has_next) {
            const char *r = argv[++i];
            if (strcmp(r, "pinned") == 0) root = NULL;
            else if (strcmp(r, "mock") == 0) root = mmo_mock_root_pub;
            else { fprintf(stderr, "replay: --root must be pinned or mock\n"); return 2; }
        }
        else if (argv[i][0] != '-' && !path) path = argv[i];
    }
    if (!path) {
        fprintf(stderr, "replay: a trace file is required (--trace FILE)\n");
        return 2;
    }

    /* Held on the stack like the capture path in cmd_record; large but bounded. */
    static mmo_trace tr;
    char err[256];
    if (mmo_trace_parse_file(path, &tr, err, sizeof err) != 0) {
        fprintf(stderr, "replay: %s: %s\n", path, err);
        return 1;
    }
    if (mmo_trace_replay(&tr, root, err, sizeof err) != 0) {
        fprintf(stderr, "replay: %s does not reproduce: %s\n", path, err);
        return 1;
    }
    printf("replay: %s replays clean (%d frames, %d render pins)\n",
           path, tr.nrecs, tr.nrenders);
    return 0;
}

/* --- sprite: which picture a species has, and where this client runs out ------ */
static const char *sprite_archive_name(int archive)
{
    return archive == MMO_SPRITE_ARCHIVE_OTHERPOKE ? "otherpoke" : "pokegra";
}

static int sprite_table(void)
{
    int species, form, gender, shiny, f;
    static const int faces[2] = { MMO_SPRITE_FACE_BACK_ID, MMO_SPRITE_FACE_FRONT_ID };

    for (species = 0; species <= MMO_SPRITE_BAD_EGG_ID; species++) {
        int forms = mmo_sprite_form_count(species, NULL);

        for (form = 0; form < forms; form++)
            for (gender = 0; gender < 3; gender++)
                for (shiny = 0; shiny < 2; shiny++)
                    for (f = 0; f < 2; f++) {
                        mmo_sprite_ref ref;
                        const char *why = NULL;

                        if (mmo_sprite_locate(species, form, gender, shiny,
                                              faces[f], &ref, &why) != 0) {
                            fprintf(stderr, "sprite: %d/%d refused: %s\n",
                                    species, form, why ? why : "?");
                            return 1;
                        }
                        printf("sprite %d %d %d %d %d %s %d %d %d\n",
                               species, form, gender, shiny, faces[f],
                               sprite_archive_name(ref.archive), ref.character,
                               ref.palette, ref.spinda_spots);
                    }
    }
    return 0;
}

static int cmd_sprite(int argc, char **argv)
{
    int species = -1, form = 0, gender = MMO_SPRITE_GENDER_MALE_ID, shiny = 0;
    int face = MMO_SPRITE_FACE_FRONT_ID, i;
    mmo_sprite_ref ref;
    const char *why = NULL;

    /* Live archive size first: --table returns before the rest of the scan,
     * and an overlayed id is only drawable against the grown count. */
    for (i = 0; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--pokegra-members") == 0)
            mmo_sprite_set_pokegra_members(atoi(argv[++i]));
    }

    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--table") == 0)
            return sprite_table();
        if (i + 1 >= argc)
            break;
        if (strcmp(argv[i], "--species") == 0)
            species = atoi(argv[++i]);
        else if (strcmp(argv[i], "--form") == 0)
            form = atoi(argv[++i]);
        else if (strcmp(argv[i], "--gender") == 0)
            gender = atoi(argv[++i]);
        else if (strcmp(argv[i], "--shiny") == 0)
            shiny = atoi(argv[++i]);
        else if (strcmp(argv[i], "--pokegra-members") == 0)
            i++;
        else if (strcmp(argv[i], "--face") == 0) {
            i++;
            face = strcmp(argv[i], "back") == 0 ? MMO_SPRITE_FACE_BACK_ID
                                                : MMO_SPRITE_FACE_FRONT_ID;
        }
    }

    if (species < 0) {
        fprintf(stderr, "sprite: --species N (or --table) is required\n");
        return 2;
    }
    if (mmo_sprite_locate(species, form, gender, shiny, face, &ref, &why) != 0) {
        fprintf(stderr, "sprite: species %d form %d has no picture here: %s\n",
                species, form, why ? why : "?");
        return 1;
    }
    printf("species %d form %d of %d: %s character %d palette %d%s\n",
           species, form, mmo_sprite_form_count(species, NULL),
           sprite_archive_name(ref.archive), ref.character, ref.palette,
           ref.spinda_spots ? " (spots drawn from the personality value)" : "");
    return 0;
}

/* The region roster a create-a-character screen draws: every world in the order
 * a player is shown them, the one that can be chosen, and for each of the rest
 * the sentence that says what is missing. No server. */
static int cmd_regions(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    for (int i = 0; i < mmo_region_count(); i++) {
        const mmo_region *r = mmo_region_at(i);
        printf("%-8s id %-2d %s%s\n", r->name, r->id,
               r->selectable ? "playable" : (r->offered ? "unavailable" : "not offered"),
               r->drawable ? "" : " (no map)");
        if (r->reason)
            printf("           %s\n", r->reason);
    }
    printf("a new character is made in %s\n", mmo_region_name(mmo_region_default()));
    return 0;
}

/*
 * The cartridge registry: every image this client can be handed, what each one serves, and for
 * the rest the sentence a player is told.
 */
static int cmd_cartridges(int argc, char **argv)
{
    const MmoCartridge *c;
    char why[192];
    int i;

    if (argc > 0 && argv[0][0] != '-') {
        const char *code = argv[0];
        if (strlen(code) != 4) {
            fprintf(stderr, "cartridges: a game code is four characters\n");
            return 2;
        }
        c = mmo_cartridge_by_code(code);
        if (c)
            printf("%s %s %s %s\n", c->code, c->slot,
                   c->status == MMO_CART_READ ? "read" : "no-fill",
                   c->kinds[0] ? c->kinds : "-");
        if (mmo_cartridge_refusal(code, why, sizeof why) > 0) {
            printf("%s\n", why);
            return 1;
        }
        return 0;
    }

    for (i = 0; i < mmo_cartridge_count(); i++) {
        c = mmo_cartridge_at(i);
        printf("%-4s %-11s %-9s %s\n", c->code, c->slot,
               c->status == MMO_CART_READ ? "serves" : "no-fill",
               c->kinds[0] ? c->kinds : "-");
        if (mmo_cartridge_refusal(c->code, why, sizeof why) > 0)
            printf("               %s\n", why);
    }
    printf("only a slot this client has read may fill a package\n");
    return 0;
}

/* What a content package wants out of a cartridge, and what it costs when nobody has one. */
static int cmd_imports(int argc, char **argv)
{
    MmoImportPackage pkg;
    char why[256], have[256];
    const char *manifest = "roms/cartridges.found";
    int i, short_ = 0, read = 0;
    const char *dirs[8];
    int n = 0;

    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--found") == 0 && i + 1 < argc) {
            manifest = argv[++i];
            continue;
        }
        if (argv[i][0] != '-' && n < 8)
            dirs[n++] = argv[i];
    }
    if (n == 0)
        dirs[n++] = "mods/imports";

    for (i = 0; i < n; i++) {
        if (mmo_imports_read(dirs[i], &pkg) != 0) {
            fprintf(stderr, "imports: no port.recipe under %s\n", dirs[i]);
            continue;
        }
        read++;
        printf("%s: %d line(s)", dirs[i], pkg.lines);
        if (pkg.code[0])
            printf(" from %s", pkg.code);
        if (pkg.filled)
            printf(", filled by %s\n", pkg.filled_by);
        else
            printf(", unfilled\n");
        have[0] = '\0';
        mmo_imports_found(manifest, pkg.code, have, sizeof have);
        if (mmo_imports_shortfall(&pkg, have, why, sizeof why) > 0) {
            printf("    %s\n", why);
            if (!pkg.filled && pkg.lines)
                short_ = 1;
        }
    }
    if (read == 0)
        return 2;
    return short_ ? 1 : 0;
}

/* Print the drawable-body catalog: what a player may pick, and for each of
 * the rest the sentence that says why not. No server. */
static int cmd_appearances(int argc, char **argv)
{
    int all = 0;
    int i;

    if (argc > 0 && strcmp(argv[0], "--all") == 0)
        all = 1;
    for (i = 0; i < mmo_appearance_count(); i++) {
        const mmo_appearance *a = mmo_appearance_at(i);
        if (!all && !a->offered)
            continue;
        printf("%-28s gfx %-3d %s\n", a->name, a->gfx,
               a->offered ? "offered" : "not offered");
        if (a->reason)
            printf("           %s\n", a->reason);
    }
    printf("a new character with no --body is the gender's trainer model\n");
    return 0;
}

int main(int argc, char **argv)
{
    mmo_plat_crash_install("cli");

    if (argc < 2)
        return usage(argv[0]);

    if (strcmp(argv[1], "version") == 0) {
        printf("%s %s\n", MMO_CLIENT_NAME, MMO_CLIENT_VERSION);
        return 0;
    }
    if (strcmp(argv[1], "login") == 0)
        return cmd_login(argc - 2, argv + 2);
    if (strcmp(argv[1], "join") == 0)
        return cmd_join(argc - 2, argv + 2);
    if (strcmp(argv[1], "create") == 0)
        return cmd_create(argc - 2, argv + 2);
    if (strcmp(argv[1], "pair") == 0)
        return cmd_pair(argc - 2, argv + 2);
    if (strcmp(argv[1], "warp") == 0)
        return cmd_warp(argc - 2, argv + 2);
    if (strcmp(argv[1], "map") == 0)
        return cmd_map(argc - 2, argv + 2);
    if (strcmp(argv[1], "encounter") == 0)
        return cmd_encounter(argc - 2, argv + 2);
    if (strcmp(argv[1], "flags") == 0)
        return cmd_flags(argc - 2, argv + 2);
    if (strcmp(argv[1], "playthrough") == 0)
        return cmd_playthrough(argc - 2, argv + 2);
    if (strcmp(argv[1], "shop") == 0)
        return cmd_shop(argc - 2, argv + 2);
    if (strcmp(argv[1], "dialog") == 0)
        return cmd_dialog(argc - 2, argv + 2);
    if (strcmp(argv[1], "talk") == 0)
        return cmd_talk(argc - 2, argv + 2);
    if (strcmp(argv[1], "yesno") == 0)
        return cmd_yesno(argc - 2, argv + 2);
    if (strcmp(argv[1], "menu") == 0)
        return cmd_menu(argc - 2, argv + 2);
    if (strcmp(argv[1], "list") == 0)
        return cmd_list(argc - 2, argv + 2);
    if (strcmp(argv[1], "move") == 0)
        return cmd_move(argc - 2, argv + 2);
    if (strcmp(argv[1], "quest") == 0)
        return cmd_quest(argc - 2, argv + 2);
    if (strcmp(argv[1], "ui") == 0)
        return cmd_ui(argc - 2, argv + 2);
    if (strcmp(argv[1], "sync") == 0)
        return cmd_sync(argc - 2, argv + 2);
    if (strcmp(argv[1], "compete") == 0)
        return cmd_compete(argc - 2, argv + 2);
    if (strcmp(argv[1], "gm") == 0)
        return cmd_gm(argc - 2, argv + 2);
    if (strcmp(argv[1], "friends") == 0)
        return cmd_friends(argc - 2, argv + 2);
    if (strcmp(argv[1], "guild") == 0)
        return cmd_guild(argc - 2, argv + 2);
    if (strcmp(argv[1], "mail") == 0)
        return cmd_mail(argc - 2, argv + 2);
    if (strcmp(argv[1], "gtl") == 0)
        return cmd_gtl(argc - 2, argv + 2);
    if (strcmp(argv[1], "trade") == 0)
        return cmd_trade(argc - 2, argv + 2);
    if (strcmp(argv[1], "link") == 0)
        return cmd_link(argc - 2, argv + 2);
    if (strcmp(argv[1], "contest") == 0)
        return cmd_contest(argc - 2, argv + 2);
    if (strcmp(argv[1], "gift") == 0)
        return cmd_gift(argc - 2, argv + 2);
    if (strcmp(argv[1], "say") == 0)
        return cmd_say(argc - 2, argv + 2);
    if (strcmp(argv[1], "use") == 0)
        return cmd_use(argc - 2, argv + 2);
    if (strcmp(argv[1], "storage") == 0)
        return cmd_storage(argc - 2, argv + 2);
    if (strcmp(argv[1], "movelearn") == 0)
        return cmd_move_learn(argc - 2, argv + 2);
    if (strcmp(argv[1], "breed") == 0)
        return cmd_breed(argc - 2, argv + 2);
    if (strcmp(argv[1], "evolve") == 0)
        return cmd_evolve(argc - 2, argv + 2);
    if (strcmp(argv[1], "script") == 0)
        return cmd_script(argc - 2, argv + 2);
    if (strcmp(argv[1], "record") == 0)
        return cmd_record(argc - 2, argv + 2);
    if (strcmp(argv[1], "replay") == 0)
        return cmd_replay(argc - 2, argv + 2);
    if (strcmp(argv[1], "regions") == 0)
        return cmd_regions(argc - 2, argv + 2);
    if (strcmp(argv[1], "appearances") == 0)
        return cmd_appearances(argc - 2, argv + 2);
    if (strcmp(argv[1], "import-save") == 0)
        return cmd_import_save(argc - 2, argv + 2);
    if (strcmp(argv[1], "imports") == 0)
        return cmd_imports(argc - 2, argv + 2);
    if (strcmp(argv[1], "cartridges") == 0)
        return cmd_cartridges(argc - 2, argv + 2);
    if (strcmp(argv[1], "sprite") == 0)
        return cmd_sprite(argc - 2, argv + 2);
    if (strcmp(argv[1], "selftest") == 0)
        return openmmo_selftest_run(stdout) == 0 ? 0 : 1;

    fprintf(stderr, "%s: command '%s' is not implemented yet\n", argv[0], argv[1]);
    return usage(argv[0]);
}
