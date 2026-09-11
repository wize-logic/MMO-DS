/* The window's end of the hud page: attach, poll, the command
 * ring, chat formatting and the nameplate raster. The panel this file once
 * laid out and rastered is gone with the poketch mods, the UI layer
 * (view_ui.h) is where host screens live now. */

#include "view_hud.h"

#include <stdio.h>
#include <string.h>

#include "charcode.h"
#include "view_font.h"

void view_hud_init(struct view_hud *h)
{
    memset(h, 0, sizeof *h);
    h->page_mem.fd = -1;
}

void view_hud_attach(struct view_hud *h, const char *channel)
{
    char name[192];
    struct openmmo_hud_shm *p = NULL;

    if (h == NULL || h->page != NULL || channel == NULL)
        return;
    snprintf(name, sizeof name, "%s%s", channel, OPENMMO_HUD_SUFFIX);

    if (mmo_shm_attach(&h->page_mem, name, sizeof *p, 1) != 0)
        return;
    p = (struct openmmo_hud_shm *)h->page_mem.addr;

    if (p->magic != OPENMMO_HUD_MAGIC) {
        view_hud_close(h);
        return;
    }
    if (p->version != OPENMMO_HUD_VERSION) {
        fprintf(stderr,
                "openmmo-view: hud page '%s' is version %u, this window "
                "reads %u; the panel stays guest pixels\n",
                name, (unsigned)p->version, (unsigned)OPENMMO_HUD_VERSION);
        view_hud_close(h);
        return;
    }
    h->page = p;
}

void view_hud_close(struct view_hud *h)
{
    if (h == NULL)
        return;
    mmo_shm_close(&h->page_mem);
    h->page = NULL;
}

int view_hud_poll(struct view_hud *h)
{
    struct openmmo_hud_snap s;

    if (h == NULL || h->page == NULL)
        return 0;
    if (!openmmo_hud_read(h->page, &s))
        return 0;
    if (s.font_ready && !h->have_font) {
        memcpy(&h->font, (const void *)&h->page->font, sizeof h->font);
        h->have_font = 1;
    }
    if (h->any && memcmp(&h->snap, &s, sizeof s) == 0)
        return 0;
    h->any = 1;
    h->snap = s;
    if (h->dump)
        view_hud_dump(h, stderr);
    return 1;
}

void view_hud_push(struct view_hud *h, uint32_t kind, int32_t arg)
{
    if (h == NULL || h->page == NULL)
        return;
    openmmo_hud_push(h->page, kind, arg);
}

void view_hud_push_id(struct view_hud *h, uint32_t kind, int32_t arg,
                      uint32_t id_lo, uint32_t id_hi)
{
    if (h == NULL || h->page == NULL)
        return;
    openmmo_hud_push_id(h->page, kind, arg, id_lo, id_hi);
}

void view_hud_push_player(struct view_hud *h, int32_t verb, const char *name)
{
    if (h == NULL || h->page == NULL || name == NULL || name[0] == '\0')
        return;
    openmmo_hud_push_player(h->page, verb, name);
}

void view_hud_dump(const struct view_hud *h, FILE *out)
{
    const struct openmmo_hud_snap *s;

    if (h == NULL || !h->any || out == NULL)
        return;
    s = &h->snap;
    fprintf(out, "hud lower=%s chat=%u party=%u friends=%u font=%u",
            s->lower == OPENMMO_HUD_LOWER_POKETCH ? "poketch" : "guest",
            s->chat_n, s->party_n, s->friends_n, s->font_ready);
    if (s->composing)
        fprintf(out, " compose=\"%s\"", s->compose);
    fputc('\n', out);
    if (s->chat_n > 0) {
        unsigned i = s->chat_n - 1u;

        fprintf(out, "hud chat %s: %s\n", s->chat[i].sender, s->chat[i].text);
    }
}

/* ------------------------------------------------------------------ */
/* Raster                                                              */
/* ------------------------------------------------------------------ */

static uint32_t argb(unsigned a, unsigned r, unsigned g, unsigned b)
{
    return (a << 24) | (r << 16) | (g << 8) | b;
}

static void plot(uint32_t *px, int w, int h, int x, int y, uint32_t c)
{
    if (px == NULL || x < 0 || y < 0 || x >= w || y >= h)
        return;
    px[y * w + x] = c;
}

static unsigned glyph_px(const uint8_t *gfx, int x, int y)
{
    const uint8_t *tile;
    uint32_t row;

    if (gfx == NULL || x < 0 || y < 0 || x >= (int)OPENMMO_HUD_CELL ||
        y >= (int)OPENMMO_HUD_CELL)
        return 0;
    tile = gfx + (y / 8) * 0x40 + (x / 8) * 0x20;
    memcpy(&row, tile + (y % 8) * 4, 4);
    return (row >> ((x % 8) * 4)) & 0xFu;
}

static uint32_t rgb(uint32_t c)
{
    return c & 0x00FFFFFFu;
}

static void blit_glyph(uint32_t *px, int w, int h, const uint8_t *gfx,
                       int x, int y, int scale, uint32_t fg)
{
    int gx, gy, sx, sy;
    unsigned v, r, g, b;
    uint32_t sh;

    r = (fg >> 16) & 255u;
    g = (fg >> 8) & 255u;
    b = fg & 255u;
    sh = argb(255, r / 3, g / 3, b / 3);
    for (gy = 0; gy < (int)OPENMMO_HUD_CELL; gy++) {
        for (gx = 0; gx < (int)OPENMMO_HUD_CELL; gx++) {
            v = glyph_px(gfx, gx, gy);
            if (v == 0)
                continue;
            for (sy = 0; sy < scale; sy++)
                for (sx = 0; sx < scale; sx++)
                    plot(px, w, h, x + gx * scale + sx, y + gy * scale + sy,
                         v == 1 ? argb(255, r, g, b) : sh);
        }
    }
}

