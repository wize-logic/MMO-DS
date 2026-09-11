/* The mailbox's two screens, and the verbs behind them. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "applications/mail.h"
#include "bg_window.h"
#include "constants/heap.h"
#include "constants/string.h"
#include "field/field_system.h"
#include "field_system.h"
#include "field_task.h"
#include "font.h"
#include "mail.h"
#include "overlay_manager.h"
#include "savedata.h"
#include "string_gf.h"
#include "text.h"

#include "../../../include/charcode.h"
#include "../../../include/endpoint.h"
#include "../../../include/client.h"
#include "../../../include/hud_channel.h"

typedef char openmmo_mail_charcode_width_check[
    sizeof(mmo_charcode) == sizeof(charcode_t) ? 1 : -1];

extern const ApplicationManagerTemplate gMailAppArgsTemplate;
extern const struct openmmo_hud_mail_send *openmmo_hud_mail_args(void);
/* The letter a row verb named, out of the ring slot it rode. */
extern s64 openmmo_hud_cmd_row_id(int32_t arg);

/* Defined below, and called by openmmo_mail_attach above it. Without this the
 * call is an implicit int(), which the definition then conflicts with, a
 * warning on gcc and an error on the NDK's clang, so the android build stops. */
void openmmo_mail_request(s64 mail_id);

#define MAIL_SENTENCE_PX (26 * 8)
#define MAIL_LINE_H      16
#define MAIL_LINES       2

static openmmo_client *s_client;
static MailAppArgs *s_args;
static int s_armed;
static int s_opened;
static int s_show;
static int s_pending;   /* the stationery was asked for; open when settled */
static s64 s_want_id;   /* which letter it was asked for; 0 = the first */
static s64 s_asked_id;
static char s_subject[MMO_TEXT_BYTES(MMO_MAIL_SUBJECT_MAX)];
static char s_body[MMO_TEXT_BYTES(MMO_MAIL_BODY_MAX)];

static int want_open(void)
{
    const char *env = openmmo_dev_env("OPENMMO_MAIL");

    if (env == NULL || env[0] == '\0' || env[0] == '0')
        return 0;
    return 1;
}

static void hold_text(const char *subject, const char *body)
{
    memset(s_subject, 0, sizeof s_subject);
    memset(s_body, 0, sizeof s_body);
    if (subject != NULL)
        strncpy(s_subject, subject, sizeof s_subject - 1);
    if (body != NULL)
        strncpy(s_body, body, sizeof s_body - 1);
    s_show = 1;
}

static void set_mail_name(Mail *mail, const char *name)
{
    mmo_charcode buf[TRAINER_NAME_LEN + 1];
    charcode_t *dst;

    if (mail == NULL)
        return;
    dst = Mail_GetTrainerName(mail);
    if (dst == NULL)
        return;
    mmo_utf8_to_charcode(name != NULL ? name : "", buf, TRAINER_NAME_LEN + 1);
    memcpy(dst, buf, sizeof buf);
}

static const mmo_mail *pick_letter(const openmmo_mail *box)
{
    int i;

    if (box == NULL || !box->valid)
        return NULL;
    if (s_want_id != 0) {
        if (box->have_detail && box->detail.mail_id == s_want_id)
            return &box->detail;
        for (i = 0; i < box->count; i++)
            if (box->entry[i].mail_id == s_want_id)
                return &box->entry[i];
        return NULL;
    }
    if (box->have_detail && box->detail.mail_id != 0)
        return &box->detail;
    if (box->count > 0)
        return &box->entry[0];
    return NULL;
}

static const char *letter_name(const openmmo_mail *box, const mmo_mail *letter)
{
    if (letter == NULL)
        return "";
    if (box != NULL && box->listed_sent) {
        if (letter->recipient[0] != '\0')
            return letter->recipient;
    } else if (letter->sender[0] != '\0') {
        return letter->sender;
    }
    if (letter->sender_id == 0)
        return "SYSTEM";
    return "";
}

static void paint_line(Window *window, enum HeapID heap,
                       const mmo_charcode *line, int y)
{
    String *str;

    if (window == NULL || line == NULL || line[0] == MMO_CHAR_EOS)
        return;
    str = String_Init(64, heap);
    String_CopyChars(str, (const charcode_t *)line);
    Text_AddPrinterWithParamsAndColor(window, FONT_MESSAGE, str, 0, (u32)y,
                                      TEXT_SPEED_INSTANT, TEXT_COLOR(1, 2, 0),
                                      NULL);
    String_Free(str);
}

