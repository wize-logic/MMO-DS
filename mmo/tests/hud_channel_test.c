/* The page the host-drawn panel reads. */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "hud_channel.h"
#include "status_channel.h"
#include "view_hud.h"

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

#define OFF(f) offsetof(struct openmmo_hud_shm, f)

static void page_init(struct openmmo_hud_shm *h)
{
    memset(h, 0, sizeof *h);
    h->magic = OPENMMO_HUD_MAGIC;
    h->version = OPENMMO_HUD_VERSION;
}

static void test_chat_types_agree(void)
{
    CHECK((uint32_t)MMO_CHAT_NORMAL  == OPENMMO_HUD_CHAT_NORMAL &&
          (uint32_t)MMO_CHAT_SHOUT   == OPENMMO_HUD_CHAT_SHOUT &&
          (uint32_t)MMO_CHAT_WHISPER == OPENMMO_HUD_CHAT_WHISPER &&
          (uint32_t)MMO_CHAT_TRADE   == OPENMMO_HUD_CHAT_TRADE &&
          (uint32_t)MMO_CHAT_GLOBAL  == OPENMMO_HUD_CHAT_GLOBAL &&
          (uint32_t)MMO_CHAT_CHANNEL == OPENMMO_HUD_CHAT_CHANNEL &&
          (uint32_t)MMO_CHAT_TEAM    == OPENMMO_HUD_CHAT_TEAM &&
          (uint32_t)MMO_CHAT_LINK    == OPENMMO_HUD_CHAT_LINK &&
          (uint32_t)MMO_CHAT_SYSTEM  == OPENMMO_HUD_CHAT_SYSTEM &&
          (uint32_t)MMO_CHAT_NOTICE  == OPENMMO_HUD_CHAT_NOTICE &&
          (uint32_t)MMO_CHAT_BATTLE  == OPENMMO_HUD_CHAT_BATTLE,
          "the page's chat types are the wire's own, value for value");
}

static void test_layout(void)
{
    struct openmmo_hud_shm h;
    struct openmmo_hud_snap s;
    struct openmmo_hud_font f;

    CHECK(sizeof(struct openmmo_hud_chat) == 164u,
          "a chat row is type + 24-byte sender + 136-byte text");
    CHECK(sizeof(struct openmmo_hud_party) == 48u,
          "a party row is a 24-byte name and six words");
    CHECK(sizeof(struct openmmo_hud_person) == 28u,
          "a person row is a 24-byte name and the online bit");
    CHECK(sizeof(struct openmmo_hud_guild) == 1956u,
          "the guild block is name, tag, motd and 64 members");
    CHECK(sizeof(struct openmmo_hud_map) == 444u,
          "the map block is region, name, tile and 16 peers");
    CHECK(sizeof(struct openmmo_hud_net) == 200u,
          "the net block is state, latency, reason and battle");
    CHECK(sizeof(struct openmmo_hud_plate) == 32u,
          "a plate row is an anchor and a 24-byte name");
    CHECK(sizeof(struct openmmo_hud_objective) == 12u,
          "an objective row is the wire's id, value and count");
    CHECK(sizeof(struct openmmo_hud_mail_row) == 88u,
          "a mail row is a split id, a 24-byte name, a 48-byte subject, "
          "the date and the unread bit");
    CHECK(sizeof(struct openmmo_hud_mail) == 3052u,
          "the mail block is the counts, ten rows and the open letter");
    CHECK(sizeof(struct openmmo_hud_mail_send) == 2120u,
          "a compose is a name, a 48-byte subject and a 2048-byte body");
    CHECK(sizeof s == 23564u, "the snapshot is 23564 bytes, with no padding");
    CHECK(sizeof f == 65672u, "the atlas is 509 glyphs of 16x16 4bpp plus advances");
    CHECK(sizeof h == 93228u,
          "the page is header, snapshot, atlas, a 64-slot ring, its names "
          "and the GTL's and the mailbox's wide arguments");
    CHECK(OFF(magic) == 0 && OFF(version) == 4 && OFF(writer) == 8 &&
          OFF(seq) == 12 && OFF(snap) == 16,
          "header words sit at 0, 4, 8, 12 and the snapshot at 16");
    CHECK(OFF(font) == 23580u, "the atlas follows the snapshot");
    CHECK(OFF(cmd_head) == 89252u && OFF(cmd) == 89260u &&
              OFF(cmd_name) == 89516u && OFF(cmd_gtl) == 91052u &&
              OFF(cmd_mail) == 91108u,
          "the command ring follows the atlas, its names and the two sets "
          "of wide arguments behind it");
    CHECK((OPENMMO_HUD_CMD_SLOTS & (OPENMMO_HUD_CMD_SLOTS - 1u)) == 0,
          "the ring is a power of two");
    CHECK(sizeof h.cmd[0] == 4 && sizeof h.cmd_head == 4 && sizeof h.seq == 4,
          "every field is 32 bits, so both ABIs see one layout");
    CHECK(OPENMMO_HUD_LOWER_POKETCH != OPENMMO_HUD_LOWER_GUEST &&
          OPENMMO_HUD_APP_N == 6u,
          "lower has two values and there are six apps");
    /* The HUD bar's five engine-screen buttons ride one command kind. A
     * screen id that collided with another kind would open the bag from a
     * scroll, so both halves of the word are pinned here. */
    CHECK(OPENMMO_HUD_CMD_SCREEN == 5u && OPENMMO_HUD_SCREEN_N == 8u &&
          OPENMMO_HUD_SCREEN_BAG == 0 && OPENMMO_HUD_SCREEN_START == 5 &&
          OPENMMO_HUD_SCREEN_SUMMARY == 6 && OPENMMO_HUD_SCREEN_GTL == 7,
          "the screen command is kind 5 and names eight screens, one ours");
    /* The mailbox's verbs share a word with a row index, so a verb that
     * collided with another kind would delete a letter on a page ask. */
    CHECK(OPENMMO_HUD_CMD_MAIL == 11u && OPENMMO_HUD_CMD_MAIL !=
              OPENMMO_HUD_CMD_GTL &&
          OPENMMO_HUD_MAIL_ASK == 0 && OPENMMO_HUD_MAIL_READ == 1 &&
          OPENMMO_HUD_MAIL_DELETE == 2 && OPENMMO_HUD_MAIL_SEND == 3 &&
          OPENMMO_HUD_MAIL_PAPER == 4,
          "the mail command is kind 11 and carries five verbs");
    /* The official client's own pager is a Cp0(10, 8), so a page here is ten rows, the
     * same ten the server slices. */
    CHECK(OPENMMO_HUD_MAIL_ROWS == 10,
          "a mailbox page is the official client's ten rows");
}