static int rom_draw(uint32_t *px, int w, int h, const struct openmmo_hud_font *font,
                    int x, int y, const char *s, int scale, uint32_t fg)
{
    mmo_charcode text[96];
    mmo_charcode_result r;
    int i, cx = x;
    unsigned cc, gi;

    if (font == NULL || s == NULL || font->glyph_n == 0) {
        int tw = openmmo_font_width(s, scale);
        int th = OPENMMO_FONT_H * scale;
        int ix, iy;

        openmmo_font_draw(px, w, h, x, y, s, scale, rgb(fg));
        for (iy = 0; iy < th; iy++) {
            for (ix = 0; ix < tw; ix++) {
                int p = (y + iy) * w + (x + ix);

                if (x + ix < 0 || y + iy < 0 || x + ix >= w || y + iy >= h)
                    continue;
                if (px[p] & 0x00FFFFFFu)
                    px[p] |= 0xFF000000u;
            }
        }
        return x + tw;
    }
    r = mmo_utf8_to_charcode(s, text, 96);
    for (i = 0; i < (int)r.written; i++) {
        cc = text[i];
        if (cc == 0 || cc > font->glyph_n)
            cc = 1;
        gi = cc - 1u;
        if (gi >= font->glyph_n)
            continue;
        blit_glyph(px, w, h, font->gfx[gi], cx, y, scale, fg);
        cx += (int)font->advance[gi] * scale;
    }
    return cx;
}

/* The width one string draws to, in the same steps rom_draw takes. */
static int rom_width(const struct openmmo_hud_font *font, const char *s,
                     int scale)
{
    mmo_charcode text[96];
    mmo_charcode_result r;
    int i, w = 0;
    unsigned cc, gi;

    if (s == NULL)
        return 0;
    if (font == NULL || font->glyph_n == 0)
        return openmmo_font_width(s, scale);
    r = mmo_utf8_to_charcode(s, text, 96);
    for (i = 0; i < (int)r.written; i++) {
        cc = text[i];
        if (cc == 0 || cc > font->glyph_n)
            cc = 1;
        gi = cc - 1u;
        if (gi < font->glyph_n)
            w += (int)font->advance[gi] * scale;
    }
    return w;
}

int view_hud_plate_size(const struct openmmo_hud_font *font, const char *name,
                        int scale, int *w, int *h)
{
    int tw;

    if (name == NULL || name[0] == '\0' || scale < 1)
        return 0;
    tw = rom_width(font, name, scale);
    if (tw <= 0)
        return 0;
    if (w != NULL)
        *w = tw + 4 * scale;
    if (h != NULL)
        *h = VIEW_HUD_PLATE_H * scale;
    return 1;
}

/* One nameplate, rastered the way the game used to paint it onto the guest
 * surface: a dark box with the ROM font's letters in white. Opaque ARGB, so a
 * window can copy it straight over the world. */
int view_hud_plate_raster(uint32_t *px, int cap_w, int cap_h,
                          const struct openmmo_hud_font *font,
                          const char *name, int scale, int *out_w, int *out_h)
{
    int bw = 0, bh = 0, x, y;

    if (px == NULL || !view_hud_plate_size(font, name, scale, &bw, &bh))
        return 0;
    if (bw > cap_w || bh > cap_h)
        return 0;
    for (y = 0; y < bh; y++) {
        for (x = 0; x < bw; x++)
            px[y * cap_w + x] = argb(255, 0x10, 0x10, 0x18);
    }
    rom_draw(px, cap_w, cap_h, (font != NULL && font->glyph_n != 0) ? font : NULL,
             2 * scale, scale, name, scale, 0x00F8F8F8u);
    if (out_w != NULL)
        *out_w = bw;
    if (out_h != NULL)
        *out_h = bh;
    return 1;
}

static const char *chat_tag(uint32_t type)
{
    switch (type) {
    case OPENMMO_HUD_CHAT_SHOUT:   return "SHOUT";
    case OPENMMO_HUD_CHAT_WHISPER: return "WHISPER";
    case OPENMMO_HUD_CHAT_TRADE:   return "TRADE";
    case OPENMMO_HUD_CHAT_GLOBAL:  return "GLOBAL";
    case OPENMMO_HUD_CHAT_CHANNEL: return "CHANNEL";
    case OPENMMO_HUD_CHAT_TEAM:    return "TEAM";
    case OPENMMO_HUD_CHAT_LINK:    return "LINK";
    case OPENMMO_HUD_CHAT_SYSTEM:  return "SYSTEM";
    case OPENMMO_HUD_CHAT_NOTICE:  return "NOTICE";
    case OPENMMO_HUD_CHAT_BATTLE:  return "BATTLE";
    default:                       return "WORLD";
    }
}

void view_hud_format_chat(const struct openmmo_hud_chat *c, char *dst, size_t cap)
{
    const char *tag;
    const char *who;
    const char *text;

    if (dst == NULL || cap == 0)
        return;
    dst[0] = '\0';
    if (c == NULL)
        return;
    tag = (c->type == OPENMMO_HUD_CHAT_NORMAL) ? NULL : chat_tag(c->type);
    who = c->sender;
    text = c->text;
    if (who[0] == '\0' || (who[0] == '-' && who[1] == '\0'))
        who = NULL;
    if (tag != NULL && who != NULL)
        snprintf(dst, cap, "%s  %s: %s", tag, who, text);
    else if (tag != NULL)
        snprintf(dst, cap, "%s  %s", tag, text);
    else if (who != NULL)
        snprintf(dst, cap, "%s: %s", who, text);
    else
        snprintf(dst, cap, "%s", text);
}