/* Paints src into window, at most max_lines. Returns how many glyphs
 * were consumed. A leftover is the caller's to put on the next window. */
static int wrap_paint(Window *window, enum HeapID heap, const mmo_charcode *src,
                      int max_lines)
{
    mmo_charcode line[64];
    int i, take, lines, y, more;
    u32 width;

    if (window == NULL || src == NULL)
        return 0;
    i = 0;
    lines = 0;
    while (src[i] != MMO_CHAR_EOS && lines < max_lines) {
        take = 0;
        while (src[i + take] != MMO_CHAR_EOS && take < 62) {
            line[take] = src[i + take];
            line[take + 1] = MMO_CHAR_EOS;
            width = Font_CalcCharArrayWidth(FONT_MESSAGE,
                                            (const charcode_t *)line, 0);
            if (width > MAIL_SENTENCE_PX) {
                if (take == 0)
                    take = 1;
                break;
            }
            take++;
        }
        more = (src[i + take] != MMO_CHAR_EOS);
        if (more && lines + 1 == max_lines && take >= 3) {
            mmo_charcode dots[4];

            mmo_utf8_to_charcode("...", dots, 4);
            line[take - 3] = dots[0];
            line[take - 2] = dots[1];
            line[take - 1] = dots[2];
        }
        line[take] = MMO_CHAR_EOS;
        y = lines * MAIL_LINE_H;
        paint_line(window, heap, line, y);
        i += take;
        lines++;
        if (src[i] == MMO_CHAR_SPACE)
            i++;
    }
    Window_CopyToVRAM(window);
    return i;
}

int openmmo_mail_paint(Window *windows, int heap)
{
    mmo_charcode subject[MMO_MAIL_SUBJECT_MAX + 1];
    mmo_charcode body[MMO_MAIL_BODY_MAX + 1];
    enum HeapID hid = (enum HeapID)heap;
    int clipped = 0;

    if (!s_show || windows == NULL)
        return 0;

    mmo_utf8_to_charcode(s_subject, subject, MMO_MAIL_SUBJECT_MAX + 1);
    mmo_utf8_to_charcode(s_body, body, MMO_MAIL_BODY_MAX + 1);
    if (subject[0] != MMO_CHAR_EOS) {
        int used = wrap_paint(&windows[0], hid, subject, MAIL_LINES);

        clipped |= (subject[used] != MMO_CHAR_EOS);
    }
    if (body[0] != MMO_CHAR_EOS) {
        int used = wrap_paint(&windows[1], hid, body, MAIL_LINES);

        if (body[used] != MMO_CHAR_EOS) {
            int used2 = wrap_paint(&windows[2], hid, body + used, MAIL_LINES);

            clipped |= (body[used + used2] != MMO_CHAR_EOS);
        }
    }
    if (clipped)
        printf("openmmo: mail text clipped to the three sentence windows\n");
    return 1;
}

void openmmo_mail_attach(openmmo_client *c)
{
    s_client = c;
    s_args = NULL;
    s_opened = 0;
    s_show = 0;
    s_pending = 0;
    s_want_id = 0;
    s_asked_id = 0;
    hold_text(NULL, NULL);
    s_show = 0;
    if (want_open() && !s_armed) {
        s_armed = 1;
        openmmo_mail_request(0);
        printf("openmmo: mail viewer armed\n");
    }
}

/* Ask for the stationery screen. `mail_id` zero is "whatever is first",
 * which is what OPENMMO_MAIL=1 means on a headless boot. Asking again after
 * the screen has been put down opens it again. */
void openmmo_mail_request(s64 mail_id)
{
    s_want_id = mail_id;
    s_pending = 1;
    s_opened = 0;
    s_asked_id = 0;
}

void openmmo_mail_pump(FieldSystem *fs)
{
    if (s_args == NULL)
        return;
    if (fs != NULL && FieldSystem_HasChildProcess(fs))
        return;
    MailAppArgs_Free(s_args);
    s_args = NULL;
    s_show = 0;
    s_want_id = 0;
    printf("openmmo: mail viewer closed\n");
}