static void test_publish_read(void)
{
    struct openmmo_hud_shm h;
    struct openmmo_hud_snap in, out;

    page_init(&h);
    memset(&in, 0, sizeof in);
    in.lower = OPENMMO_HUD_LOWER_POKETCH;
    in.app = OPENMMO_HUD_APP_CHAT;
    in.chat_n = 1;
    in.chat[0].type = OPENMMO_HUD_CHAT_NORMAL;
    snprintf(in.chat[0].sender, sizeof in.chat[0].sender, "Rowan");
    snprintf(in.chat[0].text, sizeof in.chat[0].text, "welcome to Sinnoh");
    in.net.state = OPENMMO_ST_IN_GAME;
    in.net.latency_ms = -1;
    in.composing = 1;
    snprintf(in.compose, sizeof in.compose, "hello");
    in.send_type = OPENMMO_HUD_CHAT_TRADE;

    openmmo_hud_publish(&h, &in);
    CHECK(openmmo_hud_read(&h, &out) &&
          out.lower == OPENMMO_HUD_LOWER_POKETCH &&
          out.app == OPENMMO_HUD_APP_CHAT &&
          out.chat_n == 1 &&
          strcmp(out.chat[0].sender, "Rowan") == 0 &&
          strcmp(out.chat[0].text, "welcome to Sinnoh") == 0 &&
          out.net.state == OPENMMO_ST_IN_GAME &&
          out.composing == 1 &&
          strcmp(out.compose, "hello") == 0 &&
          out.send_type == OPENMMO_HUD_CHAT_TRADE,
          "a published snapshot reads back as itself");
    CHECK((h.seq & 1u) == 0, "the lock is left even");

    h.seq |= 1u;
    memcpy((void *)out.chat[0].text, "half a sen", 10);
    CHECK(!openmmo_hud_read(&h, &out),
          "a read that lands inside a write returns nothing at all");

    h.seq &= ~1u;
    h.magic = 0;
    CHECK(!openmmo_hud_read(&h, &out), "a page with no magic is not read");
    h.magic = OPENMMO_HUD_MAGIC;
    h.version = OPENMMO_HUD_VERSION + 1u;
    CHECK(!openmmo_hud_read(&h, &out), "nor is one whose version is not ours");
}

static void test_ring(void)
{
    struct openmmo_hud_shm h;
    uint32_t out[OPENMMO_HUD_CMD_SLOTS + 8], tail = 0, dropped = 0;
    unsigned i, n;

    page_init(&h);
    n = openmmo_hud_read_cmds(&h, &tail, out, 8, &dropped);
    CHECK(n == 0 && dropped == 0, "an untouched ring reads as nothing asked");

    openmmo_hud_push(&h, OPENMMO_HUD_CMD_APP, (int32_t)OPENMMO_HUD_APP_PARTY);
    openmmo_hud_push(&h, OPENMMO_HUD_CMD_SCROLL, -3);
    openmmo_hud_push(&h, OPENMMO_HUD_CMD_COMPOSE, 1);
    openmmo_hud_push(&h, OPENMMO_HUD_CMD_PANEL, 0);
    n = openmmo_hud_read_cmds(&h, &tail, out, 8, &dropped);
    CHECK(n == 4 &&
          openmmo_hud_cmd_kind(out[0]) == OPENMMO_HUD_CMD_APP &&
          openmmo_hud_cmd_arg(out[0]) == (int32_t)OPENMMO_HUD_APP_PARTY &&
          openmmo_hud_cmd_arg(out[1]) == -3 &&
          openmmo_hud_cmd_kind(out[2]) == OPENMMO_HUD_CMD_COMPOSE &&
          openmmo_hud_cmd_arg(out[3]) == 0,
          "what was asked comes back in the order it was asked, signs included");
    n = openmmo_hud_read_cmds(&h, &tail, out, 8, &dropped);
    CHECK(n == 0, "a drained ring stays drained");

    for (i = 0; i < OPENMMO_HUD_CMD_SLOTS + 4u; i++)
        openmmo_hud_push(&h, OPENMMO_HUD_CMD_SCROLL, (int32_t)i);
    n = openmmo_hud_read_cmds(&h, &tail, out, OPENMMO_HUD_CMD_SLOTS + 8,
                              &dropped);
    CHECK(dropped == 4 && n == OPENMMO_HUD_CMD_SLOTS &&
          openmmo_hud_cmd_arg(out[0]) == 4,
          "falling a full ring behind skips the lost commands and says so");
}

static void test_player_cmd(void)
{
    struct openmmo_hud_shm h;
    uint32_t out[4] = { 0, 0, 0, 0 }, tail = 0;
    unsigned n;
    int32_t arg;

    page_init(&h);
    openmmo_hud_push_player(&h, OPENMMO_HUD_ACT_WHISPER, "Lucas");
    openmmo_hud_push_player(&h, OPENMMO_HUD_ACT_FRIEND, "Dawn");
    n = openmmo_hud_read_cmds(&h, &tail, out, 4, NULL);
    arg = openmmo_hud_cmd_arg(out[0]);
    CHECK(n == 2 &&
          openmmo_hud_cmd_kind(out[0]) == OPENMMO_HUD_CMD_PLAYER &&
          openmmo_hud_player_verb(arg) == OPENMMO_HUD_ACT_WHISPER &&
          strcmp(openmmo_hud_player_name(&h, arg), "Lucas") == 0,
          "a player command carries its verb and its name");
    arg = openmmo_hud_cmd_arg(out[1]);
    CHECK(openmmo_hud_player_verb(arg) == OPENMMO_HUD_ACT_FRIEND &&
          strcmp(openmmo_hud_player_name(&h, arg), "Dawn") == 0,
          "and two in a row keep their own names apart");
}

static void test_font_once(void)
{
    struct openmmo_hud_shm h;
    struct openmmo_hud_font f;
    struct openmmo_hud_snap s;

    page_init(&h);
    memset(&f, 0, sizeof f);
    f.glyph_n = OPENMMO_HUD_GLYPHS;
    f.cell = OPENMMO_HUD_CELL;
    f.gfx[0][0] = 0xA5;
    f.advance[0] = 7;
    openmmo_hud_publish_font(&h, &f);
    CHECK(h.font.gfx[0][0] == 0xA5 && h.font.advance[0] == 7,
          "the atlas is written outside the seqlock");

    memset(&s, 0, sizeof s);
    s.font_ready = 1;
    openmmo_hud_publish(&h, &s);
    CHECK(h.font.gfx[0][0] == 0xA5,
          "a snapshot publish does not clobber the atlas");
}

static void test_chat_format(void)
{
    struct openmmo_hud_chat c;
    char line[96];

    memset(&c, 0, sizeof c);
    c.type = OPENMMO_HUD_CHAT_NOTICE;
    snprintf(c.text, sizeof c.text, "Welcome to OpenMMO!");
    view_hud_format_chat(&c, line, sizeof line);
    CHECK(strstr(line, "NOTICE") != NULL && strstr(line, "Welcome to OpenMMO!") != NULL,
          "a notice is tagged NOTICE and keeps the words");
    CHECK(strchr(line, '[') == NULL && strchr(line, ']') == NULL,
          "and does not use brackets the ROM font cannot draw");

    memset(&c, 0, sizeof c);
    c.type = OPENMMO_HUD_CHAT_NORMAL;
    snprintf(c.sender, sizeof c.sender, "Rowan");
    snprintf(c.text, sizeof c.text, "hello");
    view_hud_format_chat(&c, line, sizeof line);
    CHECK(strcmp(line, "Rowan: hello") == 0,
          "a player line is sender: text, with no channel tag");
}

int hud_channel_tests_run(void)
{
    printf("hud channel\n");
    test_chat_types_agree();
    test_layout();
    test_publish_read();
    test_ring();
    test_player_cmd();
    test_font_once();
    test_chat_format();
    return failures;
}