int openmmo_mail_try_open(FieldSystem *fs)
{
    SaveData *save;
    const openmmo_mail *box;
    const mmo_mail *letter;
    const char *name;
    const char *kind;
    int demo;

    if (fs == NULL)
        return 0;
    if (!s_pending)
        return 0;
    if (s_opened)
        return 0;
    if (fs->task != NULL || !FieldSystem_IsRunningFieldMap(fs)
        || FieldSystem_HasChildProcess(fs)) {
        s_pending = 1;
        return 0;
    }

    box = openmmo_client_mail(s_client);
    letter = pick_letter(box);
    if (box != NULL && box->valid && letter == NULL) {
        printf("openmmo: mail inbox empty, not opening the viewer\n");
        s_pending = 0;
        s_want_id = 0;
        s_opened = 1;
        return 0;
    }
    demo = (letter == NULL);

    if (!demo && letter->body[0] == '\0' && s_client != NULL
        && letter->mail_id != 0 && s_asked_id != letter->mail_id) {
        if (openmmo_client_mail_read(s_client, letter->mail_id) == 0) {
            s_asked_id = letter->mail_id;
            printf("openmmo: asking for mail %lld\n",
                   (long long)letter->mail_id);
            s_pending = 1;
            return 0;
        }
    }

    save = FieldSystem_GetSaveData(fs);
    if (save == NULL)
        return 0;

    s_args = MailAppArgs_New_Check(save, 0, HEAP_ID_FIELD2);
    if (s_args == NULL || s_args->mail == NULL) {
        printf("openmmo: mail viewer would not allocate\n");
        s_args = NULL;
        s_pending = 0;
        return 0;
    }

    if (demo) {
        name = "MAIL";
        hold_text("Hello",
                  "The letter is the server's. This is the screen the game already draws.");
        kind = "demo";
    } else {
        name = letter_name(box, letter);
        hold_text(letter->subject, letter->body);
        kind = box->listed_sent ? "sent" : "inbox";
    }
    set_mail_name(s_args->mail, name);

    FieldSystem_StartChildProcess(fs, &gMailAppArgsTemplate, s_args);
    s_opened = 1;
    s_pending = 0;
    if (demo) {
        printf("openmmo: mail viewer opened (%s)\n", kind);
    } else {
        printf("openmmo: mail viewer opened (%s id %lld)\n",
               kind, (long long)letter->mail_id);
    }
    return 1;
}

/* One command from the window's Mail frame (OPENMMO_HUD_CMD_MAIL). */
void openmmo_mail_window_cmd(unsigned arg)
{
    const struct openmmo_hud_mail_send *wide = openmmo_hud_mail_args();
    unsigned verb = arg & 0xFFu;
    int row = (int)((arg >> 8) & 0xFFu);
    s64 id = openmmo_hud_cmd_row_id((int32_t)arg);
    const openmmo_mail *box;

    if (s_client == NULL)
        return;
    box = openmmo_client_mail(s_client);
    switch (verb) {
    case OPENMMO_HUD_MAIL_ASK: {
        int sent = (int)((arg >> 8) & 0xFFu);
        int page = (int)((arg >> 16) & 0xFFu);

        printf("openmmo: mail window asks %s page %d\n",
               sent ? "sent" : "inbox", page);
        openmmo_client_mail_page(s_client, (s16)page, sent);
        return;
    }
    case OPENMMO_HUD_MAIL_READ:
        if (id == 0) {
            printf("openmmo: mail row %d named no letter\n", row);
            return;
        }
        printf("openmmo: mail window opens %lld\n", (long long)id);
        openmmo_client_mail_read(s_client, id);
        return;
    case OPENMMO_HUD_MAIL_DELETE:
        if (id == 0) {
            printf("openmmo: mail row %d named no letter\n", row);
            return;
        }
        printf("openmmo: mail window deletes %lld\n", (long long)id);
        openmmo_client_mail_delete(s_client, id, box->page);
        return;
    case OPENMMO_HUD_MAIL_SEND: {
        struct openmmo_hud_mail_send m;

        if (wide == NULL)
            return;
        memcpy(&m, wide, sizeof m);
        m.to[sizeof m.to - 1] = '\0';
        m.subject[sizeof m.subject - 1] = '\0';
        m.body[sizeof m.body - 1] = '\0';
        printf("openmmo: mail window sends to '%s'\n", m.to);
        openmmo_client_mail_send(s_client, m.to, m.subject, m.body);
        return;
    }
    case OPENMMO_HUD_MAIL_PAPER:
        if (id == 0) {
            printf("openmmo: mail row %d named no letter\n", row);
            return;
        }
        openmmo_mail_request(id);
        printf("openmmo: mail stationery asked for %lld\n", (long long)id);
        return;
    default:
        printf("openmmo: mail verb %u is not one of ours\n", verb);
        return;
    }
}
